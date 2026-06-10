/*
  Front UNO controller for the 4-wheel skid-steer car.

  Chassis model: rigid 4-wheel car, 4 fixed-direction wheels, no steering
  linkage. Turning is achieved entirely by differential PWM between the
  left and right sides ("skid steering" / "tank steering").

  Wiring: this board drives the two FRONT wheels (left + right).
  The rear board drives the two rear wheels independently with the same
  sketch (only BOARD_NAME and the per-wheel trim/direction values differ).

  Because the chassis is rigid and the front/rear axles are coaxial in
  the body frame, the front-left and rear-left wheels should always be
  commanded to the same speed, and likewise for the right side. So each
  board runs the same kinematics on the same Bluetooth command stream
  and the chassis stays coordinated — no inter-board sync needed, no
  delay queue, no path offset.

  Bluetooth-serial command set (single ASCII char each):
    F  drive forward            U  speed level up
    B  drive backward           D  speed level down
    L  arc-turn left            S  stop
    R  arc-turn right

  Behavior rules (kept from the previous car):
    - MotionState (STATIONARY/MOVING): in STATIONARY only F/B/S take
      effect; L/R/U/D are silently ignored.
    - L/R smoothly enter the target curvature, hold it for
      TURN_HOLD_TIME_S, then auto-recenter back to straight — a single
      turn command does not lock the car into an endless circle.
    - F<->B reversal inserts a brief motor-off pause to protect the
      H-bridge from a hard polarity flip while current is still flowing.
    - If no valid command is received for COMMAND_TIMEOUT_MS the car
      stops. WARNING: 20s is long; keep the emergency-stop / battery
      switch within reach during testing.
*/

#include <Arduino.h>
#include <math.h>

// =======================
// Board identity
// =======================

// Human-readable board name. Unit: none
const char BOARD_NAME[] = "FRONT";

// =======================
// Physical parameters
// =======================

// Distance between left and right wheel contact centers.
// Unit: meter (m)
const float WHEEL_TRACK_M = 0.105f;

// Nominal path radius of the chassis center during a steady L/R turn.
// Smaller value -> sharper turn. With skid steering this is an "intent"
// value, not a precisely measured outcome (the inner/outer wheels will
// slip a little against the ground).
// At 0.05 m combined with MIN_INNER_SPEED_RATIO = -1, the inner wheel
// actually reverses — i.e. close to an in-place spin. Increase toward
// 0.10~0.15 for a wider arc, decrease toward 0.03 for an even sharper
// spin.
// Unit: meter (m)
const float TURN_RADIUS_M = 0.050f;

// Smooth transition time for entering a turn. Kept short so the
// differential bite shows up quickly; with smootherStep, the first
// ~25% of this window only reaches ~10% of the target curvature.
// Unit: second (s)
const float ENTER_TURN_TIME_S = 0.300f;

// Smooth transition time for exiting a turn (returning to straight).
// Unit: second (s)
const float EXIT_TURN_TIME_S = 0.300f;

// How long L/R holds the target curvature after the entry transition
// finishes, before auto-recentering. A single L/R command produces:
// enter (ENTER_TURN_TIME_S) -> hold (this) -> exit (EXIT_TURN_TIME_S)
// -> continue straight.
// Split into left/right because the chassis is open-loop and the two
// sides rarely produce equal yaw rates — usually because of motor/trim
// asymmetry or uneven floor friction. Tune: shrink the side that
// over-turns, grow the side that under-turns.
// Unit: second (s)
const float TURN_HOLD_TIME_LEFT_S  = 4.000f;
const float TURN_HOLD_TIME_RIGHT_S = 3.000f;

// Minimum inner-wheel speed ratio allowed during a turn.
//   +0.60  inner wheel must stay at >=60% of outer (gentle arc)
//    0.00  inner wheel may stop entirely (sharp arc, outer-only push)
//   -1.00  inner wheel may fully reverse at outer's speed (in-place spin)
// The clamp inside clampTurnCurvature still leaves a tiny epsilon so
// the denominator stays nonzero.
// Unit: none
const float MIN_INNER_SPEED_RATIO = -1.000f;

// =======================
// Open-loop speed settings
// =======================

// Available chassis-center speeds.
// Unit: meter per second (m/s)
const float SPEED_LEVELS_MPS[] = {
  0.100f,
  0.150f,
  0.200f,
};

// Default speed level index in SPEED_LEVELS_MPS.
// Unit: none
const int DEFAULT_SPEED_LEVEL_INDEX = 1;

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

// Per-wheel open-loop correction multipliers (compensate for motor
// variation so the two sides actually run at the commanded speed).
// Unit: none
const float LEFT_WHEEL_TRIM = 0.880f;
const float RIGHT_WHEEL_TRIM = 1.100f;

// Motor direction correction. Use 1 if the wheel turns the "right" way
// when given forward PWM, -1 if it spins backward (wired in reverse).
// Unit: none
const int LEFT_MOTOR_DIR = -1;
const int RIGHT_MOTOR_DIR = 1;

// =======================
// Pin configuration
// =======================

