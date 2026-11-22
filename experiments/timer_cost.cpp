// b6_timer_cost: the timer-operation experiment (Marc and Sarofem, plan p.22).
// Both timer backends hold the same 10, 100 or 500 pending timers and run the
// same operations, in the order the TimerManager uses them:
//   insert  - n sleeps (TimerManager::sleep -> TimerQueue::insert)
//   cancel  - n/10 cancellations by timer ID (TimerManager::cancel -> remove)
//   expire  - pop every remaining timer in wake order (expireDue -> popHead)
// Two wake-tick patterns: "random" (seeded, so every run is the same) and
// "ascending" (each new timer wakes last, the common sleep pattern).
//
// Table 1 counts key comparisons (operator< and operator==), using the same
// sortedList and MinHeap templates with an entry that counts its compares.
// The counts are exact and machine-independent. Table 2 times the real
// TimerQueueAdapter classes the TimerManager uses: median nanoseconds per
// operation over many repetitions. Timings depend on the machine and build;
// compare the two backends with each other, not with other machines.
// Prints Markdown tables (docs/results.md is made from this output).

#include "timer_manager.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <vector>

using namespace b6;

namespace {

std::uint64_t g_less = 0;
std::uint64_t g_equal = 0;

// TimerEntry with the same ordering and identity, counting every comparison.
struct CountedEntry {
    TimerEntry entry;
    bool operator<(const CountedEntry& other) const { ++g_less; return entry < other.entry; }
    bool operator==(const CountedEntry& other) const { ++g_equal; return entry == other.entry; }
};

enum class Pattern { Random, Ascending };
const char* name(Pattern p) { return p == Pattern::Random ? "random" : "ascending"; }

// Deterministic workload: wake ticks for n timers and the IDs to cancel.
struct Workload {
    std::vector<TimerEntry> timers;
    std::vector<TimerId> cancels;
};

Workload makeWorkload(std::size_t n, Pattern pattern) {
    std::uint64_t state = 0x9E3779B97F4A7C15ULL ^ (n * 31 + static_cast<std::uint64_t>(pattern));
    auto next = [&state] {  // xorshift64: repeatable on every compiler.
        state ^= state << 13; state ^= state >> 7; state ^= state << 17;
        return state;
    };
    Workload w;
    for (std::size_t i = 0; i < n; ++i) {
        TimerEntry e;
        e.timerId = static_cast<TimerId>(i + 1);
        e.jobId = static_cast<JobId>(i + 1);
        e.wakeTick = pattern == Pattern::Random ? static_cast<Tick>(1 + next() % (4 * n))
                                                : static_cast<Tick>(i + 1);
        e.insertionSequence = i + 1;
        w.timers.push_back(e);
    }
    std::vector<TimerId> ids;
    for (std::size_t i = 0; i < n; ++i) ids.push_back(static_cast<TimerId>(i + 1));
    for (std::size_t i = 0; i < n / 10; ++i) {  // Partial Fisher-Yates: distinct IDs.
        std::swap(ids[i], ids[i + next() % (n - i)]);
        w.cancels.push_back(ids[i]);
    }
    return w;
}

struct Counts {
    double insert = 0, cancel = 0, expire = 0;  // Comparisons per operation.
    std::vector<TimerId> order;                 // Expiry order, for the equivalence check.
};

template <class Storage>
Counts countComparisons(const Workload& w) {
    Storage storage;
    Counts c;
    auto take = [] { const std::uint64_t n = g_less + g_equal; g_less = g_equal = 0; return n; };
    take();
    for (const TimerEntry& e : w.timers) storage.insertTask(CountedEntry{e});
    c.insert = static_cast<double>(take()) / static_cast<double>(w.timers.size());
    for (TimerId id : w.cancels) {
        CountedEntry probe;
        probe.entry.timerId = id;
        storage.deleteTask(probe);
    }
    c.cancel = w.cancels.empty() ? 0 : static_cast<double>(take()) / static_cast<double>(w.cancels.size());
    // Reading the head is a copy, not a comparison, so this counts pops only.
    const std::size_t remaining = static_cast<std::size_t>(storage.size());
    CountedEntry head;
    while (storage.getValueAtHead(head)) {
        c.order.push_back(head.entry.timerId);
        storage.removeHead();
    }
    c.expire = static_cast<double>(take()) / static_cast<double>(remaining);
    return c;
}

struct Timing { double insert = 0, cancel = 0, expire = 0; };  // Median ns per operation.

double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

template <class Adapter>
Timing time(const Workload& w, const char* backendName, int repetitions) {
    using Clock = std::chrono::steady_clock;
    auto ns = [](Clock::time_point a, Clock::time_point b) {
        return static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(b - a).count());
    };
    std::vector<double> ins, can, exp;
    const double n = static_cast<double>(w.timers.size());
    const double k = static_cast<double>(w.cancels.size());
    for (int r = 0; r < repetitions; ++r) {
        Adapter queue(backendName);
        queue.reserve(kMaxJobs);  // As the TimerManager does before a run.
        const auto t0 = Clock::now();
        for (const TimerEntry& e : w.timers) queue.insert(e);
        const auto t1 = Clock::now();
        for (TimerId id : w.cancels) queue.remove(id);
        const auto t2 = Clock::now();
        while (queue.popHead()) {}
        const auto t3 = Clock::now();
        ins.push_back(ns(t0, t1) / n);
        if (k > 0) can.push_back(ns(t1, t2) / k);
        exp.push_back(ns(t2, t3) / (n - k));
    }
    return {median(ins), can.empty() ? 0 : median(can), median(exp)};
}

} // namespace

