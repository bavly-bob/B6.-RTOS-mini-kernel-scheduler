#ifndef B6_APP_PRESETS_H
#define B6_APP_PRESETS_H

#include "simulation.h"

#include <string>

namespace b6 {

// A named, deterministic demonstration scenario (plan p.14). Each preset has
// an independent expected result in docs/scheduler.md and tests/test_scheduler.cpp.
struct Preset {
    const char* id;
    const char* description;
    void (*build)(ScenarioInput& input);
};

std::size_t presetCount();
const Preset& presetAt(std::size_t index);
const Preset* findPreset(const std::string& id);  // nullptr if unknown.

} // namespace b6
#endif
