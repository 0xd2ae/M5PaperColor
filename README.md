# PaperColor 六色天气日历

ESP-IDF 6.1 原生 C 固件，面向 M5Stack PaperColor：400×600 竖屏、QWeather 天气图标、公历／农历、月历与中国大陆放假调休。官方组件仅用于协议参考，不作为依赖。

![六色界面预览](docs/previews/sunny.png)

预览使用模拟天气和理想色值；实际墨水屏色彩、方向及可读性需要实机确认。

界面先由 ImageGen 生成视觉参考，再在 Sketch 中按 400×600 重建为可编辑图层。参考图、最终 Sketch 文件及其导出图位于 `docs/design/`；固件坐标以 `papercolor-ui-v3.sketch` 为准。Sketch 中的天气图标直接导入随项目保存的 QWeather 官方 SVG，没有使用自绘占位图标。

## 构建与刷机

本机使用 EIM 安装的环境；常规安装可先运行对应 ESP-IDF 的 `export.sh`。

```sh
. ${HOME}/.espressif/tools/activate_idf_v6.1.sh
idf.py -B build-calendar build
idf.py -B build-calendar -p /dev/tty.usbmodem213101 flash monitor
```

替换串口为实际设备，使用 Ctrl+] 退出监视器。`build-calendar/` 与原有 `build/` 分开，避免清除原始构建。VS Code 的 clangd 已指向新编译数据库。

配置开启 16 MB Flash、8 MB Octal PSRAM，分区为 256 KB NVS 和单应用分区，无 OTA。升级已有其他固件时，分区布局会改变；刷机前保留原固件与配置备份。程序不会在 NVS 初始化失败时自动擦除数据。

六色色块和中文硬件测试：`idf.py -B build-calendar menuconfig` → **PaperColor calendar** → 启用 **Display six-color hardware test**，重新构建刷机；完成后关闭该选项。

## 首次配置

1. 启动后或长按顶部 **C** 三秒，设备进入配置模式。
2. 手机连接开放热点 **PaperColor-Setup**，无需密码。
3. 若系统弹出网络配置页可直接使用；否则在浏览器打开 **http://192.168.4.1**（必须使用 HTTP，不能使用 HTTPS）。连接 2.4 GHz 家庭 Wi-Fi，再填写和风天气控制台提供的 **API Host**（仅域名）和 **API Key**。
4. 搜索并选择中国大陆城市／区县，验证天气并保存。城市保存 Location ID 与经纬度；天气使用当前 v1 经纬度接口。
5. 保存成功后退出配置并更新屏幕；也可以按顶部 C 退出。十分钟无请求会自动退出。

保留相同家庭 Wi-Fi／API Host 时，密码／凭据留空沿用原值。更改家庭 Wi-Fi 名称后空密码表示开放网络。配置期间手机应继续连接 **PaperColor-Setup**；设备连接家庭 Wi-Fi 时热点可能变换信道，手机断开后需要重新连接。

**家庭 Wi-Fi 密码与 API Key 以明文保存在 TF 卡的 `papercolor/config.json`。** 任何拿到这张卡的人都能读出它们；不要把卡借给别人或混用，丢卡后请更换 Wi-Fi 密码并在和风天气控制台重置 Key。凭据不返回配置页、不写入日志。没有插卡时配置无法保存，设备只做离线日历显示。配置 HTTP 服务不做额外鉴权；开放热点和服务只在配置模式运行，十分钟无操作自动退出。设备天气请求使用证书校验的 HTTPS，禁用自动重定向。不应将本固件视为已完成量产安全加固。

## TF 卡

使用 FAT32 格式的 TF 卡，关机后插拔。卡与屏幕共用 SPI 总线，固件在屏幕传输期间独占总线；深度睡眠前卸载并断开卡电源。

```text
papercolor/
  config.json            Wi-Fi、和风天气凭据与城市（明文）
  config.example.json    离线配置模板，不含凭据
  holidays.json          导入的放假调休表
  anniversaries.json     纪念日列表（最多 32 个）
  images/<id>.e6         400×600 六色 4bpp 帧缓冲，120,000 字节，设备直接显示
  images/<id>.jpg        原图（≤1600 px），网页重新编辑用
  images/<id>.json       旋转／裁剪／调色参数
  fonts/font18|24|36.bin 扩展字库（可选）
  web/ort/               ONNX Runtime Web 与模型（可选）
```

**离线导入配置**：复制 `config.example.json` 为 `config.json`，填写 Wi-Fi、API Host、API Key 和城市名称，插卡开机即可，无需进入配置页。`location`／经纬度可留空，设备首次联网时用城市名查询并写回卡上。也可以用脚本准备整张卡：

