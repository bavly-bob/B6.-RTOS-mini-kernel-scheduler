# overload: demand above 100%

Plan p.16. Periodic A: C=3, T=D=4. Periodic B: C=3, T=D=5. Horizon 20.
Demand is 3/4 + 3/5 = 135%, so some deadlines must be missed.

**Releases (both policies):** A at 0, 4, 8, 12, 16 and B at 0, 5, 10, 15: nine jobs.
Total demand is 27 CPU ticks for 20 available, so the CPU is never idle and at least
7 ticks of work remain.

**Fixed priority (A above B):** A always finishes 3 ticks after its release and never
misses. B gets one tick in each of A's gaps, so each B job misses.
CPU: A0[0,3) B0[3,4) A1[4,7) B0[7,8) A2[8,11) B1[11,12) A3[12,15) B0[15,16) A4[16,19)
B2[19,20). Misses: B0 at 5, B1 at 10, B2 at 15, B3 at 20. B0 completes at 16 and keeps
its miss. B1, B2 and B3 are unfinished.

**EDF:** CPU: A0[0,3) B0[3,6) A1[6,9) B1[9,12) A2[12,15) B2[15,18) A3[18,20). Every
job except A0 misses, because lateness spreads over both tasks.

**Look for:** every release appears as its own job. Unfinished jobs show "not
completed", never the horizon as a fake completion. The backlogged B jobs share
B's ring, so B1 can run before B0 has finished.
