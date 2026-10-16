# 来源与许可

本项目不链接 M5Stack Arduino 组件。以下固定提交仅用于核对寄存器、初始化序列和色码；移植的协议序列保留上游授权。

| 来源 | 固定版本 / 提交 | 用途 |
| --- | --- | --- |
| [PaperColor 文档](https://docs.m5stack.com/en/core/PaperColor) | 本地 `PaperColor.md` / PDF | 引脚、尺寸、刷新与供电 |
| [M5GFX](https://github.com/m5stack/M5GFX) | `347a23274081a105cb9fc0fc3bbf0e15c78e79c3` | ED2208 初始化与色码；MIT，Panel 文件另标 LovyanGFX BSD-2-Clause |
| [M5Unified](https://github.com/m5stack/M5Unified) | `e125e73f28b6b83e621e48df432af426bb2ff935` | RX8130 时间、IRQ 和计时寄存器；PaperColor RGB 引脚、数量、GRB 顺序与时序；MIT |
| [M5PM1](https://github.com/m5stack/M5PM1) | `be9a5456c007c333e7ac963f33bfde1ffa5d82ee` | 电源配置与电压寄存器；MIT |
| [官方工厂示例](https://github.com/m5stack/M5PaperColor-UserDemo) | `1ff998e0cf3b9ce916f2e0319561d043db21cc4a` | 电源、RTC 及 SHT40（0x44，0xFD 高精度测量）使用参考；不引入其依赖 |
| [QWeather Icons](https://github.com/qwd/Icons) | `1.8.0` / `d7d4ef4d8334f9e70a75a088cb77804bf81daea4` | 官方天气字形；图标 CC BY 4.0、代码 MIT；转换为压缩位图并六色重着色 |
| [cJSON](https://github.com/DaveGamble/cJSON) | `v1.7.19` | 本地固定版本 JSON 解析器；MIT |
| [霞鹜文楷 Lite](https://github.com/lxgw/LxgwWenKai-Lite) | 原始文件 SHA-256 见 `main/generated/manifest.json` | 中文字形；SIL OFL 1.1，派生位图命名 PaperCalendarBitmap |
| [lunar-python](https://github.com/6tail/lunar-python) | `1.4.8` | 离线生成逐日农历资源；MIT |
| [china-holiday-calender](https://github.com/lanceliao/china-holiday-calender) | JSON `Generated: 20251104T235904Z` | 2023–2026 放假／补班快照 |
| [PaperColor-Frame pipeline.md](https://github.com/MarsTechHAN/PaperColor-Frame/blob/main/doc/pipeline.md) | 文档 | 六色图像管线的算法思路：Lab／CIEDE2000、亮度压缩与色度保留、超采样缩放、区域分类与可见性偏置、边缘感知 Stucki 扩散、局部精修、实测调色板 |
| [ONNX Runtime Web](https://www.npmjs.com/package/onnxruntime-web) | `1.30.0`（npm 完整性校验） | 可选，由 `prepare_sdcard.py` 下载到 TF 卡，不随固件分发；MIT |
| [Image-Adaptive 3D-LUT](https://github.com/HuiZeng/Image-Adaptive-3DLUT) / [SCI](https://github.com/vis-opt-group/SCI) / [RMBG-1.4](https://www.npmjs.com/package/@rmbg/model-briaai) | SHA-256 见 `prepare_sdcard.py` | 可选，由 `prepare_sdcard.py --ort` 下载／转换到 TF 卡（FiveK 3D-LUT 调色 + SCI 局部光影恢复 + RMBG-1.4 主体取景），不随固件分发；3D-LUT Apache-2.0，RMBG-1.4 限非商业使用 |
| [ES8311](https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/products/atom/Atomic%20Echo%20Base/ES8311.pdf) / [Espressif es8311](https://github.com/espressif/esp-adf/blob/master/components/esp_codec_dev/device/es8311/es8311.c) | 寄存器说明；16 kHz、MCLK 256×Fs 的初始化取值 | 扬声器从机播放；Apache-2.0，仅采用公开寄存器取值，不链接该驱动 |
| [Emscripten](https://emscripten.org/) | `6.0.12` | 把 `imaging/` 编译为独立 WASM；构建工具，不进入产物 |

`imaging/` 是按 pipeline.md 的文字描述独立实现的代码（clean-room）。PaperColor-Frame 采用 GPL-3.0，本项目没有复制或改写其源码；参数（区域阈值、偏置表、扩散权重）为本项目自行设定，默认实测调色板取自该文档公布的测量值。

[和风天气 v1 当前天气](https://dev.qweather.com/en/docs/api/weather/weather-current/)、[每日预报](https://dev.qweather.com/en/docs/api/weather/weather-daily-forecast/)、[城市搜索](https://dev.qweather.com/en/docs/api/geoapi/city-lookup/)、[认证](https://dev.qweather.com/en/docs/configuration/authentication/)与[Gzip](https://dev.qweather.com/en/docs/configuration/api-config/)用于接口约定。界面保留和风天气来源标注，实际服务使用需符合账户许可。

授权文本随项目存放在 `licenses/`；原始字体不修改，派生位图继承字体授权。农历资源的生成工具与上游算法固定版本，关键日期使用独立已知日期及国务院放假通知检查。
