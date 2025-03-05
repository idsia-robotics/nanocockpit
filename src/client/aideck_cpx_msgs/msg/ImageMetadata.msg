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
