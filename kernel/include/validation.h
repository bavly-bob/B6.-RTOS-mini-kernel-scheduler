#ifndef B6_KERNEL_VALIDATION_H
#define B6_KERNEL_VALIDATION_H

#include "types.h"

namespace b6 {
// Empty means valid; otherwise identifies the first invalid field or instruction.
std::string validateScenario(const Scenario& scenario);
} // namespace b6

#endif
