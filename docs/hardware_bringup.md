# Hardware Bring-up Checklist

把车从"接线板 + 蓝牙通了"推到"真车能听话跑起来"的分阶段流程。每一阶段都明确：通什么电、看什么、出问题怎么停。**禁止跳阶段**——一个失败信号能省后面半小时的排查。

适用前提：
- PC 端已经能让 Arduino 串口监视器看到 `[BT] got 'F'`（用 `Examples/bt_echo_debug.ino`），见 [README.md](../README.md) 的 "Arduino Echo Debug Sketch" 段
- 板上烧的是 `Examples/car_bluetooth_drive.ino`（生产 sketch，`DEBUG_PRINTS=0`，1 秒超时自动停车）

---

## 阶段 0：紧急停车准备（每次实测前必演练）

打开**三个**终端窗口，分别预先输好命令，**不要回车**：

```bash
# 终端 1：rfcomm 守护（必须先跑）
sudo ./tools/rfcomm_keepalive.sh 0:04:25:01:21:00:12

# 终端 2：紧急停车（出问题立刻回车）
./tools/emergency_stop.sh

# 终端 3：正常发命令
python -m pc_voice_controller.main --mode manual-code --ports /dev/rfcomm0
```

⚠️ **物理紧急停车**（蓝牙挂了也能用）：直接拔电池/总开关。这是最后一道防线。如果是 USB 供电没接电池，拔 USB。

---

## 阶段 1：纯接线静态检查（不通电）

电池 / USB **全部断开**，万用表（蜂鸣档）核对：

- [ ] HC-04 VCC ↔ Arduino 5V 通
- [ ] HC-04 GND ↔ Arduino GND 通
- [ ] HC-04 TXD ↔ Arduino pin 0 (RX0) 通
- [ ] HC-04 RXD ↔ Arduino pin 1 (TX1) 通
- [ ] 电机驱动 VCC ↔ 电池正极 通（这是大电流回路，必须粗线/可靠焊接）
- [ ] 电机驱动 GND ↔ Arduino GND **和**电池负极 都通
- [ ] IB_LEFT (pin 3) ↔ 电机驱动 IN1（或对应输入）通
- [ ] IA_LEFT (pin 5) ↔ 电机驱动 IN2 通
- [ ] IB_RIGHT (pin 9) ↔ 电机驱动 IN3 通
- [ ] IA_RIGHT (pin 10) ↔ 电机驱动 IN4 通
- [ ] 左轮电机 ↔ 电机驱动 OUT1/OUT2
- [ ] 右轮电机 ↔ 电机驱动 OUT3/OUT4
- [ ] **没有任何短路**：电池正负之间电阻应该是开路（兆欧级），不是几欧

任一项不通就先解决，绝不能上电。

---

## 阶段 2：USB 供电 + 电机不接，纯 GPIO 验证

**电池不接、电机线从电机驱动 OUT 端拔下来**（或断电机驱动那路电源）。只让 Arduino 通过 USB 供电，HC-04 跟着上电。

```bash
# 终端 1
sudo ./tools/rfcomm_keepalive.sh 0:04:25:01:21:00:12
# 等 "Connected /dev/rfcomm0..." + HC-04 LED 常亮

# 终端 3
python -m pc_voice_controller.main --mode manual-code --ports /dev/rfcomm0
```

万用表表笔搭电机驱动的输入引脚（IN1/2/3/4），测对地电压：

| 发命令 | IN1 (pin 3) | IN2 (pin 5) | IN3 (pin 9) | IN4 (pin 10) |
|---|---|---|---|---|
| S | 0V | 0V | 0V | 0V |
| F | ~2V (PWM) | 0V | ~2.4V (PWM) | 0V |
| B | 0V | ~2V | 0V | ~2.4V |
| L | 0V | 0V | ~2.4V | 0V |
| R | ~2V | 0V | 0V | 0V |
| U（在 F 状态下） | 升到 ~2.7V | — | 升到 ~3.3V | — |
| D（多按几下） | 降到 ~1.4V | — | 降到 ~1.7V | — |

预期值是基于 `SPEED_TABLE_LEFT={70,100,140}` 和 `SPEED_TABLE_RIGHT={85,120,170}`（默认中档 1）算的，万用表测的是平均电压所以会比 5V 低不少。

发完 F 后**不发新命令**等 2 秒，应该看到 IN1/IN3 电压都降回 0V（**1 秒超时自动停车** ✓）。

**如果这一步全过**，sketch 完全没问题，可以进下一阶段。

**如果某一组 IN 一直 0V** 或方向不对，先**别接电机**。回去查接线，再回阶段 1 重核。

---

## 阶段 3：装电池 + 悬空车轮，验证方向

车**架空**（垫高让四个轮子离地空转），电机线接回去，电池接上。

```bash
# 终端 1：rfcomm 守护已经在跑
# 终端 2：紧急停车命令已预输好，准备 Ctrl+R 重发
# 终端 3：
python -m pc_voice_controller.main --mode manual-code --ports /dev/rfcomm0
```

按下面顺序，**每条命令之间停 2 秒看反应再发下一条**：

