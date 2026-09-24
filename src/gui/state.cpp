#include "state.hpp"
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
std::optional<Bytes> selected_binary_bits(TransmitSource source, std::string_view text) {
    switch (source) {
    case TransmitSource::message_file: return std::nullopt;
    case TransmitSource::binary: return parse_binary_bits(text);
    }
    throw Error("Select Message / File or Binary input");
}

Inbox::Inbox(std::size_t capacity) : capacity_(capacity) {
    if (!capacity) throw Error("Receive cache capacity must be positive");
}
void Inbox::put(DecodedPacket packet) {
    if (packet.message.data.size() > capacity_) throw Error("Received message exceeds the cache capacity");
    auto existing = std::find_if(items_.begin(), items_.end(), [&](const auto& item) {
        return item.message.id == packet.message.id;
    });
    if (existing != items_.end()) {
        used_ -= existing->message.data.size();
        items_.erase(existing);
    }
    while (!items_.empty() &&
           (packet.message.data.size() > capacity_ - used_ || items_.size() >= 4096)) {
        used_ -= items_.front().message.data.size();
        items_.pop_front();
    }
    used_ += packet.message.data.size();
    items_.push_back(std::move(packet));
}
void Inbox::clear() noexcept { items_.clear(); used_ = 0; }
std::vector<const DecodedPacket*> Inbox::file_items() const {
    std::vector<const DecodedPacket*> files;
    for (const auto& packet : items_) {
        if (packet.message.kind == MessageKind::file || packet.message.kind == MessageKind::screenshot)
            files.push_back(&packet);
    }
    return files;
}

void TransmissionPolicy::started(bool simulation, bool encrypted, Clock::time_point now) {
    if (remaining(simulation, encrypted, now).count() > 0)
        throw Error(active_ ? "A transmission is already active" : "The encrypted transmit cooldown is still active");
    active_ = true;
    active_encrypted_output_ = !simulation && encrypted;
}
void TransmissionPolicy::finished(Clock::time_point now) noexcept {
    if (active_ && active_encrypted_output_) next_encrypted_ = now + std::chrono::seconds(6);
    abort_start();
}
void TransmissionPolicy::abort_start() noexcept { active_ = false; active_encrypted_output_ = false; }
std::chrono::milliseconds TransmissionPolicy::remaining(bool simulation, bool encrypted, Clock::time_point now) const noexcept {
    if (active_) return std::chrono::milliseconds::max();
    if (simulation || !encrypted || now >= next_encrypted_) return std::chrono::milliseconds::zero();
    return std::chrono::ceil<std::chrono::milliseconds>(next_encrypted_ - now);
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

std::string signal_status_label(const SignalLine& line) {
    if (line.binary) return line.complete?"binary received":"binary pending";
    return line.validated?(line.text_message?"verified":"verified file"):"pending";
}
std::string signal_preamble_label(const SignalLine& line) {
    if (line.binary) return "Preamble none";
    const auto value=line.preamble_received_percent;
    if (!value || !std::isfinite(*value) || *value<0 || *value>100) return "Preamble --";
    if (*value>99.9 && *value<100) return "Preamble >99.9%";
    std::ostringstream text; text.imbue(std::locale::classic());
    text<<"Preamble "<<std::fixed<<std::setprecision(1)<<*value<<'%';
    return text.str();
}
std::string signal_data_label(const SignalLine& line) {
    if (line.binary) return line.received_bits>line.text.size()?"FEC off / prefix":"FEC off";
    if (!line.validated) return "Data pre-FEC pending";
    const auto& accuracy=line.pre_fec_accuracy;
    if (!accuracy || !accuracy->received_data_bits || accuracy->corrected_data_bits>accuracy->received_data_bits)
        return "Data pre-FEC --";
    if (!accuracy->corrected_data_bits) return "Data 100% pre-FEC";
    const auto percent=100.*(1.-static_cast<double>(accuracy->corrected_data_bits)/static_cast<double>(accuracy->received_data_bits));
    // A small error in a large file must never round to a perfect reception.
    if (percent>99.99) return "Data >99.99% pre-FEC";
    std::ostringstream text; text.imbue(std::locale::classic());
    text<<"Data "<<std::fixed<<std::setprecision(2)<<percent<<"% pre-FEC";
    return text.str();
}

void Signals::update(SignalLine line) {
    if (line.binary) {
        line.validated=false; line.packet_id.clear(); line.text_message=false;
        line.preamble_received_percent.reset(); line.pre_fec_accuracy.reset();
    }
    if (line.text.size()>4096) line.text.resize(4096);
    const auto found=std::find_if(lines_.begin(),lines_.end(),[&](const auto& item) { return item.id==line.id; });
    if (found!=lines_.end()) {
        if ((found->validated && !line.validated) || (found->binary && found->complete && !line.complete)) return;
        *found=std::move(line);
    } else {
        if (lines_.size()>=64) lines_.pop_front();
        lines_.push_back(std::move(line));
    }
}
std::optional<std::string> Signals::copy_id(std::size_t index) const {
    if (index>=lines_.size() || lines_[index].binary || !lines_[index].validated || !lines_[index].text_message || lines_[index].packet_id.empty()) return std::nullopt;
    return lines_[index].packet_id;
}
std::optional<std::string> Signals::copy_bits(std::size_t index) const {
    if (index>=lines_.size()) return std::nullopt;
    const auto& line=lines_[index];
    if (!line.binary || !line.complete || !line.received_bits || line.received_bits!=line.expected_bits ||
        line.text.size()!=line.received_bits || line.text.find_first_not_of("01")!=std::string::npos) return std::nullopt;
    return line.text;
}

std::string id_label(const Message& message) {
    constexpr char digits[]="0123456789abcdef";
    std::string result;
    for (auto value:message.id) { result+=digits[value>>4]; result+=digits[value&15]; }
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
