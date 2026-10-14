#pragma once

// Original receiver-input PCM, shared across rolling acquisition banks.  This
// owner has no keyed-calendar knowledge: a candidate's null interval NEVER
// authorizes dropping global capture.  All coordinates are the ORIGINAL
// resampled input coordinates (bank input_sample_origin + bank local sample),
// not compressed source coordinates or native-device sample counters.
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <utility>

namespace datapump::modem::detail::scheduled_capture {

class Error: public std::runtime_error { public: using std::runtime_error::runtime_error; };
inline std::size_t checked_add(std::size_t a,std::size_t b) {
    if(b>std::numeric_limits<std::size_t>::max()-a)throw Error("shared capture size overflow");
    return a+b;
}
inline std::size_t checked_multiply(std::size_t a,std::size_t b) {
    if(a && b>std::numeric_limits<std::size_t>::max()/a)throw Error("shared capture size overflow");
    return a*b;
}
inline std::uint64_t coordinate_end(std::uint64_t first,std::uint64_t count) {
    if(count>std::numeric_limits<std::uint64_t>::max()-first)throw Error("shared capture coordinate overflow");
    return first+count;
}
inline std::uint64_t coordinate_product(std::uint64_t a,std::uint64_t b) {
    if(a && b>std::numeric_limits<std::uint64_t>::max()/a)throw Error("shared capture coordinate product overflow");
    return a*b;
}

struct Limits {
    std::size_t workspace_bytes=0,maximum_chunks=0,maximum_consumers=0;
    // 8192 at production sizes: small provider feeds fill the same slab.  The
    // committed prefix is immutable; only disjoint uncommitted locations change.
    std::size_t chunk_samples=8192;
};
struct Identity {
    std::uint64_t input_revision=0,consumer_generation=0;
    friend bool operator==(const Identity&,const Identity&)=default;
};
struct Event {
    // Evidence is retained for the timing-policy owner, not interpreted here.
    std::uint64_t evidence=0;
    bool timing_discontinuity=false,missing_capture=false;
};
struct FutureCertificate {
    Identity identity;
    std::uint64_t retained_from=0,admission_frontier=0;
    // Issuer is responsible for proving this floor covers ALL configured
    // keys/epochs/timing/clock cells and future rolling consumers.  A chosen
    // candidate, current bank or presently null hop is insufficient proof.
};
struct View {
    std::span<const float> pcm;
    std::uint64_t absolute_first=0;
    Identity identity;
    Event event_before; // Describes the original chunk boundary, below.
    std::uint64_t event_coordinate=0;
};
struct Accounting {
    std::size_t fixed_bytes=0,payload_bytes=0,reserved_bytes=0,chunks=0;
    std::uint64_t retained_first=0,captured_end=0;
};

class SharedScheduledCapture {
    struct Chunk {
        std::unique_ptr<float[]> data;
        std::uint64_t sequence=0,first=0,end=0,grid_first=0;
        std::size_t capacity=0,bytes=0,readers=0;
        Event event;
    };
    struct Claim { std::uint64_t token=0,floor=0; bool active=false,future=false; };
    struct State {
        mutable std::mutex mutex;
        Limits limits;
        Identity identity;
        std::unique_ptr<Chunk[]> chunks;
        std::unique_ptr<Claim[]> claims;
        std::uint64_t origin=0,end=0,oldest_sequence=1,next_sequence=1,next_token=1;
        std::size_t chunk_count=0,fixed_bytes=0,payload_bytes=0,lease_count=0,claim_count=0;

