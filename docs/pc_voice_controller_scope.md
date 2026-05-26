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

- 语音识别复用参考项目的 DashScope `paraformer-realtime-v2` 流式 ASR。
- 命令解析使用关键词规则，不使用 LLM，降低延迟和不确定性。
- 蓝牙按串口设备处理。Linux 默认端口为 `/dev/rfcomm0,/dev/rfcomm1`；Windows 使用 `COM5,COM6` 这类端口；波特率 `9600`。
- 蓝牙发送器默认使用短连接策略：每条命令打开串口、写入、flush、关闭串口。这个策略来自 HC-04 实测，避免长连接模式下第二次写入 `Input/output error`。
- 对流式 ASR 重复文本做防抖：默认 1 秒内不重复发送同一动作码。
- 未识别到 7 个动作之一时不发送任何蓝牙指令。

## Implementation Order

1. 纯文本命令解析。
2. 假串口和 dry-run 验证发送协议。
3. 手动中文输入模式验证 `文本 -> 动作码 -> 蓝牙发送`。
4. 手动动作码模式验证真实蓝牙链路。
5. 接入麦克风和 DashScope ASR。
6. 单蓝牙模块联调通过后，再切换到双蓝牙端口。
7. 电控同学在 Arduino 端加入接收超时保护，避免某个蓝牙断开后一块板继续动作。

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

建议电控端必须实现：

- 上电默认停止。
- 收到非法字符忽略。
- 收到 `S` 立即停止。
- 超过 800-1000ms 没收到任何有效命令时自动停止。
- 先拔掉电机或悬空车轮测试串口，再接电机测试运动。
