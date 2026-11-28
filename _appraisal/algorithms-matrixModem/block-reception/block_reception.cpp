// Self-contained HF sample-domain research driver using the native modem FFT.
#include "../calc/mm_common.hpp"
#include "search_fft.hpp"
#include <array>
#include <bit>
#include <complex>
#include <chrono>
#include <optional>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <memory>
#include <span>
#include <string_view>
#include <thread>

namespace {
using Sample = std::complex<float>;
using datapump::modem::detail::search_fft::FloatPlan;
using datapump::modem::detail::search_fft::fft;

constexpr int nfft = 512, positions = 352, tones = 64, symbols = 32, words = 1024;
constexpr int nsamples = nfft * tones, pad = 64, delay = 8, nfixtures = 8;
constexpr int first = (nfft - positions) / 2;
constexpr std::array offsets = {-64, 0, 64};
constexpr double tau = 2 * mm::kPi;
constexpr std::array<Sample, 4> inverse_quarter_turn = {{{1, 0}, {0, -1}, {-1, 0}, {0, 1}}};

template <class T>
void shuffle(std::span<T> items, mm::Rng& rng) {
    for (size_t n = items.size(); n > 1; --n) {
        // Unbiased bounded draw from the reproducible fixture stream.
        const auto reject = (uint64_t{0} - n) % n;
        uint64_t x;
        do { x = rng.next(); } while (x < reject);
        std::swap(items[n - 1], items[x % n]);
    }
}

struct Schedule {
    std::array<std::array<uint16_t, words>, tones> position{};
    std::array<int, tones> quarter_phase{};
    std::array<int, tones> symbol_order{};
    explicit Schedule(uint64_t ordinal, uint64_t key, int held_cells = 1) {
        mm::Rng rng(key, ordinal);
        std::array<std::array<uint8_t, words>, symbols> values{};
        for (auto& table : values) {
            unsigned mask = rng.next() & 255;
            std::array<unsigned, 8> rows{};
            for (auto& row : rows) row = rng.next() % words;
            for (unsigned m = 0; m < words; ++m) {
                unsigned v = mask;
                for (int i = 0; i < 8; ++i) v ^= (std::popcount(rows[i] & m) % 2) << i;
                table[m] = static_cast<uint8_t>(v);
            }
        }
        const int active_tones = tones / held_cells;
        for (int t = 0; t < active_tones; ++t) symbol_order[t] = t % symbols;
        shuffle<int>(std::span(symbol_order).first(active_tones), rng);
        for (int t = 0; t < active_tones; ++t) {
            std::array<int, positions> map{};
            std::iota(map.begin(), map.end(), 0);
            shuffle<int>(map, rng);
            const int start = t * held_cells;
            quarter_phase[start] = rng.next() % 4;
            for (int m = 0; m < words; ++m) position[start][m] = map[values[symbol_order[t]][m]];
            for (int j = 1; j < held_cells; ++j) {
                quarter_phase[start + j] = quarter_phase[start];
                position[start + j] = position[start];
            }
        }
    }
};

std::vector<Sample> noise(size_t count, mm::Rng& rng) {
    std::vector<Sample> samples(count);
    for (auto& v : samples) {
        double radius = std::sqrt(rng.exp1());
        v = static_cast<Sample>(std::polar(radius, tau * rng.uniform()));
    }
    return samples;
}

int fft_bin(int position) {
    // Shift positions to signed baseband around zero.
    return (position + first + nfft / 2) % nfft;
}

void transmit(std::span<Sample> samples, int start, const Schedule& schedule, int word,
              double db_energy, bool two_ray, double phase, double echo_phase, double echo_bins = 2.,
              double carrier_cycles = 0., mm::Rng* phase_resets = nullptr) {
    const double amplitude = std::sqrt(mm::undb(db_energy) / (nsamples * (two_ray ? 2 : 1)));
    const auto doppler = std::polar(1., tau * echo_bins / nfft);
    auto echo = std::polar(1., echo_phase);
    for (int t = 0; t < tones; ++t) {
        double initial = phase + tau * schedule.quarter_phase[t] / 4;
        if (carrier_cycles != 0.) initial += tau * carrier_cycles * t / tones;
        if (phase_resets) initial += tau * phase_resets->uniform();
        auto v = std::polar(amplitude, initial);
        double angular = tau * fft_bin(schedule.position[t][word]) / nfft;
        auto rotation = std::polar(1., angular + tau * carrier_cycles / nsamples);
        for (int j = 0; j < nfft; ++j) {
            int n = start + t * nfft + j;
            samples[n] += static_cast<Sample>(v);
            if (two_ray) {
                samples[n + delay] += static_cast<Sample>(v * echo);
                echo *= doppler;
            }
            v *= rotation;
        }
    }
}

bool transmit_fractional(std::span<Sample> samples, double start, const Schedule& schedule, int word,
                         double db_energy, bool two_ray, double phase, double echo_phase,
                         double carrier_cycles, mm::Rng* phase_resets = nullptr) {
    const double end = start + nsamples;
    const int last = int(std::ceil(end)) - 1 + (two_ray ? delay : 0);
    if (start < 0. || last >= int(samples.size())) return false;
    const double amplitude = std::sqrt(mm::undb(db_energy) / (nsamples * (two_ray ? 2 : 1)));
    for (int t = 0; t < tones; ++t) {
        const double boundary = start + t * nfft;
        double initial = phase + tau * schedule.quarter_phase[t] / 4
            + tau * carrier_cycles * t / tones;
        if (phase_resets) initial += tau * phase_resets->uniform();
        const double hz = double(schedule.position[t][word] - 176) / nfft;
        for (int n = int(std::floor(boundary)); n < int(std::ceil(boundary + nfft)); ++n) {
            if (n < boundary || n >= boundary + nfft) continue;
            const double rel = n - boundary;
            const double carrier = carrier_cycles * rel / nsamples;
            const Sample direct = static_cast<Sample>(std::polar(amplitude,
                initial + tau * (hz * rel + carrier)));
            samples[n] += direct;
            if (two_ray) {
                const Sample echo = direct * static_cast<Sample>(std::polar(1., echo_phase
                    + tau * 2. * (n - start) / nfft));
                samples[n + delay] += echo;
            }
        }
    }
    return true;
}

float quantile(std::span<float> scratch, size_t rank) {
    std::nth_element(scratch.begin(), scratch.begin() + rank, scratch.end());
    return scratch[rank];
}

std::vector<Sample> sample_codes(std::span<const Sample> samples) {
    std::vector<float> power;
    if (samples.size() != nsamples + 2 * pad) return {};
    for (size_t i = 0; i < samples.size(); i += 16) {
        float value = std::norm(samples[i]);
        if (std::isfinite(value)) power.push_back(value);
    }
    if (power.empty()) return {};
    double sigma = std::sqrt(quantile(power, power.size() / 4) / (-2 * std::log(.75)));
    std::vector<Sample> codes(samples.size());
    if (!(sigma > 0)) return {};
    bool observed = false;
    for (size_t i = 0; i < samples.size(); ++i) {
        float re = std::round(samples[i].real() / (.45 * sigma));
        float im = std::round(samples[i].imag() / (.45 * sigma));
        if (std::isfinite(re) && std::isfinite(im) && std::max(std::abs(re), std::abs(im)) <= 7) {
            codes[i] = {re, im};
            observed |= i >= pad && i < pad + nsamples;
        }
    }
    return observed ? std::move(codes) : std::vector<Sample>{};
}

struct Best {
    double score = -INFINITY;
    int word = -1;
};

struct BankResult {
    Best original, expanded, coherent;
};

using Projection = std::array<Sample, positions>;
using ProjectionBlock = std::array<Projection, tones>;
using WordRow = std::array<Sample, words>;

// The order and tie break are shared by all families. Scores need only be
// comparable within this particular calibrated search, not Gaussian.
template <class T>
Best standardized_best(const std::array<T, words>& scores) {
    double mean = std::accumulate(scores.begin(), scores.end(), 0.) / words;
    double ss = 0.;
    for (auto s : scores) ss += (s - mean) * (s - mean);
    auto winner = std::max_element(scores.begin(), scores.end());
    double z = ss > 0 ? (*winner - mean) / std::sqrt(ss / words) : 0.;
    return {z, int(winner - scores.begin())};
}

void coherent_search(const ProjectionBlock& bins, const Schedule& schedule,
                     int coarse_time, int fine_time, Best& best) {
    std::array<Sample, positions> timing;
    for (int p = 0; p < positions; ++p)
        timing[p] = static_cast<Sample>(std::polar(1., tau * fft_bin(p) * (fine_time - coarse_time) / nfft));
    std::vector<WordRow> gathered(tones), tree(tones);
    for (int t = 0; t < tones; ++t) {
        Projection corrected{};
        for (int p = 0; p < positions; ++p)
            corrected[p] = bins[t][p] * timing[p] * inverse_quarter_turn[schedule.quarter_phase[t]];
        for (int m = 0; m < words; ++m) gathered[t][m] = corrected[schedule.position[t][m]];
    }
    for (double cycles : {-.5, 0., .5}) {
        for (int t = 0; t < tones; ++t) {
            auto rot = static_cast<Sample>(std::polar(1., -tau * cycles * (t + .5) / tones));
            for (int m = 0; m < words; ++m) tree[t][m] = gathered[t][m] * rot;
        }
        for (int width = 2; width <= tones; width *= 2) {
            std::array<float, words> sums{};
            for (int t = 0; t < tones; t += width) {
                auto& left = tree[t];
                const auto& right = tree[t + width / 2];
                for (int m = 0; m < words; ++m) {
                    left[m] += right[m];
                    sums[m] += std::norm(left[m]);
                }
            }
            auto winner = standardized_best(sums);
            if (winner.score > best.score) best = winner;
        }
    }
}

#include "timing_trial.hpp"

template <int held_cells, size_t rows, size_t bins>
void score_energy(std::array<std::array<float, bins>, rows>& energy, const Schedule& schedule,
                  bool include_pairs, BankResult& result) {
    constexpr int radius = 2 * held_cells;
    static_assert(rows * held_cells == tones);
    static_assert(bins == (positions - 1) * held_cells + 2 * radius + 1);
    const int routes = include_pairs ? 4 : 2;
    for (int p = 0; p < int(bins); ++p) {
        std::array<float, rows> column{};
        for (int t = 0; t < int(rows); ++t) column[t] = energy[t][p];
        float median = quantile(column, rows / 2);
        for (auto& row : energy) row[p] = median > 0 ? row[p] / median : 0;
    }
    std::array<std::array<int, words>, 4> sums{};
    for (int t = 0; t < int(rows); ++t) {
        const auto& row = energy[t];
        auto scratch = row;
        float median = quantile(scratch, bins / 2);
        float scale = median > 0 ? float(std::log(2.) / median * 15. / 6.) : 0;
        std::array<std::array<int, positions>, 4> evidence{};
        const auto code = [scale](float value) {
            return std::clamp(int(std::round(value * scale)), 0, 15);
        };
        for (int p = 0; p < positions; ++p) {
            const int i = p * held_cells;
            float smear = std::accumulate(row.begin() + i, row.begin() + i + 2 * radius + 1, 0.f) / (2 * radius + 1);
            evidence[0][p] = code(row[i + radius]);
            evidence[1][p] = code(smear);
            if (include_pairs) {
                evidence[2][p] = code((row[i + radius] + row[i + 2 * radius]) * .5f);
                evidence[3][p] = code((row[i + radius] + row[i]) * .5f);
            }
        }
        for (int h = 0; h < routes; ++h)
            for (int m = 0; m < words; ++m)
                sums[h][m] += evidence[h][schedule.position[t * held_cells][m]];
    }
    for (int h = 0; h < routes; ++h) {
        const auto& scores = sums[h];
        Best candidate = standardized_best(scores);
        if (h < 2 && candidate.score > result.original.score) result.original = candidate;
        if (candidate.score > result.expanded.score) result.expanded = candidate;
    }
}

// Retain the half-spaced bins of the two-cell transform for the broad aperture.
void held_energy(std::span<const Sample> codes, const Schedule& schedule, const FloatPlan& held_plan,
                 int offset, BankResult& result) {
    constexpr int length = 2 * nfft, bins = 2 * (positions - 1) + 9;
    std::array<std::array<float, bins>, tones / 2> energy{};
    std::array<Sample, length> spectrum{};
    for (int t = 0; t < tones / 2; ++t) {
        std::copy_n(codes.begin() + pad + offset + t * length, length, spectrum.begin());
        fft(spectrum, false, held_plan);
        for (int p = 0; p < bins; ++p)
            energy[t][p] = std::norm(spectrum[(2 * fft_bin(0) - 4 + p) % length]);
    }
    score_energy<2>(energy, schedule, false, result);
}

struct RidgeRow {
    int signed_bin{};
    double peak_over_median = -INFINITY;
};
using Ridge = std::array<RidgeRow, tones>;
constexpr int ridge_edge = 230;
using RidgeEnergyRow = std::array<uint8_t, 2 * ridge_edge + 1>;
using RidgeEnergy = std::array<RidgeEnergyRow, tones>;

// Both observers use the same unnormalized, nominal-time FFT. The waterfall
// covers the union of the two guarded orientation banks. The wider ridge
// extractor also includes positions reached by the trial's affine frequency faults.
void observe_row(const std::array<Sample, nfft>& spectrum, double* waterfall_sum, RidgeRow* ridge,
                 RidgeEnergyRow* evidence = nullptr) {
    constexpr int waterfall_edge = positions / 2 + 2;
    const int edge = (ridge || evidence) ? ridge_edge : waterfall_edge;
    std::array<float, 2 * ridge_edge + 1> power;
    std::array<float, 2 * ridge_edge + 1> frequency_order;
    float waterfall_peak = 0.f, ridge_peak = -1.f;
    int peak_bin = 0;
    for (int k = -edge; k <= edge; ++k) {
        float value = std::norm(spectrum[(nfft + k) % nfft]);
        power[k + edge] = value;
        if (waterfall_sum && std::abs(k) <= waterfall_edge)
            waterfall_peak = std::max(waterfall_peak, value);
        if (ridge && value > ridge_peak) {
            ridge_peak = value;
            peak_bin = k;
        }
    }
    if (evidence) frequency_order = power;
    if (waterfall_sum) {
        auto inner = std::span(power).subspan(edge - waterfall_edge, 2 * waterfall_edge + 1);
        float median = quantile(inner, waterfall_edge);
        *waterfall_sum += median > 0 ? double(waterfall_peak) / median : -INFINITY;
    }
    if (ridge || evidence) {
        // Rearranging the inner entries above preserves the full order statistic.
        float median = quantile(power, ridge_edge);
        if (ridge)
            *ridge = median > 0 ? RidgeRow{peak_bin, double(ridge_peak) / median} : RidgeRow{};
        if (evidence) {
            double step = median > 0 ? std::log(2.) * 15. / (6. * median) : 0.;
            for (size_t i = 0; i < evidence->size(); ++i)
                (*evidence)[i] = static_cast<uint8_t>(std::min(15., std::round(step * frequency_order[i])));
        }
    }
}

template <size_t orientations>
std::array<BankResult, orientations> search_banks(std::span<const Sample> samples,
        const Schedule& schedule, const FloatPlan& plan, bool include_pairs, bool include_coherent,
        const FloatPlan* held_plan, double* waterfall, Ridge* ridge, RidgeEnergy* ridge_energy,
        TimingCapture* timing_capture = nullptr) {
    static_assert(orientations == 1 || orientations == 2);
    // Include both guard bins on each side in each normalization.
    constexpr int bins = positions + 4;
    if (waterfall) *waterfall = -INFINITY;
    if (ridge) ridge->fill({});
    const auto codes = sample_codes(samples);
    std::array<BankResult, orientations> result{};
    if (codes.empty()) {
        if (ridge_energy) ridge_energy->fill({});
        return result;
    }
    if (timing_capture) timing_capture->valid = true;
    std::optional<std::array<ProjectionBlock, orientations>> coherent_bins;
    if (include_coherent) coherent_bins.emplace();
    using Energy = std::array<std::array<float, bins>, tones>;
    std::array<Energy, orientations> energy{};
    std::array<Sample, nfft> spectrum{};
    for (int offset : offsets) {
        double waterfall_sum = 0.;
        for (int t = 0; t < tones; ++t) {
            std::copy_n(codes.begin() + pad + offset + t * nfft, nfft, spectrum.begin());
            float component_step = 0;
            if (include_coherent) {
                double squared = 0;
                for (auto x : spectrum) squared += std::norm(x);
                component_step = float(.45 * std::sqrt(squared / 2.));
            }
            fft(spectrum, false, plan);
            if ((waterfall || ridge || ridge_energy) && offset == 0)
                observe_row(spectrum, waterfall ? &waterfall_sum : nullptr, ridge ? &(*ridge)[t] : nullptr,
                            ridge_energy ? &(*ridge_energy)[t] : nullptr);
            for (size_t orientation = 0; orientation < orientations; ++orientation) {
                // DFT(conj(x))[k] = conj(DFT(x)[-k]). A reversal of the
                // 352 placement indices alone would instead reflect about -1/2 bin.
                auto bin_at = [orientation](int position) {
                    int k = fft_bin(position);
                    return orientation == 0 ? k : (nfft - k) % nfft;
                };
                if (include_coherent) {
                    auto quant = [component_step](float x) {
                        return component_step > 0 ? std::clamp(std::round(x / component_step), -7.f, 7.f) : 0.f;
                    };
                    for (int p = 0; p < positions; ++p) {
                        Sample z = spectrum[bin_at(p)];
                        if (orientation != 0) z = std::conj(z);
                        (*coherent_bins)[orientation][t][p] = {quant(z.real()), quant(z.imag())};
                    }
                    if (timing_capture && orientation == 0)
                        timing_capture->projection[offset_index(offset)][t] =
                            (*coherent_bins)[orientation][t];
                }
                if (!held_plan)
                    for (int p = 0; p < bins; ++p)
                        energy[orientation][t][p] = std::norm(spectrum[bin_at(p - 2)]);
            }
        }
        if (waterfall && offset == 0) *waterfall = waterfall_sum / tones;
        for (size_t orientation = 0; orientation < orientations; ++orientation) {
            auto& bank = result[orientation];
            if (include_coherent) {
                const auto& projections = (*coherent_bins)[orientation];
                coherent_search(projections, schedule, offset, offset, bank.coherent);
                if (offset == 0)
                    for (int fine : {-32, 32})
                        coherent_search(projections, schedule, 0, fine, bank.coherent);
            }
            if (held_plan) held_energy(codes, schedule, *held_plan, offset, bank);
            else score_energy<1>(energy[orientation], schedule, include_pairs, bank);
        }
    }
    return result;
}

BankResult score_banks(std::span<const Sample> samples, const Schedule& schedule,
                       const FloatPlan& plan, bool include_pairs, bool include_coherent = false,
                       const FloatPlan* held_plan = nullptr, TimingResults* timing = nullptr, bool audit = false) {
    std::unique_ptr<TimingCapture> capture;
    if (timing) capture = std::make_unique<TimingCapture>();
    auto result = search_banks<1>(samples, schedule, plan, include_pairs,
                           include_coherent || timing, held_plan, nullptr, nullptr, nullptr,
                           capture.get())[0];
    if (timing) *timing = capture->valid ? score_timing(*capture, schedule, audit) : TimingResults{};
    return result;
}

std::array<BankResult, 2> score_orientations(std::span<const Sample> samples,
        const Schedule& schedule, const FloatPlan& plan, double* waterfall = nullptr,
        Ridge* ridge = nullptr, RidgeEnergy* ridge_energy = nullptr) {
    return search_banks<2>(samples, schedule, plan, false, true, nullptr, waterfall, ridge, ridge_energy);
}

Best decode(std::span<const Sample> samples, const Schedule& schedule, const FloatPlan& plan) {
    return score_banks(samples, schedule, plan, false).original;
}

struct Trial {
    std::vector<Sample> samples;
    unsigned ordinal, word;
    int alignment;
    double phase, echo_phase;
    Trial(uint64_t seed, int trial) {
        mm::Rng rng(seed, trial);
        ordinal = rng.next() % nfixtures;
        word = rng.next() % words;
        alignment = rng.next() % 2 ? 32 : -32;
        phase = tau * rng.uniform();
        echo_phase = tau * rng.uniform();
        samples = noise(nsamples + 2 * pad, rng);
    }
};

struct Outcomes {
    int correct{}, wrong{}, absent{};
    void observe(Best result, int truth, double threshold) {
        if (result.score <= threshold) ++absent;
        else if (result.word == truth) ++correct;
        else ++wrong;
    }
};

void proportion(int count, int total) {
    constexpr double z = 1.959963984540054;
    double p = double(count) / total, zz = z * z, den = 1 + zz / total;
    double mid = (p + zz / (2 * total)) / den;
    double delta = z * std::sqrt((p * (1 - p) + zz / (4 * total)) / total) / den;
    std::cout << count << " [" << std::max(0., (mid - delta) * 100.) << ", "
              << std::min(100., (mid + delta) * 100.) << "]";
}

void print(std::string_view label, const Outcomes& out) {
    int n = out.correct + out.wrong + out.absent;
    std::cout << label << " | " << n << " | ";
    proportion(out.correct, n); std::cout << " | ";
    proportion(out.wrong, n); std::cout << " | ";
    proportion(out.absent, n); std::cout << '\n';
}

// Keep a pending absence as an interior erasure when another payload arrives.
// A new accepted payload after a completed pair opens another episode.
struct Calendar {
    bool active{};
    int pending{}, gap{}, completions{}, erasures{};
    void unscored() {
        if (active) ++gap;
        pending = 0;
    }
    void accept(bool present) {
        if (present) {
            erasures += gap;
            gap = 0;
            active = true;
            pending = 0;
        } else if (active) {
            ++gap;
            if (++pending == 2) {
                ++completions;
                active = false;
                pending = gap = 0;
            }
        }
    }
};

bool stream_check(bool two_extinguished, const std::vector<Schedule>& fixtures,
                  const FloatPlan& plan, double threshold) {
    // -1 is physical transmitter quiet. -2 is a transmitted word under zero channel gain.
    std::vector<int> truth{113, -2};
    if (two_extinguished) truth.push_back(-2);
    truth.insert(truth.end(), {889, -1, -1});
    mm::Rng rng(0x63616c656e646172ULL, two_extinguished);
    auto samples = noise(truth.size() * nsamples + 2 * pad, rng);
    for (size_t i = 0; i < truth.size(); ++i)
        if (truth[i] >= 0)
            transmit(samples, int(i * nsamples) + pad + 32, fixtures[i], truth[i],
                     29., false, 0., 0.);
    Calendar calendar;
    bool matches = true;
    for (size_t i = 0; i < truth.size(); ++i) {
        auto r = decode(std::span(samples).subspan(i * nsamples, nsamples + 2 * pad),
                        fixtures[i], plan);
        if (r.word < 0) {
            calendar.unscored();
            matches = false;
            continue;
        }
        bool present = r.score > threshold;
        matches &= present ? truth[i] == r.word : truth[i] < 0;
        const int previous = calendar.completions;
        calendar.accept(present);
        std::cout << (two_extinguished ? "two" : "one") << " | " << i << " | "
                  << truth[i] << " | " << r.score << " | " << (present ? r.word : -1)
                  << " | " << (calendar.completions > previous ? "closed" : ".") << '\n';
        const bool expected_completion = i + 1 == truth.size() || (two_extinguished && i == 2);
        matches &= (calendar.completions > previous) == expected_completion;
    }
    return matches && calendar.completions == (two_extinguished ? 2 : 1)
        && calendar.erasures == (two_extinguished ? 0 : 1);
}
struct Comparison {
    Outcomes original, expanded;
    int gained{}, lost{};
    void observe(const BankResult& r, int truth, double old_threshold, double new_threshold) {
        original.observe(r.original, truth, old_threshold);
        expanded.observe(r.expanded, truth, new_threshold);
        if (truth < 0) return;
        bool before = r.original.score > old_threshold && r.original.word == truth;
        bool after = r.expanded.score > new_threshold && r.expanded.word == truth;
        gained += !before && after;
        lost += before && !after;
    }
};

// Exact two-sided sign test for the primary matched binary outcome.
double paired_p(int gained, int lost) {
    int n = gained + lost, k = std::min(gained, lost);
    double term = std::ldexp(1., -n), tail = term;
    for (int i = 1; i <= k; ++i) {
        term *= double(n - i + 1) / i;
        tail += term;
    }
    return std::min(1., 2 * tail);
}

void compare_apertures(const FloatPlan& plan, const std::vector<Schedule>& fixtures) {
    std::array<double, 399> cal_before{}, cal_after{};
    for (int i = 0; i < 399; ++i) {
        Trial trial(0x941, i);
        auto r = score_banks(trial.samples, fixtures[trial.ordinal], plan, true);
        cal_before[i] = r.original.score;
        cal_after[i] = r.expanded.score;
    }
    std::sort(cal_before.begin(), cal_before.end());
    std::sort(cal_after.begin(), cal_after.end());
    double old_threshold = cal_before[395], new_threshold = cal_after[395];
    std::cout << "aperture calibration thresholds " << old_threshold << " / " << new_threshold << '\n';
    Comparison null;
    for (int i = 0; i < 400; ++i) {
        Trial trial(0xab3, i);
        auto r = score_banks(trial.samples, fixtures[trial.ordinal], plan, true);
        null.observe(r, -1, old_threshold, new_threshold);
    }
    print("noise, original", null.original);
    print("noise, expanded", null.expanded);
    constexpr std::array<const char*, 4> labels = {"single", "+2 bins", "-2 bins", "+1.5 bins"};
    constexpr std::array echo_bins = {0., 2., -2., 1.5};
    std::array<Comparison, 4> rows{};
    for (int i = 0; i < 160; ++i) {
        Trial trial(0xd01, i);
        for (size_t j = 0; j < rows.size(); ++j) {
            auto samples = trial.samples;
            transmit(samples, pad + trial.alignment, fixtures[trial.ordinal], trial.word,
                     18., j != 0, trial.phase, trial.echo_phase, echo_bins[j]);
            auto r = score_banks(samples, fixtures[trial.ordinal], plan, true);
            rows[j].observe(r, trial.word, old_threshold, new_threshold);
        }
    }
    for (size_t j = 0; j < rows.size(); ++j) {
        print(std::string(labels[j]) + ", original", rows[j].original);
        print(std::string(labels[j]) + ", expanded", rows[j].expanded);
        std::cout << "paired gains / losses " << rows[j].gained << " / " << rows[j].lost << '\n';
    }
    std::cout << "primary two-sided p=" << std::scientific << paired_p(rows[1].gained, rows[1].lost) << '\n';
}
struct Cutoffs {
    double baseline, per_tone, coherent;
};

std::optional<int> joint_word(const BankResult& scores, const Cutoffs& cut) {
    if (scores.original.word < 0 || scores.coherent.word < 0) return std::nullopt;
    if (scores.original.score > cut.per_tone) return scores.original.word;
    if (scores.coherent.score > cut.coherent) return scores.coherent.word;
    return -1; // scored quiet
}

struct JointOutcomes {
    Outcomes baseline, allocated_per_tone, allocated_coherent, joint;
    void observe(const BankResult& scores, int truth, const Cutoffs& cut) {
        baseline.observe(scores.original, truth, cut.baseline);
        allocated_per_tone.observe(scores.original, truth, cut.per_tone);
        allocated_coherent.observe(scores.coherent, truth, cut.coherent);
        auto word = joint_word(scores, cut);
        if (!word) throw std::runtime_error("unscored Monte Carlo input");
        if (*word < 0) ++joint.absent;
        else if (*word == truth) ++joint.correct;
        else ++joint.wrong;
    }
    void report(const std::string& label) const {
        print(label + " baseline", baseline);
        print(label + " reserved per-tone", allocated_per_tone);
        print(label + " reserved coherent", allocated_coherent);
        print(label + " joint", joint);
    }
};

bool joint_calendar(const FloatPlan& plan, const std::vector<Schedule>& fixtures, const Cutoffs& cut) {
    constexpr std::array<int, 5> truth = {113, -2, 889, -1, -1};
    mm::Rng rng(0xbe614698);
    auto samples = noise(truth.size() * nsamples + 2 * pad, rng);
    for (size_t t = 0; t < truth.size(); ++t)
        if (truth[t] >= 0)
            transmit(samples, t * nsamples + pad + 32, fixtures[t], truth[t], 25.,
                     false, .82, 0., 2., .25);
    Calendar calendar;
    bool pass = true;
    for (size_t t = 0; t < truth.size(); ++t) {
        auto scores = score_banks(std::span(samples).subspan(t * nsamples, nsamples + 2 * pad),
                                  fixtures[t], plan, false, true);
        auto word = joint_word(scores, cut);
        if (!word) {
            calendar.unscored();
            pass = false;
            continue;
        }
        calendar.accept(*word >= 0);
        std::cout << "joint calendar " << t << " " << *word << " completions " << calendar.completions << '\n';
        pass &= *word == std::max(-1, truth[t]) && calendar.completions == (t + 1 == truth.size());
    }
    pass &= calendar.erasures == 1;
    // Explicit missing data, zero scale and wholly unusable numerical samples
    // between quiet observations. None can complete a message.
    std::vector<Sample> zero(nsamples + 2 * pad);
    std::vector<Sample> missing(zero.size(), {NAN, NAN});
    // Numeric scale can come from the guards while every interior IQ is missing.
    for (int t = 0; t < pad; ++t) missing[t] = missing[missing.size()-1-t] = {1, 1};
    for (auto lost : {std::span<const Sample>{}, std::span<const Sample>(zero), std::span<const Sample>(missing)}) {
        Calendar c;
        c.accept(true);
        c.accept(false);
        auto scored = joint_word(score_banks(lost, fixtures[1], plan, false, true), cut);
        if (scored) return false;
        c.unscored();
        c.accept(false);
        if (c.completions != 0) return false;
        auto resumed = c;
        resumed.accept(true);
        if (resumed.erasures != 3) return false;
        c.accept(false);
        if (c.completions != 1) return false;
    }
    return pass;
}

bool compare_coherent(const FloatPlan& plan, const std::vector<Schedule>& fixtures) {
    const auto start = std::chrono::steady_clock::now();
    std::array<double, 399> per_tone{}, coherent{};
    for (int i = 0; i < 399; ++i) {
        Trial trial(0x962ec1, i);
        auto scores = score_banks(trial.samples, fixtures[trial.ordinal], plan, false, true);
        per_tone[i] = scores.original.score;
        coherent[i] = scores.coherent.score;
    }
    std::sort(per_tone.begin(), per_tone.end());
    std::sort(coherent.begin(), coherent.end());
    Cutoffs cut{per_tone[395], per_tone[397], coherent[397]};
    std::cout << "coherent-branch cutoffs (baseline per-tone, allocated per-tone, allocated coherent) "
              << cut.baseline << " " << cut.per_tone << " " << cut.coherent << '\n';
    JointOutcomes null;
    for (int i = 0; i < 400; ++i) {
        Trial trial(0x129e1f, i);
        null.observe(score_banks(trial.samples, fixtures[trial.ordinal], plan, false, true), -1, cut);
    }
    null.report("noise");
    constexpr std::array<const char*, 6> label = {"single 15", "single 18", "two ray 15",
                                                 "two ray 18", "phase reset 18", "phase reset 22"};
    constexpr std::array energy = {15., 18., 15., 18., 18., 22.};
    std::array<JointOutcomes, 6> rows{};
    for (int i = 0; i < 120; ++i) {
        Trial trial(0x3ab067, i);
        auto& schedule = fixtures[trial.ordinal];
        mm::Rng rf(0x2ee71, i);
        const double cycles = rf.next() % 2 ? -.25 : .25;
        for (int j = 0; j < 6; ++j) {
            auto samples = trial.samples;
            mm::Rng reset(0x29f14, i);
            transmit(samples, pad + trial.alignment, schedule, trial.word, energy[j],
                     j == 2 || j == 3, trial.phase, trial.echo_phase, 2., cycles, j >= 4 ? &reset : nullptr);
            auto scores = score_banks(samples, schedule, plan, false, true);
            rows[j].observe(scores, trial.word, cut);
        }
    }
    for (int j = 0; j < 6; ++j) rows[j].report(label[j]);
    bool pass = joint_calendar(plan, fixtures, cut);
    std::cout << "joint calendar and unscored controls " << (pass ? "pass" : "FAIL")
              << ", experiment seconds "
              << std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() << '\n';
    return pass;
}
#include "held_tones.hpp"
#include "orientation_trial.hpp"
#include "affine_trial.hpp"
#include "fault_admission.hpp"
#include "pointing_trial.hpp"

#include "timing_experiment.hpp"

// Export the on-grid tone centres for the waterfall in the format guide.
bool format_example(const Schedule& schedule, const FloatPlan& plan) {
    constexpr int word = 173;
    constexpr double energy_db = 18., common_phase = .73;
    std::vector<Sample> samples(nsamples);
    transmit(samples, 0, schedule, word, energy_db, false, common_phase, 0.);
    const auto expected = std::polar(nfft * std::sqrt(mm::undb(energy_db) / nsamples), common_phase);
    std::cout << "tone,symbol,position,phase_deg\n";
    for (int t = 0; t < tones; ++t) {
        std::array<Sample, nfft> spectrum;
        std::copy_n(samples.begin() + t * nfft, nfft, spectrum.begin());
        fft(spectrum, false, plan);
        const int p = schedule.position[t][word], q = schedule.quarter_phase[t];
        const auto peak = std::max_element(spectrum.begin(), spectrum.end(),
            [](Sample a, Sample b) { return std::norm(a) < std::norm(b); });
        const Sample corrected = *peak * inverse_quarter_turn[q];
        if (peak != spectrum.begin() + fft_bin(p) ||
            !(std::abs(std::complex<double>(corrected) - expected) <= 1e-5 * std::abs(expected)))
            return false;
        std::cout << t << ',' << schedule.symbol_order[t] << ',' << p << ',' << 90 * q << '\n';
    }
    return true;
}
} // namespace

