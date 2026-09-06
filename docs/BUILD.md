# 从源码构建

只想玩真机，优先使用 [预编译固件包](FLASHING.md)。开发或修改默认 BPM、音量、偏移、旋钮方向时再安装 ESP-IDF。

## 固定开发环境

目标为 `esp32s3`，SDK 固定 **ESP-IDF 5.5.5**。没有把其他 S3 板、旧 SDK 或其他 EasyInput 应用的配置当作本项目默认值。建议 Python 3.10–3.13；ESP-IDF 的安装程序会准备自己的工具与 Python 环境。

官方版本入口：<https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32s3/get-started/index.html>

**macOS / Linux**：先按官方指南安装系统依赖（Git、CMake、Ninja、Python 及对应系统库），再执行：

```bash
mkdir -p ~/esp
cd ~/esp
git clone --branch v5.5.5 --recursive https://github.com/espressif/esp-idf.git esp-idf-v5.5.5
cd esp-idf-v5.5.5
./install.sh esp32s3
. ./export.sh
```

`~/esp` 只是示例目录；已存在 SDK 不应覆盖。每开一个终端都要重新 `. /your/path/esp-idf-v5.5.5/export.sh`。递归子模块不可省略，GitHub 自动源码归档不包含全部子模块。首次安装需要网络与足够磁盘空间。

**Windows**：使用 Espressif 官方 ESP-IDF Tools Installer 或 VS Code ESP-IDF 扩展，选择 **5.5.5**，打开它提供的 **ESP-IDF PowerShell / Command Prompt**。建议使用无空格、无中文的短路径，如 `C:\src\BeatEcho`。预编译包烧录可用普通 PowerShell，不必用 WSL；WSL 编译环境能访问 USB 与否需要另外处理，初版不依赖 WSL USB 转发。

## 获取源码并构建

```bash
git clone https://github.com/brucx/BeatEcho.git
cd BeatEcho
python scripts/be.py doctor
python scripts/be.py configure
python scripts/be.py build
python scripts/be.py package
```

`configure` 是可选的图形终端配置菜单；可保留默认值。`build` **只编译，不烧录**；`package` 从成功构建的 `flasher_args.json` 读取地址，将三个镜像、校验清单、烧录脚本和用户指南打包到 `dist/beat-echo-esp32s3/` 及同名 ZIP。包中记录源提交和 IDF 版本；本地未提交改动会标记 `+dirty`。只打包固定布局的 Beat Echo 构建，不支持任意固件目录。

默认打包输出存在时会拒绝覆盖；明确检查后移走旧包，或使用 `python scripts/be.py package --out dist/my-new-bundle`。不要把 `firmware/build` 里的单个应用镜像直接当成完整出厂镜像。

等价底层编译命令：

```bash
idf.py --version
idf.py -C firmware menuconfig
idf.py -C firmware build
```

## 配置项

`menuconfig → Beat Echo`：

| 配置 | 初始值 / 用途 |
| --- | --- |
| `BE_DEFAULT_BPM` | 90；允许 60–160 |
| `BE_DEFAULT_DIFFICULTY` | 0=Easy，1=Normal，2=Hard |
| `BE_VOLUME` | 25%；硬上限 60%，首次保持低音量 |
| `BE_INPUT_OFFSET_MS` | 0；正数从输入时间扣除，范围 ±200 ms，需实测后校准 |
| `BE_ENCODER_REVERSE` | 关闭；只改变方向解释，不改变引脚 |
| `BE_POWER_SETTLE_MS` | 100 ms 项目策略，**不是经过验证的电气最小值** |

固定基础：16 MiB Flash / DIO / 40 MHz，USB Serial/JTAG 日志，FreeRTOS 1000 Hz。初版不启用 8 MiB PSRAM，实时缓冲放在内部内存。不要更改 `sdkconfig` 目标、Flash 容量、板级引脚或保留资源来“绕过”构建失败。

`sdkconfig.defaults` 只在配置首次创建或对应选项尚未设置时提供默认值。已有 `firmware/sdkconfig` 不会被默认值文件覆盖；优先使用 menuconfig 修改。需要干净配置时先备份自定义项，再删除**生成的** `firmware/sdkconfig`、`firmware/sdkconfig.old` 和 `firmware/build/`，不能删源码或固件备份。

## 测试与网页构建

```bash
cmake -S . -B build-host -DCMAKE_BUILD_TYPE=Debug -DBE_SANITIZERS=ON
cmake --build build-host
ctest --test-dir build-host --output-on-failure
python -m unittest discover -s tests -p 'test_flash*.py' -v
python scripts/check_board.py
python scripts/build_web.py
node tests/test_wasm.mjs
```

本机测试需要 CMake 3.20+、C++17 编译器；网页生成需要支持 wasm32 的 Clang、wasm-ld，WASM 测试需要 Node.js 20+。Windows MSVC 未作为本机 CMake 测试配置验证；开发者可使用 Linux/macOS 的 GCC/Clang 或 CI。这些工具与下载固件包烧录无关。

网页源文件版本控制在 `web/`；`beat_echo.wasm` 和自包含 `index.html` 由构建生成。不会要求用户手工复制 C++ WASM 字节。直接试玩可下载成功 CI 的 `beat-echo-browser` artifact，解压后打开 `index.html`。

## CI 与发布

每次 push / pull request 执行 C++/WASM 测试、跨平台烧录工具单元测试、ESP-IDF 5.5.5 目标构建。CI **没有连接真实硬件**。固件构建成功后生成：

- `beat-echo-flash-bundle`：解压即可使用的烧录包，无需 ESP-IDF。
- `beat-echo-debug-symbols`：ELF/MAP、构建配置与地址资料，用于同一提交的排错。
- `beat-echo-browser`：自包含试玩网页。

发布标签 `v*` 的流水线只有在所有测试与构建成功后才上传 Release 文件；是否创建版本标签由仓库维护者决定。普通源码推送不会把未测硬件标成稳定发布，也不会执行任何烧录动作。
