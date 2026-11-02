#include "DynamicArray.h"
#include <cassert>
#include <iostream>
#include <stdexcept>
#include <utility>

// A simple task used to test the array without the rest of the scheduler.
struct Task {
    int id;
    int priority;
};

void testGrowth() {
    DynamicArray array(0);
    Task tasks[5] = {{1, 2}, {2, 0}, {3, 1}, {4, 1}, {5, 3}};

    assert(array.empty());
    for (int i = 0; i < 5; i++) {
        array.pushBack(&tasks[i]);
    }

    assert(array.size() == 5);
    assert(array.capacity() == 8);  // 1, 2, 4, 8 as the array grows
    for (int i = 0; i < 5; i++) {
        assert(array.at(i) == &tasks[i]);
    }
    std::cout << "PASS: Adding elements and resizing\n";
}

void testInsertAndRemove() {
    DynamicArray array;
    Task a = {1, 1}, b = {2, 2}, c = {3, 3};

    array.pushBack(&a);
    array.pushBack(&c);
    array.insert(1, &b);
    assert(array.size() == 3);
    assert(array.at(1) == &b);

    array.set(1, &a);
    assert(array.at(1) == &a);
    assert(array.erase(0) == &a);
    assert(array.popBack() == &c);
    assert(array.size() == 1);
    assert(array.at(0) == &a);

    assert(array.removeFirst(&a));
    assert(!array.removeFirst(&a));
    assert(array.empty());
    std::cout << "PASS: Insert, set, and remove\n";
}

void testSearchAndClear() {
    DynamicArray array;
    Task a = {10, 1}, b = {11, 2}, missing = {12, 3};
    array.pushBack(&a);
    array.pushBack(&b);
    array.pushBack(&a);  // Duplicate pointers are allowed.
    array.pushBack(nullptr);

    assert(array.indexOf(&a) == 0);
    assert(array.contains(&b));
    assert(!array.contains(&missing));
    assert(array.indexOf(&missing) == array.size());
    assert(array.removeFirst(&a));
    assert(array.indexOf(&a) == 1);
    assert(array.removeFirst(nullptr));

    int oldCapacity = array.capacity();
    array.clear();
    assert(array.empty());
    assert(array.capacity() == oldCapacity);
    std::cout << "PASS: Search and clear\n";
}

void testCopyAndMove() {
    Task a = {21, 1}, b = {22, 2};
    DynamicArray original;
    original.reserve(20);
    original.pushBack(&a);

    DynamicArray copy(original);
    original.set(0, &b);
    assert(copy.at(0) == &a);  // Each array has its own pointer slots.

    DynamicArray assigned;
    assigned = original;
    assigned = assigned;
    assert(assigned.at(0) == &b);

    DynamicArray moved(std::move(original));
    assert(moved.at(0) == &b);
    assert(original.empty());
    original.pushBack(&a);
    assert(original.at(0) == &a);

    DynamicArray moveAssigned;
    moveAssigned = std::move(moved);
    assert(moveAssigned.at(0) == &b);
    assert(moved.empty());
    std::cout << "PASS: Copy and move\n";
}

void testInvalidIndicesAndLargeArray() {
    DynamicArray array;
    Task task = {99, 5};
    bool caught = false;

    try { array.at(0); }
    catch (const std::out_of_range&) { caught = true; }
    assert(caught);

    caught = false;
    try { array.insert(-1, &task); }
    catch (const std::out_of_range&) { caught = true; }
    assert(caught);

    caught = false;
    try { array.popBack(); }
    catch (const std::out_of_range&) { caught = true; }
    assert(caught);

    Task tasks[1000];
    for (int i = 0; i < 1000; i++) {
        tasks[i].id = i;
        array.pushBack(&tasks[i]);
    }
    assert(array.size() == 1000);
    assert(array.at(999) == &tasks[999]);
    assert(array.erase(500) == &tasks[500]);
    assert(array.at(500) == &tasks[501]);
    std::cout << "PASS: Invalid indices and 1,000 elements\n";
}

int main() {
    testGrowth();
    testInsertAndRemove();
    testSearchAndClear();
    testCopyAndMove();
    testInvalidIndicesAndLargeArray();
    std::cout << "All DynamicArray tests passed!\n";
    return 0;
}