// Fixed-threshold end-to-end admissions on the configured one-cell calendar.
enum class AnchorScope { none, strongest, expanded, all, compare };

struct FaultScores {
    Orientations soft;
    Best affine, anchor, expanded, all;
    double waterfall;
    bool available;
};

bool ridge_available(const Ridge& ridge) {
    return std::all_of(ridge.begin(), ridge.end(),
                       [](const RidgeRow& row) { return std::isfinite(row.peak_over_median); });
}

FaultScores score_faults(std::span<const Sample> samples, const Schedule& schedule,
                         const FloatPlan& plan, AnchorScope scope = AnchorScope::none) {
    Ridge ridge;
    RidgeEnergy energy;
    double waterfall;
    const bool need_energy = scope != AnchorScope::none;
    auto soft = score_orientations(samples, schedule, plan, &waterfall, &ridge, need_energy ? &energy : nullptr);
    const bool available = ridge_available(ridge);
    Best anchor, expanded, all;
    if (need_energy && available) {
        if (scope == AnchorScope::compare) {
            const auto pooled = affine_trial::score_all_policies(ridge, energy, schedule);
            anchor = pooled.strongest;
            expanded = pooled.expanded;
            all = pooled.all;
        } else if (scope == AnchorScope::all) {
            const auto rows = affine_trial::select_anchors(ridge, affine_trial::AnchorPolicy::all_rows);
            all = affine_trial::score_anchors(ridge, energy, schedule, rows).best;
        } else {
            anchor = affine_trial::anchored(ridge, energy, schedule).best;
            if (scope == AnchorScope::expanded) {
                const auto rows = affine_trial::select_anchors(ridge, affine_trial::AnchorPolicy::group_runner_up);
                const auto runner = affine_trial::score_anchors(ridge, energy, schedule, rows).best;
                expanded = runner.score > anchor.score ? runner : anchor;
            }
        }
    }
    return {soft, affine_blind_word(ridge, schedule), anchor, expanded, all, waterfall, available};
}

struct FaultCuts {
    Cutoffs nominal{}, blind{};
    double affine{}, waterfall{}, high_waterfall{};
};

struct RefinementCuts {
    double waterfall, direct, expanded, all;
    // Independently frozen 396th of 399 quiet anchored maxima in --affine-refine.
    static constexpr double diagnostic = 290.;
};

std::optional<int> fault_word(const FaultScores& scores, const FaultCuts& cuts) {
    if (!scores.available) return std::nullopt;
    auto soft = joint_word(blind_orientation(scores.soft), cuts.blind);
    if (!soft || *soft >= 0) return soft;
    return scores.affine.score > cuts.affine ? scores.affine.word : -1;
}

std::vector<FaultScores> quiet_fault_scores(int count, uint64_t seed, const FloatPlan& plan,
                                            const std::vector<Schedule>& fixtures,
                                            AnchorScope scope = AnchorScope::none) {
    std::vector<FaultScores> scores(count);
    {
        std::vector<std::jthread> workers;
        for (int lane = 0; lane < 6; ++lane)
            workers.emplace_back([&, lane] {
                for (int i = lane; i < count; i += 6) {
                    Trial trial(seed, i);
                    scores[i] = score_faults(trial.samples, fixtures[trial.ordinal], plan, scope);
                }
            });
    }
    return scores;
}

