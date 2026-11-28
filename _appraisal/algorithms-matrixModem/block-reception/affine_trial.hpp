// Strong-tone diagnostic on the configured block calendar. Frequencies use signed 6.25 Hz bins.
namespace affine_trial {
constexpr int band_edge = ridge_edge, band_size = 2 * band_edge + 1;
constexpr double max_intercept = 24, min_scale = .5, max_scale = 1.1;
struct Map { double slope, intercept; };
constexpr std::array<Map, 3> maps{{{1., 0.}, {.73, 17.375}, {-.81, -13.625}}};

struct Peaks {
    Ridge ridge;
    double visibility{};
};

using EnergyRows = RidgeEnergy;

bool extract(std::span<const Sample> samples, const FloatPlan& plan, Peaks& peaks,
             EnergyRows* evidence = nullptr, double* narrow_waterfall = nullptr) {
    if (narrow_waterfall) *narrow_waterfall = -INFINITY;
    peaks.visibility = 0.;
    double narrow_sum = 0.;
    const auto codes = sample_codes(samples);
    if (codes.empty()) return false;
    std::array<Sample, nfft> spectrum{};
    for (int t = 0; t < tones; ++t) {
        std::copy_n(codes.begin() + pad + t * nfft, nfft, spectrum.begin());
        fft(spectrum, false, plan);
        auto& row = peaks.ridge[t];
        observe_row(spectrum, narrow_waterfall ? &narrow_sum : nullptr, &row,
                    evidence ? &(*evidence)[t] : nullptr);
        if (!std::isfinite(row.peak_over_median)) return false;
        peaks.visibility += row.peak_over_median / tones;
    }
    if (narrow_waterfall) *narrow_waterfall = narrow_sum / tones;
    return true;
}

// On each signed slope interval the quadratic has an interior minimizer or an edge
// minimizer. For each candidate slope the best intercept is the clamped mean residual.
double fit_score(int n, double mx, double my, double xx, double xy, double yy) {
    if (!(xx > 0 && yy > 0)) return -INFINITY;
    double best = -INFINITY;
    for (double sign : {-1., 1.}) {
        const double lo = sign < 0 ? -max_scale : min_scale;
        const double hi = sign < 0 ? -min_scale : max_scale;
        const auto consider = [&](double slope) {
            const double b = std::clamp(slope, lo, hi);
            const double free_intercept = my - b * mx;
            const double bias = free_intercept - std::clamp(free_intercept, -max_intercept, max_intercept);
            const double explained = (2 * xy - b * xx) * b - n * bias * bias;
            best = std::max(best, explained / yy);
        };
        consider(xy / xx);
        const double uncentered_xx = xx + n * mx * mx;
        for (double a : {-max_intercept, max_intercept})
            consider((xy + n * mx * (my - a)) / uncentered_xx);
    }
    return best;
}

Best blind(const Ridge& peaks, const Schedule& schedule, std::array<Best, 2>* by_size = nullptr) {
    if (by_size) by_size->fill({});
    for (const auto& row : peaks) if (!std::isfinite(row.peak_over_median)) return {};
    std::array<int, tones> rank;
    std::iota(rank.begin(), rank.end(), 0);
    std::sort(rank.begin(), rank.end(), [&](int a, int b) {
        if (peaks[a].peak_over_median == peaks[b].peak_over_median) return a < b;
        return peaks[a].peak_over_median > peaks[b].peak_over_median;
    });
    Best best;
    std::array<int, words> sx{}, sxx{}, sxy{};
    int sy = 0, syy = 0;
    for (int j = 0; j < tones; ++j) {
        const int t = rank[j], y = peaks[t].signed_bin, n = j + 1;
        sy += y;
        syy += y * y;
        for (int m = 0; m < words; ++m) {
            const int x = int(schedule.position[t][m]) - positions / 2;
            sx[m] += x;
            sxx[m] += x * x;
            sxy[m] += x * y;
        }
        if (n != tones / 2 && n != tones) continue;
        const double my = double(sy) / n;
        const double yy = syy - my * sy;
        if (!(yy > 0)) continue;
        Best footprint;
        for (int m = 0; m < words; ++m) {
            const double mx = double(sx[m]) / n;
            const double score = fit_score(n, mx, my, sxx[m] - mx * sx[m],
                                           sxy[m] - mx * sy, yy);
            if (score > footprint.score) footprint = {score, m};
        }
        if (by_size) (*by_size)[n == tones] = footprint;
        if (footprint.score > best.score) best = footprint;
    }
    return best;
}

// This comparator knows only the frequency map. All words compete on measured soft energy.
Best known_map(const EnergyRows& energy, const Schedule& schedule, Map map) {
    std::array<int, positions> index;
    for (int p = 0; p < positions; ++p) {
        index[p] = band_edge + int(std::lround(map.slope * (p - positions / 2) + map.intercept));
        if (index[p] < 0 || index[p] >= band_size) throw std::logic_error("oracle aperture outside band");
    }
    std::array<int, words> sums{};
    for (int t = 0; t < tones; ++t)
        for (int m = 0; m < words; ++m) sums[m] += energy[t][index[schedule.position[t][m]]];
    return standardized_best(sums);
}

void emit(Trial& block, const Schedule& schedule, Map map, double db_energy,
          bool substituted = false, bool constant_line = false, double replacement_gain_db = 0.,
          int substituted_per_group = 1) {
    const double amplitude = std::sqrt(mm::undb(db_energy) / nsamples);
    const double replacement_amplitude = amplitude * std::sqrt(mm::undb(replacement_gain_db));
    double phase = block.phase;
    for (int t = 0; t < tones; ++t) {
        // Direct time-domain modulation, with no use of the receiver's fit or bin-rounding rule.
        double f = map.intercept;
        if (!constant_line) {
            f += map.slope * (int(schedule.position[t][block.word]) - 176);
            if (substituted && t % 8 < substituted_per_group) f = -205.;
            phase = block.phase + tau * schedule.quarter_phase[t] / 4.;
        }
        const auto step = std::polar(1., tau * f / 512.);
        const bool replaced = substituted && !constant_line && t % 8 < substituted_per_group;
        auto v = std::polar(replaced ? replacement_amplitude : amplitude, phase);
        for (int j = 0; j < nfft; ++j) {
            block.samples[pad + block.alignment + t * nfft + j] += static_cast<Sample>(v);
            v *= step;
        }
        if (constant_line) phase = std::remainder(phase + tau * f, tau);
    }
}

struct Scores {
    Best baseline, affine;
    std::array<Best, maps.size()> oracle;
    double visibility;
};

Scores score(const Trial& block, const Schedule& schedule, const FloatPlan& plan) {
    Peaks peaks;
    EnergyRows energy;
    if (!extract(block.samples, plan, peaks, &energy)) throw std::runtime_error("invalid affine fixture");
    Scores result{decode(block.samples, schedule, plan), blind(peaks.ridge, schedule), {}, peaks.visibility};
    for (size_t i = 0; i < maps.size(); ++i)
        result.oracle[i] = known_map(energy, schedule, maps[i]);
    return result;
}

struct Thresholds {
    double baseline, affine, base_half, affine_half, visibility;
    std::array<double, maps.size()> oracle{};
};

Thresholds calibrate(const std::vector<Schedule>& fixtures, const FloatPlan& plan) {
    std::array<std::array<double, 399>, 3 + maps.size()> values;
    for (int i = 0; i < 399; ++i) {
        Trial block(0xaff10001, i);
        const auto scores = score(block, fixtures[block.ordinal], plan);
        values[0][i] = scores.baseline.score;
        values[1][i] = scores.affine.score;
        values[2][i] = scores.visibility;
        for (size_t k = 0; k < maps.size(); ++k) values[3+k][i] = scores.oracle[k].score;
    }
    for (auto& v : values) std::sort(v.begin(), v.end());
    Thresholds cuts{values[0][395], values[1][395], values[0][397], values[1][397], values[2][395]};
    for (size_t k = 0; k < maps.size(); ++k) cuts.oracle[k] = values[3+k][395];
    return cuts;
}

struct Counts {
    int n{}, visible{}, gained{}, lost{};
    std::array<int, 4> correct{}, wrong{}, visible_correct{};
    void observe(const Scores& scores, const Thresholds& cuts, size_t which, int truth) {
        const auto word = [](Best b, double cut) { return b.score > cut ? b.word : -1; };
        const int priority = word(scores.baseline, cuts.base_half);
        const std::array accepted = {word(scores.baseline, cuts.baseline), word(scores.affine, cuts.affine),
            word(scores.oracle[which], cuts.oracle[which]),
            priority >= 0 ? priority : word(scores.affine, cuts.affine_half)};
        const bool seen = scores.visibility > cuts.visibility;
        ++n;
        visible += seen;
        for (size_t j = 0; j < accepted.size(); ++j) {
            const bool right = truth >= 0 && accepted[j] == truth;
            correct[j] += right;
            wrong[j] += accepted[j] >= 0 && !right;
            visible_correct[j] += seen && right;
        }
        if (truth >= 0) {
            gained += accepted[0] != truth && accepted[3] == truth;
            lost += accepted[0] == truth && accepted[3] != truth;
        }
    }
    void report(const std::string& name) const {
        std::cout << name << " n=" << n << " visible=" << visible;
        for (size_t j = 0; j < correct.size(); ++j)
            std::cout << " | " << correct[j] << '/' << wrong[j] << '/' << n - correct[j] - wrong[j]
                      << " v+c=" << visible_correct[j];
        std::cout << " | hybrid gained/lost=" << gained << '/' << lost << '\n';
    }
};

// Subtract a first row, divide by the signed gcd of integer differences, and
// retain all coordinates. Equal signatures are equivalent up to a nonzero
// real signed affine map on this fixed footprint (constant rows included).
int distinct_shapes(const Schedule& schedule, int retained) {
    using Shape = std::array<int, tones>;
    std::vector<Shape> shapes(words);
    for (int m = 0; m < words; ++m) {
        auto& shape = shapes[m];
        int divisor = 0, sign = 0;
        for (int t = 1; t < retained; ++t) {
            shape[t] = int(schedule.position[t][m]) - int(schedule.position[0][m]);
            divisor = std::gcd(divisor, shape[t]);
            if (sign == 0 && shape[t] != 0) sign = shape[t] > 0 ? 1 : -1;
        }
        if (divisor)
            for (int t = 1; t < retained; ++t) shape[t] /= sign * divisor;
    }
    std::sort(shapes.begin(), shapes.end());
    return std::unique(shapes.begin(), shapes.end()) - shapes.begin();
}

bool check_shapes(const std::vector<Schedule>& fixtures) {
    for (int kept : {64, 32, 2}) {
        int least = words, most = 0;
        for (const auto& schedule : fixtures) {
            int n = distinct_shapes(schedule, kept);
            least = std::min(least, n);
            most = std::max(most, n);
        }
        std::cout << "unrestricted signed-affine shapes; first " << kept << " rows, range "
                  << least << ".." << most << " of " << words << '\n';
    }
    // Show a non-unit, nonzero-offset two-word collision inside the actual fit box.
    const auto& schedule = fixtures.front();
    for (int m = 1; m < words; ++m) {
        const int u = int(schedule.position[0][0]) - positions / 2;
        const int v = int(schedule.position[1][0]) - positions / 2;
        const int x = int(schedule.position[0][m]) - positions / 2;
        const int y = int(schedule.position[1][m]) - positions / 2;
        if (u == v || x == y) continue;
        const double slope = double(v - u) / (y - x), intercept = u - slope * x;
        if (std::abs(slope) < min_scale || std::abs(slope) > max_scale ||
            std::abs(intercept) > max_intercept || std::abs(slope) == 1. || intercept == 0) continue;
        std::cout << "ordinal 0 first-two-row identity word 0 bins " << u << ',' << v
                  << " equals word " << m << " bins " << x << ',' << y << " at slope "
                  << v - u << '/' << y - x << ", intercept " << u << " - slope * " << x << '\n';
        return true;
    }
    return false;
}
} // namespace affine_trial

