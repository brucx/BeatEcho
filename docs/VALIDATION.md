# 验证记录与证据边界

## 当前证据

这是源码导入和硬件烧录工具完善的记录，不是实板验收证明。仓库 CI 的每次运行绑定具体提交；使用固件时应选择所有作业成功的运行，并核对包内 `manifest.json.source_commit`。

| 层级 | 已执行内容 | 不代表什么 |
| --- | --- | --- |
| 原生核心 | Debug ASan/UBSan 与 Release 构建、37 个用例、1,097,102 次检查；其中 14,400 个生成回合 | 不代表 ESP32-S3 的性能或电气行为 |
| WASM | 同一 C++ 核心重新编译，1,440 个完整回合、15,636 次精确命中、重试/取消/11 个音色及 HTML 嵌入检查 | 不代表浏览器或板端端到端时延 |
| 烧录工具 | 40 个离线单元测试：坏包/错目标/错误地址/备份失败/写入失败/同芯片恢复限制/命令参数与干跑 | 测试使用合成镜像与模拟串口调用，不是烧录过真板 |
| 板级静态检查 | GPIO、共享电源初始化顺序、USB 日志与 FreeRTOS 配置守卫 | 不验证电压、波形、器件或装配 |
| ESP-IDF 目标 | 首次真实 ESP-IDF 5.5.5 / ESP32-S3 构建成功，产生 bootloader、partition table 和 application | 不代表固件已经在实物启动 |
| 真实 USB 写入 / 读回 | 未执行 | 必须由操作者在明确设备上验证 |
| 按键、编码器、音频、灯光、电源、续航 | 本项目未执行实板测量 | 没有可承诺的实际延迟、功耗或稳定性数字 |

首次目标构建的不可变源码提交：`190d6cad73424699daa5135585af83f7c60c3701`。
[实际构建运行与日志](https://github.com/brucx/BeatEcho/actions/runs/34044976505)。这是早期固件导入提交；后续完整工具链与固件包的验证应看 [当前 CI](https://github.com/brucx/BeatEcho/actions/workflows/ci.yml)，不要混淆两个提交。

CI 增加 Linux、macOS、Windows 三个平台的烧录工具测试、固定 esptool 依赖安装、CLI 参数检查，以及固件打包后的离线校验。跨平台作业是否通过，以对应运行结果为准，不以配置文件存在为依据。

## 本机复现

```bash
cmake -S . -B build-host -DCMAKE_BUILD_TYPE=Debug -DBE_SANITIZERS=ON
cmake --build build-host
ctest --test-dir build-host --output-on-failure
./build-host/beat_echo_tests
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
ctest --test-dir build-release --output-on-failure
python -m unittest discover -s tests -p test_flash_tools.py -v
python scripts/check_board.py
python scripts/build_web.py
node tests/test_wasm.mjs
```

在激活 ESP-IDF 5.5.5 的终端追加：

```bash
python scripts/be.py build
python scripts/be.py package
python scripts/be.py verify-package
python scripts/be.py flash --port DRY_RUN_ONLY --dry-run
```

最后一条不会打开任何串口。完整烧录成功后还应保存启动日志、实际音频与输入时间测量、电源轨波形、长时间运行与异常恢复记录；按 [BRINGUP.md](BRINGUP.md) 验收。

## 已知未覆盖

未测特定 Windows/macOS/Linux 驱动与真实板的 USB 枚举、ROM/stub 切换、连续完整备份/写入/校验、断电恢复流程。未验证 Secure Boot/Flash Encryption 设备的真实拒绝过程；脚本设计为无法明确确认关闭即拒绝，不提供绕过。未验证不同板批次的 LED 电平裕量、I2S 参数、GPIO8 稳定等待和编码器手感。

构建产物始终标记 `hardware_verified: false`。不要仅修改该字段来表示验收；需要与实际板号/批次和测量记录相对应的新验证证据。
