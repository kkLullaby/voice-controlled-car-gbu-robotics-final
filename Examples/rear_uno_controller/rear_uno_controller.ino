/*
  Rear UNO controller for the articulated four-wheel voice car.

  Same open-loop kinematics as the front board. The one rear-specific bit:
  PATH_OFFSET_M = VEHICLE_LENGTH_M, and only L/R go through a delay queue,
  so the rear module begins its turn `VEHICLE_LENGTH_M / speed` seconds
  after the front module — mimicking how a trailing axle follows a pulled
  front axle along the same path.

  2026-06 update (kept in sync with front_uno_controller):
    - MotionState (STATIONARY/MOVING): in STATIONARY only F/B/S take effect,
      L/R/U/D are silently ignored.
    - F/B/U/D/S bypass the delay queue and execute immediately, so the
      front and rear boards start/stop/change-speed in sync. ONLY L/R use
      the rear path-offset delay.
    - L/R auto-recenter after TURN_HOLD_TIME_S.
    - F<->B reversal inserts a brief stop to protect the H-bridge.
    - COMMAND_TIMEOUT_MS raised to 20s for sustained-motion testing.
*/

#include <Arduino.h>
#include <math.h>

// =======================
// Board identity
// =======================

// Human-readable board name. Unit: none
const char BOARD_NAME[] = "REAR";

// =======================
// Physical parameters
// =======================

// Distance between left and right wheel contact centers on one UNO module.
// Unit: meter (m)
const float WHEEL_TRACK_M = 0.160f;

// Distance between front wheel axle center and rear wheel axle center.
// Unit: meter (m)
const float VEHICLE_LENGTH_M = 0.320f;

// Distance offset of this wheel module along the planned vehicle path.
// Front board: 0.0 m
// Rear board: VEHICLE_LENGTH_M
// Unit: meter (m)
const float PATH_OFFSET_M = VEHICLE_LENGTH_M;

// Desired path radius of the module center during steady turning.
// Unit: meter (m)
const float TURN_RADIUS_M = 0.800f;

// Smooth transition time for entering a turn.
// Unit: second (s)
const float ENTER_TURN_TIME_S = 1.200f;

// Smooth transition time for exiting a turn.
// Unit: second (s)
const float EXIT_TURN_TIME_S = 1.200f;

// How long L/R holds the target curvature after the entry transition
// finishes, before auto-recentering back to straight.
// Unit: second (s)
const float TURN_HOLD_TIME_S = 1.500f;

// Minimum inner-wheel speed ratio allowed during a turn.
// Example: 0.60 means the inner wheel should not go below 60% of the outer wheel.
// Unit: none
const float MIN_INNER_SPEED_RATIO = 0.600f;

// =======================
// Open-loop speed settings
// =======================

// Available center speeds for this module.
// Unit: meter per second (m/s)
const float SPEED_LEVELS_MPS[] = {
  0.100f,
  0.150f,
  0.200f,
};

// Default speed level index in SPEED_LEVELS_MPS.
// Unit: none
const int DEFAULT_SPEED_LEVEL_INDEX = 1;

// Minimum speed used when converting path offset to time delay.
// Unit: meter per second (m/s)
const float MIN_DELAY_SPEED_MPS = 0.050f;

// =======================
// Speed-to-PWM calibration table
// =======================

// Target wheel linear speed samples.
// Unit: meter per second (m/s)
const float SPEED_TABLE_MPS[] = {
  0.000f,
  0.050f,
  0.100f,
  0.150f,
  0.200f,
  0.250f,
};

// PWM values corresponding to SPEED_TABLE_MPS.
// Unit: Arduino analogWrite value, range 0-255
const int PWM_TABLE[] = {
  0,
  70,
  90,
  115,
  140,
  165,
};

// Largest PWM allowed during the first car tests.
// Unit: Arduino analogWrite value, range 0-255
const int PWM_MAX_SAFE = 170;

// Per-wheel open-loop correction multipliers.
// Unit: none
const float LEFT_WHEEL_TRIM = 1.000f;
const float RIGHT_WHEEL_TRIM = 1.000f;