```sh
.venv/bin/python tools/prepare_sdcard.py /Volumes/卡名 --config my-config.json
```

脚本创建目录、复制模板与扩展字库（缺失时先运行 `generate_assets.py --sd-out` 生成），并检查配置字段。旧版本保存在 NVS 的配置和年度表会在首次插卡启动时迁移到卡上，然后从 NVS 删除；NVS 之后只保存天气缓存。

## 纪念日

在配置页“纪念日”中添加。**倒数日**显示“还有 N 天”（每年重复或仅一次，过后显示“已过 N 天”）；**经历日**显示“已经 N 天”。每年重复的日期可选按农历（如农历生日），农历三十在小月按廿九，公历 2 月 29 日在平年按 2 月 28 日。

纪念日页为整屏：图片加顶部或底部文字条（名称、天数、日期／农历／周年），无图片时整屏为文字条颜色。倒数日当天、经历日首日、每满 100 天及每个周年当天，设备自动显示纪念日页；按 C 或长按 B 返回日历后当天不再自动切回。停留在纪念日页时整点唤醒只重绘天数，不联网更新天气。

### 图片编辑

浏览器端完成所有图像处理，设备只保存与显示结果：

- 旋转、铺满裁剪／完整显示、缩放、拖动取景、留白颜色。
- 预设（均衡、风景、人像、动漫、文档）与手动参数：曝光、亮度、对比度、饱和度、自然饱和度、色温、色调、伽马、清晰度、锐化、平滑度、色度保留。
- **自动调色**：先做类似相机 ISP 的测光（中位亮度、受高光余量限制）、灰世界白平衡（避开肤色、天空、植物）与场景识别，再在 150×225 小图上试 9 组曝光／饱和度，用面板光学模拟（3×3 墨点混合、亮部脏点、层次压平）打分，选出这块屏最能还原的一组。
- 抖色路径：**E6 Mix**（推荐，针对实测反射色优化）与 **Classic VED**。预览可切换实测色、理想色和原图。
- 结果是 400×600、每字节两像素的帧缓冲，网页通过 `e6.wasm`（`imaging/` 中的 C 代码，固件内嵌约 36 KB）生成。

保存时上传帧缓冲、原图和参数；名称有缺字时提示，屏幕上显示为 `?`。修改 `imaging/` 后重建 WASM：

```sh
brew install emscripten
tools/build_wasm.sh
```

### 可选 AI 自动（ONNX）

配置页是 `http://192.168.4.1`，不是安全上下文，浏览器不提供 WebGPU 和 SharedArrayBuffer，因此只能用 ONNX Runtime Web 的单线程 WASM 后端。运行时（约 14 MB）和模型放在卡上，首次加载较慢，之后由浏览器缓存：

```sh
.venv/bin/python tools/prepare_sdcard.py /Volumes/卡名 --ort
```

默认下载并校验两个预训练模型（也可通过 `--saliency-model` / `--enhance-model` 替换为自定义模型）：

- **前景主体分割模型 RMBG-1.4 INT8**（`saliency.onnx`，约 42.3 MB，解锁动态分辨率后默认按 1×3×512×512 运行）：高精度识别人像、宠物与各类物体主体位置与范围，自动避开顶部或底部纪念日文字条进行取景与适度缩放，并为主体区域提供测光权重。
- **专业调色与局部光影融合模型 3D-LUT + SCI**（`enhance.onnx`，约 2.3 MB，输入输出 1×3×256×256）：融合 CVPR 2022 **SCI**（自校准局部光影恢复）与 IEEE TPAMI 2022 **Image-Adaptive 3D-LUT**（基于 MIT-Adobe FiveK Expert C 专业修图训练的自适应 33×33×33 三维色彩查找表与三线性插值），再以联合双边上采样（Joint Bilateral Upsampling）作用到全分辨率原图，在保留 100% 边缘锐度的同时完成逆光暗部恢复、高光柔和滚降与色彩分级。

模型完成主体取景与局部暗部恢复后，最终曝光与饱和度仍由上面的墨水屏光学模拟打分选出。模型文件随脚本下载到 TF 卡，不随固件分发。`tests/onnx_fixture.py` 可生成极小算子图供自动化测试使用。

## 按键与更新

| 位置 | 按钮 | 短按 | 长按三秒 |
| --- | --- | --- | --- |
| 顶部 | C / GPIO1 | 回到今天并更新（纪念日页时返回日历） | 进入配置 |
| 侧边上方 | A / GPIO10 | 上月；纪念日页时上一个纪念日 | 无操作 |
| 侧边下方 | B / GPIO9 | 下月；纪念日页时下一个纪念日 | 切换日历／纪念日页 |

