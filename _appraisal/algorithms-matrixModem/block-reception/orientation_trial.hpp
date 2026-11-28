// Included after the one-cell block receiver and calendar in block_reception.cpp.
using Orientations = std::array<BankResult, 2>;

void reflect_samples(std::span<Sample> samples) {
    for (auto& z : samples) z = std::conj(z);
}

BankResult blind_orientation(const Orientations& scores) {
    BankResult best = scores[0];
    if (scores[1].original.score > best.original.score) best.original = scores[1].original;
    if (scores[1].coherent.score > best.coherent.score) best.coherent = scores[1].coherent;
    return best;
}

struct OrientationCuts {
    Cutoffs known, blind;
    double waterfall;
};

OrientationCuts calibrate_orientation(const FloatPlan& plan, const std::vector<Schedule>& fixtures) {
    std::array<double, 399> known_per{}, known_coh{}, blind_per{}, blind_coh{}, waterfall{};
    for (int i = 0; i < 399; ++i) {
        Trial trial(0x53494401, i);
        auto scores = score_orientations(trial.samples, fixtures[trial.ordinal], plan, &waterfall[i]);
        auto blind = blind_orientation(scores);
        known_per[i] = scores[0].original.score;
        known_coh[i] = scores[0].coherent.score;
        blind_per[i] = blind.original.score;
        blind_coh[i] = blind.coherent.score;
    }
    for (auto* values : {&known_per, &known_coh, &blind_per, &blind_coh, &waterfall})
        std::sort(values->begin(), values->end());
    // Conjugated circular Gaussian noise has the original law. Use the same
    // cutoffs for a receiver told the orientation and for the original receiver.
    return {{0., known_per[397], known_coh[397]}, {0., blind_per[397], blind_coh[397]},
            waterfall[395]};
}

struct OrientationRow {
    std::array<Outcomes, 3> outcomes{};
    std::array<int, 3> visible_correct{};
    PairedDecisions paired;
    int visible{}, per_correct{}, coherent_only_correct{};

    void observe(const Orientations& scores, const OrientationCuts& cut, int oracle_orientation,
                 double waterfall, int truth) {
        const auto blind = blind_orientation(scores);
        const std::array<int, 3> decisions = {
            joint_word(scores[0], cut.known).value(), joint_word(scores[oracle_orientation], cut.known).value(),
            joint_word(blind, cut.blind).value()
        };
        const bool seen = waterfall > cut.waterfall;
        visible += seen;
        for (size_t j = 0; j < decisions.size(); ++j) {
            auto& out = outcomes[j];
            if (decisions[j] < 0) ++out.absent;
            else if (decisions[j] == truth) {
                ++out.correct;
                visible_correct[j] += seen;
            } else ++out.wrong;
        }
        if (truth < 0) return;
        paired.observe(decisions[0], decisions[2], truth);
        bool per_admitted = blind.original.score > cut.blind.per_tone;
        per_correct += per_admitted && blind.original.word == truth;
        coherent_only_correct += !per_admitted && decisions[2] == truth;
    }

    void report(const std::string& label) const {
        std::cout << label << " | " << visible;
        for (size_t j = 0; j < outcomes.size(); ++j) {
            const auto& out = outcomes[j];
            std::cout << " | " << out.correct << '/' << out.wrong << '/' << out.absent
                      << " | " << visible_correct[j] << '/' << visible - visible_correct[j];
        }
        std::cout << " | " << paired.gains << '/' << paired.losses << " | " << per_correct
                  << '/' << coherent_only_correct << '\n';
    }
};

void add_unmodulated_line(std::span<Sample> samples, int start, double phase) {
    // A broken oscillator runs at a single in-band frequency independently
    // of every scheduled value and phase, with the same energy as a 44 dB block.
    auto z = std::polar(std::sqrt(mm::undb(44.) / nsamples), phase);
    const auto rotation = std::polar(1., tau * 29. / nfft);
    for (auto& sample : samples.subspan(start, nsamples)) {
        sample += static_cast<Sample>(z);
        z *= rotation;
    }
}