Best affine_blind_word(const Ridge& ridge, const Schedule& schedule) {
    return affine_trial::blind(ridge, schedule);
}

bool check_affine(const FloatPlan& plan, const std::vector<Schedule>& fixtures,
                  const std::vector<Schedule>& other) {
    using namespace affine_trial;
    const auto start = std::chrono::steady_clock::now();
    // Exact interior/edge regressions, including the zero-variance carrier.
    for (Map map : {Map{1., 3.}, Map{-max_scale, -max_intercept}, Map{min_scale, max_intercept}}) {
        const double mx = 4., my = map.slope * mx + map.intercept, xx = 10000.;
        if (std::abs(fit_score(64, mx, my, xx, map.slope * xx, map.slope * map.slope * xx) - 1.) > 1e-12)
            return false;
    }
    if (std::isfinite(fit_score(64, 0, 10, 100, 0, 0))) return false;
    if (!check_shapes(fixtures)) return false;
    const auto cuts = calibrate(fixtures, plan);
    std::cout << std::setprecision(7) << "baseline, affine, baseline-half, affine-half, visibility cutoffs "
              << cuts.baseline << ' ' << cuts.affine << ' ' << cuts.base_half << ' ' << cuts.affine_half
              << ' ' << cuts.visibility << " oracle cutoffs";
    for (auto c : cuts.oracle) std::cout << ' ' << c;
    std::cout << "\ncorrect/wrong/absent, visible+correct: nominal per-tone | blind affine | known map | hybrid\n";
    Counts quiet;
    std::array<Counts, maps.size()> quiet_oracle;
    for (int i = 0; i < 400; ++i) {
        Trial block(0xaff10002, i);
        const auto scores = score(block, fixtures[block.ordinal], plan);
        quiet.observe(scores, cuts, 0, -1);
        for (size_t k = 0; k < maps.size(); ++k) quiet_oracle[k].observe(scores, cuts, k, -1);
    }
    quiet.report("quiet");
    std::cout << "quiet oracle false counts (nominal, compressed, reversed/compressed)";
    for (const auto& c : quiet_oracle) std::cout << ' ' << c.wrong[2];
    std::cout << '\n';
    constexpr std::array energies{18., 27., 34.};
    std::array<std::array<Counts, energies.size()>, maps.size()> rows;
    Counts outliers, wrong_schedule, line;
    for (int i = 0; i < 48; ++i) {
        const Trial clean(0xaff10003, i);
        const auto& schedule = fixtures[clean.ordinal];
        for (size_t k = 0; k < maps.size(); ++k) {
            for (size_t e = 0; e < energies.size(); ++e) {
                auto block = clean;
                emit(block, schedule, maps[k], energies[e]);
                rows[k][e].observe(score(block, schedule, plan), cuts, k, block.word);
            }
        }
        auto damaged = clean;
        emit(damaged, schedule, maps[1], 34., true);
        outliers.observe(score(damaged, schedule, plan), cuts, 1, damaged.word);
        Trial wrong(0xaff10004, i);
        emit(wrong, other[wrong.ordinal], maps[1], 34.);
        wrong_schedule.observe(score(wrong, fixtures[wrong.ordinal], plan), cuts, 1, -1);
        Trial carrier(0xaff10005, i);
        emit(carrier, fixtures[carrier.ordinal], maps[1], 34., false, true);
        line.observe(score(carrier, fixtures[carrier.ordinal], plan), cuts, 1, -1);
    }
    const std::array<std::string,3> names{"nominal", "compressed", "reversed/compressed"};
    for (size_t k = 0; k < maps.size(); ++k) {
        for (size_t e = 0; e < energies.size(); ++e)
            rows[k][e].report(names[k] + " " + std::to_string(int(energies[e])));
    }
    outliers.report("compressed 34, eight substituted tones");
    wrong_schedule.report("compressed 34 wrong schedule");
    line.report("unmodulated line 34");
    std::cout << "affine seconds " << std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() << '\n';
    return true;
}

