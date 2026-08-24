//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// TVPWindow : engine-side window core
//---------------------------------------------------------------------------

#include "tjsCommHead.h"

#include <algorithm>
#include "TVPWindow.h"
#include "WindowManager.h"
#include "TVPMsg.h"
#include "TVPDebug.h"
#include "TVPEvent.h"
#include "TVPSystem.h"
#include "TVPApplication.h"
#include "LayerManager.h"
#include "Random.h"

#include "Platform.h"
#include "PlatformThread.h"
#include "PlatformView.h"

#include "TVPCompositor.h"

//---------------------------------------------------------------------------
// Input Events（各输入事件 tag）
//---------------------------------------------------------------------------
tTVPUniqueTagForInputEvent tTVPOnCloseInputEvent ::Tag;
tTVPUniqueTagForInputEvent tTVPOnResizeInputEvent ::Tag;
tTVPUniqueTagForInputEvent tTVPOnClickInputEvent ::Tag;
tTVPUniqueTagForInputEvent tTVPOnDoubleClickInputEvent ::Tag;
tTVPUniqueTagForInputEvent tTVPOnMouseDownInputEvent ::Tag;
tTVPUniqueTagForInputEvent tTVPOnMouseUpInputEvent ::Tag;
tTVPUniqueTagForInputEvent tTVPOnMouseMoveInputEvent ::Tag;
tTVPUniqueTagForInputEvent tTVPOnReleaseCaptureInputEvent ::Tag;
tTVPUniqueTagForInputEvent tTVPOnMouseOutOfWindowInputEvent ::Tag;
tTVPUniqueTagForInputEvent tTVPOnMouseEnterInputEvent ::Tag;
tTVPUniqueTagForInputEvent tTVPOnMouseLeaveInputEvent ::Tag;
tTVPUniqueTagForInputEvent tTVPOnKeyDownInputEvent ::Tag;
tTVPUniqueTagForInputEvent tTVPOnKeyUpInputEvent ::Tag;
tTVPUniqueTagForInputEvent tTVPOnKeyPressInputEvent ::Tag;
tTVPUniqueTagForInputEvent tTVPOnFileDropInputEvent ::Tag;
tTVPUniqueTagForInputEvent tTVPOnMouseWheelInputEvent ::Tag;
tTVPUniqueTagForInputEvent tTVPOnPopupHideInputEvent ::Tag;
tTVPUniqueTagForInputEvent tTVPOnWindowActivateEvent ::Tag;
tTVPUniqueTagForInputEvent tTVPOnTouchDownInputEvent ::Tag;
tTVPUniqueTagForInputEvent tTVPOnTouchUpInputEvent ::Tag;
tTVPUniqueTagForInputEvent tTVPOnTouchMoveInputEvent ::Tag;
tTVPUniqueTagForInputEvent tTVPOnTouchScalingInputEvent ::Tag;
tTVPUniqueTagForInputEvent tTVPOnTouchRotateInputEvent ::Tag;
tTVPUniqueTagForInputEvent tTVPOnMultiTouchInputEvent ::Tag;
tTVPUniqueTagForInputEvent tTVPOnHintChangeInputEvent ::Tag;
tTVPUniqueTagForInputEvent tTVPOnDisplayRotateInputEvent ::Tag;
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPWindow
//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
enum
{
    mrOk,
    mrAbort,
    mrCancel,
};

