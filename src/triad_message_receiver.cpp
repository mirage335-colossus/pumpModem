#include "datapump/triad_message_receiver.hpp"
#include "datapump/compression.hpp"
#include "datapump/received_text.hpp"
#include "datapump/speculation.h"
#include "datapump/triad_stream.hpp"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <limits>
#include <string_view>
#include <utility>
#include <lzma.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>

namespace datapump::triad::v3 {
namespace {
constexpr std::size_t quiet_slots = 2;
constexpr std::size_t maximum_diagnostic_bits = 4096;
constexpr std::size_t tag_bytes = 32;
constexpr std::size_t short_text_bytes = 16;

std::size_t checked_add(std::size_t a, std::size_t b) {
    if (b > std::numeric_limits<std::size_t>::max()-a)
        throw ReceiverWorkspaceError("Robust message storage overflow");
    return a+b;
}

std::size_t checked_multiply(std::size_t a, std::size_t b) {
    if (a && b > std::numeric_limits<std::size_t>::max()/a)
        throw ReceiverWorkspaceError("Robust message storage overflow");
    return a*b;
}

std::uint64_t address_add(std::uint64_t first, std::uint64_t offset) {
    if (offset > std::numeric_limits<std::uint64_t>::max()-first)
        throw Error("Robust message address exceeds its calendar");
    return first+offset;
}

void validate_mode(FecMode mode) {
    if (mode != FecMode::off && mode != FecMode::rs20 && mode != FecMode::rs60)
        throw Error("Invalid Robust FEC mode");
}

std::size_t short_wire_limit(FecMode mode) {
    validate_mode(mode);
    return mode == FecMode::off ? 208 : mode == FecMode::rs20 ? 304 : 400;
}

struct IntervalLayout {
    std::size_t parity, source, maximum_intervals;
    std::size_t source_limit() const { return maximum_intervals*source; }
    std::size_t wire_limit() const { return maximum_intervals*message_interval_bits; }
};

IntervalLayout interval_layout(const NativeMessageProfile& profile) {
    validate_mode(profile.fec);
    if (!profile.content_limit || profile.content_limit > default_memory_limit)
        throw Error("Invalid local Robust content quota");
    const std::size_t parity = profile.fec == FecMode::off ? 0 : profile.fec == FecMode::rs20 ? 22 : 48;
    const auto source = message_interval_bytes-parity-(profile.key ? tag_bytes : 0);
    const auto storage = checked_add(checked_multiply(2, profile.content_limit), 4096);
    const auto intervals = std::min(storage/source,
        (std::numeric_limits<std::size_t>::max()-quiet_slots-1)/message_interval_bits);
    return {parity, source, profile.content_limit > short_text_bytes ? intervals : 0};
}

const std::array<std::uint8_t, message_marker_bits>& marker() {
    static const auto value = [] {
        constexpr std::string_view domain = "DataPump/byte-boundary/v1";
        std::array<std::uint8_t,32> digest{};
        unsigned length = 0;
        if (EVP_Digest(domain.data(), domain.size(), digest.data(), &length,
                       EVP_sha256(), nullptr) != 1 || length != digest.size())
            throw Error("Robust interval marker derivation failed");
        std::array<std::uint8_t,message_marker_bits> bits{};
        for (std::size_t i=0; i<bits.size(); ++i) {
            const auto bit = i % (message_marker_bits/2);
            bits[i] = (digest[bit/8]>>(7-bit%8))&1;
        }
        return bits;
    }();
    return value;
}

bool enough_marker_evidence(std::size_t known, std::size_t errors) {
    // One configured start, no sliding or deleted-bit hypotheses. At least
    // 144 known bits and at most eight mismatches give a fair-bit false-match
    // bound below 2^-100 (sum of C(n,i), i<=8, divided by 2^n).
    return known >= 144 && errors <= 8;
}

bool marker_matches(std::span<const MessageBit> prefix) {
    if (prefix.size() < 144 || prefix.size() > message_marker_bits) return false;
    std::size_t known = 0, errors = 0;
    for (std::size_t i=0; i<prefix.size(); ++i) if (prefix[i]) {
        ++known;
        errors += *prefix[i] != marker()[i];
    }
    return enough_marker_evidence(known, errors);
}

Bytes interval_tag(const NativeMessageProfile& profile, MessageAddress address,
                   std::span<const std::uint8_t> area) {
    Bytes context{'D','P','-','T','R','I','A','D','-','I','N','T','E','R','V','A','L',1,
                  static_cast<std::uint8_t>(profile.fec), static_cast<std::uint8_t>(profile.compression)};
    for (const auto value : {address.epoch, address.ordinal})
        for (int shift=56; shift>=0; shift-=8) context.push_back(static_cast<std::uint8_t>(value>>shift));
    context.insert(context.end(), area.begin(), area.end());
    auto tag = profile.key->confidentiality_mac(context);
    if (tag.size() != tag_bytes) throw Error("Invalid Robust interval authentication tag");
    return tag;
}

void set_packed_bit(std::span<std::uint8_t> bytes, std::size_t position, unsigned bit) {
    bytes[position/8] |= static_cast<std::uint8_t>(bit<<(7-position%8));
}

unsigned packed_bit(std::span<const std::uint8_t> bytes, std::size_t position) {
    const auto byte = datapump_index_nospec(position/8, bytes.size());
    return (bytes[byte]>>(7-position%8))&1U;
}

Bytes encode_interval_source(std::span<const std::uint8_t> text, const NativeMessageProfile& profile,
                             const IntervalLayout& layout) {
    if (profile.compression) {
        auto source = compression::encode_lzma2(text, layout.source_limit());
        const auto padding = (layout.source-source.size()%layout.source)%layout.source;
        if (padding > layout.source_limit()-source.size())
            throw Error("Robust source padding exceeds its local quota");
        source.resize(source.size()+padding, 0);
        return source;
    }
    const auto cells = layout.source*8/9;
    const auto intervals = 1+(text.size()-1)/cells;
    if (intervals > layout.maximum_intervals) throw Error("Robust source exceeds its local quota");
    Bytes source(intervals*layout.source, 0);
    for (std::size_t i=0; i<text.size(); ++i) {
        auto area = std::span(source).subspan((i/cells)*layout.source, layout.source);
        const auto position = (i%cells)*9;
        set_packed_bit(area, position, 1);
        for (unsigned bit=0; bit<8; ++bit)
            set_packed_bit(area, position+1+bit, (text[i]>>(7-bit))&1U);
    }
    return source;
}

Bytes decode_interval_source(std::span<const std::uint8_t> source, const NativeMessageProfile& profile,
                             const IntervalLayout& layout) {
    if (source.empty() || source.size() > layout.source_limit() || source.size()%layout.source)
        throw Error("Incomplete Robust source areas");
    datapump_speculation_barrier();
    if (profile.compression) {
        auto decoded = compression::decode_lzma2(source, profile.content_limit);
        const auto padding = source.subspan(decoded.consumed_bytes);
        if (padding.size() >= layout.source || std::ranges::any_of(padding, [](auto byte) { return byte != 0; }))
            throw Error("Invalid Robust final source padding");
        return std::move(decoded.data);
    }
    Bytes result;
    const auto cells = layout.source*8/9;
    for (std::size_t start=0; start<source.size(); start+=layout.source) {
        const auto area = source.subspan(start, layout.source);
        bool padding = false;
        for (std::size_t cell=0; cell<cells; ++cell) {
            const auto position = cell*9;
            const bool present = packed_bit(area, position);
            unsigned value = 0;
            for (unsigned bit=0; bit<8; ++bit) value = (value<<1)|packed_bit(area, position+1+bit);
            if (!present) {
                if (value) throw Error("Nonzero absent Robust source cell");
                padding = true;
            } else {
                if (padding || result.size() == profile.content_limit)
                    throw Error("Invalid Robust source cell extent");
                datapump_speculation_barrier();
                result.push_back(static_cast<std::uint8_t>(value));
            }
        }
        for (std::size_t bit=cells*9; bit<layout.source*8; ++bit)
            if (packed_bit(area, bit)) throw Error("Nonzero Robust source padding bit");
    }
    return result;
}

bool finite_nonnegative(double value) { return std::isfinite(value) && value >= 0; }
bool geometry_valid(const ReceiverSlotScore& row) {
    return std::isfinite(row.sample_rate_hz) && row.sample_rate_hz > 0 &&
        row.stop_sample > row.first_sample &&
        row.consumed_samples == row.stop_sample-row.first_sample;
}
bool terminal_state(MessageState state) {
    return state == MessageState::complete || state == MessageState::invalid_message ||
        state == MessageState::incomplete_message || state == MessageState::incomplete ||
        state == MessageState::resource_limit || state == MessageState::failed;
}
void validate(const MessageReceiverOptions& options) {
    // All extents come from the local profile. No received header or decoded
    // byte count can increase them. Defaults retain the canonical short profile.
    if (options.native_profile) {
        const auto layout = interval_layout(*options.native_profile);
        const auto short_limit = short_wire_limit(options.native_profile->fec);
        if (options.maximum_data_bits != std::max(short_limit, layout.wire_limit()) ||
            options.maximum_slots != std::max(2*short_limit+quiet_slots, layout.wire_limit()+quiet_slots+1) ||
            options.maximum_decoded_bytes != options.native_profile->content_limit)
            throw Error("Invalid local Robust interval bounds");
        return;
    }
    if (options.maximum_slots < quiet_slots || !options.maximum_data_bits ||
        !options.maximum_decoded_bytes || options.maximum_decoded_bytes > default_memory_limit ||
        options.maximum_data_bits > default_memory_limit ||
        options.maximum_slots > 2*options.maximum_data_bits+quiet_slots)
        throw Error("Invalid local Robust message bounds");
}
struct ServiceTimer {
    MessageResult& result;
    std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();
    ~ServiceTimer() {
        result.message_service_seconds += std::chrono::duration<double>(
            std::chrono::steady_clock::now()-begin).count();
    }
};
std::size_t parity_bytes(std::size_t data_bytes, FecMode mode) {
    if (mode == FecMode::rs20) return (data_bytes+4)/5;
    if (mode == FecMode::rs60) return (3*data_bytes+4)/5;
    throw Error("Invalid Robust FEC mode");
}
std::uint32_t message_crc(std::span<const std::uint8_t> body, FecMode mode) {
    const std::array<std::uint8_t,6> domain{'D','P','R','F',1,static_cast<std::uint8_t>(mode)};
    return lzma_crc32(body.data(), body.size(), lzma_crc32(domain.data(), domain.size(), 0));
}
void region_stats(FecRegionStats& stats, std::span<const MessageBit> supplied,
                  std::span<const std::uint8_t> corrected, std::size_t first, std::size_t stop) {
    for (std::size_t byte=first; byte<stop; ++byte) {
        bool changed = false, erased = false;
        for (unsigned bit=0; bit<8; ++bit) {
            const auto input = supplied[8*byte+bit];
            if (input) {
                ++stats.received_bits;
                if (*input != ((corrected[byte]>>(7-bit))&1)) {
                    ++stats.corrected_bits;
                    changed = true;
                }
            } else {
                ++stats.missing_bits;
                erased = true;
            }
        }
        stats.corrected_bytes += changed;
        stats.erased_bytes += erased;
        stats.repaired_bytes += changed || erased;
    }
}
}

StreamContent decode_short_message(std::span<const MessageBit> bits) {
    Bytes exact;
    if (bits.size() > 208) throw Error("Robust short message exceeds its local bit bound");
    exact.reserve(bits.size());
    for (const auto bit : bits) {
        if (!bit || *bit > 1) throw Error("Robust short message contains an erasure");
        exact.push_back(*bit);
    }
    StreamContent content;
    content.message.data = compression::decode_short_bits(exact, 16);
    content.consumed_bytes = exact.size(); // One byte stores each exact symbol bit.
    return content;
}

Bytes encode_message_bits(std::span<const std::uint8_t> text, FecMode mode) {
    validate_mode(mode);
    if (text.empty() || text.size()>16) throw Error("Robust text must contain one to sixteen bytes");
    auto bits = compression::encode_short_bits(text, 208);
    if (mode == FecMode::off) return bits;
    Bytes body(1+(bits.size()+7)/8, 0);
    body[0] = static_cast<std::uint8_t>(bits.size());
    for (std::size_t index=0; index<bits.size(); ++index)
        body[1+index/8] |= bits[index]<<(7-index%8);
    const auto checksum = message_crc(body, mode);
    for (unsigned shift : {24U,16U,8U,0U}) body.push_back(static_cast<std::uint8_t>(checksum>>shift));
    const auto word = fec::rs_encode(body, parity_bytes(body.size(), mode));
    Bytes result;
    result.reserve(8*word.size());
    for (const auto byte : word) for (unsigned bit=0; bit<8; ++bit)
        result.push_back((byte>>(7-bit))&1);
    return result;
}

StreamContent decode_message_bits(std::span<const MessageBit> bits, FecMode mode) {
    validate_mode(mode);
    if (mode == FecMode::off) return decode_short_message(bits);
    // Physical completion supplied the span. Enumerate the tiny public size
    // set before examining received bytes; no received count drives allocation.
    if (bits.empty() || bits.size()%8 || bits.size()>400)
        throw Error("Robust FEC span is outside its public frame profile");
    const auto bytes = bits.size()/8;
    std::size_t body_size = 0;
    for (std::size_t candidate=6; candidate<=31; ++candidate)
        if (candidate+parity_bytes(candidate, mode) == bytes) {
            if (body_size) throw Error("Ambiguous Robust FEC frame span");
            body_size = candidate;
        }
    if (!body_size) throw Error("Robust FEC span is outside its public frame profile");
    Bytes word(bytes, 0);
    std::vector<std::size_t> erasures;
    for (std::size_t byte=0; byte<bytes; ++byte) {
        bool erased = false;
        for (unsigned bit=0; bit<8; ++bit) {
            const auto value = bits[8*byte+bit];
            if (!value) erased = true;
            else {
                if (*value>1) throw Error("Invalid Robust FEC bit value");
                word[byte] |= *value<<(7-bit);
            }
        }
        if (erased) erasures.push_back(byte);
    }
    const auto changed = fec::rs_correct(word, parity_bytes(body_size, mode), erasures);
    const auto count = static_cast<std::size_t>(word[0]);
    if (!count || count>208 || 1+(count+7)/8+4 != body_size)
        throw Error("Invalid Robust FEC short bit extent");
    if (count%8 && (word[body_size-5] & ((1U<<(8-count%8))-1)))
        throw Error("Invalid Robust FEC padding");
    std::uint32_t checksum = 0;
    for (std::size_t index=body_size-4; index<body_size; ++index) checksum = (checksum<<8)|word[index];
    if (checksum != message_crc(std::span(word).first(body_size-4), mode))
        throw Error("Robust FEC checksum mismatch");
    Bytes short_bits(count);
    for (std::size_t index=0; index<count; ++index)
        short_bits[index] = (word[1+index/8]>>(7-index%8))&1;
    StreamContent content;
    content.message.data = compression::decode_short_bits(short_bits, 16);
    content.consumed_bytes = word.size();
    content.corrected_bytes = changed;
    // A public CRC is error detection, never sender authentication.
    content.authenticated = false;
    region_stats(content.fec_stats.data, bits, word, 0, body_size-4);
    region_stats(content.fec_stats.integrity, bits, word, body_size-4, body_size);
    region_stats(content.fec_stats.parity, bits, word, body_size, word.size());
    const auto& data = content.fec_stats.data;
    content.pre_fec_accuracy = StreamBitAccuracy{data.received_bits, data.corrected_bits, data.missing_bits};
    return content;
}

MessageDecoder message_decoder(FecMode mode) {
    validate_mode(mode);
    return [mode](std::span<const MessageBit> bits) { return decode_message_bits(bits, mode); };
}

MessageReceiverOptions message_receiver_options(FecMode mode) {
    const auto count = short_wire_limit(mode);
    return {2*count+quiet_slots, count, short_text_bytes, mode != FecMode::off, {}, {}};
}

MessageReceiverOptions message_receiver_options(NativeMessageProfile profile, MessageAddressLookup address) {
    auto options = message_receiver_options(profile.fec);
    const auto layout = interval_layout(profile);
    options.maximum_decoded_bytes = profile.content_limit;
    options.maximum_data_bits = std::max(options.maximum_data_bits, layout.wire_limit());
    options.maximum_slots = std::max(options.maximum_slots, layout.wire_limit()+quiet_slots+1);
    options.native_profile = std::move(profile);
    options.address = std::move(address);
    return options;
}

MessageReceiverStorage message_receiver_storage(const MessageReceiverOptions& options) {
    validate(options);
    if (!options.native_profile) {
        // Keep the short/custom episode and its exact diagnostic arrays under
        // their existing local reservation.
        const auto slots = checked_multiply(64, options.maximum_slots);
        const auto bits = checked_multiply(8, options.maximum_data_bits);
        const auto content = checked_multiply(8, options.maximum_decoded_bytes);
        return {0, checked_add(4096, checked_add(content, checked_add(slots, bits)))};
    }
    const auto& profile = *options.native_profile;
    const auto layout = interval_layout(profile);
    const auto diagnostics = 2*maximum_diagnostic_bits*(sizeof(std::uint64_t)+sizeof(std::uint8_t));
    const auto short_bits = 2*short_wire_limit(profile.fec)*sizeof(MessageBit);
    // The result can contain a decoded source and its restricted view. The
    // source decoder can grow one result vector before it moves into that result.
    const auto receipt = checked_add(4096+diagnostics+short_bits,
        checked_multiply(4, profile.content_limit));
    if (!layout.maximum_intervals) return {0, receipt};
    auto episode = checked_add(128U*1024U, checked_multiply(2, layout.source_limit()));
    episode = checked_add(episode, checked_multiply(2, profile.content_limit));
    if (profile.compression) episode = checked_add(episode, compression::long_decoder_limit);
    return {episode, receipt};
}

MessageAddress message_address(const PublicProfile& profile, std::uint64_t public_slot) {
    const auto slot = profile.transmit_slot(public_slot);
    return {slot.pattern_begin.epoch, address_add(profile.config().format.symbol_ordinal, public_slot)};
}

MessageTransmitStorage text_message_transmit_storage(const NativeMessageProfile& profile) {
    const auto layout = interval_layout(profile);
    const auto input_and_metadata = checked_add(8192, checked_multiply(2, profile.content_limit));
    const auto preparing = checked_add(input_and_metadata, 64U*1024U);
    if (!layout.maximum_intervals) {
        const auto bits = short_wire_limit(profile.fec);
        return {checked_add(preparing, bits), checked_add(input_and_metadata, bits)};
    }
    const auto source = layout.source_limit();
    if (!profile.compression)
        return {checked_add(preparing, source), checked_add(input_and_metadata, source)};
    // The capped output grows geometrically and the old and new allocations
    // can coexist briefly during LZMA output append or final area padding.
    const auto growth = checked_multiply(3, source);
    return {checked_add(checked_add(preparing, growth), compression::long_encoder_limit),
        checked_add(input_and_metadata, checked_multiply(2, source))};
}

struct PreparedTextMessage::Impl {
    NativeMessageProfile profile;
    IntervalLayout layout;
    Bytes source;
    std::size_t wire_bits;
    bool long_form;
};

PreparedTextMessage::PreparedTextMessage(std::shared_ptr<const Impl> impl) : impl_(std::move(impl)) {}
std::size_t PreparedTextMessage::wire_bits() const noexcept { return impl_->wire_bits; }
bool PreparedTextMessage::long_form() const noexcept { return impl_->long_form; }
std::size_t PreparedTextMessage::working_bytes() const noexcept {
    return 4096+sizeof(PreparedTextMessage)+sizeof(Impl)+impl_->source.capacity();
}

PreparedTextMessage prepare_text_message(std::span<const std::uint8_t> text, NativeMessageProfile profile) {
    const auto layout = interval_layout(profile);
    if (text.empty() || text.size() > profile.content_limit)
        throw Error("Robust text is empty or exceeds its local content quota");
    const bool long_form = text.size() > short_text_bytes;
    auto source = long_form ? encode_interval_source(text, profile, layout) : encode_message_bits(text, profile.fec);
    const auto wire = long_form ? source.size()/layout.source*message_interval_bits : source.size();
    return PreparedTextMessage(std::make_shared<PreparedTextMessage::Impl>(
        PreparedTextMessage::Impl{std::move(profile), layout, std::move(source), wire, long_form}));
}

MessageBitSource PreparedTextMessage::bit_source(std::uint64_t first, MessageAddressLookup address) const {
    if (impl_->long_form && impl_->profile.key && !address)
        throw Error("Keyed Robust intervals require the local public address lookup");
    (void)address_add(first, impl_->wire_bits-1);
    return [message=impl_, first, address=std::move(address), word=Bytes{},
            cached=std::numeric_limits<std::uint64_t>::max()](std::uint64_t relative) mutable -> std::uint8_t {
        if (relative >= message->wire_bits) throw Error("Robust wire bit is outside the prepared draft");
        if (!message->long_form) return message->source[static_cast<std::size_t>(relative)];
        const auto within = static_cast<std::size_t>(relative%message_interval_bits);
        if (within < message_marker_bits) return marker()[within];
        const auto interval = relative/message_interval_bits;
        if (interval != cached) {
            const auto area = std::span(message->source).subspan(
                static_cast<std::size_t>(interval)*message->layout.source, message->layout.source);
            word.assign(area.begin(), area.end());
            if (message->profile.key) {
                const auto slot = address_add(first, relative-within+message_marker_bits);
                const auto tag = interval_tag(message->profile, address(slot), area);
                word.insert(word.end(), tag.begin(), tag.end());
            }
            if (message->layout.parity) word = fec::rs_encode(word, message->layout.parity);
            cached = interval;
        }
        return static_cast<std::uint8_t>(packed_bit(word, within-message_marker_bits));
    };
}

struct QuietSlotMessageReceiver::IntervalReceiver {
    IntervalReceiver(const NativeMessageProfile& profile_, const MessageAddressLookup& address_,
                     std::uint64_t first)
        : profile(profile_), address(address_), first_slot(first), layout(interval_layout(profile_)) {}

