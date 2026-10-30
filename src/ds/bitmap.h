#ifndef B6_SCHEDULER_DS_BITMAP_H
#define B6_SCHEDULER_DS_BITMAP_H

#include <cstdint>

// Tracks the 32 ready-queue priority levels
class PriorityBitmap {
public:
    // Valid priority indices 0 -> 31.
    static constexpr int kPriorityLevels = 32;

    PriorityBitmap() : bits_(0U) {}

    // Sets a priority bit; returns false for an invalid index. O(1).
    bool set(int index) {
        if (!isValidIndex(index)) {
            return false;
        }

        bits_ |= (std::uint32_t{1} << index);
        return true;
    }

    // Clears a priority bit; returns false for an invalid index. O(1).
    bool clear(int index) {
        if (!isValidIndex(index)) {
            return false;
        }

        bits_ &= ~(std::uint32_t{1} << index);
        return true;
    }

    // Returns whether a priority bit is set. Invalid indices return false.
    bool test(int index) const {
        return isValidIndex(index) && (bits_ & (std::uint32_t{1} << index)) != 0U;
    }

    // Returns the highest set priority, O(1).
    int highestSetBit() const {
        // At most 32 checks, so this remains O(1).
        for (int index = kPriorityLevels - 1; index >= 0; --index) {
            if (test(index)) {
                return index;
            }
        }
        return -1;
    }

    // Returns true when no priority is set.
    bool isEmpty() const {
        return bits_ == 0U;
    }

private:
    // Bit 31 represents the highest priority.
    std::uint32_t bits_;

    // Prevents invalid bit shifts.
    static bool isValidIndex(int index) {
        return index >= 0 && index < kPriorityLevels;
    }
};

#endif  // B6_SCHEDULER_DS_BITMAP_H
