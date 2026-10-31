#include "../ds/include/timing_wheel.h"

#include <cassert>

void assertTick(TimingWheel& wheel,
                       const int* expectedIDs,
                       int expectedCount) {
    int expiredCount = -1;
    int* expiredIDs = wheel.tick(expiredCount);

    assert(expiredCount == expectedCount);
    assert((expiredIDs != nullptr) == (expectedCount > 0));
    for (int i = 0; i < expectedCount; ++i) {
        assert(expiredIDs[i] == expectedIDs[i]);
    }

    delete[] expiredIDs;
}

void testDelaysAndSlotWrap() {
    TimingWheel wheel(4);

    wheel.addTimer(10, 1);
    wheel.addTimer(20, 4);
    wheel.addTimer(30, 5);
    wheel.addTimer(40, 0);  // Zero means due on the next tick.
    wheel.addTimer(50, -1); // Negative delays are ignored.

    const int firstTick[] = {10, 40};
    assertTick(wheel, firstTick, 2);
    assert(wheel.getCurrentSlot() == 1);

    assertTick(wheel, nullptr, 0);
    assertTick(wheel, nullptr, 0);

    const int fourthTick[] = {20};
    assertTick(wheel, fourthTick, 1);

    const int fifthTick[] = {30};
    assertTick(wheel, fifthTick, 1);
    assert(wheel.getCurrentSlot() == 1);
}

void testLongDelayRounds() {
    TimingWheel wheel(3);
    wheel.addTimer(77, 8);

    for (int tick = 0; tick < 7; ++tick) {
        assertTick(wheel, nullptr, 0);
    }

    const int eighthTick[] = {77};
    assertTick(wheel, eighthTick, 1);
}

void testRemoveHeadMiddleAndTail() {
    TimingWheel wheel(5);

    // All timers occupy one slot, allowing removals from different positions.
    wheel.addTimer(1, 2);
    wheel.addTimer(2, 2);
    wheel.addTimer(3, 2);
    wheel.addTimer(4, 2);
    wheel.removeTimer(2);   // Middle.
    wheel.removeTimer(4);   // Tail.
    wheel.removeTimer(1);   // Head.
    wheel.removeTimer(999); // Missing IDs are harmless.

    assertTick(wheel, nullptr, 0);
    const int secondTick[] = {3};
    assertTick(wheel, secondTick, 1);
}

void testInvalidWheelSize() {
    TimingWheel zeroWheel(0);
    zeroWheel.addTimer(1, 1);
    assertTick(zeroWheel, nullptr, 0);
    zeroWheel.removeTimer(1);
    assert(zeroWheel.getCurrentSlot() == 0);

    TimingWheel negativeWheel(-4);
    negativeWheel.addTimer(2, 1);
    assertTick(negativeWheel, nullptr, 0);
    assert(negativeWheel.getCurrentSlot() == 0);
}

void test_timing_wheel() {
    testDelaysAndSlotWrap();
    testLongDelayRounds();
    testRemoveHeadMiddleAndTail();
    testInvalidWheelSize();
}
