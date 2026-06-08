/*
  Front UNO controller for the articulated four-wheel voice car.

  This sketch is open-loop: it computes target wheel linear speeds from
  geometry, then maps those speeds to PWM by a calibration table.
  Fill the physical parameters and the speed-to-PWM table after the car is built.
*/

#include <Arduino.h>
#include <math.h>

// =======================
// Board identity
// =======================

// Human-readable board name. Unit: none
const char BOARD_NAME[] = "FRONT";

// Distance offset of this wheel module along the planned vehicle path.
// Front board: 0.0 m
// Rear board: VEHICLE_LENGTH_M
// Unit: meter (m)
const float PATH_OFFSET_M = 0.000f;

// =======================
// Physical parameters
// =======================

// Distance between left and right wheel contact centers on one UNO module.
// Unit: meter (m)
const float WHEEL_TRACK_M = 0.160f;

// Distance between front wheel axle center and rear wheel axle center.
// Unit: meter (m)
const float VEHICLE_LENGTH_M = 0.320f;

// Desired path radius of the module center during steady turning.
// Unit: meter (m)
const float TURN_RADIUS_M = 0.800f;

// Smooth transition time for entering a turn.
// Unit: second (s)
const float ENTER_TURN_TIME_S = 1.200f;

// Smooth transition time for exiting a turn.
// Unit: second (s)
const float EXIT_TURN_TIME_S = 1.200f;

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
// Unit: millisecond (ms)
const unsigned long COMMAND_TIMEOUT_MS = 3000;

// =======================
// Runtime state
// =======================

enum DriveState {
  DRIVE_STOPPED,
  DRIVE_ACTIVE,
};

DriveState driveState = DRIVE_STOPPED;
int speedLevelIndex = DEFAULT_SPEED_LEVEL_INDEX;
int driveDirection = 1;

float maneuverStartCurvature = 0.0f;
float maneuverTargetCurvature = 0.0f;
float maneuverTransitionTimeS = 0.0f;

unsigned long maneuverStartMs = 0;
unsigned long lastCommandMs = 0;
unsigned long lastControlMs = 0;

const int SPEED_LEVEL_COUNT = sizeof(SPEED_LEVELS_MPS) / sizeof(SPEED_LEVELS_MPS[0]);
const int SPEED_TABLE_COUNT = sizeof(SPEED_TABLE_MPS) / sizeof(SPEED_TABLE_MPS[0]);

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

  emergencyStop();
}

void loop() {
  readBluetoothCommands();

  const unsigned long now = millis();
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
    handleCommand(command);
  }
}

void handleCommand(char command) {
  switch (command) {
    case 'F':
      lastCommandMs = millis();
      driveDirection = 1;
      driveState = DRIVE_ACTIVE;
      beginCurvatureManeuver(0.0f);
      break;
    case 'B':
      lastCommandMs = millis();
      driveDirection = -1;
      driveState = DRIVE_ACTIVE;
      beginCurvatureManeuver(0.0f);
      break;
    case 'L':
      lastCommandMs = millis();
      driveDirection = 1;
      driveState = DRIVE_ACTIVE;
      beginCurvatureManeuver(turnCurvature(+1));
      break;
    case 'R':
      lastCommandMs = millis();
      driveDirection = 1;
      driveState = DRIVE_ACTIVE;
      beginCurvatureManeuver(turnCurvature(-1));
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

  const float elapsedS = (nowMs - maneuverStartMs) / 1000.0f;
  const float localTimeS = elapsedS - pathDelaySeconds();

  if (localTimeS <= 0.0f) {
    return maneuverStartCurvature;
  }
  if (localTimeS >= maneuverTransitionTimeS) {
    return maneuverTargetCurvature;
  }

  const float u = localTimeS / maneuverTransitionTimeS;
  const float h = smootherStep(u);
  return maneuverStartCurvature + (maneuverTargetCurvature - maneuverStartCurvature) * h;
}

float pathDelaySeconds() {
  float speedMps = selectedSpeedMps();
  if (speedMps < MIN_DELAY_SPEED_MPS) {
    speedMps = MIN_DELAY_SPEED_MPS;
  }
  return PATH_OFFSET_M / speedMps;
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
  }
}

void decreaseSpeedLevel() {
  if (speedLevelIndex > 0) {
    speedLevelIndex -= 1;
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
  maneuverStartCurvature = 0.0f;
  maneuverTargetCurvature = 0.0f;
  maneuverTransitionTimeS = 0.0f;
  maneuverStartMs = millis();
  stopMotors();
}

void stopMotors() {
  analogWrite(LEFT_FORWARD_PWM_PIN, 0);
  analogWrite(LEFT_REVERSE_PWM_PIN, 0);
  analogWrite(RIGHT_FORWARD_PWM_PIN, 0);
  analogWrite(RIGHT_REVERSE_PWM_PIN, 0);
}
