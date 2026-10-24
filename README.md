# 💖 全屏粒子爱心跳动 (Particle Heartbeat)

一个基于 C++ 与 EasyX 图形库开发的高帧率全屏粒子特效程序。包含震撼的“银河盘旋-树干上升”沉浸式入场动画、极致丝滑的 60 FPS 心跳呼吸效果，以及**深度防篡改**的专属防伪机制。

## ✨ 核心特性

- **全屏自适应**：自动获取显示器分辨率，全屏无边框沉浸式展示。
- **电影级入场动画**：
- 粒子在屏幕底部汇聚成缓慢旋转的“银河盘”。
- 随机延迟脱离，像树干一样沿着贝塞尔曲线垂直向上攀升。
- 最终逐渐聚集成一颗完整的心形。
- **极致渲染性能**：
- 抛弃传统 GDI 绘图，直接操作 DIB 像素缓冲区。
- 采用 QPC (`QueryPerformanceCounter`) 实现精准的 60 FPS 锁定。
- **加法混合 (Additive Blending)**：重叠的粒子会自动提亮，产生高光泛光效果。
- **抗锯齿软粒子**：3x3 模糊光晕算法，边缘更柔和。
- **高级物理细节**：约 1% 的粒子会带有长拖尾，沿着飞行轨迹划过夜空。
- **背景音乐**：内置 `miniaudio` 音频库，完美支持无缝循环播放 MP3 音乐。
- **深度防伪签名**：底层渲染管线深度耦合专属 LUT 校验表，任何试图篡改作者信息或删除验证的行为，都会导致程序无法编译或画面直接“爆炸”。

## 🎮 操作说明

- **按 `Esc`**：安全退出程序。
- 背景音乐只需要替换项目目录中的`bgm.mp3`

## 🚀 如何运行 (普通用户)

如果你拿到了编译好的压缩包，直接运行即可：

解压所有文件到一个**纯英文路径**下。

确保 `love.exe`、`bgm.mp3` 和 `libwinpthread-1.dll` 在**同一个文件夹**内。

双击 `love.exe` 即可全屏观赏。

## 🛠️ 如何编译 (开发者)

### 环境依赖
- **系统**: Windows 10 / 11
- **IDE**: CLion (推荐) 或 VSCode
- **编译器**: MinGW-w64 (GCC)
- **图形库**: EasyX for MinGW
- **音频库**: 已内置 `miniaudio.h`

### 编译步骤

将 EasyX 解压，将 `include` 和 `lib64` 文件夹放入项目根目录下的 `thirdparty/EasyX` 中。

确保 `bgm.mp3` 位于项目根目录下。

项目已配置好 CMake 依赖，并且自动处理了 MinGW 静态链接的兼容性问题。

使用 CMake 构建项目：
```bash
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

**自动打包**：`CMakeLists.txt` 已配置 `POST_BUILD` 脚本，编译完成后，会自动将 `bgm.mp3` 和缺失的 `libwinpthread-1.dll` 复制到输出目录 `bin/` 中。

## 📂 项目结构

```text
├── main.cpp # 核心逻辑代码（图形学算法、防伪、音频）
├── miniaudio.h # 跨平台音频库单头文件
├── bgm.mp3 # 背景音乐（需自行提供）
├── CMakeLists.txt # 构建脚本（自动复制依赖）
├── README.md # 说明文档
└── thirdparty/
└── EasyX/
├── include/ # EasyX 头文件
└── lib64/ # EasyX 静态库 (x64)
```