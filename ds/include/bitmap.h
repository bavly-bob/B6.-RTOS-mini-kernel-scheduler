#ifndef B6_SCHEDULER_DS_BITMAP_H
#define B6_SCHEDULER_DS_BITMAP_H

#include <cstdint>

// Tracks the 32 ready-queue priority levels
class PriorityBitmap {
public:
    // Valid priority indices 0 -> 31.
    static constexpr int kPriorityLevels = 32;

    PriorityBitmap();

    // Sets a priority bit; returns false for an invalid index. O(1).
    bool set(int index);

    // Clears a priority bit; returns false for an invalid index. O(1).
    bool clear(int index);

    // Returns whether a priority bit is set. Invalid indices return false.
    bool test(int index) const;

    // Returns the highest set priority, O(1).
    int highestSetBit() const;

    // Returns true when no priority is set.
    bool isEmpty() const;

private:
    // Bit 31 represents the highest priority.
    std::uint32_t bits_;

    // Prevents invalid bit shifts.
    static bool isValidIndex(int index);
};

#endif  // B6_SCHEDULER_DS_BITMAP_H