    NativeMessageProfile profile;
    MessageAddressLookup address;
    std::uint64_t first_slot;
    IntervalLayout layout;
    std::array<MessageBit, 8*message_interval_bytes> coded{};
    std::size_t position = message_marker_bits, corrected_bytes = 0;
    std::size_t marker_known = 0, marker_errors = 0;
    bool invalid = false;
    Bytes source;
    StreamFecStats stats;

    void push(MessageBit bit, MessageResult& receipt) {
        const auto within = position++%message_interval_bits;
        // Once aligned, every marker occupies the same slots, including a
        // damaged marker. The configured calendar never inserts or drops bits.
        if (within < message_marker_bits) {
            if (bit) {
                ++marker_known;
                marker_errors += *bit != marker()[within];
            }
            if (within+1==message_marker_bits) {
                if (enough_marker_evidence(marker_known, marker_errors)) ++receipt.recognized_markers;
                else ++receipt.damaged_markers;
                marker_known = marker_errors = 0;
            }
            return;
        }
        if (invalid) return;
        coded[within-message_marker_bits] = bit;
        if (within+1 != message_interval_bits) return;
        try { finish_interval(); }
        catch (const Error&) {
            // A later valid interval cannot turn a damaged source into a
            // shorter message. Continue timing the episode without source work.
            invalid = true;
            Bytes{}.swap(source);
        }
    }

