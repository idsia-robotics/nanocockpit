#include "frontnet_config.h"
#include "frontnet_types.h"
#include "frontnet_inference.h"
#include "frontnet_kf.h"
#include "frontnet_ctrl.h"
#include "frontnet_appchannel.h"
#include "frontnet_test_inferences.h"
#include "frontnet_state_fwd.h"

#define DEBUG_MODULE "FRONTNET"

#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "queue.h"

#include "app.h"
#include "commander.h"
#include "debug.h"
#include "ledseq.h"
#include "log.h"
#include "param.h"
#include "pm.h"
#include "stabilizer.h"
#include "static_mem.h"
#include "system.h"
#include "usec_time.h"

#include <stdint.h>
#include <math.h>

typedef enum {
  INFERENCE_CMD,
  TIMER_CMD
} frontnet_cmd_e;

typedef struct frontnet_cmd_s {
  frontnet_cmd_e type;
  union {
    inference_stamped_t inference;
    void *data;
  };
} frontnet_cmd_t;

static bool isInit = false;

static QueueHandle_t commandQueue;
STATIC_MEM_QUEUE_ALLOC(commandQueue, 1, sizeof(frontnet_cmd_t));

static TimerHandle_t timer;
STATIC_MEM_TIMER_ALLOC(timer);

STATIC_MEM_TASK_ALLOC(frontnetTask, FRONTNET_STACKSIZE);

static bool useInferenceTimeState = true;
static frontnet_kf_t kf = FRONTNET_KF_DEFAULT_CONFIG;
static frontnet_target_t targetConfig = FRONTNET_TARGET_DEFAULT_CONFIG;
static frontnet_ctrl_t controllerConfig = FRONTNET_CTRL_DEFAULT_CONFIG;

static uint32_t lastInference = 0;
static uint32_t lastTimer = 0;
static uint32_t lastUpdate = 0;

static state_t state;
static inference_stamped_t inference;
static odometry_t subjectOdom;
static odometry_t targetOdom;

static pose_t hoverPose;

static bool enableControl = false;
static bool controlEnabled = false;
static ctrl_mode_e controlMode = FRONTNET_CTRL_MODE;
static setpoint_t setpoint;
static float minBatteryVoltage = FRONTNET_MIN_BATTERY_VOLTAGE;
static bool verbose = false;

#define VERBOSE_PRINT(...) if (verbose) { DEBUG_PRINT(__VA_ARGS__); }

static uint32_t lastRateUpdate = 0;
static uint32_t inferencesSinceRateUpdate = 0;
static uint8_t averageInferenceRate = 0;

static uint32_t inferenceLatency = 0;

static uint64_t kfLatencySum = 0;
static uint32_t currentKfLatencySample = 0; 

static ledseqStep_t seq_autonomous_def[] = {
  { true, LEDSEQ_WAITMS(1000)},
  {    0, LEDSEQ_LOOP},
};

static ledseqContext_t seq_autonomous = {
  .sequence = seq_autonomous_def,
  .led = LED_GREEN_R,
};

static void computeSubjectPoseInOdomFrame(const inference_stamped_t *inference, const state_t *state, pose_t *subjectPose) {
  // Convert an inference output, expressed in frame cf/base_link (with a flipped yaw, hence the +M_PI_F), to a pose expressed in frame cf/odom
  // NOTE: the firmware uses degrees and degree/s for attitudes and attitude rates in its data types (state_t and setpoint_t)
  float statePhi = radians(state->attitude.yaw);
  float sn = sinf(statePhi);
  float cs = cosf(statePhi);

  *subjectPose = (pose_t){
    .position = {
      .timestamp = inference->stm32_timestamp,
      .x = state->position.x + cs * inference->x - sn * inference->y,
      .y = state->position.y + sn * inference->x + cs * inference->y,
      .z = state->position.z + inference->z,
    },
    
    .attitude = {
      .timestamp = inference->stm32_timestamp,
      .roll  = 0.0f,
      .pitch = 0.0f,
      .yaw   = statePhi + inference->phi + M_PI_F
    }
  };
}

