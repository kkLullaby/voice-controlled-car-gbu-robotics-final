# 给电控同学的电机调试文档

PC 端 + 蓝牙 + Arduino 接收已经全部跑通。现在轮到你把电机接上、把车跑起来。

## 1. 你拿到手的现状

- 两块 Arduino 已经烧好生产 sketch `Examples/car_bluetooth_drive.ino`
- HC-04 已经焊在板子上（**接在硬件 Serial pin 0/1，不是 SoftwareSerial**），蓝牙能配对、能传字符
- PC 端任何方式（按钮 / 中文 / 浏览器语音 / 命令行）发出的动作码都能到 Arduino
- **电机没接，电池没装**——这是你的工作

校验过：用浏览器语音说"前进"，两块板都收到 `F`，sketch 已经在调用 `motorForward()`。
**没校验的是**：`motorForward()` 写出来的 GPIO 方向是不是匹配你的电机驱动板和你的电机接线。这一步只能你来测。

## 2. 命令协议

电脑发 ASCII 单字符（带换行）：

```text
F  前进     U  加速一档
B  后退     D  减速一档
L  左转
R  右转
S  停止
```

sketch 行为：

- 上电默认 `motorStop()`
- 收到 F/B/L/R/S 切换运动状态，立刻执行
- 收到 U/D 调整速度档位（三档：低/中/高），如果当前正在运动就立刻按新档位重发 PWM
- **超过 1000ms 没收到任何有效字符 → 自动停车**（COMMAND_TIMEOUT_MS）
- 收到 `\r \n 空格 不在 F/B/L/R/S/U/D 表里的字符` 全部静默忽略
- 默认不打印任何调试信息（`DEBUG_PRINTS=0`），因为打印会经 HC-04 反向漏到 PC 端

## 3. 引脚映射（sketch 用了哪些）

| 用途 | Arduino pin | sketch 里的名字 |
|---|---|---|
| HC-04 TXD（蓝牙数据进 Arduino）| 0 (RX0) | 硬件 Serial |
| HC-04 RXD（Arduino 给蓝牙）| 1 (TX1) | 硬件 Serial |
| 左轮 IB（PWM 方向 B）| 3 | `IB_LEFT_pin` |
| 左轮 IA（方向 A）| 5 | `IA_LEFT_pin` |
| 右轮 IB（PWM 方向 B）| 9 | `IB_RIGHT_pin` |
| 右轮 IA（方向 A）| 10 | `IA_RIGHT_pin` |

接电机驱动模块（L298N / TB6612 / 其他）时，pin 3/5/9/10 接到驱动板的 IN 输入端：

```text
Arduino pin 3  -> 驱动板 IN1 (左轮 B)
Arduino pin 5  -> 驱动板 IN2 (左轮 A)
Arduino pin 9  -> 驱动板 IN3 (右轮 B)
Arduino pin 10 -> 驱动板 IN4 (右轮 A)
```

驱动板的 OUT 接电机，VCC 接电池（电机大电流回路），GND 与 Arduino GND 共地。

## 4. 三档速度

sketch 里写死的速度表（PWM 0-255）：

```cpp
const int SPEED_TABLE_LEFT[]  = {  70, 100, 140 };  // 低/中/高
const int SPEED_TABLE_RIGHT[] = {  85, 120, 170 };
const int DEFAULT_SPEED_LEVEL = 1;  // 中档
```

左右轮基础不一致是为了校正机械偏心（左轮天然更快/慢这种）。**如果你的电机一边明显比另一边快，先用这两个表微调，不要碰其他东西。**

## 5. 调试分阶段流程（**禁止跳阶段**）

详见 `docs/hardware_bringup.md` 完整版。这里给你最常用的 3 个阶段：

### 阶段 2：USB 供电 + 电机不接，万用表测 GPIO

电池**不接**，电机线**从电机驱动 OUT 端拔下来**（或断电机驱动那路电源）。

让 PC 端同学（或你自己）启动 keepalive：

```bash
sudo ./tools/rfcomm_keepalive.sh 0:04:25:01:21:00:12 1:04:25:02:05:03:42
```

然后用命令行发命令：

```bash
python -m pc_voice_controller.main --mode manual-code --ports /dev/rfcomm0
```

万用表对地测电机驱动的 IN1/IN2/IN3/IN4（也就是 Arduino 的 pin 3/5/9/10），按下表核对：

| 发命令 | IN1 (pin 3) | IN2 (pin 5) | IN3 (pin 9) | IN4 (pin 10) |
|---|---|---|---|---|
| S | 0V | 0V | 0V | 0V |
| F | ~2V (PWM) | 0V | ~2.4V (PWM) | 0V |
| B | 0V | ~2V | 0V | ~2.4V |
| L | 0V | 0V | ~2.4V | 0V |
| R | ~2V | 0V | 0V | 0V |

万用表测的是平均电压，对应 PWM duty。比 5V 低很多是正常的。

**任一项不对就别接电机**——告诉 PC 端同学，可能 sketch 有 bug 要改。

**发 F 后不发新命令等 2 秒**，应该看到 IN1/IN3 降回 0V——这验证 1 秒自动停车有效。

