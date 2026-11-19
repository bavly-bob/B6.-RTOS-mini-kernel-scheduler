#ifndef SORTED_LIST_H
#define SORTED_LIST_H

#include <cstddef>
#include <functional>

// sortedList: a singly linked list that is always kept in ascending order.
//
// Purpose in B6: the SortedList timer backend. Each element is a TimerEntry
// value ordered by (wakeTick, insertionSequence), so the head is always the
// next timer to expire. It is also usable for any value type with a strict
// weak ordering (`Compare`) and an identity test (`operator==`).
//
// Design decisions
// - Values are stored by copy inside list-owned nodes. The list never stores or
//   deletes caller pointers, so it has no ownership questions for TimerEntry.
// - Stable order: a new value is placed AFTER every existing value that is not
//   greater than it. Equal keys therefore leave in first-in, first-out order.
// - `deleteTask` finds a value by `operator==`, which may differ from the sort
//   key. TimerEntry compares by timerId, so cancellation does not need the
//   wake tick. The search is O(n); this is documented rather than hidden.
// - A tail pointer and an element count are maintained so `size()` is O(1).
// - Copying is disabled: two lists must never share (and double-delete) nodes.
//
// Costs (n = size()):
//   insertTask O(n) search + one node allocation
//   getValueAtHead, removeHead, isEmpty, size  O(1)
//   deleteTask O(n) search, O(1) unlink
//   clear / destructor O(n)
template <class Type, class Compare = std::less<Type>>
class sortedList {
private:
    struct Node {
        Type data;   // Copy of the caller's value.
        Node* next;  // Next node in ascending order, or nullptr at the tail.
    };

    Node* head = nullptr;      // Smallest value, or nullptr when empty.
    Node* tail = nullptr;      // Largest value (last node), or nullptr.
    std::size_t count = 0;     // Number of nodes, kept in step with every change.
    Compare compare;           // compare(a, b) is true when a must precede b.

public:
    sortedList();
    ~sortedList();

    sortedList(const sortedList&) = delete;
    sortedList& operator=(const sortedList&) = delete;

    // Inserts a copy of val in sorted position, after any equal keys. O(n).
    // If allocation throws, the list is unchanged.
    void insertTask(const Type& val);

    // Copies the smallest value into result. Returns false (result unchanged)
    // when the list is empty. O(1).
    bool getValueAtHead(Type& result) const;

    bool isEmpty() const;          // True when there are no nodes. O(1).
    std::size_t size() const;      // Number of stored values. O(1).

    // Removes the smallest value. Returns false when the list is empty. O(1).
    bool removeHead();

    // Removes the first value v with v == val. Returns false if none. O(n).
    bool deleteTask(const Type& val);

    // Removes every value, keeping the list usable. O(n).
    void clear();
};

// Template definitions are included relative to this header, so users only
// need ds/include on their include path (the same pattern as MinHeap.h and
// circular_list.h). The .tpp file is never compiled on its own.
#include "../src/sortedList.tpp"

#endif
