// Fixed timing experiment; included after the shared decision/calendar helpers.
struct TimingPair {
    BankResult sparse, dense;
};

TimingPair timing_pair(std::span<const Sample> samples, const Schedule& schedule,
                      const FloatPlan& plan, bool audit = false) {
    TimingResults timing;
    const auto sparse = score_banks(samples, schedule, plan, false, true, nullptr, &timing, audit);
    if (audit && (!timing.audit_match || timing.sparse.word != sparse.coherent.word ||
        std::bit_cast<uint64_t>(timing.sparse.score) != std::bit_cast<uint64_t>(sparse.coherent.score)))
        throw std::runtime_error("timing scalar/sparse comparison failed");
    auto dense = sparse;
    dense.coherent = timing.dense;
    return {sparse, dense};
}

// Known-word, known-arrival oracle on unquantized samples. Never used to decide
// a received word. The integer aperture start and phase correction are separate.
double timing_oracle_power(std::span<const Sample> samples, const Schedule& schedule,
        int word, int aperture, double correction, const FloatPlan& plan) {
    std::complex<double> sum{};
    std::array<Sample, nfft> spectrum{};
    for (int t = 0; t < tones; ++t) {
        std::copy_n(samples.begin() + pad + aperture + t * nfft, nfft, spectrum.begin());
        fft(spectrum, false, plan);
        const int p = schedule.position[t][word];
        sum += std::complex<double>(spectrum[fft_bin(p)]) * std::polar(1.,
            tau * (p - 176) * (correction - aperture) / nfft - tau * schedule.quarter_phase[t] / 4);
    }
    // The deterministic source below has unit total energy, so its matched
    // amplitude is sqrt(nsamples). Its common initial phase is irrelevant.
    return std::norm(sum) / nsamples;
}

bool timing_checks(const FloatPlan& plan, const std::vector<Schedule>& fixtures) {
    auto require = [](bool ok, const char* message) {
        if (!ok) throw std::runtime_error(message);
    };
    const auto phase = [](int bin, double d) { return std::polar(1., tau * bin * d / nfft); };
    require(std::abs(phase(-100, .25) - phase(412, .25)) > 1.4,
            "fractional signed-bin discriminator");
    require(std::abs(phase(-100, 32.) - phase(412, 32.)) < 1e-10,
            "integer signed-bin equivalence");
    double largest_tx_error = 0.;
    for (bool rays : {false, true}) for (double carrier : {-.25, .25}) {
        std::vector<Sample> old(nsamples + 2 * pad + delay), shifted(old.size());
        transmit(old, pad + 32, fixtures[0], 173, 18., rays, .73, .42, 2., carrier);
        require(transmit_fractional(shifted, pad + 32., fixtures[0], 173, 18., rays,
                                   .73, .42, carrier), "fractional TX bounds");
        for (size_t i = 0; i < old.size(); ++i)
            largest_tx_error = std::max(largest_tx_error, double(std::abs(old[i] - shifted[i])));
    }
    require(largest_tx_error < 2e-6, "integer TX parity with nonzero carrier and echo");
    std::cout << "signed-frequency controls pass; integer TX maximum sample error "
              << largest_tx_error << '\n';

    Trial trial(0x74696d04, 0);
    // One independent arithmetic audit, including fractional hypotheses, tests
    // every word/RF/rung at eleven declared points without running in the MC path.
    timing_pair(trial.samples, fixtures[trial.ordinal], plan, true);
    std::cout << "scalar double all-word/RF/rung audit and exact sparse parity pass\n";
    for (double arrival : {-64., -32.125, -32., -31.875, -16.125, -16.,
                            16., 16.125, 31.875, 32., 32.125, 64.}) {
        std::vector<Sample> signal(nsamples + 2 * pad);
        require(transmit_fractional(signal, pad + arrival, fixtures[0], 173, 0., false,
                                   .73, 0., 0.), "oracle TX bounds");
        const double known = timing_oracle_power(signal, fixtures[0], 173,
                                                int(std::ceil(arrival)), arrival, plan);
        const double nearest = std::round(arrival * 4.) / 4.;
        const int anchor = nearest < -32. ? -64 : nearest > 32. ? 64 : 0;
        const double at_true = timing_oracle_power(signal, fixtures[0], 173, anchor, arrival, plan);
        const double at_grid = timing_oracle_power(signal, fixtures[0], 173, anchor, nearest, plan);
        require(std::abs(known - 1.) < 2e-6, "known-window matched power");
        std::cout << "oracle arrival " << arrival << " exact-window " << known
                  << " reused-window true/grid " << at_true << '/' << at_grid << '\n';
    }
    // Actual adjacent samples, equal/absent/opposite neighbouring tone. This
    // independently retains the earlier U/16 boundary counterexample.
    for (double neighbour : {1., 0., -1.}) {
        double sum = 0.;
        for (int n = 32; n < nfft + 32; ++n) sum += n < nfft ? 1. : neighbour;
        const double power = std::pow(sum / nfft, 2);
        require(std::abs(power - std::pow((480. + 32. * neighbour) / 512., 2)) < 1e-12,
                "boundary sample control");
        std::cout << "U/16 boundary neighbour " << neighbour << " retained power " << power << '\n';
    }
    return true;
}