        State(Limits l,Identity id,std::uint64_t first):limits(l),identity(id),origin(first),end(first) {
            if(!l.maximum_chunks || !l.maximum_consumers || !l.chunk_samples ||
               l.chunk_samples>std::numeric_limits<std::uint64_t>::max())throw Error("invalid shared capture limits");
            fixed_bytes=checked_add(sizeof(State),256); // state/control allocation allowance
            fixed_bytes=checked_add(fixed_bytes,checked_multiply(l.maximum_chunks,sizeof(Chunk)));
            fixed_bytes=checked_add(fixed_bytes,checked_multiply(l.maximum_consumers,sizeof(Claim)));
            if(fixed_bytes>l.workspace_bytes)throw Error("shared capture metadata exceeds workspace");
            chunks=std::make_unique<Chunk[]>(l.maximum_chunks);
            claims=std::make_unique<Claim[]>(l.maximum_consumers);
        }
        Chunk& at(std::uint64_t sequence) { return chunks[sequence%limits.maximum_chunks]; }
        const Chunk& at(std::uint64_t sequence)const { return chunks[sequence%limits.maximum_chunks]; }
        std::uint64_t retained_first()const { return chunk_count?at(oldest_sequence).first:end; }
        std::uint64_t floor()const {
            bool future=false;
            std::uint64_t result=end;
            for(std::size_t i=0;i<limits.maximum_consumers;++i)if(claims[i].active) {
                result=std::min(result,claims[i].floor);future|=claims[i].future;
            }
            // There is no implicit future-consumer omission certificate.
            return future?result:origin;
        }
        void reclaim() {
            const auto protected_from=floor();
            while(chunk_count) {
                auto& c=at(oldest_sequence);
                if(c.end>protected_from || c.readers)break;
                payload_bytes-=c.bytes;c=Chunk{};--chunk_count;++oldest_sequence;
            }
        }
        std::size_t claim_slot(std::uint64_t token)const {
            for(std::size_t i=0;i<limits.maximum_consumers;++i)
                if(claims[i].active && claims[i].token==token)return i;
            throw Error("shared capture consumer retired");
        }
    };
    std::shared_ptr<State> state_;
    static std::shared_ptr<State> create(Limits limits,Identity identity,std::uint64_t origin) {
        // Do not allocate even the state/control block before validating the
        // complete fixed reservation.  Payload is checked separately at append.
        if(!limits.maximum_chunks || !limits.maximum_consumers || !limits.chunk_samples)
            throw Error("invalid shared capture limits");
        if(required_fixed_bytes(limits)>limits.workspace_bytes)throw Error("shared capture metadata exceeds workspace");
        return std::make_shared<State>(limits,identity,origin);
    }
    static std::uint64_t retained_sample_capacity(Limits limits,std::size_t fixed_bytes) {
        // Inputs come from validated layout or an existing owner. Preserve the
        // owner's original saturating span calculation independently of the
        // separate maximum payload reservation overflow check.
        const auto charge=checked_add(checked_multiply(limits.chunk_samples,sizeof(float)),128);
        const auto slots=std::min(limits.maximum_chunks,(limits.workspace_bytes-fixed_bytes)/charge);
        if(slots<=3)return 0;
        const auto usable=slots-3;
        if(usable>std::numeric_limits<std::uint64_t>::max()/limits.chunk_samples)
            return std::numeric_limits<std::uint64_t>::max();
        return static_cast<std::uint64_t>(usable)*limits.chunk_samples;
    }
public:
    static std::size_t required_fixed_bytes(Limits limits) {
        return checked_add(checked_add(checked_add(sizeof(State),256),
            checked_multiply(limits.maximum_chunks,sizeof(Chunk))),
            checked_multiply(limits.maximum_consumers,sizeof(Claim)));
    }
    static std::size_t maximum_reserved_bytes(Limits limits) {
        return checked_add(required_fixed_bytes(limits),checked_multiply(limits.maximum_chunks,
            checked_add(checked_multiply(limits.chunk_samples,sizeof(float)),128)));
    }
    // Pure capacity description. No owner/state/control/payload allocation,
    // sample availability, subscriber registration or reclamation authority.
    // Existing instance layout is unchanged; this nested type is metadata only.
    struct Layout {
        Limits limits;
        std::size_t fixed_bytes=0,slab_bytes=0,maximum_bytes=0,reserved_bytes=0;
        std::uint64_t guaranteed_samples=0;
    };
    static Layout layout(Limits limits) {
        if(!limits.maximum_chunks || !limits.maximum_consumers || !limits.chunk_samples ||
           limits.chunk_samples>std::numeric_limits<std::uint64_t>::max())
            throw Error("invalid shared capture limits");
        Layout out;out.limits=limits;out.fixed_bytes=required_fixed_bytes(limits);
        if(out.fixed_bytes>limits.workspace_bytes)throw Error("shared capture metadata exceeds workspace");
        out.slab_bytes=checked_add(checked_multiply(limits.chunk_samples,sizeof(float)),128);
        out.maximum_bytes=maximum_reserved_bytes(limits);
        out.reserved_bytes=std::min(limits.workspace_bytes,out.maximum_bytes);
        out.guaranteed_samples=retained_sample_capacity(limits,out.fixed_bytes);
        return out;
    }
    // Same owner capacity policy used by Live and the planning-only projection.
    // Consumer count remains an explicit caller-owned hard allocation bound;
    // it is not a proof of how many future banks will retire or be admitted.
    static Layout cohort_layout(std::size_t cohort_workspace,std::size_t maximum_consumers) {
        const auto allowance=std::min<std::size_t>(cohort_workspace/4,64U*1024U*1024U);
        Limits limits{allowance,std::max<std::size_t>(1,allowance/(8192*sizeof(float)+256)),maximum_consumers,8192};
        while(limits.maximum_chunks>1 && maximum_reserved_bytes(limits)>allowance)--limits.maximum_chunks;
        if(maximum_reserved_bytes(limits)>allowance)throw Error("shared capture mandatory metadata exceeds DSP workspace");
        return layout(limits);
    }
    // Charge this bound ONCE to the cohort before subdividing the remainder
    // among banks. accounting().reserved_bytes is current allocated usage,
    // not a guarantee that later capture fits remaining workspace. Capacity
    // alone does not prove current availability/identity or authorize eviction.
    // Three slabs reserve alignment plus two transactional append allocations.
    // Event fragmentation may still exhaust chunks: no arbitrary-discontinuity
    // guarantee is inferred from this ordinary coalesced-ingress span.
    std::uint64_t guaranteed_retained_samples()const {
        std::lock_guard lock(state_->mutex);return retained_sample_capacity(state_->limits,state_->fixed_bytes);
    }
    std::size_t capacity_reservation()const {
        return std::min(state_->limits.workspace_bytes,maximum_reserved_bytes(state_->limits));
    }
    class RangeLease {
        friend class SharedScheduledCapture;
        std::shared_ptr<State> state_;
        std::uint64_t first_=0,end_=0,first_sequence_=0,last_sequence_=0;
        RangeLease(std::shared_ptr<State> state,std::uint64_t first,std::uint64_t end,
                   std::uint64_t first_sequence,std::uint64_t last_sequence):
            state_(std::move(state)),first_(first),end_(end),first_sequence_(first_sequence),last_sequence_(last_sequence) {}
        void release()noexcept {
            if(!state_)return;
            auto state=std::move(state_);
            std::lock_guard lock(state->mutex);
            if(first_!=end_)for(auto sequence=first_sequence_;;++sequence) {
                auto& c=state->at(sequence);
                if(c.sequence!=sequence || !c.readers)std::terminate();
                --c.readers;if(sequence==last_sequence_)break;
            }
            if(!state->lease_count)std::terminate();
            --state->lease_count;state->reclaim();
        }
    public:
        RangeLease()=default;
        RangeLease(const RangeLease&)=delete;
        RangeLease& operator=(const RangeLease&)=delete;
        RangeLease(RangeLease&& other)noexcept { *this=std::move(other); }
        RangeLease& operator=(RangeLease&& other)noexcept {
            if(this!=&other) {release();state_=std::move(other.state_);first_=other.first_;end_=other.end_;
                first_sequence_=other.first_sequence_;last_sequence_=other.last_sequence_;}
            return *this;
        }
        ~RangeLease(){release();}
        explicit operator bool()const noexcept{return bool(state_);}
        std::uint64_t first()const noexcept{return first_;}
        std::uint64_t end()const noexcept{return end_;}
        // Like a container's iterators, this particular lease object must not
        // be moved/released concurrently with visit or by its callback. Other
        // leases and owner append/retirement may run concurrently. Cancellation
        // joins its reader before retiring that reader's lease.
        template<class Callback>void visit(Callback&& callback)const {
            if(!state_)throw Error("empty shared capture lease");
            if(first_==end_)return;
            for(auto sequence=first_sequence_;;++sequence) {
                View view;
                {
                    std::lock_guard lock(state_->mutex);
                    const auto& c=state_->at(sequence);
                    if(c.sequence!=sequence || !c.readers)throw Error("shared capture lease identity lost");
                    const auto first=std::max(first_,c.first),end=std::min(end_,c.end);
                    view={std::span<const float>(c.data.get()+std::size_t(first-c.grid_first),std::size_t(end-first)),
                          first,state_->identity,c.event,c.first};
                }
                // The lease pins data; callback runs without the registry mutex.
                // A concurrent append writes ONLY beyond the committed prefix.
                callback(view);
                if(sequence==last_sequence_)break;
            }
        }
    };
    class Retention {
        friend class SharedScheduledCapture;
        std::shared_ptr<State> state_;
        std::uint64_t token_=0;
        bool future_=false;
        Retention(std::shared_ptr<State> state,std::uint64_t token,bool future):
            state_(std::move(state)),token_(token),future_(future){}
        void release()noexcept {
            if(!state_)return;
            auto state=std::move(state_);std::lock_guard lock(state->mutex);
            for(std::size_t i=0;i<state->limits.maximum_consumers;++i)
                if(state->claims[i].active && state->claims[i].token==token_) {
                    state->claims[i]=Claim{};--state->claim_count;state->reclaim();return;
                }
            std::terminate();
        }
        void advance(std::uint64_t floor) {
            const auto slot=state_->claim_slot(token_);auto& c=state_->claims[slot];
            if(floor<c.floor || floor>state_->end)throw Error("invalid shared capture retention advance");
            c.floor=floor;state_->reclaim();
        }
    public:
        Retention()=default;
        Retention(const Retention&)=delete;
        Retention& operator=(const Retention&)=delete;
        Retention(Retention&& other)noexcept{*this=std::move(other);}
        Retention& operator=(Retention&& other)noexcept {
            if(this!=&other){release();state_=std::move(other.state_);token_=other.token_;future_=other.future_;}
            return *this;
        }
        ~Retention(){release();}
        explicit operator bool()const noexcept{return bool(state_);}
        void retain_from(std::uint64_t first) {
            if(!state_ || future_)throw Error("future capture retention needs certificate");
            std::lock_guard lock(state_->mutex);advance(first);
        }
        void certify(const FutureCertificate& certificate) {
            if(!state_ || !future_)throw Error("not a future capture retention");
            std::lock_guard lock(state_->mutex);
            if(certificate.identity!=state_->identity || certificate.retained_from>certificate.admission_frontier ||
               certificate.admission_frontier>state_->end)throw Error("invalid future capture certificate");
            advance(certificate.retained_from);
        }
    };

