# 栖屏 QiScreen

栖屏把鸿蒙手机或平板用作 Mac 的无线扩展屏。这个仓库提供华为应用市场版本 **1.3.6（1003006）** 的对应源码，包含鸿蒙应用、构建脚本和固定版本的第三方依赖。

1.3.6 修复了调整分辨率、帧率、播放策略时选中状态未及时更新的问题。

## 使用

1. 让 Mac 和鸿蒙设备连接到同一个可互访的局域网。
2. 在栖屏点击「开始接收」。
3. 从 Mac 的「控制中心 → 屏幕镜像」选择栖屏，再在显示器设置里选择扩展显示或镜像。

双击投屏画面可显示控制栏。双指缩放，放大后可以拖动画面，点「适应」恢复完整桌面。画质和帧率由 Mac、接收设备及网络条件协商。

## 构建

需要 DevEco Studio 和 HarmonyOS 6.1.1（API 24）SDK。先把 `build-profile.example.json5` 复制为 `build-profile.json5`。如 DevEco Studio 安装在其他位置，在构建前将 `DEVECO_HOME` 指向其 `Contents` 目录。

```bash
cp build-profile.example.json5 build-profile.json5
bash scripts/build_release.sh --unsigned
```

调试安装时，在 DevEco Studio 中配置自己的调试签名，再运行 `bash scripts/build.sh`。发布构建使用开发者自己的发布证书及 Profile。首次构建会编译工程随附的原生依赖。

## 公开源码包范围

OpenSSL 上游自测和演示所用的示例私钥文件已排除。栖屏构建只编译 OpenSSL 库，关闭 OpenSSL 的命令行应用和测试。若需要运行 OpenSSL 自身的测试，请从 [OpenSSL 上游](https://github.com/openssl/openssl) 获取对应版本的测试材料。

## 许可证

项目代码按 GPL-3.0-or-later 提供。参见 [GPLv3 正文](LICENSE)和[第三方组件说明](THIRD_PARTY.md)。
