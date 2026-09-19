#include "datapump/legacy/modem.hpp"
#include <cmath>
#include <stdexcept>
namespace datapump::legacy {
void validate(const Config& c) {
    if(c.mode!=Mode::bpsk31&&c.mode!=Mode::bpsk125&&c.mode!=Mode::olivia4_2000)
        throw std::invalid_argument("Unknown Legacy modulation");
    const double half=c.mode==Mode::olivia4_2000?1000:(c.mode==Mode::bpsk125?250:62.5);
    if(!std::isfinite(c.carrier_hz)||c.carrier_hz<half||c.carrier_hz+half>3900)
        throw std::invalid_argument("Carrier must keep the Legacy signal between 0 and 3900 Hz");
}
std::string_view mode_name(Mode mode) {
    switch(mode) {
    case Mode::olivia4_2000:return "Olivia-4/2k";
    case Mode::bpsk31:return "BPSK31";
    case Mode::bpsk125:return "BPSK125";
    }
    throw std::invalid_argument("Unknown Legacy modulation");
}
struct Transmitter::Impl {std::unique_ptr<detail::WaveTransmitter> wave;};
Transmitter::Transmitter(Config c,std::string text,TextCallback sent):impl_(std::make_unique<Impl>()) {
    validate(c);
    if(text.empty()||text.size()>encoded_text_byte_limit)
        throw std::invalid_argument("Legacy codec text must contain 1 to 32772 bytes including session line breaks");
    // The supported wire alphabets are byte oriented. NUL is the idle symbol,
    // not printable text, and cannot be submitted as a hidden binary payload.
    if(text.find('\0')!=std::string::npos)throw std::invalid_argument("Legacy text cannot contain NUL");
    if(c.mode==Mode::olivia4_2000)for(unsigned char byte:text)
        if(byte<8||byte==127)throw std::invalid_argument("Olivia reserves ASCII 0..7 and 127 for idle/control signaling");
    impl_->wave=c.mode==Mode::olivia4_2000?detail::olivia_transmitter(c,std::move(text),std::move(sent)):
        detail::psk_transmitter(c,std::move(text),std::move(sent));
}
Transmitter::~Transmitter()=default;
std::size_t Transmitter::read(std::span<float> out) {return impl_->wave->read(out);}
struct Receiver::Impl {Config config;TextCallback text;std::unique_ptr<detail::WaveReceiver> wave;
    void reset() {wave=config.mode==Mode::olivia4_2000?detail::olivia_receiver(config,text):detail::psk_receiver(config,text);}
};
Receiver::Receiver(Config c,TextCallback text):impl_(std::make_unique<Impl>()) {
    validate(c);impl_->config=c;impl_->text=std::move(text);impl_->reset();
}
Receiver::~Receiver()=default;
void Receiver::push(std::span<const float> samples){impl_->wave->push(samples);}
void Receiver::reset(){impl_->reset();}
}
