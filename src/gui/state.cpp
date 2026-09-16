#include "state.hpp"
#include "binary_editor.hpp"
#include "datapump/live.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <map>
#include <set>
#include <sstream>

namespace datapump::gui {
Bytes parse_binary_bits(std::string_view text) {
    std::size_t count=0;
    for (const char value:text) {
        if (value=='0' || value=='1') ++count;
        else if (std::string_view(" \t\r\n\f\v").find(value)==std::string_view::npos)
            throw Error("Binary input accepts only 0, 1 and whitespace");
    }
    if (!count) throw Error("Enter one or more binary bits");
    Bytes bits;
    bits.reserve(count);
    for (const char value:text)
        if (value=='0' || value=='1') bits.push_back(static_cast<std::uint8_t>(value-'0'));
    return bits;
}
Inbox::Inbox(std::size_t capacity) : capacity_(capacity) {
    if (!capacity) throw Error("Receive cache capacity must be positive");
}
void Inbox::put(StreamContent stream, std::optional<ReceptionIdentity> identity) {
    if (stream.message.data.size() > capacity_) throw Error("Received message exceeds the cache capacity");
    auto existing = std::find_if(items_.begin(), items_.end(), [&](const auto& item) {
        return item.message.local_id == stream.message.local_id;
    });
    if (existing != items_.end()) {
        used_ -= existing->message.data.size();
        identities_.erase(existing->message.local_id);
        items_.erase(existing);
    }
    while (!items_.empty() &&
           (stream.message.data.size() > capacity_ - used_ || items_.size() >= 4096)) {
        used_ -= items_.front().message.data.size();
        identities_.erase(items_.front().message.local_id);
        items_.pop_front();
    }
    used_ += stream.message.data.size();
    if(identity)identities_[stream.message.local_id]=*identity;
    items_.push_back(std::move(stream));
}
void Inbox::clear() noexcept { items_.clear(); identities_.clear(); used_ = 0; }
void Inbox::erase(std::string_view reception_id) {
    const auto found=std::find_if(items_.begin(),items_.end(),[&](const auto& item) {
        return id_label(item.message)==reception_id;
    });
    if(found==items_.end())return;
    used_-=found->message.data.size();
    identities_.erase(found->message.local_id);
    items_.erase(found);
}
std::vector<std::string> Inbox::erase_signal(std::uint64_t signal_id) {
    std::vector<std::string> erased;
    for(auto item=items_.begin();item!=items_.end();) {
        const auto identity=identities_.find(item->message.local_id);
        if(identity==identities_.end() || identity->second.signal_id!=signal_id) {++item;continue;}
        erased.push_back(id_label(item->message));
        used_-=item->message.data.size();identities_.erase(identity);item=items_.erase(item);
    }
    return erased;
}
std::optional<std::uint64_t> Inbox::revision(std::uint64_t signal_id) const {
    std::optional<std::uint64_t> revision;
    for(const auto& [id,identity]:identities_)
        if(identity.signal_id==signal_id && (!revision || identity.revision>*revision))revision=identity.revision;
    return revision;
}
std::vector<const StreamContent*> Inbox::file_items() const {
    std::vector<const StreamContent*> files;
    for (const auto& stream : items_) {
        if(stream.message.kind!=MessageKind::text)files.push_back(&stream);
    }
    return files;
}

void TransmissionPolicy::started(bool simulation, bool encrypted, Clock::time_point now) {
    if (remaining(simulation, encrypted, now).count() > 0)
        throw Error(active_ ? "A transmission is already active" : "The six-second transmit separation is still active");
    active_ = true;
    active_hardware_output_ = !simulation;
}
void TransmissionPolicy::finished(Clock::time_point now) noexcept {
    if (active_ && active_hardware_output_) next_hardware_ = now + std::chrono::seconds(6);
    abort_start();
}
void TransmissionPolicy::abort_start() noexcept { active_ = false; active_hardware_output_ = false; }
std::chrono::milliseconds TransmissionPolicy::remaining(bool simulation, bool /*encrypted*/, Clock::time_point now) const noexcept {
    if (active_) return std::chrono::milliseconds::max();
    if (simulation || now >= next_hardware_) return std::chrono::milliseconds::zero();
    return std::chrono::ceil<std::chrono::milliseconds>(next_hardware_ - now);
}

PlotUpdate PlotReplayPolicy::observe(std::uint64_t sequence, std::uint64_t transmission_id,
                                    bool replay, std::size_t replay_frame) {
    const bool changed = !sequence_ || *sequence_ != sequence;
    const bool beginning = replay && (!replaying_ || !transmission_ || *transmission_ != transmission_id);
    const bool frame_changed = !replay_frame_ || *replay_frame_ != replay_frame;
    const bool update = replay ? beginning || frame_changed : replaying_ || changed;
    sequence_ = sequence;
    transmission_ = transmission_id;
    replay_frame_ = replay ? std::optional<std::size_t>(replay_frame) : std::nullopt;
    replaying_ = replay;
    return {update, update, beginning};
}
std::string format_bit_rate(double rate) {
    if (!std::isfinite(rate) || rate < 0) return "Unavailable";
    const char* unit = "bit/s";
    if (rate >= 1e9) { rate /= 1e9; unit = "Gbit/s"; }
    else if (rate >= 1e6) { rate /= 1e6; unit = "Mbit/s"; }
    else if (rate >= 1e3) { rate /= 1e3; unit = "kbit/s"; }
    std::ostringstream text;
    text.imbue(std::locale::classic());
    text << std::setprecision(3) << std::defaultfloat << rate << ' ' << unit;
    return text.str();
}
std::vector<std::string> key_entry_names(std::string_view text) {
    std::vector<std::string> names;
    for (;;) {
        const auto comma=text.find(',');
        auto name=text.substr(0,comma);
        const auto first=name.find_first_not_of(" \t\r\n"),last=name.find_last_not_of(" \t\r\n");
        if(first==std::string_view::npos)throw Error("Each key entry needs a name");
        names.emplace_back(name.substr(first,last-first+1));
        if(names.size()>128)throw Error("A keyfile supports at most 128 named entries");
        if(comma==std::string_view::npos)return names;
        text.remove_prefix(comma+1);
    }
}
std::vector<std::string> key_choice_labels(std::span<const std::string> names) {
    std::vector<std::string> labels{"None"};
    labels.reserve(names.size()+1);
    for(std::size_t index=0;index<names.size();++index) {
        labels.push_back(std::to_string(index+1)+". "+display_label(names[index]));
    }
    return labels;
}
std::string folder_uri(const std::filesystem::path& directory) {
    if(directory.empty())throw Error("Choose a keyfile first");
    auto encoded_path=std::filesystem::absolute(directory).lexically_normal().generic_u8string();
    std::string path(encoded_path.begin(),encoded_path.end());
    if(path.find('\0')!=std::string::npos)throw Error("Folder path contains a zero byte");
    std::string uri="file://";
#ifdef _WIN32
    if(path.starts_with("//?/UNC/"))path="//"+path.substr(8);
    else if(path.starts_with("//?/"))path.erase(0,4);
    uri=path.starts_with("//")?"file:":"file:///";
#endif
    constexpr char hex[]="0123456789ABCDEF";
    for(std::size_t index=0;index<path.size();++index) {
        const auto byte=static_cast<unsigned char>(path[index]);
        bool unreserved=(byte>='a' && byte<='z') || (byte>='A' && byte<='Z') ||
            (byte>='0' && byte<='9') || byte=='-' || byte=='.' || byte=='_' || byte=='~' || byte=='/';
#ifdef _WIN32
        unreserved=unreserved || (index==1 && byte==':');
#endif
        if(unreserved)uri+=static_cast<char>(byte);
        else { uri+='%';uri+=hex[byte>>4];uri+=hex[byte&15]; }
    }
    return uri;
}

namespace {
bool recovery_pending(const SignalLine& line) {
    using transfer::RecoveryState;
    const auto state=line.recovery_progress.state;
    if(state==RecoveryState::recovered && !line.validated)return true;
    return state!=RecoveryState::none && state!=RecoveryState::recovered && state!=RecoveryState::exhausted;
}
std::optional<Bytes> signal_byte_prefix(const SignalLine& line) {
    if (!signal_byte_aligned(line) || line.text.empty() || line.text.size()>line.received_bits ||
        line.text.find_first_not_of("01")!=std::string::npos) return {};
    Bytes bytes(line.text.size()/8);
    for(std::size_t i=0;i<bytes.size()*8;++i)
        bytes[i/8]|=static_cast<std::uint8_t>((line.text[i]-'0')<<(7-i%8));
    return bytes;
}
bool physical_complete_bits(const SignalLine& line) {
    return line.binary && line.complete && line.received_bits &&
        (!line.expected_bits || line.received_bits==line.expected_bits) &&
        line.text.size()==line.received_bits && line.text.find_first_not_of("01")==std::string::npos;
}
bool complete_bits(const SignalLine& line) {return !recovery_pending(line) && physical_complete_bits(line);}
}
bool signal_byte_aligned(const SignalLine& line) {
    return !recovery_pending(line) && line.binary && line.complete && line.received_bits && line.received_bits%8==0;
}
std::string signal_display_text(const SignalLine& line) {
    if(const auto bytes=signal_byte_prefix(line)) return BinaryEditor(*bytes).text();
    if(!line.binary && line.complete && !line.validated && line.text_message && line.pattern_score)
        return BinaryEditor(Bytes(line.text.begin(),line.text.end())).text();
    return line.text;
}
std::string signal_status_label(const SignalLine& line) {
    using transfer::RecoveryState;
    if(line.recovery_progress.state==RecoveryState::recovered && !line.validated)return "source invalid";
    if(line.recovery_progress.state==RecoveryState::ready || line.recovery_progress.state==RecoveryState::running)return "recovering";
    if(recovery_pending(line))return "search incomplete";
    if (signal_byte_aligned(line)) return "text received";
    if (line.binary) return line.complete?"binary received":"binary pending";
    if(line.complete && line.pattern_score && !line.validated)return "text received";
    return line.validated?(line.text_message?"decoded":"decoded bytes"):"pending";
}
std::string signal_recovery_label(const SignalLine& line) {
    using transfer::RecoveryState;
    const auto& progress=line.recovery_progress;
    if(progress.state==RecoveryState::none)return {};
    std::string label="Reception complete";
    switch(progress.state) {
    case RecoveryState::ready: label+=" — recovery queued";break;
    case RecoveryState::running: label+=" — recovering";break;
    case RecoveryState::incomplete: label+=" — search incomplete (time budget reached)";break;
    case RecoveryState::cancelled: label+=" — search incomplete (cancelled)";break;
    case RecoveryState::unavailable: label+=" — search unavailable";break;
    case RecoveryState::ambiguous: label+=" — ambiguous recovery";break;
    case RecoveryState::exhausted: label+=" — recovery search exhausted";break;
    case RecoveryState::recovered: label+=line.validated?" — recovered":" — source invalid";break;
    case RecoveryState::none: break;
    }
    label+="; "+std::to_string(progress.attempts);
    if(progress.total)label+=" / "+std::to_string(progress.total);
    label+=" attempts";
    if(progress.elapsed.count())label+="; "+std::to_string(progress.elapsed.count()/1000)+" s";
    return label;
}
std::string signal_gap_label(const SignalLine& line) {
    if(!line.missing_symbols)return {};
    if(line.validated)return std::to_string(line.missing_symbols)+(line.missing_symbols==1?" missing timed bit":" missing timed bits");
    return std::to_string(line.missing_symbols)+(line.missing_symbols==1?" missing bit filled with 0":" missing bits filled with 0");
}
std::string signal_repair_label(const SignalLine& line) {
    if(!line.validated)return {};
    const auto& stats=line.fec_stats;
    const auto repaired=stats.data.repaired_bytes+stats.integrity.repaired_bytes+stats.parity.repaired_bytes;
    if(!repaired)return {};
    const auto erased=stats.data.erased_bytes+stats.integrity.erased_bytes+stats.parity.erased_bytes;
    auto label="RS repaired "+std::to_string(repaired)+" B (data "+std::to_string(stats.data.repaired_bytes);
    if(stats.integrity.repaired_bytes)label+=", HMAC "+std::to_string(stats.integrity.repaired_bytes);
    label+=", parity "+std::to_string(stats.parity.repaired_bytes);
    if(erased)label+="; erasures "+std::to_string(erased);
    if(stats.data.missing_bits)label+="; missing data bits "+std::to_string(stats.data.missing_bits);
    return label+")";
}
std::string signal_preamble_label(const SignalLine& line) {
    if(line.pattern_score && std::isfinite(*line.pattern_score)) {
        std::ostringstream text;text.imbue(std::locale::classic());
        text<<"Pattern score "<<std::fixed<<std::setprecision(1)<<*line.pattern_score;
        return text.str();
    }
    if (line.binary) return "Preamble none";
    const auto value=line.preamble_received_percent;
    if (!value || !std::isfinite(*value) || *value<0 || *value>100) return "Preamble --";
    if (*value>99.9 && *value<100) return "Preamble >99.9%";
    std::ostringstream text; text.imbue(std::locale::classic());
    text<<"Preamble "<<std::fixed<<std::setprecision(1)<<*value<<'%';
    return text.str();
}
std::string signal_data_label(const SignalLine& line) {
    if(recovery_pending(line))return "Source not recovered";
    if(line.complete&&!line.validated&&!line.binary&&!line.raw_bits.empty())return "No checksum / FEC";
    if (line.binary || (line.pattern_score && line.complete && !line.validated))
        return line.received_bits>line.text.size()?"Raw observations / prefix":"Raw observations";
    if (!line.validated) return "Data pre-FEC pending";
    const auto& accuracy=line.pre_fec_accuracy;
    if (!accuracy || !accuracy->received_data_bits || accuracy->corrected_data_bits>accuracy->received_data_bits)
        return "Data pre-FEC --";
    if (!accuracy->corrected_data_bits) return accuracy->missing_data_bits?"Data 100% known":"Data 100% pre-FEC";
    const auto percent=100.*(1.-static_cast<double>(accuracy->corrected_data_bits)/static_cast<double>(accuracy->received_data_bits));
    // A small error in a large file must never round to a perfect reception.
    if (percent>99.99) return accuracy->missing_data_bits?"Data >99.99% known":"Data >99.99% pre-FEC";
    std::ostringstream text; text.imbue(std::locale::classic());
    text<<"Data "<<std::fixed<<std::setprecision(2)<<percent<<(accuracy->missing_data_bits?"% known":"% pre-FEC");
    return text.str();
}

bool Signals::update(SignalLine line) {
    if(std::find(retired_ids_.begin(),retired_ids_.end(),line.id)!=retired_ids_.end())return false;
    if (line.binary) {
        line.validated=false; line.reception_id.clear(); line.text_message=false;
        line.preamble_received_percent.reset(); line.pre_fec_accuracy.reset();line.fec_stats={};
    }
    if (line.raw_bits.size()>4096 || line.raw_bits.find_first_not_of("01")!=std::string::npos ||
        (line.received_bits && line.raw_bits.size()!=line.received_bits) ||
        (line.expected_bits && line.raw_bits.size()!=line.expected_bits)) line.raw_bits.clear();
    if (line.text.size()>4096) line.text.resize(4096);
    const auto found=std::find_if(lines_.begin(),lines_.end(),[&](const auto& item) { return item.id==line.id; });
    if (found!=lines_.end()) {
        if(line.revision<found->revision)return false;
        if(line.revision==found->revision && ((found->validated && !line.validated) ||
            ((found->binary || found->pattern_score) && found->complete && !line.complete)))return false;
        *found=std::move(line);
    } else {
        if (lines_.size()>=64) lines_.pop_front();
        lines_.push_back(std::move(line));
    }
    return true;
}
void Signals::erase(std::uint64_t id) {
    std::erase_if(lines_,[&](const auto& line){return line.id==id;});
    if(std::find(retired_ids_.begin(),retired_ids_.end(),id)!=retired_ids_.end())return;
    if(retired_ids_.size()>=64)retired_ids_.pop_front();
    retired_ids_.push_back(id);
}
void apply_receptions(Inbox& inbox, Signals& signals, live::Snapshot& snapshot) {
    if(snapshot.signals.empty() && snapshot.received.empty())return;
    Signals staged=signals;
    std::set<std::string> referenced_receptions;
    std::map<std::uint64_t,const live::SignalUpdate*> accepted;
    const auto retract=[&](const std::string& reception_id) {
        if(reception_id.empty())return;
        referenced_receptions.insert(reception_id);
        inbox.erase(reception_id);
    };
    const auto retract_signal=[&](std::uint64_t id) {
        for(const auto& reception_id:inbox.erase_signal(id))referenced_receptions.insert(reception_id);
    };
    for(const auto& signal:snapshot.signals) {
        if(!signal.reception_id.empty())referenced_receptions.insert(signal.reception_id);
        const auto cached_revision=inbox.revision(signal.id);
        if(cached_revision && (signal.revision<*cached_revision ||
            (signal.revision==*cached_revision && (!signal.complete || !signal.validated))))continue;
        const auto previous=std::find_if(staged.lines().begin(),staged.lines().end(),[&](const auto& line) {
            return line.id==signal.id;
        });
        const auto old_reception=previous!=staged.lines().end() && signal.revision>previous->revision?
            previous->reception_id:std::string{};
        const bool short_text=signal.complete&&!signal.validated&&!signal.binary&&!signal.raw_bits.empty();
        SignalLine line{signal.id,signal.frequency_hz,signal.text,signal.validated,signal.reception_id,short_text,
            signal.preamble_received_percent,signal.pre_fec_accuracy,signal.binary,signal.complete,
            signal.received_bits,signal.expected_bits,signal.pattern_score};
        line.raw_bits=signal.raw_bits;line.missing_symbols=signal.missing_symbols;
        line.fec_stats=signal.fec_stats;line.revision=signal.revision;line.recovery_progress=signal.recovery_progress;
        if(!staged.update(std::move(line)))continue;
        if(cached_revision && signal.revision>*cached_revision)retract_signal(signal.id);
        retract(old_reception);
        accepted[signal.id]=&signal;
        for(const auto id:signal.superseded_ids) {
            if(id==signal.id)continue;
            const auto obsolete=std::find_if(staged.lines().begin(),staged.lines().end(),[&](const auto& value) {
                return value.id==id;
            });
            retract_signal(id);
            if(obsolete!=staged.lines().end())retract(obsolete->reception_id);
            staged.erase(id);accepted.erase(id);
        }
    }
    std::map<std::string,const live::SignalUpdate*> completed_receptions;
    for(const auto& [id,signal]:accepted)
        if(signal->complete&&signal->validated&&!signal->reception_id.empty())
            completed_receptions[signal->reception_id]=signal;
    for(auto& received:snapshot.received) {
        if(!received.stream_complete||!received.content_validated)continue;
        const auto reception_id=id_label(received.content.message);
        if(referenced_receptions.contains(reception_id)&&!completed_receptions.contains(reception_id))continue;
        const auto completed=completed_receptions.find(reception_id);
        const auto identity=completed==completed_receptions.end()?std::optional<Inbox::ReceptionIdentity>{}:
            Inbox::ReceptionIdentity{completed->second->id,completed->second->revision};
        inbox.put(std::move(received.content),identity);
    }
    for(const auto& [id,signal]:accepted) {
        const auto current=std::find_if(staged.lines().begin(),staged.lines().end(),[&](const auto& line) {
            return line.id==id;
        });
        if(current==staged.lines().end())continue;
        const auto stream=std::find_if(inbox.items().begin(),inbox.items().end(),[&](const auto& item) {
            return id_label(item.message)==signal->reception_id;
        });
        auto line=*current;
        line.text_message=line.text_message||(stream!=inbox.items().end()&&stream->message.kind==MessageKind::text);
        staged.update(std::move(line));
    }
    signals=std::move(staged);
}
std::optional<std::string> Signals::copy_id(std::size_t index) const {
    if (index>=lines_.size() || recovery_pending(lines_[index]) || lines_[index].binary || !lines_[index].validated || !lines_[index].text_message || lines_[index].reception_id.empty()) return std::nullopt;
    return lines_[index].reception_id;
}
std::optional<std::string> Signals::copy_bits(std::size_t index) const {
    if (index>=lines_.size()) return std::nullopt;
    const auto& line=lines_[index];
    if (signal_byte_aligned(line) || !complete_bits(line)) return std::nullopt;
    return line.text;
}
std::optional<std::string> Signals::copy_raw_bits(std::size_t index) const {
    if (index>=lines_.size()) return {};
    const auto& line=lines_[index];
    // Exact admitted observations are a diagnostic/raw operation, independent
    // of whether a possible coded-source search has completed.
    if (physical_complete_bits(line)) return line.text;
    if(recovery_pending(line))return {};
    if (line.binary || !line.complete || line.validated || !line.text_message || line.text.empty() ||
        !line.pattern_score || !std::isfinite(*line.pattern_score) || line.raw_bits.empty()) return {};
    return line.raw_bits;
}
std::optional<std::string> Signals::copy_text(std::size_t index) const {
    const auto bytes=copy_bytes(index);
    if(!bytes)return {};
    return BinaryEditor(*bytes).text();
}
std::optional<Bytes> Signals::copy_bytes(std::size_t index) const {
    if(index>=lines_.size())return {};
    const auto& line=lines_[index];
    if(recovery_pending(line))return {};
    if(signal_byte_aligned(line) && complete_bits(line))return signal_byte_prefix(line);
    if(line.binary || !line.complete || line.validated || !line.text_message || !line.pattern_score ||
       !std::isfinite(*line.pattern_score) || line.text.empty())return {};
    return Bytes(line.text.begin(),line.text.end());
}

std::string id_label(const Message& message) {
    constexpr char digits[]="0123456789abcdef";
    std::string result;
    for (auto value:message.local_id) { result+=digits[value>>4]; result+=digits[value&15]; }
    return result;
}
std::string display_label(std::string_view text) {
    std::string result;
    for (const char value:text) {
        const auto byte=static_cast<unsigned char>(value);
        result += byte<32 || byte==127 ? ' ' : value;
    }
    return result;
}
}
