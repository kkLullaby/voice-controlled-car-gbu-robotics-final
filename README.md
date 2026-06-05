# Voice Bluetooth Car Controller

电脑端语音到蓝牙动作码传输模块，用于期末四轮声控小车。当前仓库重点覆盖电脑端职责：

```text
语音 / 手动输入
  -> 中文命令解析
  -> F/B/L/R/S/U/D 动作码
  -> 蓝牙串口广播到 Arduino 板
```

## Current Status

**PC 端 + 蓝牙链路 + Arduino 接收/电机 sketch 已经全部跑通**。剩下的是物理安装电机+电池+车轮后的真车调试，详见 `docs/hardware_bringup.md`。

已完成：

- 中文命令解析：`前进`、`停止`、`左转` 等转为动作码。
- 命令行控制：支持手动动作码、手动中文、DashScope ASR 三种模式。
- Web 控制台：支持按钮、中文输入、浏览器原生语音（Chrome/Edge）、DashScope 启停入口。
- 蓝牙发送：单板和双板广播实测全通（双板烟测 26/26 字符）。
- HC-04 + Linux rfcomm 实测确认：发送器默认**长连接**策略（`close_after_send=False`），配合 `tools/rfcomm_keepalive.sh` 持有 SPP 链路。短连接每次都重做 RFCOMM 握手 HC-04 字节会丢；长连接稳定。
- 紧急停车工具 `tools/emergency_stop.sh`：并行向所有 `/dev/rfcomm*` 连发 S。
- 调试 sketch `Examples/bt_echo_debug.ino`：硬件 Serial 收字符并回显（验证"字节到没到 Arduino"）。
- **生产 sketch `Examples/car_bluetooth_drive.ino`**：F/B/L/R/S/U/D 完整电机控制 + 三档速度 + 1 秒超时自动停车，两块板都已烧好。

正在进行 / 待做：

- 真车装电机 + 装电池 + 装车轮（电控同学，按 `docs/hardware_bringup.md`）
- 落地慢速直线 + 转弯方向校正
- （可选）DashScope ASR 替代浏览器语音（需要 API key + PyAudio）

## Architecture

方案 A：前后两块板分别控制一组左右轮，电脑同时给两个蓝牙模块发同一条动作码。

```text
电脑
  ├── /dev/rfcomm0 或 COMx -> 蓝牙 A -> Arduino A -> 前左/前右
  └── /dev/rfcomm1 或 COMy -> 蓝牙 B -> Arduino B -> 后左/后右
```

单板测试时只配置一个端口：

```text
/dev/rfcomm0
```

双板联调时配置两个端口：

```text
/dev/rfcomm0,/dev/rfcomm1
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

Arduino 端应兼容有无换行，推荐按字符读取，忽略 `\r`、`\n`。

## Setup

Linux：

```bash
cd /home/kk/code/robot_class/finalproj_cowork
python -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
cp .env.example .env
```

Windows：

```powershell
cd path\to\finalproj_cowork
py -m venv .venv
.\.venv\Scripts\Activate.ps1
pip install -r requirements.txt
copy .env.example .env
```

编辑 `.env`：

```text
DASHSCOPE_API_KEY=你的 DashScope Key
BT_PORTS=/dev/rfcomm0,/dev/rfcomm1
BT_BAUD=9600
```

Windows 下把 `BT_PORTS` 改成设备管理器里的 COM 口，例如：

```text
BT_PORTS=COM5,COM6
```

### Linux 蓝牙首次配对（一次性）

如果是新机器从零开始，蓝牙模块没配对过：

```bash
# 1. 扫描确认能看到 HC-04
bluetoothctl scan on
# 等到看到 04:25:01:21:00:12 HC-04 出现，Ctrl+C 退