void dump_orientation_waterfall(const FloatPlan& plan, const std::vector<Schedule>& fixtures) {
    Trial trial(0x53494403, 0);
    transmit(trial.samples, pad + trial.alignment, fixtures[trial.ordinal], trial.word,
             34., false, trial.phase, trial.echo_phase, 2., -.25);
    auto normal = sample_codes(trial.samples);
    reflect_samples(trial.samples);
    auto mirrored = sample_codes(trial.samples);
    constexpr int edge = positions / 2 + 2, bin_count = 2 * edge + 1;
    std::cout << "# active trial 0, ordinal " << trial.ordinal << ", word " << trial.word
              << ", start error " << trial.alignment << " samples, -0.25 cycles/block, E/N0 34 dB"
              << ", nominal 0.16 s rows, 6.25 Hz bins, dB relative to each row median\n"
              << "row,bin,normal_db,mirrored_db\n" << std::setprecision(8);
    for (int t = 0; t < tones; ++t) {
        std::array<std::array<double, bin_count>, 2> row_db{};
        int orientation = 0;
        for (const auto* codes : {&normal, &mirrored}) {
            std::array<Sample, nfft> spectrum;
            std::copy_n(codes->begin() + pad + t * nfft, nfft, spectrum.begin());
            fft(spectrum, false, plan);
            std::array<float, bin_count> power;
            for (int k = -edge; k <= edge; ++k)
                power[k + edge] = std::norm(spectrum[(nfft + k) % nfft]);
            auto scratch = power;
            double median = quantile(scratch, edge);
            if (!(median > 0.)) throw std::runtime_error("zero median in waterfall capture");
            for (int i = 0; i < bin_count; ++i)
                row_db[orientation][i] = 10. * std::log10(power[i] / median);
            ++orientation;
        }
        for (int k = -edge; k <= edge; ++k)
            std::cout << t << ',' << k << ',' << row_db[0][k + edge] << ','
                      << row_db[1][k + edge] << '\n';
    }
}

bool orientation_parity(const FloatPlan& plan, const std::vector<Schedule>& fixtures) {
    double max_score_error = 0., max_waterfall_error = 0.;
    bool same_word = true;
    for (int i = 0; i < 4; ++i) {
        Trial trial(0x53494407, i);
        const auto& schedule = fixtures[trial.ordinal];
        if (i % 2) transmit(trial.samples, pad + trial.alignment, schedule, trial.word,
                            24., false, trial.phase, 0., 2., .25);
        double original_waterfall, reflected_waterfall;
        auto banks = score_orientations(trial.samples, schedule, plan, &original_waterfall);
        for (const auto& result : banks) {
            auto reference = score_banks(trial.samples, schedule, plan, false, true);
            for (auto member : {&BankResult::original, &BankResult::coherent}) {
                same_word &= (result.*member).word == (reference.*member).word;
                max_score_error = std::max(max_score_error,
                                           std::abs((result.*member).score - (reference.*member).score));
            }
            reflect_samples(trial.samples);
        }
        reflect_samples(trial.samples);
        score_orientations(trial.samples, schedule, plan, &reflected_waterfall);
        max_waterfall_error = std::max(max_waterfall_error,
                                       std::abs(original_waterfall - reflected_waterfall));
    }
    std::cout << "direct-IQ parity: same winners " << same_word << ", max score difference "
              << max_score_error << ", max reflected waterfall difference " << max_waterfall_error << '\n';
    return same_word && max_score_error < 1.e-4 && max_waterfall_error < 1.e-4;
}

bool orientation_calendar(const FloatPlan& plan, const std::vector<Schedule>& fixtures,
                          const OrientationCuts& cuts) {
    constexpr std::array truth = {113, -1, 889, -1, -1};
    mm::Rng rng(0x53494406);
    auto stream = noise(nsamples * truth.size() + 2 * pad, rng);
    for (size_t t = 0; t < truth.size(); ++t)
        if (truth[t] >= 0)
            transmit(stream, t * nsamples + pad + 32, fixtures[t], truth[t], 34., false, .32, 0., 2., .25);
    reflect_samples(stream);
    Calendar calendar;
    bool pass = true;
    for (size_t t = 0; t < truth.size(); ++t) {
        const auto samples = std::span(stream).subspan(t * nsamples, nsamples + 2 * pad);
        auto scores = score_orientations(samples, fixtures[t], plan);
        auto word = joint_word(blind_orientation(scores), cuts.blind);
        if (word) calendar.accept(*word >= 0);
        else calendar.unscored();
        pass &= word && *word == truth[t] && calendar.completions == int(t + 1 == truth.size());
        std::cout << "mirrored calendar " << t << " word " << word.value_or(-2)
                  << " completions " << calendar.completions << '\n';
    }
    Calendar missing;
    missing.accept(true);
    missing.accept(false);
    double waterfall = 0.;
    auto invalid = score_orientations({}, fixtures.front(), plan, &waterfall);
    auto word = joint_word(blind_orientation(invalid), cuts.blind);
    if (word) return false;
    missing.unscored();
    missing.accept(false);
    pass &= !missing.completions && waterfall == -INFINITY;
    missing.accept(false);
    return pass && missing.completions == 1 && calendar.erasures == 1;
}

