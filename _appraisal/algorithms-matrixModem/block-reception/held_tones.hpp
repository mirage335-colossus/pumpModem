// Fixed contrast using the receiver, schedule, scorer and calibration of the
// enclosing research driver. Both geometries see the identical channel record.
constexpr int doppler_components = 32;

double component_frequency(double spread, int component) {
    return .5 * spread * mm::qinv((component + .5) / doppler_components);
}

// Effective diversity of the continuous-time mean power over one block.
// For a proper Gaussian tap, its power covariance is the squared magnitude
// of its complex covariance. Finite quantiles and a continuous PSD differ.
void report_power_diversity(double spread) {
    constexpr double duration = double(nsamples) / 3200;
    double finite_variance = 0;
    for (int i = 0; i < doppler_components; ++i) {
        for (int j = 0; j < doppler_components; ++j) {
            double x = mm::kPi * duration * (component_frequency(spread, i) - component_frequency(spread, j));
            double sinc = i == j ? 1. : std::sin(x) / x;
            finite_variance += sinc * sinc / (doppler_components * doppler_components);
        }
    }
    const double x = mm::kPi * spread * duration;
    const double continuum_variance = std::sqrt(mm::kPi) * std::erf(x) / x
                                     + std::expm1(-x * x) / (x * x);
    std::cout << spread << " Hz effective energy diversity, finite/continuum "
              << 1 / finite_variance << " / " << 1 / continuum_variance << '\n';
}

