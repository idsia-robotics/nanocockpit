/*
 * frontnet_test_inferences.c
 * Elia Cereda <elia.cereda@idsia.ch>
 *
 * Copyright (C) 2022-2025 IDSIA, USI-SUPSI
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 * 
 * This software is based on the following publication:
 *    E. Cereda, A. Giusti, D. Palossi. "NanoCockpit: Performance-optimized 
 *    Application Framework for AI-based Autonomous Nanorobotics"
 * We kindly ask for a citation if you use in academic work.
 */

#include "frontnet_config.h"
#include "frontnet_inference.h"

#define DEBUG_MODULE "FN-TEST"

#include "FreeRTOS.h"
#include "static_mem.h"
#include "system.h"
#include "task.h"
#include "timers.h"

#include "debug.h"
#include "math3d.h"
#include "param.h"
#include "stabilizer.h"

#define TEST_TASK_NAME "FN-TEST"
#define TEST_PRIORITY FRONTNET_PRIORITY
#define TEST_STACK_SIZE configMINIMAL_STACK_SIZE

#define TEST_TIMER_RATE 20
#define TEST_INFERENCE_RATE 0.5
#define TEST_TASK_DELAY M2T(500)

#define TEST_BASE_HORIZONTAL_DISTANCE 1.3f

static bool isInit = false;

static TimerHandle_t timer;
STATIC_MEM_TIMER_ALLOC(timer);

STATIC_MEM_TASK_ALLOC(testTask, TEST_STACK_SIZE);

static bool enableTest = false;
static bool testEnabled = false;

static uint32_t lastInference = 0;
static int currentInference = 0;
static inference_stamped_t inferences[] = {
  {0, TEST_BASE_HORIZONTAL_DISTANCE + 0.0f,  0.0f,  0.0f, 0.0f},
  {0, TEST_BASE_HORIZONTAL_DISTANCE - 0.5f,  0.0f,  0.0f, 0.0f},
  {0, TEST_BASE_HORIZONTAL_DISTANCE + 0.0f,  0.0f,  0.0f, 0.0f},
  {0, TEST_BASE_HORIZONTAL_DISTANCE + 0.5f,  0.0f,  0.0f, 0.0f},

  {0, TEST_BASE_HORIZONTAL_DISTANCE + 0.0f, +1.0f,  0.0f, 0.0f},
  {0, TEST_BASE_HORIZONTAL_DISTANCE + 0.0f,  0.0f,  0.0f, 0.0f},
  {0, TEST_BASE_HORIZONTAL_DISTANCE + 0.0f, -1.0f,  0.0f, 0.0f},

  {0, TEST_BASE_HORIZONTAL_DISTANCE + 0.0f,  0.0f, +0.5f, 0.0f},
  {0, TEST_BASE_HORIZONTAL_DISTANCE + 0.0f,  0.0f,  0.0f, 0.0f},
  {0, TEST_BASE_HORIZONTAL_DISTANCE + 0.0f,  0.0f, -0.5f, 0.0f},

  {0, TEST_BASE_HORIZONTAL_DISTANCE + 0.0f,  0.0f,  0.0f, +M_PI_F/4},
  {0, TEST_BASE_HORIZONTAL_DISTANCE + 0.0f,  0.0f,  0.0f, 0.0f},
  {0, TEST_BASE_HORIZONTAL_DISTANCE + 0.0f,  0.0f,  0.0f, -M_PI_F/4},
};

static inference_stamped_t inferenceOdomToBase(const inference_stamped_t *inferenceOdom, const state_t *state) {
  float statePhi = radians(state->attitude.yaw);
  float sn = sinf(statePhi);
  float cs = cosf(statePhi);

  inference_stamped_t inferenceBase = {
    .stm32_timestamp = state->position.timestamp,
    .x =  cs * inferenceOdom->x + sn * inferenceOdom->y - cs * state->position.x - sn * state->position.y,
    .y = -sn * inferenceOdom->x + cs * inferenceOdom->y + sn * state->position.x - cs * state->position.y,
    .z = inferenceOdom->z - state->position.z,
    .phi = inferenceOdom->phi - statePhi
  };

  return inferenceBase;
}

static void timerCallback() {
  stateCompressed_t stateCompressed;
  state_t state;
  stabilizerGetLatestState(&stateCompressed);
  stabilizerDecompressState(&stateCompressed, &state);

  inference_stamped_t *inferenceOdom = &inferences[currentInference];
  inference_stamped_t inferenceBase = inferenceOdomToBase(inferenceOdom, &state);
  frontnetEnqueueInference(&inferenceBase);

  uint32_t timestamp = xTaskGetTickCount();
  if ((timestamp - lastInference) > F2T(TEST_INFERENCE_RATE)) {
    currentInference = (currentInference + 1) % (sizeof(inferences) / sizeof(inference_stamped_t));
    lastInference = timestamp;
  }
}

static void testTask() {
  systemWaitStart();

  while (true) {
    if (enableTest && !testEnabled) {
      lastInference = xTaskGetTickCount();
      currentInference = 0;
      xTimerStart(timer, 0);
      testEnabled = true;
    }

    if (!enableTest && testEnabled) {
      xTimerStop(timer, 0);
      testEnabled = false;
    }

    vTaskDelay(TEST_TASK_DELAY);
  }
}

void frontnetTestInferencesInit() {
  if (isInit) {
    return;
  }

  timer = STATIC_MEM_TIMER_CREATE(timer, "frontnetTestTimer", F2T(TEST_TIMER_RATE), pdTRUE, NULL, timerCallback);
  ASSERT(timer);
  
  STATIC_MEM_TASK_CREATE(testTask, testTask, TEST_TASK_NAME, NULL, TEST_PRIORITY);
  isInit = true;
}

PARAM_GROUP_START(frontnet_test)
  // When enabled, produce a pre-defined test sequence of inferences in loop.
  // TODO: after upgrading to crazyflie-firmware ≥2022.01, use PARAM_ADD_WITH_CALLBACK and do away with testTask.
  PARAM_ADD(PARAM_UINT8, enable, &enableTest)
PARAM_GROUP_STOP(frontnet_test)