30 ms 消抖，800 ms 内翻月合并。刷新期间继续采集按键，C 清除已有翻月请求，配置请求优先。浏览时天气和大日期仍代表今天；五分钟后回到本月。电源按钮保留硬件原有行为。

整点、午夜、上电或手动触发更新；联网窗口最多 60 秒。屏幕刷新约 15–30 秒，成功且画面变化时更新，完成后断开 Wi-Fi、关闭屏幕／音频／SD／RGB／Grove 相应电源并深度睡眠。RTC 定时 IRQ 与三键唤醒，ESP 定时器作为备用唤醒源。续航需测量后评估。

网络失败保留天气缓存并标记离线；超过三小时标记过期，旧预报显示实际日期，避免将昨天标成今天。切换城市不会显示旧城市天气。RTC 无有效时间时显示等待校时，不编造日期。电量百分比是基于电压的粗略估计。

## 室内温湿度与状态灯

板载 SHT40 在每次唤醒、联网和点亮 RGB 之前测量，减少自热影响；保持唤醒超过十分钟时重新测量。天气区右栏以绿色显示“室内 23℃ 58%”，取整显示以避免细小变化触发整屏刷新；读取或 CRC 失败时显示“室内 --”。PaperColor 机壳内测量值可能略高于房间实际温度。

两颗 RGB 灯（G21，M5PM1 LDO 供电）亮度较低，深度睡眠前关闭 LDO。整点等无人值守唤醒只显示电量警告，夜间保持黑暗；上电和按键唤醒显示完整状态。

| 状态 | 颜色 |
| --- | --- |
| 上电开机 | 绿色约 1.5 秒 |
| 联网更新天气 | 蓝色常亮 |
| 屏幕刷新中 | 黄色常亮 |
| 配置模式 | 紫色闪烁 |
| 进入休眠（关机） | 白色渐暗熄灭 |
| 电量 ≤20% | 第二颗灯每 2 秒红色短闪 |
| 电量 ≤5% | 两颗灯红色快闪，覆盖其他状态 |

## 扬声器

板载 ES8311 解码器与 AW8737A 功放平时断电，只有要出声时才给 G45／G46 上电。声音由固件里的八音盒合成器生成，不依赖音频文件。整点报时、开机提示、按键提示和纪念日音乐**默认全部关闭**；在配置页“声音与报时”里打开，并可用试听按钮从扬声器播放。

| 设置 | 默认 | 行为 |
| --- | --- | --- |
| 按键提示音 | 关 | 短按 A／B／C 与长按 B／C 满三秒时立刻给一声短音，不必等屏幕刷完 |
| 开机提示音 | 关 | 仅上电开机时播放；整点唤醒和进入休眠不发声 |
| 整点报时 | 关 | 打开后在设定整点范围内报时（默认 08:00–21:00，可跨午夜）。电量 ≤10% 时跳过 |
| 纪念日音乐 | 关 | 打开后，纪念日当天白天的第一次自动唤醒，以及手动切到该纪念日时播放。每个纪念日可另选曲子或单独静音 |

内置曲子：清脆双音报时、威斯敏斯特大本钟、森林布谷鸟钟、祝你生日快乐、茉莉花、新年好、铃儿响叮当、友谊地久天长、D 大调卡农、婚礼进行曲、欢乐颂、致爱丽丝、摇篮曲、天空之城。没有选择曲子时保持安静。

## 农历和节假日

农历资源覆盖 **2000–2099** 年，支持闰月与除夕；传统节日与放假安排独立。二十四节气按北京时间预先计算（`main/generated/terms.bin`），月历中以绿色显示，与传统节日同日时优先显示节日，今日信息行两者都显示。月历周一开头、最多六行，红色“休”表示放假区间、黄色“班”表示补班；普通周末不标“休”。