    void finish_interval() {
        Bytes word(message_interval_bytes, 0);
        std::array<std::size_t,message_interval_bytes> erasures{};
        std::size_t missing = 0;
        for (std::size_t byte=0; byte<word.size(); ++byte) {
            bool erased = false;
            for (unsigned bit=0; bit<8; ++bit) {
                const auto value = coded[8*byte+bit];
                if (value) word[byte] |= *value<<(7-bit);
                else erased = true;
            }
            if (erased) erasures[missing++] = byte;
        }
        if (layout.parity)
            corrected_bytes += fec::rs_correct(word, layout.parity, std::span(erasures).first(missing));
        else if (missing) throw Error("Robust interval has erasures with FEC off");
        const auto area = std::span(word).first(layout.source);
        const auto parity_start = message_interval_bytes-layout.parity;
        if (profile.key) {
            const auto first_coded = address_add(first_slot, position-message_interval_bits+message_marker_bits);
            const auto expected = interval_tag(profile, address(first_coded), area);
            const auto supplied = std::span(word).subspan(layout.source, tag_bytes);
            if (CRYPTO_memcmp(expected.data(), supplied.data(), tag_bytes) != 0)
                throw Error("Robust interval authentication failed");
            datapump_speculation_barrier();
        }
        if (layout.source > layout.source_limit()-source.size())
            throw Error("Robust coded source exceeds its local quota");
        const auto needed = source.size()+layout.source;
        if (needed > source.capacity())
            source.reserve(std::max(needed, std::min(layout.source_limit(), 2*source.capacity())));
        source.insert(source.end(), area.begin(), area.end());
        region_stats(stats.data, coded, word, 0, layout.source);
        region_stats(stats.integrity, coded, word, layout.source, parity_start);
        region_stats(stats.parity, coded, word, parity_start, word.size());
    }

