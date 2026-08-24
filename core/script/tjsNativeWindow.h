#pragma once

#include "tjsNative.h"
#include "TVPWindow.h"

//---------------------------------------------------------------------------
// tTJSNI_Window : Window Native Instance
//---------------------------------------------------------------------------
class tTVPBaseBitmap;
class tTJSNI_BaseVideoOverlay;
class tTJSNI_Window : public tTJSNativeInstance
{
    typedef tTJSNativeInstance inherited;

private:
    std::vector<tTJSVariantClosure> ObjectVector;
    bool ObjectVectorLocked = false;

protected:
    TVPWindow* Window = nullptr;
    tObjectList<tTJSNI_BaseVideoOverlay> VideoOverlay;

public:
    tTJSNI_Window();
    ~tTJSNI_Window();
    tjs_error Construct(tjs_int numparams, tTJSVariant** param, iTJSDispatch2* tjs_obj) override;
    void Invalidate() override;

public:
    TVPWindow* GetWindow() const { return Window; }
    bool CanDeliverEvents() const { return Window && Window->GetVisible(); }
    iTJSDispatch2* GetOwnerNoAddRef() const
    {
        return Window ? Window->GetOwnerNoAddRef() : nullptr;
    }
    iTVPDrawDevice* GetDrawDevice() const
    {
        return Window ? Window->GetDrawDevice() : nullptr;
    }
    const tTJSVariant& GetDrawDeviceObject() const { return Window->GetDrawDeviceObject(); }
    void SetDrawDeviceObject(const tTJSVariant& val) { Window->SetDrawDeviceObject(val); }

    //----- 脚本侧附属对象管理
    void Add(tTJSVariantClosure clo);
    void Remove(tTJSVariantClosure clo);

    //----- interface to video overlay object（脚本侧列表）
    void RegisterVideoOverlayObject(tTJSNI_BaseVideoOverlay* ovl);
    void UnregisterVideoOverlayObject(tTJSNI_BaseVideoOverlay* ovl);
    void ReadjustVideoRect();
    void WindowMoved();
    void DetachVideoOverlay();

    //----- 事件投递（脚本 API）
    void PostInputEvent(const ttstr& name, iTJSDispatch2* params);

    //----- 转发到 TVPWindow（TJS 属性/方法）
    void ResetDrawDevice() { Window->ResetDrawDevice(); }
    void GetVideoOffset(tjs_int& ofsx, tjs_int& ofsy) { Window->GetVideoOffset(ofsx, ofsy); }
    void ZoomRectangle(tjs_int& l, tjs_int& t, tjs_int& r, tjs_int& b)
    {
        Window->ZoomRectangle(l, t, r, b);
    }
    void Close() { Window->Close(); }
    void OnCloseQueryCalled(bool b) { Window->OnCloseQueryCalled(b); }
    void BringToFront() { Window->BringToFront(); }
    void Update(tTVPUpdateType type) { Window->UpdateWindow(type); }
    void ShowModal() { Window->ShowWindowAsModal(); }
    void TickBeat() { Window->TickBeat(); }
    void NotifyWindowClose() { Window->NotifyWindowClose(); }
    void SendCloseMessage() {}
    bool GetWindowActive() { return Window->GetWindowActive(); }
    bool IsMainWindow() const { return Window->IsMainWindow(); }

    void SetVisible(bool s) { Window->SetVisible(s); }
    bool GetVisible() const { return Window->GetVisible(); }
    void GetCaption(ttstr& v) const
    {
        if (Window)
            v = Window->GetCaption();
        else
            v.Clear();
    }
    void SetCaption(const ttstr& v) { Window->SetCaption(v.AsStdString()); }

    void SetWidth(tjs_int w) { Window->SetWidth(w); }
    tjs_int GetWidth() const { return Window ? Window->GetWidth() : 0; }
    void SetHeight(tjs_int h) { Window->SetHeight(h); }
    tjs_int GetHeight() const { return Window ? Window->GetHeight() : 0; }
    void SetSize(tjs_int w, tjs_int h) { Window->SetSize(w, h); }

    void SetMinWidth(int v) { Window->SetMinWidth(v); }
    int GetMinWidth() const { return Window->GetMinWidth(); }
    void SetMinHeight(int v) { Window->SetMinHeight(v); }
    int GetMinHeight() const { return Window->GetMinHeight(); }
    void SetMinSize(int w, int h) { Window->SetMinSize(w, h); }
    void SetMaxWidth(int v) { Window->SetMaxWidth(v); }
    int GetMaxWidth() const { return Window->GetMaxWidth(); }
    void SetMaxHeight(int v) { Window->SetMaxHeight(v); }
    int GetMaxHeight() const { return Window->GetMaxHeight(); }
    void SetMaxSize(int w, int h) { Window->SetMaxSize(w, h); }

    void SetLeft(tjs_int l) { Window->SetLeft(l); }
    tjs_int GetLeft() const { return Window->GetLeft(); }
    void SetTop(tjs_int t) { Window->SetTop(t); }
    tjs_int GetTop() const { return Window->GetTop(); }
    void SetPosition(tjs_int l, tjs_int t) { Window->SetPosition(l, t); }

#ifdef USE_OBSOLETE_FUNCTIONS
    void SetLayerLeft(tjs_int l) { Window->SetLayerLeft(l); }
    tjs_int GetLayerLeft() const { return Window->GetLayerLeft(); }
    void SetLayerTop(tjs_int t) { Window->SetLayerTop(t); }
    tjs_int GetLayerTop() const { return Window->GetLayerTop(); }
    void SetLayerPosition(tjs_int l, tjs_int t) { Window->SetLayerPosition(l, t); }
    void SetInnerSunken(bool b) { Window->SetInnerSunken(b); }
    bool GetInnerSunken() const { return Window->GetInnerSunken(); }
    void BeginMove() { Window->BeginMove(); }
#endif

