# Voice Bluetooth Car Controller

电脑端语音到蓝牙动作码传输模块，用于期末四轮声控小车。当前仓库重点覆盖电脑端职责：

```text
语音 / 手动输入
  -> 中文命令解析
  -> F/B/L/R/S/U/D 动作码
  -> 蓝牙串口广播到 Arduino 板
```

## Current Status

已完成：

- 中文命令解析：`前进`、`停止`、`左转` 等转为动作码。
- 命令行控制：支持手动动作码、手动中文、DashScope ASR 三种模式。
- Web 控制台：支持按钮、中文输入、浏览器语音、DashScope 启停入口。
- 蓝牙发送：支持单板测试和双板广播。
- HC-04 实测优化：发送器默认采用短连接策略，每次发送后关闭串口，下一条命令重新打开，避免长连接第二次写入 `Input/output error`。

未包含：

- Arduino 车端电机控制状态机。
- 两块 Arduino 的供电方案。
- 小车四轮机械/电控接线实现。

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

## Run

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

## Project Files

```text
pc_voice_controller/
  command_parser.py      中文命令解析和防抖
  bluetooth_sender.py    单/多蓝牙串口发送，短连接策略
  asr_listener.py        DashScope 麦克风 ASR
  main.py                命令行入口

web_panel/
  server.py              本地 Web API 和状态管理
  static/                Web 控制台页面

docs/
  pc_voice_controller_scope.md
  windows_electrical_handoff.md
```

## Development Checks

```bash
python -m unittest discover -s tests
```
