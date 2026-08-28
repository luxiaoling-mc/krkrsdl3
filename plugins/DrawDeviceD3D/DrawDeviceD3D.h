#pragma once

#include "tjsCommHead.h"
#include "DrawDevice.h"
#include "TVPWindow.h"
#include "TVPCompositor.h"
#include "ncbind/ncbind.hpp"

//---------------------------------------------------------------------------
// DrawDeviceD3D —— 基于渲染后端抽象的 GPU 绘制设备
//
// 角色：
//   1. iTVPDrawDevice 实现（经 Window.drawDevice = xxx 的 interface 属性注入）
//   2. 脚本类 DrawDeviceD3D（new DrawDeviceD3D(w, h)）：
//      primaryLayers / layerManagerIndex / update(diff) / capture(layer, index)
//      / startTransition / stopTransition / transState / setScreenRect
//      / setPrimarySize / setOffset / stretchType / bicubicParam / clearColor
//      / checkEnable / getModule / maskMode
//   3. 独立 D3D 层（D3DLayer + D3DPicture）的 GPU 合成管理：
//      D3D 层走渲染后端的 Layer 合成路径（软件 RenderManager 混合语义）；
//      普通 Layer 树保持完全软渲染（tjsNativeLayer 无 GPU 路径），
//      其 DrawBuffer 每帧作为贴图上传并与 D3D 层合成到合成目标。
//
// 呈现模型：
//   - 每个 LayerManager 的 DrawBuffer 为软件位图（与渲染基准一致）
//   - 每帧：软件 DrawBuffer 上传 → 与独立 D3D 层（drawPlane Both/Front/Back、
//     frontIndex 排序）合成到合成目标；D3DEmotePlayer 先绘制到所属 D3DLayer
//     → 转场交叉淡化
//   - 合成目标作为窗口贴图交给后端上屏（GPU 后端零拷贝；
//     软件后端回读为 SDL 纹理，保底）
//---------------------------------------------------------------------------
namespace drawdevice
{

// 设备实例解析：脚本传入的设备对象可以是 DrawDeviceD3D 或 D3D
//（定义于 DrawDeviceD3D_reg.cpp；两者原生实例均为 DrawDeviceD3D 子类）
class DrawDeviceD3D;
DrawDeviceD3D* GetDrawDeviceInstance(iTJSDispatch2* obj);

class D3DImage;
class D3DPicture;
class D3DEmotePlayer;

class DrawDeviceD3D : public iTVPDrawDevice
{
    // ---- 层管理器（primaryLayers）----
    struct ManagerInfo
    {
        iTVPLayerManager* Manager = nullptr;
        tTJSNI_BaseLayer* Primary = nullptr;
    };
    std::vector<ManagerInfo> Managers;

    // ---- 独立 D3D 层（D3DLayer，不在 LayerManager 树内）----
    std::vector<class D3DLayer*> D3DLayers;

    // ---- 窗口/设备 ----
    TVPWindow* Window = nullptr;
    krkrsdl3::iTVPRenderBackend* Backend = nullptr;

    tjs_int Width = 0, Height = 0;        // 画面（虚拟）尺寸
    tjs_int ScreenX = 0, ScreenY = 0;     // setScreenRect
    tjs_int ScreenW = 0, ScreenH = 0;
    tjs_int OffsetX = 0, OffsetY = 0;     // setOffset
    tjs_int LayerManagerIndex = 0;        // 事件传递索引
    tjs_uint32 ClearColor = 0xFF000000;
    tjs_int StretchType = 0;
    tjs_real BicubicParam = 0.5;
    tjs_real TransState = 1.0;            // 0=完成 1=开始
    bool EmoteModuleEnabled = false;
    tjs_int MaskMode = 0;

    // ---- 合成目标 ----
    tjs_int LayerDrawIndex = 1;           // back<->fore 切换索引 通过Trans进行 1<->2切换
    void* CompositeTarget = nullptr;      // 后端离屏目标（合成结果，compositor 体系贴图）
    void* PresentScratchTexture = nullptr; // SW 后端呈现中转用贴图（目标 → 一般贴图）
    void* PrevCompositeTarget = nullptr;  // 转场快照（crossfade 用）
    void* ScratchTexture = nullptr;       // 软件 DrawBuffer 上传用的一般贴图
    bool TransitionActive = false;
    tjs_real TransitionProgress = 1.0;

