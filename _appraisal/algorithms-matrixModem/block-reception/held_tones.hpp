// Fixed contrast using the receiver, schedule, scorer and calibration of the
// enclosing research driver. Both geometries see the identical channel record.
std::vector<Sample> diffuse_path(double spread, int trial) {
    constexpr int components = 32;
    mm::Rng rng(0xd0a51, trial);
    std::vector<Sample> path(nsamples);
    for (int i = 0; i < components; ++i) {
        double frequency = .5 * spread * mm::qinv((i + .5) / components);
        auto rotation = std::polar(1., tau * frequency / 3200.);
        double magnitude = std::sqrt(rng.exp1() / components);
        auto v = std::polar(magnitude, tau * rng.uniform());
        for (auto& sample : path) {
            sample += static_cast<Sample>(v);
            v *= rotation;
        }
    }
    return path;
}

struct PairedDecisions {
    int gains{}, losses{};
    void observe(int first_word, int second_word, int truth) {
        gains += first_word != truth && second_word == truth;
        losses += first_word == truth && second_word != truth;
    }
};

void compare_held_tones(const FloatPlan& plan, const std::vector<Schedule>& singles) {
    const auto start = std::chrono::steady_clock::now();
    const FloatPlan held_plan(2 * nfft);
    std::vector<Schedule> held;
    for (int ordinal = 0; ordinal < nfixtures; ++ordinal) held.emplace_back(ordinal, 0xaa6218f2, 2);
    auto fixture = [&](int shape, int ordinal) -> const Schedule& {
        return shape ? held[ordinal] : singles[ordinal];
    };
    std::array<double, 2> score_seconds{};
    auto run = [&](int shape, int ordinal, const std::vector<Sample>& samples) {
        auto before = std::chrono::steady_clock::now();
        auto r = score_banks(samples, fixture(shape, ordinal), plan, false, true, shape ? &held_plan : nullptr);
        score_seconds[shape] += std::chrono::duration<double>(std::chrono::steady_clock::now() - before).count();
        return r;
    };
    std::array<std::array<double, 399>, 2> energy_cal{}, coherent_cal{};
    for (int i = 0; i < 399; ++i) {
        Trial trial(0x2aa541, i);
        for (int shape = 0; shape < 2; ++shape) {
            auto r = run(shape, trial.ordinal, trial.samples);
            energy_cal[shape][i] = r.original.score;
            coherent_cal[shape][i] = r.coherent.score;
        }
    }
    std::array<Cutoffs, 2> cuts{};
    for (int shape = 0; shape < 2; ++shape) {
        auto& a = energy_cal[shape];
        auto& b = coherent_cal[shape];
        std::sort(a.begin(), a.end());
        std::sort(b.begin(), b.end());
        cuts[shape] = {a[395], a[397], b[397]};
        std::cout << "geometry " << shape+1 << " cutoffs " << a[395] << ' ' << a[397] << ' ' << b[397] << '\n';
    }
    std::array<JointOutcomes, 2> null{};
    for (int i = 0; i < 400; ++i) {
        Trial trial(0x2bb156, i);
        for (int shape = 0; shape < 2; ++shape)
            null[shape].observe(run(shape, trial.ordinal, trial.samples), -1, cuts[shape]);
    }
    for (int shape = 0; shape < 2; ++shape) null[shape].report("noise, cells " + std::to_string(shape+1));
    std::array<std::array<JointOutcomes, 2>, 4> rows{};
    std::array<std::array<PairedDecisions, 2>, 4> paired{};
    for (int i = 0; i < 120; ++i) {
        Trial trial(0x2dd433, i);
        int offset = pad + trial.alignment;
        for (int width = 0; width < 2; ++width) {
            auto path = diffuse_path(width ? 20. : .5, i);
            for (int level = 0; level < 2; ++level) {
                auto& row = rows[width * 2 + level];
                std::array<int, 2> per_word{}, joint{};
                for (int shape = 0; shape < 2; ++shape) {
                    std::vector<Sample> tx(trial.samples.size());
                    transmit(tx, offset, fixture(shape, trial.ordinal), trial.word,
                             level ? 22. : 18., false, trial.phase, trial.echo_phase);
                    auto samples = trial.samples;
                    for (int n = 0; n < nsamples; ++n) samples[n + offset] += path[n] * tx[n + offset];
                    auto r = run(shape, trial.ordinal, samples);
                    row[shape].observe(r, trial.word, cuts[shape]);
                    per_word[shape] = r.original.score > cuts[shape].baseline ? r.original.word : -1;
                    joint[shape] = joint_word(r, cuts[shape]).value();
                }
                auto& pair = paired[width * 2 + level];
                pair[0].observe(per_word[0], per_word[1], trial.word);
                pair[1].observe(joint[0], joint[1], trial.word);
            }
        }
    }
    for (int j = 0; j < 4; ++j) {
        const std::string stratum = std::string(j < 2 ? "0.5" : "20") + " Hz " + (j % 2 ? "22" : "18") + " dB ";
        for (int shape = 0; shape < 2; ++shape)
            rows[j][shape].report(stratum + std::to_string(shape+1) + " cells");
        for (const auto& p : paired[j])
            std::cout << "paired held gains / losses " << p.gains << " / " << p.losses << '\n';
    }
    std::cout << "scoring seconds over 1279 blocks, one / two cells "
              << score_seconds[0] << " / " << score_seconds[1] << '\n';
    auto primary = paired[0][0];
    std::cout << "primary matched per-tone p=" << paired_p(primary.gains, primary.losses)
              << ", experiment seconds "
              << std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() << '\n';
}
