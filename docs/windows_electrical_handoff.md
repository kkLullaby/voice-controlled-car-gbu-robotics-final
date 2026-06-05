# Windows 电控开发交接指南

> **更新（2026-06）**：PC 端（Linux）已经完成所有联调，包括双板真实蓝牙广播、浏览器语音整链路。Arduino 端的接收和电机控制 sketch 也已经写好并实测过 PC 端字节到位（`Examples/car_bluetooth_drive.ino`）。**剩下只是真车装电机+装电池+落地测试**。
>
> 如果你在 Windows 上操作，看这份文档。如果在 Linux 上操作，看 [motor_debug_handoff.md](motor_debug_handoff.md)（更详细，覆盖了本项目实测踩过的所有坑）。
> 整车 bring-up 流程看 [hardware_bringup.md](hardware_bringup.md)。

这份文档给负责 Arduino / 电控 / 小车端的同学使用。电脑端已经实现语音识别、中文命令解析和蓝牙串口发送；电控端需要保证两块 Arduino 能稳定接收并执行动作码。

## 1. 项目分工

电脑端负责：

```text
语音或按钮
  -> F/B/L/R/S/U/D 动作码
  -> 蓝牙串口发送
```

电控端负责：

```text
蓝牙模块接收串口字符
  -> Arduino 解析动作码
  -> 控制电机驱动
  -> 必要时做避障和安全停车
```

本项目采用方案 A：

```text
电脑
  ├── 蓝牙 A -> Arduino A -> 前左/前右
  └── 蓝牙 B -> Arduino B -> 后左/后右
```

电脑会给两块板发送同一条动作码。两块 Arduino 不需要互相通信。

## 2. 串口协议

电脑发送 ASCII 单字符动作码，并追加换行：

```text
F\n  前进
B\n  后退
L\n  左转
R\n  右转
S\n  停止
U\n  加速
D\n  减速
```

Arduino 端建议按字符处理，忽略换行：

```text
有效字符：F B L R S U D
忽略字符：\r \n 空格 其他未知字符
```

## 3. Windows 蓝牙配对和 COM 口

1. 给蓝牙模块和 Arduino 上电。
2. Windows 打开：

```text
设置 -> 蓝牙和设备 -> 添加设备 -> 蓝牙
```

3. 配对蓝牙模块。常见 PIN：

```text
1234
0000
```

4. 打开设备管理器：

```text
设备管理器 -> 端口 (COM 和 LPT)
```

5. 找到蓝牙串口对应的 COM 口，例如：

```text
Standard Serial over Bluetooth link (COM5)
Standard Serial over Bluetooth link (COM6)
```

6. 单板测试时只记一个端口：

```text
COM5
```

7. 双板联调时记两个端口：

```text
COM5,COM6
```

如果同名蓝牙模块太多，建议先只开一个模块配对，改名或贴标签后再配第二个。

## 4. Windows 电脑端测试命令

进入项目目录后安装依赖：

```powershell
py -m venv .venv
.\.venv\Scripts\Activate.ps1
pip install -r requirements.txt
```

启动 Web 控制台，单板真实蓝牙：

```powershell
python -m web_panel.server --host 127.0.0.1 --port 8765 --real-bluetooth --bt-ports COM5
```

双板真实蓝牙：

```powershell
python -m web_panel.server --host 127.0.0.1 --port 8765 --real-bluetooth --bt-ports COM5,COM6
```

打开浏览器：

```text
http://127.0.0.1:8765
```

命令行直接测试：

```powershell
python -m pc_voice_controller.main --mode manual-code --ports COM5
```

输入：

```text
S
F
S
B
S
L
S
R
S
```

测试顺序一定先发 `S`，确认停车命令能收到。

## 5. Arduino 接收逻辑（**已经实现**）

电控端的接收+电机控制 sketch 已经写好：`Examples/car_bluetooth_drive.ino`。它已经包含：

- F/B/L/R/S/U/D 七个命令处理
- 三档速度（U/D 切换）
- 1 秒超时自动停车
- 非法字符忽略
- HC-04 接**硬件 Serial pin 0/1**（注意定制板的特殊接法，不是 SoftwareSerial）

直接在 Arduino IDE 打开烧到两块板上即可。烧之前注意把 PC 端的 `rfcomm_keepalive`（Linux）或对应的串口监视器关掉，HC-04 在 RX0 上有时会干扰 bootloader 同步。

如果你想理解 sketch 的实现细节或者改电机方向，看 [motor_debug_handoff.md](motor_debug_handoff.md) §3-§5。

## 5.5. Arduino 接收逻辑（最小参考实现）

如果你要自己另写 sketch 或调试，下面是协议层最小参考：

