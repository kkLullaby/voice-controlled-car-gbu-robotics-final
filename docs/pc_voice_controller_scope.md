# PC Voice Controller Scope

本目录负责期末项目中的电脑端语音到蓝牙指令传输，不负责小车供电、双板协同、四轮电机接线和 Arduino 车端状态机。

## Boundary

```text
电脑麦克风
  -> DashScope ASR
  -> 中文命令解析
  -> 7 个动作码
  -> 蓝牙串口发送
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
/dev/rfcomm0 -> 蓝牙 A -> 前轮 Arduino
/dev/rfcomm1 -> 蓝牙 B -> 后轮 Arduino
```

## First Version Decisions

- 语音识别复用参考项目的 DashScope `paraformer-realtime-v2` 流式 ASR。
- 命令解析使用关键词规则，不使用 LLM，降低延迟和不确定性。
- 蓝牙按 Linux 串口设备处理，默认端口为 `/dev/rfcomm0,/dev/rfcomm1`，波特率 `9600`。
- 对流式 ASR 重复文本做防抖：默认 1 秒内不重复发送同一动作码。
- 未识别到 7 个动作之一时不发送任何蓝牙指令。

## Implementation Order

1. 纯文本命令解析。
2. 假串口和 dry-run 验证发送协议。
3. 手动中文输入模式验证 `文本 -> 动作码 -> 蓝牙发送`。
4. 手动动作码模式验证真实蓝牙链路。
5. 接入麦克风和 DashScope ASR。

## Web Panel

`web_panel.server` 提供本地控制面板：

```bash
python -m web_panel.server --host 127.0.0.1 --port 8765
```

面板复用同一套解析和发送模块，默认 dry-run。浏览器语音按钮使用浏览器自带中文语音识别，DashScope 按钮使用后端麦克风 ASR。真实双蓝牙发送时使用：

```bash
python -m web_panel.server --host 127.0.0.1 --port 8765 --real-bluetooth --bt-ports /dev/rfcomm0,/dev/rfcomm1
```
