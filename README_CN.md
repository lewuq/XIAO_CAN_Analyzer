<h1 align="center">XIAO CAN Analyzer</h1>

<p align="center">
  基于 <strong>XIAO STM32C5 Console</strong> 的便携式经典 CAN / CAN FD 分析仪。<br>
  静默识别常见波特率，在 <strong>0.95 英寸 AMOLED</strong> 上浏览报文，并记录到外置 Flash。
</p>

<p align="center">
  <a href="https://www.seeedstudio.com/"><img src="docs/badges/seeed.png" height="26" alt="Seeed Studio"></a>
  <a href="https://wiki.seeedstudio.com/xiao_stm32c5_getting_started/"><img src="docs/badges/stm32c5.png" height="26" alt="XIAO STM32C5"></a>
  <a href="#硬件"><img src="docs/badges/amoled.png" height="26" alt="SH8501G AMOLED 屏幕"></a>
  <a href="https://registry.platformio.org/tools/platformio/toolchain-gccarmnoneeabi"><img src="docs/badges/arm-gcc.png" height="26" alt="Arm GCC"></a>
  <a href="https://platformio.org/"><img src="docs/badges/platformio.png" height="26" alt="PlatformIO"></a>
  <a href="https://docs.zephyrproject.org/"><img src="docs/badges/zephyr.png" height="26" alt="Zephyr RTOS"></a>
</p>

<p align="center">
  <a href="#波特率识别"><img src="docs/badges/can-fd.png" height="26" alt="CAN / CAN FD"></a>
  <a href="#记录"><img src="docs/badges/flash.png" height="26" alt="16 MiB 外置 Flash"></a>
</p>

<p align="center">
  <a href="firmware/latest/firmware_can_analyzer_v5_1_50pct.uf2?raw=true"><img src="docs/badges/download.png" height="26" alt="下载 V5.1 UF2"></a>
  <a href="#版本历史"><img src="docs/badges/history-zh.png" height="26" alt="固件历史"></a>
  <a href="#界面预览"><img src="docs/badges/preview-zh.png" height="26" alt="V5.1 界面预览"></a>
  <a href="LICENSE"><img src="docs/badges/license.png" height="26" alt="MIT 许可证"></a>
</p>

<p align="center">
  <strong><a href="#快速开始">快速开始</a> · <a href="#硬件">硬件</a> · <a href="#按键">按键</a> · <a href="#波特率识别">波特率识别</a> · <a href="#can-fd-台架测试">台架测试</a> · <a href="#从源码编译">从源码编译</a></strong>
</p>

<p align="center">
  <a href="README.md"><img src="docs/badges/english.png" height="26" alt="English"></a>
  <img src="docs/badges/chinese-current.png" height="26" alt="简体中文">
</p>

---

## 快速开始

