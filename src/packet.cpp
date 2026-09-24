#include "datapump/packet.hpp"
#include "datapump/compression.hpp"
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <string_view>

namespace datapump {
namespace {
constexpr std::size_t tag_size = 32;
constexpr std::size_t body_metadata_size = 23; // ID, ID checksum, three one-byte lengths; followed by original-size ULEB128.
constexpr std::uint8_t compressed_flag = 1, authenticated_flag = 2, repeat_flag = 4;

struct Field {
    std::array<std::uint8_t, 512> exp{};
    std::array<std::uint8_t, 256> log{};
    Field() {
        unsigned value = 1;
        for (unsigned i = 0; i < 255; ++i) {
            exp[i] = static_cast<std::uint8_t>(value);
            log[value] = static_cast<std::uint8_t>(i);
            value <<= 1;
            if ((value & 256) != 0) value ^= 0x11d;
        }
        for (std::size_t i = 255; i < exp.size(); ++i) exp[i] = exp[i - 255];
    }
    std::uint8_t mul(std::uint8_t a, std::uint8_t b) const {
        return a == 0 || b == 0 ? 0 : exp[static_cast<unsigned>(log[a]) + log[b]];
    }
    std::uint8_t div(std::uint8_t a, std::uint8_t b) const {
        if (b == 0) throw Error("Reed-Solomon division by zero");
        return a == 0 ? 0 : exp[static_cast<unsigned>(log[a]) + 255 - log[b]];
    }
};
const Field gf;

std::uint8_t evaluate(const Bytes& polynomial, std::uint8_t x) {
    std::uint8_t value = 0;
    for (const auto coefficient : polynomial) value = gf.mul(value, x) ^ coefficient;
    return value;
}
Bytes syndromes(const Bytes& codeword, std::size_t parity) {
    Bytes result(parity);
    for (std::size_t i = 0; i < parity; ++i) result[i] = evaluate(codeword, gf.exp[i]);
    return result;
}
bool all_zero(const Bytes& bytes) {
    return std::all_of(bytes.begin(), bytes.end(), [](auto value) { return value == 0; });
}
std::size_t add_size(std::size_t a, std::size_t b) {
    if (b > std::numeric_limits<std::size_t>::max() - a) throw Error("Packet length overflow");
    return a + b;
}
std::size_t mul_size(std::size_t a, std::size_t b) {
    if (a != 0 && b > std::numeric_limits<std::size_t>::max() / a) throw Error("Packet length overflow");
    return a * b;
}
void put_integer(Bytes& bytes, std::size_t offset, std::uint64_t value, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) bytes[offset + count - 1 - i] = static_cast<std::uint8_t>(value >> (i * 8));
}
std::uint64_t integer(const Bytes& bytes, std::size_t offset, std::size_t count) {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < count; ++i) value = (value << 8) | bytes[offset + i];
    return value;
}
std::uint32_t crc32(const std::uint8_t* bytes, std::size_t count) {
    std::uint32_t crc = 0xffffffffU;
    for (std::size_t i = 0; i < count; ++i) {
        crc ^= bytes[i];
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return crc ^ 0xffffffffU;
}
std::uint32_t id_checksum(const std::array<std::uint8_t, 16>& id, bool repeatable) {
    std::array<std::uint8_t, 17> input{};
    std::copy(id.begin(), id.end(), input.begin());
    input.back() = repeatable ? 1 : 0;
    return crc32(input.data(), input.size());
}
std::uint16_t header_crc(const std::uint8_t* bytes,std::size_t count) {
    std::uint16_t crc=0xffff;
    for(std::size_t i=0;i<count;++i) {
        crc^=static_cast<std::uint16_t>(static_cast<unsigned>(bytes[i])<<8);
        for(unsigned bit=0;bit<8;++bit)
            crc=static_cast<std::uint16_t>((static_cast<unsigned>(crc)<<1)^((crc&0x8000)?0x1021U:0U));
    }
    return crc;
}
Bytes original_size_field(std::size_t value) {
    if(value>std::numeric_limits<std::uint32_t>::max())throw Error("Packet content exceeds 32-bit format limit");
    Bytes result;
    do { auto byte=static_cast<std::uint8_t>(value&127);value>>=7;result.push_back(static_cast<std::uint8_t>(byte|(value?128:0))); } while(value);
    return result;
}
Bytes digest(const Bytes& bytes) {
    Bytes result(tag_size);
    unsigned length = 0;
    if (EVP_Digest(bytes.data(), bytes.size(), result.data(), &length, EVP_sha256(), nullptr) != 1 || length != tag_size)
        throw Error("SHA-256 failed");
    return result;
}
void validate_kind(MessageKind kind) {
    if (kind != MessageKind::text && kind != MessageKind::file && kind != MessageKind::screenshot)
        throw Error("Unknown packet content kind");
}
void validate_fec(FecMode mode) {
    if (mode != FecMode::off && mode != FecMode::rs20 && mode != FecMode::rs60)
        throw Error("Unknown Reed-Solomon mode");
}
bool valid_utf8(std::string_view value) {
    for (std::size_t offset = 0; offset < value.size();) {
        const auto first = static_cast<std::uint8_t>(value[offset++]);
        if (first < 0x80) continue;
        std::uint32_t codepoint = 0, minimum = 0;
        std::size_t continuation = 0;
        if (first >= 0xc2 && first <= 0xdf) { continuation = 1; codepoint = first & 0x1fU; minimum = 0x80; }
        else if (first >= 0xe0 && first <= 0xef) { continuation = 2; codepoint = first & 0x0fU; minimum = 0x800; }
        else if (first >= 0xf0 && first <= 0xf4) { continuation = 3; codepoint = first & 0x07U; minimum = 0x10000; }
        else return false;
        if (continuation > value.size() - offset) return false;
        for (std::size_t i = 0; i < continuation; ++i) {
            const auto byte = static_cast<std::uint8_t>(value[offset++]);
            if ((byte & 0xc0U) != 0x80U) return false;
            codepoint = (codepoint << 6) | (byte & 0x3fU);
        }
        if (codepoint < minimum || codepoint > 0x10ffff || (codepoint >= 0xd800 && codepoint <= 0xdfff)) return false;
    }
    return true;
}
void validate_metadata(const Message& message) {
    validate_kind(message.kind);
    if ((!message.filename.empty() || message.kind != MessageKind::text) && !safe_filename(message.filename))
        throw Error("Unsafe attachment filename");
    const auto clean = [](const std::string& value, std::size_t maximum) {
        return value.size() <= maximum && valid_utf8(value) && std::none_of(value.begin(), value.end(), [](unsigned char byte) {
            return byte < 32 || byte == 127;
        });
    };
    if (!clean(message.callsign, 64) || !clean(message.grid, 32)) throw Error("Invalid callsign or grid metadata");
}
std::size_t block_capacity(FecMode mode) { return mode == FecMode::rs20 ? 210 : 150; }
std::size_t parity_count(std::size_t count, FecMode mode) {
    if (mode == FecMode::off || count == 0) return 0;
    const std::size_t parity = (count * (mode == FecMode::rs20 ? 1 : 3) + 4) / 5;
    return (parity + 1) & ~std::size_t{1};
}
std::size_t coded_size(std::size_t length, FecMode mode) {
    if (mode == FecMode::off) return length;
    const auto capacity = block_capacity(mode);
    const auto whole = length / capacity;
    const auto remainder = length % capacity;
    return add_size(mul_size(whole, capacity + parity_count(capacity, mode)), remainder + parity_count(remainder, mode));
}

std::optional<std::size_t> try_rs_correct(Bytes& codeword,std::size_t parity_symbols);

struct Header {
    Bytes bytes;
    FecMode fec = FecMode::off;
    std::uint8_t flags = 0;
    MessageKind kind = MessageKind::text;
    std::size_t body_length = 0, wire_length = 0, corrected = 0,extent=0,parity=0;
};
std::size_t decoder_working(const Header& header,std::size_t original=0) {
    return add_size(add_size(coded_size(header.body_length,header.fec),mul_size(header.body_length,2)),add_size(original,4096));
}
std::optional<Header> try_header(const Bytes& wire,std::size_t max_memory,bool strict=true) {
    std::optional<Header> best;bool ambiguous=false;
    for(std::size_t size=packet_min_prefix_size;size<=packet_header_size;++size)
        for(const auto mode:{FecMode::off,FecMode::rs20,FecMode::rs60}) {
            const auto parity=parity_count(size,mode),extent=size+parity;
            if(wire.size()<extent)continue;
            // Every contradicted systematic byte needs at least one RS
            // correction. Count bytes, not individual invalid bits: one
            // damaged control byte may violate several rules at once. CRC
            // disagreement is deliberately excluded because the same data
            // error can cause it without any damaged CRC byte.
            std::size_t contradictions=(wire[0]&128) || (wire[0]&3)!=static_cast<unsigned>(mode) || ((wire[0]>>2)&3)==3;
            for(std::size_t i=1;i<size-2 && contradictions<=parity/2;++i) {
                const auto byte=wire[i];const bool last=i==size-3;
                contradictions+=(i==5 && byte>15) || ((byte&128)!=0)==last || (last && i>1 && byte==0);
            }
            if(contradictions>parity/2)continue;
            try {
                Header candidate;candidate.fec=mode;candidate.extent=extent;candidate.parity=parity;
                candidate.bytes.assign(wire.begin(),wire.begin()+static_cast<std::ptrdiff_t>(extent));
                if(parity) {
                    const auto corrected=try_rs_correct(candidate.bytes,parity);
                    if(!corrected)continue;
                    candidate.corrected=*corrected;
                }
                candidate.bytes.resize(size);const auto& bytes=candidate.bytes;
                if((bytes[0]&128) || (bytes[0]&3)!=static_cast<unsigned>(mode) ||
                   integer(bytes,size-2,2)!=header_crc(bytes.data(),size-2))continue;
                candidate.kind=static_cast<MessageKind>((bytes[0]>>2)&3);
                if(candidate.kind>MessageKind::screenshot)continue;
                candidate.flags=static_cast<std::uint8_t>((bytes[0]>>4)&7);
                std::uint32_t length=0;bool canonical=true;
                for(std::size_t i=1;i<size-2;++i) {
                    const auto byte=bytes[i];const bool last=i==size-3;
                    if((i==5 && byte>15) || ((byte&128)!=0)==last || (last && i>1 && byte==0)){canonical=false;break;}
                    length|=static_cast<std::uint32_t>(byte&127)<<(7*(i-1));
                }
                if(!canonical || length<body_metadata_size+1+tag_size)continue;
                candidate.body_length=length;
                candidate.wire_length=add_size(extent,coded_size(length,mode));
                if(decoder_working(candidate)>max_memory)continue;
                if(!best || candidate.corrected<best->corrected){best=std::move(candidate);ambiguous=false;}
                else if(candidate.corrected==best->corrected && candidate.bytes!=best->bytes)ambiguous=true;
            }catch(const Error&) {}
        }
    if(best && !ambiguous)return best;
    if(!strict || wire.size()<packet_prefix_size)return {};
    throw Error(ambiguous?"Ambiguous packet bootstrap":"Invalid or oversized packet bootstrap");
}
Header read_header(const Bytes& wire,std::size_t max_memory) {
    auto header=try_header(wire,max_memory);
    if(!header)throw Error("Incomplete packet bootstrap");
    return std::move(*header);
}
struct BodyMetadata {
    std::size_t original=0,filename=0,callsign=0,grid=0,strings_offset=0,payload_offset=0,payload_length=0;
};
BodyMetadata body_metadata(const Bytes& prefix,const Header& header,std::size_t max_memory) {
    if(prefix.size()<body_metadata_size+1)throw Error("Incomplete packet metadata");
    BodyMetadata result;
    result.filename=prefix[20];result.callsign=prefix[21];result.grid=prefix[22];
    if(result.callsign>64 || result.grid>32)throw Error("Invalid packet metadata lengths");
    std::uint32_t original=0;
    std::size_t position=body_metadata_size;
    for(unsigned i=0;;++i) {
        if(i==5 || position>=std::min(prefix.size(),header.body_length-tag_size))throw Error("Invalid or incomplete original-size field");
        const auto byte=prefix[position++];
        if(i==4 && byte>15)throw Error("Packet original length overflow");
        original|=static_cast<std::uint32_t>(byte&127)<<(7*i);
        if(!(byte&128)) { if(i && byte==0)throw Error("Noncanonical original-size field");break; }
    }
    result.original=original;result.strings_offset=position;
    if(result.original<16 && header.fec!=FecMode::off)throw Error("Tiny packets must not contain Reed-Solomon parity");
    result.payload_offset=add_size(position,result.filename+result.callsign+result.grid);
    if(result.payload_offset>header.body_length-tag_size)throw Error("Invalid packet metadata lengths");
    result.payload_length=header.body_length-tag_size-result.payload_offset;
    if(header.flags&compressed_flag) {
        if(!result.original || result.payload_length>=result.original)throw Error("Invalid compression lengths");
    } else if(result.payload_length!=result.original)throw Error("Inconsistent packet payload lengths");
    if(result.original>max_memory || decoder_working(header,result.original)>max_memory)
        throw Error("Packet exceeds decoder working memory limit");
    return result;
}
void set_metadata(Message& message,const Bytes& body,const Header& header,const BodyMetadata& metadata) {
    if(body.size()<metadata.payload_offset)throw Error("Incomplete packet metadata strings");
    message.kind=header.kind;message.repeatable=(header.flags&repeat_flag)!=0;
    std::copy_n(body.begin(),message.id.size(),message.id.begin());
    if(integer(body,16,4)!=id_checksum(message.id,message.repeatable))throw Error("Packet identifier checksum failed");
    const auto string_at=[&](std::size_t offset,std::size_t length) {
        return std::string(body.begin()+static_cast<std::ptrdiff_t>(offset),body.begin()+static_cast<std::ptrdiff_t>(offset+length));
    };
    message.filename=string_at(metadata.strings_offset,metadata.filename);
    message.callsign=string_at(metadata.strings_offset+metadata.filename,metadata.callsign);
    message.grid=string_at(metadata.strings_offset+metadata.filename+metadata.callsign,metadata.grid);
    validate_metadata(message);
}
// Read only systematic bytes needed for a preview/structure; no body allocation
// from an untrusted declared length and no assumption of padded final RS rows.
Bytes systematic_prefix(const Bytes& wire,const Header& header,std::size_t limit) {
    limit=std::min(limit,header.body_length-tag_size);
    Bytes result;result.reserve(limit);
    const auto available=std::min(wire.size(),header.wire_length)-header.extent;
    if(header.fec==FecMode::off) {
        const auto count=std::min(limit,available);
        result.insert(result.end(),wire.begin()+static_cast<std::ptrdiff_t>(header.extent),wire.begin()+static_cast<std::ptrdiff_t>(header.extent+count));
    } else {
        const auto capacity=block_capacity(header.fec),rows=(header.body_length+capacity-1)/capacity;
        const auto last=header.body_length-(rows-1)*capacity,last_width=last+parity_count(last,header.fec);
        for(std::size_t i=0;i<limit;++i) {
            const auto row=i/capacity,column=i%capacity;
            const auto position=column<last_width?column*rows+row:last_width*rows+(column-last_width)*(rows-1)+row;
            if(position>=available)break;
            result.push_back(wire[header.extent+position]);
        }
    }
    return result;
}
Bytes encode_body(const Bytes& body, FecMode mode) {
    if (mode == FecMode::off) return body;
    const auto capacity = block_capacity(mode);
    const auto block_count = (body.size() + capacity - 1) / capacity;
    const auto full_width = capacity + parity_count(capacity, mode);
    Bytes rows;
    rows.reserve(coded_size(body.size(), mode));
    for (std::size_t offset = 0; offset < body.size(); offset += capacity) {
        const auto count = std::min(capacity, body.size() - offset);
        const Bytes chunk(body.begin() + static_cast<std::ptrdiff_t>(offset), body.begin() + static_cast<std::ptrdiff_t>(offset + count));
        const auto encoded = packet_codec::rs_encode(chunk, parity_count(count, mode));
        rows.insert(rows.end(), encoded.begin(), encoded.end());
    }
    Bytes result;
    result.reserve(rows.size());
    // Transmit columns: consecutive channel errors spread across RS codewords.
    for (std::size_t column = 0; column < full_width; ++column)
        for (std::size_t row = 0; row < block_count; ++row) {
            const auto index = row * full_width + column;
            if (index < rows.size()) result.push_back(rows[index]);
        }
    return result;
}
Bytes decode_body(const Bytes& wire, const Header& header, std::size_t& corrected,
                  std::uint64_t& corrected_data_bits) {
    if (header.fec == FecMode::off)
        return Bytes(wire.begin() + static_cast<std::ptrdiff_t>(header.extent), wire.begin() + static_cast<std::ptrdiff_t>(header.wire_length));
    const auto capacity = block_capacity(header.fec);
    const auto block_count = (header.body_length + capacity - 1) / capacity;
    const auto full_width = capacity + parity_count(capacity, header.fec);
    Bytes rows(header.wire_length - header.extent);
    std::size_t source = header.extent;
    for (std::size_t column = 0; column < full_width; ++column)
        for (std::size_t row = 0; row < block_count; ++row) {
            const auto index = row * full_width + column;
            if (index < rows.size()) rows[index] = wire[source++];
        }
    Bytes result;
    result.reserve(header.body_length);
    for (std::size_t row = 0; row < block_count; ++row) {
        const auto count = std::min(capacity, header.body_length - row * capacity);
        const auto start = row * full_width;
        const auto parity = parity_count(count, header.fec);
        Bytes block(rows.begin() + static_cast<std::ptrdiff_t>(start), rows.begin() + static_cast<std::ptrdiff_t>(start + count + parity));
        const auto repaired=packet_codec::rs_correct(block, parity);
        corrected += repaired;
        // rows retains the received, deinterleaved bytes. RS is systematic:
        // only the first count bytes carry encoded body data. Parity repairs
        // still contribute to corrected_bytes, but never to data-bit accuracy.
        if(repaired)
            for(std::size_t i=0;i<count;++i)
                corrected_data_bits+=static_cast<std::uint64_t>(
                    std::popcount(static_cast<unsigned>(rows[start+i]^block[i])));
        result.insert(result.end(), block.begin(), block.begin() + static_cast<std::ptrdiff_t>(count));
    }
    return result;
}


}

