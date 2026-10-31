#include "../src/ds/circular_list.h"

#include <cassert>

// Check observable order as well as every forward/backward link. The expected
// sequence is supplied independently by each test, rather than calculated from
// the list itself. Fixed iteration bounds also catch an incorrectly closed ring.
// This helper is O(n); production list operations remain O(1).
static void assertOrder(const CircularList<int>& list,
                        CircularList<int>::Node* first,
                        int* const* expected, std::size_t count) {
    assert(list.size() == count);
    assert(list.isEmpty() == (count == 0));
    if (count == 0) {
        assert(first == nullptr);
        assert(list.front() == nullptr);
        return;
    }

    assert(first != nullptr);
    assert(list.front() == expected[0]);
    CircularList<int>::Node* node = first;
    for (std::size_t index = 0; index < count; ++index) {
        assert(node != nullptr);
        assert(index == 0 || node != first);
        assert(node->getValue() == expected[index]);
        assert(node->getNext() != nullptr);
        assert(node->getPrevious() != nullptr);
        assert(node->getNext()->getPrevious() == node);
        assert(node->getPrevious()->getNext() == node);
        node = node->getNext();
    }
    assert(node == first);

    node = first->getPrevious();
    for (std::size_t index = count; index > 0; --index) {
        assert(node->getValue() == expected[index - 1]);
        node = node->getPrevious();
    }
    assert(node == first->getPrevious());
}

// Empty operations and rejected insertions must leave the list reusable.
static void testEmptyList() {
    CircularList<int> list;
    assertOrder(list, nullptr, nullptr, 0);
    assert(list.front() == nullptr);
    assert(list.popFront() == nullptr);
    assert(!list.remove(nullptr));
    assert(list.pushBack(nullptr) == nullptr);
    list.rotate();
    assertOrder(list, nullptr, nullptr, 0);
}

// A singleton links to itself in both directions, even after many rotations.
// Exercise both ways of removing its last node, then reuse the emptied list.
static void testSingleNode() {
    int value = 42;
    int* expected[] = {&value};
    CircularList<int> list;
    auto* node = list.pushBack(&value);
    assertOrder(list, node, expected, 1);
    for (int turn = 0; turn < 10; ++turn) {
        list.rotate();
        assertOrder(list, node, expected, 1);
    }
    assert(list.popFront() == &value);
    assertOrder(list, nullptr, nullptr, 0);

    node = list.pushBack(&value);
    assert(list.remove(node));
    assertOrder(list, nullptr, nullptr, 0);
    node = list.pushBack(&value);
    assertOrder(list, node, expected, 1);
}

// Remove a middle node, then the tail, then the head. Surviving handles must
// still identify their original nodes and both traversal directions must work.
static void testRemovePositions() {
    int values[] = {10, 20, 30, 40};
    CircularList<int> list;
    auto* first = list.pushBack(&values[0]);
    auto* middle = list.pushBack(&values[1]);
    auto* third = list.pushBack(&values[2]);
    auto* tail = list.pushBack(&values[3]);
    int* initial[] = {&values[0], &values[1], &values[2], &values[3]};
    assertOrder(list, first, initial, 4);

    assert(list.remove(middle));
    int* afterMiddle[] = {&values[0], &values[2], &values[3]};
    assertOrder(list, first, afterMiddle, 3);

    assert(list.remove(tail));
    int* afterTail[] = {&values[0], &values[2]};
    assertOrder(list, first, afterTail, 2);

    assert(list.remove(first));
    int* afterHead[] = {&values[2]};
    assertOrder(list, third, afterHead, 1);
    assert(list.remove(third));
    assertOrder(list, nullptr, nullptr, 0);
}

