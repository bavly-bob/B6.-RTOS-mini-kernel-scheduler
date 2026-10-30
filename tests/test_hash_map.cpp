#include "../src/ds/hash_map.h"

#include <cassert>

// Finds three distinct keys that begin in one chain of an eight-bucket map. 
static void findCollidingKeys(int& first, int& second, int& third) {
    first = 0;
    second = 0;
    third = 0;
    const std::size_t targetBucket = HashMap<int>::hash(0) % 8;

    for (int key = 0; third == 0; ++key) {
        if (HashMap<int>::hash(key) % 8 != targetBucket) {
            continue;
        }
        if (first == 0 && key != 0) {
            first = key;
        } else if (second == 0 && key != 0) {
            second = key;
        } else if (third == 0 && key != 0) {
            third = key;
        }
    }
}

// Tests lookup, collisions, removal, update, resizing, and a missing key. 
int main() {
    HashMap<int> map;
    int firstValue = 10;
    int secondValue = 20;
    int thirdValue = 30;
    int replacementValue = 40;

    assert(map.size() == 0);
    assert(!map.contains(999));
    assert(map.get(999) == nullptr);
    assert(!map.remove(999));

    int firstKey;
    int secondKey;
    int thirdKey;
    findCollidingKeys(firstKey, secondKey, thirdKey);

    // The three keys share one original chain; remove one from its middle.
    assert(map.put(firstKey, &firstValue));
    assert(map.put(secondKey, &secondValue));
    assert(map.put(thirdKey, &thirdValue));
    assert(map.size() == 3);
    assert(map.remove(secondKey));
    assert(map.get(firstKey) == &firstValue);
    assert(map.get(secondKey) == nullptr);
    assert(map.get(thirdKey) == &thirdValue);

    // Updating an existing key must not create another node.
    assert(!map.put(firstKey, &replacementValue));
    assert(map.size() == 2);
    assert(map.get(firstKey) == &replacementValue);

    // Inserting past 75 percent load forces a resize and preserves every key.
    int values[40]{};
    for (int index = 0; index < 40; ++index) {
        values[index] = index;
        assert(map.put(index + 100, &values[index]));
    }
    assert(map.bucketCount() > 8);
    for (int index = 0; index < 40; ++index) {
        assert(map.contains(index + 100));
        assert(*map.get(index + 100) == index);
    }
}