namespace packet_codec {
Bytes rs_encode(const Bytes& data, std::size_t parity_symbols) {
    if (data.empty() || parity_symbols == 0 || parity_symbols >= 255 || data.size() > 255 - parity_symbols)
        throw Error("Invalid Reed-Solomon block dimensions");
    Bytes generator{1};
    for (std::size_t root = 0; root < parity_symbols; ++root) {
        Bytes next(generator.size() + 1);
        for (std::size_t i = 0; i < generator.size(); ++i) {
            next[i] ^= generator[i];
            next[i + 1] ^= gf.mul(generator[i], gf.exp[root]);
        }
        generator = std::move(next);
    }
    // Allocate the complete shortened codeword once. The parity region starts
    // at zero; the data region is the polynomial division input.
    Bytes result(data.size() + parity_symbols, 0);
    std::copy(data.begin(), data.end(), result.begin());
    for (std::size_t i = 0; i < data.size(); ++i) {
        const auto coefficient = result[i];
        for (std::size_t j = 1; j < generator.size(); ++j) result[i + j] ^= gf.mul(generator[j], coefficient);
    }
    std::copy(data.begin(), data.end(), result.begin());
    return result;
}

 } // namespace packet_codec
namespace {
std::optional<std::size_t> try_rs_correct(Bytes& codeword, std::size_t parity_symbols) {
    if (parity_symbols == 0 || parity_symbols >= codeword.size() || codeword.size() > 255)
        return {};
    const auto syndrome = syndromes(codeword, parity_symbols);
    if (all_zero(syndrome)) return 0;
    // Berlekamp-Massey: locator coefficients are ordered by increasing power.
    Bytes locator(parity_symbols + 1), previous(parity_symbols + 1);
    locator[0] = previous[0] = 1;
    std::size_t degree = 0, shift = 1;
    std::uint8_t previous_discrepancy = 1;
    for (std::size_t step = 0; step < parity_symbols; ++step) {
        auto discrepancy = syndrome[step];
        for (std::size_t i = 1; i <= degree; ++i) discrepancy ^= gf.mul(locator[i], syndrome[step - i]);
        if (discrepancy == 0) { ++shift; continue; }
        const auto saved = locator;
        const auto factor = gf.div(discrepancy, previous_discrepancy);
        for (std::size_t i = 0; i + shift < locator.size(); ++i) locator[i + shift] ^= gf.mul(factor, previous[i]);
        if (2 * degree <= step) {
            degree = step + 1 - degree;
            previous = saved;
            previous_discrepancy = discrepancy;
            shift = 1;
        } else ++shift;
    }
    if (degree == 0 || degree * 2 > parity_symbols) return {};
    std::vector<std::size_t> positions;
    Bytes locations;
    for (std::size_t position = 0; position < codeword.size(); ++position) {
        const auto exponent = codeword.size() - 1 - position;
        const auto inverse = gf.exp[255 - exponent];
        std::uint8_t value = locator[degree];
        for (std::size_t i = degree; i > 0; --i) value = gf.mul(value, inverse) ^ locator[i - 1];
        if (value == 0) {
            positions.push_back(position);
            locations.push_back(gf.exp[exponent]);
        }
    }
    if (positions.size() != degree) return {};
    // Solve the small Vandermonde system for error magnitudes. This avoids
    // convention-sensitive Forney offsets and also covers shortened codewords.
    std::vector<Bytes> matrix(degree, Bytes(degree + 1));
    for (std::size_t column = 0; column < degree; ++column) {
        std::uint8_t power = 1;
        for (std::size_t row = 0; row < degree; ++row) {
            matrix[row][column] = power;
            power = gf.mul(power, locations[column]);
        }
    }
    for (std::size_t row = 0; row < degree; ++row) matrix[row][degree] = syndrome[row];
    for (std::size_t column = 0; column < degree; ++column) {
        auto pivot = column;
        while (pivot < degree && matrix[pivot][column] == 0) ++pivot;
        if (pivot == degree) return {};
        std::swap(matrix[column], matrix[pivot]);
        const auto scale = matrix[column][column];
        for (std::size_t i = column; i <= degree; ++i) matrix[column][i] = gf.div(matrix[column][i], scale);
        for (std::size_t row = 0; row < degree; ++row) {
            if (row == column) continue;
            const auto factor = matrix[row][column];
            for (std::size_t i = column; i <= degree; ++i) matrix[row][i] ^= gf.mul(factor, matrix[column][i]);
        }
    }
    auto repaired = codeword;
    for (std::size_t i = 0; i < degree; ++i) repaired[positions[i]] ^= matrix[i][degree];
    if (!all_zero(syndromes(repaired, parity_symbols))) return {};
    codeword = std::move(repaired);
    return degree;
}
} // namespace
namespace packet_codec {
std::size_t rs_correct(Bytes& codeword,std::size_t parity_symbols) {
    if(parity_symbols==0 || parity_symbols>=codeword.size() || codeword.size()>255)
        throw Error("Invalid Reed-Solomon block dimensions");
    const auto result=try_rs_correct(codeword,parity_symbols);
    if(!result)throw Error("Uncorrectable Reed-Solomon block");
    return *result;
}



}