namespace affine_trial {
struct AnchoredFit {
    Best best;
    int proposals{};
};

struct AnchorRows {
    std::array<int, tones> index{};
    int count{};
};

enum class AnchorPolicy { strongest, group_runner_up, all_rows };

AnchorRows select_anchors(const Ridge& ridge, AnchorPolicy policy) {
    std::array<int, tones> rank;
    std::iota(rank.begin(), rank.end(), 0);
    for (const auto& row : ridge) if (!std::isfinite(row.peak_over_median)) return {};
    if (policy == AnchorPolicy::all_rows) return {rank, tones};
    std::sort(rank.begin(), rank.end(), [&](int a, int b) {
        if (ridge[a].peak_over_median == ridge[b].peak_over_median) return a < b;
        return ridge[a].peak_over_median > ridge[b].peak_over_median;
    });
    std::array<bool, tones> used{};
    if (policy == AnchorPolicy::strongest)
        for (int j = 0; j < 8; ++j) used[rank[j]] = true;
    const int group_rank = policy == AnchorPolicy::group_runner_up ? 1 : 0;
    std::array<int, 8> group_counts{};
    for (int t : rank) {
        if (group_counts[t / 8]++ == group_rank) used[t] = true;
    }
    AnchorRows anchors;
    for (int t = 0; t < tones; ++t) if (used[t]) anchors.index[anchors.count++] = t;
    return anchors;
}

struct AllAnchorScores {
    Best strongest, expanded, all;
    int proposals{};
};

// The map aims at floor(y + (p-x)*dy/dx + 1/2), including exact negative
// half-bin targets. Q20 stores a nearest-rounded slope. Across at most 351
// positions this gives at most 175.5 Q20 units of error. Biasing upward by
// 176 units therefore includes exact ties; the largest displacement (351.5
// units) is below 1/(2*351) bin, the minimum possible nonzero distance to
// a half-bin for any anchor denominator here. No non-tie can cross a bin.
struct ExactBinMap {
    static constexpr int units = 1 << 20, bias = positions / 2;
    static_assert(2 * (positions - 1) * positions < units);
    // Bounds for both the scaled slope numerator and intermediate index sum.
    static_assert(2LL * band_edge * units + positions < INT32_MAX);
    static_assert((2LL * band_edge + 2LL * positions + 1) * units < INT32_MAX);
    int slope_units{}, base_units{};
    int index(int position) const { return (base_units + slope_units * position) >> 20; }
};

bool anchor_map(int x, int y, int dx, int dy, ExactBinMap& map) {
    static_assert(min_scale == .5 && max_scale == 1.1 && max_intercept == 24.);
    const int denominator = std::abs(dx);
    if (denominator < 64 || 2 * std::abs(dy) < denominator || 10 * std::abs(dy) > 11 * denominator ||
        std::abs(y * dx - dy * x) > 24 * denominator) return false;
    const int numerator = dx > 0 ? dy : -dy;
    constexpr int q = ExactBinMap::units;
    const int nearest = numerator >= 0 ? denominator / 2 : -denominator / 2;
    const int rounded = (numerator * q + nearest) / denominator;
    map = {rounded, (band_edge + y) * q - x * rounded + q / 2 + ExactBinMap::bias};
    return true;
}

// Each anchor pair supplies a map, and all 64 soft rows contribute to its score.
// When requested, row membership bits also recover the two smaller maxima in this pass.
template <bool track_subsets>
AllAnchorScores score_pairs(const Ridge& ridge, const EnergyRows& energy, const Schedule& schedule,
                            const AnchorRows& anchors, const std::array<unsigned, tones>& membership) {
    AllAnchorScores result;
    Best runner_up;
    for (int m = 0; m < words; ++m) {
        std::array<int, tones> x;
        for (int t = 0; t < tones; ++t) x[t] = int(schedule.position[t][m]) - positions / 2;
        for (int i = 0; i < anchors.count; ++i) {
            const int t = anchors.index[i];
            for (int j = i + 1; j < anchors.count; ++j) {
                const int u = anchors.index[j], dx = x[u] - x[t];
                ExactBinMap fit;
                if (!anchor_map(x[t], ridge[t].signed_bin, dx, ridge[u].signed_bin - ridge[t].signed_bin, fit))
                    continue;
                ++result.proposals;
                int sum = 0;
                for (int k = 0; k < tones; ++k) {
                    sum += energy[k][fit.index(x[k])];
                }
                const Best candidate{double(sum), m};
                if (sum > result.all.score) result.all = candidate;
                if constexpr (track_subsets) {
                    const unsigned common = membership[t] & membership[u];
                    if ((common & 1) && sum > result.strongest.score) result.strongest = candidate;
                    if ((common & 2) && sum > runner_up.score) runner_up = candidate;
                }
            }
        }
    }
    if constexpr (track_subsets)
        result.expanded = runner_up.score > result.strongest.score ? runner_up : result.strongest;
    return result;
}

AnchoredFit score_anchors(const Ridge& ridge, const EnergyRows& energy, const Schedule& schedule,
                          const AnchorRows& anchors) {
    const auto result = score_pairs<false>(ridge, energy, schedule, anchors, {});
    return {result.all, result.proposals};
}

AllAnchorScores score_all_policies(const Ridge& ridge, const EnergyRows& energy, const Schedule& schedule) {
    const auto all = select_anchors(ridge, AnchorPolicy::all_rows);
    if (all.count == 0) return {};
    std::array<unsigned, tones> membership{};
    const auto strongest = select_anchors(ridge, AnchorPolicy::strongest);
    for (int i = 0; i < strongest.count; ++i) membership[strongest.index[i]] |= 1;
    const auto second = select_anchors(ridge, AnchorPolicy::group_runner_up);
    for (int i = 0; i < second.count; ++i) membership[second.index[i]] |= 2;
    return score_pairs<true>(ridge, energy, schedule, all, membership);
}

AnchoredFit anchored(const Ridge& ridge, const EnergyRows& energy, const Schedule& schedule) {
    return score_anchors(ridge, energy, schedule, select_anchors(ridge, AnchorPolicy::strongest));
}
} // namespace affine_trial

