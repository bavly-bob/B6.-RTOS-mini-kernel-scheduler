#include <iostream>
#include <cassert>
#include "sortedList.h"

using namespace std;

int main() {
    sortedList<int> list;
    int value;

    // Test 1: Empty list
    assert(list.isEmpty());

    // Test 2: Insert values in random order
    list.insertTask(30);
    list.insertTask(10);
    list.insertTask(20);

    assert(!list.isEmpty());

    // Test 3: Check ascending order
    list.getValueAtHead(value);
    assert(value == 10);

    list.removeHead();
    list.getValueAtHead(value);
    assert(value == 20);

    // Test 4: Delete a specific value
    assert(list.deleteTask(30));

    // Test 5: Remove the remaining value
    list.removeHead();
    assert(list.isEmpty());

    // Test 6: Operations on an empty list
    assert(!list.removeHead());
    assert(!list.deleteTask(50));
    assert(!list.getValueAtHead(value));

    cout << "All tests passed!" << endl;

    return 0;
}