bool safe_filename(const std::string& name) {
    if (name.empty() || name.size() > 255 || !valid_utf8(name) || name == "." || name == ".." || name.back() == '.' || name.back() == ' ') return false;
    for (const unsigned char byte : name)
        if (byte < 32 || byte == 127 || byte == '/' || byte == '\\' || byte == ':' || byte == '<' || byte == '>' || byte == '"' || byte == '|' || byte == '?' || byte == '*')
            return false;
    auto stem = name.substr(0, name.find('.'));
    while (!stem.empty() && stem.back() == ' ') stem.pop_back();
    for (auto& character : stem) if (character >= 'a' && character <= 'z') character = static_cast<char>(character - 'a' + 'A');
    if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL") return false;
    if (stem.starts_with("COM") || stem.starts_with("LPT")) {
        const auto suffix = stem.substr(3);
        if ((suffix.size() == 1 && suffix[0] >= '1' && suffix[0] <= '9') ||
            suffix == "\xc2\xb9" || suffix == "\xc2\xb2" || suffix == "\xc2\xb3" ||
            suffix == "\xb9" || suffix == "\xb2" || suffix == "\xb3") return false;
    }
    return true;
}

Bytes encode_packet(const Message& message, const PacketOptions& options, std::size_t max_memory) {
    validate_metadata(message);validate_fec(options.fec);
    const auto fec=message.data.size()<16?FecMode::off:options.fec;
    const auto size_field=original_size_field(message.data.size());
    const auto metadata_size=body_metadata_size+size_field.size()+message.filename.size()+message.callsign.size()+message.grid.size();
    const auto maximum_body=add_size(add_size(metadata_size,message.data.size()),tag_size);
    const auto maximum_coded=coded_size(maximum_body,fec);
    const auto working=add_size(add_size(mul_size(maximum_coded,2),mul_size(maximum_body,2)),add_size(message.data.size(),4096));
    if(working>max_memory)throw Error("Packet exceeds encoder working memory limit");
    Bytes payload=message.data;
    std::uint8_t flags=message.repeatable?repeat_flag:0;
    if(options.compression && !payload.empty()) {
        if(payload.size()<256) {
            auto compressed=compression::encode_short(payload);
            if(compressed.size()<payload.size()){payload=std::move(compressed);flags|=compressed_flag;}
        } else if(auto compressed=compression::encode_long(payload)) {
            payload=std::move(*compressed);flags|=compressed_flag;
        }
    }
    if(options.authenticator)flags|=authenticated_flag;
    auto identifier=message.id;
    if(std::all_of(identifier.begin(),identifier.end(),[](auto byte){return byte==0;}) &&
       RAND_bytes(identifier.data(),static_cast<int>(identifier.size()))!=1)throw Error("Packet identifier generation failed");
    Bytes body(body_metadata_size);
    body.reserve(metadata_size+payload.size()+tag_size);
    std::copy(identifier.begin(),identifier.end(),body.begin());
    put_integer(body,16,id_checksum(identifier,message.repeatable),4);
    body[20]=static_cast<std::uint8_t>(message.filename.size());
    body[21]=static_cast<std::uint8_t>(message.callsign.size());
    body[22]=static_cast<std::uint8_t>(message.grid.size());
    body.insert(body.end(),size_field.begin(),size_field.end());
    body.insert(body.end(),message.filename.begin(),message.filename.end());
    body.insert(body.end(),message.callsign.begin(),message.callsign.end());
    body.insert(body.end(),message.grid.begin(),message.grid.end());
    body.insert(body.end(),payload.begin(),payload.end());
    payload=Bytes{};
    const auto body_length=add_size(body.size(),tag_size);
    if(body_length>std::numeric_limits<std::uint32_t>::max())throw Error("Packet body exceeds 32-bit format limit");
    Bytes header{static_cast<std::uint8_t>(static_cast<unsigned>(fec)|(static_cast<unsigned>(message.kind)<<2)|(flags<<4))};
    const auto body_size_field=original_size_field(body_length);
    header.insert(header.end(),body_size_field.begin(),body_size_field.end());
    header.resize(header.size()+2);
    put_integer(header,header.size()-2,header_crc(header.data(),header.size()-2),2);
    Bytes tag;
    {
        Bytes canonical;canonical.reserve(header.size()+body.size());
        canonical.insert(canonical.end(),header.begin(),header.end());
        canonical.insert(canonical.end(),body.begin(),body.end());
        tag=options.authenticator?options.authenticator(canonical):digest(canonical);
    }
    if(tag.size()!=tag_size)throw Error("Packet authenticator must contain 32 bytes");
    body.insert(body.end(),tag.begin(),tag.end());
    const auto parity=parity_count(header.size(),fec);
    auto result=parity?packet_codec::rs_encode(header,parity):header;
    auto coded=encode_body(body,fec);result.insert(result.end(),coded.begin(),coded.end());
    return result;
}