std::vector<Sample> diffuse_path(double spread, int trial) {
    mm::Rng rng(0xd0a51, trial);
    std::vector<Sample> path(nsamples);
    for (int i = 0; i < doppler_components; ++i) {
        double frequency = component_frequency(spread, i);
        auto rotation = std::polar(1., tau * frequency / 3200.);
        double magnitude = std::sqrt(rng.exp1() / doppler_components);
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

struct BudgetSweep {
    static constexpr std::array<double, 3> percents = {.5, 1., 2.};
    static constexpr std::array<int, 3> ranks = {399, 398, 396};
    std::array<Cutoffs, 3> cutoffs{};
    // Row 0 is physical quiet. Rows 1–4 preserve the channel/energy strata.
    std::array<std::array<Outcomes, 3>, 5> counts{};

    void calibrate(const std::array<double, 399>& per, const std::array<double, 399>& coh) {
        for (size_t b = 0; b < ranks.size(); ++b)
            cutoffs[b] = {0., per[ranks[b] - 1], coh[ranks[b] - 1]};
    }

    void observe(int row, const BankResult& scores, int truth) {
        for (size_t b = 0; b < ranks.size(); ++b) {
            auto word = joint_word(scores, cutoffs[b]);
            if (!word) throw std::runtime_error("unscored budget sweep input");
            auto& out = counts[row][b];
            if (*word < 0) ++out.absent;
            else if (*word == truth) ++out.correct;
            else ++out.wrong;
        }
    }

    void report(int shape) const {
        constexpr std::array<const char*, 5> names = {"quiet", "0.5Hz/18dB", "0.5Hz/22dB",
                                                      "20Hz/18dB", "20Hz/22dB"};
        for (size_t b = 0; b < percents.size(); ++b) {
            std::cout << "budget " << percents[b] << "% geometry " << shape
                      << " per/coherent thresholds " << cutoffs[b].per_tone
                      << " / " << cutoffs[b].coherent << '\n';
            for (size_t j = 0; j < names.size(); ++j)
                print(std::string("budget ") + names[j], counts[j][b]);
        }
    }
};

void compare_held_tones(const FloatPlan& plan, const std::vector<Schedule>& singles, bool sweep = false) {
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
    std::array<BudgetSweep, 2> budgets{};
    for (int shape = 0; shape < 2; ++shape) {
        auto& a = energy_cal[shape];
        auto& b = coherent_cal[shape];
        std::sort(a.begin(), a.end());
        std::sort(b.begin(), b.end());
        cuts[shape] = {a[395], a[397], b[397]};
        if (sweep) budgets[shape].calibrate(a, b);
        std::cout << "geometry " << shape+1 << " cutoffs " << a[395] << ' ' << a[397] << ' ' << b[397] << '\n';
    }
    std::array<JointOutcomes, 2> null{};
    for (int i = 0; i < 400; ++i) {
        Trial trial(0x2bb156, i);
        for (int shape = 0; shape < 2; ++shape) {
            auto scores = run(shape, trial.ordinal, trial.samples);
            null[shape].observe(scores, -1, cuts[shape]);
            if (sweep) budgets[shape].observe(0, scores, -1);
        }
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
                    if (sweep) budgets[shape].observe(1 + width * 2 + level, r, trial.word);
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
    for (double width : {.5, 20.}) report_power_diversity(width);
    if (sweep)
        for (int shape = 0; shape < 2; ++shape) budgets[shape].report(shape + 1);
    auto primary = paired[0][0];
    std::cout << "primary matched per-tone p=" << paired_p(primary.gains, primary.losses)
              << ", experiment seconds "
              << std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() << '\n';
}

std::vector<BankResult> quiet_scores(int count, uint64_t seed, const FloatPlan& plan,
                                     const std::vector<Schedule>& fixtures) {
    constexpr int threads = 6;
    std::vector<BankResult> scores(count);
    {
        std::vector<std::jthread> workers;
        for (int lane = 0; lane < threads; ++lane)
            workers.emplace_back([&, lane] {
                for (int i = lane; i < count; i += threads) {
                    Trial trial(seed, i);
                    scores[i] = score_banks(trial.samples, fixtures[trial.ordinal], plan, false, true);
                }
            });
    }
    return scores;
}

// One-sided exact 95% upper endpoint, solving P_p(X <= k) = 0.05.
double upper_binomial(int k, int n) {
    if (k == n) return 1.;
    if (k == 0) return -std::expm1(std::log(.05) / n);
    double lo = double(k) / n, hi = 1;
    const double comb = std::lgamma(n + 1.) - std::lgamma(k + 1.) - std::lgamma(n - k + 1.);
    for (int step = 0; step < 55; ++step) {
        double p = (lo + hi) / 2;
        double log_mass = comb + k * std::log(p) + (n - k) * std::log1p(-p);
        double relative = 1, sum = 1;
        for (int i = k; i > 0; --i) {
            relative *= double(i) / (n - i + 1) * (1 - p) / p;
            sum += relative;
        }
        if (log_mass + std::log(sum) > std::log(.05)) lo = p;
        else hi = p;
    }
    return hi;
}

void wrong_schedule_holdout(const FloatPlan& plan, const std::vector<Schedule>& singles,
                            const std::vector<Schedule>& other, const std::array<Cutoffs, 2>& cuts) {
    std::array<std::array<int, 2>, 2> false_words{};
    for (int i = 0; i < 160; ++i) {
        Trial trial(0x5d9c137, i);
        for (int level = 0; level < 2; ++level) {
            auto samples = trial.samples;
            transmit(samples, pad + trial.alignment, other[trial.ordinal], trial.word,
                     level ? 36. : 24., false, trial.phase, trial.echo_phase);
            auto r = score_banks(samples, singles[trial.ordinal], plan, false, true);
            for (int j = 0; j < 2; ++j)
                false_words[level][j] += joint_word(r, cuts[j]).value() >= 0;
        }
    }
    for (int level = 0; level < 2; ++level) {
        std::cout << "wrong schedule " << (level ? 36 : 24) << " dB false at 1e-4/1e-2 "
                  << false_words[level][0] << " / " << false_words[level][1] << " of 160" << '\n';
    }
}

void check_rare_admission(const FloatPlan& plan, const std::vector<Schedule>& singles,
                          const std::vector<Schedule>& other) {
    auto start = std::chrono::steady_clock::now();
    constexpr int ncal = 19999, nhold = 30000;
    const auto calibration = quiet_scores(ncal, 0x5a1027d, plan, singles);
    std::vector<double> per, coh;
    for (auto r : calibration) {
        per.push_back(r.original.score);
        coh.push_back(r.coherent.score);
    }
    std::sort(per.begin(), per.end());
    std::sort(coh.begin(), coh.end());
    // baseline is unused for this joint-decision comparison.
    const std::array<Cutoffs, 2> cuts = {{{0., per.back(), coh.back()}, {0., per[19899], coh[19899]}}};
    for (auto cut : cuts)
        std::cout << "rare study per/coherent thresholds " << cut.per_tone << " / " << cut.coherent << '\n';
    std::cout << std::flush;

    std::array<int, 2> false_joint{}, false_per{}, false_coh{};
    for (auto r : quiet_scores(nhold, 0x5b8419a, plan, singles)) {
        for (int j = 0; j < 2; ++j) {
            false_per[j] += r.original.score > cuts[j].per_tone;
            false_coh[j] += r.coherent.score > cuts[j].coherent;
            false_joint[j] += joint_word(r, cuts[j]).value() >= 0;
        }
    }
    for (int j = 0; j < 2; ++j)
        std::cout << "rate " << (j ? 1e-2 : 1e-4) << " quiet false per/coherent/joint "
                  << false_per[j] << " / " << false_coh[j] << " / " << false_joint[j] << " of " << nhold
                  << ", joint upper95 " << upper_binomial(false_joint[j], nhold) << '\n';
    std::cout << "fixed-threshold 1e-4 holdout endpoint: "
              << (upper_binomial(false_joint[0], nhold) <= 1e-4 ? "met" : "not met") << '\n';
    std::array<std::array<Outcomes, 2>, 4> rows{};
    for (int i = 0; i < 120; ++i) {
        Trial trial(0x5c365e7, i);
        auto& schedule = singles[trial.ordinal];
        int offset = pad + trial.alignment;
        for (int width = 0; width < 2; ++width) {
            // Disjoint fading-coefficient streams from the earlier fixture.
            auto path = diffuse_path(width ? 20. : .5, 10000 + i);
            for (int level = 0; level < 2; ++level) {
                std::vector<Sample> tx(trial.samples.size());
                transmit(tx, offset, schedule, trial.word, level ? 22. : 18.,
                         false, trial.phase, trial.echo_phase);
                auto samples = trial.samples;
                for (int n = 0; n < nsamples; ++n) samples[n+offset] += path[n] * tx[n+offset];
                auto r = score_banks(samples, schedule, plan, false, true);
                for (int j = 0; j < 2; ++j) {
                    int word = joint_word(r, cuts[j]).value();
                    auto& out = rows[width*2 + level][j];
                    if (word < 0) ++out.absent;
                    else if (unsigned(word) == trial.word) ++out.correct;
                    else ++out.wrong;
                }
            }
        }
    }
    for (int i = 0; i < 4; ++i) {
        std::string stratum = std::string(i < 2 ? "0.5 Hz " : "20 Hz ") + (i % 2 ? "22 dB " : "18 dB ");
        print(stratum + "1e-4", rows[i][0]);
        print(stratum + "1e-2", rows[i][1]);
    }
    wrong_schedule_holdout(plan, singles, other, cuts);
    std::cout << "rare experiment seconds "
              << std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() << '\n';
}
