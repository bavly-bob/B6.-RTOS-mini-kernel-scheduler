#include "MinHeap.h"
#include "../kernel/include/types.h"  // header-only records: TimerEntry, Job

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <type_traits>

// Checks stay active with NDEBUG, unlike assert().
#define CHECK(cond)                                                           \
    do {                                                                      \
        if (!(cond)) {                                                        \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << ": " #cond \
                      << "\n";                                                \
            std::exit(1);                                                     \
        }                                                                     \
    } while (0)

// Entry type independent of kernel/types.h: ordered by (key, seq),
// identified by id, so ordering and identity are deliberately different.
struct Entry {
    int id = 0;
    long long key = 0;
    unsigned seq = 0;

    bool operator<(const Entry& o) const {
        return key < o.key || (key == o.key && seq < o.seq);
    }
    bool operator==(const Entry& o) const { return id == o.id; }
};

// Pops everything and checks the output never decreases.
static void checkDrainsSorted(MinHeap<int>& heap, int expectedCount) {
    int previous = 0;
    for (int i = 0; i < expectedCount; ++i) {
        int value = heap.extractMin();
        if (i > 0) {
            CHECK(previous <= value);
        }
        previous = value;
    }
    CHECK(heap.isEmpty());
}

static void testOrdering() {
    MinHeap<int> heap;
    const int values[] = {42, 7, 19, -3, 100, 0, 7, 55, -20, 13, 8};
    const int n = sizeof(values) / sizeof(values[0]);
    for (int i = 0; i < n; ++i) {
        heap.insert(values[i]);
    }
    CHECK(heap.size() == n);
    CHECK(heap.peek() == -20);
    checkDrainsSorted(heap, n);

    // Bottom-up construction from a reversed array.
    int reversed[50];
    for (int i = 0; i < 50; ++i) {
        reversed[i] = 50 - i;
    }
    MinHeap<int> built(reversed, 50);
    CHECK(built.size() == 50);
    for (int expected = 1; expected <= 50; ++expected) {
        CHECK(built.extractMin() == expected);
    }

    // Interleaved insert/extract keeps the minimum correct.
    MinHeap<int> mixed;
    mixed.insert(5);
    mixed.insert(3);
    CHECK(mixed.extractMin() == 3);
    mixed.insert(1);
    mixed.insert(4);
    CHECK(mixed.peek() == 1);
    CHECK(mixed.extractMin() == 1);
    CHECK(mixed.extractMin() == 4);
    CHECK(mixed.extractMin() == 5);
    CHECK(mixed.isEmpty());
    std::cout << "PASS: heap ordering\n";
}

static void testDuplicates() {
    MinHeap<int> heap;
    for (int i = 0; i < 10; ++i) {
        heap.insert(4);
    }
    heap.insert(2);
    heap.insert(2);
    CHECK(heap.size() == 12);
    CHECK(heap.extractMin() == 2);
    CHECK(heap.extractMin() == 2);
    for (int i = 0; i < 10; ++i) {
        CHECK(heap.extractMin() == 4);
    }
    CHECK(heap.isEmpty());

    // remove() takes exactly one of several equal entries.
    heap.insert(9);
    heap.insert(9);
    heap.insert(9);
    CHECK(heap.remove(9));
    CHECK(heap.size() == 2);
    CHECK(heap.extractMin() == 9);
    CHECK(heap.extractMin() == 9);
    std::cout << "PASS: duplicates\n";
}

static void testEmptyBehavior() {
    MinHeap<int> heap;
    CHECK(heap.isEmpty());
    CHECK(heap.size() == 0);
    CHECK(!heap.remove(1));

    bool threw = false;
    try {
        heap.peek();
    } catch (const EmptyHeapError& e) {
        threw = e.message != nullptr;
    }
    CHECK(threw);

    threw = false;
    try {
        heap.extractMin();
    } catch (const EmptyHeapError& e) {
        threw = e.message != nullptr;
    }
    CHECK(threw);

    // Draining back to empty, and clear(), both restore empty behavior.
    heap.insert(1);
    CHECK(heap.extractMin() == 1);
    CHECK(heap.isEmpty());
    heap.insert(2);
    heap.insert(3);
    int capacityBefore = heap.capacity();
    heap.clear();
    CHECK(heap.isEmpty());
    CHECK(heap.capacity() == capacityBefore);
    threw = false;
    try {
        heap.extractMin();
    } catch (const EmptyHeapError&) {
        threw = true;
    }
    CHECK(threw);

    // Empty and negative-count array construction.
    MinHeap<int> fromNothing(nullptr, 0);
    CHECK(fromNothing.isEmpty());
    MinHeap<int> fromNegative(nullptr, -4);
    CHECK(fromNegative.isEmpty());
    fromNegative.insert(6);
    CHECK(fromNegative.peek() == 6);
    std::cout << "PASS: empty behavior\n";
}