FaultCuts train_fault_cuts(std::span<const FaultScores> training) {
    constexpr int count = 39999;
    if (training.size() != count) throw std::runtime_error("unexpected calibration size");
    std::vector<double> per(count), coh(count), waterfall(count);
    FaultCuts cuts;
    cuts.blind.per_tone = cuts.blind.coherent = cuts.affine = -INFINITY;
    for (int i = 0; i < count; ++i) {
        auto blind = blind_orientation(training[i].soft);
        cuts.blind.per_tone = std::max(cuts.blind.per_tone, blind.original.score);
        cuts.blind.coherent = std::max(cuts.blind.coherent, blind.coherent.score);
        cuts.affine = std::max(cuts.affine, training[i].affine.score);
        per[i] = training[i].soft[0].original.score;
        coh[i] = training[i].soft[0].coherent.score;
        waterfall[i] = training[i].waterfall;
    }
    auto order_statistic = [](std::vector<double>& v, size_t rank) {
        std::nth_element(v.begin(), v.begin() + rank - 1, v.end());
        return v[rank - 1];
    };
    cuts.nominal.per_tone = order_statistic(per, count - 1);
    cuts.nominal.coherent = order_statistic(coh, count - 1);
    cuts.high_waterfall = *std::max_element(waterfall.begin(), waterfall.end());
    cuts.waterfall = order_statistic(waterfall, 39600);
    return cuts;
}

struct SupplementOutcome {
    int crossings{}, added_correct{}, added_wrong{}, visible_correct{};

    void observe(bool crossed, int word, int primary, int truth, bool visible) {
        crossings += crossed;
        const bool added = primary < 0 && crossed;
        added_correct += added && truth >= 0 && word == truth;
        added_wrong += added && word != truth;
        const int final = added ? word : primary;
        visible_correct += visible && truth >= 0 && final == truth;
    }
};

struct FaultRow {
    Outcomes normal{}, oracle{}, combined{};
    int oracle_blocks{}, visible{}, visible_combined_correct{}, final_affine{};
    PairedDecisions paired;
    int eligible{}, below_gate_correct{}, ls_only_correct{}, ls_wrong_blocks_correct_anchor{};
    std::array<int, 3> crossings{}; // Primary per-tone, coherent, and ridge-fit families.
    std::array<SupplementOutcome, 4> alternatives{};

    void observe(const FaultScores& scores, const FaultCuts& cuts, int truth,
                 std::optional<int> known_orientation = {}) {
        auto record = [&](Outcomes& count, int decided) {
            if (decided < 0) ++count.absent;
            else if (decided == truth) ++count.correct;
            else ++count.wrong;
        };
        int baseline = joint_word(scores.soft[0], cuts.nominal).value();
        int decision = fault_word(scores, cuts).value();
        record(normal, baseline);
        record(combined, decision);
        if (known_orientation) {
            record(oracle, joint_word(scores.soft[*known_orientation], cuts.nominal).value());
            ++oracle_blocks;
        }
        const bool seen = scores.waterfall > cuts.waterfall;
        visible += seen;
        visible_combined_correct += seen && truth >= 0 && decision == truth;
        final_affine += decision >= 0 && *joint_word(blind_orientation(scores.soft), cuts.blind) < 0;
        if (truth >= 0) paired.observe(baseline, decision, truth);
    }

    void score(std::span<const Sample> samples, const Schedule& schedule, const FloatPlan& plan,
               const FaultCuts& cuts, int truth, const RefinementCuts* extra,
               std::optional<int> orientation = {}) {
        const auto r = score_faults(samples, schedule, plan, extra ? AnchorScope::compare : AnchorScope::none);
        observe(r, cuts, truth, orientation);
        if (!extra) return;
        const auto blind = blind_orientation(r.soft);
        const auto candidate = r.anchor;
        bool direct_correct = candidate.score > extra->direct && candidate.word == truth && truth >= 0;
        crossings[0] += blind.original.score > cuts.blind.per_tone;
        crossings[1] += blind.coherent.score > cuts.blind.coherent;
        crossings[2] += r.affine.score > cuts.affine;
        if (joint_word(blind, cuts.blind).value() < 0 && r.affine.score > cuts.affine) {
            ls_only_correct += truth >= 0 && r.affine.word == truth && !direct_correct;
            ls_wrong_blocks_correct_anchor += r.affine.word != truth && direct_correct;
        }
        const bool seen = r.waterfall > cuts.waterfall, high = r.waterfall > extra->waterfall;
        const int primary = fault_word(r, cuts).value();
        eligible += primary < 0 && high;
        below_gate_correct += primary < 0 && !high && candidate.score > extra->diagnostic &&
                              truth >= 0 && candidate.word == truth;
        alternatives[0].observe(high && candidate.score > extra->diagnostic, candidate.word, primary, truth, seen);
        alternatives[1].observe(candidate.score > extra->direct, candidate.word, primary, truth, seen);
        alternatives[2].observe(r.expanded.score > extra->expanded, r.expanded.word, primary, truth, seen);
        alternatives[3].observe(r.all.score > extra->all, r.all.word, primary, truth, seen);
    }