[[maybe_unused]] Best affine_anchored_word(std::span<const Sample> samples, const Schedule& schedule,
                                          const FloatPlan& plan) {
    affine_trial::Peaks peaks;
    affine_trial::EnergyRows energy;
    if (!affine_trial::extract(samples, plan, peaks, &energy)) return {};
    return affine_trial::anchored(peaks.ridge, energy, schedule).best;
}

namespace affine_trial {
struct FollowOn {
    Best ls, soft;
    std::array<Best, maps.size()> oracle;
    double visibility;
    int proposals;
    double seconds;
};

FollowOn follow_on(const Trial& block, const Schedule& schedule, const FloatPlan& plan) {
    const auto start = std::chrono::steady_clock::now();
    Peaks peaks;
    EnergyRows energy;
    if (!extract(block.samples, plan, peaks, &energy)) throw std::runtime_error("invalid anchor fixture");
    const auto fitted = anchored(peaks.ridge, energy, schedule);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    FollowOn r{blind(peaks.ridge, schedule), fitted.best, {}, peaks.visibility, fitted.proposals, seconds};
    for (size_t k = 0; k < maps.size(); ++k) r.oracle[k] = known_map(energy, schedule, maps[k]);
    return r;
}

struct AnchorCuts {
    double ls, soft, visibility;
    std::array<double, maps.size()> oracle{};
};

struct AnchorCounts {
    int n{}, visible{}, gain{}, loss{}, proposals{};
    double seconds{};
    std::array<int, 3> right{}, wrong{}, visible_right{};
    void observe(const FollowOn& s, const AnchorCuts& c, size_t k, int truth) {
        const std::array accepted{s.ls.score > c.ls ? s.ls.word : -1,
                                  s.soft.score > c.soft ? s.soft.word : -1,
                                  s.oracle[k].score > c.oracle[k] ? s.oracle[k].word : -1};
        ++n;
        const bool seen = s.visibility > c.visibility;
        visible += seen;
        proposals += s.proposals;
        seconds += s.seconds;
        for (size_t j = 0; j < accepted.size(); ++j) {
            const bool correct = truth >= 0 && accepted[j] == truth;
            right[j] += correct;
            wrong[j] += accepted[j] >= 0 && !correct;
            visible_right[j] += seen && correct;
        }
        if (truth >= 0) {
            gain += accepted[1] == truth && accepted[0] != truth;
            loss += accepted[0] == truth && accepted[1] != truth;
        }
    }
    void report(const std::string& name) const {
        std::cout << name << " n=" << n << " visible=" << visible;
        for (size_t j = 0; j < right.size(); ++j)
            std::cout << " | " << right[j] << '/' << wrong[j] << '/' << n - right[j] - wrong[j]
                      << " v+c=" << visible_right[j];
        std::cout << " | soft gained/lost=" << gain << '/' << loss
                  << " avg proposals=" << double(proposals)/n << " avg soft ms=" << 1e3*seconds/n << '\n';
    }
};
} // namespace affine_trial