static void testCopyAndGrowth() {
    MinHeap<int> heap;
    const int initialCapacity = heap.capacity();
    CHECK(initialCapacity > 0);
    for (int i = 0; i < initialCapacity; ++i) {
        heap.insert(1000 - i);
    }
    CHECK(heap.capacity() == initialCapacity);
    heap.insert(-1);  // forces doubling
    CHECK(heap.capacity() == initialCapacity * 2);
    for (int i = 0; i < 200; ++i) {
        heap.insert((i * 37) % 101);
    }
    CHECK(heap.size() == initialCapacity + 201);
    CHECK(heap.peek() == -1);

    // Copy construction is deep and independent.
    MinHeap<int> copy(heap);
    CHECK(copy.size() == heap.size());
    CHECK(copy.extractMin() == -1);
    CHECK(heap.peek() == -1);
    CHECK(copy.size() == heap.size() - 1);
    heap.insert(-50);
    CHECK(copy.peek() != -50);

    // Copy assignment replaces old contents; self-assignment is harmless.
    MinHeap<int> assigned;
    assigned.insert(12345);
    assigned = heap;
    CHECK(assigned.size() == heap.size());
    CHECK(assigned.extractMin() == -50);
    CHECK(heap.peek() == -50);
    MinHeap<int>& alias = assigned;
    assigned = alias;
    CHECK(assigned.size() == heap.size() - 1);
    checkDrainsSorted(assigned, heap.size() - 1);
    CHECK(heap.size() == initialCapacity + 202);

    // Built heap grows past an exact-size initial array.
    int values[20];
    for (int i = 0; i < 20; ++i) {
        values[i] = i;
    }
    MinHeap<int> built(values, 20);
    CHECK(built.capacity() == 20);
    built.insert(-7);
    CHECK(built.capacity() == 40);
    CHECK(built.peek() == -7);
    std::cout << "PASS: copy and growth\n";
}

static void testReserveAndRemove() {
    MinHeap<int> heap;
    heap.insert(3);
    heap.reserve(2);  // smaller: no change
    CHECK(heap.capacity() == 8);
    heap.reserve(64);
    CHECK(heap.capacity() == 64);
    CHECK(heap.peek() == 3);
    for (int i = 0; i < 63; ++i) {
        heap.insert(100 + i);
    }
    CHECK(heap.capacity() == 64);  // no reallocation within reserved space

    // Remove root, an inner node, a leaf, and the last slot.
    MinHeap<int> r;
    const int values[] = {1, 5, 2, 8, 6, 3, 4, 9, 10, 7};
    for (int v : values) {
        r.insert(v);
    }
    CHECK(r.remove(1));
    CHECK(r.remove(6));
    CHECK(r.remove(10));
    CHECK(!r.remove(42));
    CHECK(r.size() == 7);
    const int expected[] = {2, 3, 4, 5, 7, 8, 9};
    for (int v : expected) {
        CHECK(r.extractMin() == v);
    }

    // The moved last element must sift up when it is smaller than the hole's parent.
    MinHeap<int> up;
    const int shape[] = {1, 10, 2, 11, 12, 3, 4};
    for (int v : shape) {
        up.insert(v);
    }
    CHECK(up.remove(11));  // last value 4 moves under 10 and must rise
    const int afterUp[] = {1, 2, 3, 4, 10, 12};
    for (int v : afterUp) {
        CHECK(up.extractMin() == v);
    }
    std::cout << "PASS: reserve and arbitrary removal\n";
}

static void testIndependentEntryType() {
    MinHeap<Entry> heap;
    heap.insert({1, 50, 0});
    heap.insert({2, 10, 2});
    heap.insert({3, 10, 1});  // same key, earlier sequence
    heap.insert({4, 30, 3});
    heap.insert({5, 10, 4});

    CHECK(heap.peek().id == 3);

    // Identity removal ignores the ordering key fields.
    Entry byId;
    byId.id = 4;
    CHECK(heap.remove(byId));
    byId.id = 99;
    CHECK(!heap.remove(byId));

    const int expectedIds[] = {3, 2, 5, 1};
    for (int id : expectedIds) {
        CHECK(heap.extractMin().id == id);
    }
    CHECK(heap.isEmpty());

    Entry array[3] = {{7, 3, 0}, {8, 1, 0}, {9, 2, 0}};
    MinHeap<Entry> built(array, 3);
    MinHeap<Entry> copy(built);
    CHECK(built.extractMin().id == 8);
    CHECK(copy.peek().id == 8);
    CHECK(copy.size() == 3);
    std::cout << "PASS: independent entry type\n";
}

