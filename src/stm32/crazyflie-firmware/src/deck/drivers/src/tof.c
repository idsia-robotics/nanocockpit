/*
 * tof.h
 * Elia Cereda <elia.cereda@idsia.ch>
 * Luca Crupi <luca.crupi@idsia.ch>
 * Gabriele Abbate <gabriele.abbate@idsia.ch>
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
 */

#define DEBUG_MODULE "TOF"

#include "tof.h"

#include "aideck_protocol.h"

#include "FreeRTOS.h"

#include "debug.h"
#include "deck.h"
#include "param.h"
#include "static_mem.h"
#include "system.h"
#include "i2cdev.h"

#include "vl53l5cx_api.h"

// #define SEND_TO_CONSOLE

static VL53L5CX_Configuration 	dev = {
  .platform = {
    .address = VL53L5CX_DEFAULT_I2C_ADDRESS,
    .I2Cx    = I2C1_DEV
  }
};
static VL53L5CX_ResultsData 	  results;
static uint8_t isAlive, resolution;
static uint8_t dataReady;
static bool isInit;
static tof_msg_t tof;

static void tofTask(void* arg);
STATIC_MEM_TASK_ALLOC(tofTask, TOF_DECK_TASK_STACKSIZE);

void tofInit(DeckInfo* info) {
  if (isInit)
    return;

  int status = 0;
  status |= vl53l5cx_is_alive(&dev, &isAlive);
  if (!isAlive) {
    DEBUG_PRINT("VL53L5CXV0 not detected at requested address (0x%x)\n", dev.platform.address);
	}

  DEBUG_PRINT("Sensor initializing, please wait few seconds\n");
  status |= vl53l5cx_init(&dev);
  
  ASSERT(status == 0);

  STATIC_MEM_TASK_CREATE(tofTask, tofTask, TOF_DECK_TASK_NAME, NULL, TOF_DECK_TASK_PRI);

  isInit = true;
}

bool tofTest(void) {
  if (!isInit)
    return false;

  int status = vl53l5cx_is_alive(&dev, &isAlive);
  ASSERT(status == 0);

  return isAlive;
}

static void tofTask(void* arg) {
  systemWaitStart();

  int status = 0;
  status |= vl53l5cx_set_resolution(&dev, VL53L5CX_RESOLUTION_8X8);
  status |= vl53l5cx_set_ranging_frequency_hz(&dev, 15);
  status |= vl53l5cx_set_ranging_mode(&dev, VL53L5CX_RANGING_MODE_CONTINUOUS);
  status |= vl53l5cx_start_ranging(&dev);
  ASSERT(status == 0);

  status = vl53l5cx_get_resolution(&dev, &resolution);
  ASSERT(status == 0);
  tof.resolution = resolution;

  TickType_t xLastWakeTime = xTaskGetTickCount();
  while (1) {
    vTaskDelayUntil(&xLastWakeTime, M2T(10));

    status = vl53l5cx_check_data_ready(&dev, &dataReady);
    ASSERT(status == 0);

		if (!dataReady) {
      continue;
    }

    status = vl53l5cx_get_ranging_data(&dev, &results);
    ASSERT(status == 0);

    #ifdef SEND_TO_CONSOLE
      consolePrintf("TOF_%2u:", resolution);
      for (int i = 0; i < resolution; i++) {
        if (results.nb_target_detected[i] > 0 && (results.target_status[i] == 9 || results.target_status[i] == 5 || results.target_status[i] == 6))
          consolePrintf("%d,", results.distance_mm[i]);
        else
          consolePrintf("%d,", 4000);
      }
      consolePrintf("\n");
    #else
      for(int i = 0; i < resolution; i++){
        if (results.nb_target_detected[i] > 0 && (results.target_status[i] == 9 || results.target_status[i] == 5 || results.target_status[i] == 6))
          tof.data[i] = (results.distance_mm[i] / 4000.f) * 255;
        else
          tof.data[i] = 255;
      }
      send_tof_msg(&tof);
    #endif
  }
}

static const DeckDriver tof_deck = {
  .name = "idsiaTOF",
  .usedGpio = (DECK_USING_SDA | DECK_USING_SCL),
  .init = tofInit,
  .test = tofTest,
};

PARAM_GROUP_START(deck)
PARAM_ADD(PARAM_UINT8 | PARAM_RONLY, idsiaTOFDeck, &isInit)
PARAM_GROUP_STOP(deck)

DECK_DRIVER(tof_deck);
