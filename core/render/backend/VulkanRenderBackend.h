#pragma once

#include "TVPCompositor.h"
#include <vulkan/vulkan.h>

//---------------------------------------------------------------------------
// Vulkan 渲染后端（合并实现：窗口合成 + 2D 网格）
//
// 编译条件：_KRKRSDL3_USE_SDL3 且 _KRKRSDL3_USE_VULKAN（CMake 中检测到 Vulkan 时定义）。
// 不支持的平台（Emscripten/OHOS）不会编译本文件，也不会注册 "vulkan" 后端。
//
// 一个类（iTVPRenderBackend，见 backend/RenderBackend.h）同时承担两个角色：
//   - 窗口贴图合成（上屏）
//   - 一般贴图 + 离屏网格绘制（供 emoteplayer 等插件）
//     GPU 后端下窗口贴图与一般贴图是同一套纹理实现
//     （CreateWindowTexture == CreateTexture 等）。
//
// 实现要点：
//   - 窗口表面：SDL_Vulkan_CreateSurface，实例扩展来自 SDL_Vulkan_GetInstanceExtensions
//   - 纹理：线性布局 + 主机可见内存（与"每帧全量上传"的现有数据流一致），
//     采样布局为 VK_IMAGE_LAYOUT_GENERAL；未来可升级为 staging + 设备本地内存
//   - 呈现：FIFO（vsync）或 IMMEDIATE，随 -vsync 配置切换；交换链在窗口尺寸
//     变化或呈现返回 OUT_OF_DATE 时重建
//   - 着色器：嵌入式 SPIR-V（shader/vk_quad.* + vk2d_quad.*，经 glslc 编译，
//     见同目录 shader/）
//---------------------------------------------------------------------------
namespace krkrsdl3
{
iTVPRenderBackend* CreateVulkanRenderBackend(VkInstance _instance, VkSurfaceKHR _surface);
bool VulkanRenderBackendAvailable();
} // namespace krkrsdl3