// Bluetooth serial baud rate.
// Unit: bit per second (baud)
const unsigned long BLUETOOTH_BAUD = 9600;

// Left motor forward PWM pin.
const int LEFT_FORWARD_PWM_PIN = 5;

// Left motor reverse PWM pin.
const int LEFT_REVERSE_PWM_PIN = 3;

// Right motor forward PWM pin.
const int RIGHT_FORWARD_PWM_PIN = 10;

// Right motor reverse PWM pin.
const int RIGHT_REVERSE_PWM_PIN = 9;

// =======================
// Safety and timing
// =======================

// Control-loop update period.
// Unit: millisecond (ms)
const unsigned long CONTROL_PERIOD_MS = 20;

// Stop the car if no valid Bluetooth command is received in this time.
// Unit: millisecond (ms)
const unsigned long COMMAND_TIMEOUT_MS = 20000;

// Brief motor-off pause inserted when reversing F<->B, to protect the
// H-bridge from a hard polarity flip while current is still flowing.
// Unit: millisecond (ms)
const unsigned long DIRECTION_REVERSAL_PAUSE_MS = 100;

// =======================
// Runtime state
// =======================

enum DriveState {
  DRIVE_STOPPED,
  DRIVE_ACTIVE,
};

// High-level motion state. STATIONARY = car not moving (boot, or after
// S). In STATIONARY only F/B/S are accepted; L/R/U/D are silently
// ignored, because turning or changing speed while stopped is
// meaningless in this control scheme.
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

// When the current turn (non-zero target curvature) should auto-recenter
// to straight. 0 means "no pending auto-recenter".
unsigned long turnRecenterAtMs = 0;

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

  // Auto-recenter: a previously-issued L/R has held its target curvature
  // long enough; transition back to straight and clear the latch.
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
    handleCommand(command);
  }
}

void handleCommand(char command) {
  // In STATIONARY, only F/B (start moving) and S (stay stopped) take
  // effect. L/R/U/D silently ignored — the car never starts moving from
  // a turn or speed-change command.
  if (motionState == MOTION_STATIONARY
      && command != 'F' && command != 'B' && command != 'S') {
    return;
  }

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
  // F<->B reversal protection: if we're already moving in the opposite
  // direction, stop the motors for a short pause before flipping. This
  // prevents the H-bridge from seeing a hard polarity flip while
  // current is still flowing through the motor windings.
  if (motionState == MOTION_MOVING && newDirection != driveDirection) {
    stopMotors();
    delay(DIRECTION_REVERSAL_PAUSE_MS);
  }
  driveDirection = newDirection;
  driveState = DRIVE_ACTIVE;
  motionState = MOTION_MOVING;
  // Starting (or re-asserting) straight motion cancels any pending turn.
  turnRecenterAtMs = 0;
  beginCurvatureManeuver(0.0f);
}

void beginTurn(int turnDirection) {
  // turnDirection +1 = left, -1 = right.
  beginCurvatureManeuver(turnCurvature(turnDirection));
  // Pick the hold time for this side. Splitting L/R lets you compensate
  // when one direction over-turns relative to the other.
  const float holdTimeS = (turnDirection >= 0)
      ? TURN_HOLD_TIME_LEFT_S
      : TURN_HOLD_TIME_RIGHT_S;
  // Schedule the auto-recenter for after enter-transition + hold.
  // We deliberately do NOT add EXIT_TURN_TIME_S here: the recenter just
  // *triggers* the exit transition, which then takes EXIT_TURN_TIME_S
  // to complete on its own.
  unsigned long holdMs =
      (unsigned long)(ENTER_TURN_TIME_S * 1000.0f)
    + (unsigned long)(holdTimeS * 1000.0f);
  turnRecenterAtMs = millis() + holdMs;
  if (turnRecenterAtMs == 0) {
    // 0 is the "no pending recenter" sentinel; bump by 1ms to avoid it.
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

  // Allow ratio to go negative (inner wheel reverses for in-place spin).
  // Lower bound -0.95 keeps the denominator (1 + ratio) safely away from
  // zero; the resulting maxAbsCurvature is already enormous.
  float ratio = MIN_INNER_SPEED_RATIO;
  if (ratio < -0.95f) {
    ratio = -0.95f;
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

  // Differential drive (skid-steer) split:
  //   v_left  = v_center * (1 - track/2 * curvature)
  //   v_right = v_center * (1 + track/2 * curvature)
  // Positive curvature = left turn (right wheel faster than left).
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
  motionState = MOTION_STATIONARY;
  maneuverStartCurvature = 0.0f;
  maneuverTargetCurvature = 0.0f;
  maneuverTransitionTimeS = 0.0f;
  maneuverStartMs = millis();
  turnRecenterAtMs = 0;
  stopMotors();
}

void stopMotors() {
  analogWrite(LEFT_FORWARD_PWM_PIN, 0);
  analogWrite(LEFT_REVERSE_PWM_PIN, 0);
  analogWrite(RIGHT_FORWARD_PWM_PIN, 0);
  analogWrite(RIGHT_REVERSE_PWM_PIN, 0);
}
