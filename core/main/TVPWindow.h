//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// TVPWindow : engine-side window core
//---------------------------------------------------------------------------
#pragma once

#include "tjsNative.h"
#include "drawable.h"
#include "ComplexRect.h"
#include "tvpinputdefs.h"
#include "TVPEvent.h"
#include "ObjectList.h"
#include "DrawDevice.h"
#include "LayerTreeOwner.h"
#include "VelocityTracker.h"
#include "RenderManager.h"
#include "PlatformView.h"

#define USE_OBSOLETE_FUNCTIONS

//---------------------------------------------------------------------------
// Window related constants
//---------------------------------------------------------------------------
enum
{
    ssShift = TVP_SS_SHIFT,
    ssAlt = TVP_SS_ALT,
    ssCtrl = TVP_SS_CTRL,
    ssLeft = TVP_SS_LEFT,
    ssRight = TVP_SS_RIGHT,
    ssMiddle = TVP_SS_MIDDLE,
    ssDouble = TVP_SS_DOUBLE,
    ssRepeat = TVP_SS_REPEAT,
};

enum tTVPWMRRegMode
{
    wrmRegister = 0,
    wrmUnregister = 1
};
enum
{
    orientUnknown,
    orientPortrait,
    orientLandscape,
};

enum tTVPUpdateType
{
    utNormal, // only needed region
    utEntire  // entire of window
};
enum tTVPBorderStyle
{
    bsNone = 0,
    bsSingle = 1,
    bsSizeable = 2,
    bsDialog = 3,
    bsToolWindow = 4,
    bsSizeToolWin = 5
};
enum tTVPMouseCursorState
{
    mcsVisible,    // the mouse cursor is visible
    mcsTempHidden, // the mouse cursor is temporarily hidden
    mcsHidden      // the mouse cursor is invisible
};
//---------------------------------------------------------------------------

// script side types; only forward-declared here, never included
class tTJSNI_BaseLayer;

//---------------------------------------------------------------------------
//! @brief 窗口核心类（非 script 部分）
//---------------------------------------------------------------------------
class TVPWindow : public iTVPLayerTreeOwner
{
    // script 绑定（tTJSNI_Window）通过本类操作窗口
    iTJSDispatch2* Owner = nullptr; //!< 对应的 TJS Window 对象（由脚本绑定设置）
    tTJSVariant DrawDeviceObject;   //!< Current Draw Device TJS2 Object
    iTVPDrawDevice* DrawDevice = nullptr; //!< Current Draw Device

    tTVPRect WindowExposedRegion;
    bool WindowUpdating = false; // window is in updating
    bool WaitVSync = false;

    // paint box / sprite
    tjs_int LayerWidth = 0, LayerHeight = 0;
    tjs_int ActualZoomDenom = 1; // Zooming factor denominator (actual)
    tjs_int ActualZoomNumer = 1; // Zooming factor numerator (actual)
    std::string Caption;

    // input state
    tjs_int LastMouseX = 0, LastMouseY = 0;
    bool UseMouseKey = false;
    bool MouseLeftButtonEmulatedPushed = false, MouseRightButtonEmulatedPushed = false;
    VelocityTrackers TouchVelocityTracker;
    VelocityTracker MouseVelocityTracker;

    // form state
    bool isFullScreen = false;
    tTVPImeMode LastSetImeMode = ::imDisable;
    tTVPImeMode DefaultImeMode = ::imDisable;
    tjs_int TextInputPosY = 0;

    // close / modal state
    bool Closing = false, ProgramClosing = false, CanCloseWork = false;
    bool in_mode_ = false; // is modal
    int modal_result_ = 0;
    bool Closed = false; // 已与脚本对象脱离（NotifyWindowClose 后）

protected:
    tTVPMouseCursorState MouseCursorState = mcsVisible;
    tjs_int HintDelay = 500;
    tjs_int ZoomDenom = 1; // Zooming factor denominator (setting)
    tjs_int ZoomNumer = 1; // Zooming factor numerator (setting)
    double TouchScaleThreshold = 5, TouchRotateThreshold = 5;

public:
    TVPSprite* pSprite = nullptr; //!< 合成器使用的窗口 sprite

public:
    TVPWindow();
    ~TVPWindow();

