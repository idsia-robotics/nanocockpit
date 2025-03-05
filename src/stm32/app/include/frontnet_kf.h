// We model the drone dynamics and inference prediction as a stochastic linear 
// process with normally-distributed and zero-mean noise. More precisely, we 
// call (p_n, v_n) the position and velocity of the human subject and o_n the
// inference prediction, at time t_n and w.r.t. the drone's odometry frame.
// We assume that: 
//    p_{n+1} = p_n + v_n * (t_{n+1} − t_n)
//    v_{n+1} = v_n +   a * (t_{n+1} − t_n)
//    o_n = p_n + e
// where the acceleration a ∈ R^4 has zero mean and covariance Q, and the 
// observation error e ∈ R^3 × S^1 has zero mean and covariance R.
// We make two further assumptions: the processes are isotropic and invariant,
// i.e., covariances Q and R are constant and diagonal.
// We can then decouple the Kalman filters for each component at the cost of 
// neglecting that predictions, w.r.t. the drone longitudinal and lateral axis, 
// have slightly different MSE and that errors depend on the human subject 
// relative position.
// 
// Terminology: https://en.wikipedia.org/wiki/Kalman_filter
#pragma once

#include "frontnet_types.h"

#include <stdint.h>

typedef struct kf_d1_state_s {
  // Position
  float x;  // [m] or [rad]
  
  // Velocity
  float v;  // [m/s] or [rad/s]

  // Covariance
  float p_xx;
  float p_vv;
  float p_xv;
} kf_d1_state_t;

// Decoupled Kalman filter 
typedef struct kf_d1_s {
  // The estimated component is an angle which should be normalized to [-pi; pi]
  bool angle;
  
  // Variance of observation noise (i.e., inference)
  float r_xx;

  // Variance of process noise (i.e., acceleration)
  float q_vv;
  
  // Current state estimate
  kf_d1_state_t state;
} kf_d1_t;

typedef struct frontnet_kf_s {
  // Disable Kalman filter and return the subject pose unfiltered, for test purposes
  bool bypassFilter;

  kf_d1_t x;
  kf_d1_t y;
  kf_d1_t z;
  kf_d1_t phi;

  uint32_t lastUpdate;
} frontnet_kf_t;

void frontnetKfUpdate(frontnet_kf_t *kf, const pose_t *subjectPose, odometry_t *subjectOdom);