    void report(const std::string& label, bool refinement = false) const {
        auto triplet = [](const Outcomes& out) {
            std::cout << out.correct << '/' << out.wrong << '/' << out.absent;
        };
        std::cout << label << " | ";
        triplet(normal);
        std::cout << " | ";
        if (oracle_blocks) triplet(oracle);
        else std::cout << "n/a";
        std::cout << " | ";
        triplet(combined);
        std::cout << " | " << visible << " | " << visible_combined_correct << '/'
                  << visible - visible_combined_correct << " | " << final_affine
                  << " | " << paired.gains << '/' << paired.losses;
        if (refinement) {
            std::cout << " | " << eligible << '/' << below_gate_correct << " | " << ls_only_correct
                      << '/' << ls_wrong_blocks_correct_anchor << " | " << crossings[0] << '/'
                      << crossings[1] << '/' << crossings[2];
            for (const auto& alt : alternatives)
                std::cout << " | " << alt.added_correct << '/' << alt.added_wrong << ' '
                          << alt.visible_correct << '/' << visible - alt.visible_correct << ' ' << alt.crossings;
        }
        std::cout << '\n';
    }
};

void active_fault_holdouts(const FloatPlan& plan, const std::vector<Schedule>& fixtures,
                          const std::vector<Schedule>& other, const FaultCuts& cuts,
                          const RefinementCuts* extra = nullptr) {
    constexpr std::array energies = {15., 18., 34.};
    std::array<std::array<FaultRow, 2>, energies.size()> active{};
    std::array<FaultRow, 2> distorted{}, wrong{}, stuck{};
    for (int i = 0; i < 64; ++i) {
        Trial trial(0xfcad1003, i);
        const auto& schedule = fixtures[trial.ordinal];
        for (size_t level = 0; level < energies.size(); ++level) {
            auto samples = trial.samples;
            transmit(samples, pad + trial.alignment, schedule, trial.word, energies[level],
                     false, trial.phase, 0., 2., i % 2 ? .25 : -.25);
            for (int o = 0; o < 2; ++o) {
                if (o == 1) reflect_samples(samples);
                active[level][o].score(samples, schedule, plan, cuts, trial.word, extra, o);
            }
        }
        for (int o = 0; o < 2; ++o) {
            auto block = trial;
            affine_trial::emit(block, schedule, affine_trial::maps[1 + o], 34.);
            distorted[o].score(block.samples, schedule, plan, cuts, trial.word, extra);
        }
        Trial mismatch(0xfcad1004, i), line(0xfcad1005, i);
        for (int level = 0; level < 2; ++level) {
            const double db = level ? 44. : 34.;
            auto samples = mismatch.samples;
            transmit(samples, pad + mismatch.alignment, other[mismatch.ordinal], mismatch.word,
                     db, false, mismatch.phase, 0., 2., i % 2 ? .25 : -.25);
            wrong[level].score(samples, fixtures[mismatch.ordinal], plan, cuts, -1, extra);
            auto carrier = line;
            affine_trial::emit(carrier, fixtures[line.ordinal], {0., 29.}, db, false, true);
            stuck[level].score(carrier.samples, fixtures[line.ordinal], plan, cuts, -1, extra);
        }
    }
    std::cout << "case | nominal C/W/A | orientation oracle C/W/A | combined C/W/A"
                 " | visible | visible C/fail | admitted via affine | paired gains/losses";
    if (extra) std::cout << " | gate eligible/below-gate correct at 290 | LS uniquely correct/blocked anchor"
                            " | crossings per/coh/LS | each alternative [added C/W, visible C/fail, crossings]"
                            " in order gated, direct, expanded, all pairs";
    std::cout << '\n';
    for (size_t level = 0; level < energies.size(); ++level)
        for (int o = 0; o < 2; ++o)
            active[level][o].report(std::string(o ? "mirrored " : "normal ") +
                                    std::to_string(int(energies[level])), extra != nullptr);
    distorted[0].report("compressed 34", extra != nullptr);
    distorted[1].report("reversed compressed 34", extra != nullptr);
    for (int j = 0; j < 2; ++j) {
        wrong[j].report(j ? "wrong schedule 44" : "wrong schedule 34", extra != nullptr);
        stuck[j].report(j ? "independent line 44" : "independent line 34", extra != nullptr);
    }
}