# 2. 配对 + 信任两块板
bluetoothctl pair 04:25:01:21:00:12 ; bluetoothctl trust 04:25:01:21:00:12
bluetoothctl pair 04:25:02:05:03:42 ; bluetoothctl trust 04:25:02:05:03:42
# PIN 输 1234 或 0000
```

之后每次启动只需要跑 `tools/rfcomm_keepalive.sh`，它会自动负责 bind 和持有 SPP 链路。

## Run

> **Linux 真实蓝牙：先拉起 rfcomm 守护**
>
> HC-04 在 Linux 上必须有进程持有 SPP 链路（`rfcomm bind` 的按需连接对 HC-04 不可靠）。所以**先在另一个终端窗口**跑：
>
> ```bash
> sudo ./tools/rfcomm_keepalive.sh 0:04:25:01:21:00:12 1:04:25:02:05:03:42
> ```
>
> 看到 `Connected /dev/rfcomm0 to ... on channel 1` + HC-04 LED 常亮，再去启动下面的发送命令。Ctrl+C 这个脚本会断开所有链路。
> 单板就只写一个参数：`0:04:25:01:21:00:12`。
> Windows 不需要这一步——COM 口由 Windows 蓝牙栈自动维持。

启动 Web 面板 dry-run，不碰真实蓝牙：

```bash
python -m web_panel.server --host 127.0.0.1 --port 8765
```

打开：

```text
http://127.0.0.1:8765
```

单板真实蓝牙测试：

```bash
python -m web_panel.server --host 127.0.0.1 --port 8765 --real-bluetooth --bt-ports /dev/rfcomm0
```

双板真实蓝牙广播：

```bash
python -m web_panel.server --host 127.0.0.1 --port 8765 --real-bluetooth --bt-ports /dev/rfcomm0,/dev/rfcomm1
```

Windows 示例：

```powershell
python -m web_panel.server --host 127.0.0.1 --port 8765 --real-bluetooth --bt-ports COM5,COM6
```

命令行手动动作码测试：

```bash
python -m pc_voice_controller.main --mode manual-code --ports /dev/rfcomm0
```

命令行中文 dry-run：

```bash
python -m pc_voice_controller.main --mode manual-text --dry-run
```

接入 DashScope 语音识别：

```bash
python -m pc_voice_controller.main --mode asr --ports /dev/rfcomm0,/dev/rfcomm1
```

## Bluetooth Notes

Linux 上经典蓝牙串口常见设备名是 `/dev/rfcomm0`、`/dev/rfcomm1`。检查方式：

```bash
ls /dev/rfcomm* /dev/ttyUSB* /dev/ttyACM* 2>/dev/null
rfcomm -a
```

HC-04/HC-05 如果没有出现 `/dev/rfcomm0`，通常需要绑定：

```bash
sudo rfcomm bind 0 <蓝牙模块 MAC> 1
```

当前实测过的模块：

```text
Name: HC-04
MAC: 04:25:01:21:00:12
SPP Channel: 1
```

**短连接 vs 长连接**：早期版本默认短连接（`open→write→close` 每次发送），是为了规避 HC-04 长连接下"第二次写入 `Input/output error`"的传言。但 Linux 实测的真实坑相反——**短连接每次都重做 RFCOMM 握手，HC-04 SPP 还没建好 close 就把链路扯了，导致字符根本送不出去**（监视器观察：长时间 `total_chars=0`）。现在默认 `close_after_send=False`，配合 `tools/rfcomm_keepalive.sh` 始终持有 SPP 链路，26/26 字符全到位。

**定制板的 HC-04 接线坑**：本项目用的 Arduino 定制板把 HC-04 直接接在硬件 Serial（pin 0/1）上，不是 SoftwareSerial。`Examples/bt_echo_debug.ino` 用 `Serial.read()` 直接读字节，没用 SoftwareSerial。**注意**：HC-04 接 RX0 会在烧 sketch 时偶尔干扰 bootloader 同步，多按几次"上传"即可。也别在 Arduino IDE 串口监视器输入框打字——那些字符会跟蓝牙字符混在同一根硬件 Serial 上无法区分。

## Project Files

```text
pc_voice_controller/
  command_parser.py      中文命令解析和防抖
  bluetooth_sender.py    单/多蓝牙串口发送，长连接策略
  asr_listener.py        DashScope 麦克风 ASR
  main.py                命令行入口

web_panel/
  server.py              本地 Web API 和状态管理
  static/                Web 控制台页面

tools/
  rfcomm_keepalive.sh    Linux 下持有 HC-04 SPP 链路的看门狗脚本
  emergency_stop.sh      连发 S 给所有 /dev/rfcomm*，紧急停车用

Examples/
  bt_echo_debug.ino           Arduino 调试 sketch，回显蓝牙收到的字节
  car_bluetooth_drive.ino     **生产 sketch**：F/B/L/R/S/U/D 电机控制 + 1s 超时自动停
  sketchcar_may19a.ino        旧避障参考 sketch

tests/
  test_*.py              unittest 用例
  smoke_dual_bluetooth.py  双板/单板真实串口烟测脚本

docs/
  pc_voice_controller_scope.md   电脑端职责边界
  windows_electrical_handoff.md  给电控同学的 Windows 端交接
  motor_debug_handoff.md         **给电控同学的电机调试手册**
  hardware_bringup.md            **整车实测分阶段 checklist（上车前必读）**
```

## Development Checks

```bash
python -m unittest discover -s tests
```

## Arduino Echo Debug Sketch

`Examples/bt_echo_debug.ino` 是一个**不动电机**的调试 sketch，从 HC-04 收字节并打印到 Arduino IDE 串口监视器，验证"PC 端蓝牙写入 → HC-04 → Arduino 收到字符"这一段。

接线（本项目实测的定制板）：

```text
HC-04 TXD -> Arduino pin 0 (RX0)   ← 硬件 Serial RX
HC-04 RXD -> Arduino pin 1 (TX1)   ← 硬件 Serial TX
电源/GND 由定制板内部接好
```

调试流程（按顺序）：

1. 烧 `Examples/bt_echo_debug.ino` 到板上（FQBN `arduino:avr:uno`，9600）
2. 打开 IDE 串口监视器，**9600，结束符"无"**，应看到 `=== bt_echo_debug ready ===`
3. 另一终端拉起 SPP 长连接（前台保持）：
   ```bash
   sudo ./tools/rfcomm_keepalive.sh 0:04:25:01:21:00:12
   ```
   看到 `Connected /dev/rfcomm0 ...` + HC-04 LED 常亮
4. 再开一终端发命令：
   ```bash
   python -m pc_voice_controller.main --mode manual-code --ports /dev/rfcomm0
   ```
   输入 `F` 回车，串口监视器应立刻看到：
   ```text
   [BT] got 'F' (0x46)
          -> ACK
   [BT] skip '\n' (0x0A)
   ```

## Dual-Board Smoke Test

双板硬件就位后用这条跑安全序列 `S F S B S L S R S U S D S`，每步间隔 800ms（低于 Arduino 端 1s 自动停车超时），逐板汇总成功/失败次数：

```bash
python -m tests.smoke_dual_bluetooth --ports /dev/rfcomm0,/dev/rfcomm1
```

先验证脚本本身（不碰硬件）：

```bash
python -m tests.smoke_dual_bluetooth --dry-run --ports A,B
```

Windows 示例：

```powershell
python -m tests.smoke_dual_bluetooth --ports COM5,COM6
```

中途 Ctrl+C 会补发一次 `S` 作为安全停车。
