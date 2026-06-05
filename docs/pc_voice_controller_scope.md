# PC Voice Controller Scope

本目录负责期末项目中的电脑端语音到蓝牙指令传输。Arduino 车端电机控制、四轮接线、双板供电和机械结构由电控同学负责，但本仓库需要把双方接口写清楚。

## Boundary

```text
电脑麦克风
  -> DashScope ASR
  -> 中文命令解析
  -> 7 个动作码
  -> 蓝牙串口发送到 1 或 2 块 Arduino
```

## Command Protocol

电脑端发送单字符动作码，并追加换行：

```text
F\n  前进
B\n  后退
L\n  左转
R\n  右转
S\n  停止
U\n  加速
D\n  减速
```

方案 A 使用前后两块 Arduino 板：电脑同时向两个蓝牙串口广播同一条动作码。

```text
/dev/rfcomm0 或 COMx -> 蓝牙 A -> 前轮 Arduino
/dev/rfcomm1 或 COMy -> 蓝牙 B -> 后轮 Arduino
```

单板调试时只配置一个端口，双板联调时配置两个端口。电脑端不关心前板/后板的内部电机接线，只保证两块板收到同一条动作码。

## First Version Decisions

- 语音识别复用参考项目的 DashScope `paraformer-realtime-v2` 流式 ASR；浏览器原生 SpeechRecognition 作为 web 面板备选（不依赖 PyAudio 和 DashScope key）。
- 命令解析使用关键词规则，不使用 LLM，降低延迟和不确定性。
- 蓝牙按串口设备处理。Linux 默认端口为 `/dev/rfcomm0,/dev/rfcomm1`；Windows 使用 `COM5,COM6` 这类端口；波特率 `9600`。
- 蓝牙发送器**默认使用长连接策略**（`close_after_send=False`）：开一次串口后持续 write，写失败才 close + 下次自动 reopen。
  - 早期版本是短连接（每次 open/close），原因是误信"HC-04 长连接第二次写入 Input/output error"的传言。Linux 实测的真相相反：短连接每次都重做 RFCOMM/SPP 握手，HC-04 还没握手完 close 就把链路扯了，字符发不出去（监视器观察 `total_chars=0`）。长连接稳定。
- **Linux 上必须配合 `tools/rfcomm_keepalive.sh` 持有 SPP 链路**：因为 `rfcomm bind` 是按需连接，Python 一次 open 触发 RFCOMM 建链，但 HC-04 SPP 握手要数百毫秒——光靠按需连接每次都会丢字。Windows 不需要这一步，COM 口由 Windows 蓝牙栈自动维持。
- 对流式 ASR 重复文本做防抖：默认 1 秒内不重复发送同一动作码。
- 未识别到 7 个动作之一时不发送任何蓝牙指令。

## Implementation Order

1. 纯文本命令解析。✅
2. 假串口和 dry-run 验证发送协议。✅
3. 手动中文输入模式验证 `文本 -> 动作码 -> 蓝牙发送`。✅
4. 手动动作码模式验证真实蓝牙链路。✅
5. 接入麦克风和 DashScope ASR（或浏览器原生 SpeechRecognition）。✅
6. 单蓝牙模块联调通过后，再切换到双蓝牙端口。✅
7. 电控同学在 Arduino 端加入接收超时保护，避免某个蓝牙断开后一块板继续动作。✅（`Examples/car_bluetooth_drive.ino` 已实现 1s 超时自动停车）
8. 真车装电机 + 装电池后按 `docs/hardware_bringup.md` 6 阶段流程联调。**进行中**

## Web Panel

`web_panel.server` 提供本地控制面板：

```bash
python -m web_panel.server --host 127.0.0.1 --port 8765
```

面板复用同一套解析和发送模块，默认 dry-run。浏览器语音按钮使用浏览器自带中文语音识别，DashScope 按钮使用后端麦克风 ASR。真实双蓝牙发送时使用：

```bash
python -m web_panel.server --host 127.0.0.1 --port 8765 --real-bluetooth --bt-ports /dev/rfcomm0,/dev/rfcomm1
```

单板测试：

```bash
python -m web_panel.server --host 127.0.0.1 --port 8765 --real-bluetooth --bt-ports /dev/rfcomm0
```

Windows 双板测试：

```powershell
python -m web_panel.server --host 127.0.0.1 --port 8765 --real-bluetooth --bt-ports COM5,COM6
```

## Arduino-Side Expectations

每块 Arduino 只需要接收自己的蓝牙串口数据，并执行同一套动作码：

```text
F/B/L/R/S 持续运动状态
U/D       一次性调速动作
```

电控端必须实现（**已在 `Examples/car_bluetooth_drive.ino` 落地**）：

- 上电默认停止。
- 收到非法字符忽略。
- 收到 `S` 立即停止。
- 超过 1000ms 没收到任何有效命令时自动停止。
- 先拔掉电机或悬空车轮测试串口，再接电机测试运动。

**重要硬件约束**：本项目用的 Arduino 定制板把 HC-04 直接接在硬件 Serial（pin 0/1）上，**不是** SoftwareSerial。所以 sketch 用 `Serial.read()` 直接读字节。这也带来几个副作用：

- 烧 sketch 时 HC-04 字节流会偶尔干扰 bootloader 同步，要预先停掉 `rfcomm_keepalive`，烧失败 retry 几次也无害
- `Serial.print` 的所有输出都会经 HC-04 反向传回 PC，所以生产 sketch 默认 `DEBUG_PRINTS=0` 静默
- Arduino IDE 串口监视器**不能在输入框打字**——会经 USB 跑到 Arduino 跟蓝牙字符混淆

电控同学的工作流详见 [motor_debug_handoff.md](motor_debug_handoff.md)。