内置 [holidayAPI.json](https://raw.githubusercontent.com/lanceliao/china-holiday-calender/master/holidayAPI.json) 的 2023–2026 年快照。未来通过配置页上传同格式 JSON（最多 60 KB），无需重刷固件。结构示例：

```json
{
  "Timezone": "Asia/Shanghai",
  "Years": {
    "2026": [{
      "Name": "元旦", "StartDate": "2026-01-01", "EndDate": "2026-01-03",
      "Duration": 3, "CompDays": ["2026-01-04"]
    }]
  }
}
```

导入检查日期、年度、区间长度、重复项及休／班冲突；失败保留旧表。导入以完整表替换，不与旧表合并。文件缺少当年数据时提示“本年度调休数据未导入”；跨年放假区间仍可以匹配实际日期。第三方年度表应与国务院通知核对；设备不自动下载该地址。

## 天气图标

采用 [QWeather Icons](https://github.com/qwd/Icons) 1.8.0，固定提交与源文件校验值见 `main/generated/icons-manifest.json`。官方轮廓／填充字形离线转换为 112 px 主图标以及 20／30 px 预报图标，按天气代码精确匹配，未知代码使用 `999`；不再使用自绘天气图形。保留官方比例，填充适配六色、轮廓保持高对比度。

图标 © QWeather，采用 [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/)，本项目进行了位图转换及六色重着色，原始字体、映射、署名和授权文本随项目保留。

## 字体与资源生成

中文采用 [霞鹜文楷 Lite](https://github.com/lxgw/LxgwWenKai-Lite) Regular 与 Medium。原始 TTF 保存在 `assets/fonts/source.ttf` 和 `assets/fonts/source-medium.ttf`，SHA-256 和字库大小见 `main/generated/manifest.json`。固件仅嵌入派生的 **PaperCalendarBitmap** 压缩单色位图字库，不在设备上解析 TTF。保留 OFL 授权和 Reserved Font Name 要求。

```sh
python3 -m venv .venv
.venv/bin/pip install -r tools/requirements.txt
.venv/bin/python tools/generate_assets.py
.venv/bin/python tools/generate_icons.py
.venv/bin/python tools/run_host_checks.py
```

TF 卡扩展字库为 18／24／36 px Regular，覆盖源字体内全部 CJK 统一汉字（含扩展 A）、日文假名、符号与全角字符，共约 2 万字、9.3 MB：

```sh
.venv/bin/python tools/generate_assets.py --sd-out sdcard/papercolor/fonts
```

查字顺序：内置字库 → 卡上同字号字库 → 36 px 退回 24 px → `?`。内置字形与卡上字形同源同阈值，常用字两者显示一致；卡上字库按需二分查找并缓存 512 个字形。

内置字号为 14／18／24 px，48／64／88 px 仅含数字与符号。18 px 包含源字体内 GB2312 汉字，14／24 px 按固定界面和天气词汇裁剪。18／24 px 另有 Medium 字重位图（`font*m.bin`），用于城市名、天气描述和室内温湿度；其余关键标题仍使用 Regular 位图的像素加粗绘制。日历节日名只做横向 1 px 加粗：18 px 下“重”等字没有空间容纳更粗的横画。缺字显示 `?`。资源生成依赖固定版本 Pillow、fonttools、lunar-python；已生成资源随项目保存，常规固件构建不需要这些 Python 包。

## 测试与结构

`main/board.c`：原生屏幕／电源／RTC／SHT40；`led.c`：RGB 状态灯；`audio.c`：八音盒合成与 ES8311／扬声器；`buttons.c`：消抖与请求合并；`calendar.c`：公历、农历和年度表；`data.c`／`network.c`：天气解析及联网；`compression.c`：有界 Gzip 解压和校验；`sdcard.c`：TF 卡供电与挂载；`storage.c`：卡上文件（原子写入）与 NVS 天气缓存；`anniversary.c`：纪念日模型；`fonts.c`：卡上字库；`setup.c`／`web/`：配置服务与图片编辑器；`render.c`：六色绘制；`M5PaperColor.c`：生命周期与睡眠。`imaging/`：浏览器图像管线（编译为 `main/web/e6.wasm`）。

主机测试直接编译实际可移植 C 模块，使用 AddressSanitizer／UndefinedBehaviorSanitizer，检查日期转换、闰月、除夕、跨年假期、补班、坏年度表、天气解析、按键合并、损坏 Gzip、纪念日（倒数／经历、农历、2 月 29 日、整百天）、卡上配置 JSON、城市解析、卡上字库与缺字、图像管线（纯色、确定性、白边干净、打分）和 WASM 冒烟测试，并输出 `docs/previews/` 图片（含 `anniv-*.png`）。当前脚本面向 macOS（Mach-O 资源嵌入）；Gzip 主机测试用 zlib，设备使用 ESP32-S3 ROM miniz。

```sh
python3 tools/preview_server.py            # 加 --no-card 模拟未插卡
# 浏览 http://127.0.0.1:8765，使用模拟接口（含纪念日与图片上传，存于临时目录），不控制设备。
# /sdweb/ 由 sdcard/papercolor/web/ 提供，可用于测试 ONNX。
```

本地构建、逻辑测试和预览不能证明实机刷新／唤醒、真实和风天气账户权限或电池续航。本地验证记录见 [VALIDATION.md](docs/VALIDATION.md)。实机检查步骤见 [HARDWARE_CHECKLIST.md](docs/HARDWARE_CHECKLIST.md)，来源与授权见 [REFERENCES.md](docs/REFERENCES.md)。