// Motor direction correction.
// Use 1 for normal direction and -1 if the wheel runs backward.
// Unit: none
const int LEFT_MOTOR_DIR = 1;
const int RIGHT_MOTOR_DIR = 1;

// =======================
// Pin configuration
// =======================

// Bluetooth serial baud rate.
// Unit: bit per second (baud)
const unsigned long BLUETOOTH_BAUD = 9600;

// Left motor forward PWM pin.
// Unit: Arduino pin number
const int LEFT_FORWARD_PWM_PIN = 5;

// Left motor reverse PWM pin.
// Unit: Arduino pin number
const int LEFT_REVERSE_PWM_PIN = 3;

// Right motor forward PWM pin.
// Unit: Arduino pin number
const int RIGHT_FORWARD_PWM_PIN = 10;

// Right motor reverse PWM pin.
// Unit: Arduino pin number
const int RIGHT_REVERSE_PWM_PIN = 9;

// =======================
// Safety and timing
// =======================

// Control-loop update period.
// Unit: millisecond (ms)
const unsigned long CONTROL_PERIOD_MS = 20;

// Stop the car if no valid Bluetooth command is received in this time.
// Raised from 3s to 20s for sustained-motion testing.
// Unit: millisecond (ms)
const unsigned long COMMAND_TIMEOUT_MS = 20000;

// Brief motor-off pause inserted when reversing F<->B, to protect the
// H-bridge from a hard direction flip while current is still flowing.
// Unit: millisecond (ms)
const unsigned long DIRECTION_REVERSAL_PAUSE_MS = 100;

// =======================
// Runtime state
// =======================

enum DriveState {
  DRIVE_STOPPED,
  DRIVE_ACTIVE,
};

// High-level motion state. STATIONARY = car not moving (boot, or after S).
// In STATIONARY only F/B/S are accepted; L/R/U/D are silently ignored.
enum MotionState {
  MOTION_STATIONARY,
  MOTION_MOVING,
};

DriveState driveState = DRIVE_STOPPED;
MotionState motionState = MOTION_STATIONARY;
int speedLevelIndex = DEFAULT_SPEED_LEVEL_INDEX;
int driveDirection = 1;

float maneuverStartCurvature = 0.0f;
float maneuverTargetCurvature = 0.0f;
float maneuverTransitionTimeS = 0.0f;

unsigned long maneuverStartMs = 0;
unsigned long lastCommandMs = 0;
unsigned long lastControlMs = 0;

// When the current turn should auto-recenter to straight (relative to the
// rear board's local time, i.e. already delayed by pathDelayMs). 0 means
// "no pending auto-recenter".
unsigned long turnRecenterAtMs = 0;

const int SPEED_LEVEL_COUNT = sizeof(SPEED_LEVELS_MPS) / sizeof(SPEED_LEVELS_MPS[0]);
const int SPEED_TABLE_COUNT = sizeof(SPEED_TABLE_MPS) / sizeof(SPEED_TABLE_MPS[0]);

// =======================
// Command queue (rear-only delay path for L/R)
// =======================

// Only L and R commands are queued here. F/B/U/D/S are executed
// immediately to keep front and rear in sync on starts/stops/speed
// changes. The queue's only job is to push L/R execution back by
// pathDelayMs so the rear module begins its turn after the front
// module has rolled forward by VEHICLE_LENGTH_M.

struct QueuedCommand {
  char code;
  unsigned long arrivedMs;
};

const int CMD_QUEUE_SIZE = 8;
QueuedCommand cmdQueue[CMD_QUEUE_SIZE];
int queueHead = 0;
int queueCount = 0;

unsigned long pathDelayMs = 0;

void setup() {
  pinMode(LEFT_FORWARD_PWM_PIN, OUTPUT);
  pinMode(LEFT_REVERSE_PWM_PIN, OUTPUT);
  pinMode(RIGHT_FORWARD_PWM_PIN, OUTPUT);
  pinMode(RIGHT_REVERSE_PWM_PIN, OUTPUT);

  Serial.begin(BLUETOOTH_BAUD);

  const unsigned long now = millis();
  maneuverStartMs = now;
  lastCommandMs = now;
  lastControlMs = now;

  updatePathDelayMs();
  emergencyStop();
}

