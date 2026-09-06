# 直接烧录到 EasyInput V2.0

这份指南同时放在源码仓库和预编译固件包里。**预编译包只需要 Python，不需要安装 ESP-IDF、CMake 或编译器。** 所有串口操作都发生在你自己的电脑；不会上传备份、MAC 或日志。

## 先确认硬件与风险

仅支持 **EasyInput V2.0 / PCB 丝印 AI Keyboard V2.1，ESP32-S3R8，16 MiB Flash**。芯片型号相同不代表其他 ESP32-S3 开发板引脚相同。不要刷入通用 S3 板。

新固件会替换原来的 EasyInput 应用，原来的键盘、BLE、麦克风等功能不保留。首次使用 USB 数据线直连电脑，保持供电稳定并有人看护。当前版本没有实现电池管理/自动休眠，不用于无人看护电池运行。

默认烧录流程：校验包 → 操作者确认板型和端口 → 检查安全状态与 16MB Flash → 完整备份 16 MiB → 写入三个固件段 → 回读校验。任何一步失败就停止，不会自动换端口、强制写入、整片擦除或修改 eFuse。启用 Secure Boot 或 Flash Encryption 的设备会被拒绝。

构建和回读校验通过只证明代码可编译、写入字节一致，**不能代替真实按键、音频、共享电源和长期稳定性验证**。100 ms 电源稳定等待是项目初始策略，未被证明为板级安全最小值。

## 1. 下载正确的固件包

仓库：<https://github.com/brucx/BeatEcho>

进入 **Actions → Beat Echo verification**，选择目标分支/提交上全部成功的运行，下载 **beat-echo-flash-bundle** artifact 并解压。Actions artifact 可能要求登录 GitHub，且有保存期限。若 Releases 已发布某版本，也可下载其 `beat-echo-esp32s3.zip`。不要下载 GitHub 自动生成的 Source code ZIP 然后找固件：源码包不包含已编译 `.bin`。

解压后应直接看到：

```text
manifest.json
bootloader.bin
partition-table.bin
beat_echo.bin
requirements-flash.txt
scripts/be.py
scripts/flash.sh
scripts/flash.ps1
FLASHING.md
```

只使用这个仓库可信构建的包。SHA-256 能发现损坏，**不是来源认证或数字签名**。在 `manifest.json` 查看 `source_commit`，应与所选 Actions/Release 的提交一致；不要混用不同运行中的二进制。

## 2. 准备一次 Python 环境

推荐 Python **3.10–3.13**。已有 ESP-IDF 环境的用户也建议另开普通终端，为预编译烧录建一个小型虚拟环境，避免 esptool 版本冲突。脚本固定使用 esptool 4.11.0，不自动改全局 Python。

**macOS / Linux**，终端进入解压目录：

```bash
python3 -m venv .venv
source .venv/bin/activate
python -m pip install -r requirements-flash.txt
python scripts/be.py doctor
python scripts/be.py verify-package
```

**Windows PowerShell**，进入解压目录：

```powershell
py -3 -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r requirements-flash.txt
.\.venv\Scripts\python.exe scripts\be.py doctor
.\.venv\Scripts\python.exe scripts\be.py verify-package
```

Windows 不必更改 PowerShell 执行策略，也不必激活虚拟环境；直接使用上面的 Python 路径即可。下面命令中的 `python` 可替换为 `.\.venv\Scripts\python.exe`。

## 3. 进入下载模式并选择端口

连接能传数据的 USB 线，保持开发板**开机**。**短按并松开一次 BOOT**，等待电脑出现 ESP32-S3 下载串口。不要按住 BOOT 配合上电；这块板没有独立的用户 RESET 键。不要因为通用 ESP32 教程这样写就套用到此板。

```bash
python scripts/be.py ports
```

macOS 常见 `/dev/cu.usbmodem…`，Linux 常见 `/dev/ttyACM0`，Windows 常见 `COM5`。这些只是格式示例，不是你当前端口。通过插拔前后对比确认，多个设备时先拔掉无关设备。脚本不会自动选择第一个端口。

关闭 Arduino、ESP-IDF monitor、串口助手及其他占用该串口的软件。脚本使用 `--before no_reset --after no_reset`，不依赖通用复位手势；必须先手动进入下载模式。

## 4. 一条命令备份并烧录

把示例端口替换为刚确认的实际端口：

```bash
# macOS 示例
python scripts/be.py flash --port /dev/cu.usbmodem1101

# Linux 示例
python scripts/be.py flash --port /dev/ttyACM0

# Windows 示例
.\.venv\Scripts\python.exe scripts\be.py flash --port COM5
```

交互提示时核对板型和端口，输入 `FLASH` 确认。默认 460800 波特；通信不稳定时**先检查线材和供电，再**显式降低到 `--baud 115200` 重试。备份失败不会继续写入。