    // 解除 sprite 对 compositor 纹理（backend 离屏目标）的借用；
    // 由 GPU DrawDevice 析构时调用（其目标即将销毁，sprite 不可再引用）
    void ReleaseBorrowedTexture();

    //----- script binding interface
    void SetOwner(iTJSDispatch2* owner) { Owner = owner; }
    iTJSDispatch2* GetOwnerNoAddRef() const override { return Owner; }
    bool IsClosed() const { return Closed; }
    void NotifyWindowClose() { Closed = true; }

    //----- interface to draw device
    void SetDrawDeviceObject(const tTJSVariant& val);
    const tTJSVariant& GetDrawDeviceObject() const { return DrawDeviceObject; }
    iTVPDrawDevice* GetDrawDevice() const { return DrawDevice; }
    void ResetDrawDevice() {}
    bool IsMainWindow() const;

    //----- input event posting（供 WindowManager / 脚本绑定调用）
    void PostKeyDown(tjs_uint16 key, tjs_uint32 shift); // 旧 InternalKeyDown
    void PostKeyPress(tjs_uint16 key);                  // 旧 iWindowLayer::OnKeyPress

    //----- input event reception（输入事件队列投递到此处）
    void OnClose();
    void OnResize();
    void OnClick(tjs_int x, tjs_int y);
    void OnDoubleClick(tjs_int x, tjs_int y);
    void OnMouseDown(tjs_int x, tjs_int y, tTVPMouseButton mb, tjs_uint32 flags);
    void OnMouseUp(tjs_int x, tjs_int y, tTVPMouseButton mb, tjs_uint32 flags);
    void OnMouseMove(tjs_int x, tjs_int y, tjs_uint32 flags);
    void OnReleaseCapture();
    void OnMouseOutOfWindow();
    void OnMouseEnter();
    void OnMouseLeave();
    void OnKeyDown(tjs_uint key, tjs_uint32 shift);
    void OnKeyUp(tjs_uint key, tjs_uint32 shift);
    void OnKeyPress(tjs_uint16 key);
    void OnFileDrop(const tTJSVariant& array);
    void OnMouseWheel(tjs_uint32 shift, tjs_int delta, tjs_int x, tjs_int y);
    void OnPopupHide();
    void OnActivate(bool activate_or_deactivate);
    void OnTouchDown(tjs_real x, tjs_real y, tjs_real cx, tjs_real cy, tjs_uint32 id);
    void OnTouchUp(tjs_real x, tjs_real y, tjs_real cx, tjs_real cy, tjs_uint32 id);
    void OnTouchMove(tjs_real x, tjs_real y, tjs_real cx, tjs_real cy, tjs_uint32 id);
    void OnTouchScaling(
        tjs_real startdist, tjs_real curdist, tjs_real cx, tjs_real cy, tjs_int flag);
    void OnTouchRotate(tjs_real startangle,
                       tjs_real curangle,
                       tjs_real dist,
                       tjs_real cx,
                       tjs_real cy,
                       tjs_int flag);
    void OnMultiTouch();
    void OnHintChange(const ttstr& text, tjs_int x, tjs_int y, bool isshow);
    void OnDisplayRotate(
        tjs_int orientation, tjs_int rotate, tjs_int bpp, tjs_int hresolution, tjs_int vresolution);

    void ClearInputEvents() { TVPCancelInputEvents(this); }
    void PostReleaseCaptureEvent();

