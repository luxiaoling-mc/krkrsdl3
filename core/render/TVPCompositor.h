#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "PlatformView.h"

//---------------------------------------------------------------------------
// TVPCompositor
//---------------------------------------------------------------------------
// 通用合成器，用来管理所有非Layer/Bitmap系列的渲染，包括上屏Windows/Surface、emote-GPU加速
//---------------------------------------------------------------------------
namespace krkrsdl3
{
//---------------------------------------------------------------------------
// 渲染后端抽象层（合并接口）
//
// 目标：让"窗口贴图合成"这一层不再依赖具体图形 API。
// 当前实现的后端：
//   - "software" : SDL 软渲染（各平台入口通过 PlatformView.h 的平台函数提供）
//   - "opengl"   : OpenGL 3.3 Core / OpenGL ES 3.0（桌面 glad / 移动 GLES3）
//   - "vulkan"   : Vulkan 1.0（SDL3 窗口表面，位于 environ/sdl3/render/）
//   - "metal"    : 预留（未来 macOS，接口已按 Metal 适配设计，见 docs）
//
// 每个后端只有一个实现类，同时承担两个角色（合并设计）：
//   - 窗口贴图合成（上屏）：BeginFrame/EndFrame + *WindowTexture 方法
//   - 2D 网格渲染（供 emoteplayer 等插件）：离屏目标 + 一般贴图 + DrawMesh
//
// 贴图区分（接口上显式区分两类贴图）：
//   - 窗口贴图（CreateWindowTexture 等）：上屏合成用。
//     SW=SDL_Texture（经平台钩子）；GPU=与一般贴图同一实现。
//   - 一般贴图（CreateTexture 等）：离屏网格绘制用。
//     SW=CPU buffer；GPU=与窗口贴图同一实现。
//   对 GPU 后端而言两类贴图就是同一个函数；对软件后端实现不同，
//   因此接口上仍保留两套方法名。
//
// 设计要点（为 Metal 预留）：
//   1. 贴图句柄为不透明 void* —— GL 存内部贴图对象，Vulkan 存 VulkanTexture*，
//      Metal 可存 id<MTLTexture> 的堆包装对象，无需改接口。
//   2. 贴图更新以"整张 CPU 像素 + pitch"为单位 —— Metal 的 staging buffer
//      上传路径可直接映射到该接口。
//   3. 帧控制只有 BeginFrame/EndFrame —— Metal 的 CAMetalLayer drawable
//      获取与 present 封装在 EndFrame 内，对上层不可见。
//   4. 绘制以"目标矩形像素坐标"为单位，letterbox 计算由合成器统一完成，
//      所有后端行为一致。
//---------------------------------------------------------------------------
class iTVPRenderBackend
{
public:
    virtual ~iTVPRenderBackend() = default;

    // 后端标识："software" / "opengl" / "vulkan" / "metal"
    virtual const char* GetName() const = 0;
    // 是否硬件加速（用于日志与信息展示）
    virtual bool IsHardware() const { return false; }

    // ---- 帧控制（由 TVPRenderOnce 统一调度）----
    virtual void BeginFrame(int winWidth, int winHeight) = 0;
    virtual void EndFrame() = 0; // 提交并呈现（SwapBuffers / Present / vkQueuePresent）

    // ---- 窗口贴图管理（上屏合成用；SW=SDL_Texture，GPU=与一般贴图同一实现）----
    // 句柄为后端持有的不透明指针，nullptr 表示无效
    virtual void* CreateWindowTexture(int width, int height) = 0;
    virtual void UpdateWindowTexture(void* handle, const uint8_t* buff, int width, int height, int pitch) = 0;
    virtual void DestroyWindowTexture(void* handle) = 0;
    // 以像素坐标绘制窗口贴图（位置与尺寸已由合成器计算，含 letterbox）
    virtual void DrawWindowTexture(void* handle, float posX, float posY, float width, float height) = 0;

