#pragma once

#include "math3d.h"
#include "stabilizer_types.h"

typedef enum {
  GROUND_ALTITUDE_REF = 0,
  SUBJECT_ALTITUDE_REF = 1
} altitude_ref_e;

typedef struct frontnet_target_s {
  float horizontalDistance;  // [m]
  float altitude;            // [m]
  altitude_ref_e altitudeReference;
} frontnet_target_t;

// TODO: do not use point_t and attitude_t: 1) each of them is stamped individually, 2) attitude would 
// be defined in degrees by cf but we use it in radians.
// Pose in free space, broken down in position and orientation, based on geometry_msgs/Pose from ROS
typedef struct pose_s {
  point_t position;    // [m]
  attitude_t attitude; // [rad]
} pose_t;

// Velocity in free space, broken down in linear and angular velocities, based geometry_msgs/Twist from ROS
typedef struct twist_s {
  velocity_t linear;   // [m/s]
  attitude_t angular;  // [rad/s]
} twist_t;

// Estimate of a pose and velocity in free space, based on nav_msgs/Odometry from ROS
typedef struct odometry_s {
  pose_t pose;
  twist_t twist;
} odometry_t;

#define MIN(x, y) ((x <= y) ? x : y)

static inline float normalizeAngle(float alpha) {
  alpha = fmodf(alpha, 2 * M_PI_F);

  if (alpha > M_PI_F) {
    alpha -= 2 * M_PI_F;
  } else if (alpha < -M_PI_F) {
    alpha += 2 * M_PI_F;
  }
  
  return alpha;
}

static inline void setPoseFromState(pose_t *pose, const state_t *state) {
  *pose = (pose_t){
    .position = state->position,
    .attitude = state->attitude
  };
}

static inline void setOdomFromPose(odometry_t *odom, const pose_t *pose) {
  *odom = (odometry_t){
    .pose = *pose,
    .twist = {{0,0,0,0}, {0,0,0,0}}
  };
}