### 阶段 3：装电机 + 装电池 + 悬空车轮

**车必须悬空**（垫高让四个轮子离地空转）。

第一条命令永远先发 `S`，验证停车有效：

```
> S    ← 4 个轮子都不动 = OK
> F    ← 看哪几个轮子转、什么方向（俯视看）
> S    ← 立刻停
```

预期：发 F 时**左右两个驱动轮**都向前转，转向应该是让车向前走。

| 现象 | 原因 | 修法 |
|---|---|---|
| F 时两个轮子都反转 | 电机驱动板 OUT1/OUT2 接反了，或电池极性接反 | 拆开重接 |
| F 时只有一个轮子转 | 另一个电机/驱动通道坏了，或接线松了 | 万用表逐段查 |
| F 时一个正转一个反转 | 单个电机的两根线接反了 | 把那一个电机的 OUT1 和 OUT2 交换 |
| F 时两个都不转但能听见声音 | 速度档位太低，扭矩不够 | 先把车放下来看会不会动；不行就改 sketch 里的 SPEED_TABLE 值 |
| F 完全没反应 | 蓝牙断了 / rfcomm_keepalive 没在跑 / 电池没装好 | 看 HC-04 LED 是不是常亮；不是就喊 PC 端同学 |

方向都对之后，按下面顺序逐条测：

```
S
B  ← 两轮后退
S
L  ← 只有右轮转（左轮停），实现原地左转
S
R  ← 只有左轮转（右轮停），实现原地右转
S
F  ← 重新前进
U  ← 升档，听马达声音变高
D D  ← 降两档到最低档，声音变低
S
```

### 阶段 4：落地慢速直线

**前提**：阶段 3 全过。

把车从悬空状态放到地面，找**至少 2 米空旷地面**。准备好紧急停车（见第 6 节）。

```
> D D  ← 强制降到最低档（默认中档，先慢一点）
> F    ← 短促前进
> S    ← 立刻停
```

观察走直线还是偏：
- **偏左** = 右轮快 → 在 sketch 里调小 `SPEED_TABLE_RIGHT` 的值
- **偏右** = 左轮快 → 在 sketch 里调小 `SPEED_TABLE_LEFT` 的值

每次改完 sketch 都要重烧。重烧步骤需要 PC 端同学配合（或你自己用 arduino-cli）：

```bash
arduino-cli upload -p /dev/ttyUSB0 --fqbn arduino:avr:uno path/to/sketch_dir
```

烧之前**关掉 IDE 串口监视器**和**让 PC 端同学 Ctrl+C 退掉 rfcomm_keepalive**（HC-04 接在 RX0 上会干扰 bootloader）。烧完再重启 keepalive。

## 6. 紧急停车（出问题时第一时间用）

车跑出预定区域、撞到东西、不停了，按这个顺序自救：

1. **键盘按回车发 emergency_stop**（PC 端同学应该已经在第二个终端窗口预输好命令了）：
   ```bash
   ./tools/emergency_stop.sh
   ```
   这会向所有 rfcomm 设备并行连发 5 个 S。

2. **如果上一步无效**：HC-04 通信断了。等 1 秒 sketch 应该自动停车（COMMAND_TIMEOUT_MS）。

3. **如果还不停**：sketch 出问题或电源没断。**物理拔电池/总开关**。

4. **如果是 USB 供电没接电池**：物理拔 USB。

## 7. 双板的事

方案 A：两块板都烧**同一份** sketch，PC 端同时给两块发同一条动作码。两块板各管两个轮子。

```text
板 A: 前左轮 + 前右轮
板 B: 后左轮 + 后右轮
```

如果两块板**物理安装方向相反**（比如后板倒装 180°），**在那块板的 sketch 里反转电机方向**——不要让 PC 端给两块板发不同命令。修改 `motorForward()` 里 `digitalWrite` 的 HIGH/LOW 配置即可。

## 8. 什么时候找 PC 端同学

- 阶段 2 万用表测电压表对不上
- 蓝牙连接不稳，HC-04 LED 闪烁不规律
- 发命令延迟超过 1 秒
- 你想让 sketch 加新功能（比如循迹、避障、超声波）

什么不用找 PC 端：

- 电机方向反了（在 sketch 电机函数里改 digitalWrite）
- 左右轮速度不一致（改 SPEED_TABLE）
- 烧录失败 retry 几次能过（HC-04 在 RX0 上的偶发干扰）
- 接线问题

## 9. 一句话总结

```text
PC 端做的事:   语音/按钮/中文 -> F/B/L/R/S/U/D -> 蓝牙广播
你做的事:     蓝牙字符到了之后 -> 电机引脚 -> 真的转
```

界限就在 Arduino 的硬件 Serial 上。字符到 Serial.read() 之前的事 PC 端管，之后的事你管。

## 10. 相关文档

- `README.md` —— 项目总览，看一遍
- `docs/hardware_bringup.md` —— 完整 6 阶段实测 checklist
- `docs/pc_voice_controller_scope.md` —— PC 端边界定义
- `Examples/car_bluetooth_drive.ino` —— 你要烧的 sketch
- `Examples/bt_echo_debug.ino` —— 只在排查"字符到底到没到 Arduino"时用
