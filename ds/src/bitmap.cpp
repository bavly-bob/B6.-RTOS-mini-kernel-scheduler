#include "../include/bitmap.h"

PriorityBitmap::PriorityBitmap() : bits_(0U) {}

bool PriorityBitmap::set(int index) {
    if (!isValidIndex(index)) {
        return false;
    }
    bits_ |= (std::uint32_t{1} << index);
    return true;
}

bool PriorityBitmap::clear(int index) {
    if (!isValidIndex(index)) {
        return false;
    }
    bits_ &= ~(std::uint32_t{1} << index);
    return true;
}

bool PriorityBitmap::test(int index) const {
    return isValidIndex(index) && (bits_ & (std::uint32_t{1} << index)) != 0U;
}

int PriorityBitmap::highestSetBit() const {
    // At most 32 checks, so this remains O(1).
    for (int index = kPriorityLevels - 1; index >= 0; --index) {
        if (test(index)) {
            return index;
        }
    }
    return -1;
}

bool PriorityBitmap::isEmpty() const {
    return bits_ == 0U;
}

bool PriorityBitmap::isValidIndex(int index) {
    return index >= 0 && index < kPriorityLevels;
}
