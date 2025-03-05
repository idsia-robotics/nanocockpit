#pragma once

#include "aideck_protocol.h"

/**
 * Send a new inference output to the Frontnet task.
 * Best effort: if the task has not processed yet the previous inference output, the new one overwrites it.
 *
 * @param inference the new inference output, in cf/base_link reference frame.
 */
void frontnetEnqueueInference(const inference_stamped_t *inference);