    // ---- 2D 网格渲染（离屏目标 + 一般贴图 + 蒙版/混合 + DrawMesh）----
    // 渲染目标（离屏）：GL= FBO，软渲染= CPU 缓冲，Vulkan= 离屏图像
    virtual void* CreateTarget(int width, int height) = 0;
    virtual void DestroyTarget(void* target) = 0;
    // 绑定当前绘制目标（内部完成视口/深度/混合等状态设置）
    virtual void SetTarget(void* target) = 0;
    // 清屏：clearColor=true 清颜色+深度；false 仅清深度（软件后端无深度，此时不清屏）
    virtual void ClearTarget(bool clearColor) = 0;
    // 回读 CPU 像素（调用期间像素有效，Unlock 后失效）
    virtual uint8_t* LockTarget(void* target, int& pitch) = 0;
    virtual void UnlockTarget(void* target) = 0;
    // 目标作为可采样纹理使用（GPU 纹理可直接作为绘制目标；返回的句柄
    // 可传给 DrawMesh 的 texture 参数与 SetMask）。GL 返回 FBO 颜色纹理，
    // Vulkan 返回目标自身（其图像视图），软渲染返回目标自身（其缓冲）。
    virtual void* GetTargetTexture(void* target) = 0;
    // 直接更新目标内容（整幅上传；用于 GPU 纹理的像素上传）。
    // GL 走 glTexSubImage2D，Vulkan 走 staging buffer + copy，
    // 软渲染直接写缓冲。调用后目标内容立即可作为采样/回读源。
    virtual void UpdateTargetTexture(void* target,
                                     const uint8_t* pixels,
                                     int width,
                                     int height,
                                     int pitch) = 0;

    // 一般贴图（SW=CPU buffer；GPU=与窗口贴图同一实现）
    virtual void* CreateTexture(int width, int height) = 0;
    virtual void UpdateTexture(void* texture, const uint8_t* pixels, int width, int height, int pitch) = 0;
    virtual void DestroyTexture(void* texture) = 0;
    // 读取一般贴图像素（SW=CPU 缓冲零拷贝；GPU 后端返回 nullptr，GPU 走别名路径）
    virtual uint8_t* LockTexture(void* texture, int& pitch) { return nullptr; }

    // 蒙版：绑定蒙版目标（alpha >= 128 为有效像素）；nullptr 关闭蒙版
    virtual void SetMask(void* maskTarget) = 0;
    // 混合模式：0 普通 alpha / 1,4 乘色 / 3 加色 / 6 跳过 / 21 纯色替换；
    // uniformColor 仅 mode 21 使用，可为 nullptr
    virtual void SetBlendMode(int mode, const float* uniformColor = nullptr) = 0;
    // vertices: 交错 (x, y, u, v)，每顶点 4 个 float（NDC + UV）
    virtual void DrawMesh(const float* vertices,
                          int vertexCount,
                          const uint16_t* indices,
                          int indexCount,
                          void* texture,
                          float opacity) = 0;