int main(int argc, char** argv) {
    const FloatPlan plan(nfft);
    std::vector<Schedule> fixtures, other;
    for (int ordinal = 0; ordinal < nfixtures; ++ordinal) {
        fixtures.emplace_back(ordinal, 0xaa6218f2);
        other.emplace_back(ordinal, 0xd3d65011);
    }
    if (argc == 2 && std::string_view(argv[1]) == "--format-example")
        return format_example(fixtures.front(), plan) ? 0 : 1;
    if (argc == 2 && std::string_view(argv[1]) == "--timing-checks")
        return timing_checks(plan, fixtures) ? 0 : 1;
    if (argc == 2 && std::string_view(argv[1]) == "--timing-unrelated")
        return timing_unrelated(plan, fixtures, other);
    if ((argc == 2 || argc == 3) && std::string_view(argv[1]) == "--timing-pilot") {
        int trials = 1;
        if (argc == 3) {
            try { trials = std::stoi(argv[2]); } catch (...) { return 2; }
            if (trials < 1 || trials > 3) return 2;
        }
        return timing_pilot(plan, fixtures, trials);
    }
    if ((argc == 2 || argc == 3) && std::string_view(argv[1]) == "--timing-coverage") {
        int workers = std::max(1u, std::thread::hardware_concurrency());
        if (argc == 3) {
            try {
                size_t used = 0;
                workers = std::stoi(argv[2], &used);
                if (used != std::string_view(argv[2]).size()) return 2;
            } catch (...) { return 2; }
        }
        if (workers < 1 || workers > 256) return 2;
        return timing_coverage(plan, fixtures, workers);
    }
    if (argc == 2 && std::string_view(argv[1]) == "--pointing-loss") {
        check_pointing_loss(plan, fixtures);
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--orientation")
        return check_orientation(plan, fixtures, other) ? 0 : 1;
    if (argc == 2 && std::string_view(argv[1]) == "--orientation-waterfall") {
        dump_orientation_waterfall(plan, fixtures);
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--affine")
        return check_affine(plan, fixtures, other) ? 0 : 1;
    if (argc == 2 && std::string_view(argv[1]) == "--affine-all-pairs")
        return check_affine_all_pairs(plan, fixtures, other) ? 0 : 1;
    if (argc == 2 && std::string_view(argv[1]) == "--affine-contamination")
        return check_affine_contamination(plan, fixtures, other) ? 0 : 1;
    if (argc == 2 && std::string_view(argv[1]) == "--affine-refine")
        return check_affine_refinement(plan, fixtures, other) ? 0 : 1;
    if (argc == 2 && std::string_view(argv[1]) == "--fault-admission")
        return check_fault_admission(plan, fixtures, other) ? 0 : 1;
    if (argc == 2 && std::string_view(argv[1]) == "--fault-refinement")
        return check_fault_refinement(plan, fixtures, other) ? 0 : 1;
    if (argc == 2 && std::string_view(argv[1]) == "--rare") {
        check_rare_admission(plan, fixtures, other);
        return 0;
    }
    if (argc == 2 && (std::string_view(argv[1]) == "--held" || std::string_view(argv[1]) == "--thresholds")) {
        compare_held_tones(plan, fixtures, std::string_view(argv[1]) == "--thresholds");
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--coherent")
        return compare_coherent(plan, fixtures) ? 0 : 1;
    if (argc == 2 && std::string_view(argv[1]) == "--apertures") {
        compare_apertures(plan, fixtures);
        return 0;
    }
    if (argc != 1) return 2;
    std::vector<double> cal;
    for (int i = 0; i < 399; ++i) {
        Trial trial(0x181, i);
        cal.push_back(decode(trial.samples, fixtures[trial.ordinal], plan).score);
    }
    std::sort(cal.begin(), cal.end());
    double threshold = cal[395];
    std::cout << std::fixed << std::setprecision(4) << "calibration threshold " << threshold << '\n';
    std::array<Outcomes, 6> signal{};
    Outcomes null, wrong_schedule;
    constexpr std::array levels = {18., 22., 24.};
    for (int i = 0; i < 400; ++i) {
        Trial trial(0x195, i);
        null.observe(decode(trial.samples, fixtures[trial.ordinal], plan), -1, threshold);
    }
    for (int i = 0; i < 160; ++i) {
        Trial trial(0x211, i);
        auto mismatch = trial.samples;
        transmit(mismatch, pad + trial.alignment, other[trial.ordinal], trial.word,
                 24., false, trial.phase, trial.echo_phase);
        wrong_schedule.observe(decode(mismatch, fixtures[trial.ordinal], plan), -1, threshold);
        for (int ray = 0; ray <= 1; ++ray) {
            for (size_t j = 0; j < levels.size(); ++j) {
                auto observed = trial.samples;
                auto& fixture = fixtures[trial.ordinal];
                transmit(observed, pad + trial.alignment, fixture, trial.word,
                         levels[j], ray, trial.phase, trial.echo_phase);
                auto r = decode(observed, fixture, plan);
                signal[ray * levels.size() + j].observe(r, trial.word, threshold);
            }
        }
    }
    std::cout << std::setprecision(2) << "case | trials | correct [95%] | wrong/false [95%] | absent [95%]\n";
    print("noise", null);
    print("wrong-schedule 24 dB", wrong_schedule);
    for (int r = 0; r < 2; ++r)
        for (size_t j = 0; j < levels.size(); ++j)
            print(std::string(r ? "two-ray " : "single-path ") + std::to_string(int(levels[j]))
                  + " dB", signal[r * levels.size() + j]);
    std::cout << "extinguished | ordinal | emitted word/quiet/outage | score | accepted word or -1 | event\n";
    bool one = stream_check(false, fixtures, plan, threshold);
    bool two = stream_check(true, fixtures, plan, threshold);
    std::cout << "calendar assertions: " << (one && two ? "pass" : "fail") << '\n';
    return one && two ? 0 : 1;
}