static void computeTargetOdom(const frontnet_target_t *config, const odometry_t *subjectOdom, const state_t *state, odometry_t *targetOdom) {
  // Compute the target odometry (i.e., pose + velocity) that we want the drone to reach, expressed in frame cf/odom
  const point_t *subjectPos = &subjectOdom->pose.position;
  const attitude_t *subjectAtt = &subjectOdom->pose.attitude;

  // Target pose is horizontalDistance meters in front of subject pose, in the direction of subject yaw
  float targetX = subjectPos->x + cosf(subjectAtt->yaw) * config->horizontalDistance;
  float targetY = subjectPos->y + sinf(subjectAtt->yaw) * config->horizontalDistance;

  // Target altitude is relative to either the ground or the subject, depending on configuration
  float targetZ;
  if (config->altitudeReference == GROUND_ALTITUDE_REF) {
    targetZ = config->altitude;
  } else if (config->altitudeReference == SUBJECT_ALTITUDE_REF) {
    targetZ = subjectPos->z + config->altitude;
  } else {
    // Unknown altitude reference, set targetZ just to make the compiler happy
    targetZ = 0.0f;
    ASSERT_FAILED();
  }

  // Target yaw keeps the drone looking at the subject as it's moving to reach it
  float targetYaw = atan2f(subjectPos->y - state->position.y, subjectPos->x - state->position.x);

  *targetOdom = (odometry_t){
    .pose = {
      .position.timestamp = subjectPos->timestamp,
      .position.x = targetX,
      .position.y = targetY,
      .position.z = targetZ,

      .attitude.timestamp = subjectAtt->timestamp,
      .attitude.roll  = 0.0f,
      .attitude.pitch = 0.0f,
      .attitude.yaw   = targetYaw
    },

    // Target moves at the same velocity as the subject, allowing control to anticipate it
    .twist = subjectOdom->twist
  };
}

void frontnetEnqueueInference(const inference_stamped_t *inference) {
  frontnet_cmd_t command = {
    .type = INFERENCE_CMD,
    .inference = *inference
  };
  xQueueOverwrite(commandQueue, &command);
}

static void timerCallback(TimerHandle_t xTimer) {
  frontnet_cmd_t command = {
    .type = TIMER_CMD
  };
  xQueueSend(commandQueue, &command, 0);
}

