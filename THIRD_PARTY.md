# 第三方组件

本项目自身代码按 GPL-3.0-or-later 提供，许可证正文见 `LICENSE`。各依赖保留原作者版权声明和各自许可证；工程不包含被分析的“无线扩展屏”应用代码或资源。

| 组件 | 固定版本／提交 | 用途 | 原始来源 |
|---|---|---|---|
| UxPlay | `95a0e49561a52e4b52ef59c8e17cdd701509d35a` | AirPlay / RAOP、配对、媒体传输和内置 mDNS | https://github.com/FDH2/UxPlay |
| OpenSSL | `openssl-3.5.9` / `45e844fa2a14ec92d146bd8f5778ac130b6625fb` | 协议所需密码运算 | https://github.com/openssl/openssl |
| libplist | `2.8.0` / `fe3dc34dc3484006e12b403f7ceb06f0ad40b6f4` | Apple plist 编解码 | https://github.com/libimobiledevice/libplist |

许可证位置：

- UxPlay：`third_party/UxPlay/LICENSE`，协议核心中的各文件另含 LGPL 等原始声明。
- OpenSSL：`third_party/openssl/LICENSE.txt`（Apache-2.0）。
- libplist：`third_party/libplist/COPYING`（LGPL-2.1-or-later），内部 libcnary 另见其 `COPYING`。

本项目重新实现了鸿蒙界面、NAPI 生命周期及原生 AVCodec / OHAudio 渲染适配，不构建 UxPlay 的 GStreamer 桌面渲染器。默认广告 H.264 屏幕镜像能力；1.0.4 增加由用户开启、按硬件能力发布的 H.265 支持，1.3.0 已在 MatePad 实际收到并硬解 HEVC，其他设备仍由协商决定。不启用独立 HLS 视频播放。

1.0.4 在 `lib/mirror_hevc_config.h` 和 `lib/raop_rtp_mirror.c` 增加 HEVC 参数集的长度、NAL 类型、容量校验，并修正 NAL 类型位运算优先级。

分发安装包时同时提供本工程的对应源代码、构建脚本与许可证。当前第三方代码为固定提交的本地源码，构建无需动态下载其他二进制库。

鸿蒙适配修改：`lib/mdnsd/dnssd_mdnsd.c` 使用接收端唯一标识构造主机名，避免系统默认 `localhost.local` 冲突。原版权和许可证声明保留。

连接生命周期适配：`lib/httpd.c` 增加非阻塞唤醒管道，使 Unix 平台停止监听时无需等待 select 超时；显式初始化／释放互斥锁和唤醒描述符，并确保已退出的工作线程被回收。对应回归测试包含空闲停止和重复创建后的描述符检查。

1.3.0 生命周期适配：`raop_rtp_mirror.c` 正常 EOF 关闭流套接字，中断时释放未完成视频包，限制分配大小；`raop_rtp_mirror.c`、`raop_rtp.c`、`raop_ntp.c` 以 joined 而非 running 判断是否需要回收工作线程。视频线程不再自行回收自身。原版权和许可证声明保留。