bool mixed_fault_calendar(const FloatPlan& plan, const std::vector<Schedule>& fixtures,
                          const FaultCuts& cuts) {
    // In one physical episode the transmitter changes faults between words.
    constexpr std::array truth = {319, 715, -1, -1};
    mm::Rng rng(0xfcad1006);
    auto stream = noise(nsamples * truth.size() + 2 * pad, rng);
    Trial first_block(0xfcad1006, 1);
    first_block.alignment = 32;
    first_block.word = truth[0];
    first_block.samples.assign(first_block.samples.size(), Sample{});
    affine_trial::emit(first_block, fixtures[0], affine_trial::maps[1], 34.);
    for (size_t i = 0; i < first_block.samples.size(); ++i) stream[i] += first_block.samples[i];
    std::vector<Sample> tx(nsamples);
    transmit(tx, 0, fixtures[1], truth[1], 34., false, .41, 0., 2., .25);
    for (int i = 0; i < nsamples; ++i)
        stream[nsamples + pad + 32 + i] += std::conj(tx[i]);
    Calendar calendar;
    bool pass = true;
    for (size_t t = 0; t < truth.size(); ++t) {
        auto block = std::span(stream).subspan(t * nsamples, nsamples + 2 * pad);
        auto word = fault_word(score_faults(block, fixtures[t], plan), cuts);
        if (word) calendar.accept(*word >= 0);
        else calendar.unscored();
        pass &= word && *word == truth[t] && calendar.completions == int(t + 1 == truth.size());
        std::cout << "mixed calendar " << t << " word " << word.value_or(-2) << " completions "
                  << calendar.completions << '\n';
    }
    return pass && !calendar.erasures && !fault_word(score_faults({}, fixtures[0], plan), cuts);
}

bool fault_missing_row(const FloatPlan& plan, const std::vector<Schedule>& fixtures,
                       const FaultCuts& cuts) {
    Trial trial(0xfcad1007, 0);
    auto& schedule = fixtures[trial.ordinal];
    auto complete = score_faults(trial.samples, schedule, plan);
    Ridge constant;
    constant.fill({29, 50.});
    complete.affine = affine_blind_word(constant, schedule);
    if (!complete.available || complete.affine.word >= 0 || !fault_word(complete, cuts))
        return false;
    // Aggregate input scaling still succeeds when one complete nominal cell is missing.
    auto cell = std::span(trial.samples).subspan(pad + 23 * nfft, nfft);
    std::fill(cell.begin(), cell.end(), Sample{NAN, NAN});
    auto incomplete = score_faults(trial.samples, schedule, plan);
    if (incomplete.available || fault_word(incomplete, cuts)) return false;
    Calendar calendar;
    calendar.accept(true);
    calendar.accept(false);
    calendar.unscored();
    calendar.accept(false);
    if (calendar.completions != 0) return false;
    calendar.accept(false);
    return calendar.completions == 1;
}