static void frontnetTask(void *_param) {
  DEBUG_PRINT(
    "state_stm32_timestamp,inference_stm32_timestamp,"
    // "output_x,output_y,output_z,output_phi,"
    "kf_x,kf_y,kf_z,kf_phi,kf_vx,kf_vy,kf_vz,kf_vphi,"
    "ctrl_enabled,ctrl_mode,setpoint_priority"
    // "ctrl_vx,ctrl_vx,ctrl_vz,ctrl_vphi"
    "\n"
  );

  // Process inferences immediately as they arrive, all the way to feeding a new
  // setpoint to the commander. Additionally, when control is enabled, continuously
  // update the setpoint at FRONTNET_TIMER_RATE Hz even if no inferences are received.

  systemWaitStart();

  while (true) {
    // Wait up to FRONTNET_COMMAND_TIMEOUT ms for an external command to come in.
    frontnet_cmd_t command;
    bool commandReceived = xQueueReceive(commandQueue, &command, FRONTNET_COMMAND_TIMEOUT);

    // Retrieve the latest available state at the time we wake up after xQueueReceive.
    stateCompressed_t stateCompressed;
    stabilizerGetLatestState(&stateCompressed);
    stabilizerDecompressState(&stateCompressed, &state);

    if (commandReceived) {
      switch (command.type) {
        // INFERENCE_CMD contains a new inference output that we received and that should be
        // used to update the drone's target pose.
        case INFERENCE_CMD:
        {
          inference = command.inference;
          
          uint32_t inferenceTime = xTaskGetTickCount();
          uint32_t inferenceDt = T2M(inferenceTime - lastInference);
          lastInference = inferenceTime;

          inferenceLatency = T2M(inferenceTime - inference.stm32_timestamp);
          
          VERBOSE_PRINT(
            "Received inference: t: %lu, [%0.3f, %0.3f, %0.3f, %0.3f], %ldms since previous inference, %ldms inference latency\n",
            inference.stm32_timestamp,
            (double)inference.x, (double)inference.y, (double)inference.z, (double)inference.phi,
            inferenceDt, inferenceLatency
          );

          state_t inferenceState;
          if (useInferenceTimeState) {
            // Retrieve the state estimation from the time the camera image was acquired
            stateCompressed_t stateCompressed;
            bool hasInferenceState = stateFwdDequeueAtTimestamp(inference.stm32_timestamp, &stateCompressed);

            if (!hasInferenceState) {
              VERBOSE_PRINT(
                "State corresponding to inference not available (need %ldms, now %ldms), discarding\n",
                inference.stm32_timestamp, inferenceTime
              );
              break;
            }

            stabilizerDecompressState(&stateCompressed, &inferenceState);
          } else {
            inferenceState = state;
          }
          
          pose_t subjectPose;
          computeSubjectPoseInOdomFrame(&inference, &inferenceState, &subjectPose);
          
          uint64_t start_us = usecTimestamp();
          frontnetKfUpdate(&kf, &subjectPose, &subjectOdom);
          uint64_t end_us = usecTimestamp();

          computeTargetOdom(&targetConfig, &subjectOdom, &state, &targetOdom);

          kfLatencySum += end_us - start_us;
          currentKfLatencySample = (currentKfLatencySample + 1) % FRONTNET_PROFILE_KF_COUNT;
          if (currentKfLatencySample == 0) {
            VERBOSE_PRINT("Average KF latency %0.3fus\n", (double)kfLatencySum / FRONTNET_PROFILE_KF_COUNT);
            kfLatencySum = 0;
          }

          // Reset the time-to-next-update since we just received a new inference
          if (controlEnabled) {
            xTimerReset(timer, 0);
          }

          // Count received inferences to compute the average inference rate
          inferencesSinceRateUpdate += 1;

          int setpointPriority = commanderGetActivePriority();
          DEBUG_PRINT(
            "%lu,%lu,"
            // "%.3f,%.3f,%.3f,%.3f,"
            "%.3f,%.3f,%.3f,%.3f,"
            "%.3f,%.3f,%.3f,%.3f,"
            "%d,%d,%d"
            // "%.3f,%.3f,%.3f,%.3f"
            "\n",
            inference.stm32_timestamp, lastInference,
            // (double)inference.x, (double)inference.y, (double)inference.z, (double)inference.phi,
            (double)subjectOdom.pose.position.x, (double)subjectOdom.pose.position.y, (double)subjectOdom.pose.position.z, (double)subjectOdom.pose.attitude.yaw,
            (double)subjectOdom.twist.linear.x, (double)subjectOdom.twist.linear.y, (double)subjectOdom.twist.linear.z, (double)subjectOdom.twist.angular.yaw,
            controlEnabled, controlMode, setpointPriority
            // (double)setpoint.velocity.x, (double)setpoint.velocity.y, (double)setpoint.velocity.z, (double)setpoint.attitudeRate.yaw
          );

          break;
        }

        // TIMER_CMD is sent periodically while control is enabled. It is used to update the 
        // control setpoints so that, even if no new inferences come in, so that the drone will 
        // converge to the desired target pose and hover there.
        case TIMER_CMD:
        {
          // There is nothing to do specifically for the target, setpoint update is handled below,
          // by the same code used for INFERENCE_CMD.
          uint32_t timerTime = xTaskGetTickCount();
          uint32_t dt = T2M(timerTime - lastTimer);
          lastTimer = timerTime;
          VERBOSE_PRINT("Received timer callback (%ldms since last timer)\n", dt);
          break;
        }
      }
    } else {
      // Command queue timeout, should only happen when no inference is being received and control is disabled.
      // It is used to check if enableControl has changed since the last iteration.
      VERBOSE_PRINT("Last Frontnet command received more than %dms ago.\n", FRONTNET_COMMAND_TIMEOUT);
    }

    uint32_t updateTime = xTaskGetTickCount();

    uint32_t rateUpdateDt = updateTime - lastRateUpdate;
    if (rateUpdateDt > FRONTNET_INFERENCE_RATE_PERIOD) {
        float _averageInferenceRate = inferencesSinceRateUpdate / T2S(rateUpdateDt);
        averageInferenceRate = (uint8_t)roundf(_averageInferenceRate);
        DEBUG_PRINT("Average inference rate %0.2fHz\n", (double)_averageInferenceRate);

        lastRateUpdate = updateTime;
        inferencesSinceRateUpdate = 0;
    }

    bool willEnableControl = enableControl && !controlEnabled;
    bool willDisableControl = !enableControl && controlEnabled;
    controlEnabled = enableControl;

    if (willEnableControl) {
      xTimerStart(timer, 0);
      ledseqRun(&seq_autonomous);
      DEBUG_PRINT("Autonomous control enabled.\n");
    }

    if (willDisableControl) {
      xTimerStop(timer, 0);
      ledseqStop(&seq_autonomous);
      DEBUG_PRINT("Autonomous control disabled.\n");
    }

    bool inferenceTimeout = (updateTime - lastInference) > FRONTNET_INFERENCE_TIMEOUT;
    if (inferenceTimeout) {
      if (controlEnabled && controlMode == FRONTNET_CTRL_MODE) {
        DEBUG_PRINT("Last inference was received more than %dms ago, hovering.\n", FRONTNET_INFERENCE_TIMEOUT);
        controlMode = HOVER_CTRL_MODE;
        setPoseFromState(&hoverPose, &state);
      }
    } else {
      if (controlMode == HOVER_CTRL_MODE) {
        controlMode = FRONTNET_CTRL_MODE;
      }
    }

    if (controlEnabled) {
      if (controlMode == FRONTNET_CTRL_MODE) {
        frontnetSetpointUpdate(&controllerConfig, &targetOdom, &state, &setpoint);
      } else if (controlMode == HOVER_CTRL_MODE) {
        hoverSetpointUpdate(&hoverPose, &state, &setpoint);
      } else if (controlMode == LAND_CTRL_MODE) {
        landSetpointUpdate(&controllerConfig, &state, &setpoint);
      } else {
        DEBUG_PRINT("Unknown control mode %d.\n", controlMode);
      }
      
      commanderSetSetpoint(&setpoint, FRONTNET_SETPOINT_PRIORITY);
    }

    lastUpdate = updateTime;
  }
}

