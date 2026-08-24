#pragma once

#include <vector>
#include <string>

/*
* Windows/View Operation
*/
void TVPSetWindowTitle(const char* title);
std::string TVPGetWindowTitle();
void TVPSetWindowFullscreen(bool isFullscreen);
void TVPGetWindowSize(int* w, int* h);
void TVPGetWindowSizeInPixels(int* w, int* h);
void TVPSetWindowSize(int w, int h);
int TVPDrawSceneOnce(int interval);
int TVPConvertKeyCodeToVKCode(int keyCode);

/*
* Render Operation
*/

struct TVPSprite
{
    // 后端持有的不透明贴图句柄（由渲染后端分配/释放，见 core/render/backend/RenderBackend.h）
    // GL 后端: 内部贴图对象指针；SDL 软渲染: SDL_Texture*；
    // Vulkan 后端: VulkanTexture*；未来 Metal: 持有 id<MTLTexture> 的包装对象指针
    void* texture = nullptr;
    int type = 0; // 0:窗口 1:modal 2:overlay
    int xPos = 0, yPos = 0;
    float scale = 1.0;
    int width = 0, height = 0;
    bool isVisible = false;
    // 纹理是否借用于外部（如 GPU 纹理别名）：借用的句柄不由 sprite 销毁
    bool borrowedTexture = false;
};

// 获取所有渲染可用后端（SDL 渲染驱动列表，仅作信息展示；选择逻辑见 TVPListRenderBackends）
std::vector<std::string> TVPListAllRenderBackend();
// 软件渲染路径是否可用（各平台入口实现；OHOS 等无软渲染的平台返回 false）
bool TVPSoftwareRenderBackendAvailable();
// 软渲染平台接口（由各平台入口实现，供 SWRenderBackend 调用）
void TVPCreateTextureBackend(TVPSprite& sp);
void TVPUpdateTextureBackend(TVPSprite* sp, uint8_t* buff, int width, int height, int pitch);
void TVPDestroyTextureBackend(TVPSprite* sp);
void TVPRenderTextureBackend(TVPSprite* sp, int posX, int posY, int width, int height);
// 软渲染清屏/呈现（由各平台入口实现）
void TVPRenderClearBackend();
void TVPRenderPresentBackend();
// GL 呈现钩子（SDL_GL_SwapWindow / eglSwapBuffers），由 GLRenderBackend 调用
void TVPSwapBuffersBackend();
