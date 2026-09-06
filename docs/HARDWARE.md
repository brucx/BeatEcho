# EasyInput V2.0 硬件边界

## 权威基线与来源

产品 EasyInput V2.0、固件别名 `v2`、PCB 丝印 `AI Keyboard V2.1` 归一为同一硬件基线。当前核对日期为 2026-09-06，参考仓库合同标记的实板确认日期为 2026-08-05。

- GPIO 与硬件合同：<https://github.com/CY-CHENYUE/easyinput-board-cy/blob/main/references/board-contract.json>
- 本次读取的合同文件 Git blob SHA：`7be327312cadccf9dd7d0862e76e98bdae4f8ba7`。
- 引脚语义：<https://github.com/CY-CHENYUE/easyinput-board-cy/blob/main/references/pinout.md>
- 电源安全：<https://github.com/CY-CHENYUE/easyinput-board-cy/blob/main/references/power-and-peripherals.md>
- 证据缺口：<https://github.com/CY-CHENYUE/easyinput-board-cy/blob/main/references/known-gaps-and-errata.md>

这里只复述开发必需的硬件事实，不打包上述仓库的代码、图片、Logo、提示音或完整文档。来源不是本项目已经通过电气测试的证据。

## 采用的引脚

| 资源 | GPIO / 属性 |
| --- | --- |
| S1–S8 | 2、47、38、41、1、6、7、48；独立低有效，内部上拉 |
| 旋钮 A / B / 按压 | 17 / 16 / 18 |
| 共享外设电源 | GPIO8，高有效，LED/MIC/SPK 共用 |
| RGB 灯 | GPIO12，5 颗串联 WS2812，GRB |
| 独立状态灯 | GPIO42 |
| 扬声器 BCLK / WS / DATA OUT | 14 / 13 / 15；MAX98357A 路径 |
| 麦克风 BCLK / WS / DATA IN | 9 / 10 / 11；本项目不采集 |
| USB D− / D+ | 19 / 20；只保留原生 USB 串口日志，不占作普通 GPIO |
| BOOT | GPIO0；下载专用，不是游戏按键 |
| Flash / PSRAM 总线 | GPIO26–37，不得使用 |

ESP32-S3R8，16 MiB 外部 Flash，8 MiB 封装内 Octal PSRAM。初版核心与实时缓冲放在内部内存，不启用 PSRAM，不以额外外设或屏幕为前提。

## 共享电源顺序

`Hardware::begin()` 先预装 GPIO8 输出锁存值为低，再设为输出。下游命令脚 9/10/12/13/14/15 在配置方向前锁存为低；GPIO11 配置为 disabled/floating，关闭上下拉。此后才拉高 GPIO8，等待项目配置的稳定期，再初始化灯与 I2S。

**`CONFIG_BE_POWER_SETTLE_MS=100` 只是未经实测的项目初始值，不是板级保证的最小等待时间，也不能证明电源稳定。** 上板者需要根据实际器件与电源波形确认稳定条件并修订此设置。没有这些实测前，不能声称电气安全已验收。

游戏运行中保持共享 rail 开启；熄灯发送全黑帧，不拉低 GPIO8。初始化失败清理时先终止 I2S/RMT 使用者、恢复下游安全状态，再断 rail。严禁每次击打或闪灯时开关 GPIO8。

`power_off_after_quiesce()` 的前置条件是音频已经停止；正常运行不调用它。麦克风虽然未启用，其输入在 rail 过渡时仍保持浮空。

## 项目选择，不是硬件事实

音频初选 32 kHz、16-bit、Philips I2S、左右声道复制同一个单声道样本；I2S0 主机模式。3 个 DMA 缓冲，每个 128 帧。采样率、格式、增益、controller 选择、缓冲深度都需要在这块实板验证，并非由引脚合同自动保证。

LED 使用 RMT 10 MHz，0 码 0.4/0.9 µs、1 码 0.8/0.5 µs，300 µs 低电平复位。所有 5 个像素用同一持久缓冲发送。信号电平裕量与器件批次兼容性仍需实测；静态 pin guard 不验证这些电气参数。

## 当前未确认

完整 BOM、装配差异、共享电源爬升和负载波形、USB/电池下 V_LED 电压、3.3 V 数据输入裕量、扬声器实际音量、音频与灯效电流均未在本项目测量。没有验证电池续航或安全保护策略；初版不实现电池管理与自动休眠。建议仅先进行有人看护的 USB 台架验证。
