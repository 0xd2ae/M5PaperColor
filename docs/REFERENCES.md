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

[和风天气 v1 当前天气](https://dev.qweather.com/en/docs/api/weather/weather-current/)、[每日预报](https://dev.qweather.com/en/docs/api/weather/weather-daily-forecast/)、[城市搜索](https://dev.qweather.com/en/docs/api/geoapi/city-lookup/)、[认证](https://dev.qweather.com/en/docs/configuration/authentication/)与[Gzip](https://dev.qweather.com/en/docs/configuration/api-config/)用于接口约定。界面保留和风天气来源标注，实际服务使用需符合账户许可。

授权文本随项目存放在 `licenses/`；原始字体不修改，派生位图继承字体授权。农历资源的生成工具与上游算法固定版本，关键日期使用独立已知日期及国务院放假通知检查。
