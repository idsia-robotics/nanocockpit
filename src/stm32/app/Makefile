# Makefile
# Elia Cereda <elia.cereda@idsia.ch>
# 
# Copyright (C) 2022-2025 IDSIA, USI-SUPSI
# 
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
# 
#     http://www.apache.org/licenses/LICENSE-2.0
# 
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

## NOTES
# - Removed ACTIVATE_STARTUP_SOUND in config.h 
# - TODO: crazyflie-firmware currently wastes 300 bytes if we reimplement appInit (app_handler.c)

APP=1

# DEBUG=1
CFLAGS += -DDECK_FORCE=bcAI:idsiaTOF -Werror=maybe-uninitialized -Wno-error=unused-variable

VPATH += src/
INCLUDES += -Iinclude/
PROJ_OBJ += frontnet_main.o frontnet_kf.o frontnet_ctrl.o
PROJ_OBJ += frontnet_aideck_protocol.o frontnet_appchannel.o frontnet_rng.o frontnet_state_fwd.o
PROJ_OBJ += frontnet_test_inferences.o

CRAZYFLIE_BASE=../crazyflie-firmware
include $(CRAZYFLIE_BASE)/Makefile