PacketLayout packet_layout(const Bytes& wire,std::size_t max_memory) {
    const auto header=read_header(wire,max_memory);
    if(wire.size()<header.wire_length)throw Error("Incomplete packet layout");
    PacketLayout result;
    const auto metadata=body_metadata(systematic_prefix(wire,header,body_metadata_size+5),header,max_memory);
    result.fec=header.fec;
    result.compressed=(header.flags&compressed_flag)!=0;result.authenticated=(header.flags&authenticated_flag)!=0;
    result.header_bytes=header.bytes.size();result.header_parity_bytes=header.parity;
    result.metadata_bytes=metadata.payload_offset;
    result.original_bytes=metadata.original;result.payload_bytes=metadata.payload_length;result.integrity_bytes=tag_size;
    result.body_bytes=header.body_length;result.wire_bytes=header.wire_length;
    result.body_parity_bytes=coded_size(header.body_length,header.fec)-header.body_length;
    if(header.fec!=FecMode::off) {
        result.block_capacity=block_capacity(header.fec);
        result.block_count=header.body_length/result.block_capacity+(header.body_length%result.block_capacity!=0);
        result.full_block_parity=parity_count(result.block_capacity,header.fec);
        result.last_block_data=header.body_length-(result.block_count-1)*result.block_capacity;
        result.last_block_parity=parity_count(result.last_block_data,header.fec);
    }
    return result;
}

