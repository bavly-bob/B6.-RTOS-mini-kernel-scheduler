// Independent tests for sortedList. Checks stay active even with NDEBUG.
#include "sortedList.h"

#include <cstdio>
#include <cstdlib>
#include <iostream>

#define CHECK(condition) do { if (!(condition)) { \
    std::fprintf(stderr, "CHECK failed: %s (%s:%d)\n", #condition, __FILE__, __LINE__); \
    std::exit(1); } } while (0)

// Ordered by key; identity (operator==) is the tag, like TimerEntry's timerId.
struct Item {
    int key = 0;
    int tag = 0;
    bool operator<(const Item& other) const { return key < other.key; }
    bool operator==(const Item& other) const { return tag == other.tag; }
};

static Item popHead(sortedList<Item>& list) {
    Item item;
    CHECK(list.getValueAtHead(item));
    CHECK(list.removeHead());
    return item;
}

static void testEmpty() {
    sortedList<int> list;
    int value = 7;
    CHECK(list.isEmpty());
    CHECK(list.size() == 0);
    CHECK(!list.getValueAtHead(value));
    CHECK(value == 7);  // Unchanged on failure.
    CHECK(!list.removeHead());
    CHECK(!list.deleteTask(50));
    std::cout << "PASS: empty list\n";
}

static void testAscendingOrder() {
    sortedList<int> list;
    list.insertTask(30);  // Empty insert.
    list.insertTask(10);  // New head.
    list.insertTask(40);  // New tail (shortcut).
    list.insertTask(20);  // Middle.
    CHECK(list.size() == 4);
    int expected[] = {10, 20, 30, 40};
    for (int e : expected) {
        int value = 0;
        CHECK(list.getValueAtHead(value));
        CHECK(value == e);
        CHECK(list.removeHead());
    }
    CHECK(list.isEmpty());
    std::cout << "PASS: head, middle and tail inserts\n";
}

static void testEqualKeysKeepArrivalOrder() {
    sortedList<Item> list;
    list.insertTask({5, 1});
    list.insertTask({5, 2});
    list.insertTask({1, 3});
    list.insertTask({5, 4});
    list.insertTask({3, 5});
    list.insertTask({5, 6});
    int expectedTags[] = {3, 5, 1, 2, 4, 6};
    for (int tag : expectedTags) CHECK(popHead(list).tag == tag);
    std::cout << "PASS: equal keys are FIFO\n";
}

static void testDeleteByIdentity() {
    sortedList<Item> list;
    for (int i = 1; i <= 5; ++i) list.insertTask({i * 10, i});
    // Search uses operator== (tag), not the key.
    CHECK(list.deleteTask({0, 3}));   // Middle.
    CHECK(list.deleteTask({0, 5}));   // Tail.
    CHECK(list.deleteTask({0, 1}));   // Head.
    CHECK(!list.deleteTask({0, 9}));  // Missing.
    CHECK(list.size() == 2);
    // The tail pointer must be correct after deleting the old tail.
    list.insertTask({100, 6});
    CHECK(popHead(list).tag == 2);
    CHECK(popHead(list).tag == 4);
    CHECK(popHead(list).tag == 6);
    CHECK(list.isEmpty());

    sortedList<int> only;
    only.insertTask(1);
    CHECK(only.deleteTask(1));
    CHECK(only.isEmpty() && only.size() == 0);
    only.insertTask(2);  // Usable again with a fresh head and tail.
    int value = 0;
    CHECK(only.getValueAtHead(value) && value == 2);
    std::cout << "PASS: delete head, middle, tail, missing and only element\n";
}

static void testClearAndReuse() {
    sortedList<int> list;
    for (int i = 0; i < 100; ++i) list.insertTask(100 - i);
    CHECK(list.size() == 100);
    list.clear();
    CHECK(list.isEmpty() && list.size() == 0);
    list.insertTask(3);
    list.insertTask(1);
    int value = 0;
    CHECK(list.getValueAtHead(value) && value == 1);
    std::cout << "PASS: clear and reuse\n";
}

// Deterministic pseudo-random operations checked against a plain array.
static void testAgainstReference() {
    sortedList<Item> list;
    Item reference[512];
    int count = 0;
    unsigned seed = 12345;
    auto next = [&seed]() { seed = seed * 1103515245u + 12345u; return (seed >> 16) & 0x7fff; };
    int nextTag = 1;
    for (int step = 0; step < 4000; ++step) {
        unsigned choice = next() % 4;
        if ((choice < 2 && count < 512) || count == 0) {
            Item item{static_cast<int>(next() % 20), nextTag++};
            list.insertTask(item);
            int pos = count;  // Stable insert into the reference array.
            while (pos > 0 && item.key < reference[pos - 1].key) {
                reference[pos] = reference[pos - 1];
                --pos;
            }
            reference[pos] = item;
            ++count;
        } else if (choice == 2) {
            CHECK(popHead(list).tag == reference[0].tag);
            for (int i = 1; i < count; ++i) reference[i - 1] = reference[i];
            --count;
        } else {
            int victim = static_cast<int>(next() % count);
            CHECK(list.deleteTask(reference[victim]));
            for (int i = victim + 1; i < count; ++i) reference[i - 1] = reference[i];
            --count;
        }
        CHECK(list.size() == static_cast<std::size_t>(count));
    }
    for (int i = 0; i < count; ++i) CHECK(popHead(list).tag == reference[i].tag);
    CHECK(list.isEmpty());
    std::cout << "PASS: 4,000 mixed operations match a reference array\n";
}

int main() {
    testEmpty();
    testAscendingOrder();
    testEqualKeysKeepArrivalOrder();
    testDeleteByIdentity();
    testClearAndReuse();
    testAgainstReference();
    std::cout << "All sortedList tests passed!\n";
    return 0;
}
