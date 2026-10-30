#include "../src/ds/bitmap.h"

#include <cassert>

/** Tests normal bitmap use, the required boundary bits, and invalid indices. */
int main() {
    PriorityBitmap bitmap;

    // An unused scheduler has no ready priority and no highest priority.
    assert(bitmap.isEmpty());
    assert(bitmap.highestSetBit() == -1);

    // The two boundary bits are valid and the higher priority wins.
    assert(bitmap.set(0));
    assert(bitmap.set(31));
    assert(!bitmap.isEmpty());
    assert(bitmap.test(0));
    assert(bitmap.test(31));
    assert(bitmap.highestSetBit() == 31);

    // Setting the same priority again must not affect the result.
    assert(bitmap.set(31));
    assert(bitmap.highestSetBit() == 31);

    // Clearing the highest bit exposes the remaining lower priority.
    assert(bitmap.clear(31));
    assert(!bitmap.test(31));
    assert(bitmap.highestSetBit() == 0);
    assert(bitmap.clear(0));
    assert(bitmap.isEmpty());

    // Invalid indices do not shift by an invalid amount or change the bitmap.
    assert(!bitmap.set(-1));
    assert(!bitmap.set(32));
    assert(!bitmap.clear(-1));
    assert(!bitmap.clear(32));
    assert(!bitmap.test(-1));
    assert(!bitmap.test(32));
    assert(bitmap.isEmpty());
}
