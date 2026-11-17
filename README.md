# M5StopWatch-UserDemo
M5Stack StopWatch user demo for hardware evaluation.

## Build

### Fetch Dependencies

```bash
python3 ./fetch_repos.py
```

### Tool Chains

[ESP-IDF v5.5.4](https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32s3/index.html)

### Build

```bash
idf.py build
```

### Flash

```bash
idf.py flash
```

## Automatic network date and time

After Wi-Fi obtains an IP address, the device synchronizes its date/time via SNTP
in the background and saves the UTC time to the RX8130 RTC. It refreshes every
six hours and immediately after reconnecting. A 30-second timeout triggers a
retry with the next server after one minute; offline operation keeps the clock.

The default display timezone is Beijing time (`CST-8`, UTC+8); an existing saved
timezone is preserved. Online synchronization can overwrite manual clock edits.
See [network time checks](tests/time_sync/README.md) for tests and device checks.

## FluidBox Integration
With genuine thanks to [omiya-bonsai](https://github.com/omiya-bonsai) for the open-source "FluidBox" fluid simulation demo for the M5Stack Stopwatch.
Incorporated into the *M5StopWatch-UserDemo* project by [Simon J. Stuart](https://github.com/LK-Simon)

## Codex 用量待机屏

用量服务已独立为 [codex_usage 组件](components/codex_usage/README.md)，通过回调接入设备状态、时钟和配对码存储。

在 **Settings → Standby display → Codex usage** 选择 Codex 用量模式。
无操作 60 秒后以低亮度显示时间、额度窗口已用百分比、进度条和距离重置的时间。
窗口名称取自 Codex 返回的时长（例如 `5h` / `7d`），不是 token 数量或 API 费用。
设置会保存，触摸或按键唤醒；秒表运行和闹钟响铃期间沿用原有唤醒逻辑。

首次连接：

1. 让设备和电脑连接同一个可信 Wi-Fi，在设备上打开 **Connect Codex**，查看 IP 和配对码。
2. 电脑需要 Python 3 和已登录 ChatGPT 账户的 Codex CLI；应使用与要查看的 Codex 相同的账户及 `CODEX_HOME`。
3. 在项目目录运行以下命令，并按提示输入设备上的配对码：

   ```bash
   python3 scripts/sync_codex_usage.py --device 192.168.1.42
   ```

将示例 IP 替换为设备 IP。脚本每 60 秒同步一次，保持运行即可，`Ctrl+C` 停止。
可用 `--once` 单次同步，`--codex /path/to/codex` 指定 CLI，`--name "My Mac"` 指定屏幕显示的电脑名称。
脚本自动查找 PATH 中的 CLI，或 macOS ChatGPT/Codex 应用内置的 CLI。
这是一条独立的本地同步连接，不会自动读取桌面应用当前打开的聊天或账户切换状态。

数据使用官方 [Codex app-server `account/rateLimits/read`](https://learn.chatgpt.com/docs/app-server#6-rate-limits-chatgpt)
接口获取，仅将额度窗口和电脑名称发送到设备；账户令牌和聊天内容不会发送到设备。
API-key-only 账户或没有返回额度窗口的账户会显示同步错误，不会显示为 0% 用量。
首次未连接显示等待提示；停止同步超过 3 分钟、Wi-Fi 断线或额度重置后尚未刷新时标记为缓存。
设备重启后等待新数据，避免把旧额度当成实时值。

设备仅在 Codex 模式且连接普通 Wi-Fi 时启用 `8765` 端口的接收服务，通过配对码校验。
本地 HTTP 为明文，只在可信局域网使用，不要将端口映射到公网；恢复出厂设置会重新生成配对码。
验证方法见 [Codex usage tests](tests/codex_usage/README.md)。

## Power LED Off while Display On
Incorporated into the *M5StopWatch-UserDemo* project by [Simon J. Stuart](https://github.com/LK-Simon) the green power indicator LED will now switch off when the AMOLED display is on. This will save a small amount of power, but also eliminate the distracting green LED when it is not necessary.

## PlatformIO/PIOArduino build support
Incorporated into the *M5StopWatch-UserDemo* project by [Simon J. Stuart](https://github.com/LK-Simon) you can now load the repository folder in Visual Studio Code with either PlatformIO or PIOArduino installed, and build/upload the demo directly to your M5Stack StopWatch device.

## Weather component

天气服务已拆为 [weather 组件](components/weather/README.md)，通过回调接入网络状态、计时和存储。待机界面位于 `main/apps/common/standby/`；原有城市、模式和天气缓存保持兼容。