PacketLayout packet_empty_layout(const Message& message,FecMode requested_fec) {
    validate_metadata(message);validate_fec(requested_fec);
    PacketLayout result;result.fec=message.data.size()<16?FecMode::off:requested_fec;
    result.metadata_bytes=body_metadata_size+1+message.filename.size()+message.callsign.size()+message.grid.size();
    result.integrity_bytes=tag_size;result.body_bytes=result.metadata_bytes+tag_size;
    result.body_parity_bytes=coded_size(result.body_bytes,result.fec)-result.body_bytes;
    result.header_bytes=1+original_size_field(result.body_bytes).size()+2;
    result.header_parity_bytes=parity_count(result.header_bytes,result.fec);
    result.wire_bytes=result.header_bytes+result.header_parity_bytes+result.body_bytes+result.body_parity_bytes;
    if(result.fec!=FecMode::off) {
        result.block_capacity=block_capacity(result.fec);
        result.block_count=result.body_bytes/result.block_capacity+(result.body_bytes%result.block_capacity!=0);
        result.full_block_parity=parity_count(result.block_capacity,result.fec);
        result.last_block_data=result.body_bytes-(result.block_count-1)*result.block_capacity;
        result.last_block_parity=parity_count(result.last_block_data,result.fec);
    }
    return result;
}