bool check_fault_admission(const FloatPlan& plan, const std::vector<Schedule>& fixtures,
                           const std::vector<Schedule>& other) {
    const auto start = std::chrono::steady_clock::now();
    auto cuts = train_fault_cuts(quiet_fault_scores(39999, 0xfcad1001, plan, fixtures));
    std::cout << std::setprecision(12) << "39999 quiet calibration; nominal per/coherent "
              << cuts.nominal.per_tone << ' ' << cuts.nominal.coherent
              << "; two-orientation per/coherent and affine " << cuts.blind.per_tone << ' '
              << cuts.blind.coherent << ' ' << cuts.affine << "; 1% waterfall " << cuts.waterfall
              << "; training maximum waterfall " << cuts.high_waterfall
              << std::endl;
    constexpr int n = 30000;
    FaultRow quiet;
    std::array<int, 3> per_family{};
    for (const auto& r : quiet_fault_scores(n, 0xfcad1002, plan, fixtures)) {
        auto blind = blind_orientation(r.soft);
        per_family[0] += blind.original.score > cuts.blind.per_tone;
        per_family[1] += blind.coherent.score > cuts.blind.coherent;
        per_family[2] += r.affine.score > cuts.affine;
        quiet.observe(r, cuts, -1);
    }
    const double upper = upper_binomial(quiet.combined.wrong, n);
    std::cout << "quiet false original/combined " << quiet.normal.wrong << '/' << quiet.combined.wrong
              << " of " << n << "; per/coherent/affine " << per_family[0] << '/' << per_family[1]
              << '/' << per_family[2] << "; waterfall " << quiet.visible << "; exact combined upper95 "
              << upper << "; fixed-threshold 1e-4 endpoint " << (upper <= 1e-4 ? "met" : "not met")
              << std::endl;
    active_fault_holdouts(plan, fixtures, other, cuts);
    bool pass = mixed_fault_calendar(plan, fixtures, cuts) && fault_missing_row(plan, fixtures, cuts);
    std::cout << "mixed calendar and unscored checks " << (pass ? "pass" : "FAIL")
              << "; experiment seconds "
              << std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() << '\n';
    return pass;
}

enum class Refinement { gated, direct, expanded, all };

std::optional<int> refined_fault_word(std::span<const Sample> samples, const Schedule& schedule,
        const FloatPlan& plan, const FaultCuts& cuts, const RefinementCuts& extra,
        Refinement policy, int& calls) {
    const bool direct = policy != Refinement::gated;
    constexpr std::array scopes = {AnchorScope::none, AnchorScope::strongest, AnchorScope::expanded, AnchorScope::all};
    const auto scores = score_faults(samples, schedule, plan, scopes[static_cast<size_t>(policy)]);
    auto word = fault_word(scores, cuts);
    if (!word || (!direct && (*word >= 0 || scores.waterfall <= extra.waterfall))) return word;
    ++calls;
    Best candidate = direct ? scores.anchor : affine_anchored_word(samples, schedule, plan);
    double threshold = direct ? extra.direct : extra.diagnostic;
    if (policy == Refinement::expanded) {
        candidate = scores.expanded;
        threshold = extra.expanded;
    }
    if (policy == Refinement::all) {
        candidate = scores.all;
        threshold = extra.all;
    }
    if (*word < 0 && candidate.score > threshold) return candidate.word;
    return word;
}