int timing_pilot(const FloatPlan& plan, const std::vector<Schedule>& fixtures, int repeats) {
    Trial trial(0x74696d04, 1);
    auto active = trial.samples;
    if (!transmit_fractional(active, pad + 16.125, fixtures[trial.ordinal], trial.word,
                             18., false, trial.phase, 0., .25)) return 1;
    timing_pair(trial.samples, fixtures[trial.ordinal], plan); // warm immutable tables
    for (bool present : {false, true}) {
        double total = 0.;
        for (int i = 0; i < repeats; ++i) {
            const auto start = std::chrono::steady_clock::now();
            const auto pair = timing_pair(present ? active : trial.samples, fixtures[trial.ordinal], plan);
            total += std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            if (pair.sparse.original.score != pair.dense.original.score) return 1;
        }
        std::cout << (present ? "active" : "quiet") << " paired-block seconds " << total / repeats << '\n';
    }
    std::cout << "capture bytes " << sizeof(TimingCapture)
              << "; shared rotation bytes " << (513 * positions + 3 * tones) * sizeof(Sample)
              << "; dense local arrays bytes " << (tones * positions + 2 * tones * words) * sizeof(Sample)
                    + words * sizeof(float)
              << "; excludes frontend, input, plans, allocator and stacks; no capacity claim\n";
    return 0;
}

struct TimingCounts {
    int correct{}, wrong{}, missed{};
    void add(int decision, int truth) {
        if (decision < 0) ++missed;
        else if (truth >= 0 && decision == truth) ++correct;
        else ++wrong;
    }
};

struct TimingComparison {
    // Shared per-tone, sparse coherent, dense coherent, sparse joint, dense joint.
    std::array<TimingCounts, 5> counts{};
    int gained{}, lost{};
    void add(const TimingPair& pair, int truth, const Cutoffs& sparse_cut, const Cutoffs& dense_cut) {
        const auto threshold_word = [](Best b, double cut) { return b.score > cut ? b.word : -1; };
        const std::array decisions{
            threshold_word(pair.sparse.original, sparse_cut.per_tone),
            threshold_word(pair.sparse.coherent, sparse_cut.coherent),
            threshold_word(pair.dense.coherent, dense_cut.coherent),
            joint_word(pair.sparse, sparse_cut).value(), joint_word(pair.dense, dense_cut).value()};
        for (size_t j = 0; j < counts.size(); ++j) counts[j].add(decisions[j], truth);
        if (truth >= 0) {
            gained += decisions[3] != truth && decisions[4] == truth;
            lost += decisions[3] == truth && decisions[4] != truth;
        }
    }
    void print(std::string_view label) const {
        std::cout << label << " C/W/M per-tone,sparse-coherent,dense-coherent,sparse-joint,dense-joint";
        for (const auto& c : counts) std::cout << " | " << c.correct << '/' << c.wrong << '/' << c.missed;
        std::cout << " | paired gains/losses " << gained << '/' << lost << '\n';
    }
};