void appInit() {
  /// App Layer entrypoint
  /// The firmware calls this function early in the boot process to start the application tasks
  if (isInit) {
    return;
  }

  commandQueue = STATIC_MEM_QUEUE_CREATE(commandQueue);
  ASSERT(commandQueue);

  timer = STATIC_MEM_TIMER_CREATE(timer, "frontnetTimer", F2T(FRONTNET_TIMER_RATE), pdTRUE, NULL, timerCallback);
  ASSERT(timer);
  
  ledseqInit();
  ledseqRegisterSequenceFront(&seq_autonomous);

  // Support generating a sequence of test inferences
  frontnetTestInferencesInit();

  // Periodically forward the state estimation to GAP8
  stateFwdInit();

  // Receive inferences over CRTP app channel
  frontnetAppChannelInit();

  STATIC_MEM_TASK_CREATE(frontnetTask, frontnetTask, FRONTNET_TASK_NAME, NULL, FRONTNET_PRIORITY);
  isInit = true;

  DEBUG_PRINT("PULP-Frontnet started\n");
}

// NOTE: kept old parameter and log names for backward compatibility with drone_arena
PARAM_GROUP_START(frontnet)
// High-level control
// TODO: refactor to use PARAM_CALLBACK when we upgrade to cf_firmare >= 2022.01
PARAM_ADD(PARAM_UINT8, enable_control, &enableControl)
PARAM_ADD(PARAM_UINT8, verbose, &verbose)