void loop() {
  readBluetoothCommands();
  processQueue();

  const unsigned long now = millis();

  // Auto-recenter when a previous L/R has held long enough.
  if (motionState == MOTION_MOVING
      && turnRecenterAtMs != 0
      && (long)(now - turnRecenterAtMs) >= 0) {
    turnRecenterAtMs = 0;
    beginCurvatureManeuver(0.0f);
  }

  if (driveState == DRIVE_ACTIVE && now - lastCommandMs > COMMAND_TIMEOUT_MS) {
    emergencyStop();
  }

  if (now - lastControlMs >= CONTROL_PERIOD_MS) {
    lastControlMs = now;
    updateWheelOutputs(now);
  }
}

void readBluetoothCommands() {
  while (Serial.available() > 0) {
    char command = Serial.read();
    if (command == '\n' || command == '\r' || command == ' ') {
      continue;
    }
    if (command >= 'a' && command <= 'z') {
      command = command - 'a' + 'A';
    }

    // STATIONARY filter: only F/B/S can do anything. L/R/U/D silently
    // dropped (no queueing either — otherwise a turn queued while
    // stopped would fire pathDelayMs after the next F).
    if (motionState == MOTION_STATIONARY
        && command != 'F' && command != 'B' && command != 'S') {
      continue;
    }

    lastCommandMs = millis();

    if (command == 'L' || command == 'R') {
      // Only L/R go through the rear delay queue.
      enqueue(command, millis());
    } else {
      // F/B/U/D/S execute immediately. On F/B the front and rear boards
      // must start at the same instant; on U/D both must change speed
      // together; on S both must stop together. We also drop any
      // pending queued turn — a hard direction/state change invalidates
      // older queued turns.
      if (command == 'F' || command == 'B' || command == 'S') {
        queueCount = 0;
        queueHead = 0;
      }
      handleCommand(command);
    }
  }
}

void enqueue(char code, unsigned long arrivedMs) {
  if (queueCount >= CMD_QUEUE_SIZE) {
    queueHead = (queueHead + 1) % CMD_QUEUE_SIZE;
    queueCount -= 1;
  }
  int tail = (queueHead + queueCount) % CMD_QUEUE_SIZE;
  cmdQueue[tail].code = code;
  cmdQueue[tail].arrivedMs = arrivedMs;
  queueCount += 1;
}

void processQueue() {
  const unsigned long now = millis();
  while (queueCount > 0) {
    QueuedCommand& cmd = cmdQueue[queueHead];
    if (now - cmd.arrivedMs < pathDelayMs) {
      break;
    }
    handleCommand(cmd.code);
    queueHead = (queueHead + 1) % CMD_QUEUE_SIZE;
    queueCount -= 1;
  }
}

void updatePathDelayMs() {
  float speedMps = selectedSpeedMps();
  if (speedMps < MIN_DELAY_SPEED_MPS) {
    speedMps = MIN_DELAY_SPEED_MPS;
  }
  pathDelayMs = (unsigned long)(PATH_OFFSET_M / speedMps * 1000.0f);
}

void handleCommand(char command) {
  switch (command) {
    case 'F':
      lastCommandMs = millis();
      startOrUpdateDrive(+1);
      break;
    case 'B':
      lastCommandMs = millis();
      startOrUpdateDrive(-1);
      break;
    case 'L':
      lastCommandMs = millis();
      beginTurn(+1);
      break;
    case 'R':
      lastCommandMs = millis();
      beginTurn(-1);
      break;
    case 'S':
      lastCommandMs = millis();
      emergencyStop();
      break;
    case 'U':
      lastCommandMs = millis();
      increaseSpeedLevel();
      break;
    case 'D':
      lastCommandMs = millis();
      decreaseSpeedLevel();
      break;
    default:
      break;
  }
}