    //----- paint box / geometry
    void SetPaintBoxSize(tjs_int w, tjs_int h);
    void SetWidth(tjs_int w);
    void SetHeight(tjs_int h);
    void SetSize(tjs_int w, tjs_int h);
    tjs_int GetWidth() const { return LayerWidth; }
    tjs_int GetHeight() const { return LayerHeight; }
    void GetSize(tjs_int& w, tjs_int& h);
    void GetWinSize(tjs_int& w, tjs_int& h);
    void SetPosition(tjs_int x, tjs_int y);
    void SetZoom(tjs_int numer, tjs_int denom);
    void SetZoomNumer(tjs_int n) { SetZoom(n, ZoomDenom); }
    tjs_int GetZoomNumer() const { return ZoomNumer; }
    void SetZoomDenom(tjs_int d) { SetZoom(ZoomDenom, d); }
    tjs_int GetZoomDenom() const { return ZoomDenom; }
    void ZoomRectangle(tjs_int& left, tjs_int& top, tjs_int& right, tjs_int& bottom);
    // 统一呈现入口：把 compositor 体系（iTVPRenderBackend）的可采样贴图呈现到窗口 sprite
    //   GPU 后端 → 纹理别名（零拷贝）；软件后端 → 回读上传窗口贴图（SW 保底路径）
    // 各 DrawDevice 自行把呈现源（软渲染 iTVPTexture2D / D3D 合成目标）转化为
    // compositor 贴图后调用本入口
    void PresentTexture(void* texture, tjs_int w, tjs_int h);
    void GetVideoOffset(tjs_int& ofsx, tjs_int& ofsy)
    {
        ofsx = 0;
        ofsy = 0;
    }

    //----- form state
    bool GetVisible() const { return pSprite ? pSprite->isVisible : false; }
    void SetVisible(bool bVisible);
    void SetVisibleFromScript(bool b) { SetVisible(b); }
    const char* GetCaption() const { return Caption.c_str(); }
    void SetCaption(const std::string& s);
    void SetFullScreenMode(bool isFull);
    bool GetFullScreenMode() const { return isFullScreen; }
    bool GetWindowActive() const;
    void BringToFront();
    void ShowWindowAsModal();
    void TickBeat();
    void SetDefaultMouseCursor() {}
    void SetMouseCursor(tjs_int) {}
    void GetCursorPos(tjs_int& x, tjs_int& y);
    void SetCursorPos(tjs_int, tjs_int) {}
    void SetHintText(iTJSDispatch2*, const ttstr&) {}
    void WindowReleaseCapture() {}
    void SetAttentionPoint(tTJSNI_BaseLayer* layer, tjs_int left, tjs_int top);
    void DisableAttentionPoint() {}
    void SetImeMode(tTVPImeMode mode);
    void SetDefaultImeMode(tTVPImeMode mode) { DefaultImeMode = mode; }
    tTVPImeMode GetDefaultImeMode() const { return DefaultImeMode; }
    void ResetImeMode() { SetImeMode(DefaultImeMode); }
    void SetUseMouseKey(bool b);
    bool GetUseMouseKey() const { return UseMouseKey; }
    void ResetMouseVelocity() { MouseVelocityTracker.clear(); }
    bool GetMouseVelocity(float& x, float& y, float& speed) const;
    void ResetTouchVelocity(tjs_int id) { TouchVelocityTracker.end(id); }

    //----- close / modal
    void Close();
    void OnCloseQueryCalled(bool b);
    void UpdateWindow(tTVPUpdateType type);

    //----- update management
    void NotifySrcResize();
    void RequestUpdate() { TVPPostWindowUpdate(this); }
    void BeginUpdate(const tTVPComplexRect& rects) { WindowUpdating = true; }
    void EndUpdate() { WindowUpdating = false; }
    void UpdateContent();
    void DeliverDrawDeviceShow();
    void NotifyUpdateRegionFixed(const tTVPComplexRect& updaterects) { BeginUpdate(updaterects); }
    void NotifyWindowExposureToLayer(const tTVPRect& cliprect);
    void DumpPrimaryLayerStructure();
    void RecheckInputState();
    void SetShowUpdateRect(bool b);
    bool WaitForVBlank(tjs_int* in_vblank, tjs_int* delayed);