int main() {
    using ListCount = sortedList<CountedEntry>;
    using HeapCount = MinHeap<CountedEntry>;
    using ListQueue = TimerQueueAdapter<sortedList<TimerEntry>>;
    using HeapQueue = TimerQueueAdapter<MinHeap<TimerEntry>>;
    const std::size_t sizes[] = {10, 100, 500};
    bool allSame = true;

    std::cout << std::fixed << std::setprecision(1);
    std::cout << "Table 1. Key comparisons per operation (exact, machine-independent).\n\n"
                 "| Pending timers | Wake ticks | Backend | Insert | Cancel by ID | Expire (pop) | Same expiry order |\n"
                 "| --- | --- | --- | --- | --- | --- | --- |\n";
    for (std::size_t n : sizes) {
        for (Pattern p : {Pattern::Random, Pattern::Ascending}) {
            const Workload w = makeWorkload(n, p);
            const Counts list = countComparisons<ListCount>(w);
            const Counts heap = countComparisons<HeapCount>(w);
            const bool same = list.order == heap.order && list.order.size() == n - w.cancels.size();
            allSame = allSame && same;
            for (const auto& [backend, c] : {std::pair<const char*, const Counts&>{"sorted_list", list},
                                              std::pair<const char*, const Counts&>{"heap", heap}})
                std::cout << "| " << n << " | " << name(p) << " | " << backend << " | " << c.insert
                          << " | " << c.cancel << " | " << c.expire << " | " << (same ? "yes" : "**NO**")
                          << " |\n";
        }
    }

    std::cout << "\nTable 2. Median nanoseconds per operation (this machine and build only).\n\n"
                 "| Pending timers | Wake ticks | Backend | Insert | Cancel by ID | Expire (pop) |\n"
                 "| --- | --- | --- | --- | --- | --- |\n";
    for (std::size_t n : sizes) {
        for (Pattern p : {Pattern::Random, Pattern::Ascending}) {
            const Workload w = makeWorkload(n, p);
            const int reps = 2001;
            const Timing list = time<ListQueue>(w, "sorted_list", reps);
            const Timing heap = time<HeapQueue>(w, "heap", reps);
            for (const auto& [backend, t] : {std::pair<const char*, const Timing&>{"sorted_list", list},
                                              std::pair<const char*, const Timing&>{"heap", heap}})
                std::cout << "| " << n << " | " << name(p) << " | " << backend << " | " << t.insert
                          << " | " << t.cancel << " | " << t.expire << " |\n";
        }
    }
    std::cout << "\nCancellations: n/10 distinct timer IDs. Medians over 2001 repetitions.\n";
    return allSame ? 0 : 1;
}
