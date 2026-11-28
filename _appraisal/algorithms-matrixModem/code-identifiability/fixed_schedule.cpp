// Use the research prototype's actual schedule, without copying its generator.
#define main sub9_monte_carlo_main
#include "../sub9-fsk-rate/coherent_block_mc.cpp"
#undef main

#include <cassert>
#include <set>
#include <utility>

static int distinct_messages(const Keyed& schedule, const std::vector<int>& keep) {
    std::set<std::vector<uint8_t>> seen;
    for (int msg = 0; msg < NMSG; ++msg) {
        std::vector<uint8_t> labels;
        for (int t : keep) {
            labels.push_back(schedule.pos[msg * schedule.nt + t]);
        }
        seen.insert(std::move(labels));
    }
    return static_cast<int>(seen.size());
}

static void report_distances(const Keyed& schedule, int hops) {
    int min_distance = hops, min_span = hops;
    for (int left = 0; left < NMSG; ++left) {
        for (int right = left + 1; right < NMSG; ++right) {
            int distance = 0, first = hops, last = -1;
            for (int hop = 0; hop < hops; ++hop) {
                const int a = left * schedule.nt + 2 * hop;
                const int b = right * schedule.nt + 2 * hop;
                if (schedule.pos[a] != schedule.pos[b] || schedule.pos[a + 1] != schedule.pos[b + 1]) {
                    ++distance;
                    first = std::min(first, hop);
                    last = hop;
                }
            }
            min_distance = std::min(min_distance, distance);
            min_span = std::min(min_span, distance == 0 ? 0 : last - first + 1);
        }
    }
    std::printf("  minimum differing hops / minimum containing span: %d / %d\n", min_distance, min_span);
}

int main() {
    const Format format{'x', "P16 L2", 3, 8, 16, 2};
    constexpr int hops = 32;
    constexpr int tones_per_hop = 2;
    for (unsigned seed = 1; seed <= 4; ++seed) {
        const Keyed schedule(format, seed, false);
        assert(schedule.nt == hops * tones_per_hop);
        std::printf("seed %u: distinct 10-bit messages\n", seed);
        for (int lost : {0, 16, 28, 31}) {
            int lowest = NMSG, highest = 0;
            const int starts = lost == 0 ? 1 : hops - lost + 1;
            for (int start = 0; start < starts; ++start) {
                std::vector<int> keep;
                for (int t = 0; t < schedule.nt; ++t) {
                    const int hop = t / tones_per_hop;
                    if (hop < start || hop >= start + lost) keep.push_back(t);
                }
                int count = distinct_messages(schedule, keep);
                lowest = std::min(lowest, count);
                highest = std::max(highest, count);
            }
            std::printf("  %2d consecutive hops erased: %d..%d\n", lost, lowest, highest);
        }
        std::vector<int> first, second;
        for (int t = 0; t < schedule.nt; ++t) {
            if (schedule.symof[t] == 0) first.push_back(t);
            if (schedule.symof[t] == 1) second.push_back(t);
        }
        assert(first.size() == 2 && second.size() == 2);
        std::printf("  two tones from one / two symbols: %d / %d\n",
                    distinct_messages(schedule, first),
                    distinct_messages(schedule, {first.front(), second.front()}));
        report_distances(schedule, hops);
    }
}
