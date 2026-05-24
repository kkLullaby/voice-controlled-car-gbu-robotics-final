// 超声波模块引脚
const int ECHO_pin = 12;
const int TRIG_pin = 13;

// 电机控制引脚（核心）
const int IB_LEFT_pin = 3;
const int IA_LEFT_pin = 5;
const int IB_RIGHT_pin = 9;
const int IA_RIGHT_pin = 10;

// 循迹模块引脚（本次不用，保留）
const int L_SENSOR_EN_pin = 2;
const int R_SENSOR_EN_pin = A5;
const int L_SENSOR_OUT_pin = A1;
const int R_SENSOR_OUT_pin = A0;

// 光和音频输出引脚（本次不用，保留）
const int SPEAKER_pin = 8;
const int LED2_pin = 11;
const int LED3_pin = 6;

// 传感器输入引脚（本次不用，保留）
const int LIGHT_pin = A4;
const int THERM_pin = A3;
const int MIC_pin = A2;
const int BADGE_pin = A7;
const int VIBRATION_pin = A6;

// 蓝牙模块控制引脚（本次不用，保留）
const int STATE_pin = 7;
const int EN_pin = 4;

// 蓝牙软串口（修复：原rx_pin=13与TRIG引脚冲突，修改为无用引脚）
const int tx_pin = 11;
const int rx_pin = 0; 

// 速度（0-255，保留你的偏心校正参数）
const int SPEED1 = 100;  // 左轮速度
const int SPEED2 = 120;  // 右轮速度

// 避障阈值：前方距离小于20cm开始避障（可自行修改）
const int AVOID_DISTANCE = 20;

void setup() {
  Serial.begin(9600); // 串口调试（可选）
  // 电机引脚设为输出
  pinMode(IA_LEFT_pin, OUTPUT);
  pinMode(IB_LEFT_pin, OUTPUT);
  pinMode(IA_RIGHT_pin, OUTPUT);
  pinMode(IB_RIGHT_pin, OUTPUT);
  
  // 初始化超声波引脚
  pinMode(TRIG_pin, OUTPUT);
  pinMode(ECHO_pin, INPUT);
}

// 读取超声波距离函数，返回值：厘米（cm）
float readUltrasonic() {
  // 发送触发信号
  digitalWrite(TRIG_pin, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_pin, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_pin, LOW);
  
  // 读取回波时间，超时时间设置为30000us（约5米）
  long time = pulseIn(ECHO_pin, HIGH, 30000); 
  // 换算距离：距离(cm) = 时间(us) * 0.034 / 2
  float distance = time * 0.017; 
  return distance;
}

// 走直线：保留你的原有逻辑
void goStraight() {
  // 左轮前进
  digitalWrite(IA_LEFT_pin, LOW);
  digitalWrite(IB_LEFT_pin, HIGH);
  analogWrite(IB_LEFT_pin, SPEED1);

  // 右轮前进
  digitalWrite(IA_RIGHT_pin, HIGH);
  digitalWrite(IB_RIGHT_pin, LOW);
  analogWrite(IB_RIGHT_pin, SPEED2);
}

// 停车
void stopCar() {
  digitalWrite(IA_LEFT_pin, LOW);
  digitalWrite(IB_LEFT_pin, LOW);
  digitalWrite(IA_RIGHT_pin, LOW);
  digitalWrite(IB_RIGHT_pin, LOW);
  analogWrite(IB_LEFT_pin, 0);
  analogWrite(IB_RIGHT_pin, 0);
}

// 后退函数
void backCar() {
  // 左轮后退
  digitalWrite(IA_LEFT_pin, HIGH);
  digitalWrite(IB_LEFT_pin, LOW);
  analogWrite(IB_LEFT_pin, SPEED1);

  // 右轮后退
  digitalWrite(IA_RIGHT_pin, LOW);
  digitalWrite(IB_RIGHT_pin, HIGH);
  analogWrite(IB_RIGHT_pin, SPEED2);
}

// 左转函数（避障用）
void turnLeft() {
  // 左轮静止，右轮前进 → 原地左转
  digitalWrite(IA_LEFT_pin, LOW);
  digitalWrite(IB_LEFT_pin, LOW);
  analogWrite(IB_LEFT_pin, 0);

  digitalWrite(IA_RIGHT_pin, HIGH);
  digitalWrite(IB_RIGHT_pin, LOW);
  analogWrite(IB_RIGHT_pin, SPEED2);
}

void loop() {
  // 获取前方距离
  float dist = readUltrasonic();
  
  // 距离大于阈值 或 测距失败 → 直行
  if (dist > AVOID_DISTANCE || dist == 0) {
    goStraight();
  } 
  // 距离过近 → 执行避障逻辑
  else {
    stopCar();   // 停车
    delay(200);  
    backCar();   // 后退
    delay(500);  
    stopCar();   // 停车
    delay(200);
    turnLeft();  // 左转
    delay(600);
    stopCar();   // 停车
    delay(200);
  }
}