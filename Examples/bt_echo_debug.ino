// bt_echo_debug.ino
//
// 蓝牙接收调试 sketch — 不控制电机，只验证 HC-04 收到的字符。
//
// 接线（定制板，实测确认）：
//   HC-04 TXD -> Arduino pin 0 (RX0)   ← 硬件串口 RX
//   HC-04 RXD -> Arduino pin 1 (TX1)   ← 硬件串口 TX
//   电源/GND 由定制板内部接好
//
// 注意：这块定制板把 HC-04 接在硬件 Serial 上，与 USB 串口监视器共用。
// 这意味着：
//   1. 烧 sketch 时，HC-04 字节可能干扰 bootloader 同步 — 必要时多按几次"上传"
//   2. 串口监视器不要在输入框里打字 — 那些字符会经 USB 跑到 Arduino，
//      和 HC-04 来的字节混在一起无法区分
//   3. Serial.print 的输出会同时经 USB 给监视器看（正常）也经 HC-04 反向
//      传给 PC（无害，PC 端不读就行）
//
// 调试方法：
//   1. 烧好后打开 Arduino IDE 串口监视器，波特率 9600，结束符"无"
//   2. 等监视器打出 "=== bt_echo_debug ready ==="（如果开监视器时 sketch
//      已经跑过，按 RESET 按钮重启 sketch 再看）
//   3. 在另一个终端先建蓝牙长连接（前台阻塞）：
//        sudo rfcomm connect 0 04:25:01:21:00:12 1
//      看到 "Connected /dev/rfcomm0..." + HC-04 LED 常亮再继续
//   4. 再开一个终端发命令：
//        python -m pc_voice_controller.main --mode manual-code --ports /dev/rfcomm0
//      输入 F 回车，监视器应该看到：
//        [BT] got 'F' (0x46) -> ACK
//        [BT] skip '\n' (0x0A)

const unsigned long IDLE_REPORT_INTERVAL_MS = 2000;
unsigned long lastCharMs = 0;
unsigned long lastIdleReportMs = 0;
unsigned long charCount = 0;

bool isCommandCode(char c) {
  return c == 'F' || c == 'B' || c == 'L' || c == 'R'
      || c == 'S' || c == 'U' || c == 'D';
}

void printChar(const char* tag, char c) {
  Serial.print(F("[BT] "));
  Serial.print(tag);
  Serial.print(F(" '"));
  if (c == '\r')      Serial.print(F("\\r"));
  else if (c == '\n') Serial.print(F("\\n"));
  else if (c == ' ')  Serial.print(F(" "));
  else                Serial.print(c);
  Serial.print(F("' (0x"));
  if ((unsigned char)c < 0x10) Serial.print('0');
  Serial.print((unsigned char)c, HEX);
  Serial.println(')');
}

void setup() {
  Serial.begin(9600);   // 硬件串口：同时挂着 USB 监视器和 HC-04

  Serial.println();
  Serial.println(F("=== bt_echo_debug ready ==="));
  Serial.println(F("Reading from hardware Serial (HC-04 on RX0) @ 9600"));
  Serial.println(F("Waiting for HC-04 to forward bytes..."));
  lastCharMs = millis();
  lastIdleReportMs = millis();
}

void loop() {
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    lastCharMs = millis();
    charCount++;

    if (isCommandCode(c)) {
      printChar("got ", c);
      Serial.println(F("       -> ACK"));
      // ACK 字节也通过同一个硬件 Serial 发回去 → 走 TX1 → HC-04 → PC
      // 注意 Serial.print 的所有输出（包括 [BT] 行）都会被发到蓝牙端。
      // PC 端目前不读，所以无害。
    } else if (c == '\r' || c == '\n' || c == ' ') {
      printChar("skip", c);
    } else {
      printChar("BAD ", c);
    }
  }

  unsigned long now = millis();
  if (now - lastIdleReportMs > IDLE_REPORT_INTERVAL_MS) {
    lastIdleReportMs = now;
    Serial.print(F("[idle] total_chars="));
    Serial.print(charCount);
    Serial.print(F(" ms_since_last_char="));
    Serial.println(now - lastCharMs);
  }
}