bool check_affine_refinement(const FloatPlan& plan, const std::vector<Schedule>& fixtures,
                             const std::vector<Schedule>& other) {
    using namespace affine_trial;
    std::array<std::array<double, 399>, 3 + maps.size()> cal;
    for (int i = 0; i < 399; ++i) {
        Trial block(0xaff20001, i);
        const auto s = follow_on(block, fixtures[block.ordinal], plan);
        cal[0][i] = s.ls.score;
        cal[1][i] = s.soft.score;
        cal[2][i] = s.visibility;
        for (size_t k = 0; k < maps.size(); ++k) cal[3+k][i] = s.oracle[k].score;
    }
    for (auto& a : cal) std::sort(a.begin(), a.end());
    AnchorCuts cuts{cal[0][395], cal[1][395], cal[2][395]};
    for (size_t k = 0; k < maps.size(); ++k) cuts.oracle[k] = cal[3+k][395];
    std::cout << std::setprecision(7) << "follow-on LS, anchored soft, visibility cutoffs "
              << cuts.ls << ' ' << cuts.soft << ' ' << cuts.visibility << " oracle";
    for (auto c : cuts.oracle) std::cout << ' ' << c;
    std::cout << "\ncorrect/wrong/absent, visible+correct: LS | anchored soft | oracle\n";
    AnchorCounts quiet, wrong_schedule, line;
    std::array<int, maps.size()> quiet_oracle{};
    for (int i = 0; i < 400; ++i) {
        Trial block(0xaff20002, i);
        const auto scores = follow_on(block, fixtures[block.ordinal], plan);
        quiet.observe(scores, cuts, 0, -1);
        for (size_t k = 0; k < maps.size(); ++k) quiet_oracle[k] += scores.oracle[k].score > cuts.oracle[k];
    }
    quiet.report("quiet");
    std::cout << "quiet oracle false counts";
    for (int n : quiet_oracle) std::cout << ' ' << n;
    std::cout << '\n';
    struct Case { int map, db; bool substituted = false; };
    constexpr std::array<Case, 12> cases{{{0,18}, {0,27}, {1,18}, {1,24}, {1,27}, {1,34},
        {2,18}, {2,24}, {2,27}, {2,34}, {1,27,true}, {1,34,true}}};
    std::array<AnchorCounts, cases.size()> rows;
    for (int i = 0; i < 48; ++i) {
        const Trial clean(0xaff20003, i);
        const auto& schedule = fixtures[clean.ordinal];
        for (size_t c = 0; c < cases.size(); ++c) {
            auto block = clean;
            auto config = cases[c];
            emit(block, schedule, maps[config.map], config.db, config.substituted);
            rows[c].observe(follow_on(block, schedule, plan), cuts, config.map, block.word);
        }
    }
    for (size_t c = 0; c < cases.size(); ++c) {
        const auto config = cases[c];
        rows[c].report("map " + std::to_string(config.map) + " at " + std::to_string(config.db)
                       + (config.substituted ? " dB, eight substituted" : " dB"));
    }
    for (int i = 0; i < 64; ++i) {
        Trial wrong(0xaff20004, i);
        emit(wrong, other[wrong.ordinal], maps[1], 34.);
        wrong_schedule.observe(follow_on(wrong, fixtures[wrong.ordinal], plan), cuts, 1, -1);
        Trial carrier(0xaff20005, i);
        emit(carrier, fixtures[carrier.ordinal], maps[1], 34., false, true);
        line.observe(follow_on(carrier, fixtures[carrier.ordinal], plan), cuts, 1, -1);
    }
    wrong_schedule.report("compressed 34 wrong schedule");
    line.report("unmodulated line 34");
    return true;
}