bool packet_bootstrap_possible(const Bytes& prefix,std::size_t max_memory) {
    if(prefix.size()<packet_prefix_size)return true;
    return try_header(prefix,max_memory,false).has_value();
}

std::optional<std::size_t> packet_frame_size(const Bytes& prefix, std::size_t max_memory) {
    const auto header=try_header(prefix,max_memory);
    return header?std::optional<std::size_t>(header->wire_length):std::nullopt;
}

std::optional<std::size_t> packet_probe_frame_size(const Bytes& prefix,std::size_t max_memory) {
    const auto header=try_header(prefix,max_memory,false);
    return header?std::optional<std::size_t>(header->wire_length):std::nullopt;
}

std::optional<std::size_t> packet_header_extent(const Bytes& prefix,std::size_t max_memory) {
    const auto header=try_header(prefix,max_memory);
    return header?std::optional<std::size_t>(header->extent):std::nullopt;
}

std::optional<PacketPreview> preview_packet_partial(const Bytes& wire,std::size_t max_memory) {
    if(wire.size()<packet_min_prefix_size)return std::nullopt;
    try {
        const auto header=read_header(wire,max_memory);
        const auto body=systematic_prefix(wire,header,65536+body_metadata_size+5+255+64+32);
        const auto metadata=body_metadata(body,header,max_memory);
        PacketPreview result;result.wire_size=header.wire_length;
        set_metadata(result.message,body,header,metadata);
        if(result.message.kind!=MessageKind::text)return result;
        const auto count=std::min(metadata.payload_length,body.size()-metadata.payload_offset);
        const auto payload=std::span<const std::uint8_t>(body).subspan(metadata.payload_offset,count);
        if(header.flags&compressed_flag)result.message.data=metadata.original<256?
            compression::preview_short(payload,metadata.original,65536):compression::preview_long(payload,metadata.original,65536);
        else result.message.data.assign(payload.begin(),payload.begin()+static_cast<std::ptrdiff_t>(std::min<std::size_t>(payload.size(),65536)));
        return result;
    } catch(const Error&) { return std::nullopt; }
}