    // ---- Layer 合成（图层合成路径，供 DrawDeviceD3D 的 GPU RenderManager 使用）----
    // 与 2D 网格（DrawMesh，emoteplayer 用）的区别：混合公式遵循软件 RenderManager
    // （gl/tvpgl.cpp 的 bm* 方法语义），而非 emoteplayer 的混合约定。GL/VK/SW
    // 三个后端实现与软件合成保持一致（允许 ±1 舍入误差）。
    //
    // opacity 为 0..1 的浮点（对应软件方法 0..255 的 opacity 参数，后端内部换算）。
    enum LayerBlendMethod
    {
        LBM_COPY = 0,       // dest = src（直写，含 alpha）
        LBM_ALPHA = 1,      // dest = dest + (src - dest) * ((src.a * opa8) >> 8) >> 8（全通道）
        LBM_CONSTALPHA = 2, // dest = dest + (src - dest) * opa8 >> 8（源视为不透明）
        LBM_ADD = 3,        // dest = sat(dest + s)；s.RGB = src.RGB * opa8 >> 8，s.A = 0
        LBM_SUB = 4,        // dest = sat(dest - s)；s.RGB = 255-(255-src.RGB)*opa8>>8，s.A = src.A
        LBM_MUL = 5,        // dest.RGB = dest.RGB * s.RGB >> 8（s 同 SUB），dest.A = 0
        LBM_MUL_HDA = 6,    // 同 LBM_MUL，dest.A 保留
        LBM_FILL = 7,       // dest = uniformColor（直写，含 alpha；opacity 忽略）
        LBM_COPYCOLOR = 8,  // dest = (dest & 0xff000000) | (src & 0x00ffffff)
        LBM_COPYOPAQUE = 9, // dest = src | 0xff000000
        LBM_COPYMASK = 10,  // dest = (dest & 0x00ffffff) | (src & 0xff000000)
    };
    // 设置图层合成混合状态（作用于当前目标；uniformColor 仅 LBM_FILL 使用）
    virtual void LayerSetBlend(int method, float opacity, const float* uniformColor = nullptr) = 0;
    // 在当前目标上绘制源贴图矩形（目标像素坐标；uv0/uv1 为归一化源矩形）。
    // 坐标约定与 DrawDeviceD3D 的合成路径一致：内容 y 向下，内容顶(t=0)
    // 映射到 NDC -1（GL/VK/SW 三后端回读结果一致）。
    virtual void LayerDrawRect(void* texture,
                               float x,
                               float y,
                               float w,
                               float h,
                               float u0 = 0.0f,
                               float v0 = 0.0f,
                               float u1 = 1.0f,
                               float v1 = 1.0f) = 0;

    // 后端信息采集（GL 的厂商/版本/扩展日志等），无操作默认实现
    virtual void FetchInfo() {}
};

//---------------------------------------------------------------------------
// 后端注册表：静态注册 + 平台探测，供 -render 参数选择与日志使用
//---------------------------------------------------------------------------
struct TVPRenderBackendDesc
{
    const char* name;        // 命令行使用的名字（"software"/"opengl"/"vulkan"）
    const char* description; // 人可读描述
    bool (*probe)();         // 平台可用性探测，nullptr 表示编译进来即可用
    iTVPRenderBackend* (*create)(); // 工厂；部分后端需要平台参数（如 SDL 窗口），
                                    // 由入口直接构造，此时可为 nullptr
};
void TVPRegisterRenderBackend(const TVPRenderBackendDesc& desc);
// 探测通过的可用后端名列表
std::vector<std::string> TVPListRenderBackends();
bool TVPRenderBackendAvailable(const std::string& name);

//---------------------------------------------------------------------------
// 全局当前后端（同时承担窗口合成与 2D 网格渲染，插件经此获取渲染器）
//---------------------------------------------------------------------------
void TVPSetRenderBackend(iTVPRenderBackend* backend);
iTVPRenderBackend* TVPGetRenderBackend();
// 销毁当前后端并清空指针
void TVPShutdownRenderBackend();

//---------------------------------------------------------------------------
// Windows Compositor
//---------------------------------------------------------------------------
void fetchGLInfo();
TVPSprite* KRKR_Get_Current_Sprite();
// 贴图管理
void TVPJoinTexture(TVPSprite* sp);
void TVPDepartTexture(TVPSprite* sp);
// 渲染函数
void TVPRenderOnce(int winWidth, int winHeight);
void TVPCreateTexture(TVPSprite& sp);
void TVPUpdateTexture(TVPSprite* sp, uint8_t* buff, int width, int height, int pitch);
void TVPDestroyTexture(TVPSprite* sp);
// 清理合成器中所有窗口贴图
void TVPClearAllTexture();
} // namespace krkrsdl3
