#include "frontnet_inference.h"

#define DEBUG_MODULE "FN-AIDECK"

#include "debug.h"

#include <string.h>

// Take the inference output received from aideck_protocol on the aideck task and forward it to frontnet
// TODO: replace this with CPX
void inference_stamped_callback(inference_stamped_t *inference) {
  frontnetEnqueueInference(inference);
}