void startOrUpdateDrive(int newDirection) {
  if (motionState == MOTION_MOVING && newDirection != driveDirection) {
    stopMotors();
    delay(DIRECTION_REVERSAL_PAUSE_MS);
  }
  driveDirection = newDirection;
  driveState = DRIVE_ACTIVE;
  motionState = MOTION_MOVING;
  turnRecenterAtMs = 0;
  beginCurvatureManeuver(0.0f);
}

void beginTurn(int turnDirection) {
  beginCurvatureManeuver(turnCurvature(turnDirection));
  unsigned long holdMs =
      (unsigned long)(ENTER_TURN_TIME_S * 1000.0f)
    + (unsigned long)(TURN_HOLD_TIME_S * 1000.0f);
  turnRecenterAtMs = millis() + holdMs;
  if (turnRecenterAtMs == 0) {
    turnRecenterAtMs = 1;
  }
}

void beginCurvatureManeuver(float targetCurvature) {
  const unsigned long now = millis();
  maneuverStartCurvature = plannedCurvatureAt(now);
  maneuverTargetCurvature = clampTurnCurvature(targetCurvature);
  maneuverTransitionTimeS = transitionTimeFor(maneuverStartCurvature, maneuverTargetCurvature);
  maneuverStartMs = now;
}

float plannedCurvatureAt(unsigned long nowMs) {
  if (maneuverTransitionTimeS <= 0.0f) {
    return maneuverTargetCurvature;
  }

  // No pathDelaySeconds() subtraction here, unlike the front board: the
  // rear-board delay has already been applied by processQueue() before
  // beginCurvatureManeuver was even called. Subtracting again would
  // double-count it.
  const float elapsedS = (nowMs - maneuverStartMs) / 1000.0f;

  if (elapsedS <= 0.0f) {
    return maneuverStartCurvature;
  }
  if (elapsedS >= maneuverTransitionTimeS) {
    return maneuverTargetCurvature;
  }

  const float u = elapsedS / maneuverTransitionTimeS;
  const float h = smootherStep(u);
  return maneuverStartCurvature + (maneuverTargetCurvature - maneuverStartCurvature) * h;
}

float transitionTimeFor(float startCurvature, float targetCurvature) {
  const float epsilon = 0.0001f;
  const bool startStraight = fabs(startCurvature) < epsilon;
  const bool targetStraight = fabs(targetCurvature) < epsilon;

  if (startStraight && targetStraight) {
    return 0.0f;
  }
  if (targetStraight) {
    return EXIT_TURN_TIME_S;
  }
  if (startStraight) {
    return ENTER_TURN_TIME_S;
  }
  if (startCurvature * targetCurvature < 0.0f) {
    return ENTER_TURN_TIME_S + EXIT_TURN_TIME_S;
  }
  return ENTER_TURN_TIME_S;
}

float smootherStep(float u) {
  if (u <= 0.0f) {
    return 0.0f;
  }
  if (u >= 1.0f) {
    return 1.0f;
  }
  return u * u * u * (10.0f + u * (-15.0f + 6.0f * u));
}

float turnCurvature(int turnDirection) {
  if (TURN_RADIUS_M <= 0.0f) {
    return 0.0f;
  }
  return clampTurnCurvature(turnDirection * (1.0f / TURN_RADIUS_M));
}

float clampTurnCurvature(float curvature) {
  if (WHEEL_TRACK_M <= 0.0f) {
    return 0.0f;
  }

  float ratio = MIN_INNER_SPEED_RATIO;
  if (ratio < 0.0f) {
    ratio = 0.0f;
  }
  if (ratio > 0.95f) {
    ratio = 0.95f;
  }

  const float maxAbsCurvature =
      2.0f * (1.0f - ratio) / (WHEEL_TRACK_M * (1.0f + ratio));

  if (curvature > maxAbsCurvature) {
    return maxAbsCurvature;
  }
  if (curvature < -maxAbsCurvature) {
    return -maxAbsCurvature;
  }
  return curvature;
}

