#include "state.hpp"
#include "binary_editor.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
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
void Inbox::put(StreamContent stream) {
    if (stream.message.data.size() > capacity_) throw Error("Received message exceeds the cache capacity");
    auto existing = std::find_if(items_.begin(), items_.end(), [&](const auto& item) {
        return item.message.local_id == stream.message.local_id;
    });
    if (existing != items_.end()) {
        used_ -= existing->message.data.size();
        items_.erase(existing);
    }
    while (!items_.empty() &&
           (stream.message.data.size() > capacity_ - used_ || items_.size() >= 4096)) {
        used_ -= items_.front().message.data.size();
        items_.pop_front();
    }
    used_ += stream.message.data.size();
    items_.push_back(std::move(stream));
}
void Inbox::clear() noexcept { items_.clear(); used_ = 0; }
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
std::optional<Bytes> signal_byte_prefix(const SignalLine& line) {
    if (!signal_byte_aligned(line) || line.text.empty() || line.text.size()>line.received_bits ||
        line.text.find_first_not_of("01")!=std::string::npos) return {};
    Bytes bytes(line.text.size()/8);
    for(std::size_t i=0;i<bytes.size()*8;++i)
        bytes[i/8]|=static_cast<std::uint8_t>((line.text[i]-'0')<<(7-i%8));
    return bytes;
}
bool complete_bits(const SignalLine& line) {
    return line.binary && line.complete && line.received_bits &&
        (!line.expected_bits || line.received_bits==line.expected_bits) &&
        line.text.size()==line.received_bits && line.text.find_first_not_of("01")==std::string::npos;
}
}
bool signal_byte_aligned(const SignalLine& line) {
    return line.binary && line.complete && line.received_bits && line.received_bits%8==0;
}
std::string signal_display_text(const SignalLine& line) {
    if(const auto bytes=signal_byte_prefix(line)) return BinaryEditor(*bytes).text();
    if(!line.binary && line.complete && !line.validated && line.text_message && line.pattern_score)
        return BinaryEditor(Bytes(line.text.begin(),line.text.end())).text();
    return line.text;
}
std::string signal_status_label(const SignalLine& line) {
    if (signal_byte_aligned(line)) return "text received";
    if (line.binary) return line.complete?"binary received":"binary pending";
    if(line.complete && line.pattern_score && !line.validated)return "text received";
    return line.validated?(line.text_message?"decoded":"decoded bytes"):"pending";
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

void Signals::update(SignalLine line) {
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
        if ((found->validated && !line.validated) || ((found->binary || found->pattern_score) && found->complete && !line.complete)) return;
        *found=std::move(line);
    } else {
        if (lines_.size()>=64) lines_.pop_front();
        lines_.push_back(std::move(line));
    }
}
std::optional<std::string> Signals::copy_id(std::size_t index) const {
    if (index>=lines_.size() || lines_[index].binary || !lines_[index].validated || !lines_[index].text_message || lines_[index].reception_id.empty()) return std::nullopt;
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
    if (complete_bits(line)) return line.text;
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
