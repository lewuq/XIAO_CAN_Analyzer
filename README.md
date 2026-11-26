<h1 align="center">XIAO CAN Analyzer</h1>

<p align="center">
  A handheld, listen-only <strong>Classical CAN and CAN FD analyzer</strong> for the <strong>XIAO STM32C5 Console</strong>.<br>
  Automatically detect common bitrates, inspect frames on the <strong>0.95-inch AMOLED</strong>, and log traffic to external Flash.
</p>

<p align="center">
  <a href="https://www.seeedstudio.com/"><img src="docs/badges/seeed.png" height="26" alt="Seeed Studio"></a>
  <a href="https://wiki.seeedstudio.com/xiao_stm32c5_getting_started/"><img src="docs/badges/stm32c5.png" height="26" alt="XIAO STM32C5"></a>
  <a href="#hardware"><img src="docs/badges/amoled.png" height="26" alt="SH8501G AMOLED display"></a>
  <a href="https://registry.platformio.org/tools/platformio/toolchain-gccarmnoneeabi"><img src="docs/badges/arm-gcc.png" height="26" alt="Arm GCC"></a>
  <a href="https://platformio.org/"><img src="docs/badges/platformio.png" height="26" alt="PlatformIO"></a>
  <a href="https://docs.zephyrproject.org/"><img src="docs/badges/zephyr.png" height="26" alt="Zephyr RTOS"></a>
</p>

<p align="center">
  <a href="#bitrate-detection"><img src="docs/badges/can-fd.png" height="26" alt="CAN / CAN FD"></a>
  <a href="#capture"><img src="docs/badges/flash.png" height="26" alt="16 MiB external Flash"></a>
</p>

<p align="center">
  <a href="firmware/latest/firmware_can_analyzer_v5_50pct.uf2?raw=true"><img src="docs/badges/download.png" height="26" alt="Download v5 UF2"></a>
  <a href="#version-history"><img src="docs/badges/history.png" height="26" alt="Firmware history"></a>
  <a href="#display-previews"><img src="docs/badges/preview.png" height="26" alt="v5 UI preview"></a>
  <a href="LICENSE"><img src="docs/badges/license.png" height="26" alt="MIT License"></a>
</p>

<p align="center">
  <strong><a href="#quick-start">Quick start</a> · <a href="#hardware">Hardware</a> · <a href="#controls">Controls</a> · <a href="#bitrate-detection">Bitrate detection</a> · <a href="#can-fd-bench-test">Bench test</a> · <a href="#build-from-source">Build from source</a></strong>
</p>

<p align="center">
  <img src="docs/badges/english-current.png" height="26" alt="English">
  <a href="README_CN.md"><img src="docs/badges/chinese.png" height="26" alt="简体中文"></a>
</p>

---

## Quick start

