/*
 * frontnet_appchannel.c
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
// #include "frontnet_types.h"

#define DEBUG_MODULE "FN-APPCHANNEL"

#include "app_channel.h"
#include "debug.h"

#include "FreeRTOS.h"
#include "static_mem.h"
#include "system.h"
#include "task.h"

#include <string.h>

typedef enum {
  INFERENCE_STAMPED_MSG  = 0,
} __attribute__((packed)) frontnet_msg_e;

typedef struct frontnet_msg_s {
  frontnet_msg_e type;

  union {
    inference_stamped_t inference_stamped;
  };
} __attribute__((packed)) frontnet_msg_t;

static bool isInit = false;

STATIC_MEM_TASK_ALLOC(appChannelTask, FN_APPCHANNEL_STACK_SIZE);

static frontnet_msg_t rxBuffer;

static void appChannelTask() {
    systemWaitStart();

    while (true) {
        size_t rxSize = appchannelReceivePacket(&rxBuffer, sizeof(rxBuffer), APPCHANNEL_WAIT_FOREVER);
        if (rxSize == 0) {
          DEBUG_PRINT("No packet received, should not happen.\n");
        } else {
          if (rxBuffer.type == INFERENCE_STAMPED_MSG) {
            frontnetEnqueueInference(&rxBuffer.inference_stamped);
          } else {
            DEBUG_PRINT("Received message with unknown type %d.\n", rxBuffer.type);
          }
        }
    }
}

void frontnetAppChannelInit() {
  if (isInit) {
    return;
  }
  
  STATIC_MEM_TASK_CREATE(appChannelTask, appChannelTask, FN_APPCHANNEL_TASK_NAME, NULL, FN_APPCHANNEL_PRIORITY);
  isInit = true;

  DEBUG_PRINT("Frontnet App Channel started\n");
}
