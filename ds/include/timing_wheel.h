#pragma once

#include "circular_list.h"

/*
 * A timing wheel is a circular buffer of slots, each of which can hold multiple timers.
 * Timers are stored in per-slot CircularList<Timer> instances. tick() returns
 * an owning array of expired process IDs; the caller must delete[] it.
 */
class TimingWheel {
public:
    // A timer is a process ID and the number of remaining rounds before it expires.
    struct Timer {
        int processID;
        int remainingRounds;
    };

    // Create a timing wheel with the given number of slots. A non-positive size creates an empty wheel that cannot store timers. O(n) allocation.
    TimingWheel(int size);
    ~TimingWheel();

    // Each timing wheel is a unique owner of its slots and timers. Disabling copying and moving avoids shared ownership, double deletion, and invalid owner information.
    TimingWheel(const TimingWheel&) = delete;
    TimingWheel& operator=(const TimingWheel&) = delete;
    TimingWheel(TimingWheel&&) = delete;
    TimingWheel& operator=(TimingWheel&&) = delete;

    // Add a timer for the given process ID with the specified delay in ticks. Zero is due on the next tick; negative delays are ignored. O(1) allocation and link work.
    void addTimer(int processID, int delay);

    // Advance the wheel by one slot (always, even if no timers exist), expiring due timers in the new slot.
    // Returns an owning array of expired process IDs and sets expiredCount to the number of expired timers. 
    // The caller must delete[] the returned array. 
    // O(n) where n is the number of timers in the new slot, including those waiting more rounds.
    int* tick(int& expiredCount);

    // Remove every timer for the given process ID, if any, from all slots.
    // O(W + N) where W is the wheel size and N is the number of stored timers.
    void removeTimer(int processID);
    int getCurrentSlot() const;

private:

    CircularList<Timer>* slots;
    int wheelSize;
    int currentSlot;
};