bool timing_stream(const FloatPlan& plan, const std::vector<Schedule>& fixtures,
                   const Cutoffs& cut) {
    mm::Rng rng(0x74696d05, 0);
    auto samples = noise(5 * nsamples + 2 * pad + delay, rng);
    constexpr std::array truth{217, 218, -1, -1, 221};
    for (int slot : {0, 1, 4})
        if (!transmit_fractional(samples, pad + 16.125 + slot * nsamples, fixtures[slot], truth[slot],
                25., false, .41 + tau * .25 * slot, 0., .25)) return false;
    Calendar calendar;
    bool exact = true;
    for (int slot = 0; slot < 5; ++slot) {
        const auto pair = timing_pair(std::span<const Sample>(samples).subspan(slot * nsamples,
                                     nsamples + 2 * pad), fixtures[slot], plan);
        const auto decision = joint_word(pair.dense, cut);
        if (!decision) { calendar.unscored(); exact = false; continue; }
        calendar.accept(*decision >= 0);
        exact &= *decision == truth[slot];
        exact &= calendar.completions == (slot >= 3 ? 1 : 0);
        if (slot == 3) exact &= !calendar.active;
        if (slot == 4) exact &= calendar.active;
        std::cout << "dense stream slot " << slot << " truth/decision " << truth[slot] << '/'
                  << *decision << " closures " << calendar.completions << '\n';
    }
    std::vector<Sample> zero(nsamples + 2 * pad), invalid(zero.size(), {NAN, NAN});
    for (auto input : {std::span<const Sample>{}, std::span<const Sample>(zero),
                       std::span<const Sample>(invalid)}) {
        Calendar c;
        c.accept(true); c.accept(false);
        const auto result = joint_word(timing_pair(input, fixtures[0], plan).dense, cut);
        if (result) return false;
        c.unscored(); c.accept(false);
        if (c.completions) return false;
        c.accept(false);
        if (c.completions != 1) return false;
    }
    std::cout << "dense adjacent-waveform stream " << (exact ? "pass" : "FAIL")
              << "; missing/zero/unusable input controls pass\n";
    return exact;
}

