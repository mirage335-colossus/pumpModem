#include "gpu.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace matrix_rx {
namespace {

using Clock = std::chrono::steady_clock;
constexpr Shape primary_shape{512, 32768, 64, 32};
constexpr Shape low_rate_shape{2048, 32768, 64, 32};

void cuda_check(cudaError_t result) {
    if (result != cudaSuccess) throw std::runtime_error(cudaGetErrorString(result));
}

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

enum class Decision { present, quiet, unresolved };
const char* name(Decision decision) {
    switch (decision) {
    case Decision::present: return "present";
    case Decision::quiet: return "quiet";
    case Decision::unresolved: return "unresolved";
    }
    throw std::logic_error("invalid decision");
}

double threshold(const Shape& shape) {
    return std::log(static_cast<double>(shape.rows()) * shape.words / noise_false_alarm);
}

double quiet_limit(const Shape& shape) {
    const double mean = 4.0 * shape.bits;
    const double trial = std::log(shape.rows() / noise_false_alarm);
    return mean + std::sqrt(2.0 * mean * trial) + trial;
}

struct Observation {
    Winner winner;
    Decision decision;
    double power;
    double front_ms;
    double score_ms;
    double total_ms;
};

double milliseconds(Clock::time_point start, Clock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

class Receiver {
public:
    explicit Receiver(Shape geometry, int tile_words = 0)
        : shape(geometry), front(shape), search(shape, tile_words), power(shape.rows()) {}

    Observation receive(const Scene& scene, bool history, Route route) {
        const auto start = Clock::now();
        auto evidence = front.analyze(scene.input, history);
        cuda_check(cudaDeviceSynchronize());
        const auto prepared = Clock::now();
        search.search(evidence, route);
        auto winner = search.winner();
        const auto scored = Clock::now();
        require(std::isfinite(winner.score) && std::isfinite(winner.error_bound)
                    && winner.error_bound >= 0.0f && winner.row >= 0
                    && winner.row < shape.rows() && winner.word >= 0
                    && winner.word < shape.words, "invalid GPU result");
        cuda_check(cudaMemcpy(power.data(), evidence.power, power.size() * sizeof(float),
                              cudaMemcpyDeviceToHost));
        require(std::all_of(power.begin(), power.end(), [](float x) {
            return std::isfinite(x) && x >= 0.0f;
        }), "invalid occupancy");
        const double peak = *std::max_element(power.begin(), power.end());
        Decision decision = Decision::unresolved;
        if (winner.score - winner.error_bound > threshold(shape)) {
            decision = Decision::present;
        } else if (winner.score + winner.error_bound <= threshold(shape)
                   && peak <= quiet_limit(shape)) {
            decision = Decision::quiet;
        }
        const auto end = Clock::now();
        return {winner, decision, peak, milliseconds(start, prepared),
                milliseconds(prepared, scored), milliseconds(start, end)};
    }

    Shape shape;
    FrontEnd front;
    CodebookSearch search;
    std::vector<float> power;
};

void verification() {
    check_fixture();
    const Shape shape{32, 32, 4, 8};
    Receiver receiver(shape);
    for (const auto channel : {Channel::flat, Channel::noise, Channel::holdout}) {
        auto scene = render(shape, channel, 14, 311);
        for (bool history : {false, true}) {
            auto gpu = receiver.front.analyze(scene.input, history);
            Evidence got{std::vector<float>(shape.rows() * shape.bits),
                         std::vector<float>(shape.rows()), std::vector<float>(shape.rows())};
            cuda_check(cudaMemcpy(got.contrast.data(), gpu.contrast,
                                  got.contrast.size() * sizeof(float), cudaMemcpyDeviceToHost));
            cuda_check(cudaMemcpy(got.common.data(), gpu.common,
                                  got.common.size() * sizeof(float), cudaMemcpyDeviceToHost));
            cuda_check(cudaMemcpy(got.power.data(), gpu.power,
                                  got.power.size() * sizeof(float), cudaMemcpyDeviceToHost));
            double best = -std::numeric_limits<double>::infinity();
            double second = best;
            int best_row = -1;
            int best_word = -1;
            for (int row = 0; row < shape.rows(); ++row) {
                const auto expected = reference_row(shape, scene.input, row, history);
                require(std::abs(expected.common[0] - got.common[row]) < 0.001,
                        "GPU common differs from DFT reference");
                require(std::abs(expected.power[0] - got.power[row]) < 0.001,
                        "GPU occupancy differs from DFT reference");
                for (int bit = 0; bit < shape.bits; ++bit) {
                    require(std::abs(expected.contrast[bit] - got.contrast[bit * shape.rows() + row])
                                < 0.0001, "GPU contrast differs from DFT reference");
                }
                for (int word = 0; word < shape.words; ++word) {
                    double score = expected.common[0];
                    for (int bit = 0; bit < shape.bits; ++bit) {
                        score += static_cast<double>(expected.contrast[bit]) * code_sign(word, bit);
                    }
                    score *= 0.5;
                    if (score > best) {
                        second = best;
                        best = score;
                        best_row = row;
                        best_word = word;
                    } else {
                        second = std::max(second, score);
                    }
                }
            }
            for (auto route : {Route::fp32, Route::fp16}) {
                receiver.search.search(gpu, route);
                const auto found = receiver.search.winner();
                require(std::abs(found.score - best) <= found.error_bound + 0.002,
                        "GPU maximum score differs from scalar codebook");
                if (best - second > 2.0 * found.error_bound + 0.002) {
                    require(found.row == best_row && found.word == best_word,
                            "GPU argmax differs from scalar codebook with clear margin");
                }
            }
        }
    }
    std::puts("VERIFY direct_dft_and_scalar_search PASS cells=6144 row_word_scores=6144");
}

void print_geometry(const Receiver& receiver) {
    const auto& shape = receiver.shape;
    std::printf("SHAPE bits=%d words=%d starts=%d frequencies=%d data_s=%.4f "
                "opportunity_s=%.4f pres_threshold=%.6f quiet_limit=%.6f "
                "frontend_bytes=%zu scorer_bytes=%zu\n", shape.bits, shape.words,
                shape.starts, shape.frequencies, shape.data_samples() / double(sample_rate),
                shape.opportunity_samples() / double(sample_rate), threshold(shape),
                quiet_limit(shape), receiver.front.bytes(), receiver.search.bytes());
}

void print_observation(const Scene& scene, Channel channel, double cn0, std::uint64_t seed,
                       bool history, Route route, const Observation& result) {
    std::printf("TRIAL channel=%s cn0=%g seed=%llu history=%d route=%s truth=%d "
                "truth_start=%d truth_freq=%d word=%d row=%d decision=%s "
                "score=%.6f allowance=%.6f power=%.6f front_ms=%.3f score_ms=%.3f total_ms=%.3f\n",
                name(channel), cn0, static_cast<unsigned long long>(seed), history ? 1 : 0,
                route == Route::fp16 ? "fp16" : "fp32", scene.word, scene.start, scene.frequency,
                result.winner.word, result.winner.row,
                name(result.decision), result.winner.score, result.winner.error_bound,
                result.power, result.front_ms, result.score_ms, result.total_ms);
}

void suite() {
    verification();
    struct TrialGroup {
        Channel channel;
        double cn0;
        int repeats;
    };
    constexpr std::array primary{
        TrialGroup{Channel::flat, 10, 3}, TrialGroup{Channel::moderate, 8, 5},
        TrialGroup{Channel::disturbed, 10, 5}, TrialGroup{Channel::holdout, 10, 5},
        TrialGroup{Channel::disturbed, 4, 5}, TrialGroup{Channel::noise, 0, 8},
        TrialGroup{Channel::interferer, 26, 4}, TrialGroup{Channel::wrong_word, 10, 4},
    };
    constexpr std::array low_rate{
        TrialGroup{Channel::moderate, 3, 4}, TrialGroup{Channel::disturbed, 6, 4},
        TrialGroup{Channel::holdout, 5, 4}, TrialGroup{Channel::noise, 0, 4},
    };
    for (const auto& shape : {primary_shape, low_rate_shape}) {
        Receiver receiver(shape);
        print_geometry(receiver);
        const auto warm = render(shape, Channel::noise, 0, 91);
        receiver.receive(warm, true, Route::fp32);
        receiver.receive(warm, true, Route::fp16);
        int disagreements = 0;
        int out_of_allowance = 0;
        const auto run = [&](const TrialGroup& group, int group_index) {
            for (int i = 0; i < group.repeats; ++i) {
                const std::uint64_t seed = 1000000ULL * shape.bits + 100ULL * group_index + i;
                const auto scene = render(shape, group.channel, group.cn0, seed);
                for (bool history : {true, false}) {
                    const auto first = i % 2 == 0 ? Route::fp32 : Route::fp16;
                    const auto second = i % 2 == 0 ? Route::fp16 : Route::fp32;
                    const auto a = receiver.receive(scene, history, first);
                    const auto b = receiver.receive(scene, history, second);
                    print_observation(scene, group.channel, group.cn0, seed, history, first, a);
                    print_observation(scene, group.channel, group.cn0, seed, history, second, b);
                    const bool differential = a.decision != b.decision ||
                        (a.decision == Decision::present && a.winner.word != b.winner.word);
                    disagreements += differential;
                    if (std::abs(a.winner.score - b.winner.score) >
                            a.winner.error_bound + b.winner.error_bound + 0.002) {
                        ++out_of_allowance;
                    }
                }
            }
        };
        int group_index = 0;
        if (shape.bits == primary_shape.bits) {
            for (const auto& group : primary) run(group, group_index++);
        } else {
            for (const auto& group : low_rate) run(group, group_index++);
        }
        std::printf("DIFFERENTIAL bits=%d disagreements=%d out_of_allowance=%d\n",
                    shape.bits, disagreements, out_of_allowance);
        if (shape.bits == primary_shape.bits) {
            const auto one = render(shape, Channel::moderate, 12, 2401);
            const auto two = render(shape, Channel::disturbed, 14, 2402);
            const auto pause = render(shape, Channel::noise, 0, 2403);
            const auto tail_a = render(shape, Channel::noise, 0, 2404);
            const auto tail_b = render(shape, Channel::noise, 0, 2405);
            int consecutive_quiet = 0;
            int delivered = 0;
            int slot = 0;
            int completed = -1;
            double total_ms = 0.0;
            for (const Scene* scene : {&one, &pause, &two, &tail_a, &tail_b}) {
                const auto result = receiver.receive(*scene, true, Route::fp16);
                delivered += (result.decision == Decision::present && result.winner.word == scene->word);
                consecutive_quiet = result.decision == Decision::quiet ? consecutive_quiet + 1 : 0;
                total_ms += result.total_ms;
                ++slot;
                if (consecutive_quiet == 2 && completed < 0) completed = slot;
                std::printf("SEQUENCE slot=%d decision=%s delivered=%d quiet_run=%d\n",
                            slot, name(result.decision), delivered, consecutive_quiet);
            }
            require(delivered == 2 && completed == 5, "physical quiet-slot sequence failed");
            std::printf("SEQUENCE PASS completed_slot=%d total_ms=%.3f\n", completed, total_ms);
        }
    }
}

long long epoch_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

void profile(const std::string& mode, int tile_words) {
    const auto route = mode == "fp32" ? Route::fp32 : Route::fp16;
    const bool complete = mode == "full";
    Receiver receiver(low_rate_shape, tile_words);
    const auto signal = render(receiver.shape, Channel::disturbed, 8, 97401);
    const auto quiet = render(receiver.shape, Channel::noise, 0, 97402);
    const auto check_signal = receiver.receive(signal, true, route);
    const auto check_quiet = receiver.receive(quiet, true, route);
    require(check_signal.decision == Decision::present && check_signal.winner.word == signal.word
                && check_quiet.decision == Decision::quiet, "profile fixtures did not validate");
    auto evidence = receiver.front.analyze(signal.input, true);
    cuda_check(cudaDeviceSynchronize());
    const auto start = Clock::now();
    const int seconds = 10;
    const auto target = start + std::chrono::seconds(seconds);
    std::printf("MEASURE_BEGIN %s %lld\n", mode.c_str(), epoch_ms());
    std::fflush(stdout);
    int count = 0;
    do {
        if (complete) {
            const auto a = receiver.receive(signal, true, route);
            const auto b = receiver.receive(quiet, true, route);
            require(a.decision == Decision::present && a.winner.word == signal.word
                        && b.decision == Decision::quiet, "replayed decisions changed");
            count += 2;
        } else {
            for (int batch = 0; batch < 4; ++batch) {
                receiver.search.search(evidence, route);
            }
            const auto winner = receiver.search.winner();
            require(winner.word == signal.word, "replayed score changed");
            count += 4;
        }
    } while (Clock::now() < target);
    const double elapsed_ms = milliseconds(start, Clock::now());
    const double flops = 2.0 * receiver.shape.rows() * receiver.shape.words * receiver.shape.bits;
    std::printf("MEASURE_END %s %lld count=%d wall_ms=%.3f dense_score_tflops=%.3f\n",
                mode.c_str(), epoch_ms(), count, elapsed_ms,
                count * flops / (elapsed_ms * 1.0e9));
}

double middle(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

void cache_audit() {
    cudaDeviceProp device{};
    int device_id = 0;
    cuda_check(cudaGetDevice(&device_id));
    cuda_check(cudaGetDeviceProperties(&device, device_id));
    std::printf("CACHE_DEVICE l2_bytes=%d sm=%d shared_per_sm=%zu\n",
                device.l2CacheSize, device.multiProcessorCount, device.sharedMemPerMultiprocessor);
    constexpr std::array tiles{0, 4096, 8192, 16384};
    constexpr int repeats = 17;
    struct Measurements {
        std::vector<double> wall, convert, gemm, reduction;
        double greatest_score_delta = 0.0;
    };
    for (const auto& shape : {primary_shape, low_rate_shape}) {
        FrontEnd front(shape);
        std::array<std::unique_ptr<CodebookSearch>, tiles.size()> scorers;
        std::array<Measurements, tiles.size()> results;
        for (std::size_t i = 0; i < tiles.size(); ++i)
            scorers[i] = std::make_unique<CodebookSearch>(shape, tiles[i]);
        int cases = 0;
        int differences = 0;
        for (const auto channel : {Channel::moderate, Channel::disturbed, Channel::holdout,
                                   Channel::interferer, Channel::wrong_word, Channel::noise}) {
            const double cn0 = channel == Channel::interferer ? 26.0 : 10.0;
            const auto scene = render(shape, channel, cn0, 7814 + cases);
            for (const bool history : {true, false}) {
                const auto evidence = front.analyze(scene.input, history);
                cuda_check(cudaDeviceSynchronize());
                std::array<Winner, tiles.size()> warmed;
                for (std::size_t i = 0; i < tiles.size(); ++i) {
                    scorers[i]->search(evidence, Route::fp16);
                    warmed[i] = scorers[i]->winner();
                    const auto& a = warmed.front();
                    const auto& b = warmed[i];
                    const double delta = std::abs(double(a.score) - b.score);
                    results[i].greatest_score_delta = std::max(results[i].greatest_score_delta, delta);
                    differences += a.row != b.row || a.word != b.word ||
                                   delta > a.error_bound + b.error_bound + 0.002 ||
                                   (a.score - a.error_bound > threshold(shape)) !=
                                   (b.score - b.error_bound > threshold(shape));
                }
                for (int rep = 0; rep < repeats; ++rep) {
                    for (std::size_t i = 0; i < tiles.size(); ++i) {
                        const std::size_t index = (i + rep) % tiles.size();
                        auto& scorer = *scorers[index];
                        auto& measurement = results[index];
                        const auto start = Clock::now();
                        scorer.search(evidence, Route::fp16);
                        const auto observed = scorer.winner();
                        measurement.wall.push_back(milliseconds(start, Clock::now()));
                        require(observed.row == warmed[index].row && observed.word == warmed[index].word,
                                "score layout changed winner during replay");
                    }
                }
                for (std::size_t i = 0; i < tiles.size(); ++i) {
                    scorers[i]->search(evidence, Route::fp16, nullptr, true);
                    (void)scorers[i]->winner();
                    const auto times = scorers[i]->stage_times();
                    results[i].convert.push_back(times.conversion_ms);
                    results[i].gemm.push_back(times.gemm_ms);
                    results[i].reduction.push_back(times.reduction_ms);
                }
                ++cases;
            }
        }
        for (std::size_t i = 0; i < tiles.size(); ++i) {
            const auto& r = results[i];
            std::printf("CACHE bits=%d tile_words=%d cases=%d n_wall=%zu scorer_bytes=%zu "
                        "wall_ms=%.4f convert_ms=%.4f gemm_ms=%.4f reduce_ms=%.4f max_gap=%.7f\n",
                        shape.bits, tiles[i], cases, r.wall.size(), scorers[i]->bytes(), middle(r.wall),
                        middle(r.convert), middle(r.gemm), middle(r.reduction), r.greatest_score_delta);
        }
        std::printf("CACHE_PARITY bits=%d differences=%d\n", shape.bits, differences);
        require(differences == 0, "cache layout changed a candidate outcome");
    }
}

} // namespace
} // namespace matrix_rx

int main(int argc, char** argv) {
    try {
        const char* usage = "usage: matrix_rx suite|verify|cache-audit or matrix_rx score|fp32|full [tile_words]";
        if (argc < 2 || argc > 3) throw std::invalid_argument(usage);
        const std::string mode(argv[1]);
        if (mode == "score" || mode == "fp32" || mode == "full") {
            matrix_rx::profile(mode, argc == 3 ? std::stoi(argv[2]) : 0);
        } else if (argc == 2 && mode == "verify") matrix_rx::verification();
        else if (argc == 2 && mode == "suite") matrix_rx::suite();
        else if (argc == 2 && mode == "cache-audit") matrix_rx::cache_audit();
        else throw std::invalid_argument(usage);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "ERROR %s\n", error.what());
        return 1;
    }
}
