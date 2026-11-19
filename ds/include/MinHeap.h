#ifndef MINHEAP_H
#define MINHEAP_H

#include <cstdint>
#include <functional>

// Array-backed binary min-heap, used as the Heap timer backend and the EDF
// ready queue.
//
// Requirements on T: default-constructible and copy-assignable. Ordering comes
// from Compare (default: T's operator<). remove()/deleteTask() also need
// operator== (identity, e.g. TimerEntry::timerId or EdfEntry::jobId()).
// Entries that tie under Compare come out in an unspecified (but repeatable)
// order; use a total order such as TimerEntry's (wakeTick, insertionSequence)
// or EdfEntry's (deadline, releaseSequence, jobId) when ties must be fixed.
//
// Queued entries are only exposed by const reference, so an ordering key can
// never be changed in place. To change one, remove the entry and insert anew.
//
// IndexHook (optional): called as hook(entry, index) whenever an entry lands at
// a new array slot and hook(entry, -1) when it leaves the heap. The EDF queue
// uses it to keep Job::edfIndex current so removeAt() is O(log n).
//
// Agreed costs (n = size()):
//   insert        O(log n); amortized O(1) reallocation, capacity doubles
//   peek          O(1)
//   extractMin    O(log n)
//   removeAt(i)   O(log n)
//   build (array) O(n) bottom-up heapify
//   reserve(k)    O(n) one-time copy when k > capacity(); never shrinks.
//                 After reserve(k), the first k - size() inserts do not allocate.
//   remove(v)     O(n) linear search by operator== + O(log n) repair.
//                 Prefer lazy cancellation (TimerEntry::active = false, skip on
//                 extract) or removeAt() with a tracked index on hot paths.
//   clear         O(1) without a hook, O(n) with one; keeps capacity
//   copy/assign   O(capacity) allocation + O(n) element copies. Copies do not
//                 call the hook: never copy a heap whose hook tracks positions.

struct EmptyHeapError {
    const char* message;
};

// Default hook: no position tracking.
struct NoHeapIndexHook {
    template <typename T>
    void operator()(const T&, int) const {}
};

// Immutable EDF ready-queue entry. Build it once from a released Job:
//   EdfEntry(job.absoluteDeadline, job.releaseSequence, job.jobId)
// Ordered by (absoluteDeadline, releaseSequence, jobId), a total order because
// job IDs are unique, so EDF selection never depends on insertion order or on
// pointer values. Identity (operator==) is the job ID.
class EdfEntry {
public:
    EdfEntry() = default;
    EdfEntry(std::int64_t absoluteDeadline, std::uint64_t releaseSequence, int jobId)
        : deadline_(absoluteDeadline), releaseSequence_(releaseSequence), jobId_(jobId) {}

    std::int64_t absoluteDeadline() const { return deadline_; }
    std::uint64_t releaseSequence() const { return releaseSequence_; }
    int jobId() const { return jobId_; }

    bool operator<(const EdfEntry& other) const {
        if (deadline_ != other.deadline_) return deadline_ < other.deadline_;
        if (releaseSequence_ != other.releaseSequence_)
            return releaseSequence_ < other.releaseSequence_;
        return jobId_ < other.jobId_;
    }
    bool operator==(const EdfEntry& other) const { return jobId_ == other.jobId_; }

private:
    // No setters: the key is fixed at construction. Assignment replaces a
    // whole entry (needed for heap storage) but never edits one field.
    std::int64_t deadline_ = 0;
    std::uint64_t releaseSequence_ = 0;
    int jobId_ = 0;
};

template <typename T, typename Compare = std::less<T>, typename IndexHook = NoHeapIndexHook>
class MinHeap {
public:
    explicit MinHeap(const Compare& compare = Compare(), const IndexHook& hook = IndexHook());

    // Builds a heap from `count` values in O(n). A negative count is treated as 0.
    MinHeap(const T* values, int count, const Compare& compare = Compare(),
            const IndexHook& hook = IndexHook());

    MinHeap(const MinHeap& other);
    MinHeap& operator=(const MinHeap& other);
    ~MinHeap();

    void insert(const T& value);

    // Throws EmptyHeapError on an empty heap.
    const T& peek() const;

    // Throws EmptyHeapError on an empty heap.
    T extractMin();

    // Non-throwing variants. Return false (and leave `out` unchanged) when empty.
    bool tryPeek(T& out) const;
    bool tryExtractMin(T& out);

    // Removes the first entry equal (operator==) to `value`. Returns false if absent.
    bool remove(const T& value);

    // Removes the entry at array slot `index` (as reported by IndexHook).
    // Returns false if index is out of range.
    bool removeAt(int index);

    // Read-only slot access for index validation; index must be in [0, size()).
    const T& at(int index) const;

    // Ensures capacity() >= requestedCapacity. Never shrinks.
    void reserve(int requestedCapacity);

    bool isEmpty() const;
    int size() const;
    int capacity() const;
    void clear();

    // Same names and results as sortedList, so a TimerManager can drive either
    // TimerBackend through one template without branching per backend.
    void insertTask(const T& value) { insert(value); }
    bool getValueAtHead(T& result) const { return tryPeek(result); }
    bool removeHead() {
        if (count == 0) return false;
        removeAt(0);
        return true;
    }
    bool deleteTask(const T& value) { return remove(value); }

private:
    static const int INITIAL_CAPACITY = 8;

    T* data;
    int count;
    int cap;
    Compare less;
    IndexHook hook;

    static int parent(int i);
    static int left(int i);
    static int right(int i);

    void swapSlots(int a, int b);
    void reallocate(int newCapacity);
    void grow();
    void siftUp(int i);
    void siftDown(int i);
};

// Template definitions live in ds/src; include them relative to this header so
// users only need ds/include on their include path.
#include "../src/MinHeap.cpp"

#endif // MINHEAP_H