namespace affine_trial {
struct ContaminationScores {
    std::array<Best, 2> ls;
    Best original, peeled, oracle;
    double visibility{};
    AnchorRows original_rows, peeled_rows;
};

std::optional<ContaminationScores> contamination_scores(std::span<const Sample> samples,
        const Schedule& schedule, const FloatPlan& plan) {
    Peaks peaks;
    EnergyRows energy;
    if (!extract(samples, plan, peaks, &energy)) return std::nullopt;
    ContaminationScores s;
    blind(peaks.ridge, schedule, &s.ls);
    s.original_rows = select_anchors(peaks.ridge, AnchorPolicy::strongest);
    s.peeled_rows = select_anchors(peaks.ridge, AnchorPolicy::group_runner_up);
    s.original = score_anchors(peaks.ridge, energy, schedule, s.original_rows).best;
    s.peeled = score_anchors(peaks.ridge, energy, schedule, s.peeled_rows).best;
    s.oracle = known_map(energy, schedule, maps[1]);
    s.visibility = peaks.visibility;
    return s;
}

struct ContaminationCounts {
    int total{}, missing{}, visible{}, peeled_admitted{}, gain{}, loss{};
    std::array<int, 6> right{}, wrong{};
    std::array<int, 2> drawn{}, bad{}, all_bad{};
    void observe(const std::optional<ContaminationScores>& scores, int truth, bool substituted,
                 double original_cut, double expanded_cut) {
        ++total;
        if (!scores) { ++missing; return; }
        const auto& s = *scores;
        visible += s.visibility > 10.26171;
        const bool peeled_wins = s.peeled.score > s.original.score;
        Best expanded = peeled_wins ? s.peeled : s.original;
        const auto admitted = [](Best b, double cut) { return b.score > cut ? b.word : -1; };
        // The earlier independent diagnostic supplied the LS, old soft and oracle thresholds.
        // The original and expanded soft banks also have a fresh paired calibration here.
        const std::array words_out{admitted(s.ls[0], .4905372), admitted(s.ls[1], .4905372),
            admitted(s.original, 290), admitted(s.original, original_cut),
            admitted(expanded, expanded_cut), admitted(s.oracle, 4.893102)};
        for (size_t j = 0; j < words_out.size(); ++j) {
            const bool ok = truth >= 0 && words_out[j] == truth;
            right[j] += ok;
            wrong[j] += words_out[j] >= 0 && !ok;
        }
        peeled_admitted += words_out[4] >= 0 && peeled_wins;
        if (truth >= 0) {
            gain += words_out[4] == truth && words_out[3] != truth;
            loss += words_out[4] != truth && words_out[3] == truth;
        }
        if (substituted) {
            const std::array sets{s.original_rows, s.peeled_rows};
            for (size_t j = 0; j < sets.size(); ++j) {
                const auto& rows = sets[j];
                const int affected = std::count_if(rows.index.begin(), rows.index.begin() + rows.count,
                                                   [](int t) { return t % 8 == 0; });
                bad[j] += affected;
                drawn[j] += rows.count;
                all_bad[j] += rows.count > 0 && affected == rows.count;
            }
        }
    }
    void report(const std::string& name) const {
        std::cout << name << " n=" << total << " unscored=" << missing << " visible=" << visible;
        for (size_t j = 0; j < right.size(); ++j)
            std::cout << " | " << right[j] << '/' << wrong[j] << '/' << total - missing - right[j] - wrong[j];
        std::cout << " | peeled winner admitted=" << peeled_admitted << " gained/lost=" << gain << '/' << loss;
        if (drawn[0])
            std::cout << " | bad/anchors old " << bad[0] << '/' << drawn[0] << " peeled " << bad[1]
                      << '/' << drawn[1] << " all bad trials " << all_bad[0] << '/' << all_bad[1];
        std::cout << '\n';
    }
};
} // namespace affine_trial

bool check_affine_contamination(const FloatPlan& plan, const std::vector<Schedule>& fixtures,
                                const std::vector<Schedule>& other) {
    using namespace affine_trial;
    if (contamination_scores({}, fixtures.front(), plan)) return false;
    std::array<double, 399> originals, expanded;
    for (int i = 0; i < 399; ++i) {
        Trial block(0xaff30001, i);
        const auto s = contamination_scores(block.samples, fixtures[block.ordinal], plan);
        if (!s) return false;
        originals[i] = s->original.score;
        expanded[i] = std::max(s->original.score, s->peeled.score);
    }
    std::sort(originals.begin(), originals.end());
    std::sort(expanded.begin(), expanded.end());
    const double old_cut = originals[395], new_cut = expanded[395];
    const double bright_db = 15.;
    const double bright_total_db = mm::db((56. + 8. * mm::undb(bright_db)) / 64.);
    std::cout << std::setprecision(7) << "paired original/expanded cutoffs " << old_cut << '/' << new_cut
              << " total dB increment with replacements " << bright_total_db
              << "\ncorrect/wrong/absent: LS strongest 32 | LS all 64 | old soft >290 | old paired | expanded paired | oracle\n";
    std::array<ContaminationCounts, 6> cases;
    ContaminationCounts quiet, wrong, line;
    for (int i = 0; i < 400; ++i) {
        Trial block(0xaff30002, i);
        quiet.observe(contamination_scores(block.samples, fixtures[block.ordinal], plan), -1, false, old_cut, new_cut);
    }
    quiet.report("quiet");
    constexpr std::array levels{27., 34.};
    for (int i = 0; i < 48; ++i) {
        const Trial clean(0xaff30003, i);
        const auto& schedule = fixtures[clean.ordinal];
        for (size_t e = 0; e < levels.size(); ++e) {
            for (int fault = 0; fault < 3; ++fault) {
                auto block = clean;
                emit(block, schedule, maps[1], levels[e], fault != 0, false, fault == 2 ? bright_db : 0.);
                cases[e*3 + fault].observe(contamination_scores(block.samples, schedule, plan), block.word,
                                           fault != 0, old_cut, new_cut);
            }
        }
    }
    const std::array<std::string, 3> names{"intact", "eight equal-power", "eight +15 dB"};
    for (size_t e = 0; e < levels.size(); ++e)
        for (size_t k = 0; k < names.size(); ++k)
            cases[e*3+k].report("map 1 " + std::to_string(int(levels[e])) + " " + names[k]);
    for (int i = 0; i < 64; ++i) {
        Trial block(0xaff30004, i);
        emit(block, other[block.ordinal], maps[1], 34., true, false, bright_db);
        wrong.observe(contamination_scores(block.samples, fixtures[block.ordinal], plan), -1, true, old_cut, new_cut);
        Trial carrier(0xaff30005, i);
        emit(carrier, fixtures[carrier.ordinal], maps[1], 34. + bright_total_db, false, true);
        line.observe(contamination_scores(carrier.samples, fixtures[carrier.ordinal], plan), -1, false, old_cut, new_cut);
    }
    wrong.report("wrong-schedule 34 eight +15 dB");
    line.report("line same total energy as 34 eight +15 dB");
    return true;
}