    bool WindowReady = false;
    bool Initialized = false;

public:
    DrawDeviceD3D(tjs_int w, tjs_int h);
    ~DrawDeviceD3D();

    // ---- iTVPDrawDevice ----
    void Destruct() override;
    void SetWindowInterface(TVPWindow* window) override;
    void AddLayerManager(iTVPLayerManager* manager) override;
    void RemoveLayerManager(iTVPLayerManager* manager) override;
    void SetDestRectangle(const tTVPRect& rect) override {}
    void SetWindowSize(tjs_int w, tjs_int h) override {}
    void SetClipRectangle(const tTVPRect& rect) override {}
    void GetSrcSize(tjs_int& w, tjs_int& h) override;
    void NotifyLayerResize(iTVPLayerManager* manager) override;
    void NotifyLayerImageChange(iTVPLayerManager* manager) override;
    void OnClick(tjs_int x, tjs_int y) override;
    void OnDoubleClick(tjs_int x, tjs_int y) override;
    void OnMouseDown(tjs_int x, tjs_int y, tTVPMouseButton mb, tjs_uint32 flags) override;
    void OnMouseUp(tjs_int x, tjs_int y, tTVPMouseButton mb, tjs_uint32 flags) override;
    void OnMouseMove(tjs_int x, tjs_int y, tjs_uint32 flags) override;
    void OnReleaseCapture() override;
    void OnMouseOutOfWindow() override;
    void OnKeyDown(tjs_uint key, tjs_uint32 shift) override;
    void OnKeyUp(tjs_uint key, tjs_uint32 shift) override;
    void OnKeyPress(tjs_uint16 key) override;
    void OnMouseWheel(tjs_uint32 shift, tjs_int delta, tjs_int x, tjs_int y) override;
    void OnTouchDown(tjs_real x, tjs_real y, tjs_real cx, tjs_real cy, tjs_uint32 id) override;
    void OnTouchUp(tjs_real x, tjs_real y, tjs_real cx, tjs_real cy, tjs_uint32 id) override;
    void OnTouchMove(tjs_real x, tjs_real y, tjs_real cx, tjs_real cy, tjs_uint32 id) override;
    void OnTouchScaling(tjs_real startdist, tjs_real curdist, tjs_real cx, tjs_real cy, tjs_int flag) override;
    void OnTouchRotate(tjs_real startangle, tjs_real curangle, tjs_real dist, tjs_real cx, tjs_real cy, tjs_int flag) override;
    void OnMultiTouch() override;
    void OnDisplayRotate(tjs_int orientation, tjs_int rotate, tjs_int bpp, tjs_int width, tjs_int height) override {}
    void RecheckInputState() override;
    void SetDefaultMouseCursor(iTVPLayerManager* manager) override;
    void SetMouseCursor(iTVPLayerManager* manager, tjs_int cursor) override;
    void GetCursorPos(iTVPLayerManager* manager, tjs_int& x, tjs_int& y) override;
    void SetCursorPos(iTVPLayerManager* manager, tjs_int x, tjs_int y) override;
    void SetHintText(iTVPLayerManager* manager, iTJSDispatch2* sender, const ttstr& text) override;
    void WindowReleaseCapture(iTVPLayerManager* manager) override;
    void SetAttentionPoint(iTVPLayerManager* manager, tTJSNI_BaseLayer* layer, tjs_int l, tjs_int t) override;
    void DisableAttentionPoint(iTVPLayerManager* manager) override;
    void SetImeMode(iTVPLayerManager* manager, tTVPImeMode mode) override;
    void ResetImeMode(iTVPLayerManager* manager) override;
    tTJSNI_BaseLayer* GetPrimaryLayer() override;
    tTJSNI_BaseLayer* GetFocusedLayer() override;
    void SetFocusedLayer(tTJSNI_BaseLayer* layer) override;
    void RequestInvalidation(const tTVPRect& rect) override {}
    void Update() override;
    void Show() override;
    bool WaitForVBlank(tjs_int* in_vblank, tjs_int* delayed) override { return false; }
    void DumpLayerStructure() override;
    void SetShowUpdateRect(bool b) override {}
    bool SwitchToFullScreen(int window, tjs_uint w, tjs_uint h, tjs_uint bpp, tjs_uint color, bool changeresolution) override { return true; }
    void RevertFromFullScreen(int window, tjs_uint w, tjs_uint h, tjs_uint bpp, tjs_uint color) override {}
    void StartBitmapCompletion(iTVPLayerManager* manager) override {}
    void NotifyBitmapCompleted(iTVPLayerManager* manager, tjs_int x, tjs_int y, tTVPBaseTexture* bmp, const tTVPRect& cliprect, tTVPLayerType type, tjs_int opacity) override {}
    void EndBitmapCompletion(iTVPLayerManager* manager) override {}