    StreamContent finish() {
        if (invalid || position%message_interval_bits || source.empty())
            throw Error("Incomplete or invalid Robust interval source");
        StreamContent result;
        result.message.data = decode_interval_source(source, profile, layout);
        if (result.message.data.size() <= short_text_bytes)
            throw Error("Robust interval source is below its text threshold");
        result.consumed_bytes = position/message_interval_bits*message_interval_bytes;
        result.corrected_bytes = corrected_bytes;
        result.authenticated = profile.key.has_value();
        result.fec_stats = stats;
        result.pre_fec_accuracy = StreamBitAccuracy{stats.data.received_bits,
            stats.data.corrected_bits, stats.data.missing_bits};
        return result;
    }
};

QuietSlotMessageReceiver::QuietSlotMessageReceiver(MessageDecoder decoder, MessageReceiverOptions options,
                                                   std::uint64_t first_public_slot)
    : decoder_(std::move(decoder)), options_(std::move(options)), first_public_slot_(first_public_slot) {
    if (!decoder_) throw Error("Robust message decoder is required");
    validate(options_);
    if (options_.native_profile && options_.native_profile->key &&
        options_.native_profile->content_limit > short_text_bytes && !options_.address)
        throw Error("Keyed Robust intervals require the local public address lookup");
}

QuietSlotMessageReceiver::~QuietSlotMessageReceiver() = default;
QuietSlotMessageReceiver::QuietSlotMessageReceiver(QuietSlotMessageReceiver&&) noexcept = default;
QuietSlotMessageReceiver& QuietSlotMessageReceiver::operator=(QuietSlotMessageReceiver&&) noexcept = default;

bool QuietSlotMessageReceiver::terminal() const noexcept { return terminal_state(result_.state); }

void QuietSlotMessageReceiver::fail(MessageState state, const char* reason) {
    result_.state = state;
    result_.reason = reason;
    if (terminal_state(state)) intervals_.reset();
}

SlotDecision QuietSlotMessageReceiver::classification(const ReceiverSlotScore& row) noexcept {
    if (!row.complete || !row.all_valid || !row.zero_h0_verified || !geometry_valid(row))
        return SlotDecision::ambiguous;
    if (row.decision == SlotDecision::data) {
        return row.bit && *row.bit <= 1 && row.operational_passes_conditional_bound &&
            std::isfinite(row.operational_log_e) && std::isfinite(row.operational_threshold) &&
            row.operational_log_e >= row.operational_threshold
            ? SlotDecision::data : SlotDecision::ambiguous;
    }
    if (row.decision != SlotDecision::quiet || !row.noise_statistics_valid || !row.quiet_modes ||
        !finite_nonnegative(row.quiet_energy) || !finite_nonnegative(row.quiet_threshold) ||
        !std::isfinite(row.quiet_noise_upper_tail_probability) ||
        row.quiet_noise_upper_tail_probability <= 0 || row.quiet_noise_upper_tail_probability > .001 ||
        row.quiet_energy > row.quiet_threshold || row.quiet_first_sample < row.first_sample ||
        row.quiet_stop_sample > row.stop_sample || row.quiet_stop_sample <= row.quiet_first_sample)
        return SlotDecision::ambiguous;
    return SlotDecision::quiet;
}

bool QuietSlotMessageReceiver::physical_geometry(const ReceiverSlotScore& row) {
    if (!geometry_valid(row) || (result_.sample_rate_hz && row.sample_rate_hz != result_.sample_rate_hz) ||
        (previous_first_ && (row.first_sample <= *previous_first_ || row.stop_sample <= *previous_stop_)))
        return false;
    const auto newly_covered = previous_stop_ && row.first_sample <= *previous_stop_
        ? row.stop_sample-*previous_stop_ : row.stop_sample-row.first_sample;
    if (newly_covered > std::numeric_limits<std::uint64_t>::max()-result_.represented_samples)
        return false;
    result_.represented_samples += newly_covered;
    result_.sample_rate_hz = row.sample_rate_hz;
    result_.represented_baseband_seconds = result_.represented_samples/row.sample_rate_hz;
    previous_first_ = row.first_sample;
    previous_stop_ = row.stop_sample;
    return true;
}

void QuietSlotMessageReceiver::commit_bit(MessageBit value, std::uint64_t local_index) {
    const auto& profile = options_.native_profile;
    if (profile) {
        const auto limit = intervals_ ? intervals_->layout.wire_limit() : short_wire_limit(profile->fec);
        if (result_.observed_bits+result_.missing_slots >= limit) {
            fail(MessageState::resource_limit, "local_wire_bit_limit_exceeded");
            return;
        }
    }
    if (value) {
        ++result_.observed_bits;
        if (!profile || result_.received_bits.size() < maximum_diagnostic_bits)
            result_.received_bits.push_back(*value);
    } else {
        ++result_.missing_slots;
        if (!profile || result_.erasure_slot_indices.size() < maximum_diagnostic_bits)
            result_.erasure_slot_indices.push_back(local_index);
    }
    if (intervals_) {
        intervals_->push(value, result_);
        return;
    }
    result_.bit_slots.push_back(value);
    if (profile && !leading_ambiguity_ && result_.bit_slots.size() == message_marker_bits &&
        marker_matches(result_.bit_slots)) {
        result_.long_form = true;
        result_.recognized_markers = 1;
        if (profile->content_limit <= short_text_bytes) {
            fail(MessageState::resource_limit, "local_interval_content_limit_exceeded");
            return;
        }
        const auto first = address_add(first_public_slot_, local_index-(message_marker_bits-1));
        intervals_ = std::make_unique<IntervalReceiver>(*profile, options_.address, first);
        result_.bit_slots.clear();
    }
}

void QuietSlotMessageReceiver::commit_erasures(std::uint64_t stop) {
    if (!pending_nondata_first_) return;
    const auto begin = *pending_nondata_first_;
    pending_nondata_first_.reset();
    if (options_.native_profile && stop-begin > options_.maximum_data_bits-result_.observed_bits-result_.missing_slots) {
        fail(MessageState::resource_limit, "local_wire_bit_limit_exceeded");
        return;
    }
    for (auto index=begin; index<stop && !terminal(); ++index) commit_bit({}, index);
}

void QuietSlotMessageReceiver::finish_decode() {
    // Only the final two eligible quiet slots are the physical suffix. Earlier
    // quiet/ambiguous slots remain erasures, including undecoded leading slots.
    const auto first_trailing = result_.slots_consumed-quiet_slots;
    commit_erasures(first_trailing);
    result_.trailing_quiet_slots = {first_trailing, first_trailing+1};
    result_.physical_complete = true;
    if (terminal()) return;
    if (leading_ambiguity_) {
        // FEC corrects located interior losses, not an unknown message start.
        // Only the continuous owner's separate two-quiet rearm can discard an
        // unresolved prefix and establish the start of a new bounded episode.
        fail(MessageState::incomplete_message, "leading_message_prefix_unqualified");
        return;
    }
    if (!result_.long_form && options_.native_profile && marker_matches(result_.bit_slots))
        result_.long_form = true; // Sufficient evidence of a truncated first marker.
    if (!result_.long_form && result_.missing_slots && !options_.decoder_accepts_erasures) {
        fail(MessageState::incomplete_message, "message_contains_erasures");
        return;
    }
    try {
        if (result_.long_form && !intervals_) throw Error("Truncated Robust interval marker");
        auto decoded = intervals_ ? intervals_->finish() : options_.native_profile
            ? decode_message_bits(result_.bit_slots, options_.native_profile->fec)
            : decoder_(result_.bit_slots);
        intervals_.reset();
        if (decoded.message.data.size() > options_.maximum_decoded_bytes ||
            decoded.message.filename.size() > options_.maximum_decoded_bytes ||
            decoded.message.callsign.size() > options_.maximum_decoded_bytes ||
            decoded.message.grid.size() > options_.maximum_decoded_bytes) {
            fail(MessageState::invalid_message, "invalid_native_decoder_result");
            return;
        }
        result_.filtered_text = received_text(decoded.message.data);
        result_.decoded_content = std::move(decoded);
        result_.message_ready_for_decode = true;
        result_.state = MessageState::complete;
        result_.reason.clear();
    } catch (...) {
        // Codec exception text can contain input bytes. Keep failure local and
        // bounded; it cannot turn physical completion into successful decoding.
        fail(MessageState::invalid_message, "codec_rejected_message");
    }
}

const MessageResult& QuietSlotMessageReceiver::consume_slot(std::uint64_t local_index,
                                                          const ReceiverSlotScore& row) {
    if (terminal()) throw Error("Robust message receiver is terminal");
    ServiceTimer timer{result_};
    result_.reacquisition_required = false;
    if (local_index != result_.slots_consumed) {
        fail(MessageState::failed, "slot_sequence_discontinuity");
        return result_;
    }
    if (result_.slots_consumed >= options_.maximum_slots) {
        fail(MessageState::resource_limit, "local_slot_limit_exceeded");
        return result_;
    }
    ++result_.slots_consumed;
    const auto category = physical_geometry(row) ? classification(row) : SlotDecision::ambiguous;
    ++result_.classification_counts[static_cast<std::size_t>(category)];
    if (category == SlotDecision::data) {
        result_.reacquisition_required = pending_nondata_first_.has_value();
        if (result_.reacquisition_required) {
            ++result_.reacquisition_count;
            commit_erasures(local_index);
            if (terminal()) return result_;
        }
        quiet_run_ = 0;
        if (result_.observed_bits >= options_.maximum_data_bits) {
            fail(MessageState::resource_limit, "local_data_bit_limit_exceeded");
            return result_;
        }
        commit_bit(*row.bit, local_index);
        if (terminal()) return result_;
        result_.state = MessageState::receiving;
        result_.reason.clear();
    } else if (category == SlotDecision::ambiguous) {
        if (!result_.observed_bits) leading_ambiguity_ = true;
        if (!pending_nondata_first_) pending_nondata_first_ = local_index;
        quiet_run_ = 0;
        result_.state = result_.observed_bits ? MessageState::ambiguous : MessageState::listening;
        result_.reason = "unscored_or_ambiguous_slot";
    } else if (result_.observed_bits) {
        if (!pending_nondata_first_) pending_nondata_first_ = local_index;
        if (++quiet_run_ >= quiet_slots) finish_decode();
        else {
            result_.state = MessageState::awaiting_end_or_resume;
            result_.reason.clear();
        }
    } else {
        // Once a possible prefix exists, intervening quiet is still part of
        // that unresolved physical region. Preserve its exact slot position;
        // silently shortening it could give a FEC decoder a different frame.
        if (leading_ambiguity_ && !pending_nondata_first_) pending_nondata_first_ = local_index;
        quiet_run_ = 0;
        result_.state = MessageState::listening;
        result_.reason.clear();
    }
    return result_;
}

const MessageResult& QuietSlotMessageReceiver::finish_input() {
    if (!terminal()) fail(MessageState::incomplete, "transport_ended_before_physical_completion");
    return result_;
}
const MessageResult& QuietSlotMessageReceiver::cancel() {
    if (!terminal()) fail(MessageState::incomplete, "receive_cancelled_before_physical_completion");
    return result_;
}

ContinuousMessageReceiver::ContinuousMessageReceiver(MessageDecoder decoder, MessageReceiverOptions options,
    std::uint64_t first_public_slot, std::size_t maximum_pending_messages)
    : decoder_(std::move(decoder)), options_(std::move(options)), episode_(decoder_, options_, first_public_slot),
      next_slot_(first_public_slot), episode_first_(first_public_slot), maximum_pending_(maximum_pending_messages) {
    if (!maximum_pending_ || maximum_pending_ > 64) throw Error("Invalid Robust pending message bound");
}

ContinuousMessageReceiver::ContinuousMessageReceiver(NativeMessageProfile profile, MessageAddressLookup address,
    std::uint64_t first_public_slot, std::size_t maximum_pending_messages)
    : ContinuousMessageReceiver(message_decoder(profile.fec), message_receiver_options(profile, std::move(address)),
        first_public_slot, maximum_pending_messages) {}

bool ContinuousMessageReceiver::physical_geometry(const ReceiverSlotScore& row) {
    if (!geometry_valid(row) || (sample_rate_hz_ && sample_rate_hz_ != row.sample_rate_hz) ||
        (previous_first_ && (row.first_sample <= *previous_first_ || row.stop_sample <= *previous_stop_)))
        return false;
    sample_rate_hz_ = row.sample_rate_hz;
    previous_first_ = row.first_sample;
    previous_stop_ = row.stop_sample;
    return true;
}

void ContinuousMessageReceiver::rearm() {
    episode_ = QuietSlotMessageReceiver(decoder_, options_, next_slot_);
    episode_first_ = next_slot_;
    recovering_ = false;
    quiet_run_ = 0;
}

bool ContinuousMessageReceiver::consume(const ReceiverSlotScore& supplied) {
    if (stopped_) throw Error("Robust continuous message receiver is stopped");
    if (messages_.size() >= maximum_pending_) return false;
    if (supplied.index != next_slot_ || next_slot_ == std::numeric_limits<std::uint64_t>::max()) {
        rejected_ = episode_.result();
        rejected_.state = MessageState::failed;
        rejected_.reason = "public_slot_sequence_discontinuity";
        rejected_.physical_complete = rejected_.message_ready_for_decode = false;
        recovering_ = stopped_ = true;
        return true;
    }
    auto row = supplied;
    if (!physical_geometry(row)) row.complete = false;
    const auto category = QuietSlotMessageReceiver::classification(row);
    ++next_slot_;
    if (recovering_) {
        quiet_run_ = category == SlotDecision::quiet ? quiet_run_+1 : 0;
        if (quiet_run_ >= quiet_slots) rearm();
        return true;
    }
    const auto& message = episode_.consume_slot(next_slot_-1-episode_first_, row);
    if (message.physical_complete) {
        messages_.push_back({episode_first_, next_slot_, message});
        rearm();
    } else if (message.state == MessageState::resource_limit || message.state == MessageState::failed ||
               message.slots_consumed >= options_.maximum_slots) {
        rejected_ = message;
        if (rejected_.state != MessageState::resource_limit && rejected_.state != MessageState::failed) {
            rejected_.state = MessageState::resource_limit;
            rejected_.reason = "local_slot_limit_before_physical_completion";
        }
        recovering_ = true;
        quiet_run_ = category == SlotDecision::quiet ? 1 : 0;
    } else if (!message.observed_bits) {
        quiet_run_ = category == SlotDecision::quiet ? quiet_run_+1 : 0;
        if (quiet_run_ >= quiet_slots) rearm();
    } else quiet_run_ = 0;
    return true;
}

std::vector<CompletedMessage> ContinuousMessageReceiver::drain_messages() {
    std::vector<CompletedMessage> result;
    result.reserve(messages_.size());
    while (!messages_.empty()) {
        result.push_back(std::move(messages_.front()));
        messages_.pop_front();
    }
    return result;
}

const MessageResult& ContinuousMessageReceiver::result() const noexcept {
    return recovering_ ? rejected_ : episode_.result();
}

void ContinuousMessageReceiver::invalidate_episode(std::string reason) {
    if (stopped_) return;
    rejected_ = episode_.result();
    rejected_.state = MessageState::incomplete;
    rejected_.physical_complete = rejected_.message_ready_for_decode = false;
    // Caller supplies a local status, never received bytes. Bound even that
    // diagnostic before it can be retained by a long-lived receiver.
    rejected_.reason = reason.substr(0, 128);
    episode_.cancel();
    recovering_ = true;
    quiet_run_ = 0;
}

void ContinuousMessageReceiver::finish_input() {
    if (stopped_) return;
    if (!recovering_) episode_.finish_input();
    stopped_ = true;
}
void ContinuousMessageReceiver::cancel() {
    if (stopped_) return;
    if (!recovering_) episode_.cancel();
    stopped_ = true;
}

} // namespace datapump::triad::v3
