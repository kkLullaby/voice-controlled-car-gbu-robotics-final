# Voice Bluetooth Car Controller

电脑端语音到蓝牙动作码传输模块。当前版本只覆盖本组分工中的电脑端链路：

```text
电脑麦克风 -> DashScope 语音识别 -> 中文命令解析 -> 蓝牙串口发送动作码
```

## Command Protocol

发送给小车端的协议是单字符动作码加换行：

```text
F 前进
B 后退
L 左转
R 右转
S 停止
U 加速
D 减速
```

实际发送格式示例：

```text
F\n
S\n
```

## Setup

```bash
cd /home/kk/code/robot_class/finalproj_cowork
python -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
cp .env.example .env
```

编辑 `.env`：

```text
DASHSCOPE_API_KEY=你的 DashScope Key
BT_PORTS=/dev/rfcomm0,/dev/rfcomm1
BT_BAUD=9600
```

Linux 上经典蓝牙串口常见设备名是 `/dev/rfcomm0`、`/dev/rfcomm1`。实际设备可以用 `ls /dev/rfcomm* /dev/ttyUSB* /dev/ttyACM*` 检查。

## Run

启动 Web 面板：

```bash
python -m web_panel.server --host 127.0.0.1 --port 8765
```

打开：

```text
http://127.0.0.1:8765
```

默认是 dry-run，不会碰真实蓝牙。要真实发送蓝牙：

```bash
python -m web_panel.server --host 127.0.0.1 --port 8765 --real-bluetooth --bt-ports /dev/rfcomm0,/dev/rfcomm1
```

先用 dry-run 验证中文解析，不连接蓝牙：

```bash
python -m pc_voice_controller.main --mode manual-text --dry-run
```

输入：

```text
前进
停下
左转
```

连接蓝牙后，手动发送动作码：

```bash
python -m pc_voice_controller.main --mode manual-code --ports /dev/rfcomm0,/dev/rfcomm1
```

接入语音识别：

```bash
python -m pc_voice_controller.main --mode asr --ports /dev/rfcomm0,/dev/rfcomm1
```

## Development Checks

```bash
python -m unittest discover -s tests
```