    // ---- 脚本 API ----
    tjs_int64 getInterface() { return (tjs_int64)(tjs_intptr_t)(iTVPDrawDevice*)this; }
    tjs_int getWidth() { return Width; }
    tjs_int getHeight() { return Height; }
    void setWidth(tjs_int w) { Width = w; }
    void setHeight(tjs_int h) { Height = h; }
    void setSize(tjs_int w, tjs_int h) { Width = w; Height = h; }
    tjs_int getLayerManagerIndex() { return LayerManagerIndex; }
    void setLayerManagerIndex(tjs_int i)
    {
        if (i >= 0 && i < (tjs_int)Managers.size())
            LayerManagerIndex = i;
    }
    tjs_uint32 getClearColor() { return ClearColor; }
    void setClearColor(tjs_uint32 c) { ClearColor = c; }
    tjs_int getStretchType() { return StretchType; }
    void setStretchType(tjs_int v) { StretchType = v; }
    tjs_real getBicubicParam() { return BicubicParam; }
    void setBicubicParam(tjs_real v) { BicubicParam = v; }
    tjs_real getTransState() { return TransState; }
    void setTransState(tjs_real v) { TransState = v; }
    tjs_int getMaskMode() { return MaskMode; }
    void setMaskMode(tjs_int v) { MaskMode = v; }
    bool checkEnable(tTJSString name) { return name == TJS_N("emote"); }
    tTJSVariant getModule(tTJSString name);
    void update(tjs_real diffTime);
    void capture(iTJSDispatch2* layer, tjs_int index);
    void startTransition(iTJSDispatch2* dict);
    void stopTransition();
    void setScreenRect(tjs_int x, tjs_int y, tjs_int w, tjs_int h);
    void setPrimarySize(tjs_int w, tjs_int h);
    void setOffset(tjs_int x, tjs_int y);
    void recreate();
    void finalize();
    tTJSVariant getPrimaryLayers();

    // ---- 内部 ----
    void RegisterD3DLayer(class D3DLayer* layer) { D3DLayers.push_back(layer); }
    void UnregisterD3DLayer(class D3DLayer* layer);
    krkrsdl3::iTVPRenderBackend* GetBackend() { return Backend; }
    tjs_int GetWidth() const { return Width; }
    tjs_int GetHeight() const { return Height; }
    tjs_int GetOffsetX() const { return OffsetX; }
    tjs_int GetOffsetY() const { return OffsetY; }

private:
    void EnsureBackend();
    void RenderFrame();
    void ComposeLayerManager(int index, void* target);
    void PresentToWindow();
    bool IsSoftwareBackend() const;
    void DrawQuadTo(void* target, void* srcTexture, float x, float y, float w, float h,
                    int blendMode, float opacity, const float matrix[16] = nullptr);
};

//---------------------------------------------------------------------------
// D3D —— KAG 系统脚本（KAGEnvPlayer.tjs 的 EnvObjectWorldSnap）使用的
// 快照环境绘制设备，native 侧实现（与 DrawDeviceD3D 同接口，C++ 继承）。
// 脚本用法：d3dDevice = new D3D(kag.exWidth, kag.exHeight);
// 之后该对象作为设备句柄传给 D3DLayer/D3DImage 的原生构造。
//---------------------------------------------------------------------------
class D3D : public DrawDeviceD3D
{
public:
    D3D(tjs_int w, tjs_int h) : DrawDeviceD3D(w, h) {}
    void finalize() {}
};

//---------------------------------------------------------------------------
// D3DLayer —— 脚本可继承的 GPU 绘制层（DrawPlaneBoth=0/Front=1/Back=2）
//---------------------------------------------------------------------------
class D3DLayer
{
public:
    static const tjs_int DrawPlaneBoth = 0;
    static const tjs_int DrawPlaneFront = 1;
    static const tjs_int DrawPlaneBack = 2;

