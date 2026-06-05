// car_bluetooth_drive.ino
//
// 蓝牙动作码 → 四轮电机控制。生产版（不是调试 sketch）。
//
// 接收协议（来自 pc_voice_controller / web_panel）：
//   F  前进     B  后退     L  左转    R  右转
//   S  停止     U  加速一档  D  减速一档
//
// 接线（本项目实测的定制板）：
//   HC-04 TXD -> Arduino pin 0 (RX0)  ← 硬件 Serial
//   HC-04 RXD -> Arduino pin 1 (TX1)
//   电机引脚 3/5/9/10（沿用 sketchcar_may19a 的接法）
//
// 安全：
//   - 上电默认 stopCar()
//   - 超过 COMMAND_TIMEOUT_MS 没收到有效命令 → 自动 stopCar
//   - 收到 S 立即停止，不等当前动作
//   - 收到非法字符忽略
//   - 收到 \r \n 空格 忽略
//
// 调试提示：HC-04 接在硬件 Serial 上，意味着所有 Serial.print 都会经过
// 蓝牙反向传给 PC。所以这个 sketch 默认 DEBUG_PRINTS=0；只在真要调试
// 时改成 1 重烧。

#define DEBUG_PRINTS 0

// 电机控制引脚（沿用 sketchcar_may19a.ino 的接法）
const int IB_LEFT_pin  = 3;
const int IA_LEFT_pin  = 5;
const int IB_RIGHT_pin = 9;
const int IA_RIGHT_pin = 10;

// 三档速度。U/D 调档时在这三档之间切换。
// 数值来自旧 sketch 的 SPEED1/SPEED2 校正基准 100/120。
const int SPEED_TABLE_LEFT[]  = {  70, 100, 140 };
const int SPEED_TABLE_RIGHT[] = {  85, 120, 170 };
const int SPEED_LEVEL_COUNT = 3;
const int DEFAULT_SPEED_LEVEL = 1;  // 中档（索引 1）

// 超过这么久没收到命令就自动停车
const unsigned long COMMAND_TIMEOUT_MS = 1000;

// 当前状态
char currentCommand = 'S';
int  speedLevel = DEFAULT_SPEED_LEVEL;
unsigned long lastCommandMs = 0;

// === 电机原语 ============================================================

void motorStop() {
  digitalWrite(IA_LEFT_pin,  LOW);
  digitalWrite(IB_LEFT_pin,  LOW);
  digitalWrite(IA_RIGHT_pin, LOW);
  digitalWrite(IB_RIGHT_pin, LOW);
  analogWrite(IB_LEFT_pin,  0);
  analogWrite(IB_RIGHT_pin, 0);
}

void motorForward() {
  int sL = SPEED_TABLE_LEFT[speedLevel];
  int sR = SPEED_TABLE_RIGHT[speedLevel];
  // 左轮前进
  digitalWrite(IA_LEFT_pin, LOW);
  digitalWrite(IB_LEFT_pin, HIGH);
  analogWrite(IB_LEFT_pin, sL);
  // 右轮前进
  digitalWrite(IA_RIGHT_pin, HIGH);
  digitalWrite(IB_RIGHT_pin, LOW);
  analogWrite(IB_RIGHT_pin, sR);
}

void motorBackward() {
  int sL = SPEED_TABLE_LEFT[speedLevel];
  int sR = SPEED_TABLE_RIGHT[speedLevel];
  digitalWrite(IA_LEFT_pin, HIGH);
  digitalWrite(IB_LEFT_pin, LOW);
  analogWrite(IB_LEFT_pin, sL);
  digitalWrite(IA_RIGHT_pin, LOW);
  digitalWrite(IB_RIGHT_pin, HIGH);
  analogWrite(IB_RIGHT_pin, sR);
}

void motorTurnLeft() {
  // 原地左转：左轮静、右轮前进
  int sR = SPEED_TABLE_RIGHT[speedLevel];
  digitalWrite(IA_LEFT_pin, LOW);
  digitalWrite(IB_LEFT_pin, LOW);
  analogWrite(IB_LEFT_pin, 0);
  digitalWrite(IA_RIGHT_pin, HIGH);
  digitalWrite(IB_RIGHT_pin, LOW);
  analogWrite(IB_RIGHT_pin, sR);
}

void motorTurnRight() {
  // 原地右转：右轮静、左轮前进
  int sL = SPEED_TABLE_LEFT[speedLevel];
  digitalWrite(IA_LEFT_pin, LOW);
  digitalWrite(IB_LEFT_pin, HIGH);
  analogWrite(IB_LEFT_pin, sL);
  digitalWrite(IA_RIGHT_pin, LOW);
  digitalWrite(IB_RIGHT_pin, LOW);
  analogWrite(IB_RIGHT_pin, 0);
}

// === 持续运动状态的重新应用 ==============================================
// 改速度档位（U/D）后需要把 SPEED 重新写一次到 PWM，否则不会变。
void reapplyCurrentMotion() {
  switch (currentCommand) {
    case 'F': motorForward();   break;
    case 'B': motorBackward();  break;
    case 'L': motorTurnLeft();  break;
    case 'R': motorTurnRight(); break;
    case 'S':
    default:  motorStop();      break;
  }
}

// === 命令处理 ============================================================

bool isCommandCode(char c) {
  return c == 'F' || c == 'B' || c == 'L' || c == 'R'
      || c == 'S' || c == 'U' || c == 'D';
}

void handleCommand(char c) {
  if (c == 'U') {
    if (speedLevel < SPEED_LEVEL_COUNT - 1) speedLevel++;
    reapplyCurrentMotion();
    return;
  }
  if (c == 'D') {
    if (speedLevel > 0) speedLevel--;
    reapplyCurrentMotion();
    return;
  }
  // F/B/L/R/S 是持续状态切换
  currentCommand = c;
  reapplyCurrentMotion();
}

// === Arduino 入口 ========================================================

void setup() {
  pinMode(IA_LEFT_pin,  OUTPUT);
  pinMode(IB_LEFT_pin,  OUTPUT);
  pinMode(IA_RIGHT_pin, OUTPUT);
  pinMode(IB_RIGHT_pin, OUTPUT);

  Serial.begin(9600);   // HC-04 = 硬件 Serial @ 9600
  motorStop();
  lastCommandMs = millis();

#if DEBUG_PRINTS
  Serial.println(F("[car] ready"));
#endif
}

void loop() {
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (!isCommandCode(c)) {
      // \r \n 空格 和其它垃圾全忽略，绝不打印（会经 HC-04 漏回 PC）
      continue;
    }
    lastCommandMs = millis();
    handleCommand(c);
#if DEBUG_PRINTS
    Serial.print(F("[car] cmd="));
    Serial.print(c);
    Serial.print(F(" lvl="));
    Serial.println(speedLevel);
#endif
  }

  // 通信掉线 / PC 端不再发命令 → 自动停车保平安
  if (millis() - lastCommandMs > COMMAND_TIMEOUT_MS && currentCommand != 'S') {
    currentCommand = 'S';
    motorStop();
#if DEBUG_PRINTS
    Serial.println(F("[car] timeout -> stop"));
#endif
  }
}