1. `S` → 没动作就是对的（默认就是停的）
2. `F` → **两个轮子都应该向前转**。看顶视图，左右轮转向应该让车前进
3. `S` → 立刻停
4. `B` → 两个轮子都应该向后转
5. `S` → 立刻停
6. `L` → 左转：**只有右轮转，左轮停**（原地左转）
7. `S` → 立刻停
8. `R` → 右转：**只有左轮转，右轮停**
9. `S` → 立刻停
10. `F` → 前进
11. `U` → 速度档位提升，听马达声音变高
12. `D` `D` → 降两档，听声音变低
13. `S` → 立刻停

**问题诊断表**：

| 现象 | 原因 | 修法 |
|---|---|---|
| 发 F 但某个轮子反转 | 该电机的 OUT1/OUT2 接反了 | 把那个电机的两根线交换 |
| 发 F 但两个轮子都反 | IB/IA 在驱动板上接反了（不太可能，因为 B 应该就对了） | 检查阶段 1 接线 |
| 发 L 左轮也在转 | sketch 里 `motorTurnLeft` 误把左轮也驱动了 | 跟我反馈，我看 sketch |
| 发 U 速度没变 | 你只发了 U 没先发 F，sketch 在 S 状态下 U 不会触发动作 | 先发 F 再 U |
| 一直没反应 | rfcomm 掉了或 sketch 卡了 | 看 HC-04 LED 是不是常亮；不是就重跑 rfcomm_keepalive；按板上 RESET |
| **车端不停车了** | sketch 1 秒超时应该兜底 | 终端 2 立刻按回车发 emergency_stop；不行就拔电池 |

⚠️ **绝对不要在阶段 3 把车放下来跑**。车架空时左右轮速度不一致只是空转响，落地后会变方向偏差或翻车。

---

## 阶段 4：落地慢速验证（**第一次上地**）

**前提**：阶段 3 全部通过且左右轮方向都对。

第一次落地：

- 找**空旷地面**，没有家具/桌腿/楼梯
- 车放正，**正前方至少 2 米空旷**
- 终端 2 的 emergency_stop 准备好，回车键准备按
- 终端 3 跑：
  ```bash
  python -m pc_voice_controller.main --mode manual-code --ports /dev/rfcomm0
  ```

测试序列（**每条之间等 2 秒**）：

1. `D` `D` → 强制降到最低速档（默认是中档，先慢一点更安全）
2. `F` → 短促前进，立刻看走直线还是偏
3. `S` → **马上停**

如果走偏：
- 偏左 = 右轮快 → 在 sketch 里调小 `SPEED_TABLE_RIGHT`，或调大 `SPEED_TABLE_LEFT`
- 偏右反之
- 修完重烧 sketch，回阶段 2 重测

直线 OK 后：

4. `F` → 前进
5. `L` → 左转 → 应该原地转方向
6. `S` → 停
7. `R` → 右转
8. `S` → 停
9. `B` → 后退（注意车后面别有人/物）
10. `S` → 停

每条命令车反应迟钝（>0.5 秒才动）= 蓝牙链路有积压字节或者 sketch 没及时读，告诉我我看代码。

---

## 阶段 5：双板联调

只有阶段 4 单板全过，再接第二块板。流程：

1. 把第二块 HC-04 配对、绑 rfcomm1
2. 启动双板守护：
   ```bash
   sudo ./tools/rfcomm_keepalive.sh 0:04:25:01:21:00:12 1:04:25:02:05:03:42
   ```
3. 两块板都烧同一份 `car_bluetooth_drive.ino`
4. 跑双板烟测：
   ```bash
   python -m tests.smoke_dual_bluetooth --ports /dev/rfcomm0,/dev/rfcomm1
   ```
   预期：两块板的轮子同步动作。如果一前一后差 100-200ms 是正常的（串行发送），差 1 秒以上不正常

5. 整车四轮装好的真车测试，先**架空**再**落地**，跟阶段 3+4 一样的顺序

⚠️ 方案 A 的前后两块板**安装方向必须一致**——前板的"前"和后板的"前"是同一个方向。否则发 `F` 会前后对拉。如果安装方向天然就反了，**在电控同学那块板的 sketch 里反转电机方向**，不要让 PC 端发不同命令。

---

## 阶段 6：DashScope 语音整链路

只有阶段 5 双板能跑稳，再上语音：

```bash
# 终端 1：rfcomm 守护
# 终端 2：emergency_stop 预备
# 终端 3：
python -m pc_voice_controller.main --mode asr --ports /dev/rfcomm0,/dev/rfcomm1
```

需要 `.env` 里有 `DASHSCOPE_API_KEY`。麦克风对着说"前进"，车应该走。

语音延迟典型 0.5-1.5 秒，是 ASR 本身的延迟，不是蓝牙的。

---

## 附录：常见死法

| 死法 | 兆头 | 自救 |
|---|---|---|
| 车把人撞到 | 车跑出预定区域 | 阶段 0 emergency_stop，物理拔电池 |
| Arduino 烧了 | Arduino 板冒烟/异味 | 立刻断 USB 和电池，别再插 |
| 电机驱动烧了 | 驱动板烫到不能摸/冒烟 | 立刻断电池 |
| HC-04 不响应 | LED 一直快闪连不上 | 拔 USB 10 秒再插（电容放电） |
| sketch 不超时 | 发 S 后车继续动 | sketch 编译版本不对，先 emergency_stop 然后查烧的对不对 |
| 蓝牙断了车不停 | rfcomm 进程死了 + 车在动 | 1 秒后 sketch 应该自动停（COMMAND_TIMEOUT_MS）；如果没停说明这块板的 sketch 出问题，物理拔电池 |
