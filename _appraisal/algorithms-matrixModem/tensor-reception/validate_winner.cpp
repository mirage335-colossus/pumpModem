#include "fixture.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace {

struct Key {
    int bits;
    int words;
    int starts;
    int frequencies;
    std::string channel;
    double cn0;
    int history;
    bool operator<(const Key& other) const {
        return std::tie(bits, words, starts, frequencies, channel, cn0, history) <
               std::tie(other.bits, other.words, other.starts, other.frequencies,
                        other.channel, other.cn0, other.history);
    }
};
struct Counts { unsigned candidates = 0, retained = 0; };

std::string field(const std::map<std::string, std::string>& values, const char* key) {
    const auto it = values.find(key);
    if (it == values.end()) throw std::runtime_error(std::string("missing field: ") + key);
    return it->second;
}

matrix_rx::Channel parse_channel(const std::string& value) {
    using matrix_rx::Channel;
    if (value == "flat") return Channel::flat;
    if (value == "moderate") return Channel::moderate;
    if (value == "disturbed") return Channel::disturbed;
    if (value == "holdout") return Channel::holdout;
    if (value == "noise") return Channel::noise;
    if (value == "interferer") return Channel::interferer;
    if (value == "wrong_word") return Channel::wrong_word;
    throw std::runtime_error("unknown channel: " + value);
}

// Stable log(cosh(x)); this is the exact per-feature normalization in the
// requested log-likelihood for the already selected candidate.
double log_cosh(double x) {
    const double ax = std::abs(x);
    return ax + std::log1p(std::exp(-2.0 * ax)) - std::log(2.0);
}

int run() {
    std::map<std::string, std::string> current;
    bool have_shape = false;
    std::map<Key, Counts> grouped;
    std::string line;
    unsigned processed = 0;
    while (std::getline(std::cin, line)) {
        std::istringstream input(line);
        std::string kind;
        if (!(input >> kind)) continue;
        std::map<std::string, std::string> values;
        std::string token;
        while (input >> token) {
            const auto split = token.find('=');
            if (split != std::string::npos)
                values.emplace(token.substr(0, split), token.substr(split + 1));
        }
        if (kind == "SHAPE") {
            current = std::move(values);
            have_shape = true;
            continue;
        }
        if (kind != "TRIAL") continue;
        if (!have_shape) throw std::runtime_error("TRIAL encountered before SHAPE");
        if (field(values, "route") != "fp16" || field(values, "decision") != "present") continue;

        using namespace matrix_rx;
        const Shape shape{std::stoi(field(current, "bits")), std::stoi(field(current, "words")),
                          std::stoi(field(current, "starts")), std::stoi(field(current, "frequencies"))};
        validate_shape(shape);
        const std::string channel_name = field(values, "channel");
        const auto channel = parse_channel(channel_name);
        const double cn0 = std::stod(field(values, "cn0"));
        const auto seed = static_cast<std::uint64_t>(std::stoull(field(values, "seed")));
        const int history = std::stoi(field(values, "history"));
        const int row = std::stoi(field(values, "row"));
        const int word = std::stoi(field(values, "word"));
        if (history < 0 || history > 1 || row < 0 || row >= shape.rows() || word < 0 || word >= shape.words)
            throw std::runtime_error("invalid recorded winner coordinates");

        const Scene scene = render(shape, channel, cn0, seed);
        const Evidence evidence = reference_row(shape, scene.input, row, history != 0);
        double exact = 0.0;
        for (int bit = 0; bit < shape.bits; ++bit) {
            const double half_difference = 0.5 * evidence.contrast[static_cast<std::size_t>(bit)];
            exact += code_sign(word, bit) * half_difference - log_cosh(half_difference);
        }
        const double threshold = std::log(static_cast<double>(shape.rows()) * shape.words /
                                          noise_false_alarm);
        const bool retained = exact > threshold;
        auto& counts = grouped[{shape.bits, shape.words, shape.starts, shape.frequencies,
                                channel_name, cn0, history}];
        ++counts.candidates;
        counts.retained += retained ? 1U : 0U;
        ++processed;
        std::cout << "CHECK bits=" << shape.bits << " channel=" << channel_name << " cn0=" << cn0
                  << " seed=" << seed << " history=" << history << " row=" << row << " word=" << word
                  << " exact_llr=" << exact << " threshold=" << threshold
                  << " retained=" << (retained ? 1 : 0) << '\n';
    }
    if (!processed) throw std::runtime_error("no fp16 present TRIAL records found");
    for (const auto& item : grouped) {
        const auto& key = item.first;
        const auto& count = item.second;
        std::cout << "SUMMARY bits=" << key.bits << " words=" << key.words
                  << " starts=" << key.starts << " frequencies=" << key.frequencies
                  << " channel=" << key.channel << " cn0=" << key.cn0 << " history=" << key.history
                  << " candidates=" << count.candidates << " retained=" << count.retained << '\n';
    }
    std::cout << "TOTAL fp16_present_candidates=" << processed << '\n';
    return 0;
}

} // namespace

int main() {
    try {
        return run();
    } catch (const std::exception& error) {
        std::cerr << "validate_winner: " << error.what() << '\n';
        return 2;
    }
}