// What a TimerManager does each tick, written only against the sortedList-shaped
// API so the same code serves either TimerBackend. Returns fired timer IDs in order.
template <typename Backend>
static int fireDue(Backend& timers, b6::Tick now, int* firedIds) {
    int fired = 0;
    b6::TimerEntry head;
    while (timers.getValueAtHead(head) && head.wakeTick <= now) {
        CHECK(timers.removeHead());
        if (head.active) {
            firedIds[fired++] = head.timerId;
        }
    }
    return fired;
}

static void testTimerBackendApi() {
    using b6::TimerEntry;
    MinHeap<TimerEntry> timers;
    b6::TimerEntry head;
    CHECK(!timers.getValueAtHead(head));
    CHECK(!timers.removeHead());
    CHECK(!timers.deleteTask(TimerEntry{}));

    // {timerId, jobId, wakeTick, insertionSequence, active}, inserted out of order.
    timers.insertTask(TimerEntry{10, 1, 7, 5, true});
    timers.insertTask(TimerEntry{11, 2, 5, 9, true});
    timers.insertTask(TimerEntry{12, 3, 5, 2, true});  // same wake, earlier sequence
    timers.insertTask(TimerEntry{13, 4, 9, 1, true});
    timers.insertTask(TimerEntry{14, 5, 5, 6, false});  // lazily cancelled
    timers.insertTask(TimerEntry{15, 6, 7, 3, true});

    CHECK(timers.getValueAtHead(head) && head.timerId == 12);

    // Physical cancellation matches by timerId only; the other fields are ignored.
    TimerEntry cancelKey;
    cancelKey.timerId = 13;
    CHECK(timers.deleteTask(cancelKey));
    CHECK(!timers.deleteTask(cancelKey));
    CHECK(timers.size() == 5);

    int fired[8];
    CHECK(fireDue(timers, 4, fired) == 0);
    CHECK(timers.size() == 5);

    // Tick 5: (5,2) then (5,9); the inactive (5,6) is drained but not fired.
    CHECK(fireDue(timers, 5, fired) == 2);
    CHECK(fired[0] == 12 && fired[1] == 11);
    CHECK(timers.size() == 2);

    // Reschedule = delete + insert with a new sequence; never edit a queued key.
    TimerEntry moved{10, 1, 8, 20, true};
    CHECK(timers.deleteTask(moved));
    timers.insertTask(moved);
    CHECK(fireDue(timers, 7, fired) == 1 && fired[0] == 15);
    CHECK(fireDue(timers, 100, fired) == 1 && fired[0] == 10);
    CHECK(timers.isEmpty());

    // Queued entries are only reachable through const references.
    static_assert(std::is_same<decltype(timers.peek()), const TimerEntry&>::value,
                  "peek must not expose a mutable ordering key");
    std::cout << "PASS: TimerManager heap backend API\n";
}

static b6::Job makeJob(b6::JobId id, b6::Tick deadline, b6::Sequence releaseSeq) {
    b6::Job job;
    job.jobId = id;
    job.absoluteDeadline = deadline;
    job.releaseSequence = releaseSeq;
    return job;
}

static EdfEntry edfEntryFor(const b6::Job& job) {
    return EdfEntry(job.absoluteDeadline, job.releaseSequence, job.jobId);
}

static void testEdfOrderingAndTies() {
    // Deadline first, then release sequence, then job ID.
    b6::Job jobs[6] = {
        makeJob(6, 20, 1), makeJob(2, 10, 4), makeJob(5, 10, 3),
        makeJob(3, 10, 3), makeJob(4, 15, 0), makeJob(1, 30, 2),
    };
    const int expected[6] = {3, 5, 2, 4, 6, 1};

    // Every insertion order produces the same selection sequence.
    int order[6] = {0, 1, 2, 3, 4, 5};
    int permutations = 0;
    do {
        MinHeap<EdfEntry> ready;
        for (int i : order) {
            ready.insert(edfEntryFor(jobs[i]));
        }
        for (int id : expected) {
            CHECK(ready.extractMin().jobId() == id);
        }
        CHECK(ready.isEmpty());
        ++permutations;
    } while (std::next_permutation(order, order + 6));
    CHECK(permutations == 720);

    // Bulk build agrees with incremental insertion.
    EdfEntry entries[6];
    for (int i = 0; i < 6; ++i) entries[i] = edfEntryFor(jobs[i]);
    MinHeap<EdfEntry> built(entries, 6);
    for (int id : expected) {
        CHECK(built.extractMin().jobId() == id);
    }

    // The key is copied at insertion; later Job edits cannot reorder the queue.
    MinHeap<EdfEntry> ready;
    b6::Job early = makeJob(7, 5, 0);
    b6::Job late = makeJob(8, 9, 1);
    ready.insert(edfEntryFor(early));
    ready.insert(edfEntryFor(late));
    early.absoluteDeadline = 100;
    CHECK(ready.peek().jobId() == 7);
    CHECK(ready.peek().absoluteDeadline() == 5);

    // Identity is the job ID, independent of key fields.
    CHECK(EdfEntry(1, 2, 3) == EdfEntry(9, 9, 3));
    CHECK(!(EdfEntry(1, 2, 3) < EdfEntry(1, 2, 3)));
    std::cout << "PASS: EDF ordering and deterministic ties\n";
}