// A full lap restores the starting order. Appending after rotation must use the
// new logical tail, and removing the rotated head must expose its successor.
static void testRotationAndAppend() {
    int values[] = {1, 2, 3, 4};
    CircularList<int> list;
    auto* first = list.pushBack(&values[0]);
    auto* second = list.pushBack(&values[1]);
    auto* third = list.pushBack(&values[2]);

    list.rotate();
    int* once[] = {&values[1], &values[2], &values[0]};
    assertOrder(list, second, once, 3);
    list.rotate();
    int* twice[] = {&values[2], &values[0], &values[1]};
    assertOrder(list, third, twice, 3);
    list.rotate();
    int* fullLap[] = {&values[0], &values[1], &values[2]};
    assertOrder(list, first, fullLap, 3);

    list.rotate();
    list.pushBack(&values[3]);
    int* appended[] = {&values[1], &values[2], &values[0], &values[3]};
    assertOrder(list, second, appended, 4);
    assert(list.remove(second));
    int* removed[] = {&values[2], &values[0], &values[3]};
    assertOrder(list, third, removed, 3);
    assert(list.popFront() == &values[2]);
    assert(list.popFront() == &values[0]);
    assert(list.popFront() == &values[3]);
    assertOrder(list, nullptr, nullptr, 0);
}

// Equal pointers represent distinct entries. Removing one must preserve the
// other. Rejecting a live handle from a different list must change neither list.
static void testDuplicatesAndForeignHandles() {
    int value = 7;
    CircularList<int> list;
    CircularList<int> other;
    auto* first = list.pushBack(&value);
    auto* second = list.pushBack(&value);
    auto* foreign = other.pushBack(&value);
    assert(first != second);
    assert(!list.remove(foreign));
    assert(!list.remove(nullptr));
    assert(list.pushBack(nullptr) == nullptr);
    int* duplicate[] = {&value, &value};
    assertOrder(list, first, duplicate, 2);
    int* single[] = {&value};
    assertOrder(other, foreign, single, 1);

    assert(list.remove(first));
    assertOrder(list, second, single, 1);
    assert(list.popFront() == &value);
    assert(!list.remove(foreign));
    assertOrder(list, nullptr, nullptr, 0);
    assertOrder(other, foreign, single, 1);
}

// Repeatedly fill and drain the same list. This catches stale head pointers,
// count drift, and forgotten nodes; sanitizers also check every allocation.
static void testRepeatedPushPop() {
    int values[32]{};
    int* expected[32]{};
    CircularList<int> list;
    for (int cycle = 0; cycle < 200; ++cycle) {
        CircularList<int>::Node* first = nullptr;
        for (int index = 0; index < 32; ++index) {
            values[index] = cycle * 32 + index;
            expected[index] = &values[index];
            auto* node = list.pushBack(&values[index]);
            if (index == 0) {
                first = node;
            }
        }
        assertOrder(list, first, expected, 32);
        for (int index = 0; index < 32; ++index) {
            auto* next = index == 31 ? nullptr : first->getNext();
            assert(list.popFront() == expected[index]);
            first = next;
            assertOrder(list, first, expected + index + 1,
                        static_cast<std::size_t>(31 - index));
        }
    }
}

// A deliberately small stand-in for a caller-owned task. Its destructor count
// makes accidental deletion by popFront, remove, or list destruction visible.
struct TrackedValue {
    int* destructionCount;
    ~TrackedValue() { ++*destructionCount; }
};

// Nodes must be released, but pointed-to objects must outlive their membership.
// Leaving nodes in the list also exercises automatic cleanup on scope exit.
static void testNonOwningLifetime() {
    int destructions = 0;
    {
        TrackedValue first{&destructions};
        TrackedValue second{&destructions};
        {
            CircularList<TrackedValue> list;
            list.pushBack(&first);
            auto* node = list.pushBack(&second);
            assert(list.popFront() == &first);
            assert(list.remove(node));
            assert(destructions == 0);
            list.pushBack(&first);
            list.pushBack(&second);
        }
        assert(destructions == 0);
    }
    assert(destructions == 2);
}

// Each scenario uses assertions and no external test framework. Successful
// execution is silent; an assertion or sanitizer report identifies a failure.
int main() {
    testEmptyList();
    testSingleNode();
    testRemovePositions();
    testRotationAndAppend();
    testDuplicatesAndForeignHandles();
    testRepeatedPushPop();
    testNonOwningLifetime();
}
