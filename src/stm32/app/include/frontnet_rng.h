#pragma once

#include <stdbool.h>
#include <stdint.h>

void frontnetRNGInit();
bool frontnetRNGGetRandomU32(uint32_t *value);