电控端每块板的逻辑应该是：

```text
setup:
  初始化蓝牙串口
  初始化电机引脚
  默认 stopCar()

loop:
  如果串口有数据:
    读取字符
    如果是 F/B/L/R/S/U/D:
      更新 lastCommandMs
      执行动作或更新运动状态

  如果 millis() - lastCommandMs > 800~1000:
    stopCar()
```

最小伪代码：

```cpp
char currentCommand = 'S';
unsigned long lastCommandMs = 0;
const unsigned long COMMAND_TIMEOUT_MS = 1000;

void loop() {
  while (Serial.available() > 0) {
    char c = Serial.read();
    if (c == '\n' || c == '\r' || c == ' ') {
      continue;
    }
    if (c == 'F' || c == 'B' || c == 'L' || c == 'R' || c == 'S' || c == 'U' || c == 'D') {
      currentCommand = c;
      lastCommandMs = millis();
      handleCommand(c);
    }
  }

  if (millis() - lastCommandMs > COMMAND_TIMEOUT_MS) {
    currentCommand = 'S';
    stopCar();
  }
}
```

如果使用 `SoftwareSerial`，把 `Serial` 换成蓝牙软串口对象。注意不要和超声波、LED、电机 PWM 引脚冲突。

## 6. 电机和安全要求

上电安全：

- Arduino 上电后默认停止。
- 蓝牙未连接时默认停止。
- 串口断开或 1 秒内没有新命令时自动停止。
- 收到未知字符不动作。
- 收到 `S` 立即停止，不等待当前动作结束。

调试安全：

- 第一次串口测试时拔掉电机或悬空车轮。
- 先只测一块板。
- 每个动作后立刻发 `S`。
- 确认方向正确后再上地测试。
- 双板联调前，分别确认两块板单独可控。

## 7. 前后两块板的动作一致性

方案 A 下，两块板执行同一条命令：

```text
F -> 前板左右轮前进，后板左右轮前进
B -> 前板左右轮后退，后板左右轮后退
L -> 前板左转动作，后板左转动作
R -> 前板右转动作，后板右转动作
S -> 两块板都停止
U -> 两块板都加速一档
D -> 两块板都减速一档
```

如果前后板安装方向相反，电控端应该在 Arduino 代码里修正电机方向，不要让电脑端为前板/后板发送不同命令。

## 8. 已知电脑端实现细节

电脑端使用 Python `pyserial` 写串口。

**Linux 实测后采用长连接策略**（`close_after_send=False`，2026-06 改动）：

```text
程序启动时：
  open COM 口（保持开着）

每条命令：
  write "F\n"
  flush

write 失败时才 close + 下次自动重新 open。
```

早期版本因为传言"HC-04 长连接第二次写入 Input/output error"而用了短连接，但 Linux 实测的真实坑相反——短连接每次都重做 RFCOMM/SPP 握手，HC-04 字节根本来不及发出去。

Windows 上 COM 口由系统蓝牙栈维持，长/短连接行为差异不像 Linux 那么明显。如果你在 Windows 上测发现"PC 端报 ok 但 Arduino 没收到"，可以试改回短连接（`BluetoothConfig(close_after_send=True)`），但目前没在 Windows 上观察到这个问题。

对电控端无影响，Arduino 只会看到正常串口字符。

## 9. 联调验收标准

单板：

```text
发送 S -> 板子收到并停止
发送 F -> 对应两个轮子前进
发送 B -> 对应两个轮子后退
发送 L/R -> 转向动作正确
发送 U/D -> 速度档位变化
拔掉蓝牙或停止发送 1 秒 -> 自动停车
```

双板：

```text
发送 F -> 前后两块板都前进
发送 S -> 前后两块板都停止
断开其中一个蓝牙 -> 断开的板 1 秒内自动停止，另一块板继续可控或也由电脑端发 S 停止
```

## 10. 常见问题

看不到 COM 口：

- 重新配对蓝牙模块。
- 确认模块是经典蓝牙串口，不是 BLE-only。
- 在设备管理器里查看 `端口 (COM 和 LPT)`。

COM 口能打开但车没反应：

- 确认 Arduino 端波特率是 `9600`。
- 确认蓝牙 TX/RX 交叉连接。
- 确认 GND 共地。
- 确认 Arduino 端忽略了 `\n`，没有把换行当非法命令导致状态错乱。

动作方向反了：

- 在 Arduino 电机控制函数里反转对应电机方向。
- 不要改电脑端动作码协议。

一块板能动，另一块不能动：

- 分别用单端口模式测试 `COM5` 和 `COM6`。
- 确认两块板烧录的是同一套接收协议。
- 确认电脑端 `--bt-ports COM5,COM6` 中端口写对。
