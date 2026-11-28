// Fixed thresholds published by the single-orientation comparator in fault-tests.md.
constexpr Cutoffs pointing_cutoffs{0., 7.0448336393, 16.1995798184};
constexpr std::array pointing_waterfall_cuts{9.9517550479, 10.431464751805};

struct PointingRow {
    int correct{}, wrong{}, absent{};
    std::array<int, 2> visible{}, visible_incorrect{}, visible_incorrect_on_quiet{};

    void observe(int decision, int truth, double display, double quiet_display) {
        const bool matched = decision >= 0 && decision == truth;
        correct += matched;
        wrong += decision >= 0 && !matched;
        absent += decision < 0;
        for (size_t j = 0; j < pointing_waterfall_cuts.size(); ++j) {
            if (display <= pointing_waterfall_cuts[j]) continue;
            ++visible[j];
            visible_incorrect[j] += !matched;
            visible_incorrect_on_quiet[j] += !matched && quiet_display > pointing_waterfall_cuts[j];
        }
    }
};

// Quiet gaps are drawn as part of the continuous sample buffer. Samples from
// adjacent active slots are present in the timing guards, including after quiet.
void pointing_messages(const FloatPlan& plan, const std::vector<Schedule>& fixtures) {
    constexpr int episodes = 64, slots = 5;
    int exact = 0, closed = 0, reopened = 0, active_correct = 0, active_wrong = 0;
    int active_absent = 0, quiet_words = 0;
    for (int i = 0; i < episodes; ++i) {
        mm::Rng rng(0x706f6903, i);
        const int first_ordinal = rng.next() % (nfixtures - slots + 1);
        const int alignment = rng.next() % 2 ? 32 : -32;
        const double carrier = rng.next() % 2 ? .25 : -.25;
        const double phase = tau * rng.uniform();
        auto samples = noise(slots * nsamples + 2 * pad, rng);
        const std::array truth{int(rng.next() % words), int(rng.next() % words), -1, -1,
                               int(rng.next() % words)};
        for (int t = 0; t < slots; ++t) {
            if (truth[t] >= 0)
                transmit(samples, t * nsamples + pad + alignment, fixtures[first_ordinal + t],
                         truth[t], 18., false, phase + tau * carrier * t, 0., 2., carrier);
        }
        std::array<int, slots> decoded{};
        Calendar calendar;
        bool correct_closure = false, correct_reopen = false;
        for (int t = 0; t < slots; ++t) {
            const auto view = std::span(samples).subspan(t * nsamples, nsamples + 2 * pad);
            const auto bank = score_banks(view, fixtures[first_ordinal + t], plan, false, true);
            decoded[t] = joint_word(bank, pointing_cutoffs).value();
            const int before = calendar.completions;
            calendar.accept(decoded[t] >= 0);
            if (truth[t] >= 0) {
                active_correct += decoded[t] == truth[t];
                active_wrong += decoded[t] >= 0 && decoded[t] != truth[t];
                active_absent += decoded[t] < 0;
            } else {
                quiet_words += decoded[t] >= 0;
            }
            if (t == 3)
                correct_closure = decoded[2] < 0 && decoded[3] < 0 && before == 0 &&
                                  calendar.completions == 1 && !calendar.active;
            if (t == 4)
                correct_reopen = correct_closure && decoded[t] == truth[t] && calendar.active;
        }
        closed += correct_closure;
        reopened += correct_reopen;
        exact += correct_reopen && decoded == truth;
    }
    std::cout << "18 dB, " << episodes << " streams: exact " << exact
              << ", active correct/wrong/absent " << active_correct << '/' << active_wrong << '/'
              << active_absent << ", quiet false words " << quiet_words << ", closed " << closed
              << ", reopened correctly after closure " << reopened << '\n';
}

void check_pointing_loss(const FloatPlan& plan, const std::vector<Schedule>& fixtures) {
    constexpr std::array energy_db{12., 15., 16.5, 18., 21., 24., 25.5, 27., 30.};
    constexpr int trials = 160;
    std::array<PointingRow, energy_db.size()> active{};
    int quiet_words = 0;
    std::array<int, 2> quiet_visible{};
    for (int i = 0; i < trials; ++i) {
        Trial trial(0x706f6901, i);
        const auto& schedule = fixtures[trial.ordinal];
        mm::Rng oscillator(0x706f6902, i);
        const double carrier = oscillator.next() % 2 ? .25 : -.25;
        auto score = [&](std::span<const Sample> samples, double& display) {
            const auto bank = search_banks<1>(samples, schedule, plan, false, true, nullptr,
                                              &display, nullptr, nullptr)[0];
            if (!std::isfinite(display)) throw std::runtime_error("unscored pointing-loss display");
            return joint_word(bank, pointing_cutoffs).value();
        };
        double quiet_display;
        quiet_words += score(trial.samples, quiet_display) >= 0;
        for (size_t j = 0; j < quiet_visible.size(); ++j)
            quiet_visible[j] += quiet_display > pointing_waterfall_cuts[j];
        for (size_t j = 0; j < energy_db.size(); ++j) {
            auto samples = trial.samples;
            transmit(samples, pad + trial.alignment, schedule, trial.word, energy_db[j], false,
                     trial.phase, 0., 2., carrier);
            double display;
            const int decision = score(samples, display);
            active[j].observe(decision, trial.word, display, quiet_display);
        }
    }
    std::cout << trials << " quiet twins: words " << quiet_words << ", ordinary/strict visible "
              << quiet_visible[0] << '/' << quiet_visible[1] << '\n'
              << "E/N0 dB | correct/wrong/absent | ordinary visible/incorrect/already visible in quiet"
                 " | strict visible/incorrect/already visible in quiet\n";
    for (size_t j = 0; j < energy_db.size(); ++j) {
        const auto& row = active[j];
        std::cout << energy_db[j] << " | " << row.correct << '/' << row.wrong << '/' << row.absent;
        for (size_t k = 0; k < pointing_waterfall_cuts.size(); ++k)
            std::cout << " | " << row.visible[k] << '/' << row.visible_incorrect[k] << '/'
                      << row.visible_incorrect_on_quiet[k];
        std::cout << '\n';
        if (energy_db[j] == 27. && row.visible[0] > 0)
            std::cout << "27 dB conditional failure upper95 "
                      << upper_binomial(row.visible_incorrect[0], row.visible[0]) << '\n';
    }
    pointing_messages(plan, fixtures);
}
