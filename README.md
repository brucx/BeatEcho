# Beat Echo · 节拍回声

听一段节奏，在下一轮用八个实体按键把它敲回来。

**EasyInput V2.0 / AI Keyboard V2.1 的离线音乐游戏**，同时提供使用相同 C++ 游戏核心的浏览器试玩版。无需账号、云服务、外部曲库或额外屏幕。

> **当前为实验版，不是实板验收完成的稳定固件。** ESP-IDF 5.5.5 / ESP32-S3 已完成实际目标构建；核心和烧录工具有自动测试。尚未连接真实开发板进行烧录、音频延迟、电源波形或长时间稳定性测试。新固件会替换原来的 EasyInput 应用，默认先完整备份再写入。

## 直接上手

| 你的目标 | 入口 |
| --- | --- |
| **下载固件，不装编译器，烧到开发板** | **[直接烧录指南](docs/FLASHING.md)** |
| 修改 BPM、音量、旋钮方向或代码后编译 | [源码构建指南](docs/BUILD.md) |
| 第一次通电、测按键/声音、验证延迟与稳定性 | [首次上板验收](docs/BRINGUP.md) |
| 没有硬件，先在电脑/手机体验 | 从 [成功的 Actions 运行](https://github.com/brucx/BeatEcho/actions/workflows/ci.yml) 下载 `beat-echo-browser`，解压打开 `index.html` |

**固件下载：** 在 Actions 选择对应提交上所有测试成功的运行，下载 `beat-echo-flash-bundle`。其中已带三个 `.bin`、SHA-256 清单、脚本和烧录说明，**不需要 ESP-IDF**。GitHub 的 Source code ZIP 是源码，不是固件包；已发布版本也可从 [Releases](https://github.com/brucx/BeatEcho/releases) 下载 `beat-echo-esp32s3.zip`。

### macOS / Linux 的最短烧录路径

先解压上述固件包，进入能看到 `manifest.json` 的目录：

```bash
python3 -m venv .venv
source .venv/bin/activate
python -m pip install -r requirements-flash.txt
python scripts/be.py verify-package
python scripts/be.py ports
```

确认手上确实是 **EasyInput V2.0**，开发板保持开机，**短按并松开一次 BOOT**。重新列出端口，把 `YOUR_PORT` 换成实际端口：

```bash
python scripts/be.py flash --port YOUR_PORT
```

提示时输入 `FLASH`。脚本会核对芯片、安全状态与 16MB Flash，完整备份到本地 `backups/`，写入并回读校验。完成后**关机再正常开机**，重新确认端口再查看日志：

```bash
python scripts/be.py ports
python scripts/be.py monitor --port YOUR_NEW_PORT
```

Windows 的虚拟环境、COM 端口、恢复原固件和排错命令见 [直接烧录指南](docs/FLASHING.md)。不要套用通用 ESP32 的“按住 BOOT 再上电”操作，不要刷入其他 S3 板。

## 游戏怎么玩

每轮：**四拍预备 → 设备示范 → 四拍准备 → 玩家复现 → 结果**。八种程序合成鼓音、三档难度、十二关、三条生命。速度范围 60–160 BPM；默认 Easy / 90 BPM。

| 板上按键 | 鼓音 | 浏览器键盘 |
| --- | --- | --- |
| S1 / S2 / S3 / S4 | 底鼓 / 军鼓 / 闭镲 / 拍手 | A / S / D / F |
| S5 / S6 / S7 / S8 | 低通鼓 / 高通鼓 / 开镲 / 吊镲 | J / K / L / ; |

菜单旋转旋钮调整 BPM；**短按并松开旋钮**开始或继续；菜单长按至少 800 ms 后松开切换难度。游戏中旋转调音量、按下松开取消回菜单。第一版旋钮用于控制菜单，不是谱面音符。网页还支持点击/触屏、空格开始/继续、Esc 返回。

五颗灯不是八个按键的独立背光：前四颗显示小节拍位，第五颗显示生命/判定。紫色为示范、琥珀色为准备、青绿色为复现；菜单先点亮 1–3 颗表示难度。**菜单默认没有持续背景音乐**，按鼓键可试听。

Perfect / Good / Bad 的窗口分别为 ±50 / ±100 / ±150 ms；多按和错键扣分。难度通过密度、休止、和弦和乐句长度增加，不通过偷偷改变时钟。失败重试同一题，只有通过的回合计入总分。固件最佳成绩仅保存在 RAM，断电清空；网页可在本机保存。

## 工具与保护

`python scripts/be.py --help` 查看全部命令：

| 命令 | 行为 |
| --- | --- |
| `doctor` / `ports` | 检查本地环境 / 列串口，不自动选设备 |
| `configure` / `build` / `package` | 配置、编译、生成完整固件包，不烧录 |
| `verify-package` | 离线校验文件哈希、镜像目标和写入范围 |
| `flash --port ...` | 显式确认 → 身份检查 → 完整备份 → 写入 → 校验 |
| `flash --port ... --dry-run` | 离线展示计划，绝不打开串口 |
| `backup` / `restore` | 完整备份；恢复仅接受匹配哈希和同一芯片 MAC |
| `monitor --port ...` | 只读日志，Ctrl+C 退出，不主动复位 |

没有默认整片擦除、`--force`、自动换串口或 eFuse 修改。备份失败停止写入；不绕过 Secure Boot / Flash Encryption。**备份可能含 Wi-Fi 密码和设备配置，不能提交 GitHub。**

## 开发与验证

```bash
# 已激活 ESP-IDF 5.5.5 的终端
python scripts/be.py build
python scripts/be.py package
```

普通 push / PR 自动执行核心测试、WASM 回归、三平台烧录工具测试、ESP32-S3 构建与固件打包。维护者显式推送 `v*` 标签后，只有全部通过才创建实验性预发布；CI 从不烧录硬件。实际证据、命令和未验证项见 [VALIDATION.md](docs/VALIDATION.md)。

```text
core/         C++17 判定、谱面、消抖、编码器、鼓音合成
firmware/     ESP-IDF 引脚、电源、I2S、RMT 与任务适配
web/          同一核心的 WASM 接口与离线网页源码
scripts/      构建、打包、防护烧录、备份恢复和校验
tests/       核心 / WASM / 烧录工具回归测试
docs/        用户指南、硬件边界与验证记录
```

## 硬件与范围

仅针对 ESP32-S3R8 / 16 MiB Flash 的当前 EasyInput 基线。GPIO8 是 LED、麦克风、扬声器共享电源，不是灯光开关；首次用低音量、稳定 USB 供电，并遵循 [硬件边界](docs/HARDWARE.md)。当前 100 ms 上电等待只是项目策略，仍需实测资格确认。

不包含 BLE/HID、Wi-Fi、麦克风评分、MIDI、OTA、电池管理和自动休眠。不要把它当作保留出厂功能的插件，也不要把网页运行成功等同于板端性能和电气安全合格。

项目按硬件事实独立实现，没有打包上游非商业代码、品牌图或歌曲。仓库所有者尚未选定额外分发许可；现有仓库可见性未改变。详见 [来源与许可说明](NOTICE.md) 与 [架构说明](docs/ARCHITECTURE.md)。
