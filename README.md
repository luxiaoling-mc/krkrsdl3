# 介绍

krkrsdl3的核心源码仓库，源码构建参考[krkrsdl3_build](https://github.com/krkrsdl3/krkrsdl3_build)。

# 目录结构说明

```
📁 core/ # 核心代码
├── 📁 archive/# 数据包格式相关代码
├── 📁 main/   # 引擎运行内核代码 窗体/事件循环/线程等
├── 📁 media/  # 媒体文件格式相关代码
    ├── 📁 font/      # 字体系统
    ├── 📁 image/     # 图片解码
    ├── 📁 movie/     # 视频解码
    ├── 📁 sound/     # 音频解码
├── 📁 msg/    # 调试信息/提示信息
├── 📁 render/ # 渲染相关代码
├── 📁 script/ # tjs2 native绑定代码
├── 📁 utils/  # 工具包
📁 environ/ # 不同系统/芯片架构之间的差异化代码
📁 plugins/ # 扩展插件代码
📁 tjs2/    # tjs2语言内核代码
```

# 依赖库说明

- oniguruma:用于tjs2语言内核的正则表达式匹配
- zlib:基础压缩算法
- ffmpeg:音视频解码
- libpng/libwebp/libjpeg-turbo:图片解码
- libvorbis/opusfile:音频解码
- freetype:字体渲染
- plutosvg:2D矢量图绘制
- sdl3/glad:跨平台核心

使用说明：对于api稳定的库默认采用最新版本，对于api有较大改动的库采用能兼容的最高版本。

# 宏定义说明

- ONLYCONSOLE:只编译脚本语言tjs2。
- _KRKRSDL3_USE_SDL3:使用SDL3作为硬件抽象层后端。
- _KRKRSDL3_USE_FFMPEG:使用ffmpeg进行视频解码。
- _KRKRSDL3_USE_OPENGL/_KRKRSDL3_USE_VULKAN...:区分不同的图形后端是否参与编译
- _KRKRSDL3_ANDROID/_KRKRSDL3_LINUX...:区分不同操作系统。
- _KRKRSDL3_EGL/_KRKRSDL3_GL:区分opengles与opengl。