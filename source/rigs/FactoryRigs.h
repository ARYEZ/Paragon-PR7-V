#pragma once

#include "Rig.h"

#include <vector>

// Starter sounds for the voice that ship with the plugin (see
// FactoryRigs.cpp for the five banks). Each sets only what differs from
// the defaults; the global knobs (INPUT, OUTPUT, LOW CUT, LIMITER, KEY)
// stay as the singer set them.
std::vector<Rig> makeFactoryRigs();
