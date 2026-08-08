# Stopwatch Micro — Claude/Codex 双额度 + Claude 控制 改装说明

在原版 Stopwatch Micro（Codex Micro 蓝牙遥控固件）基础上新增：

- **Usage 页面**：Claude（橙色）与 Codex（蓝色）双圆环显示 5 小时窗口用量，下方条形图显示周用量，含重置倒计时
- **Claude 快捷控制**：Usage 页上的 FOCUS / ENTER / ESC 按钮，经电脑配套程序转发给 Claude 桌面版
- **Wi-Fi 数据通道**：手表通过 2.4GHz Wi-Fi 轮询电脑上的配套程序（30 秒一次），蓝牙 Codex 功能完全不受影响
- **原有功能零改动**：Pairing / Command / Agent 页、全部按键与协议保持原样。A+B 现在按 Command → Agent → Usage 三页循环；未连接蓝牙时 A+B 也可在 Pairing ↔ Usage 间切换

## 一、编译（GitHub Actions）

本地无需装任何工具链，推上 GitHub 让 CI 编译：

1. 在 GitHub 新建一个仓库（Public 或 Private 均可）
2. 把本文件夹（`Stopwatch-Micro-main`，含 `.github/` 目录）推上去：
   ```bash
   cd Stopwatch-Micro-main
   git init
   git add .
   git commit -m "Add Claude/Codex usage page and companion link"
   git branch -M main
   git remote add origin https://github.com/<你的用户名>/<仓库名>.git
   git push -u origin main
   ```
   （没装 git 的话，用 GitHub Desktop 或网页 "Upload files" 也行）
3. 打开仓库的 **Actions** 页，等 "Firmware build" 跑完（约 10 分钟）
4. 进入该次运行页面，底部 **Artifacts** 下载 `Stopwatch-Micro-v0.1.0-xxx` 压缩包

## 二、烧录

1. 解压产物，得到 bootloader.bin / Stopwatch-Micro.bin / partition-table.bin / ota_data_initial.bin / flash_args
2. 手表连 USB-C，长按电源键约 2 秒至绿灯亮（下载模式）
3. 在解压目录执行：
   ```
   py -3 -m esptool --chip esp32s3 -b 460800 --before default_reset --after hard_reset write_flash @flash_args
   ```
   （也可复用之前的 flash.bat：把新 bin 文件覆盖进去后双击）

## 三、电脑配套程序

```
cd companion
双击 start_companion.bat        # 或 py -3 claude_codex_companion.py
```

启动后会打印电脑的局域网 IP（记下来）。程序提供：

- `GET /usage`：聚合 Claude / Codex 用量。Claude 读取 Claude Code 登录凭据
  （`~/.claude/.credentials.json`），Codex 读取 Codex CLI 凭据（`~/.codex/auth.json`）。
  没装对应 CLI 的一侧显示 "--"
- `POST /control`：接收手表按钮指令，聚焦 Claude 桌面窗口并发送 Enter / Esc

> 注意：用量接口不是官方公开 API，官方更新后可能失效。失效时可在 `companion/usage_override.json`
> 手写数据（格式同 /usage 响应），手表照常显示。

## 四、配置手表 Wi-Fi（一次性）

手表正常开机、USB 连电脑，然后：

```
cd companion
py -3 -m pip install pyserial
py -3 setup_watch.py --port COM3 --ssid 你的WiFi名 --password 你的WiFi密码 --host 电脑IP
```

（COM 口在设备管理器里看；SSID/密码不能含空格；只支持 2.4GHz Wi-Fi）

配置存在手表 NVS 里，重启生效。之后手表开机自动连 Wi-Fi 找配套程序。

也可用任意串口终端（115200）手动发命令：
`debug wifi <ssid> <密码>`、`debug host <IP> 8787`、`debug usage`（查状态）、`debug usage clear`（清除配置）

## 五、使用

| 操作 | 效果 |
| --- | --- |
| A+B（已连蓝牙） | Command → Agent → Usage 循环 |
| A+B（未连蓝牙） | Pairing ↔ Usage 切换 |
| Usage 页 FOCUS | 把 Claude 桌面窗口带到前台 |
| Usage 页 ENTER | 向 Claude 发送回车（确认/批准） |
| Usage 页 ESC | 向 Claude 发送 Esc（打断） |

Usage 页顶部状态：`SETUP` = 未配置 Wi-Fi，`WIFI CONNECTING` = 正在连 Wi-Fi，
`COMPANION OFFLINE` = 找不到配套程序（检查程序是否在跑、IP 是否变了、防火墙放行 8787 端口），
`LINKED` = 一切正常。

## 已知限制

- 配套程序走局域网明文 HTTP、无鉴权，仅建议在家庭/可信网络使用
- ENTER/ESC 是全局按键注入，发送前会把 Claude 窗口切到前台
- Wi-Fi 与 BLE 共享射频（软件共存已开启）；若发现 Codex 操作延迟变大，可关配套程序对比
- web/index.html 浏览器原型尚未同步 Usage 页（不影响固件功能）
