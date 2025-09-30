#
# ImageMetadata.msg
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
# 
# This software is based on the following publication:
#    E. Cereda, A. Giusti, D. Palossi. "NanoCockpit: Performance-optimized 
#    Application Framework for AI-based Autonomous Nanorobotics"
# We kindly ask for a citation if you use in academic work.
#

std_msgs/Header header

# Metadata format version
uint8 metadata_version

# Sequential frame ID from the camera's hardware frame counter
uint8 frame_id

# GAP end-of-frame timestamp [usec]
uint32 frame_gap8_timestamp

# GAP state received timestamp [usec]
uint32 state_gap8_timestamp

# STM32 state timestamp [ticks]
uint32 state_stm32_timestamp

# GAP timestamps used for RTT latency computation [usec]
uint32 reply_frame_gap8_timestamp
uint32 reply_recv_gap8_timestamp