PARAM_ADD(PARAM_UINT8, infer_t_state, &useInferenceTimeState)

// Kalman filter configuration
PARAM_ADD(PARAM_FLOAT, kalman_x_r, &kf.x.r_xx)
PARAM_ADD(PARAM_FLOAT, kalman_x_q, &kf.x.q_vv)
PARAM_ADD(PARAM_FLOAT, kalman_y_r, &kf.y.r_xx)
PARAM_ADD(PARAM_FLOAT, kalman_y_q, &kf.y.q_vv)
PARAM_ADD(PARAM_FLOAT, kalman_z_r, &kf.z.r_xx)
PARAM_ADD(PARAM_FLOAT, kalman_z_q, &kf.z.q_vv)
PARAM_ADD(PARAM_FLOAT, kalman_phi_r, &kf.phi.r_xx)
PARAM_ADD(PARAM_FLOAT, kalman_phi_q, &kf.phi.q_vv)

// Target configuration
PARAM_ADD(PARAM_FLOAT, distance, &targetConfig.horizontalDistance)
PARAM_ADD(PARAM_FLOAT, altitude, &targetConfig.altitude)
PARAM_ADD(PARAM_UINT8, rel_altitude, &targetConfig.altitudeReference)

// Controller configuration
PARAM_ADD(PARAM_FLOAT, eta, &controllerConfig.linearTau)
PARAM_ADD(PARAM_FLOAT, k, &controllerConfig.linearK)
PARAM_ADD(PARAM_FLOAT, rotation_tau, &controllerConfig.angularTau)
PARAM_ADD(PARAM_FLOAT, max_vert_speed, &controllerConfig.maxVerticalSpeed)
PARAM_ADD(PARAM_FLOAT, max_speed, &controllerConfig.maxHorizontalSpeed)
PARAM_ADD(PARAM_FLOAT, max_ang_speed, &controllerConfig.maxAngularSpeed)

PARAM_ADD(PARAM_FLOAT, min_voltage, &minBatteryVoltage)
PARAM_GROUP_STOP(frontnet)

LOG_GROUP_START(frontnet)
// High-level control
LOG_ADD(LOG_UINT8, control_enabled, &enableControl)
LOG_ADD(LOG_UINT8, control_active, &controlEnabled)

LOG_ADD(LOG_UINT32, lastUpdate, &lastInference)
LOG_ADD(LOG_UINT8, update_frequency, &averageInferenceRate)
LOG_ADD(LOG_UINT32, inf_latency, &inferenceLatency)

// Inference
LOG_ADD(LOG_FLOAT, x, &inference.x)
LOG_ADD(LOG_FLOAT, y, &inference.y)
LOG_ADD(LOG_FLOAT, z, &inference.z)
LOG_ADD(LOG_FLOAT, phi, &inference.phi)

// Subject pose, after Kalman filter
LOG_ADD(LOG_FLOAT, f_x, &subjectOdom.pose.position.x)
LOG_ADD(LOG_FLOAT, f_y, &subjectOdom.pose.position.y)
LOG_ADD(LOG_FLOAT, f_z, &subjectOdom.pose.position.z)
LOG_ADD(LOG_FLOAT, f_phi, &subjectOdom.pose.attitude.yaw)
LOG_ADD(LOG_FLOAT, f_vx, &subjectOdom.twist.linear.x)
LOG_ADD(LOG_FLOAT, f_vy, &subjectOdom.twist.linear.y)
LOG_ADD(LOG_FLOAT, f_vz, &subjectOdom.twist.linear.z)
LOG_ADD(LOG_FLOAT, f_vphi, &subjectOdom.twist.angular.yaw)
LOG_GROUP_STOP(frontnet)