// Mirrors Job::edfIndex: position per job ID, -1 when not queued.
struct EdfIndexTracker {
    int* positions = nullptr;
    void operator()(const EdfEntry& entry, int index) const { positions[entry.jobId()] = index; }
};

static void checkPositions(const MinHeap<EdfEntry, std::less<EdfEntry>, EdfIndexTracker>& heap,
                           const int* positions, int maxId) {
    int queued = 0;
    for (int id = 0; id <= maxId; ++id) {
        if (positions[id] >= 0) {
            CHECK(positions[id] < heap.size());
            CHECK(heap.at(positions[id]).jobId() == id);
            ++queued;
        }
    }
    CHECK(queued == heap.size());
}

static void testEdfIndexTracking() {
    const int maxId = 40;
    int positions[maxId + 1];
    std::fill(positions, positions + maxId + 1, -1);
    EdfIndexTracker tracker;
    tracker.positions = positions;
    MinHeap<EdfEntry, std::less<EdfEntry>, EdfIndexTracker> ready(std::less<EdfEntry>(), tracker);

    // Enough entries to force growth while positions are tracked.
    for (int id = 1; id <= maxId; ++id) {
        ready.insert(EdfEntry((id * 17) % 23, static_cast<std::uint64_t>(id), id));
        checkPositions(ready, positions, maxId);
    }

    // A job blocks: O(log n) removal through its tracked index.
    for (int id : {5, 17, 33, 1, 40}) {
        CHECK(positions[id] >= 0);
        CHECK(ready.removeAt(positions[id]));
        CHECK(positions[id] == -1);
        checkPositions(ready, positions, maxId);
    }
    CHECK(!ready.removeAt(-1));
    CHECK(!ready.removeAt(ready.size()));

    // Dispatch drains in EDF order and clears each position.
    EdfEntry previous;
    bool first = true;
    EdfEntry next;
    while (ready.tryExtractMin(next)) {
        CHECK(positions[next.jobId()] == -1);
        if (!first) CHECK(previous < next);
        previous = next;
        first = false;
        checkPositions(ready, positions, maxId);
    }
    CHECK(!ready.tryPeek(next));

    // clear() also reports removal.
    ready.insert(EdfEntry(1, 1, 2));
    ready.insert(EdfEntry(2, 2, 3));
    ready.clear();
    CHECK(positions[2] == -1 && positions[3] == -1);

    // Bulk build reports every final position.
    EdfEntry entries[4] = {EdfEntry(4, 0, 9), EdfEntry(1, 0, 8), EdfEntry(3, 0, 7), EdfEntry(2, 0, 6)};
    MinHeap<EdfEntry, std::less<EdfEntry>, EdfIndexTracker> built(entries, 4, std::less<EdfEntry>(), tracker);
    checkPositions(built, positions, maxId);
    CHECK(built.peek().jobId() == 8 && positions[8] == 0);
    std::cout << "PASS: EDF index tracking\n";
}

// A custom comparator works without an operator< on the entry type.
struct ByDescending {
    bool operator()(int a, int b) const { return a > b; }
};

static void testCustomCompare() {
    MinHeap<int, ByDescending> heap;
    for (int v : {3, 9, 1, 7}) heap.insert(v);
    CHECK(heap.extractMin() == 9);
    CHECK(heap.extractMin() == 7);
    MinHeap<int, ByDescending> copy(heap);
    CHECK(copy.extractMin() == 3);
    CHECK(heap.peek() == 3);
    std::cout << "PASS: custom comparator\n";
}

int main() {
    testOrdering();
    testDuplicates();
    testEmptyBehavior();
    testCopyAndGrowth();
    testReserveAndRemove();
    testIndependentEntryType();
    testTimerBackendApi();
    testEdfOrderingAndTies();
    testEdfIndexTracking();
    testCustomCompare();
    std::cout << "All MinHeap tests passed.\n";
    return 0;
}
