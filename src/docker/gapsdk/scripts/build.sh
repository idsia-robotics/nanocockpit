#!/bin/bash

# Exit at the first error
set -e

# Build the GAP SDK for each of the comma-separated configurations listed in GAP_CONFIGS
IFS=','
for CONFIG in $GAP_CONFIGS; do
    echo Building GAP SDK for config \'$CONFIG\'
    source configs/$CONFIG.sh
    make all openocd gap_tools
done