namespace affine_trial {
struct PairScreen {
    std::array<Best, 3> banks;
    std::array<Best, maps.size()> oracles;
    double visibility{};
    int proposals{};
};

struct PairTiming {
    int count{};
    // Shared extraction followed by each proposal bank. Gating is outside this diagnostic.
    double extract{}, narrow{}, expanded{}, all{};
};

PairScreen screen_pairs(const Trial& block, const Schedule& schedule, const FloatPlan& plan,
                        PairTiming* timing = nullptr) {
    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();
    Peaks peaks;
    EnergyRows energy;
    if (!extract(block.samples, plan, peaks, &energy)) throw std::runtime_error("unscored all-pairs fixture");
    const auto extracted = Clock::now();
    const auto& ridge = peaks.ridge;
    const auto scores = score_all_policies(ridge, energy, schedule);
    if (timing) {
        const auto begin = Clock::now();
        const auto first = anchored(ridge, energy, schedule).best;
        const auto first_end = Clock::now();
        const auto second = score_anchors(ridge, energy, schedule,
                                         select_anchors(ridge, AnchorPolicy::group_runner_up)).best;
        const auto second_end = Clock::now();
        const auto full = score_anchors(ridge, energy, schedule,
                                       select_anchors(ridge, AnchorPolicy::all_rows)).best;
        const auto end = Clock::now();
        const auto equal = [](Best a, Best b) { return a.word == b.word && a.score == b.score; };
        if (!equal(first, scores.strongest) || !equal(full, scores.all) ||
            !equal(second.score > first.score ? second : first, scores.expanded))
            throw std::runtime_error("single-pass all-pairs disagrees with separate anchor banks");
        ++timing->count;
        timing->extract += std::chrono::duration<double>(extracted - start).count();
        timing->narrow += std::chrono::duration<double>(first_end - begin).count();
        timing->expanded += std::chrono::duration<double>(second_end - begin).count();
        timing->all += std::chrono::duration<double>(end - second_end).count();
    }
    PairScreen out{{scores.strongest, scores.expanded, scores.all}, {}, peaks.visibility, scores.proposals};
    for (size_t k = 0; k < maps.size(); ++k) out.oracles[k] = known_map(energy, schedule, maps[k]);
    return out;
}

struct PairCounts {
    int n{}, visible{}, gains{}, losses{};
    std::array<int, 4> correct{}, wrong{}, visible_correct{};
    uint64_t feasible{};
    void add(const PairScreen& s, size_t k, const std::array<double, 7>& cuts, int truth) {
        ++n;
        const bool seen = s.visibility > cuts[6];
        visible += seen;
        feasible += s.proposals;
        const std::array banks{s.banks[0], s.banks[1], s.banks[2], s.oracles[k]};
        std::array<bool, 4> right{};
        for (size_t j = 0; j < banks.size(); ++j) {
            const bool accepted = banks[j].score > cuts[j == 3 ? 3 + k : j];
            right[j] = accepted && truth >= 0 && banks[j].word == truth;
            correct[j] += right[j];
            wrong[j] += accepted && !right[j];
            visible_correct[j] += seen && right[j];
        }
        if (truth >= 0) {
            gains += right[2] && !right[1];
            losses += !right[2] && right[1];
        }
    }
    void report(const std::string& name) const {
        std::cout << name << " n=" << n << " visible=" << visible;
        for (size_t j = 0; j < correct.size(); ++j)
            std::cout << " | " << correct[j] << '/' << wrong[j] << '/' << n - correct[j] - wrong[j]
                      << " v+c=" << visible_correct[j];
        std::cout << " | all vs expanded gained/lost=" << gains << '/' << losses
                  << " feasible/record=" << double(feasible) / n << '\n';
    }
};
} // namespace affine_trial

// Exercise the actual integer predictor against independent rational division
// for every admissible slope and lattice displacement. Integer translations
// cancel from this comparison. No IQ samples or Monte Carlo draws enter it.
bool check_anchor_arithmetic() {
    using namespace affine_trial;
    ExactBinMap fit;
    if (!anchor_map(-176, -93, 66, 35, fit) || fit.index(-11) != band_edge - 5) return false;
    if (!anchor_map(-176, -144, 66, 45, fit) || fit.index(0) != band_edge - 24) return false;
    uint64_t count = 0;
    for (int d = 64; d < positions; ++d) {
        for (int n = -2*d; n <= 2*d; ++n) {
            const bool admissible = 2 * std::abs(n) >= d && 10 * std::abs(n) <= 11*d;
            ExactBinMap reversed;
            if (anchor_map(0, 0, d, n, fit) != admissible ||
                anchor_map(0, 0, -d, -n, reversed) != admissible) return false;
            if (!admissible) continue;
            if (fit.slope_units != reversed.slope_units || fit.base_units != reversed.base_units) return false;
            for (int shift = 1 - positions; shift < positions; ++shift) {
                const int numerator = 2 * (band_edge * d + shift * n) + d;
                const int denominator = 2 * d;
                const int exact = numerator / denominator - (numerator % denominator < 0);
                if (fit.index(shift) != exact) return false;
                ++count;
            }
        }
    }
    std::cout << "exact anchor arithmetic, rational cases " << count << '\n';
    return true;
}