TVPWindow::TVPWindow()
{
    TVPRegisterWindowToList(this);

    // 创建 sprite（尺寸先取窗口逻辑尺寸，无效时退回平台窗口尺寸）
    int w = LayerWidth, h = LayerHeight;
    if (w <= 0 || h <= 0)
        TVPGetWindowSize(&w, &h);
    pSprite = new TVPSprite;
    pSprite->width = w;
    pSprite->height = h;
    krkrsdl3::TVPCreateTexture(*pSprite);
    krkrsdl3::TVPJoinTexture(pSprite);
}
//---------------------------------------------------------------------------
TVPWindow::~TVPWindow()
{
    TVPUnregisterWindowToList(this);

    // 解除绘制设备对本窗口的引用（D3D 等析构时不再触碰本窗口；
    // 设备只经 SetDrawDeviceObject 被引用，析构必然先于本窗口发生）
    if (DrawDevice)
        DrawDevice->SetWindowInterface(nullptr);

    if (pSprite != NULL)
    {
        if (pSprite->texture != nullptr)
        {
            krkrsdl3::TVPDepartTexture(pSprite);
            krkrsdl3::TVPDestroyTexture(pSprite);
        }
        delete pSprite;
        pSprite = nullptr;
    }
}
//---------------------------------------------------------------------------
bool TVPWindow::IsMainWindow() const
{
    return TVPMainWindow == this;
}
//---------------------------------------------------------------------------
void TVPWindow::SetDrawDeviceObject(const tTJSVariant& val)
{
    // invalidate existing draw device
    if (DrawDeviceObject.Type() == tvtObject)
        DrawDeviceObject.AsObjectClosureNoAddRef().Invalidate(0, NULL, NULL,
                                                              DrawDeviceObject.AsObjectNoAddRef());

    // assign new device
    DrawDeviceObject = val;
    DrawDevice = NULL;

    // extract interface
    if (DrawDeviceObject.Type() == tvtObject)
    {
        tTJSVariantClosure clo = DrawDeviceObject.AsObjectClosureNoAddRef();
        tTJSVariant iface_v;
        if (TJS_FAILED(clo.PropGet(0, TJS_N("interface"), NULL, &iface_v, NULL)))
            TVPThrowExceptionMessage(TVPCannotRetriveInterfaceFromDrawDevice);
        DrawDevice = reinterpret_cast<iTVPDrawDevice*>((tjs_intptr_t)(tjs_int64)iface_v);
        DrawDevice->SetWindowInterface(this);
        ResetDrawDevice();
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// input event posting
//---------------------------------------------------------------------------
void TVPWindow::PostKeyDown(tjs_uint16 key, tjs_uint32 shift)
{
    tjs_uint64 tick = TVPGetRoughTickCount();
    TVPPushEnvironNoise(&tick, sizeof(tick));
    TVPPushEnvironNoise(&key, sizeof(key));
    TVPPushEnvironNoise(&shift, sizeof(shift));
    TVPPostInputEvent(new tTVPOnKeyDownInputEvent(this, key, shift));
}
//---------------------------------------------------------------------------
void TVPWindow::PostKeyPress(tjs_uint16 key)
{
    if (UseMouseKey && (key == 0x1b || key == 13 || key == 32))
        return;
    TVPPostInputEvent(new tTVPOnKeyPressInputEvent(this, key));
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// input event reception（输入事件队列投递，引擎侧处理 + 触发 TJS 事件）
//---------------------------------------------------------------------------
void TVPWindow::OnClose()
{
    if (!GetVisible())
        return;
    if (Owner)
    {
        tTJSVariant arg[1] = {true};
        static ttstr eventname(TJS_N("onCloseQuery"));
        TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 1, arg);
    }
}
//---------------------------------------------------------------------------
void TVPWindow::OnResize()
{
    if (!GetVisible())
        return;
    if (Owner)
    {
        static ttstr eventname(TJS_N("onResize"));
        TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 0, NULL);
    }
}
//---------------------------------------------------------------------------
void TVPWindow::OnClick(tjs_int x, tjs_int y)
{
    if (!GetVisible())
        return;
    LastMouseX = x;
    LastMouseY = y;
    if (Owner)
    {
        tTJSVariant arg[2] = {x, y};
        static ttstr eventname(TJS_N("onClick"));
        TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 2, arg);
    }
    if (DrawDevice)
        DrawDevice->OnClick(x, y);
}
//---------------------------------------------------------------------------
void TVPWindow::OnDoubleClick(tjs_int x, tjs_int y)
{
    if (!GetVisible())
        return;
    LastMouseX = x;
    LastMouseY = y;
    if (Owner)
    {
        tTJSVariant arg[2] = {x, y};
        static ttstr eventname(TJS_N("onDoubleClick"));
        TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 2, arg);
    }
    if (DrawDevice)
        DrawDevice->OnDoubleClick(x, y);
}
//---------------------------------------------------------------------------
void TVPWindow::OnMouseDown(tjs_int x, tjs_int y, tTVPMouseButton mb, tjs_uint32 flags)
{
    if (!GetVisible())
        return;
    LastMouseX = x;
    LastMouseY = y;
    if (Owner)
    {
        tTJSVariant arg[4] = {x, y, (tjs_int64)mb, (tjs_int64)flags};
        static ttstr eventname(TJS_N("onMouseDown"));
        TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 4, arg);
    }
    if (DrawDevice)
        DrawDevice->OnMouseDown(x, y, mb, flags);
}
//---------------------------------------------------------------------------
void TVPWindow::OnMouseUp(tjs_int x, tjs_int y, tTVPMouseButton mb, tjs_uint32 flags)
{
    if (!GetVisible())
        return;
    LastMouseX = x;
    LastMouseY = y;
    if (Owner)
    {
        tTJSVariant arg[4] = {x, y, (tjs_int)mb, (tjs_int)flags};
        static ttstr eventname(TJS_N("onMouseUp"));
        TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 4, arg);
    }
    if (DrawDevice)
        DrawDevice->OnMouseUp(x, y, mb, flags);
}
//---------------------------------------------------------------------------
void TVPWindow::OnMouseMove(tjs_int x, tjs_int y, tjs_uint32 flags)
{
    if (!GetVisible())
        return;
    LastMouseX = x;
    LastMouseY = y;
    if (Owner)
    {
        static ttstr eventname(TJS_N("onMouseMove"));
        tTJSVariant arg[3] = {x, y, (tjs_int64)flags};
        TVPPostEvent(Owner, Owner, eventname, 0,
                     TVP_EPT_DISCARDABLE | TVP_EPT_IMMEDIATE
                     /*discardable!!*/,
                     3, arg);
    }
    if (DrawDevice)
        DrawDevice->OnMouseMove(x, y, flags);
}
//---------------------------------------------------------------------------
void TVPWindow::OnTouchDown(tjs_real x, tjs_real y, tjs_real cx, tjs_real cy, tjs_uint32 id)
{
    if (!GetVisible())
        return;
    if (Owner)
    {
        tTJSVariant arg[5] = {x, y, cx, cy, (tjs_int64)id};
        static ttstr eventname(TJS_N("onTouchDown"));
        TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 5, arg);
    }
    if (DrawDevice)
        DrawDevice->OnTouchDown(x, y, cx, cy, id);
}
//---------------------------------------------------------------------------
void TVPWindow::OnTouchUp(tjs_real x, tjs_real y, tjs_real cx, tjs_real cy, tjs_uint32 id)
{
    if (!GetVisible())
        return;
    if (Owner)
    {
        tTJSVariant arg[5] = {x, y, cx, cy, (tjs_int64)id};
        static ttstr eventname(TJS_N("onTouchUp"));
        TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 5, arg);
    }
    if (DrawDevice)
        DrawDevice->OnTouchUp(x, y, cx, cy, id);
    ResetTouchVelocity(id);
}
//---------------------------------------------------------------------------
void TVPWindow::OnTouchMove(tjs_real x, tjs_real y, tjs_real cx, tjs_real cy, tjs_uint32 id)
{
    if (!GetVisible())
        return;
    if (Owner)
    {
        tTJSVariant arg[5] = {x, y, cx, cy, (tjs_int64)id};
        static ttstr eventname(TJS_N("onTouchMove"));
        TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 5, arg);
    }
    if (DrawDevice)
        DrawDevice->OnTouchMove(x, y, cx, cy, id);
}
//---------------------------------------------------------------------------
void TVPWindow::OnTouchScaling(
    tjs_real startdist, tjs_real curdist, tjs_real cx, tjs_real cy, tjs_int flag)
{
    if (!GetVisible())
        return;
    if (Owner)
    {
        tTJSVariant arg[5] = {startdist, curdist, cx, cy, flag};
        static ttstr eventname(TJS_N("onTouchScaling"));
        TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 5, arg);
    }
    if (DrawDevice)
        DrawDevice->OnTouchScaling(startdist, curdist, cx, cy, flag);
}
//---------------------------------------------------------------------------
void TVPWindow::OnTouchRotate(
    tjs_real startangle, tjs_real curangle, tjs_real dist, tjs_real cx, tjs_real cy, tjs_int flag)
{
    if (!GetVisible())
        return;
    if (Owner)
    {
        tTJSVariant arg[6] = {startangle, curangle, dist, cx, cy, flag};
        static ttstr eventname(TJS_N("onTouchRotate"));
        TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 6, arg);
    }
    if (DrawDevice)
        DrawDevice->OnTouchRotate(startangle, curangle, dist, cx, cy, flag);
}
//---------------------------------------------------------------------------
void TVPWindow::OnMultiTouch()
{
    if (!GetVisible())
        return;
    if (Owner)
    {
        static ttstr eventname(TJS_N("onMultiTouch"));
        TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 0, NULL);
    }
    if (DrawDevice)
        DrawDevice->OnMultiTouch();
}
//---------------------------------------------------------------------------
void TVPWindow::OnReleaseCapture()
{
    if (DrawDevice)
        DrawDevice->OnReleaseCapture();
}
//---------------------------------------------------------------------------
void TVPWindow::OnMouseOutOfWindow()
{
    if (DrawDevice)
        DrawDevice->OnMouseOutOfWindow();
}
//---------------------------------------------------------------------------
void TVPWindow::OnMouseEnter()
{
    if (!GetVisible())
        return;
    if (Owner)
    {
        static ttstr eventname(TJS_N("onMouseEnter"));
        TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 0, NULL);
    }
}
//---------------------------------------------------------------------------
void TVPWindow::OnMouseLeave()
{
    if (!GetVisible())
        return;
    if (Owner)
    {
        static ttstr eventname(TJS_N("onMouseLeave"));
        TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 0, NULL);
    }
}
//---------------------------------------------------------------------------
void TVPWindow::OnKeyDown(tjs_uint key, tjs_uint32 shift)
{
    if (!GetVisible())
        return;
    if (Owner)
    {
        tTJSVariant arg[2] = {(tjs_int)key, (tjs_int)shift};
        static ttstr eventname(TJS_N("onKeyDown"));
        TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 2, arg);
    }
    if (DrawDevice)
        DrawDevice->OnKeyDown(key, shift);
}
//---------------------------------------------------------------------------
void TVPWindow::OnKeyUp(tjs_uint key, tjs_uint32 shift)
{
    if (!GetVisible())
        return;
    if (Owner)
    {
        tTJSVariant arg[2] = {(tjs_int)key, (tjs_int)shift};
        static ttstr eventname(TJS_N("onKeyUp"));
        TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 2, arg);
    }
    if (DrawDevice)
        DrawDevice->OnKeyUp(key, shift);
}
//---------------------------------------------------------------------------
void TVPWindow::OnKeyPress(tjs_uint16 key)
{
    if (!GetVisible())
        return;
    if (Owner)
    {
        tjs_char buf[5];
        TJS_unicode_to_utf8(key, buf);
        tTJSVariant arg[1] = {buf};
        static ttstr eventname(TJS_N("onKeyPress"));
        TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 1, arg);
    }
    if (DrawDevice)
        DrawDevice->OnKeyPress(key);
}
//---------------------------------------------------------------------------
void TVPWindow::OnFileDrop(const tTJSVariant& array)
{
    if (!GetVisible())
        return;
    if (Owner)
    {
        tTJSVariant arg[1] = {array};
        static ttstr eventname(TJS_N("onFileDrop"));
        TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 1, arg);
    }
}
//---------------------------------------------------------------------------
void TVPWindow::OnMouseWheel(tjs_uint32 shift, tjs_int delta, tjs_int x, tjs_int y)
{
    if (!GetVisible())
        return;
    if (Owner)
    {
        tTJSVariant arg[4] = {(tjs_int)shift, delta, x, y};
        static ttstr eventname(TJS_N("onMouseWheel"));
        TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 4, arg);
    }
    if (DrawDevice)
        DrawDevice->OnMouseWheel(shift, delta, x, y);
}
//---------------------------------------------------------------------------
void TVPWindow::OnPopupHide()
{
    if (!GetVisible())
        return;
    if (Owner)
    {
        static ttstr eventname(TJS_N("onPopupHide"));
        TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 0, NULL);
    }
}
//---------------------------------------------------------------------------
void TVPWindow::OnActivate(bool activate_or_deactivate)
{
    if (!GetVisible())
        return;

    // re-check the window activate state
    if (GetWindowActive() == activate_or_deactivate)
    {
        if (Owner)
        {
            static ttstr a_eventname(TJS_N("onActivate"));
            static ttstr d_eventname(TJS_N("onDeactivate"));
            TVPPostEvent(Owner, Owner, activate_or_deactivate ? a_eventname : d_eventname, 0,
                         TVP_EPT_IMMEDIATE, 0, NULL);
        }
    }
}
//---------------------------------------------------------------------------
void TVPWindow::OnHintChange(const ttstr& text, tjs_int x, tjs_int y, bool isshow)
{
    if (!GetVisible())
        return;
    if (Owner)
    {
        tTJSVariant arg[4] = {text, x, y, isshow ? 1 : 0};
        static ttstr eventname(TJS_N("onHintChanged"));
        TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 4, arg);
    }
}
//---------------------------------------------------------------------------
void TVPWindow::OnDisplayRotate(
    tjs_int orientation, tjs_int rotate, tjs_int bpp, tjs_int hresolution, tjs_int vresolution)
{
    if (!GetVisible())
        return;
    if (Owner)
    {
        tTJSVariant arg[5] = {orientation, rotate, bpp, hresolution, vresolution};
        static ttstr eventname(TJS_N("onDisplayRotate"));
        TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 5, arg);
    }
    if (DrawDevice)
        DrawDevice->OnDisplayRotate(orientation, rotate, bpp, hresolution, vresolution);
}
//---------------------------------------------------------------------------
void TVPWindow::PostReleaseCaptureEvent()
{
    TVPPostInputEvent(new tTVPOnReleaseCaptureInputEvent(this));
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// paint box / geometry
//---------------------------------------------------------------------------
static void AdjustNumerAndDenom(tjs_int& n, tjs_int& d)
{
    tjs_int a = n;
    tjs_int b = d;
    while (b)
    {
        tjs_int t = b;
        b = a % b;
        a = t;
    }
    n = n / a;
    d = d / a;
}
//---------------------------------------------------------------------------
void TVPWindow::ResetDrawSprite()
{
    if (pSprite->width != LayerWidth || pSprite->height != LayerHeight)
    {
        krkrsdl3::TVPDestroyTexture(pSprite);
        pSprite->width = LayerWidth;
        pSprite->height = LayerHeight;
        krkrsdl3::TVPCreateTexture(*pSprite);
    }
    pSprite->width = LayerWidth;
    pSprite->height = LayerHeight;
    if (TVPIsFirstWindow(this))
    {
        TVPSetWindowSize(LayerWidth, LayerHeight);
    }
}
//---------------------------------------------------------------------------
void TVPWindow::RecalcPaintBox()
{
    if (!LayerWidth || !LayerHeight)
        return;
    ResetDrawSprite();
}
//---------------------------------------------------------------------------
void TVPWindow::SetPaintBoxSize(tjs_int w, tjs_int h)
{
    LayerWidth = w;
    LayerHeight = h;
    RecalcPaintBox();
}
//---------------------------------------------------------------------------
void TVPWindow::SetWidth(tjs_int w)
{
    LayerWidth = w;
    RecalcPaintBox();
}
//---------------------------------------------------------------------------
void TVPWindow::SetHeight(tjs_int h)
{
    LayerHeight = h;
    RecalcPaintBox();
}
//---------------------------------------------------------------------------
void TVPWindow::SetSize(tjs_int w, tjs_int h)
{
    SetPaintBoxSize(w, h);
}
//---------------------------------------------------------------------------
void TVPWindow::GetSize(tjs_int& w, tjs_int& h)
{
    GetWinSize(w, h);
}
//---------------------------------------------------------------------------
void TVPWindow::GetWinSize(tjs_int& w, tjs_int& h)
{
    TVPGetWindowSize(&w, &h);
}
//---------------------------------------------------------------------------
void TVPWindow::SetPosition(tjs_int x, tjs_int y)
{
    pSprite->xPos = x;
    pSprite->yPos = y;
}
//---------------------------------------------------------------------------
void TVPWindow::SetZoom(tjs_int numer, tjs_int denom)
{
    AdjustNumerAndDenom(numer, denom);
    ZoomNumer = numer;
    ZoomDenom = denom;
    ActualZoomDenom = denom;
    ActualZoomNumer = numer;
    RecalcPaintBox();
}
//---------------------------------------------------------------------------
void TVPWindow::ZoomRectangle(tjs_int& left, tjs_int& top, tjs_int& right, tjs_int& bottom)
{
    left = tjs_int64(left) * ActualZoomNumer / ActualZoomDenom;
    top = tjs_int64(top) * ActualZoomNumer / ActualZoomDenom;
    right = tjs_int64(right) * ActualZoomNumer / ActualZoomDenom;
    bottom = tjs_int64(bottom) * ActualZoomNumer / ActualZoomDenom;
}
//---------------------------------------------------------------------------
void TVPWindow::PresentTexture(void* texture, tjs_int w, tjs_int h)
{
    if (!texture)
        return;
    krkrsdl3::iTVPRenderBackend* backend = krkrsdl3::TVPGetRenderBackend();
    if (!backend)
        return;

    if (!backend->IsHardware())
    {
        // 软件后端：compositor 贴图即 CPU 缓冲，读取零拷贝 → sprite 窗口贴图上传（SW 保底路径）
        if (pSprite->borrowedTexture)
        {
            // 此前借用了 GPU 纹理，恢复为 sprite 自有纹理
            pSprite->texture = nullptr; // 借用的句柄不销毁
            pSprite->borrowedTexture = false;
            krkrsdl3::TVPCreateTexture(*pSprite);
        }
        if (pSprite->texture == nullptr)
            return;

        // 同步窗口内容尺寸（paint box / sprite / 首窗口平台尺寸），
        // 与软渲染旧路径一致：内容尺寸变化时重建窗口贴图并调整平台窗口，
        // 否则呈现数据会超出窗口被裁剪
        if (pSprite->width != w || pSprite->height != h)
        {
            SetSize(w, h);
            if (pSprite->texture == nullptr) // SetSize 重建纹理失败保护
                return;
        }

        int pitch = 0;
        uint8_t* pixels = backend->LockTexture(texture, pitch);
        if (pixels)
            krkrsdl3::TVPUpdateTexture(pSprite, pixels, w, h, pitch);
        return;
    }

    // GPU 后端：sprite 窗口贴图别名该贴图（零拷贝；TVPRenderOnce 绘制之）。
    // 若 sprite 此前持有自有窗口贴图，销毁之（别名后不再需要）
    if (!pSprite->borrowedTexture && pSprite->texture != nullptr)
        krkrsdl3::TVPDestroyTexture(pSprite);
    pSprite->texture = texture;
    pSprite->width = w;
    pSprite->height = h;
    pSprite->borrowedTexture = true;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
void TVPWindow::ReleaseBorrowedTexture()
{
    // compositor 纹理（后端离屏目标）即将销毁：若 sprite 正借用其纹理，解除借用
    if (pSprite && pSprite->borrowedTexture)
    {
        pSprite->texture = nullptr;
        pSprite->borrowedTexture = false;
    }
}

//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// form state
//---------------------------------------------------------------------------
void TVPWindow::SetVisible(bool bVisible)
{
    pSprite->isVisible = bVisible;
    if (bVisible)
    {
        BringToFront();
    }
    else
    {
        TVPWindowHidden(this);
    }
}
//---------------------------------------------------------------------------
void TVPWindow::SetCaption(const std::string& s)
{
    TVPSetWindowTitle(s.c_str());
    Caption = s;
}
//---------------------------------------------------------------------------
void TVPWindow::SetFullScreenMode(bool isFull)
{
    if (TVPIsFirstWindow(this))
    {
        TVPSetWindowFullscreen(isFull);
        isFullScreen = isFull;
    }
}
//---------------------------------------------------------------------------
bool TVPWindow::GetWindowActive() const
{
    return TVPGetActiveWindow() == this;
}
//---------------------------------------------------------------------------
void TVPWindow::BringToFront()
{
    if (TVPGetActiveWindow() != this)
    {
        TVPWindow* cur = TVPGetActiveWindow();
        if (cur)
        {
            tjs_int w = 0, h = 0;
            cur->GetSize(w, h);
            cur->SetPosition(w, 0);
            cur->OnReleaseCapture();
        }
        TVPSetActiveWindow(this);
    }
}
//---------------------------------------------------------------------------
void TVPWindow::ShowWindowAsModal()
{
    pSprite->type = 1;
    in_mode_ = true;
    SetVisible(true);
    BringToFront();
    SetPosition(0, 0);

    modal_result_ = 0;
    while (GetWindowActive() && !modal_result_)
    {
        int remain = TVPDrawSceneOnce(30); // 30 fps
        if (::Application->IsTarminate())
        {
            modal_result_ = mrCancel;
        }
        else if (modal_result_ != 0)
        {
            break;
        }
        else if (remain > 0)
        {
            TVPSleepFor(remain);
        }
    }
    in_mode_ = false;
}
//---------------------------------------------------------------------------
void TVPWindow::TickBeat()
{
    bool focused = GetWindowActive();
    // mouse key
    if (UseMouseKey && focused)
    {
        // GenerateMouseEvent(false, false, false, false);
    }
}
//---------------------------------------------------------------------------
void TVPWindow::GetCursorPos(tjs_int& x, tjs_int& y)
{
    x = LastMouseX;
    y = LastMouseY;
}
//---------------------------------------------------------------------------
void TVPWindow::SetAttentionPoint(tTJSNI_BaseLayer* /*layer*/, tjs_int left, tjs_int top)
{
    TextInputPosY = top;
}
//---------------------------------------------------------------------------
void TVPWindow::SetImeMode(tTVPImeMode mode)
{
    LastSetImeMode = mode;
    switch (mode)
    {
        case ::imDisable:
        case ::imClose:
        {
            TVPHideIME();
            break;
        }
        case ::imOpen:
        default:
        {
            TVPShowIME(0, 0, 0, 0);
            break;
        }
    }
}
//---------------------------------------------------------------------------
void TVPWindow::SetUseMouseKey(bool b)
{
    UseMouseKey = b;
    if (b)
    {
        MouseLeftButtonEmulatedPushed = false;
        MouseRightButtonEmulatedPushed = false;
    }
    else
    {
        if (MouseLeftButtonEmulatedPushed)
        {
            MouseLeftButtonEmulatedPushed = false;
            MouseVelocityTracker.addMovement(TVPGetRoughTickCount(), (float)LastMouseX,
                                             (float)LastMouseY);
        }
        if (MouseRightButtonEmulatedPushed)
        {
            MouseRightButtonEmulatedPushed = false;
            MouseVelocityTracker.addMovement(TVPGetRoughTickCount(), (float)LastMouseX,
                                             (float)LastMouseY);
        }
    }
}
//---------------------------------------------------------------------------
bool TVPWindow::GetMouseVelocity(float& x, float& y, float& speed) const
{
    if (MouseVelocityTracker.getVelocity(x, y))
    {
        speed = hypotf(x, y);
        return true;
    }
    return false;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// close / modal
bool TVPWindow::OnCloseQuery()
{
    // closing actions are 3 patterns;
    // 1. closing action by the user
    // 2. "close" method
    // 3. object invalidation

    if (TVPGetBreathing())
    {
        return false;
    }

    // the default event handler will invalidate this object when
    // an onCloseQuery event reaches the handler.
    if (Owner &&
        (modal_result_ == 0 ||
         modal_result_ == mrCancel /* mrCancel=when close button is pushed in modal window */))
    {
        iTJSDispatch2* obj = Owner;
        if (obj)
        {
            tTJSVariant arg[1] = {true};
            static ttstr eventname(TJS_N("onCloseQuery"));

            if (!ProgramClosing)
            {
                // close action does not happen immediately
                TVPPostInputEvent(new tTVPOnCloseInputEvent(this));

                Closing = true; // waiting closing...
                return false;
            }
            else
            {
                CanCloseWork = true;
                TVPPostEvent(obj, obj, eventname, 0, TVP_EPT_IMMEDIATE, 1, arg);
                // this event happens immediately
                // and does not return until done
                return CanCloseWork; // CanCloseWork is set by the
                // event handler
            }
        }
        else
        {
            return true;
        }
    }
    else
    {
        return true;
    }
}
//---------------------------------------------------------------------------
bool TVPWindow::DoClose()
{
    // closing action by "close" method / close query accepted.
    // returns true if this window object has been deleted.

    if (ProgramClosing && Owner)
    {
        iTJSDispatch2* obj = Owner;
        // 先与脚本对象脱离，使脚本绑定在 Invalidate 时不再删除本窗口
        NotifyWindowClose();
        obj->Invalidate(0, nullptr, nullptr, obj);
        SetVisible(false);
        // 本窗口已不再被任何对象引用，自行销毁
        delete this;
        return true;
    }
    return false;
}
//---------------------------------------------------------------------------
void TVPWindow::Close()
{
    // closing action by "close" method
    if (Closing)
        return; // already waiting closing...

    ProgramClosing = true;
    bool deleted = false;
    try
    {
        if (in_mode_)
        {
            modal_result_ = mrCancel;
        }
        else if (OnCloseQuery())
        {
            deleted = DoClose();
        }
    }
    catch (...)
    {
        if (!deleted)
            ProgramClosing = false;
        throw;
    }
    if (!deleted)
        ProgramClosing = false;
}
//---------------------------------------------------------------------------
void TVPWindow::OnCloseQueryCalled(bool b)
{
    // closing is allowed by onCloseQuery event handler
    if (!ProgramClosing)
    {
        // closing action by the user
        if (b)
        {
            if (in_mode_)
                modal_result_ = 1; // when modal
            else
                SetVisible(false); // just hide

            Closing = false;
            if (IsMainWindow())
            {
                // this is the main window; invalidating the script object
                // deletes this window（其后不得再访问成员）
                iTJSDispatch2* obj = GetOwnerNoAddRef();
                if (obj)
                    obj->Invalidate(0, nullptr, nullptr, obj);
            }
            // no member access beyond this point
        }
        else
        {
            Closing = false;
        }
    }
    else
    {
        // closing action by the program
        CanCloseWork = b;
    }
}
//---------------------------------------------------------------------------
void TVPWindow::UpdateWindow(tTVPUpdateType type)
{
    tTVPRect r;
    r.left = 0;
    r.top = 0;
    r.right = LayerWidth;
    r.bottom = LayerHeight;
    NotifyWindowExposureToLayer(r);
    TVPDeliverWindowUpdateEvents();
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// update management
//---------------------------------------------------------------------------
void TVPWindow::NotifySrcResize()
{
    // is called from primary layer
    if (WindowUpdating)
        TVPThrowExceptionMessage(TVPInvalidMethodInUpdating);

    // ( or from the draw device to reset paint box's size )
    if (DrawDevice)
    {
        tjs_int w, h;
        DrawDevice->GetSrcSize(w, h);
        SetPaintBoxSize(w, h);
    }
}
//---------------------------------------------------------------------------
void TVPWindow::UpdateContent()
{
    if (DrawDevice)
    {
        // is called from event dispatcher
        DrawDevice->Update();

        if (!WaitVSync)
            DrawDevice->Show();

        EndUpdate();
    }
}
//---------------------------------------------------------------------------
void TVPWindow::DeliverDrawDeviceShow()
{
    // call DrawDevice->Show, at VBlank
    if (DrawDevice)
        DrawDevice->Show();
}
//---------------------------------------------------------------------------
void TVPWindow::NotifyWindowExposureToLayer(const tTVPRect& cliprect)
{
    if (DrawDevice)
        DrawDevice->RequestInvalidation(cliprect);
}
//---------------------------------------------------------------------------
void TVPWindow::DumpPrimaryLayerStructure()
{
    if (DrawDevice)
        DrawDevice->DumpLayerStructure();
}
//---------------------------------------------------------------------------
void TVPWindow::RecheckInputState()
{
    // slow timer tick (about 1 sec interval, inaccurate)
    if (DrawDevice)
        DrawDevice->RecheckInputState();
}
//---------------------------------------------------------------------------
void TVPWindow::SetShowUpdateRect(bool b)
{
    // show update rectangle if possible
    if (DrawDevice)
        DrawDevice->SetShowUpdateRect(b);
}
//---------------------------------------------------------------------------
bool TVPWindow::WaitForVBlank(tjs_int* in_vblank, tjs_int* delayed)
{
    if (DrawDevice)
        return DrawDevice->WaitForVBlank(in_vblank, delayed);
    return false;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// iTVPLayerTreeOwner（LayerManager -> LTO，全部转发给 DrawDevice）
//---------------------------------------------------------------------------
void TVPWindow::RegisterLayerManager(iTVPLayerManager* manager)
{
    if (DrawDevice)
        DrawDevice->AddLayerManager(manager);
}
//---------------------------------------------------------------------------
void TVPWindow::UnregisterLayerManager(iTVPLayerManager* manager)
{
    if (DrawDevice)
        DrawDevice->RemoveLayerManager(manager);
}
//---------------------------------------------------------------------------
void TVPWindow::StartBitmapCompletion(iTVPLayerManager* manager)
{
    if (DrawDevice)
        DrawDevice->StartBitmapCompletion(manager);
}
//---------------------------------------------------------------------------
void TVPWindow::NotifyBitmapCompleted(iTVPLayerManager* manager,
                                      tjs_int x,
                                      tjs_int y,
                                      tTVPBaseTexture* bmp,
                                      const tTVPRect& cliprect,
                                      tTVPLayerType type,
                                      tjs_int opacity)
{
    if (DrawDevice)
    {
        DrawDevice->NotifyBitmapCompleted(manager, x, y, bmp, cliprect, type, opacity);
    }
}
//---------------------------------------------------------------------------
void TVPWindow::EndBitmapCompletion(iTVPLayerManager* manager)
{
    if (DrawDevice)
        DrawDevice->EndBitmapCompletion(manager);
}
//---------------------------------------------------------------------------
void TVPWindow::SetMouseCursor(iTVPLayerManager* manager, tjs_int cursor)
{
    if (DrawDevice)
    {
        if (cursor == 0)
            DrawDevice->SetDefaultMouseCursor(manager);
        else
            DrawDevice->SetMouseCursor(manager, cursor);
    }
}
//---------------------------------------------------------------------------
void TVPWindow::GetCursorPos(iTVPLayerManager* manager, tjs_int& x, tjs_int& y)
{
    if (DrawDevice)
        DrawDevice->GetCursorPos(manager, x, y);
}
//---------------------------------------------------------------------------
void TVPWindow::SetCursorPos(iTVPLayerManager* manager, tjs_int x, tjs_int y)
{
    if (DrawDevice)
        DrawDevice->SetCursorPos(manager, x, y);
}
//---------------------------------------------------------------------------
void TVPWindow::ReleaseMouseCapture(iTVPLayerManager* manager)
{
    if (DrawDevice)
        DrawDevice->WindowReleaseCapture(manager);
}
//---------------------------------------------------------------------------
void TVPWindow::SetHint(iTVPLayerManager* manager, iTJSDispatch2* sender, const ttstr& hint)
{
    if (DrawDevice)
        DrawDevice->SetHintText(manager, sender, hint);
}
//---------------------------------------------------------------------------
void TVPWindow::NotifyLayerResize(iTVPLayerManager* manager)
{
    if (DrawDevice)
        DrawDevice->NotifyLayerResize(manager);
}
//---------------------------------------------------------------------------
void TVPWindow::NotifyLayerImageChange(iTVPLayerManager* manager)
{
    if (DrawDevice)
        DrawDevice->NotifyLayerImageChange(manager);
}
//---------------------------------------------------------------------------
void TVPWindow::SetAttentionPoint(iTVPLayerManager* manager,
                                  tTJSNI_BaseLayer* layer,
                                  tjs_int x,
                                  tjs_int y)
{
    if (DrawDevice)
        DrawDevice->SetAttentionPoint(manager, layer, x, y);
}
//---------------------------------------------------------------------------
void TVPWindow::DisableAttentionPoint(iTVPLayerManager* manager)
{
    if (DrawDevice)
        DrawDevice->DisableAttentionPoint(manager);
}
//---------------------------------------------------------------------------
void TVPWindow::SetImeMode(iTVPLayerManager* manager, tjs_int mode)
{
    if (DrawDevice)
        DrawDevice->SetImeMode(manager, (tTVPImeMode)mode);
}
//---------------------------------------------------------------------------
void TVPWindow::ResetImeMode(iTVPLayerManager* manager)
{
    if (DrawDevice)
        DrawDevice->ResetImeMode(manager);
}
//---------------------------------------------------------------------------
