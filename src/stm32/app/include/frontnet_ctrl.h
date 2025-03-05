#pragma once

#include "frontnet_types.h"

typedef enum {
    FRONTNET_CTRL_MODE = 0,
    HOVER_CTRL_MODE = 1,
    LAND_CTRL_MODE = 2
} ctrl_mode_e;

typedef struct frontnet_ctrl_s {
    // TODO: find better names
    float linearTau;          // [s]
    float linearK;            // [scalar]
    float angularTau;         // [s]
  
    float maxVerticalSpeed;   // [m/s]
    float maxHorizontalSpeed; // [m/s]
    float maxAngularSpeed;    // [rad/s]
} frontnet_ctrl_t;

void frontnetSetpointUpdate(const frontnet_ctrl_t *config, const odometry_t *targetOdom, const state_t *state, setpoint_t *setpoint);
void hoverSetpointUpdate(const pose_t *hoverPose, const state_t *state, setpoint_t *setpoint);
void landSetpointUpdate(const frontnet_ctrl_t *config, const state_t *state, setpoint_t *setpoint);