bool all_pairs_calendar(const FloatPlan& plan, const std::vector<Schedule>& fixtures,
                        const FaultCuts& cuts, const RefinementCuts& extra) {
    // The middle transmitter sends word 217 through zero gain, followed by
    // an inverted signal. Only slots 3 and 4 are physically quiet.
    constexpr std::array emitted = {617, 217, 429, -1, -1, 785};
    mm::Rng rng(0xfcad1014);
    auto stream = noise(emitted.size() * nsamples + 2 * pad, rng);
    for (size_t t = 0; t < emitted.size(); ++t) {
        if (emitted[t] < 0) continue;
        Trial tx(0xfcad1014, int(t) + 1);
        std::fill(tx.samples.begin(), tx.samples.end(), Sample{});
        tx.word = emitted[t];
        tx.alignment = 32;
        if (t == 0) affine_trial::emit(tx, fixtures[t], affine_trial::maps[1], 27., true, false, 15., 2);
        else transmit(tx.samples, pad + tx.alignment, fixtures[t], tx.word, 34.,
                      false, tx.phase, 0., 2., .25);
        if (t == 2) reflect_samples(tx.samples);
        const float gain = t == 1 ? 0.f : 1.f;
        for (size_t i = 0; i < tx.samples.size(); ++i) stream[t * nsamples + i] += gain * tx.samples[i];
    }
    Calendar calendar;
    bool pass = true;
    for (size_t t = 0; t < emitted.size(); ++t) {
        const auto iq = std::span(stream).subspan(t * nsamples, nsamples + 2 * pad);
        if (t == 0) {
            const auto first = score_faults(iq, fixtures[t], plan, AnchorScope::compare);
            const auto primary = fault_word(first, cuts);
            pass &= primary && *primary < 0 && first.waterfall > cuts.waterfall;
            std::cout << "first damaged word WF " << first.waterfall << " primary " << primary.value_or(-2)
                      << " strongest/expanded/all scores " << first.anchor.score << '/'
                      << first.expanded.score << '/' << first.all.score << '\n';
        }
        int calls = 0;
        const auto word = refined_fault_word(iq, fixtures[t], plan, cuts, extra, Refinement::all, calls);
        if (word) calendar.accept(*word >= 0);
        else calendar.unscored();
        pass &= calls == 1 && word && *word == (t == 1 ? -1 : emitted[t]);
        pass &= calendar.completions == int(t >= 4) && calendar.erasures == int(t >= 2) &&
                calendar.active == (t != 4) && calendar.pending == int(t == 1 || t == 3);
        std::cout << "all-pairs calendar ordinal " << t << " word " << word.value_or(-2)
                  << " erasures " << calendar.erasures << " completions " << calendar.completions
                  << " active " << calendar.active << '\n';
    }
    return pass;
}

void middle_strength_holdouts(const FloatPlan& plan, const std::vector<Schedule>& fixtures,
                             const FaultCuts& cuts, const RefinementCuts& extra) {
    struct Case { int map, energy; bool substituted; };
    constexpr std::array<Case, 5> cases{{{1,24,false}, {2,24,false}, {1,27,false},
                                         {2,27,false}, {1,27,true}}};
    std::array<FaultRow, cases.size()> counts;
    for (int i = 0; i < 64; ++i) {
        Trial source(0xfcad1008, i);
        const auto& schedule = fixtures[source.ordinal];
        for (size_t j = 0; j < cases.size(); ++j) {
            const auto c = cases[j];
            auto block = source;
            affine_trial::emit(block, schedule, affine_trial::maps[c.map], c.energy, c.substituted);
            counts[j].score(block.samples, schedule, plan, cuts, block.word, &extra);
        }
    }
    for (size_t j = 0; j < cases.size(); ++j) {
        const auto c = cases[j];
        counts[j].report(std::string(c.map == 1 ? "compressed " : "reversed compressed ") +
                         std::to_string(c.energy) + (c.substituted ? ", 8 substituted" : ""), true);
    }
}