int timing_coverage(const FloatPlan& plan, const std::vector<Schedule>& fixtures, int workers) {
    constexpr int calibration_n = 399, quiet_n = 400, signal_n = 120;
    const auto started = std::chrono::steady_clock::now();
    auto parallel = [workers](int count, auto body) {
        std::vector<std::jthread> pool;
        for (int lane = 0; lane < workers; ++lane)
            pool.emplace_back([&, lane] { for (int i = lane; i < count; i += workers) body(i); });
    };
    std::vector<TimingPair> calibration(calibration_n), quiet(quiet_n);
    parallel(calibration_n, [&](int i) {
        Trial trial(0x74696d01, i);
        calibration[i] = timing_pair(trial.samples, fixtures[trial.ordinal], plan);
    });
    std::array<std::vector<double>, 3> sorted;
    for (const auto& pair : calibration) {
        sorted[0].push_back(pair.sparse.original.score);
        sorted[1].push_back(pair.sparse.coherent.score);
        sorted[2].push_back(pair.dense.coherent.score);
    }
    for (auto& values : sorted) std::sort(values.begin(), values.end());
    const Cutoffs sparse_cut{0., sorted[0][397], sorted[1][397]};
    const Cutoffs dense_cut{0., sorted[0][397], sorted[2][397]};
    std::cout << std::setprecision(17) << "rank398 cutoffs per-tone/sparse-coherent/dense-coherent "
              << sparse_cut.per_tone << '/' << sparse_cut.coherent << '/' << dense_cut.coherent << std::endl;
    parallel(quiet_n, [&](int i) {
        Trial trial(0x74696d02, i);
        quiet[i] = timing_pair(trial.samples, fixtures[trial.ordinal], plan);
    });
    TimingComparison quiet_counts;
    for (const auto& pair : quiet) quiet_counts.add(pair, -1, sparse_cut, dense_cut);
    quiet_counts.print("quiet 400");
    std::cout << "quiet joint upper95 sparse/dense " << upper_binomial(quiet_counts.counts[3].wrong, quiet_n)
              << '/' << upper_binomial(quiet_counts.counts[4].wrong, quiet_n) << std::endl;

    constexpr std::array labels{"on-grid single15", "midcell single15", "fractional single15",
        "fractional single18", "fractional two-ray18", "fractional phase-reset18"};
    struct SignalResult { int truth{}; std::array<TimingPair, 6> rows; };
    std::vector<SignalResult> signals(signal_n);
    parallel(signal_n, [&](int i) {
        Trial trial(0x74696d03, i);
        mm::Rng draw(0x74696d06, i);
        const double fractional = -64. + 128. * draw.uniform();
        const double midcell = draw.next() % 2 ? -16. : 16.;
        const double carrier = draw.next() % 2 ? -.25 : .25;
        signals[i].truth = trial.word;
        for (int row = 0; row < 6; ++row) {
            auto samples = trial.samples;
            samples.resize(samples.size() + delay); // generated tail outside shared receiver view
            const double arrival = row == 0 ? trial.alignment : row == 1 ? midcell : fractional;
            mm::Rng resets(0x74696d07, i);
            if (!transmit_fractional(samples, pad + arrival, fixtures[trial.ordinal], trial.word,
                row < 3 ? 15. : 18., row == 4, trial.phase, trial.echo_phase, carrier,
                row == 5 ? &resets : nullptr)) throw std::runtime_error("signal fixture tail bounds");
            signals[i].rows[row] = timing_pair(std::span<const Sample>(samples).first(nsamples + 2 * pad),
                                               fixtures[trial.ordinal], plan);
        }
    });
    std::array<TimingComparison, 6> counts;
    for (const auto& signal : signals)
        for (size_t j = 0; j < counts.size(); ++j)
            counts[j].add(signal.rows[j], signal.truth, sparse_cut, dense_cut);
    for (size_t j = 0; j < counts.size(); ++j) counts[j].print(labels[j]);
    std::cout << "primary fractional-single15 exact paired p " << paired_p(counts[2].gained, counts[2].lost) << '\n';
    const bool stream_ok = timing_stream(plan, fixtures, dense_cut);
    std::cout << "experiment seconds " << std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count()
              << "; independent trial workers " << workers << "; no receiver-throughput claim\n";
    return stream_ok ? 0 : 1;
}

// Follow-on control declared after the fixed timing comparison, before drawing
// these records. Use its frozen full-precision cutoffs; do not recalibrate.
int timing_unrelated(const FloatPlan& plan, const std::vector<Schedule>& fixtures,
                     const std::vector<Schedule>& other) {
    const Cutoffs sparse_cut{0., 5.5478988046551079, 13.65871519005635};
    const Cutoffs dense_cut{0., 5.5478988046551079, 16.317710015395722};
    constexpr int trials = 160, workers = 12;
    std::array<std::array<TimingPair, 2>, trials> results{};
    {
        std::vector<std::jthread> pool;
        for (int lane = 0; lane < workers; ++lane)
            pool.emplace_back([&, lane] {
                for (int i = lane; i < trials; i += workers) {
                    Trial trial(0x74696d08, i);
                    mm::Rng draw(0x74696d09, i);
                    const double arrival = -64. + 128. * draw.uniform();
                    const double carrier = draw.next() % 2 ? -.25 : .25;
                    for (int j = 0; j < 2; ++j) {
                        auto samples = trial.samples;
                        if (!transmit_fractional(samples, pad + arrival, other[trial.ordinal], trial.word,
                                j == 0 ? 24. : 36., false, trial.phase, 0., carrier))
                            throw std::runtime_error("unrelated fixture bounds");
                        results[i][j] = timing_pair(samples, fixtures[trial.ordinal], plan);
                    }
                }
            });
    }
    for (int j = 0; j < 2; ++j) {
        TimingComparison counts;
        for (const auto& result : results) counts.add(result[j], -1, sparse_cut, dense_cut);
        counts.print(j == 0 ? "wrong schedule24,160" : "wrong schedule36,160");
        std::cout << "wrong schedule joint upper95 sparse/dense "
                  << upper_binomial(counts.counts[3].wrong, trials) << '/'
                  << upper_binomial(counts.counts[4].wrong, trials) << '\n';
    }
    return 0; // Counts are evidence, never a rare-error pass.
}
