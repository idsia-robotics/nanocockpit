#include "frontnet_state_fwd.h"

#include "frontnet_config.h"
#include "frontnet_rng.h"
#include "aideck_protocol.h"

#define DEBUG_MODULE "FN-STATE-FWD"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "app.h"
#include "debug.h"
#include "led.h"
#include "log.h"
#include "param.h"
#include "static_mem.h"
#include "system.h"

#include <stdint.h>

static bool isInit = false;

STATIC_MEM_TASK_ALLOC(fwdTask, STATE_FWD_STACKSIZE);

static uint32_t lastForwardTime = 0;
static stateCompressed_t state;

static QueueHandle_t stateQueue;
STATIC_MEM_QUEUE_ALLOC(stateQueue, STATE_FWD_HISTORY_COUNT, sizeof(stateCompressed_t));

static void forwardState(const stateCompressed_t *state) {
  state_msg_t msg = {
    .timestamp = state->timestamp,

    .x = state->x,
    .y = state->y,
    .z = state->z,

    .vx = state->vx,
    .vy = state->vy,
    .vz = state->vz,

    .ax = state->ax,
    .ay = state->ay,
    .az = state->az,

    .quat = state->quat,

    .rateRoll = state->rateRoll,
    .ratePitch = state->ratePitch,
    .rateYaw = state->rateYaw,
  };
  send_state_msg(&msg);
}

static void forwardRNGEntropy(uint32_t entropy) {
  rng_msg_t msg = {
    .entropy = entropy
  };
  send_rng_msg(&msg);
}

static void enqueueStateHistory(const stateCompressed_t *state) {
  bool stateSent = xQueueSend(stateQueue, state, 0);

  if (!stateSent) {
    // Queue is full, discard one element at the beginning to make space
    stateCompressed_t discard;
    xQueueReceive(stateQueue, &discard, 0);

    xQueueSend(stateQueue, state, 0);
  }
}

bool stateFwdDequeueAtTimestamp(uint32_t timestamp, stateCompressed_t *state) {
  bool stateReceived = false;
  
  ASSERT(isInit);
  
  // Discard all elements at the front of the queue older than the desired timestamp
  while ((stateReceived = xQueuePeek(stateQueue, state, 0)) && state->timestamp < timestamp) {
    stateCompressed_t discard;
    xQueueReceive(stateQueue, &discard, 0);

    // There is a race condition between the Peek/Receive pair here and Send/Receive
    // in enqueueStateHistory. In pratice, it should almost never be a problem.
    // - The expected case is that Peek and Receive return the same state, which we 
    //   want to discard anyway.
    // - Otherwise, even if Receive returns a newer state than Peek, it is expected 
    //   that the discarded state is still older than the desired timestamp.
    // - FRONTNET_STATE_HISTORY_COUNT can be tuned so that this property holds.
    ASSERT(discard.timestamp == state->timestamp || discard.timestamp < timestamp);
  }

  // State queue has been emptied, try to get the latest state
  // (added because frontnet_test_inferences can fetch a new state before it is added to the queue)
  if (!stateReceived) {
    stabilizerGetLatestState(state);
  }

  // Return true if the element now at the front has the desired timestamp
  if (state->timestamp == timestamp) {
    return true;
  } else {
    return false;
  }
}

static void fwdTask(void *_param) {
  systemWaitStart();

  lastForwardTime = xTaskGetTickCount();
  while (true) {
    stabilizerGetLatestState(&state);
    enqueueStateHistory(&state);
    forwardState(&state);

    uint32_t rngEntropy;
    if (frontnetRNGGetRandomU32(&rngEntropy)) {
      forwardRNGEntropy(rngEntropy);
    }
    
    uint32_t deadline = lastForwardTime + F2T(STATE_FWD_RATE);
    uint32_t now = xTaskGetTickCount();
    if (deadline < now) {
      DEBUG_PRINT("Missed state forward deadline by %ld ms\n", now - deadline);
      lastForwardTime = now;
    }

    vTaskDelayUntil(&lastForwardTime, F2T(STATE_FWD_RATE));
  }
}

void stateFwdInit() {
  if (isInit) {
    return;
  }

  stateQueue = STATIC_MEM_QUEUE_CREATE(stateQueue);
  ASSERT(stateQueue);

  frontnetRNGInit();

  STATIC_MEM_TASK_CREATE(fwdTask, fwdTask, STATE_FWD_TASK_NAME, NULL, STATE_FWD_PRIORITY);
  isInit = true;

  DEBUG_PRINT("State forwarding started\n");
}