    //----- 旧 iWindowLayer 的兼容性接口（保持脚本行为不变）
    void RegisterWindowMessageReceiver(tTVPWMRRegMode, void*, const void*) {}
    void SetLeft(tjs_int) {}
    void SetTop(tjs_int) {}
    void SetMinWidth(tjs_int) {}
    void SetMaxWidth(tjs_int) {}
    void SetMinHeight(tjs_int) {}
    void SetMaxHeight(tjs_int) {}
    void SetInnerWidth(tjs_int v) { SetWidth(v); }
    void SetInnerHeight(tjs_int v) { SetHeight(v); }
    void SetInnerSize(tjs_int w, tjs_int h) { SetSize(w, h); }
    void SetMinSize(tjs_int, tjs_int) {}
    void SetMaxSize(tjs_int, tjs_int) {}
    void SetBorderStyle(tTVPBorderStyle) {}
    void SetStayOnTop(bool) {}
    tjs_int GetLeft() const { return 0; }
    tjs_int GetTop() const { return 0; }
    tjs_int GetMinWidth() const { return 0; }
    tjs_int GetMaxWidth() const { return 1920; }
    tjs_int GetMinHeight() const { return 0; }
    tjs_int GetMaxHeight() const { return 1080; }
    tjs_int GetInnerWidth() const { return GetWidth(); }
    tjs_int GetInnerHeight() const { return GetHeight(); }
    bool GetStayOnTop() const { return false; }
    tTVPBorderStyle GetBorderStyle() const { return bsNone; }
    void SetTrapKey(bool) {}
    bool GetTrapKey() const { return false; }
    void RemoveMaskRegion() {}
    void SetMouseCursorState(tTVPMouseCursorState mcs) { MouseCursorState = mcs; }
    tTVPMouseCursorState GetMouseCursorState() const { return MouseCursorState; }
    void HideMouseCursor() {}
    void SetFocusable(bool) {}
    bool GetFocusable() const { return true; }
    int GetDisplayRotate() const { return 0; }
    int GetDisplayOrientation() const { return orientLandscape; }
    void SetEnableTouch(bool) {}
    bool GetEnableTouch() const { return false; }
    void SetHintDelay(tjs_int delay) { HintDelay = delay; }
    tjs_int GetHintDelay() const { return HintDelay; }
    void SetInnerSunken(bool) {}
    bool GetInnerSunken() const { return false; }
    void SetTouchScaleThreshold(double threshold) { TouchScaleThreshold = threshold; }
    double GetTouchScaleThreshold() const { return TouchScaleThreshold; }
    void SetTouchRotateThreshold(double threshold) { TouchRotateThreshold = threshold; }
    double GetTouchRotateThreshold() const { return TouchRotateThreshold; }
    tjs_real GetTouchPointStartX(tjs_int) const { return 0; }
    tjs_real GetTouchPointStartY(tjs_int) const { return 0; }
    tjs_real GetTouchPointX(tjs_int) const { return 0; }
    tjs_real GetTouchPointY(tjs_int) const { return 0; }
    tjs_int GetTouchPointID(tjs_int) const { return 0; }
    tjs_int GetTouchPointCount() const { return 0; }
    bool GetTouchVelocity(tjs_int, float&, float&, float&) const { return false; }
    void BeginMove() {}
    void SetLayerLeft(tjs_int) {}
    tjs_int GetLayerLeft() const { return 0; }
    void SetLayerTop(tjs_int) {}
    tjs_int GetLayerTop() const { return 0; }
    void SetLayerPosition(tjs_int, tjs_int) {}
    void SetShowScrollBars(bool) {}
    bool GetShowScrollBars() const { return true; }