DecodedPacket decode_packet(const Bytes& wire, const PacketOptions& options, std::size_t max_memory) {
    const auto header = read_header(wire, max_memory);
    if (wire.size() < header.wire_length) throw Error("Incomplete packet body");
    const bool authenticated = (header.flags & authenticated_flag) != 0;
    if (authenticated != static_cast<bool>(options.verifier)) throw Error("Packet authentication policy mismatch");
    if (!authenticated && options.authenticator) throw Error("Unauthenticated packet rejected by keyed receiver");
    DecodedPacket result;
    result.corrected_bytes = header.corrected;
    if(header.body_length>std::numeric_limits<std::uint64_t>::max()/8)
        throw Error("Packet data-bit count overflow");
    PacketBitAccuracy accuracy;
    accuracy.received_data_bits=static_cast<std::uint64_t>(header.body_length)*8;
    auto body = decode_body(wire, header, result.corrected_bytes, accuracy.corrected_data_bits);
    const auto content_end = body.size() - tag_size;
    const Bytes tag(body.begin() + static_cast<std::ptrdiff_t>(content_end), body.end());
    Bytes canonical = header.bytes;
    canonical.insert(canonical.end(), body.begin(), body.begin() + static_cast<std::ptrdiff_t>(content_end));
    if (authenticated) {
        if (!options.verifier(canonical, tag)) throw Error("Packet authentication failed");
    } else {
        const auto expected = digest(canonical);
        if (CRYPTO_memcmp(expected.data(), tag.data(), tag_size) != 0) throw Error("Packet integrity check failed");
    }
    const auto metadata=body_metadata(body,header,max_memory);
    auto& message=result.message;set_metadata(message,body,header,metadata);
    const auto payload=std::span<const std::uint8_t>(body).subspan(metadata.payload_offset,metadata.payload_length);
    if(header.flags&compressed_flag)message.data=metadata.original<256?
        compression::decode_short(payload,metadata.original,max_memory):compression::decode_long(payload,metadata.original,max_memory);
    else message.data.assign(payload.begin(),payload.end());
    result.consumed_bytes = header.wire_length;
    result.authenticated = authenticated;
    result.pre_fec_accuracy=accuracy;
    return result;
}
}