    void SetInnerWidth(tjs_int w) { Window->SetInnerWidth(w); }
    tjs_int GetInnerWidth() const { return Window->GetInnerWidth(); }
    void SetInnerHeight(tjs_int h) { Window->SetInnerHeight(h); }
    tjs_int GetInnerHeight() const { return Window->GetInnerHeight(); }
    void SetInnerSize(tjs_int w, tjs_int h) { Window->SetInnerSize(w, h); }

    void SetBorderStyle(tTVPBorderStyle st) { Window->SetBorderStyle(st); }
    tTVPBorderStyle GetBorderStyle() const { return Window->GetBorderStyle(); }
    void SetStayOnTop(bool b) { Window->SetStayOnTop(b); }
    bool GetStayOnTop() const { return Window->GetStayOnTop(); }

#ifdef USE_OBSOLETE_FUNCTIONS
    void SetShowScrollBars(bool b) { Window->SetShowScrollBars(b); }
    bool GetShowScrollBars() const { return Window->GetShowScrollBars(); }
#endif

    void SetFullScreen(bool b) { Window->SetFullScreenMode(b); }
    bool GetFullScreen() const { return Window->GetFullScreenMode(); }
    void SetUseMouseKey(bool b) { Window->SetUseMouseKey(b); }
    bool GetUseMouseKey() const { return Window->GetUseMouseKey(); }
    void SetTrapKey(bool b) { Window->SetTrapKey(b); }
    bool GetTrapKey() const { return Window->GetTrapKey(); }

    void SetMaskRegion(tjs_int threshold);
    void RemoveMaskRegion() { Window->RemoveMaskRegion(); }

    void SetMouseCursorState(tTVPMouseCursorState mcs) { Window->SetMouseCursorState(mcs); }
    tTVPMouseCursorState GetMouseCursorState() const { return Window->GetMouseCursorState(); }
    void SetFocusable(bool b) { Window->SetFocusable(b); }
    bool GetFocusable() { return Window->GetFocusable(); }

    void SetZoom(tjs_int numer, tjs_int denom) { Window->SetZoom(numer, denom); }
    void SetZoomNumer(tjs_int n) { Window->SetZoomNumer(n); }
    tjs_int GetZoomNumer() const { return Window->GetZoomNumer(); }
    void SetZoomDenom(tjs_int n) { Window->SetZoomDenom(n); }
    tjs_int GetZoomDenom() const { return Window->GetZoomDenom(); }

    void SetTouchScaleThreshold(tjs_real threshold) { Window->SetTouchScaleThreshold(threshold); }
    tjs_real GetTouchScaleThreshold() const { return Window->GetTouchScaleThreshold(); }
    void SetTouchRotateThreshold(tjs_real threshold)
    {
        Window->SetTouchRotateThreshold(threshold);
    }
    tjs_real GetTouchRotateThreshold() const { return Window->GetTouchRotateThreshold(); }

    tjs_real GetTouchPointStartX(tjs_int index) { return Window->GetTouchPointStartX(index); }
    tjs_real GetTouchPointStartY(tjs_int index) { return Window->GetTouchPointStartY(index); }
    tjs_real GetTouchPointX(tjs_int index) { return Window->GetTouchPointX(index); }
    tjs_real GetTouchPointY(tjs_int index) { return Window->GetTouchPointY(index); }
    tjs_real GetTouchPointID(tjs_int index) { return Window->GetTouchPointID(index); }
    tjs_int GetTouchPointCount() { return Window->GetTouchPointCount(); }
    bool GetTouchVelocity(tjs_int id, float& x, float& y, float& speed) const
    {
        return Window->GetTouchVelocity(id, x, y, speed);
    }
    bool GetMouseVelocity(float& x, float& y, float& speed) const
    {
        return Window->GetMouseVelocity(x, y, speed);
    }
    void ResetMouseVelocity() { Window->ResetMouseVelocity(); }

    void SetHintDelay(tjs_int delay) { Window->SetHintDelay(delay); }
    tjs_int GetHintDelay() const { return Window->GetHintDelay(); }

    void SetEnableTouch(bool b) { Window->SetEnableTouch(b); }
    bool GetEnableTouch() const { return Window->GetEnableTouch(); }

    int GetDisplayOrientation() { return Window->GetDisplayOrientation(); }
    int GetDisplayRotate() { return Window->GetDisplayRotate(); }

    void SetWaitVSync(bool enable) { Window->SetWaitVSync(enable); }
    bool GetWaitVSync() const { return Window->GetWaitVSync(); }

    void RegisterWindowMessageReceiver(tTVPWMRRegMode mode, void* proc, const void* userdata)
    {
        Window->RegisterWindowMessageReceiver(mode, proc, userdata);
    }

    void HideMouseCursor() { Window->HideMouseCursor(); }

    void SetDefaultImeMode(tTVPImeMode mode) { Window->SetDefaultImeMode(mode); }
    tTVPImeMode GetDefaultImeMode() const { return Window->GetDefaultImeMode(); }
    void SetImeMode(tTVPImeMode mode) { Window->SetImeMode(mode); }
    void ResetImeMode() { Window->ResetImeMode(); }
};
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTJSNC_Window : TJS Window class
//---------------------------------------------------------------------------
class tTJSNC_Window : public tTJSNativeClass
{
public:
    tTJSNC_Window();
    static tjs_uint32 ClassID;

protected:
    tTJSNativeInstance* CreateNativeInstance();
};
//---------------------------------------------------------------------------
extern tTJSNativeClass* TVPCreateNativeClass_Window();
//---------------------------------------------------------------------------