void orientation_timing(const FloatPlan& plan, const std::vector<Schedule>& fixtures) {
    Trial trial(0x53494407, 4);
    auto& schedule = fixtures[trial.ordinal];
    transmit(trial.samples, pad + trial.alignment, schedule, trial.word, 24., false, trial.phase, 0.);
    std::array<double, 2> seconds{};
    double checksum = 0.;
    for (int pair = -1; pair < 16; ++pair) {
        for (int j = 0; j < 2; ++j) {
            int blind = (j + pair + 1) % 2;
            const auto start = std::chrono::steady_clock::now();
            auto r = blind ? blind_orientation(score_orientations(trial.samples, schedule, plan))
                           : score_banks(trial.samples, schedule, plan, false, true);
            const auto elapsed = std::chrono::steady_clock::now() - start;
            if (pair >= 0) seconds[blind] += std::chrono::duration<double>(elapsed).count();
            checksum += r.original.score + r.coherent.score;
        }
    }
    std::cout << "16 scoring-only blocks, seconds single/blind " << seconds[0] << ' ' << seconds[1]
              << " checksum " << checksum << '\n';
}

bool check_orientation(const FloatPlan& plan, const std::vector<Schedule>& fixtures,
                       const std::vector<Schedule>& other) {
    bool pass = orientation_parity(plan, fixtures);
    auto cuts = calibrate_orientation(plan, fixtures);
    std::cout << "orientation cutoffs known per/coherent " << cuts.known.per_tone << ' ' << cuts.known.coherent
              << ", blind per/coherent " << cuts.blind.per_tone << ' ' << cuts.blind.coherent
              << ", waterfall " << cuts.waterfall << std::endl;
    OrientationRow quiet, wrong_key, line;
    for (int i = 0; i < 400; ++i) {
        Trial trial(0x53494402, i);
        double waterfall;
        auto scores = score_orientations(trial.samples, fixtures[trial.ordinal], plan, &waterfall);
        quiet.observe(scores, cuts, 0, waterfall, -1);
    }
    constexpr std::array energies = {15., 18., 24., 34., 44.};
    std::array<std::array<OrientationRow, 2>, energies.size()> active{};
    std::array<OrientationRow, 2> resets{};
    for (int i = 0; i < 64; ++i) {
        auto score_pair = [&](Trial& trial, double energy, std::array<OrientationRow, 2>& row,
                              mm::Rng* phase_resets) {
            auto samples = trial.samples;
            auto& schedule = fixtures[trial.ordinal];
            transmit(samples, pad + trial.alignment, schedule, trial.word, energy, false,
                     trial.phase, trial.echo_phase, 2., i % 2 ? .25 : -.25, phase_resets);
            for (int orientation = 0; orientation < 2; ++orientation) {
                double waterfall;
                if (orientation == 1) reflect_samples(samples);
                auto scores = score_orientations(samples, schedule, plan, &waterfall);
                row[orientation].observe(scores, cuts, orientation, waterfall, trial.word);
            }
        };
        Trial trial(0x53494403, i);
        for (size_t k = 0; k < energies.size(); ++k)
            score_pair(trial, energies[k], active[k], nullptr);
        Trial reset(0x53494408, i);
        mm::Rng phase_resets(0x53494409, i);
        score_pair(reset, 24., resets, &phase_resets);

        Trial wrong(0x53494404, i);
        transmit(wrong.samples, pad + wrong.alignment, other[wrong.ordinal], wrong.word, 44.,
                 false, wrong.phase, 0., 2., i % 2 ? .25 : -.25);
        Trial stuck(0x53494405, i);
        add_unmodulated_line(stuck.samples, pad + stuck.alignment, stuck.phase);
        double waterfall;
        auto wrong_scores = score_orientations(wrong.samples, fixtures[wrong.ordinal], plan, &waterfall);
        wrong_key.observe(wrong_scores, cuts, 0, waterfall, -1);
        auto line_scores = score_orientations(stuck.samples, fixtures[stuck.ordinal], plan, &waterfall);
        line.observe(line_scores, cuts, 0, waterfall, -1);
    }
    std::cout << "case | visible | original C/W/A | visible C/fail | oracle C/W/A | visible C/fail"
                 " | blind C/W/A | visible C/fail | blind gains/losses vs original | blind per/coherent-only correct\n";
    quiet.report("noise");
    wrong_key.report("wrong schedule 44");
    line.report("stuck line 44");
    for (int orientation = 0; orientation < 2; ++orientation) {
        std::string label = orientation ? "mirrored " : "normal ";
        for (size_t j = 0; j < energies.size(); ++j)
            active[j][orientation].report(label + std::to_string(int(energies[j])));
        resets[orientation].report(label + "independent phase 24");
    }
    pass &= orientation_calendar(plan, fixtures, cuts);
    orientation_timing(plan, fixtures);
    std::cout << "orientation structural checks " << (pass ? "pass" : "FAIL") << '\n';
    return pass;
}