1. Download the [v5 UF2](https://github.com/lewuq/XIAO_CAN_Analyzer/raw/refs/heads/main/firmware/latest/firmware_can_analyzer_v5_50pct.uf2).
2. Connect the Console over USB-C. Double-press the XIAO RESET button, then copy the UF2 to the `XIAOC5BOOT` drive. The board restarts after copying. [Seeed UF2 guide](https://wiki.seeedstudio.com/xiao_stm32c5_getting_started/#bootloader-usage)
3. Set **SW1 to the CANH/CANL path (A contacts)**. Connect J4-1 to **CANH**, J4-2 to **CANL**, and J4-3 to **GND**.
4. With traffic present, let AUTO find a bitrate. For a known bitrate, open **RATE**, short-press **BOOT** to choose a preset, and hold **BOOT** to apply it.

## Hardware

| Part | Role |
| --- | --- |
| **XIAO STM32C5** | FDCAN2, onboard TCAN3403-Q1 transceiver and 16 MiB of external Flash |
| **Console V1.0** | SH8501G SPI AMOLED, 240 × 120 landscape UI, D0 and BOOT buttons · [Display datasheet (PDF)](docs/datasheets/ZJY095-2424AM002-C15.pdf) |
| **J4** | Pins 1–3 carry CANH, CANL and GND with SW1 on the A contacts |
| **SW1** | Routes J4 either to the onboard transceiver or to FDCAN1 logic-level TX/RX |

Use the **A contacts** with this firmware. The B contacts need an external CAN FD transceiver and firmware configured for FDCAN1. When tapping into an already terminated bus, check JP1 and disconnect the XIAO’s onboard 120 Ω termination. When the Console terminates one physical end, use a single 120 Ω termination at that end. [XIAO STM32C5 CAN FD guide](https://wiki.seeedstudio.com/xiao_stm32c5_canfd_usage/)

## Controls

| Control | Short press | Hold about 0.7 s |
| --- | --- | --- |
| **D0 / A** | Cycle OVERVIEW → FRAMES → DATA → STORAGE → RATE | Clear current capture statistics |
| **BOOT / B** | Run the page action below | Toggle HOLD on the first three pages, load a snapshot in STORAGE, or apply a bitrate in RATE |

| Page | What you see | B short press |
| --- | --- | --- |
| **OVERVIEW** | Nominal and data rates, scan state, frame rate, ID count and receive loss | Toggle HOLD |
| **FRAMES** | Three ID entries per screen with the latest length and frame type | Select the next ID |
| **DATA** | Latest payload for the selected ID, with age and frame count | Show the next 16 bytes |
| **STORAGE** | Recording state, batch count, logger drops and saved snapshot | Start or stop recording |
| **RATE** | Scan state, active trial or configuration, and pending preset | Select AUTO or the next of 15 presets |

## Bitrate detection

AUTO starts at boot and temporarily tries candidate timings in FDCAN2 bus monitoring mode. The normal trial is about 300 ms. Partial evidence can extend a trial to 12 seconds. AUTO needs ten valid frames to confirm the nominal rate, and ten valid FD+BRS frames at a candidate timing to report a data rate. A quiet bus cannot supply bitrate evidence.

- **SCANNING / DATA SCAN** shows a trial in progress. `DATA N/A` means BRS evidence is insufficient.
- **LOCKED / IDLE** holds the confirmed rate through quiet periods. Apply AUTO again after moving to a different bus.
- **MANUAL** uses the selected nominal/data pair. The RATE page shows `SET`, while the overview reports data rate once it is confirmed.
- **NO VALID** means the last pass lacked sufficient evidence. AUTO retries.

Nominal candidates, in **kbit/s**, are 10, 20, 50, 100, 125, 250, 500, 800 and 1000. Data candidates, in **Mbit/s**, are 0.5, 1, 2, 3, 4, 6 and 8. The driver checks whether the controller clock can produce the timing. High-speed operation and scan latency depend on actual bus traffic and wiring. Frames received while scanning are excluded from capture totals and logs.

## Display previews

The **v5 firmware renderer** generated these screens with simulated CAN traffic.

![v5 AUTO scan, nominal lock, FD+BRS lock, and manual bitrate views](docs/v5-rate-preview.png)

![v5 ID list, payload, Flash recording and retained idle lock](docs/v5-capture-preview.png)

## CAN FD bench test

[Download the bench transmitter UF2](firmware/test/firmware_canfd_test_500k_2m.uf2?raw=true) and flash it to a separate XIAO STM32C5 used as the transmitter. It uses FDCAN2 at **500 kbit/s nominal / 2 Mbit/s data**, with BRS, standard ID `0x123`, a 64-byte pseudorandom payload, and a configured 10 ms interval after successful sends.

This firmware generates traffic for a dedicated test bus. Real buses can have different bitrates, frame IDs, payloads, and traffic patterns. Use the photos to check the locked bitrate, frame type, and payload presentation. Compare measured frame counts against a reference on the actual bus when evaluating frame rate or loss.

The test source and build settings are plain-text references. To rebuild in a separate PlatformIO project, download them, remove the final `.txt` suffix, and use the paths below.

| Project path | Text reference |
| --- | --- |
| `src/main.c` | [main.c.txt](docs/can-fd-test/main.c.txt) |
| `platformio.ini` | [platformio.ini.txt](docs/can-fd-test/platformio.ini.txt) |
| `zephyr/prj.conf` | [prj.conf.txt](docs/can-fd-test/prj.conf.txt) |
| `zephyr/app.overlay` | [app.overlay.txt](docs/can-fd-test/app.overlay.txt) |
| `zephyr/CMakeLists.txt` | [CMakeLists.txt.txt](docs/can-fd-test/CMakeLists.txt.txt) |

| OVERVIEW with AUTO lock | DATA with a 64-byte CAN FD frame |
| --- | --- |
| <a href="docs/photos/v5-canfd-overview.jpg"><img src="docs/photos/v5-canfd-overview.jpg" width="240" alt="Bench photo of the physical v5 analyzer showing 500K / 2M locked"></a> | <a href="docs/photos/v5-canfd-data.jpg"><img src="docs/photos/v5-canfd-data.jpg" width="240" alt="Bench photo of the physical v5 analyzer showing the payload of frame 0x123"></a> |

## Capture

- **HOLD** freezes the view and queues a snapshot of the statistics and up to 64 ID entries in Flash. Live reception continues.
- **Recording** keeps a rolling history in the upper 8 MiB of external Flash. Different bitrate configurations go into separate batches. After stopping, wait for `READY` before disconnecting power.
- **Export** works from the USB CDC terminal at 115200 baud.

```text
capture info
capture dump 1
```

Use the batch number shown by `capture info` in place of `1`. CSV rows include timestamp, ID, flags, length, payload, nominal bitrate and configured data bitrate. The last two fields are zero in records captured by earlier firmware that did not save them.

## Build from source

Install [PlatformIO](https://platformio.org/) and the [Seeed STM32C5 platform](https://wiki.seeedstudio.com/xiao_stm32c5_getting_started/#platformio). Build from the repository root.

```sh
pio pkg install -g -p "https://github.com/Seeed-Studio/platform-seeedboards.git"
pio run -e seeed-xiao-stm32c5
```

The build produces `.pio/build/seeed-xiao-stm32c5/firmware.uf2`. The [overlay](zephyr/app.overlay) maps SPI3, the display control GPIOs, both buttons and the external Flash partitions. [Host tests](test/test_host.c) cover auto-detection, navigation, rendering and window alignment. The [NOR test](test/test_nor.py) checks recovery and bitrate-separated batches with simulated Flash.

## Version history

| Firmware | What changed |
| --- | --- |
| [v5](firmware/latest/firmware_can_analyzer_v5_50pct.uf2) | Added automatic bitrate detection, manual presets and dynamic bitrate display. |
| [v4.1](firmware/history/firmware_can_analyzer_v4_1_50pct.uf2) | Aligned partial display windows to fix corruption during page changes. |
| v4 | Added external Flash capture, snapshots and partial display updates. |
| [v3](firmware/history/firmware_can_analyzer_v3_50pct.uf2) | Added the first standalone FDCAN2 analyzer UI and button controls. |

## License

The original project and bench transmitter code are available under the [MIT License](LICENSE). Third-party components and the vendor datasheet retain their respective terms.