void bright_fault_holdouts(const FloatPlan& plan, const std::vector<Schedule>& fixtures,
                          const std::vector<Schedule>& other, const FaultCuts& cuts,
                          const RefinementCuts& extra) {
    std::array<FaultRow, 9> rows{};
    for (int i = 0; i < 64; ++i) {
        Trial source(0xfcad1011, i);
        const auto& schedule = fixtures[source.ordinal];
        auto equal = source;
        affine_trial::emit(equal, schedule, affine_trial::maps[1], 27., true);
        rows[0].score(equal.samples, schedule, plan, cuts, equal.word, &extra);
        for (int count : {1, 2}) {
            const int first_row = 1 + 4 * (count - 1);
            for (int level = 0; level < 2; ++level) {
                auto block = source;
                affine_trial::emit(block, schedule, affine_trial::maps[1], level ? 34. : 27.,
                                   true, false, 15., count);
                rows[first_row + level].score(block.samples, schedule, plan, cuts, block.word, &extra);
            }
            Trial wrong(0xfcad1012, i);
            affine_trial::emit(wrong, other[wrong.ordinal], affine_trial::maps[1], 34., true, false, 15., count);
            rows[first_row + 2].score(wrong.samples, fixtures[wrong.ordinal], plan, cuts, -1, &extra);
            Trial line(0xfcad1013, i);
            const int replaced = 8 * count;
            const double total_db = 34. + mm::db((tones - replaced + replaced * mm::undb(15.)) / tones);
            affine_trial::emit(line, fixtures[line.ordinal], affine_trial::maps[1], total_db, false, true);
            rows[first_row + 3].score(line.samples, fixtures[line.ordinal], plan, cuts, -1, &extra);
        }
    }
    constexpr std::array names = {"27 eight equal-power", "27 eight boosted", "34 eight boosted",
        "wrong schedule 34 eight boosted", "line at 34 eight boosted total", "27 sixteen boosted",
        "34 sixteen boosted", "wrong schedule 34 sixteen boosted", "line at 34 sixteen boosted total"};
    for (size_t j = 0; j < rows.size(); ++j) rows[j].report(names[j], true);
}

void refinement_service(const FloatPlan& plan, const std::vector<Schedule>& fixtures,
                        const std::vector<Schedule>& other, const FaultCuts& cuts,
                        const RefinementCuts& extra) {
    constexpr int blocks = 17; // First block warms the complete scoring path.
    constexpr std::array names = {"quiet", "wrong schedule 34", "compressed 27", "boosted substitutions 34"};
    for (size_t s = 0; s < names.size(); ++s) {
        mm::Rng rng(0xfcad1009, s);
        auto stream = noise(nsamples * blocks + 2 * pad, rng);
        for (int t = 0; s && t < blocks; ++t) {
            const int ordinal = t % nfixtures;
            auto input = std::span(stream).subspan(t * nsamples, nsamples + 2 * pad);
            if (s == 1) {
                const int word = rng.next() % words;
                const double phase = tau * rng.uniform();
                transmit(input, pad + 32, other[ordinal], word, 34., false, phase, 0., 2., .25);
            } else {
                Trial block(0xfcad1010, t);
                block.samples.assign(input.size(), Sample{});
                block.alignment = 32;
                affine_trial::emit(block, fixtures[ordinal], affine_trial::maps[1], s == 3 ? 34. : 27.,
                                   s == 3, false, s == 3 ? 15. : 0.);
                for (size_t i = 0; i < input.size(); ++i) input[i] += block.samples[i];
            }
        }
        for (auto policy : {Refinement::gated, Refinement::direct, Refinement::expanded, Refinement::all}) {
            int calls = 0, checksum = 0;
            Calendar calendar;
            std::array<double, blocks - 1> elapsed{};
            for (int t = 0; t < blocks; ++t) {
                auto input = std::span(stream).subspan(t * nsamples, nsamples + 2 * pad);
                int invoked = 0;
                const auto start = std::chrono::steady_clock::now();
                auto word = refined_fault_word(input, fixtures[t % nfixtures], plan, cuts,
                                              extra, policy, invoked);
                if (word) calendar.accept(*word >= 0);
                else calendar.unscored();
                const auto end = std::chrono::steady_clock::now();
                if (t) {
                    elapsed[t - 1] = std::chrono::duration<double>(end - start).count();
                    calls += invoked;
                    checksum += word.value_or(-2);
                }
            }
            std::sort(elapsed.begin(), elapsed.end());
            constexpr std::array labels = {" gated", " direct", " expanded", " all pairs"};
            std::cout << "serial 16 consecutive " << names[s] << labels[static_cast<size_t>(policy)]
                      << "; scorer seconds total/median/max " << std::accumulate(elapsed.begin(), elapsed.end(), 0.)
                      << '/' << (elapsed[7] + elapsed[8]) / 2. << '/' << elapsed.back()
                      << "; extra scorer calls " << calls << "; word checksum " << checksum << '\n';
        }
    }
}