bool check_anchor_boundary_iq(const FloatPlan& plan, const std::vector<Schedule>& fixtures) {
    using namespace affine_trial;
    int count = 0, correct = 0;
    double lowest = INFINITY;
    for (double b : {-1.1, -.5, .5, 1.1}) {
        for (double a : {-24., 24.}) {
            for (int i = 0; i < 2; ++i) {
                Trial block(0xaff41001, i);
                const auto& schedule = fixtures[block.ordinal];
                emit(block, schedule, {b, a}, 44.);
                Peaks peaks;
                EnergyRows energy;
                if (!extract(block.samples, plan, peaks, &energy)) return false;
                auto rows = select_anchors(peaks.ridge, AnchorPolicy::all_rows);
                const auto best = score_anchors(peaks.ridge, energy, schedule, rows).best;
                ++count;
                correct += best.word == int(block.word);
                lowest = std::min(lowest, best.score);
            }
        }
    }
    std::cout << "boundary IQ 44 dB, true word selected " << correct << '/' << count
              << ", lowest winning soft score " << lowest << '\n';
    return correct == count;
}

bool check_affine_all_pairs(const FloatPlan& plan, const std::vector<Schedule>& fixtures,
                            const std::vector<Schedule>& other) {
    using namespace affine_trial;
    if (!check_anchor_arithmetic() || !check_anchor_boundary_iq(plan, fixtures)) return false;
    std::array<std::array<double, 399>, 7> calibration;
    for (int i = 0; i < 399; ++i) {
        Trial block(0xaff40001, i);
        const auto s = screen_pairs(block, fixtures[block.ordinal], plan);
        for (size_t k = 0; k < 3; ++k) {
            calibration[k][i] = s.banks[k].score;
            calibration[3+k][i] = s.oracles[k].score;
        }
        calibration[6][i] = s.visibility;
    }
    std::array<double, 7> cuts;
    std::cout << std::setprecision(7) << "strongest/expanded/all, three oracle and visibility cutoffs";
    for (size_t k = 0; k < cuts.size(); ++k) {
        std::sort(calibration[k].begin(), calibration[k].end());
        cuts[k] = calibration[k][395];
        std::cout << ' ' << cuts[k];
    }
    std::cout << "\ncorrect/wrong/absent and visible+correct: original | expanded | all | oracle\n" << std::flush;
    PairCounts quiet, wrong, line;
    PairTiming quiet_timing, hot_timing;
    for (int i = 0; i < 400; ++i) {
        Trial block(0xaff40002, i);
        quiet.add(screen_pairs(block, fixtures[block.ordinal], plan, i < 8 ? &quiet_timing : nullptr), 1, cuts, -1);
    }
    quiet.report("quiet (positive oracle)");
    constexpr std::array levels{24., 27.};
    std::array<std::array<std::array<PairCounts, 3>, levels.size()>, maps.size()> rows;
    for (int i = 0; i < 32; ++i) {
        const Trial clean(0xaff40003, i);
        const auto& schedule = fixtures[clean.ordinal];
        for (size_t k = 0; k < maps.size(); ++k) {
            for (size_t j = 0; j < levels.size(); ++j) {
                for (int hot = 0; hot <= 2; ++hot) {
                    auto block = clean;
                    emit(block, schedule, maps[k], levels[j], hot > 0, false, 15., hot);
                    const bool timed = i < 8 && k == 1 && j == 1 && hot == 2;
                    rows[k][j][hot].add(screen_pairs(block, schedule, plan, timed ? &hot_timing : nullptr),
                                        k, cuts, block.word);
                }
            }
        }
    }
    for (size_t k = 0; k < maps.size(); ++k)
        for (size_t j = 0; j < levels.size(); ++j)
            for (int hot = 0; hot <= 2; ++hot)
                rows[k][j][hot].report("map " + std::to_string(k) + " " + std::to_string(int(levels[j]))
                                       + " dB, hot/group " + std::to_string(hot));
    const double two_hot_gain = mm::db((48. + 16. * mm::undb(15.)) / 64.);
    for (int i = 0; i < 48; ++i) {
        Trial block(0xaff40004, i);
        emit(block, other[block.ordinal], maps[1], 27., true, false, 15., 2);
        wrong.add(screen_pairs(block, fixtures[block.ordinal], plan), 1, cuts, -1);
        Trial carrier(0xaff40005, i);
        emit(carrier, fixtures[carrier.ordinal], maps[1], 27. + two_hot_gain, false, true);
        line.add(screen_pairs(carrier, fixtures[carrier.ordinal], plan), 1, cuts, -1);
    }
    wrong.report("wrong-schedule, +compressed 27, two hot/group");
    line.report("line, same total energy");
    std::cout << "two-hot total gain, dB " << two_hot_gain << '\n';
    for (const auto* timing : {&quiet_timing, &hot_timing}) {
        const double ms = 1e3 / timing->count;
        std::cout << (timing == &quiet_timing ? "quiet" : "positive 27, two hot")
                  << " elapsed ms/block (shared FFT, strongest, expanded, all) "
                  << timing->extract * ms << ' ' << timing->narrow * ms << ' ' << timing->expanded * ms
                  << ' ' << timing->all * ms << " averaged over " << timing->count << '\n';
    }
    return true;
}