void updateWheelOutputs(unsigned long nowMs) {
  if (driveState == DRIVE_STOPPED) {
    stopMotors();
    return;
  }

  const float centerSpeedMps = driveDirection * selectedSpeedMps();
  const float curvature = plannedCurvatureAt(nowMs);

  const float leftWheelSpeedMps =
      centerSpeedMps * (1.0f - 0.5f * WHEEL_TRACK_M * curvature);
  const float rightWheelSpeedMps =
      centerSpeedMps * (1.0f + 0.5f * WHEEL_TRACK_M * curvature);

  setMotorFromSpeed(
      LEFT_FORWARD_PWM_PIN,
      LEFT_REVERSE_PWM_PIN,
      leftWheelSpeedMps,
      LEFT_WHEEL_TRIM,
      LEFT_MOTOR_DIR);

  setMotorFromSpeed(
      RIGHT_FORWARD_PWM_PIN,
      RIGHT_REVERSE_PWM_PIN,
      rightWheelSpeedMps,
      RIGHT_WHEEL_TRIM,
      RIGHT_MOTOR_DIR);
}

float selectedSpeedMps() {
  if (speedLevelIndex < 0) {
    speedLevelIndex = 0;
  }
  if (speedLevelIndex >= SPEED_LEVEL_COUNT) {
    speedLevelIndex = SPEED_LEVEL_COUNT - 1;
  }
  return SPEED_LEVELS_MPS[speedLevelIndex];
}

void increaseSpeedLevel() {
  if (speedLevelIndex < SPEED_LEVEL_COUNT - 1) {
    speedLevelIndex += 1;
    updatePathDelayMs();
  }
}

void decreaseSpeedLevel() {
  if (speedLevelIndex > 0) {
    speedLevelIndex -= 1;
    updatePathDelayMs();
  }
}

void setMotorFromSpeed(
    int forwardPin,
    int reversePin,
    float wheelSpeedMps,
    float trim,
    int motorDirection) {
  const float absSpeedMps = fabs(wheelSpeedMps);
  int pwm = speedToPwm(absSpeedMps);

  pwm = (int)(pwm * trim + 0.5f);
  pwm = constrain(pwm, 0, PWM_MAX_SAFE);

  if (pwm <= 0 || absSpeedMps <= 0.0001f) {
    analogWrite(forwardPin, 0);
    analogWrite(reversePin, 0);
    return;
  }

  int direction = (wheelSpeedMps >= 0.0f) ? 1 : -1;
  direction *= motorDirection;

  if (direction >= 0) {
    analogWrite(forwardPin, pwm);
    analogWrite(reversePin, 0);
  } else {
    analogWrite(forwardPin, 0);
    analogWrite(reversePin, pwm);
  }
}

int speedToPwm(float speedMps) {
  if (SPEED_TABLE_COUNT <= 0) {
    return 0;
  }
  if (speedMps <= SPEED_TABLE_MPS[0]) {
    return PWM_TABLE[0];
  }

  for (int i = 1; i < SPEED_TABLE_COUNT; i += 1) {
    if (speedMps <= SPEED_TABLE_MPS[i]) {
      const float speed0 = SPEED_TABLE_MPS[i - 1];
      const float speed1 = SPEED_TABLE_MPS[i];
      const int pwm0 = PWM_TABLE[i - 1];
      const int pwm1 = PWM_TABLE[i];

      if (speed1 <= speed0) {
        return pwm1;
      }

      const float u = (speedMps - speed0) / (speed1 - speed0);
      return (int)(pwm0 + (pwm1 - pwm0) * u + 0.5f);
    }
  }

  return PWM_TABLE[SPEED_TABLE_COUNT - 1];
}

void emergencyStop() {
  driveState = DRIVE_STOPPED;
  motionState = MOTION_STATIONARY;
  maneuverStartCurvature = 0.0f;
  maneuverTargetCurvature = 0.0f;
  maneuverTransitionTimeS = 0.0f;
  maneuverStartMs = millis();
  turnRecenterAtMs = 0;
  queueCount = 0;
  queueHead = 0;
  stopMotors();
}

void stopMotors() {
  analogWrite(LEFT_FORWARD_PWM_PIN, 0);
  analogWrite(LEFT_REVERSE_PWM_PIN, 0);
  analogWrite(RIGHT_FORWARD_PWM_PIN, 0);
  analogWrite(RIGHT_REVERSE_PWM_PIN, 0);
}