bool check_fault_refinement(const FloatPlan& plan, const std::vector<Schedule>& fixtures,
                            const std::vector<Schedule>& other) {
    const auto calibration = quiet_fault_scores(39999, 0xfcad1001, plan, fixtures, AnchorScope::compare);
    const auto cuts = train_fault_cuts(calibration);
    RefinementCuts extra{cuts.high_waterfall, -INFINITY, -INFINITY, -INFINITY};
    for (const auto& r : calibration) {
        if (!r.available || !std::isfinite(r.waterfall)) return false;
        extra.direct = std::max(extra.direct, r.anchor.score);
        extra.expanded = std::max(extra.expanded, r.expanded.score);
        extra.all = std::max(extra.all, r.all.score);
    }
    std::cout << std::setprecision(16) << "39999 quiet maximum waterfall/anchored/expanded/all-pairs "
              << extra.waterfall << '/' << extra.direct << '/' << extra.expanded << '/' << extra.all << "; conditional diagnostic cut "
              << extra.diagnostic << std::endl;
    int primary_false = 0, high_count = 0, gated_count = 0, direct_count = 0, gated_added = 0, direct_added = 0;
    int expanded_count = 0, expanded_added = 0, all_count = 0, all_added = 0;
    constexpr int nhold = 30000;
    for (const auto& r : quiet_fault_scores(nhold, 0xfcad1002, plan, fixtures, AnchorScope::compare)) {
        if (!r.available || !std::isfinite(r.waterfall)) return false;
        const bool primary = fault_word(r, cuts).value() >= 0;
        const bool high = r.waterfall > extra.waterfall;
        const bool gated = high && r.anchor.score > extra.diagnostic;
        const bool direct = r.anchor.score > extra.direct;
        const bool expanded = r.expanded.score > extra.expanded;
        primary_false += primary;
        high_count += high;
        gated_count += gated;
        direct_count += direct;
        expanded_count += expanded;
        gated_added += gated && !primary;
        direct_added += direct && !primary;
        expanded_added += expanded && !primary;
        const bool all = r.all.score > extra.all;
        all_count += all;
        all_added += all && !primary;
    }
    const int gated_false = primary_false + gated_added, direct_false = primary_false + direct_added;
    std::cout << "30000 quiet: high WF/gated/direct family crossings " << high_count << '/' << gated_count
              << '/' << direct_count << "; added false words gated/direct " << gated_added << '/' << direct_added
              << "; full false counts " << gated_false << '/' << direct_false
              << "; exact upper95 " << upper_binomial(gated_false, nhold) << '/'
              << upper_binomial(direct_false, nhold) << "; both training/holdout ridges complete\n"
              << "expanded family crossings/additional/full " << expanded_count << '/' << expanded_added
              << '/' << primary_false + expanded_added << "; exact upper95 "
              << upper_binomial(primary_false + expanded_added, nhold)
              << "; all-pair crossings/additional/full " << all_count << '/' << all_added << '/'
              << primary_false + all_added << "; exact upper95 " << upper_binomial(primary_false + all_added, nhold)
              << '\n';
    active_fault_holdouts(plan, fixtures, other, cuts, &extra);
    middle_strength_holdouts(plan, fixtures, cuts, extra);
    bright_fault_holdouts(plan, fixtures, other, cuts, extra);
    bool pass = mixed_fault_calendar(plan, fixtures, cuts) && fault_missing_row(plan, fixtures, cuts);
    std::cout << "mixed and unavailable-row calendars " << (pass ? "pass" : "FAIL") << '\n';
    pass &= all_pairs_calendar(plan, fixtures, cuts, extra);
    refinement_service(plan, fixtures, other, cuts, extra);
    return pass;
}
