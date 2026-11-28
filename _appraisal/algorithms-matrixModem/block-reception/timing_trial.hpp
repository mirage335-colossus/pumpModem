// Optional bounded comparison for sub-sample timing search. Included inside
// block_reception.cpp's anonymous namespace after the original scorer helpers.
struct TimingCapture {
    std::array<ProjectionBlock, 3> projection{};
    bool valid{};
};
struct TimingResults {
    Best sparse, dense;
    double dense_time = NAN;
    std::array<Best, 11> audit{};
    bool audit_match = true;
};
constexpr std::array<double, 3> timing_rf_cycles = {-.5, 0., .5};
constexpr std::array<int, 11> timing_audit_grid = {0, 127, 128, 191, 192, 193, 320, 321, 384, 385, 512};

constexpr int offset_index(int offset) { return offset == -64 ? 0 : offset == 0 ? 1 : 2; }

TimingResults score_timing(const TimingCapture& capture, const Schedule& schedule, bool audit = false) {
    constexpr int grid_count = 513;
    constexpr std::array<Sample, 4> undo_phase = {{{1, 0}, {0, -1}, {-1, 0}, {0, 1}}};
    // Quarter-sample timing rotations use signed physical FFT frequencies;
    // wrapped bins are equivalent only at integer timing corrections.
    struct Rotations {
        std::array<std::array<Sample, positions>, grid_count> timing{};
        std::array<std::array<Sample, tones>, timing_rf_cycles.size()> rf{};
        Rotations() {
            for (int gi = 0; gi < grid_count; ++gi) {
                const double fine = -64. + gi * .25;
                const double delta = fine - (fine < -32. ? -64. : fine > 32. ? 64. : 0.);
                for (int p = 0; p < positions; ++p)
                    timing[gi][p] = static_cast<Sample>(std::polar(1., tau * (p - 176) * delta / nfft));
            }
            for (size_t r = 0; r < timing_rf_cycles.size(); ++r)
                for (int t = 0; t < tones; ++t)
                    rf[r][t] = static_cast<Sample>(std::polar(1., -tau * timing_rf_cycles[r] * (t + .5) / tones));
        }
    };
    static const Rotations rotations{};

    TimingResults result{};
    // Preserve the existing five-point comparator exactly, including its
    // operation order and score bit patterns.
    coherent_search(capture.projection[0], schedule, -64, -64, result.sparse);
    coherent_search(capture.projection[1], schedule, 0, 0, result.sparse);
    coherent_search(capture.projection[1], schedule, 0, -32, result.sparse);
    coherent_search(capture.projection[1], schedule, 0, 32, result.sparse);
    coherent_search(capture.projection[2], schedule, 64, 64, result.sparse);
    std::array<Projection, tones> corrected{};
    std::array<WordRow, tones> gathered{};
    std::array<WordRow, tones> tree{};
    std::array<float, words> sums{};
    for (int gi = 0; gi < grid_count; ++gi) {
        const double fine = -64. + gi * .25;
        const int anchor = fine < -32. ? -64 : fine > 32. ? 64 : 0; // central wins ties
        const auto& bins = capture.projection[offset_index(anchor)];
        for (int t = 0; t < tones; ++t) {
            for (int p = 0; p < positions; ++p)
                corrected[t][p] = bins[t][p] * rotations.timing[gi][p]
                    * undo_phase[schedule.quarter_phase[t]];
            for (int m = 0; m < words; ++m) gathered[t][m] = corrected[t][schedule.position[t][m]];
        }
        Best time_best{};
        for (size_t r = 0; r < timing_rf_cycles.size(); ++r) {
            for (int t = 0; t < tones; ++t)
                for (int m = 0; m < words; ++m) tree[t][m] = gathered[t][m] * rotations.rf[r][t];
            for (int width = 2; width <= tones; width *= 2) {
                sums.fill(0.f);
                for (int t = 0; t < tones; t += width) {
                    auto& left = tree[t];
                    const auto& right = tree[t + width / 2];
                    for (int m = 0; m < words; ++m) {
                        left[m] += right[m];
                        sums[m] += std::norm(left[m]);
                    }
                }
                const Best at_rung = standardized_best(sums);
                if (at_rung.score > time_best.score) time_best = at_rung;
            }
        }
        if (time_best.score > result.dense.score) {
            result.dense = time_best;
            result.dense_time = fine;
        }
        for (size_t a = 0; a < timing_audit_grid.size(); ++a)
            if (gi == timing_audit_grid[a]) result.audit[a] = time_best;
    }
    // Independent direct-double reference at edges, handoff, and ±16 samples.
    // It evaluates every word, RF hypothesis, and rung from the same projected
    // samples, with a straightforward tone sum instead of the optimized tree.
    if (audit) for (size_t a = 0; a < timing_audit_grid.size(); ++a) {
        const int gi = timing_audit_grid[a];
        const double fine = -64. + gi * .25;
        const int anchor = fine < -32. ? -64 : fine > 32. ? 64 : 0;
        const auto& bins = capture.projection[offset_index(anchor)];
        Best reference{};
        for (double rf : timing_rf_cycles) {
            for (int width = 2; width <= tones; width *= 2) {
                std::array<double, words> scores{};
                for (int word = 0; word < words; ++word) {
                    for (int group = 0; group < tones; group += width) {
                        std::complex<double> sum{};
                        for (int t = group; t < group + width; ++t) {
                            const int p = schedule.position[t][word];
                            const double delta = fine - anchor;
                            const auto correction = std::polar(1., tau * (p - 176) * delta / nfft);
                            const auto phase = std::polar(1., -tau * schedule.quarter_phase[t] / 4);
                            const auto carrier = std::polar(1., -tau * rf * (t + .5) / tones);
                            sum += std::complex<double>(bins[t][p].real(), bins[t][p].imag())
                                * correction * phase * carrier;
                        }
                        scores[word] += std::norm(sum);
                    }
                }
                const Best candidate = standardized_best(scores);
                if (candidate.score > reference.score) reference = candidate;
            }
        }
        const double error = std::abs(reference.score - result.audit[a].score);
        if (!(error <= 2e-4) || reference.word != result.audit[a].word) {
            result.audit_match = false;
            std::cerr << "timing scalar reference mismatch at " << fine << ": optimized "
                      << result.audit[a].score << '/' << result.audit[a].word << " scalar "
                      << reference.score << '/' << reference.word << " error " << error << '\n';
        }
    }
    return result;
}
