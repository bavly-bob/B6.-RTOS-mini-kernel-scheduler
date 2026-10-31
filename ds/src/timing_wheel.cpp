#include "timing_wheel.h"

TimingWheel::TimingWheel(int size)
    : slots(nullptr),
      wheelSize(size > 0 ? size : 0),
      currentSlot(0) {
    if (wheelSize > 0) {
        slots = new CircularList<Timer>[wheelSize];
    }
}

TimingWheel::~TimingWheel() {
    if (slots != nullptr) {
        for (int i = 0; i < wheelSize; ++i) {
            Timer* timer = nullptr;
            while ((timer = slots[i].popFront()) != nullptr) {
                delete timer;
            }
        }
        delete[] slots;
    }
}

int TimingWheel::getCurrentSlot() const {
    return currentSlot;
}

void TimingWheel::addTimer(int processID, int delay) {
    // Ignore invalid wheel sizes and negative delays.
    if (wheelSize <= 0 || delay < 0) {
        return;
    }

    // tick() first advances to the next slot, 
    // so a positive delay d is reached after d calls to tick().
    int ticks = (delay == 0)? 1 : delay; // Treat zero as due on the next tick.
    
    int slotIndex = (currentSlot + (ticks % wheelSize)) % wheelSize;
    
    int rounds = (ticks - 1) / wheelSize;

    Timer* timer = new Timer{processID, rounds};
    try {
        slots[slotIndex].pushBack(timer);
    } catch (...) {
        delete timer;
        throw;
    }
}

int* TimingWheel::tick(int& expiredCount) {
    expiredCount = 0;
    if (wheelSize <= 0 || slots[currentSlot].size() == 0) {
        return nullptr;
    }

    currentSlot = (currentSlot + 1) % wheelSize;
    CircularList<Timer>& slot = slots[currentSlot];
    int timersAtStart = slot.size();

    int* expiredProcessIDs = new int[timersAtStart];
    // The existing CircularList API has no head-node accessor. Pop and reinsert
    // logical head after each processed timer; popFront returns the owned
    // Timer pointer and pushBack reuses it for timers that remain scheduled.
    for (int i = 0; i < timersAtStart; ++i) {
        Timer* timer = slot.popFront();
        if (timer == nullptr) {
            break;
        }

        if (timer->remainingRounds == 0) {
            expiredProcessIDs[expiredCount++] = timer->processID;
            delete timer;
        } else {
            --timer->remainingRounds;
            slot.pushBack(timer);
        }
    }

    if (expiredCount == 0) {
        delete[] expiredProcessIDs;
        return nullptr;
    }
    return expiredProcessIDs;
}

void TimingWheel::removeTimer(int processID) {
    if (wheelSize <= 0 || slots[currentSlot].size() == 0) {
        return;
    }

    for (int i = 0; i < wheelSize; ++i) {
        CircularList<Timer>& slot = slots[i];
        int timersAtStart = slot.size();
        for (int j = 0; j < timersAtStart; ++j) {
            Timer* timer = slot.popFront();
            if (timer == nullptr) {
                break;
            }

            if (timer->processID == processID) {
                delete timer;
            } else {
                slot.pushBack(timer);
            }
        }
    }
}
