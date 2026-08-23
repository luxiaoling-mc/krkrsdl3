#pragma once

#include <cstddef>
#include <vector>

#include "TVPCompositor.h"

//---------------------------------------------------------------------------
// OpenGL / OpenGL ES 渲染后端（合并实现）
//
// 一个类同时承担两个角色（同一接口内，见 RenderBackend.h）：
//   - 窗口贴图合成（上屏）。窗口贴图与一般贴图是
//     同一套 GL 纹理实现（CreateWindowTexture == CreateTexture 等）。
//   - 2D 网格渲染：一般贴图 + 离屏网格绘制（FBO 目标/蒙版/混合），
//     由 emoteplayer 的 GL 渲染路径迁移而来（shader/FBO/蒙版语义保持一致）。
//
// 桌面（_KRKRSDL3_GL）  : OpenGL 3.3 Core，glad 加载
// 移动 / Web / OHOS     : OpenGL ES 3.0，GLES3 头文件
// 要求：所有调用发生前，平台入口必须已经创建并 MakeCurrent 了 GL 上下文。
// 呈现（EndFrame）通过平台钩子 TVPSwapBuffersBackend() 完成。
//---------------------------------------------------------------------------
namespace krkrsdl3
{
class GLRenderBackend : public iTVPRenderBackend
{
public:
    GLRenderBackend() = default;
    ~GLRenderBackend() override;

    const char* GetName() const override { return "opengl"; }
    bool IsHardware() const override { return true; }

    // ---- 窗口贴图合成（iTVPRenderBackend）----
    void BeginFrame(int winWidth, int winHeight) override;
    void EndFrame() override;
    void* CreateWindowTexture(int width, int height) override;
    void UpdateWindowTexture(void* handle, const uint8_t* buff, int width, int height, int pitch) override;
    void DestroyWindowTexture(void* handle) override;
    void DrawWindowTexture(void* handle, float posX, float posY, float width, float height) override;
    void FetchInfo() override;

    // ---- 2D 网格渲染（一般贴图 + 离屏网格绘制）----
    void* CreateTarget(int width, int height) override;
    void DestroyTarget(void* target) override;
    void SetTarget(void* target) override;
    void ClearTarget(bool clearColor) override;
    uint8_t* LockTarget(void* target, int& pitch) override;
    void UnlockTarget(void* target) override;
    void* GetTargetTexture(void* target) override;
    void UpdateTargetTexture(void* target, const uint8_t* pixels, int width, int height, int pitch) override;
    void* CreateTexture(int width, int height) override;
    void UpdateTexture(void* texture, const uint8_t* pixels, int width, int height, int pitch) override;
    void DestroyTexture(void* texture) override;
    void SetMask(void* maskTarget) override;
    void SetBlendMode(int mode, const float* uniformColor) override;
    void DrawMesh(const float* vertices,
                  int vertexCount,
                  const uint16_t* indices,
                  int indexCount,
                  void* texture,
                  float opacity) override;

    // ---- Layer 合成（图层合成路径，软件 RenderManager 语义）----
    void LayerSetBlend(int method, float opacity, const float* uniformColor) override;
    void LayerDrawRect(void* texture,
                       float x,
                       float y,
                       float w,
                       float h,
                       float u0,
                       float v0,
                       float u1,
                       float v1) override;

private:
    // ---- 贴图（窗口贴图与一般贴图共用同一实现）----
    struct Texture
    {
        unsigned int id = 0;
        int width = 0, height = 0;
    };
    Texture* FindTexture(void* handle) const;

    // 窗口合成资源
    bool EnsureResources(); // 惰性创建 shader program / VAO / VBO / EBO

    unsigned int program = 0, vao = 0, vbo = 0, ebo = 0;
    int locTexture = -1;
    int locWindowSize = -1;
    int locPosition = -1;
    int locSize = -1;
    int viewportW = 0, viewportH = 0;

    // 2D 网格资源
    struct Target
    {
        unsigned int fbo = 0;
        unsigned int colorTex = 0;
        unsigned int depthTex = 0;
        int width = 0, height = 0;
        Texture* texture = nullptr; // 颜色纹理的采样包装（注册在 textures_ 中）
        std::vector<uint8_t> readback; // LockTarget 回读缓冲
    };
    Target* FindTarget(void* handle) const;

    bool EnsureMeshProgram();
    bool EnsureLayerProgram(); // Layer 合成专用 shader（软件 bm* 语义）

    unsigned int program_ = 0, vao_ = 0, vbo_ = 0, ibo_ = 0;
    size_t vboSize_ = 0, iboSize_ = 0;
    int locTexture_ = -1, locMask_ = -1, locEnableMask_ = -1, locEnableColor_ = -1;
    int locOpa_ = -1, locUniformColor_ = -1, locViewportSize_ = -1;

    // Layer 合成资源
    unsigned int programLayer_ = 0;
    int locLayerTexture_ = -1, locLayerMethod_ = -1, locLayerOpa_ = -1, locLayerUniformColor_ = -1;

    Target* currentTarget_ = nullptr;
    Target* maskTarget_ = nullptr; // 当前蒙版
    int blendMode_ = 0;
    bool skipDraw_ = false;
    bool enableColor_ = false;
    float uniformColor_[4] = {0, 0, 0, 0};
    int layerMethod_ = 0;     // LayerBlendMethod
    float layerOpa_ = 1.0f;   // 0..1
    float layerUniformColor_[4] = {0, 0, 0, 0};
    std::vector<Target*> targets_;
    std::vector<Texture*> textures_;
};
} // namespace krkrsdl3