    DrawDeviceD3D* Device = nullptr;
    iTJSDispatch2* ScriptObject = nullptr; // 脚本对象（onUpdate 驱动用，AddRef）
    tjs_int drawPlane = DrawPlaneBoth;
    tjs_int frontIndex = 0;
    tjs_int backIndex = 0;
    float Matrix[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    std::vector<class D3DPicture*> Pictures;
    void* EmoteTarget = nullptr;
    void* EmoteMaskTarget = nullptr;

    D3DLayer(iTJSDispatch2* d3dDeviceObj, iTJSDispatch2* scriptObject);
    ~D3DLayer();
    void DriveOnUpdate(tjs_real diffTime); // 调用脚本 onUpdate(diff)（D3DAffineLayer 等）
    void setMatrix(tjs_real m11, tjs_real m12, tjs_real m13, tjs_real m14, tjs_real m21, tjs_real m22,
                   tjs_real m23, tjs_real m24, tjs_real m31, tjs_real m32, tjs_real m33, tjs_real m34,
                   tjs_real m41, tjs_real m42, tjs_real m43, tjs_real m44);
    void AddPicture(class D3DPicture* pic);
    void RemovePicture(class D3DPicture* pic);
    void DrawEmoteTarget(void* target);
    void* GetEmoteTarget() const { return EmoteTarget; }
    tjs_int getDrawPlane() { return drawPlane; }
    void setDrawPlane(tjs_int v) { drawPlane = v; }
    tjs_int getFrontIndex() { return frontIndex; }
    void setFrontIndex(tjs_int v) { frontIndex = v; }
    tjs_int getBackIndex() { return backIndex; }
    void setBackIndex(tjs_int v) { backIndex = v; }
    void finalize();
};

//---------------------------------------------------------------------------
// D3DImage —— GPU 纹理（从 Layer 加载像素）
//---------------------------------------------------------------------------
class D3DImage
{
public:
    DrawDeviceD3D* Device = nullptr;
    void* Texture = nullptr; // 后端一般贴图（CreateTexture）
    tjs_int Width = 0, Height = 0;

    D3DImage(iTJSDispatch2* drawDeviceObj);
    ~D3DImage();
    void load(iTJSDispatch2* layerObj);
    tjs_int getWidth() { return Width; }
    tjs_int getHeight() { return Height; }
    void* GetTexture() { return Texture; }
    void finalize();
};

//---------------------------------------------------------------------------
// D3DPicture —— D3DLayer 的显示单元（纹理 + 区域 + 混合 + 坐标）
//---------------------------------------------------------------------------
class D3DPicture
{
public:
    class D3DLayer* Layer = nullptr;
    D3DImage* Image = nullptr;
    tjs_int SrcX = 0, SrcY = 0, SrcW = 0, SrcH = 0; // 源区域
    tjs_int DstX = 0, DstY = 0;                     // 目标偏移
    tjs_int blendMode = 2;                          // Layer.type 常量（ltAlpha=2）
    tjs_int opacity = 255;
    float CoordX = 0, CoordY = 0;

    D3DPicture(iTJSDispatch2* layerObj, iTJSDispatch2* imageObj);
    ~D3DPicture();
    void assignImageRange(tjs_int sx, tjs_int sy, tjs_int sw, tjs_int sh, tjs_int dx, tjs_int dy);
    void setCoord(tjs_real x, tjs_real y) { CoordX = (float)x; CoordY = (float)y; }
    void finalize();
    tjs_int getBlendMode() { return blendMode; }
    void setBlendMode(tjs_int v) { blendMode = v; }
    tjs_int getOpacity() { return opacity; }
    void setOpacity(tjs_int v) { opacity = v; }
};

//---------------------------------------------------------------------------
// Layer 附加属性（drawPlane/frontIndex/backIndex）
//---------------------------------------------------------------------------
class LayerD3DAttach
{
    tjs_int drawPlane = 0;
    tjs_int frontIndex = 0;
    tjs_int backIndex = 0;

public:
    tjs_int getDrawPlane() { return drawPlane; }
    void setDrawPlane(tjs_int v) { drawPlane = v; }
    tjs_int getFrontIndex() { return frontIndex; }
    void setFrontIndex(tjs_int v) { frontIndex = v; }
    tjs_int getBackIndex() { return backIndex; }
    void setBackIndex(tjs_int v) { backIndex = v; }
};

} // namespace drawdevice