1. 下载 [V5.1 UF2](https://github.com/lewuq/XIAO_CAN_Analyzer/raw/refs/heads/main/firmware/latest/firmware_can_analyzer_v5_1_50pct.uf2)。
2. 用 USB-C 连接 Console，快速按两次 XIAO 的 RESET 键。把 UF2 复制到 `XIAOC5BOOT` 盘，设备会自动重启。[Seeed UF2 烧录指南](https://wiki.seeedstudio.com/xiao_stm32c5_getting_started/#bootloader-usage)
3. 将 **SW1 拨到 CANH/CANL 通路（A 组触点）**，再将 **J4-1 接 CANH、J4-2 接 CANL、J4-3 接 GND**。
4. 总线有报文时，等 AUTO 自动确认速率。已知速率时，在 **RATE** 页短按 **BOOT** 选预设，长按应用。

## 硬件

| 部件 | 用途 |
| --- | --- |
| **XIAO STM32C5** | FDCAN2、板载 TCAN3403-Q1 收发器和 16 MiB 外置 Flash |
| **Console V1.0** | SH8501G SPI AMOLED、240 × 120 横屏界面、D0 和 BOOT 两个按键 · [屏幕数据手册（PDF）](docs/datasheets/ZJY095-2424AM002-C15.pdf) |
| **J4** | SW1 在 A 档时，1 至 3 脚依次为 CANH、CANL 和 GND |
| **SW1** | 将 J4 切到板载收发器或 FDCAN1 逻辑 TX/RX 通路 |

该固件使用 **A 档**。B 档需要外挂 CAN FD 收发器和使用 FDCAN1 的固件。旁路接入已有双端匹配的总线时，检查 JP1 并断开 XIAO 板载的 120 Ω 终端。Console 位于总线物理末端时，该端只需要一个 120 Ω 终端。[XIAO STM32C5 CAN FD 指南](https://wiki.seeedstudio.com/xiao_stm32c5_canfd_usage/)

## 按键

| 按键 | 短按 | 长按约 0.7 秒 |
| --- | --- | --- |
| **D0 / A** | 总览 → ID 列表 → 载荷 → 存储 → 速率 | 清空当前采集统计 |
| **BOOT / B** | 执行当前页的操作 | 前三页切换 HOLD，存储页恢复快照，速率页应用配置 |

| 页面 | 显示内容 | B 短按 |
| --- | --- | --- |
| **OVERVIEW** | 仲裁速率、数据速率、扫描状态、帧率、ID 数和接收损失 | 切换 HOLD |
| **FRAMES** | 每屏三条 ID 及最新帧长度、类型 | 选下一条 ID |
| **DATA** | 选中 ID 的最新载荷、计数和距最近接收的时间 | 看下一组 16 字节 |
| **STORAGE** | 录制状态、批次数、录制丢帧数及快照 | 开始或停止录制 |
| **RATE** | 扫描状态、试探或当前配置、待应用的预设 | 循环选择 AUTO 和 15 个预设 |

## 波特率识别

开机默认为 AUTO。分析仪用 FDCAN2 静默轮试候选位时序，常规观察窗口约 300 ms，已有部分证据时最长可延至 12 秒。AUTO 用 10 帧有效报文确认仲裁速率，用同一个候选速率下 10 帧有效 FD+BRS 报文确认数据速率。总线完全静默时无法识别速率。

- **SCANNING / DATA SCAN** 表示仍在试探。`DATA N/A` 表示 BRS 证据不足。
- **LOCKED / IDLE** 保留已确认的速率。换总线后可在 RATE 页重新应用 AUTO。
- **MANUAL** 使用选定的组合。RATE 页的 `SET` 显示配置值，总览在有足够证据后显示数据速率。
- **NO VALID** 表示本轮证据不足，AUTO 会重试。

仲裁候选为 **10、20、50、100、125、250、500、800、1000 kbit/s**，数据候选为 **0.5、1、2、3、4、6、8 Mbit/s**。驱动校验位时序，实际高速通信和识别用时取决于总线流量与接线。扫描阶段接收的试探帧不计入采集统计或日志。

## 界面预览

由 **V5.1 固件的界面渲染代码**绘制，帧和计数为模拟数据。

![V5.1 自动扫描、经典 CAN 锁定、FD+BRS 锁定和手动预设](docs/v5-rate-preview.png)

![V5.1 ID 列表、载荷、Flash 录制和锁定后空闲](docs/v5-capture-preview.png)

### V5.1 实机照片

| 总览 | 帧列表 |
| --- | --- |
| <a href="docs/photos/v5_1-overview.jpg"><img src="docs/photos/v5_1-overview.jpg" width="400" alt="总览"></a> | <a href="docs/photos/v5_1-frames.jpg"><img src="docs/photos/v5_1-frames.jpg" width="400" alt="帧列表"></a> |

| 存储 | 速率 |
| --- | --- |
| <a href="docs/photos/v5_1-storage.jpg"><img src="docs/photos/v5_1-storage.jpg" width="400" alt="存储"></a> | <a href="docs/photos/v5_1-rate.jpg"><img src="docs/photos/v5_1-rate.jpg" width="400" alt="速率"></a> |

| 载荷 |
| --- |
| <a href="docs/photos/v5_1-data.jpg"><img src="docs/photos/v5_1-data.jpg" width="400" alt="Data 页实拍，显示 CAN FD 帧 0x123 的载荷"></a> |

## CAN FD 台架测试

[下载台架发送固件 UF2](firmware/test/firmware_canfd_test_500k_2m.uf2?raw=true)，烧录到单独作为发送端的 XIAO STM32C5。固件使用 FDCAN2，仲裁速率 **500 kbit/s**、数据速率 **2 Mbit/s**，开启 BRS，发送标准 ID `0x123` 和 64 字节伪随机载荷。每次成功发送后的配置间隔为 10 ms。

该固件用于在独立台架上生成测试流量。实际总线的速率、ID、载荷和流量分布可能不同。下方实拍展示速率锁定、帧类型和载荷显示。评估帧率或丢帧时，请在实际总线上与独立的帧计数对照。

测试源码和编译配置以 TXT 存放。如需自行编译，在单独的 PlatformIO 工程中去掉文件名末尾的 `.txt`，按下表路径放置。

| 工程路径 | TXT 文件 |
| --- | --- |
| `src/main.c` | [main.c.txt](docs/can-fd-test/main.c.txt) |
| `platformio.ini` | [platformio.ini.txt](docs/can-fd-test/platformio.ini.txt) |
| `zephyr/prj.conf` | [prj.conf.txt](docs/can-fd-test/prj.conf.txt) |
| `zephyr/app.overlay` | [app.overlay.txt](docs/can-fd-test/app.overlay.txt) |
| `zephyr/CMakeLists.txt` | [CMakeLists.txt.txt](docs/can-fd-test/CMakeLists.txt.txt) |

| OVERVIEW 自动锁定实拍 | DATA 64 字节 CAN FD 帧实拍 |
| --- | --- |
| <a href="docs/photos/v5-canfd-overview.jpg"><img src="docs/photos/v5-canfd-overview.jpg" width="240" alt="分析仪实拍，已锁定 500K / 2M"></a> | <a href="docs/photos/v5-canfd-data.jpg"><img src="docs/photos/v5-canfd-data.jpg" width="240" alt="分析仪实拍，显示 0x123 帧的载荷"></a> |

## 记录

- **HOLD** 冻结界面并将统计及最多 64 个 ID 的最新数据排队存入 Flash，后台继续接收。
- **录制**在外置 Flash 后 8 MiB 循环保存，不同速率配置分别分批。停止后等待 `READY` 再断电。
- **导出**使用 115200 波特率的 USB CDC 终端。

```text
capture info
capture dump 1
```

把 `1` 换成 `capture info` 显示的批次号。CSV 包含时间、ID、标志、长度、载荷、仲裁速率和配置的数据速率。旧固件未保存的速率字段在导出时显示为 0。

## 从源码编译

安装 [PlatformIO](https://platformio.org/) 和 [Seeed STM32C5 平台](https://wiki.seeedstudio.com/xiao_stm32c5_getting_started/#platformio)，在仓库根目录运行：

```sh
pio pkg install -g -p "https://github.com/Seeed-Studio/platform-seeedboards.git"
pio run -e seeed-xiao-stm32c5
```

产物位于 `.pio/build/seeed-xiao-stm32c5/firmware.uf2`。[设备树覆盖文件](zephyr/app.overlay) 定义屏幕、按键和外置 Flash 分区。[主机测试](test/test_host.c) 覆盖速率扫描、按键导航、渲染和局部窗口对齐。[NOR 测试](test/test_nor.py) 验证掉电恢复和按速率分批。

## 版本历史

| 固件 | 更新内容 |
| --- | --- |
| [V5.1](firmware/latest/firmware_can_analyzer_v5_1_50pct.uf2) | UI 字体改为 Montserrat Bold。 |
| [v5](firmware/history/firmware_can_analyzer_v5_50pct.uf2) | 加入自动波特率识别、手动预设和动态速率显示。 |
| [v4.1](firmware/history/firmware_can_analyzer_v4_1_50pct.uf2) | 对齐局部刷新窗口，修复切页花屏。 |
| v4 | 加入外置 Flash 录制、快照和局部刷新。 |
| [v3](firmware/history/firmware_can_analyzer_v3_50pct.uf2) | 初版 FDCAN2 分析仪界面及双按键操作。 |

## 许可证

项目原创代码和台架发送程序采用 [MIT 许可证](LICENSE)。第三方组件和厂商数据手册沿用各自的条款。

Montserrat Bold 字体采用 [SIL Open Font License 1.1](docs/fonts/OFL-Montserrat.txt)。