    SharedScheduledCapture(Limits limits,Identity identity,std::uint64_t origin=0):
        state_(create(limits,identity,origin)){}
    SharedScheduledCapture(const SharedScheduledCapture&)=delete;
    SharedScheduledCapture& operator=(const SharedScheduledCapture&)=delete;
    SharedScheduledCapture(SharedScheduledCapture&&)=default;
    SharedScheduledCapture& operator=(SharedScheduledCapture&&)=default;

    Retention subscribe(std::uint64_t first,bool future=false) {
        std::lock_guard lock(state_->mutex);
        if(first<state_->retained_first() || first>state_->end)throw Error("shared capture subscription misses history");
        if(future && first!=state_->retained_first())throw Error("new future capture claim must retain all available history");
        if(state_->next_token==std::numeric_limits<std::uint64_t>::max())throw Error("shared capture consumer token exhausted");
        for(std::size_t i=0;i<state_->limits.maximum_consumers;++i)if(!state_->claims[i].active) {
            const auto token=state_->next_token++;
            state_->claims[i]={token,first,true,future};++state_->claim_count;
            return Retention(state_,token,future);
        }
        throw Error("shared capture consumer metadata exhausted");
    }
    // Caller splits large ingress spans if necessary; this never changes its
    // separately recorded original bank oscillator fences.  Count <= one slab
    // bounds transactional allocation scratch to two buffers, even unaligned.
    void append_copy(std::uint64_t first,std::span<const float> pcm,Identity identity,Event event={}) {
        if(pcm.empty()) {
            std::lock_guard lock(state_->mutex);
            if(identity!=state_->identity || first!=state_->end)throw Error("empty shared capture identity/coordinate mismatch");
            if(event.evidence || event.timing_discontinuity || event.missing_capture)
                throw Error("empty shared capture cannot silently consume timing evidence");
            return;
        }
        const auto end=coordinate_end(first,pcm.size());
        for(float value:pcm)if(!std::isfinite(value))throw Error("nonfinite shared capture input");
        std::lock_guard lock(state_->mutex);auto& s=*state_;
        if(identity!=s.identity)throw Error("shared capture input revision changed");
        if(pcm.size()>s.limits.chunk_samples)throw Error("shared capture ingress exceeds bounded slab");
        if(first<s.end || (first!=s.end && !event.missing_capture))throw Error("unproved shared capture gap or overlap");
        s.reclaim();
        Chunk* tail=s.chunk_count?&s.at(s.next_sequence-1):nullptr;
        const bool continue_tail=tail && first==s.end && !event.evidence && !event.timing_discontinuity && !event.missing_capture &&
            first-tail->grid_first<tail->capacity;
        const auto reused=continue_tail?std::min<std::size_t>(pcm.size(),tail->capacity-std::size_t(first-tail->grid_first)):0;
        std::array<std::unique_ptr<float[]>,2> staged;
        std::array<std::uint64_t,2> staged_first{},staged_grid{};
        std::array<std::size_t,2> staged_capacity{},staged_bytes{};
        std::size_t added=0,bytes=0;
        auto cursor=coordinate_end(first,reused);
        while(cursor<end) {
            if(added==staged.size())throw Error("shared capture ingress staging bound");
            const auto grid=cursor-(cursor%s.limits.chunk_samples);
            const auto room=std::min<std::uint64_t>(s.limits.chunk_samples,std::numeric_limits<std::uint64_t>::max()-grid);
            const auto capacity=std::size_t(room);
            const auto allocation=checked_add(checked_multiply(capacity,sizeof(float)),128);
            staged_first[added]=cursor;staged_grid[added]=grid;staged_capacity[added]=capacity;staged_bytes[added]=allocation;
            bytes=checked_add(bytes,allocation);++added;
            cursor=std::min(end,coordinate_end(grid,room));
        }
        if(added>s.limits.maximum_chunks-s.chunk_count)throw Error("shared capture pinned retention exceeds chunk capacity");
        if(added>std::numeric_limits<std::uint64_t>::max()-s.next_sequence)throw Error("shared capture sequence exhausted");
        const auto required=checked_add(checked_add(s.fixed_bytes,s.payload_bytes),bytes);
        if(required>s.limits.workspace_bytes)throw Error("shared capture retained PCM exceeds workspace");
        // All allocations precede committed data/coordinate changes.  A failed
        // allocation cannot consume a prefix and make retry replay its samples.
        for(std::size_t i=0;i<added;++i)staged[i]=std::make_unique<float[]>(staged_capacity[i]);
        std::size_t consumed=0;
        if(reused) {
            std::copy_n(pcm.data(),reused,tail->data.get()+std::size_t(first-tail->grid_first));
            tail->end=coordinate_end(first,reused);consumed=reused;
        }
        for(std::size_t i=0;i<added;++i) {
            const auto sequence=s.next_sequence++;auto& c=s.at(sequence);
            if(c.sequence || c.data)std::terminate();
            const auto count=std::min<std::size_t>(pcm.size()-consumed,staged_capacity[i]-std::size_t(staged_first[i]-staged_grid[i]));
            c.data=std::move(staged[i]);c.sequence=sequence;c.first=staged_first[i];c.end=coordinate_end(c.first,count);
            c.grid_first=staged_grid[i];c.capacity=staged_capacity[i];c.bytes=staged_bytes[i];
            c.event=(consumed==0?event:Event{});
            std::copy_n(pcm.data()+consumed,count,c.data.get()+std::size_t(c.first-c.grid_first));
            consumed+=count;s.payload_bytes+=c.bytes;++s.chunk_count;
        }
        s.end=end;
    }
    RangeLease borrow(std::uint64_t first,std::size_t count)const {
        const auto end=coordinate_end(first,count);std::lock_guard lock(state_->mutex);auto& s=*state_;
        if(first<s.retained_first() || end>s.end)throw Error("shared capture requested history unavailable");
        if(s.lease_count==std::numeric_limits<std::size_t>::max())throw Error("shared capture lease count exhausted");
        std::uint64_t first_sequence=0,last_sequence=0,cursor=first;
        // Slab ends are strictly increasing even across explicit capture gaps.
        // Find the first intersecting slab without a history-length scan per
        // small replay fence.  Lease pin checks still cover the full interval.
        std::size_t low=0,high=s.chunk_count;
        while(low<high) {
            const auto middle=low+(high-low)/2;
            if(s.at(s.oldest_sequence+middle).end<=first)low=middle+1;else high=middle;
        }
        for(std::size_t i=low;i<s.chunk_count && cursor<end;++i) {
            const auto sequence=s.oldest_sequence+i;const auto& c=s.at(sequence);
            if(c.end<=cursor)continue;
            if(c.first>cursor)throw Error("shared capture range contains missing samples");
            if(c.readers==std::numeric_limits<std::size_t>::max())throw Error("shared capture reader count exhausted");
            if(!first_sequence)first_sequence=sequence;
            last_sequence=sequence;cursor=std::min(end,c.end);
        }
        if(cursor!=end)throw Error("shared capture range contains missing samples");
        if(count)for(auto sequence=first_sequence;;++sequence) {++s.at(sequence).readers;if(sequence==last_sequence)break;}
        ++s.lease_count;return RangeLease(state_,first,end,first_sequence,last_sequence);
    }
    Accounting accounting()const {
        std::lock_guard lock(state_->mutex);const auto& s=*state_;
        return {s.fixed_bytes,s.payload_bytes,checked_add(s.fixed_bytes,s.payload_bytes),s.chunk_count,s.retained_first(),s.end};
    }
    void reclaim(){std::lock_guard lock(state_->mutex);state_->reclaim();}
    void reset(Identity identity,std::uint64_t origin) {
        std::lock_guard lock(state_->mutex);auto& s=*state_;
        if(s.lease_count || s.claim_count)throw Error("shared capture reset while consumers pinned");
        for(std::size_t i=0;i<s.limits.maximum_chunks;++i)s.chunks[i]=Chunk{};
        s.identity=identity;s.origin=origin;s.end=origin;s.oldest_sequence=1;s.next_sequence=1;s.next_token=1;
        s.chunk_count=0;s.payload_bytes=0;
    }
};

// Per-bank original oscillator recurrence boundaries.  Owner slab boundaries
// never replace these.  Equal contiguous provider subblocks are run-length
// encoded, so 1-sample or 512-sample feeds do not add one deque allocation each.
class OriginalCaptureFences {
    struct Run {std::uint64_t first=0,count=0,repeats=0;};
    std::unique_ptr<Run[]> runs_;
    std::size_t capacity_=0,head_=0,size_=0;
    std::uint64_t end_=0;
    mutable std::mutex mutex_;
    // Read during visit callbacks/owner workspace checks: taking mutex_ here
    // would recursively lock a traversal. Include old+new allocation overlap.
    std::atomic<std::size_t> charged_bytes_{0};
    std::size_t first_intersecting(std::uint64_t position)const {
        std::size_t low=0,high=size_;
        while(low<high) {
            const auto middle=low+(high-low)/2;const auto& r=runs_[(head_+middle)%capacity_];
            if(coordinate_end(r.first,coordinate_product(r.count,r.repeats))<=position)low=middle+1;else high=middle;
        }
        return low;
    }
public:
    class CapacityError: public Error {public:using Error::Error;};
    struct Fence {std::uint64_t original_first=0,original_count=0,selected_first=0,selected_count=0;};
    static std::size_t required_bytes(std::size_t maximum_runs) {
        return checked_add(sizeof(OriginalCaptureFences),checked_add(checked_multiply(maximum_runs,sizeof(Run)),128));
    }
    explicit OriginalCaptureFences(std::size_t maximum_runs,std::uint64_t origin=0,
                                   std::size_t workspace_bytes=std::numeric_limits<std::size_t>::max()):capacity_(maximum_runs),end_(origin) {
        if(!maximum_runs)throw Error("invalid original capture fence limit");
        if(required_bytes(maximum_runs)>workspace_bytes)throw Error("original capture fence metadata exceeds workspace");
        runs_=std::make_unique<Run[]>(maximum_runs);
        charged_bytes_.store(required_bytes(maximum_runs),std::memory_order_relaxed);
    }
    std::size_t reserved_bytes()const{return charged_bytes_.load(std::memory_order_relaxed);}
    std::size_t run_count()const{std::lock_guard lock(mutex_);return size_;}
    // Owner-serialized opt-in after scoring workers have joined. Reserve outside
    // the traversal lock; reserve may inspect this ledger's workspace charge.
    // Failure/cancellation preserves all original oscillator restart boundaries.
    template<class Reserve>bool grow(Reserve&& reserve,std::stop_token stop={}) {
        if(stop.stop_requested())return false;
        std::size_t previous_capacity;
        {std::lock_guard lock(mutex_);previous_capacity=capacity_;}
        const auto next_capacity=checked_multiply(previous_capacity,2);
        const auto replacement=checked_add(checked_multiply(next_capacity,sizeof(Run)),128);
        const auto previous=reserved_bytes(),peak=checked_add(previous,replacement);
        const auto final_charge=required_bytes(next_capacity);
        reserve(replacement);if(stop.stop_requested())return false;
        charged_bytes_.store(peak,std::memory_order_relaxed);
        struct Restore {std::atomic<std::size_t>& charge;std::size_t old;bool committed=false;
            ~Restore(){if(!committed)charge.store(old,std::memory_order_relaxed);}} restore{charged_bytes_,previous};
        auto next=std::make_unique<Run[]>(next_capacity);
        {
            std::lock_guard lock(mutex_);
            if(capacity_!=previous_capacity)throw Error("concurrent original capture fence growth");
            for(std::size_t i=0;i<size_;++i) {
                if((i%128)==0 && stop.stop_requested())return false;
                next[i]=runs_[(head_+i)%capacity_];
            }
            if(stop.stop_requested())return false;
            runs_.swap(next);capacity_=next_capacity;head_=0;
        }
        next.reset(); // release old backing before lowering the published charge
        charged_bytes_.store(final_charge,std::memory_order_relaxed);
        restore.committed=true;return true;
    }
    void append(std::uint64_t first,std::uint64_t count) {
        std::lock_guard lock(mutex_);
        const auto end=coordinate_end(first,count);
        if(!count || count>512 || first!=end_)throw Error("invalid original capture fence");
        if(size_) {
            auto& r=runs_[(head_+size_-1)%capacity_];
            if(r.count==count) {
                if(r.repeats==std::numeric_limits<std::uint64_t>::max())throw Error("original capture fence repetitions exhausted");
                ++r.repeats;end_=end;return;
            }
        }
        if(size_==capacity_)throw CapacityError("original capture fence metadata exhausted");
        runs_[(head_+size_)%capacity_]={first,count,1};++size_;end_=end;
    }
    // Must retain the full original fence if a replay starts inside it: the
    // oscillator seeds at original_first, then advances to selected_first.
    std::uint64_t replay_floor(std::uint64_t requested)const {
        std::lock_guard lock(mutex_);
        if(requested>end_)throw Error("original capture fence future position");
        for(std::size_t i=first_intersecting(requested);i<size_;++i) {
            const auto& r=runs_[(head_+i)%capacity_];
            const auto span=coordinate_product(r.count,r.repeats);
            const auto end=coordinate_end(r.first,span);
            if(requested<r.first)throw Error("original capture fence history missing");
            if(requested<end)return r.first+((requested-r.first)/r.count)*r.count;
        }
        if(requested!=end_)throw Error("original capture fence history missing");
        return requested;
    }
    void discard_before(std::uint64_t first) {
        std::lock_guard lock(mutex_);
        if(first>end_)throw Error("original capture fence future retirement");
        while(size_) {
            auto& r=runs_[head_];if(first<=r.first)break;
            const auto completed=std::min(r.repeats,(first-r.first)/r.count);
            r.first=coordinate_end(r.first,coordinate_product(completed,r.count));
            r.repeats-=completed;
            if(r.repeats)break;
            r=Run{};head_=(head_+1)%capacity_;--size_;
        }
    }
    template<class Callback>void visit(std::uint64_t first,std::uint64_t count,Callback&& callback)const {
        // Serializes concurrent capture/retirement against a complete original
        // fence traversal. Callback may borrow PCM, but must not reenter this
        // same ledger or move/destroy it while traversing.
        std::lock_guard lock(mutex_);
        const auto end=coordinate_end(first,count);if(end>end_)throw Error("original capture fence future range");
        auto cursor=first;
        for(std::size_t i=first_intersecting(cursor);i<size_ && cursor<end;++i) {
            const auto& r=runs_[(head_+i)%capacity_];
            const auto run_end=coordinate_end(r.first,coordinate_product(r.count,r.repeats));
            if(run_end<=cursor)continue;
            if(r.first>cursor)throw Error("original capture fence history missing");
            auto original=r.first+((cursor-r.first)/r.count)*r.count;
            while(cursor<end && original<run_end) {
                const auto selected_end=std::min(end,coordinate_end(original,r.count));
                callback(Fence{original,r.count,cursor,selected_end-cursor});cursor=selected_end;
                original=coordinate_end(original,r.count);
            }
        }
        if(cursor!=end)throw Error("original capture fence history missing");
    }
};

} // namespace datapump::modem::detail::scheduled_capture