默认备份保存在当前项目/固件包根目录的 `backups/`：一份 `.bin` 加同名 `.json`。备份含原有配置、可能的 Wi-Fi 密码和设备身份等私人内容，**不要上传 GitHub、公开网盘或 Issue**。把两份文件一起保存在可信位置。可以用 `--backup-dir` 指定其他本地目录。

需要查看计划而不打开串口：

```bash
python scripts/be.py flash --port YOUR_PORT --dry-run
```

`--skip-backup` 是明确放弃备份，不是推荐的首次操作；`--yes` 是明确确认板型、端口与写入行为，只供已人工确认目标后的脚本化操作。不会因为有这两个参数就跳过镜像/型号/安全状态验证。

固件包与源码仓库分开放置时：

```bash
python scripts/be.py flash --bundle /path/to/unpacked-bundle --port YOUR_PORT
```

这里 `--bundle` 接受**已解压目录**，不接受 ZIP 路径。macOS/Linux 还可调用 `bash scripts/flash.sh --port YOUR_PORT`；已激活 Python 环境且允许本地脚本的 PowerShell 可调用 `scripts/flash.ps1`，两者最终调用同一个防护实现。

## 5. 重新上电，验证第一局

看到写入及 `verify_flash` 成功后，**关机再正常开机**。不需要继续按 BOOT。重新运行 `ports`，因为下载模式与应用模式的串口名称可能变化：

```bash
python scripts/be.py ports
python scripts/be.py monitor --port YOUR_NEW_PORT
```

监视器只读日志，不主动复位；Ctrl+C 退出。驱动或操作系统仍可能在打开串口时改变控制线，因此首次观察要有人看护。

预期：状态灯慢闪、菜单亮灯；菜单本来没有连续背景音乐。逐个按 S1–S8 应出现八种不同鼓音。短按并松开旋钮开始：四拍预备 → 示范 → 四拍准备 → 复现 → 结果。先用默认 Easy / 90 BPM，逐键、长按、快速重复、双键和弦，再测十二关与反复返回菜单。

成功打印启动日志不等于全功能验收。发现红灯快闪、持续 `Audio/LED fault`、重启、明显失真或发热，立即停止挑战、断电检查，不要先调高音量或解除故障保护。

## 6. 单独备份与恢复原固件

备份不写 Flash，但会访问下载模式设备，需要同样核对目标：

```bash
python scripts/be.py backup --port YOUR_PORT
```

恢复只接受本脚本生成的完整 `.bin` 和匹配 `.json`，会检查 SHA-256、长度与**同一芯片 MAC**。按上面的 BOOT 操作重新进入下载模式后：

```bash
python scripts/be.py restore --port YOUR_PORT --file backups/easyinput-TIMESTAMP-MAC.bin
```

恢复将覆盖整个 16 MiB，是明确的回滚操作；完成后关机再开机。备份不是通用跨设备镜像，也不包含 eFuse、外部存储或芯片之外的状态。没有原固件备份时不要猜写入地址，向原固件提供方取得对应镜像与恢复说明。

## 常见问题

| 现象 | 处理 |
| --- | --- |
| 找不到串口 | 换数据线、直连 USB、确认开机、短按松开 BOOT，比较插拔前后端口；不要先改 GPIO |
| `Permission denied`（Linux） | 将当前用户加入系统串口组（常见 `dialout` 或 `uucp`），注销再登录；不建议用 sudo pip 或长期 chmod 666 |
| `Access denied` / 端口忙 | 关闭其他串口程序；USB 刚重新枚举时重新列端口 |
| 连接超时 / No serial data received | 核对 BOOT 手势、目标端口、线材、供电；必要时降低波特率；不要使用 `--force` |
| 错板/容量/安全模式被拒绝 | 停止。确认是否确为当前基线；脚本不支持加密量产设备，也不提供绕过选项 |
| SHA-256 / 大小不符 | 重新下载同一可信构建的完整包，不混搭三个 `.bin` |
| 写入中断 | 不认为成功，不随机擦除；重新进入下载模式，核对同一设备后重刷完整包或恢复备份 |
| 刷完仍是下载模式 | 关机再正常开机；重新枚举串口 |
| 有日志没有声音 | 菜单默认安静，先按 S1；检查音量和错误日志，遵循 `BRINGUP.md` 实测音频路径 |
| 旋钮方向反了 | 源码配置 `CONFIG_BE_ENCODER_REVERSE` 后重新编译；不要交换硬件引脚 |
| 需要完整崩溃堆栈 | 下载相同提交的 debug-symbols，使用对应 ELF 与 ESP-IDF monitor 解码 |

## 技术依据

板型与 BOOT 手势优先采用板级合同，而非通用芯片教程：
<https://github.com/CY-CHENYUE/easyinput-board-cy/blob/main/references/board-contract.json>

esptool 4.x 写入、读取与校验命令：
<https://docs.espressif.com/projects/esptool/en/release-v4/esp32/esptool/basic-commands.html>

ESP32-S3 原生 USB Serial/JTAG：
<https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32s3/api-guides/usb-serial-jtag-console.html>
