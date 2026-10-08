# WiiChannelMenu

一个由 Priiloader 开机自动加载的极简 Channel 启动器（原版 Wii）。

- 只显示 `sd:/channelmenu/config.ini` 中配置的 Channel，用图片（以及可选的中文名称）组成网格。
- 选中后按 **A** 直接启动 Channel，不经过 banner 确认画面。
- **Home**（Wii 遥控器或经典手柄）返回系统菜单。没有其他功能。
- 配置文件不存在或有问题 → 直接返回系统菜单（原因写入 `sd:/channelmenu/log.txt`）。

## 操作

| 操作 | Wii 遥控器（竖握） | 经典手柄 / Pro 手柄 |
|---|---|---|
| 移动选择 | 十字键 | 十字键 / 左摇杆 |
| 启动 | A | A |
| 返回系统菜单 | Home | Home |

按住方向键 0.4 秒后会自动连续移动。主机 Reset 键 = 返回系统菜单，Power 键 = 关机。

## 编译

需要 devkitPro（devkitPPC + libogc + libfat）：

```sh
export DEVKITPRO=/opt/devkitpro DEVKITPPC=/opt/devkitpro/devkitPPC
make            # 生成 WiiChannelMenu.dol
make DEBUG=1    # 生成 WiiChannelMenu-debug.dol：出现致命错误时把原因显示在屏幕上 20 秒，再返回系统菜单
```

调试版用于排查“启动后直接回到系统菜单”这类问题（例如 SD 卡无法挂载，这时 log.txt 也写不了）。

图片解码（`stb_image`，支持 PNG/JPG）和字体渲染（`stb_truetype`）已经放在 `include/` 里，不需要安装 portlibs。

## SD 卡目录结构

```
sd:/channelmenu/
    config.ini      配置（参考 sample/channelmenu/config.ini）
    font.otf        可选，用于显示中文名称（示例目录自带思源黑体 GB2312 子集）
    hbc.png ...     各 Channel 的配图
    log.txt         出错时自动生成
```

- 配图：PNG 或 JPG，不超过 2048×2048 像素。会保持原比例缩放到 4:3 的格子里，所以 4:3 的图片效果最好（例如 320×240）。
- 字体：文件不能超过 24MB（Wii 内存有限）。建议对思源黑体 / Noto Sans SC 做子集化，只保留用到的字，例如：
  `pyftsubset NotoSansSC-Regular.otf --text="自制程序频道Mii照片" --output-file=font.ttf`
- Title ID：4 字符 ID 会在 00010001（下载的频道）、00010002（系统频道）、00010008（隐藏频道）、00010004（光盘安装的频道）中依次查找已安装的；也可以直接写 16 位十六进制完整 ID。未安装的 Channel 会被跳过（并记录到 log.txt）。

## 安装到 Priiloader

1. 先用 HBC 测试：把 `WiiChannelMenu.dol` 复制到 SD 卡，用 `wiiload WiiChannelMenu.dol` 发送，或作为 HBC 应用（`sd:/apps/WiiChannelMenu/boot.dol`）运行。
2. 开机时按住 Reset 进入 Priiloader → **Load/Install File** → 选择 `WiiChannelMenu.dol` 安装。
3. Priiloader **Settings** → **Autoboot** → **Installed File**。

### 和 Priiloader 的配合

- 每一条返回系统菜单的路径（Home、配置出错、启动失败）都会先写入 Priiloader 的魔术字 `"Pune"`（`0x8132FFFB` / `0x817FEFF0`），让 Priiloader 这一次直接启动系统菜单，不再自动启动本程序，所以不会死循环。
- 从本启动器进入的 Channel 退出（回到 Wii 菜单）时，会再次经过 Priiloader 的自动启动，因此会回到本启动器。
- 如果配置出了问题导致一直进不了启动器：按 Home 或修好 SD 卡上的配置即可；开机按住 Reset 也随时可以进入 Priiloader。
