//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
//!@file 描画デバイス管理
//---------------------------------------------------------------------------

#include "tjsCommHead.h"

#include "DrawDevice.h"
#include "TVPMsg.h"
#include "LayerManager.h"
#include "TVPWindow.h"
#include "TVPCompositor.h"
#include "TVPDebug.h"
#include "TVPSystem.h"
#include "TVPSettings.h"

//---------------------------------------------------------------------------
// オプション
//---------------------------------------------------------------------------
static tjs_int TVPBasicDrawDeviceOptionsGeneration = 0;
bool TVPZoomInterpolation = true;
//---------------------------------------------------------------------------
static void TVPInitBasicDrawDeviceOptions()
{
    if (TVPBasicDrawDeviceOptionsGeneration == 1)
        return;
    TVPBasicDrawDeviceOptionsGeneration = 1;

    tTJSVariant val;
    TVPZoomInterpolation = true;
    if (TVPGetCommandLine(TJS_N("-smoothzoom"), &val))
    {
        ttstr str(val);
        if (str == TJS_N("no"))
            TVPZoomInterpolation = false;
        else
            TVPZoomInterpolation = true;
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
tTVPBasicDrawDevice::tTVPBasicDrawDevice()
{
    TVPInitBasicDrawDeviceOptions(); // read and initialize options

    // コンストラクタ
    Window = NULL;
    Manager = NULL;
    DestRect.clear();
}
//---------------------------------------------------------------------------
tTVPBasicDrawDevice::~tTVPBasicDrawDevice()
{
    // すべての managers を開放する
    // TODO: プライマリレイヤ無効化、あるいはウィンドウ破棄時の終了処理が正しいか？
    // managers は 開放される際、自身の登録解除を行うために
    // RemoveLayerManager() を呼ぶかもしれないので注意。
    // そのため、ここではいったん配列をコピーしてからそれぞれの
    // Release() を呼ぶ。
    if (Manager != NULL)
    {
        Manager->Release();
        Manager = NULL;
    }
}

//---------------------------------------------------------------------------
void tTVPBasicDrawDevice::Destruct()
{
    // 解除窗口对呈现贴图的借用，并释放呈现转化用的 scratch 贴图
    if (Window)
        Window->ReleaseBorrowedTexture();
    krkrsdl3::iTVPRenderBackend* backend = krkrsdl3::TVPGetRenderBackend();
    if (backend && ScratchTexture)
        backend->DestroyTexture(ScratchTexture);
    delete this;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
void tTVPBasicDrawDevice::SetWindowInterface(TVPWindow* window)
{
    Window = window;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
void tTVPBasicDrawDevice::AddLayerManager(iTVPLayerManager* manager)
{
    if (Manager != NULL)
    {
        // "Basic" デバイスでは２つ以上のLayer Managerを登録できない
        TVPThrowExceptionMessage(TVPBasicDrawDeviceDoesNotSupporteLayerManagerMoreThanOne);
    }
    // Managers に manager を push する。AddRefするのを忘れないこと。
    Manager = manager;
    manager->AddRef();

    manager->SetDesiredLayerType(ltOpaque); // ltOpaque な出力を受け取りたい
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
void tTVPBasicDrawDevice::RemoveLayerManager(iTVPLayerManager* manager)
{
    // Managers から manager を削除する。Releaseする。
    if (Manager == NULL)
        TVPThrowInternalError;
    Manager->Release();
    Manager = NULL;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
void tTVPBasicDrawDevice::SetDestRectangle(const tTVPRect& rect)
{
    DestRect = rect;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
void tTVPBasicDrawDevice::SetClipRectangle(const tTVPRect& rect)
{
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
void tTVPBasicDrawDevice::GetSrcSize(tjs_int& w, tjs_int& h)
{
    if (!Manager)
        return;
    if (!Manager->GetPrimaryLayerSize(w, h))
    {
        w = 0;
        h = 0;
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
void tTVPBasicDrawDevice::NotifyLayerResize(iTVPLayerManager* manager)
{
    if (Manager == manager)
        Window->NotifySrcResize();
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
void tTVPBasicDrawDevice::NotifyLayerImageChange(iTVPLayerManager* manager)
{
    if (Manager == manager)
        Window->RequestUpdate();
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
void tTVPBasicDrawDevice::OnClick(tjs_int x, tjs_int y)
{
    if (!Manager)
        return;

    Manager->NotifyClick(x, y);
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
void tTVPBasicDrawDevice::OnDoubleClick(tjs_int x, tjs_int y)
{
    if (!Manager)
        return;

    Manager->NotifyDoubleClick(x, y);
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
void tTVPBasicDrawDevice::OnMouseDown(tjs_int x, tjs_int y, tTVPMouseButton mb, tjs_uint32 flags)
{
    if (!Manager)
        return;

    Manager->NotifyMouseDown(x, y, mb, flags);
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
void tTVPBasicDrawDevice::OnMouseUp(tjs_int x, tjs_int y, tTVPMouseButton mb, tjs_uint32 flags)
{
    if (!Manager)
        return;

    Manager->NotifyMouseUp(x, y, mb, flags);
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
void tTVPBasicDrawDevice::OnMouseMove(tjs_int x, tjs_int y, tjs_uint32 flags)
{
    if (!Manager)
        return;

    Manager->NotifyMouseMove(x, y, flags);
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
void tTVPBasicDrawDevice::OnReleaseCapture()
{
    if (!Manager)
        return;

    Manager->ReleaseCapture();
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
void tTVPBasicDrawDevice::OnMouseOutOfWindow()
{
    if (!Manager)
        return;

    Manager->NotifyMouseOutOfWindow();
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
void tTVPBasicDrawDevice::OnKeyDown(tjs_uint key, tjs_uint32 shift)
{
    if (!Manager)
        return;

    Manager->NotifyKeyDown(key, shift);
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
void tTVPBasicDrawDevice::OnKeyUp(tjs_uint key, tjs_uint32 shift)
{
    if (!Manager)
        return;

    Manager->NotifyKeyUp(key, shift);
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
void tTVPBasicDrawDevice::OnKeyPress(tjs_uint16 key)
{
    if (!Manager)
        return;

    Manager->NotifyKeyPress(key);
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
void tTVPBasicDrawDevice::OnMouseWheel(tjs_uint32 shift, tjs_int delta, tjs_int x, tjs_int y)
{
    if (!Manager)
        return;

    Manager->NotifyMouseWheel(shift, delta, x, y);
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
void tTVPBasicDrawDevice::OnTouchDown(
    tjs_real x, tjs_real y, tjs_real cx, tjs_real cy, tjs_uint32 id)
{
    if (!Manager)
        return;

    Manager->NotifyTouchDown(x, y, cx, cy, id);
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
void tTVPBasicDrawDevice::OnTouchUp(tjs_real x, tjs_real y, tjs_real cx, tjs_real cy, tjs_uint32 id)
{
    if (!Manager)
        return;

    Manager->NotifyTouchUp(x, y, cx, cy, id);
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
void tTVPBasicDrawDevice::OnTouchMove(
    tjs_real x, tjs_real y, tjs_real cx, tjs_real cy, tjs_uint32 id)
{
    if (!Manager)
        return;

    Manager->NotifyTouchMove(x, y, cx, cy, id);
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
void tTVPBasicDrawDevice::OnTouchScaling(
    tjs_real startdist, tjs_real curdist, tjs_real cx, tjs_real cy, tjs_int flag)
{
    if (!Manager)
        return;

    Manager->NotifyTouchScaling(startdist, curdist, cx, cy, flag);
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
void tTVPBasicDrawDevice::OnTouchRotate(
    tjs_real startangle, tjs_real curangle, tjs_real dist, tjs_real cx, tjs_real cy, tjs_int flag)
{
    if (!Manager)
        return;

    Manager->NotifyTouchRotate(startangle, curangle, dist, cx, cy, flag);
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
void tTVPBasicDrawDevice::OnMultiTouch()
{
    if (!Manager)
        return;

    Manager->NotifyMultiTouch();
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
void tTVPBasicDrawDevice::OnDisplayRotate(
    tjs_int orientation, tjs_int rotate, tjs_int bpp, tjs_int width, tjs_int height)
{
    // 何もしない
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
void tTVPBasicDrawDevice::RecheckInputState()
{
    if (!Manager)
        return;

    Manager->RecheckInputState();
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
void tTVPBasicDrawDevice::SetDefaultMouseCursor(iTVPLayerManager* manager)
{
    if (!Manager)
        return;
    if (Manager == manager)
    {
        Window->SetDefaultMouseCursor();
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
void tTVPBasicDrawDevice::SetMouseCursor(iTVPLayerManager* manager, tjs_int cursor)
{
    if (!Manager)
        return;
    if (Manager == manager)
    {
        Window->SetMouseCursor(cursor);
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
void tTVPBasicDrawDevice::GetCursorPos(iTVPLayerManager* manager, tjs_int& x, tjs_int& y)
{
    if (!Manager)
        return;
    Window->GetCursorPos(x, y);
    if (Manager != manager)
    {
        // プライマリレイヤマネージャ以外には座標 0,0 で渡しておく
        x = 0;
        y = 0;
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
void tTVPBasicDrawDevice::SetCursorPos(iTVPLayerManager* manager, tjs_int x, tjs_int y)
{
    if (!Manager)
        return;
    if (Manager == manager)
    {
        Window->SetCursorPos(x, y);
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
void tTVPBasicDrawDevice::WindowReleaseCapture(iTVPLayerManager* manager)
{
    if (!Manager)
        return;
    if (Manager == manager)
    {
        Window->WindowReleaseCapture();
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
void tTVPBasicDrawDevice::SetHintText(iTVPLayerManager* manager,
                                      iTJSDispatch2* sender,
                                      const ttstr& text)
{
    if (!Manager)
        return;
    if (Manager == manager)
    {
        Window->SetHintText(sender, text);
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
void tTVPBasicDrawDevice::SetAttentionPoint(iTVPLayerManager* manager,
                                            tTJSNI_BaseLayer* layer,
                                            tjs_int l,
                                            tjs_int t)
{
    if (!Manager)
        return;
    if (Manager == manager)
    {
        Window->SetAttentionPoint(layer, l, t);
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
void tTVPBasicDrawDevice::DisableAttentionPoint(iTVPLayerManager* manager)
{
    if (!Manager)
        return;
    if (Manager == manager)
    {
        Window->DisableAttentionPoint();
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
void tTVPBasicDrawDevice::SetImeMode(iTVPLayerManager* manager, tTVPImeMode mode)
{
    if (!Manager)
        return;
    if (Manager == manager)
    {
        Window->SetImeMode(mode);
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
void tTVPBasicDrawDevice::ResetImeMode(iTVPLayerManager* manager)
{
    if (!Manager)
        return;
    if (Manager == manager)
    {
        Window->ResetImeMode();
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
tTJSNI_BaseLayer* tTVPBasicDrawDevice::GetPrimaryLayer()
{
    if (!Manager)
        return NULL;
    return Manager->GetPrimaryLayer();
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
tTJSNI_BaseLayer* tTVPBasicDrawDevice::GetFocusedLayer()
{
    if (!Manager)
        return NULL;
    return Manager->GetFocusedLayer();
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
void tTVPBasicDrawDevice::SetFocusedLayer(tTJSNI_BaseLayer* layer)
{
    if (!Manager)
        return;
    Manager->SetFocusedLayer(layer);
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
void tTVPBasicDrawDevice::RequestInvalidation(const tTVPRect& rect)
{
    tjs_int l = rect.left, t = rect.top, r = rect.right, b = rect.bottom;
    r++; // 誤差の吸収(本当はもうちょっと厳密にやらないとならないがそれが問題になることはない)
    b++;

    if (!Manager)
        return;
    Manager->RequestInvalidation(tTVPRect(l, t, r, b));
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
void tTVPBasicDrawDevice::Update()
{
    // すべての layer manager の UpdateToDrawDevice を呼ぶ
    if (Manager)
    {
        Manager->UpdateToDrawDevice();
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
void tTVPBasicDrawDevice::Show()
{
    if (!Window || !Manager)
        return;
    iTVPBaseBitmap* buf = Manager->GetDrawBuffer();
    if (!buf)
        return;
    iTVPTexture2D* tex = buf->GetTexture();
    if (!tex)
        return;

    // GPU 驻留（GPU RenderManager 注入场景）：Layer 体系纹理自带 compositor 句柄 → 直传
    if (!tex->IsCPUResident() && tex->GetTextureHandle())
    {
        Window->PresentTexture(tex->GetTextureHandle(), tex->GetWidth(), tex->GetHeight());
        return;
    }

    // 转化：iTVPTexture2D（软渲染 DrawBuffer）→ compositor 一般贴图 → 窗口统一呈现
    krkrsdl3::iTVPRenderBackend* backend = krkrsdl3::TVPGetRenderBackend();
    if (!backend)
        return;
    tjs_int w = tex->GetWidth(), h = tex->GetHeight();
    if (!ScratchTexture || w != ScratchW || h != ScratchH)
    {
        if (ScratchTexture)
            backend->DestroyTexture(ScratchTexture);
        ScratchTexture = backend->CreateTexture(w, h);
        ScratchW = w;
        ScratchH = h;
    }
    if (!ScratchTexture)
        return;

    void* pixels = tex->LockCPURead();
    if (pixels)
    {
        backend->UpdateTexture(ScratchTexture, (tjs_uint8*)pixels, w, h, tex->GetPitch());
        tex->UnlockCPU();
    }
    Window->PresentTexture(ScratchTexture, w, h);
}
//---------------------------------------------------------------------------
bool tTVPBasicDrawDevice::WaitForVBlank(tjs_int* in_vblank, tjs_int* delayed)
{
    return false;
}

//---------------------------------------------------------------------------
void tTVPBasicDrawDevice::DumpLayerStructure()
{
    // すべての layer manager の DumpLayerStructure を呼ぶ
    if (Manager)
    {
        Manager->DumpLayerStructure();
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
void tTVPBasicDrawDevice::SetShowUpdateRect(bool b)
{
}

void tTVPBasicDrawDevice::SetWindowSize(tjs_int w, tjs_int h)
{
}

//---------------------------------------------------------------------------
bool tTVPBasicDrawDevice::SwitchToFullScreen(
    int window, tjs_uint w, tjs_uint h, tjs_uint bpp, tjs_uint color, bool changeresolution)
{
    return true;
}
//---------------------------------------------------------------------------
void tTVPBasicDrawDevice::RevertFromFullScreen(
    int window, tjs_uint w, tjs_uint h, tjs_uint bpp, tjs_uint color)
{
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
void tTVPBasicDrawDevice::StartBitmapCompletion(iTVPLayerManager* manager)
{
}
//---------------------------------------------------------------------------
void tTVPBasicDrawDevice::NotifyBitmapCompleted(iTVPLayerManager* manager,
                                                tjs_int x,
                                                tjs_int y,
                                                tTVPBaseTexture* bmp,
                                                const tTVPRect& cliprect,
                                                tTVPLayerType type,
                                                tjs_int opacity)
{
    // bits, bitmapinfo で表されるビットマップの cliprect の領域を、x, y に描画
    // する。
    // opacity と type は無視するしかないので無視する
    tjs_int w, h;
    GetSrcSize(w, h);
}
//---------------------------------------------------------------------------
void tTVPBasicDrawDevice::EndBitmapCompletion(iTVPLayerManager* manager)
{
}