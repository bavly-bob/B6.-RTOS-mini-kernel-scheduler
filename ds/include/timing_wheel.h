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

    // Add a timer for the given process ID with the specified delay in ticks. A non-positive delay is treated as due on the next tick. O(1) allocation and link work.
    void addTimer(int processID, int delay);

    // Advance the wheel by one tick, expiring timers in the current slot. 
    // Returns an owning array of expired process IDs and sets expiredCount to the number of expired timers. 
    // The caller must delete[] the returned array. 
    // O(n) where n is the number of timers in the current slot.
    int* tick(int& expiredCount);

    // Remove a timer for the given process ID, if it exists. 
    // O(n) where n is the number of timers in the current slot.
    void removeTimer(int processID);
    int getCurrentSlot() const;

private:

    CircularList<Timer>* slots;
    int wheelSize;
    int currentSlot;
};
