#pragma once

#include "stabilizer.h"

void stateFwdInit();
bool stateFwdDequeueAtTimestamp(uint32_t timestamp, stateCompressed_t *state);