    void SetWaitVSync(bool enable) { WaitVSync = enable; }
    bool GetWaitVSync() const { return WaitVSync; }

public: // iTVPLayerTreeOwner（LayerManager -> LTO，全部转发给 DrawDevice）
    void RegisterLayerManager(iTVPLayerManager* manager) override;
    void UnregisterLayerManager(iTVPLayerManager* manager) override;
    void StartBitmapCompletion(iTVPLayerManager* manager) override;
    void NotifyBitmapCompleted(iTVPLayerManager* manager,
                               tjs_int x,
                               tjs_int y,
                               tTVPBaseTexture* bmp,
                               const tTVPRect& cliprect,
                               tTVPLayerType type,
                               tjs_int opacity) override;
    void EndBitmapCompletion(iTVPLayerManager* manager) override;
    void SetMouseCursor(iTVPLayerManager* manager, tjs_int cursor) override;
    void GetCursorPos(iTVPLayerManager* manager, tjs_int& x, tjs_int& y) override;
    void SetCursorPos(iTVPLayerManager* manager, tjs_int x, tjs_int y) override;
    void ReleaseMouseCapture(iTVPLayerManager* manager) override;
    void SetHint(iTVPLayerManager* manager, iTJSDispatch2* sender, const ttstr& hint) override;
    void NotifyLayerResize(iTVPLayerManager* manager) override;
    void NotifyLayerImageChange(iTVPLayerManager* manager) override;
    void SetAttentionPoint(iTVPLayerManager* manager,
                           tTJSNI_BaseLayer* layer,
                           tjs_int x,
                           tjs_int y) override;
    void DisableAttentionPoint(iTVPLayerManager* manager) override;
    void SetImeMode(iTVPLayerManager* manager, tjs_int mode) override;
    void ResetImeMode(iTVPLayerManager* manager) override;

private:
    void ResetDrawSprite();
    void RecalcPaintBox();
    bool OnCloseQuery();
    bool DoClose(); // 返回 true 表示 this 已被删除
};
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// Input Events
//---------------------------------------------------------------------------
class tTVPOnCloseInputEvent : public tTVPBaseInputEvent
{
    static tTVPUniqueTagForInputEvent Tag;

public:
    tTVPOnCloseInputEvent(TVPWindow* win) : tTVPBaseInputEvent(win, Tag) {};
    void Deliver() const { ((TVPWindow*)GetSource())->OnClose(); }
};
//---------------------------------------------------------------------------
class tTVPOnResizeInputEvent : public tTVPBaseInputEvent
{
    static tTVPUniqueTagForInputEvent Tag;

public:
    tTVPOnResizeInputEvent(TVPWindow* win) : tTVPBaseInputEvent(win, Tag) {};
    void Deliver() const { ((TVPWindow*)GetSource())->OnResize(); }
};
//---------------------------------------------------------------------------
class tTVPOnClickInputEvent : public tTVPBaseInputEvent
{
    static tTVPUniqueTagForInputEvent Tag;
    tjs_int X;
    tjs_int Y;

public:
    tTVPOnClickInputEvent(TVPWindow* win, tjs_int x, tjs_int y)
      : tTVPBaseInputEvent(win, Tag),
        X(x),
        Y(y){};
    void Deliver() const { ((TVPWindow*)GetSource())->OnClick(X, Y); }
};
//---------------------------------------------------------------------------
class tTVPOnDoubleClickInputEvent : public tTVPBaseInputEvent
{
    static tTVPUniqueTagForInputEvent Tag;
    tjs_int X;
    tjs_int Y;

public:
    tTVPOnDoubleClickInputEvent(TVPWindow* win, tjs_int x, tjs_int y)
      : tTVPBaseInputEvent(win, Tag),
        X(x),
        Y(y){};
    void Deliver() const { ((TVPWindow*)GetSource())->OnDoubleClick(X, Y); }
};
//---------------------------------------------------------------------------
class tTVPOnMouseDownInputEvent : public tTVPBaseInputEvent
{
    static tTVPUniqueTagForInputEvent Tag;
    tjs_int X;
    tjs_int Y;
    tTVPMouseButton Buttons;
    tjs_uint32 Flags;

public:
    tTVPOnMouseDownInputEvent(
        TVPWindow* win, tjs_int x, tjs_int y, tTVPMouseButton buttons, tjs_uint32 flags)
      : tTVPBaseInputEvent(win, Tag),
        X(x),
        Y(y),
        Buttons(buttons),
        Flags(flags){};
    void Deliver() const { ((TVPWindow*)GetSource())->OnMouseDown(X, Y, Buttons, Flags); }
};
//---------------------------------------------------------------------------
class tTVPOnMouseUpInputEvent : public tTVPBaseInputEvent
{
    static tTVPUniqueTagForInputEvent Tag;
    tjs_int X;
    tjs_int Y;
    tTVPMouseButton Buttons;
    tjs_uint32 Flags;

public:
    tTVPOnMouseUpInputEvent(
        TVPWindow* win, tjs_int x, tjs_int y, tTVPMouseButton buttons, tjs_uint32 flags)
      : tTVPBaseInputEvent(win, Tag),
        X(x),
        Y(y),
        Buttons(buttons),
        Flags(flags){};
    void Deliver() const { ((TVPWindow*)GetSource())->OnMouseUp(X, Y, Buttons, Flags); }
};
//---------------------------------------------------------------------------
class tTVPOnMouseMoveInputEvent : public tTVPBaseInputEvent
{
    static tTVPUniqueTagForInputEvent Tag;
    tjs_int X;
    tjs_int Y;
    tjs_uint32 Flags;

public:
    tTVPOnMouseMoveInputEvent(TVPWindow* win, tjs_int x, tjs_int y, tjs_uint32 flags)
      : tTVPBaseInputEvent(win, Tag),
        X(x),
        Y(y),
        Flags(flags){};
    void Deliver() const { ((TVPWindow*)GetSource())->OnMouseMove(X, Y, Flags); }
};
//---------------------------------------------------------------------------
class tTVPOnReleaseCaptureInputEvent : public tTVPBaseInputEvent
{
    static tTVPUniqueTagForInputEvent Tag;

public:
    tTVPOnReleaseCaptureInputEvent(TVPWindow* win) : tTVPBaseInputEvent(win, Tag) {};
    void Deliver() const { ((TVPWindow*)GetSource())->OnReleaseCapture(); }
};
//---------------------------------------------------------------------------
class tTVPOnMouseOutOfWindowInputEvent : public tTVPBaseInputEvent
{
    static tTVPUniqueTagForInputEvent Tag;

public:
    tTVPOnMouseOutOfWindowInputEvent(TVPWindow* win) : tTVPBaseInputEvent(win, Tag) {};
    void Deliver() const { ((TVPWindow*)GetSource())->OnMouseOutOfWindow(); }
};
//---------------------------------------------------------------------------
class tTVPOnMouseEnterInputEvent : public tTVPBaseInputEvent
{
    static tTVPUniqueTagForInputEvent Tag;

public:
    tTVPOnMouseEnterInputEvent(TVPWindow* win) : tTVPBaseInputEvent(win, Tag) {};
    void Deliver() const { ((TVPWindow*)GetSource())->OnMouseEnter(); }
};
//---------------------------------------------------------------------------
class tTVPOnMouseLeaveInputEvent : public tTVPBaseInputEvent
{
    static tTVPUniqueTagForInputEvent Tag;

public:
    tTVPOnMouseLeaveInputEvent(TVPWindow* win) : tTVPBaseInputEvent(win, Tag) {};
    void Deliver() const { ((TVPWindow*)GetSource())->OnMouseLeave(); }
};
//---------------------------------------------------------------------------
class tTVPOnKeyDownInputEvent : public tTVPBaseInputEvent
{
    static tTVPUniqueTagForInputEvent Tag;
    tjs_uint Key;
    tjs_uint32 Shift;

public:
    tTVPOnKeyDownInputEvent(TVPWindow* win, tjs_uint key, tjs_uint32 shift)
      : tTVPBaseInputEvent(win, Tag),
        Key(key),
        Shift(shift){};
    void Deliver() const { ((TVPWindow*)GetSource())->OnKeyDown(Key, Shift); }
};
//---------------------------------------------------------------------------
class tTVPOnKeyUpInputEvent : public tTVPBaseInputEvent
{
    static tTVPUniqueTagForInputEvent Tag;
    tjs_uint Key;
    tjs_uint32 Shift;

public:
    tTVPOnKeyUpInputEvent(TVPWindow* win, tjs_uint key, tjs_uint32 shift)
      : tTVPBaseInputEvent(win, Tag),
        Key(key),
        Shift(shift){};
    void Deliver() const { ((TVPWindow*)GetSource())->OnKeyUp(Key, Shift); }
};
//---------------------------------------------------------------------------
class tTVPOnKeyPressInputEvent : public tTVPBaseInputEvent
{
    static tTVPUniqueTagForInputEvent Tag;
    tjs_uint16 Key;

public:
    tTVPOnKeyPressInputEvent(TVPWindow* win, tjs_uint16 key)
      : tTVPBaseInputEvent(win, Tag),
        Key(key){};
    void Deliver() const { ((TVPWindow*)GetSource())->OnKeyPress(Key); }
};
//---------------------------------------------------------------------------
class tTVPOnFileDropInputEvent : public tTVPBaseInputEvent
{
    static tTVPUniqueTagForInputEvent Tag;
    tTJSVariant Array;

public:
    tTVPOnFileDropInputEvent(TVPWindow* win, const tTJSVariant& val)
      : tTVPBaseInputEvent(win, Tag),
        Array(val){};
    void Deliver() const { ((TVPWindow*)GetSource())->OnFileDrop(Array); }
};
//---------------------------------------------------------------------------
class tTVPOnMouseWheelInputEvent : public tTVPBaseInputEvent
{
    static tTVPUniqueTagForInputEvent Tag;
    tjs_uint32 Shift;
    tjs_int WheelDelta;
    tjs_int X;
    tjs_int Y;

public:
    tTVPOnMouseWheelInputEvent(
        TVPWindow* win, tjs_uint32 shift, tjs_int wheeldelta, tjs_int x, tjs_int y)
      : tTVPBaseInputEvent(win, Tag),
        Shift(shift),
        WheelDelta(wheeldelta),
        X(x),
        Y(y){};
    void Deliver() const { ((TVPWindow*)GetSource())->OnMouseWheel(Shift, WheelDelta, X, Y); }
};
//---------------------------------------------------------------------------
class tTVPOnPopupHideInputEvent : public tTVPBaseInputEvent
{
    static tTVPUniqueTagForInputEvent Tag;

public:
    tTVPOnPopupHideInputEvent(TVPWindow* win) : tTVPBaseInputEvent(win, Tag) {};
    void Deliver() const { ((TVPWindow*)GetSource())->OnPopupHide(); }
};
//---------------------------------------------------------------------------
class tTVPOnWindowActivateEvent : public tTVPBaseInputEvent
{
    static tTVPUniqueTagForInputEvent Tag;
    bool ActivateOrDeactivate;

public:
    tTVPOnWindowActivateEvent(TVPWindow* win, bool activate_or_deactivate)
      : tTVPBaseInputEvent(win, Tag),
        ActivateOrDeactivate(activate_or_deactivate){};
    void Deliver() const { ((TVPWindow*)GetSource())->OnActivate(ActivateOrDeactivate); }
};
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
class tTVPOnTouchDownInputEvent : public tTVPBaseInputEvent
{
    static tTVPUniqueTagForInputEvent Tag;
    tjs_real X;
    tjs_real Y;
    tjs_real CX;
    tjs_real CY;
    tjs_uint32 ID;

public:
    tTVPOnTouchDownInputEvent(
        TVPWindow* win, tjs_real x, tjs_real y, tjs_real cx, tjs_real cy, tjs_uint32 id)
      : tTVPBaseInputEvent(win, Tag),
        X(x),
        Y(y),
        CX(cx),
        CY(cy),
        ID(id){};
    void Deliver() const { ((TVPWindow*)GetSource())->OnTouchDown(X, Y, CX, CY, ID); }
};
//---------------------------------------------------------------------------
class tTVPOnTouchUpInputEvent : public tTVPBaseInputEvent
{
    static tTVPUniqueTagForInputEvent Tag;
    tjs_real X;
    tjs_real Y;
    tjs_real CX;
    tjs_real CY;
    tjs_uint32 ID;

public:
    tTVPOnTouchUpInputEvent(
        TVPWindow* win, tjs_real x, tjs_real y, tjs_real cx, tjs_real cy, tjs_uint32 id)
      : tTVPBaseInputEvent(win, Tag),
        X(x),
        Y(y),
        CX(cx),
        CY(cy),
        ID(id){};
    void Deliver() const { ((TVPWindow*)GetSource())->OnTouchUp(X, Y, CX, CY, ID); }
};
//---------------------------------------------------------------------------
class tTVPOnTouchMoveInputEvent : public tTVPBaseInputEvent
{
    static tTVPUniqueTagForInputEvent Tag;
    tjs_real X;
    tjs_real Y;
    tjs_real CX;
    tjs_real CY;
    tjs_uint32 ID;

public:
    tTVPOnTouchMoveInputEvent(
        TVPWindow* win, tjs_real x, tjs_real y, tjs_real cx, tjs_real cy, tjs_uint32 id)
      : tTVPBaseInputEvent(win, Tag),
        X(x),
        Y(y),
        CX(cx),
        CY(cy),
        ID(id){};
    void Deliver() const { ((TVPWindow*)GetSource())->OnTouchMove(X, Y, CX, CY, ID); }
};
//---------------------------------------------------------------------------
class tTVPOnTouchScalingInputEvent : public tTVPBaseInputEvent
{
    static tTVPUniqueTagForInputEvent Tag;
    tjs_real StartDistance;
    tjs_real CurrentDistance;
    tjs_real CX;
    tjs_real CY;
    tjs_int Flag;

public:
    tTVPOnTouchScalingInputEvent(TVPWindow* win,
                                 tjs_real startdist,
                                 tjs_real curdist,
                                 tjs_real cx,
                                 tjs_real cy,
                                 tjs_int flag)
      : tTVPBaseInputEvent(win, Tag),
        StartDistance(startdist),
        CurrentDistance(curdist),
        CX(cx),
        CY(cy),
        Flag(flag){};
    void Deliver() const
    {
        ((TVPWindow*)GetSource())->OnTouchScaling(StartDistance, CurrentDistance, CX, CY, Flag);
    }
};
//---------------------------------------------------------------------------
class tTVPOnTouchRotateInputEvent : public tTVPBaseInputEvent
{
    static tTVPUniqueTagForInputEvent Tag;
    tjs_real StartAngle;
    tjs_real CurrentAngle;
    tjs_real Distance;
    tjs_real CX;
    tjs_real CY;
    tjs_int Flag;

public:
    tTVPOnTouchRotateInputEvent(TVPWindow* win,
                                tjs_real startangle,
                                tjs_real curangle,
                                tjs_real dist,
                                tjs_real cx,
                                tjs_real cy,
                                tjs_int flag)
      : tTVPBaseInputEvent(win, Tag),
        StartAngle(startangle),
        CurrentAngle(curangle),
        Distance(dist),
        CX(cx),
        CY(cy),
        Flag(flag){};
    void Deliver() const
    {
        ((TVPWindow*)GetSource())
            ->OnTouchRotate(StartAngle, CurrentAngle, Distance, CX, CY, Flag);
    }
};
//---------------------------------------------------------------------------
class tTVPOnMultiTouchInputEvent : public tTVPBaseInputEvent
{
    static tTVPUniqueTagForInputEvent Tag;

public:
    tTVPOnMultiTouchInputEvent(TVPWindow* win) : tTVPBaseInputEvent(win, Tag) {};
    void Deliver() const { ((TVPWindow*)GetSource())->OnMultiTouch(); }
};
//---------------------------------------------------------------------------
class tTVPOnHintChangeInputEvent : public tTVPBaseInputEvent
{
    static tTVPUniqueTagForInputEvent Tag;
    ttstr HintMessage;
    tjs_int HintX;
    tjs_int HintY;
    bool IsShow;

public:
    tTVPOnHintChangeInputEvent(
        TVPWindow* win, const ttstr& text, tjs_int x, tjs_int y, bool isshow)
      : tTVPBaseInputEvent(win, Tag),
        HintMessage(text),
        HintX(x),
        HintY(y),
        IsShow(isshow){};

    void Deliver() const
    {
        ((TVPWindow*)GetSource())->OnHintChange(HintMessage, HintX, HintY, IsShow);
    }
};
//---------------------------------------------------------------------------
class tTVPOnDisplayRotateInputEvent : public tTVPBaseInputEvent
{
    static tTVPUniqueTagForInputEvent Tag;
    tjs_int Orientation;
    tjs_int Rotate;
    tjs_int BPP;
    tjs_int HorizontalResolution;
    tjs_int VerticalResolution;

public:
    tTVPOnDisplayRotateInputEvent(TVPWindow* win,
                                  tjs_int orientation,
                                  tjs_int rotate,
                                  tjs_int bpp,
                                  tjs_int hresolution,
                                  tjs_int vresolution)
      : tTVPBaseInputEvent(win, Tag),
        Orientation(orientation),
        Rotate(rotate),
        BPP(bpp),
        HorizontalResolution(hresolution),
        VerticalResolution(vresolution){};
    void Deliver() const
    {
        ((TVPWindow*)GetSource())
            ->OnDisplayRotate(Orientation, Rotate, BPP, HorizontalResolution, VerticalResolution);
    }
};
