#include "DrawDeviceD3D.h"

#include "Platform.h"
#include "D3DEmotePlayer.h"
#include "LayerManager.h"
#include "TVPWindow.h"
#include "tjsNativeLayer.h"
#include "TVPSystem.h"

#include <algorithm>


//---------------------------------------------------------------------------
// DrawDeviceD3D 实现
//---------------------------------------------------------------------------
namespace drawdevice
{

// 后端混合模式映射（Layer.type 常量 → 后端 SetBlendMode）
static int MapLayerTypeToBlendMode(int type)
{
    switch (type)
    {
        case 0: // ltBinder
        case 1: // ltOpaque
            return 2; // 覆盖
        case 2: // ltAlpha
            return 0;
        case 3: // ltAdditive
            return 3;
        case 5: // ltMultiplicative
            return 1;
        case 12: // ltAddAlpha
            return 3;
        default:
            return 0; // 其余类型近似 alpha（正确性由软件回退保证的场景极少）
    }
}

// Layer 合成方法 → 2D 网格混合模式（仿射四边形走 DrawMesh 时使用）
static int LayerToMeshBlendMode(int lbm)
{
    using namespace krkrsdl3;
    switch (lbm)
    {
        case iTVPRenderBackend::LBM_COPY:
        case iTVPRenderBackend::LBM_ALPHA:
        case iTVPRenderBackend::LBM_CONSTALPHA:
            return 0;
        case iTVPRenderBackend::LBM_ADD:
            return 3;
        case iTVPRenderBackend::LBM_MUL:
        case iTVPRenderBackend::LBM_MUL_HDA:
            return 1;
        default:
            return 0;
    }
}

// ===========================================================================
// 构造 / 析构
// ===========================================================================
DrawDeviceD3D::DrawDeviceD3D(tjs_int w, tjs_int h) : Width(w), Height(h)
{
    ScreenW = w;
    ScreenH = h;
    EnsureBackend();

    // 注意：不切换全局渲染管理器——Layer 树保持完全软渲染
    // （tjsNativeLayer 无 GPU 路径），以保证与渲染基准一致；
    // GPU 加速只发生在独立 D3D 层（D3DLayer）与合成目标上。
}

DrawDeviceD3D::~DrawDeviceD3D()
{
    // 解除窗口 sprite 对合成目标纹理的借用（由窗口侧管理，不再直接操作 sprite）；
    // Window 析构时已经 SetWindowInterface(nullptr) 解除引用，此处安全
    if (Window)
        Window->ReleaseBorrowedTexture();
    if (Backend)
    {
        if (PresentScratchTexture)
            Backend->DestroyTexture(PresentScratchTexture);
        if (CompositeTarget)
            Backend->DestroyTarget(CompositeTarget);
        if (PrevCompositeTarget)
            Backend->DestroyTarget(PrevCompositeTarget);
        if (ScratchTexture)
            Backend->DestroyTexture(ScratchTexture);
    }
    for (auto& mi : Managers)
    {
        if (mi.Manager)
            mi.Manager->Release();
    }
    Managers.clear();
}

void DrawDeviceD3D::Destruct()
{
    delete this;
}

void DrawDeviceD3D::EnsureBackend()
{
    if (!Backend)
        Backend = krkrsdl3::TVPGetRenderBackend();
}

bool DrawDeviceD3D::IsSoftwareBackend() const
{
    return Backend && std::string(Backend->GetName()) == "software";
}

// ===========================================================================
// iTVPDrawDevice —— 窗口 / 输入转发
// ===========================================================================
void DrawDeviceD3D::SetWindowInterface(TVPWindow* window)
{
    Window = window;
}

void DrawDeviceD3D::AddLayerManager(iTVPLayerManager* manager)
{
    ManagerInfo info;
    info.Manager = manager;
    info.Primary = manager->GetPrimaryLayer();
    manager->AddRef();
    manager->SetDesiredLayerType(ltOpaque);
    Managers.push_back(info);
}

void DrawDeviceD3D::RemoveLayerManager(iTVPLayerManager* manager)
{
    for (auto it = Managers.begin(); it != Managers.end(); ++it)
    {
        if (it->Manager == manager)
        {
            it->Manager->Release();
            Managers.erase(it);
            break;
        }
    }
}

void DrawDeviceD3D::GetSrcSize(tjs_int& w, tjs_int& h)
{
    w = Width;
    h = Height;
}

void DrawDeviceD3D::NotifyLayerResize(iTVPLayerManager* manager)
{
    if (Window)
        Window->NotifySrcResize();
}

void DrawDeviceD3D::NotifyLayerImageChange(iTVPLayerManager* manager)
{
    // 层内容变化 → 请求窗口更新（与 tTVPBasicDrawDevice 行为一致），
    // 否则 UpdateContent 不会触发，画面不会刷新
    if (Window)
        Window->RequestUpdate();
}

void DrawDeviceD3D::OnClick(tjs_int x, tjs_int y)
{
    if (LayerManagerIndex < Managers.size())
        Managers[LayerManagerIndex].Manager->NotifyClick(x, y);
}
void DrawDeviceD3D::OnDoubleClick(tjs_int x, tjs_int y)
{
    if (LayerManagerIndex < Managers.size())
        Managers[LayerManagerIndex].Manager->NotifyDoubleClick(x, y);
}
void DrawDeviceD3D::OnMouseDown(tjs_int x, tjs_int y, tTVPMouseButton mb, tjs_uint32 flags)
{
    if (LayerManagerIndex < Managers.size())
        Managers[LayerManagerIndex].Manager->NotifyMouseDown(x, y, mb, flags);
}
void DrawDeviceD3D::OnMouseUp(tjs_int x, tjs_int y, tTVPMouseButton mb, tjs_uint32 flags)
{
    if (LayerManagerIndex < Managers.size())
        Managers[LayerManagerIndex].Manager->NotifyMouseUp(x, y, mb, flags);
}
void DrawDeviceD3D::OnMouseMove(tjs_int x, tjs_int y, tjs_uint32 flags)
{
    if (LayerManagerIndex < Managers.size())
        Managers[LayerManagerIndex].Manager->NotifyMouseMove(x, y, flags);
}
void DrawDeviceD3D::OnReleaseCapture()
{
    if (LayerManagerIndex < Managers.size())
        Managers[LayerManagerIndex].Manager->ReleaseCapture();
}
void DrawDeviceD3D::OnMouseOutOfWindow()
{
    if (LayerManagerIndex < Managers.size())
        Managers[LayerManagerIndex].Manager->NotifyMouseOutOfWindow();
}
void DrawDeviceD3D::OnKeyDown(tjs_uint key, tjs_uint32 shift)
{
    if (LayerManagerIndex < Managers.size())
        Managers[LayerManagerIndex].Manager->NotifyKeyDown(key, shift);
}
void DrawDeviceD3D::OnKeyUp(tjs_uint key, tjs_uint32 shift)
{
    if (LayerManagerIndex < Managers.size())
        Managers[LayerManagerIndex].Manager->NotifyKeyUp(key, shift);
}
void DrawDeviceD3D::OnKeyPress(tjs_uint16 key)
{
    if (LayerManagerIndex < Managers.size())
        Managers[LayerManagerIndex].Manager->NotifyKeyPress(key);
}
void DrawDeviceD3D::OnMouseWheel(tjs_uint32 shift, tjs_int delta, tjs_int x, tjs_int y)
{
    if (LayerManagerIndex < Managers.size())
        Managers[LayerManagerIndex].Manager->NotifyMouseWheel(shift, delta, x, y);
}
void DrawDeviceD3D::OnTouchDown(tjs_real x, tjs_real y, tjs_real cx, tjs_real cy, tjs_uint32 id)
{
    if (LayerManagerIndex < Managers.size())
        Managers[LayerManagerIndex].Manager->NotifyTouchDown(x, y, cx, cy, id);
}
void DrawDeviceD3D::OnTouchUp(tjs_real x, tjs_real y, tjs_real cx, tjs_real cy, tjs_uint32 id)
{
    if (LayerManagerIndex < Managers.size())
        Managers[LayerManagerIndex].Manager->NotifyTouchUp(x, y, cx, cy, id);
}
void DrawDeviceD3D::OnTouchMove(tjs_real x, tjs_real y, tjs_real cx, tjs_real cy, tjs_uint32 id)
{
    if (LayerManagerIndex < Managers.size())
        Managers[LayerManagerIndex].Manager->NotifyTouchMove(x, y, cx, cy, id);
}
void DrawDeviceD3D::OnTouchScaling(tjs_real startdist, tjs_real curdist, tjs_real cx, tjs_real cy, tjs_int flag)
{
    if (LayerManagerIndex < Managers.size())
        Managers[LayerManagerIndex].Manager->NotifyTouchScaling(startdist, curdist, cx, cy, flag);
}
void DrawDeviceD3D::OnTouchRotate(tjs_real startangle, tjs_real curangle, tjs_real dist, tjs_real cx, tjs_real cy, tjs_int flag)
{
    if (LayerManagerIndex < Managers.size())
        Managers[LayerManagerIndex].Manager->NotifyTouchRotate(startangle, curangle, dist, cx, cy, flag);
}
void DrawDeviceD3D::OnMultiTouch()
{
    if (LayerManagerIndex < Managers.size())
        Managers[LayerManagerIndex].Manager->NotifyMultiTouch();
}
void DrawDeviceD3D::RecheckInputState()
{
    if (LayerManagerIndex < Managers.size())
        Managers[LayerManagerIndex].Manager->RecheckInputState();
}
void DrawDeviceD3D::SetDefaultMouseCursor(iTVPLayerManager* manager)
{
    if (Window)
        Window->SetDefaultMouseCursor();
}
void DrawDeviceD3D::SetMouseCursor(iTVPLayerManager* manager, tjs_int cursor)
{
    if (Window)
        Window->SetMouseCursor(cursor);
}
void DrawDeviceD3D::GetCursorPos(iTVPLayerManager* manager, tjs_int& x, tjs_int& y)
{
    if (Window)
        Window->GetCursorPos(x, y);
}
void DrawDeviceD3D::SetCursorPos(iTVPLayerManager* manager, tjs_int x, tjs_int y)
{
    if (Window)
        Window->SetCursorPos(x, y);
}
void DrawDeviceD3D::SetHintText(iTVPLayerManager* manager, iTJSDispatch2* sender, const ttstr& text)
{
    if (Window)
        Window->SetHintText(sender, text);
}
void DrawDeviceD3D::WindowReleaseCapture(iTVPLayerManager* manager)
{
    if (Window)
        Window->WindowReleaseCapture();
}
void DrawDeviceD3D::SetAttentionPoint(iTVPLayerManager* manager, tTJSNI_BaseLayer* layer, tjs_int l, tjs_int t)
{
    if (Window)
        Window->SetAttentionPoint(layer, l, t);
}
void DrawDeviceD3D::DisableAttentionPoint(iTVPLayerManager* manager)
{
    if (Window)
        Window->DisableAttentionPoint();
}
void DrawDeviceD3D::SetImeMode(iTVPLayerManager* manager, tTVPImeMode mode)
{
    if (Window)
        Window->SetImeMode(mode);
}
void DrawDeviceD3D::ResetImeMode(iTVPLayerManager* manager)
{
    if (Window)
        Window->ResetImeMode();
}
tTJSNI_BaseLayer* DrawDeviceD3D::GetPrimaryLayer()
{
    if (LayerManagerIndex < Managers.size())
        return Managers[LayerManagerIndex].Primary;
    return nullptr;
}
tTJSNI_BaseLayer* DrawDeviceD3D::GetFocusedLayer()
{
    if (LayerManagerIndex < Managers.size())
        return Managers[LayerManagerIndex].Manager->GetFocusedLayer();
    return nullptr;
}
void DrawDeviceD3D::SetFocusedLayer(tTJSNI_BaseLayer* layer)
{
    if (LayerManagerIndex < Managers.size())
        Managers[LayerManagerIndex].Manager->SetFocusedLayer(layer);
}
void DrawDeviceD3D::DumpLayerStructure()
{
    for (auto& mi : Managers)
        if (mi.Manager)
            mi.Manager->DumpLayerStructure();
}

// ===========================================================================
// 合成与呈现
// ===========================================================================
void DrawDeviceD3D::DrawQuadTo(void* target,
                               void* srcTexture,
                               float x,
                               float y,
                               float w,
                               float h,
                               int blendMode,
                               float opacity,
                               const float matrix[16])
{
    if (!Backend || !target || !srcTexture)
        return;
    float l = x, t = y, r = x + w, b = y + h;
    float cx0 = l, cy0 = t, cx1 = r, cy1 = t, cx2 = r, cy2 = b, cx3 = l, cy3 = b;
    if (matrix)
    {
        // 2D 仿射：x' = m0*x + m4*y + m12; y' = m1*x + m5*y + m13
        cx0 = matrix[0] * l + matrix[4] * t + matrix[12];
        cy0 = matrix[1] * l + matrix[5] * t + matrix[13];
        cx1 = matrix[0] * r + matrix[4] * t + matrix[12];
        cy1 = matrix[1] * r + matrix[5] * t + matrix[13];
        cx2 = matrix[0] * r + matrix[4] * b + matrix[12];
        cy2 = matrix[1] * r + matrix[5] * b + matrix[13];
        cx3 = matrix[0] * l + matrix[4] * b + matrix[12];
        cy3 = matrix[1] * l + matrix[5] * b + matrix[13];
    }
    // 混合模式 → Layer 合成方法（软件 RenderManager 语义）：
    //   2（覆盖）→ LBM_COPY；0（alpha）→ LBM_ALPHA；1（乘）→ LBM_MUL；
    //   3（加）→ LBM_ADD
    int lbm = krkrsdl3::iTVPRenderBackend::LBM_ALPHA;
    switch (blendMode)
    {
        case 2:
            lbm = krkrsdl3::iTVPRenderBackend::LBM_COPY;
            break;
        case 1:
            lbm = krkrsdl3::iTVPRenderBackend::LBM_MUL;
            break;
        case 3:
            lbm = krkrsdl3::iTVPRenderBackend::LBM_ADD;
            break;
        default:
            lbm = krkrsdl3::iTVPRenderBackend::LBM_ALPHA;
            break;
    }
    Backend->SetTarget(target);
    Backend->ClearTarget(false);
    Backend->LayerSetBlend(lbm, opacity, nullptr);
    if (matrix)
    {
        // 仿射四边形：走网格路径（Layer 合成仅支持矩形）
        float tw = (float)Width, th = (float)Height;
        float v[16] = {
            cx0 / tw * 2.f, cy0 / th * 2.f, 0.f, 0.f, //
            cx1 / tw * 2.f, cy1 / th * 2.f, 1.f, 0.f, //
            cx2 / tw * 2.f, cy2 / th * 2.f, 1.f, 1.f, //
            cx3 / tw * 2.f, cy3 / th * 2.f, 0.f, 1.f, //
        };
        uint16_t idx[] = {0, 1, 2, 2, 3, 0};
        Backend->SetBlendMode(LayerToMeshBlendMode(lbm), nullptr);
        Backend->DrawMesh(v, 4, idx, 6, srcTexture, opacity);
    }
    else
    {
        Backend->LayerDrawRect(srcTexture, l, t, r - l, b - t);
    }
}

void DrawDeviceD3D::ComposeLayerManager(int index, void* target)
{
    if (index < 0 || index >= (tjs_int)Managers.size())
        return;
    iTVPLayerManager* mgr = Managers[index].Manager;
    if (!mgr || !Backend)
        return;
    // 合成图层树到 DrawBuffer（FrontBuff）：
    //  1) 先清为全透明 0,0,0,0 —— 空区域不留不透明黑（uibase 等独立 primary 的
    //     DrawBuffer 初始为 0xFF000000，直接整幅 LBM_COPY 上传会得到"黑白表面"）；
    //  2) SetHoldAlpha(false) —— ltAlpha 图层以 bmAlphaOnAlpha（src-over，写入 alpha）
    //     合成，而不是 bmAlpha（保持目标 alpha=255 的不透明叠加）；
    //  3) 强制全矩形失效重绘 —— 否则旧帧内容残留在 DrawBuffer 里与本次绘制重叠。
    // 结果：DrawBuffer 的透明区域在 GPU 侧用 LBM_ALPHA 上传，露出下层
    // （D3D 世界层）内容。
    tTVPLayerManager* lm = dynamic_cast<tTVPLayerManager*>(mgr);
    if (!lm)
        return;
    lm->SetHoldAlpha(false);
    tTVPBaseTexture* buf = lm->GetOrCreateDrawBuffer();
    if (!buf)
        return;
    tjs_int pw = buf->GetWidth(), ph = buf->GetHeight();
    buf->Fill(tTVPRect(0, 0, pw, ph), 0x00000000);
    mgr->RequestInvalidation(tTVPRect(0, 0, pw, ph));
    mgr->UpdateToDrawDevice();

    iTVPTexture2D* tex = buf->GetTexture();
    if (!tex)
        return;
    // DrawBuffer（软渲染位图）作为贴图画到合成目标（带 alpha）：
    // Layer 树在 CPU 上精细合成后，每帧只做一次 CPU→GPU 上传
    void* srcTex = nullptr;
    int bufW = tex->GetWidth(), bufH = tex->GetHeight();
    {
        // 软件贴图：读 CPU 像素 → 上传后端一般贴图
        tjs_uint8* data = nullptr;
        tjs_int pitch = 0;
        tex->GetTextureData(&data, pitch);
        if (data && bufW > 0 && bufH > 0)
        {
            if (!ScratchTexture)
                ScratchTexture = Backend->CreateTexture(bufW, bufH);
            if (ScratchTexture)
            {
                Backend->UpdateTexture(ScratchTexture, data, bufW, bufH, pitch);
                srcTex = ScratchTexture;
            }
        }
    }

    if (srcTex)
        // mode 0 = LBM_ALPHA：透明区域露出下层（D3D 世界层等）
        DrawQuadTo(target, srcTex, 0, 0, (float)Width, (float)Height, 0, 1.0f);
}

// 绘制单个 D3D 层（Back/Front 平面共用）。
// D3D 层是 GPU 直通的"图层"，混合语义走渲染后端的 Layer 合成路径
// （软件 RenderManager bm* 语义，与普通 Layer 的合成基准一致）；
// 带仿射矩阵时退回网格路径（近似混合）。
static void DrawD3DLayerPictures(DrawDeviceD3D* dev, void* target, D3DLayer* layer,
                                 float offsetX, float offsetY)
{
    for (auto* pic : layer->Pictures)
    {
        if (!pic || !pic->Image || !pic->Image->Texture)
            continue;
        int mode = MapLayerTypeToBlendMode(pic->blendMode);
        int lbm = krkrsdl3::iTVPRenderBackend::LBM_ALPHA;
        switch (mode)
        {
            case 2:
                lbm = krkrsdl3::iTVPRenderBackend::LBM_COPY;
                break;
            case 3:
                lbm = krkrsdl3::iTVPRenderBackend::LBM_ADD;
                break;
            case 1:
                lbm = krkrsdl3::iTVPRenderBackend::LBM_MUL;
                break;
            default:
                lbm = krkrsdl3::iTVPRenderBackend::LBM_ALPHA;
                break;
        }
        float opa = (float)pic->opacity / 255.0f;
        float px = (float)pic->DstX + pic->CoordX + offsetX;
        float py = (float)pic->DstY + pic->CoordY + offsetY;
        float pw = (float)pic->SrcW, ph = (float)pic->SrcH;
        float sw = (float)pic->Image->Width, sh = (float)pic->Image->Height;
        float u0 = (float)pic->SrcX / sw, v0 = (float)pic->SrcY / sh;
        float u1 = (float)(pic->SrcX + pic->SrcW) / sw, v1 = (float)(pic->SrcY + pic->SrcH) / sh;

        krkrsdl3::iTVPRenderBackend* backend = dev->GetBackend();
        backend->SetTarget(target);
        backend->ClearTarget(false);

        // 仿射矩阵是否为单位阵（仅平移/缩放可由 LayerDrawRect 直接表达；
        // 一般仿射走 DrawMesh）
        const float* m = layer->Matrix;
        bool affine = (m[0] != 1.0f || m[1] != 0.0f || m[4] != 0.0f || m[5] != 1.0f ||
                       m[12] != 0.0f || m[13] != 0.0f);
        if (!affine)
        {
            //加速在渲染侧做判断更好吧
        }

        // 仿射：4 角经矩阵变换 → NDC（内容坐标 y 向下，顶=NDC -1，与 Layer 路径一致）
        float l = px, t = py, r = px + pw, b = py + ph;
        float cx0 = m[0] * l + m[4] * t + m[12];
        float cy0 = m[1] * l + m[5] * t + m[13];
        float cx1 = m[0] * r + m[4] * t + m[12];
        float cy1 = m[1] * r + m[5] * t + m[13];
        float cx2 = m[0] * r + m[4] * b + m[12];
        float cy2 = m[1] * r + m[5] * b + m[13];
        float cx3 = m[0] * l + m[4] * b + m[12];
        float cy3 = m[1] * l + m[5] * b + m[13];
        float tw = (float)dev->GetWidth(), th = (float)dev->GetHeight();
        float v[16] = {
            cx0 / tw * 2.f, cy0 / th * 2.f, u0, v0, //
            cx1 / tw * 2.f, cy1 / th * 2.f, u1, v0, //
            cx2 / tw * 2.f, cy2 / th * 2.f, u1, v1, //
            cx3 / tw * 2.f, cy3 / th * 2.f, u0, v1, //
        };
        uint16_t idx[] = {0, 1, 2, 2, 3, 0};
        backend->SetBlendMode(LayerToMeshBlendMode(lbm), nullptr);
        backend->DrawMesh(v, 4, idx, 6, pic->Image->Texture, opa);
    }
}

void DrawDeviceD3D::RenderFrame()
{
    EnsureBackend();
    if (!Backend)
        return;

    // 合成目标（虚拟屏幕尺寸）
    if (!CompositeTarget)
        CompositeTarget = Backend->CreateTarget(Width, Height);
    if (!CompositeTarget)
    {
        TVPConsoleLog("D3D RenderFrame: no composite target");
        return;
    }


    // 清屏
    Backend->SetTarget(CompositeTarget);
    Backend->ClearTarget(true);

    // 合成顺序（对应 KAG 非 D3D 的表裏双页机制，见 docs/drawdevice-d3d.md）：
    //   - Back 平面 D3D 层 = "裏"画面（world.basePlane==1，转场期间世界层所在）。
    //     非 D3D 语义：裏（back.base）平时隐藏（initBaseVisible 置
    //     back.base.visible=false），仅在转场期间作为转场源参与合成；
    //     因此这里只在本设备转场进行中绘制 Back 平面层。
    //   - LayerManager（DrawBuffer） = 表画面的普通 Layer 树（软渲染合成），
    //     画在世界层之上（消息/UI 高于世界层，对应非 D3D 的 absolute 层序）；
    //     DrawBuffer 透明区域露出世界层。
    //   - Front/Both 平面 D3D 层 = "表"画面（world.basePlane==0，平时世界层所在）。
    std::vector<D3DLayer*> backLayers, frontLayers;
    for (auto* l : D3DLayers)
    {
        if (l->drawPlane == D3DLayer::DrawPlaneBack)
            backLayers.push_back(l);
        else
            frontLayers.push_back(l);
    }
    auto sortByIndex = [](std::vector<D3DLayer*>& v) {
        std::stable_sort(v.begin(), v.end(),
                         [](D3DLayer* a, D3DLayer* b) { return a->frontIndex < b->frontIndex; });
    };
    sortByIndex(backLayers);
    sortByIndex(frontLayers);

    // 只合成主 layer manager（layerManagerIndex，脚本可设；默认 0）。
    // 其它 manager 属于子窗口/辅助窗口，非 D3D 模式下不参与主窗口显示，
    // 若一并合成会以空白白底覆盖主画面。
   
    // 先画 D3D 世界层（Front 平面 = 表画面世界层），再在其上合成 LayerManager
    // DrawBuffer（消息/UI），对应非 D3D 的层序（世界层 absolute≈0..N < 消息 1M < uibase 5M）；
    // DrawBuffer 透明区域露出世界层。
    for (auto* l : frontLayers)
        DrawD3DLayerPictures(this, CompositeTarget, l, (float)OffsetX, (float)OffsetY);
    // Back 平面世界层（裏画面）仅在转场期间作为转场源参与合成
    if (TransitionActive && TransitionProgress < 1.0f)
    {
        for (auto* l : backLayers)
            DrawD3DLayerPictures(this, CompositeTarget, l, (float)OffsetX, (float)OffsetY);
    }
    // D3DEmotePlayer（GPU 直通动画）
    for (auto* p : EmotePlayers)
    {
        if (p && p->IsActive())
            p->Draw(CompositeTarget);
    }
    if (LayerManagerIndex >= 0 && LayerManagerIndex < (tjs_int)Managers.size())
    {
        ComposeLayerManager(LayerManagerIndex, CompositeTarget);
    }

    // 转场交叉淡化：结果 = prev*(1-p) + current*p
    if (TransitionActive && PrevCompositeTarget && TransitionProgress < 1.0f)
    {
        void* prevTex = Backend->GetTargetTexture(PrevCompositeTarget);
        if (prevTex)
        {
            float opa = 1.0f - TransitionProgress;
            float v[16] = {
                -1.f, -1.f, 0.f, 0.f, //
                1.f,  -1.f, 1.f, 0.f, //
                1.f,  1.f, 1.f, 1.f, //
                -1.f, 1.f, 0.f, 1.f, //
            };
            uint16_t idx[] = {0, 1, 2, 2, 3, 0};
            Backend->SetTarget(CompositeTarget);
            Backend->ClearTarget(false);
            Backend->SetBlendMode(0, nullptr);
            Backend->DrawMesh(v, 4, idx, 6, prevTex, opa);
        }
        if (TransitionProgress >= 1.0f)
            stopTransition();
    }
}

void DrawDeviceD3D::PresentToWindow()
{
    // 统一呈现路径：合成目标（compositor 体系的后端离屏目标）→ TVPWindow::PresentTexture
    //   - GPU 后端：目标纹理直接呈现（sprite 别名，零拷贝；TVPRenderOnce 绘制之）
    //   - 软件后端：目标（CPU 缓冲）→ 一般贴图中转 → 窗口上传（SW 保底路径）
    // 窗口可见性/尺寸由 TVPWindow 统一管理，此处不再直接操作 sprite。
    EnsureBackend();
    if (!Backend || !Window || !CompositeTarget)
        return;

    if (IsSoftwareBackend())
    {
        if (!PresentScratchTexture)
            PresentScratchTexture = Backend->CreateTexture(Width, Height);
        if (!PresentScratchTexture)
            return;
        int pitch = 0;
        uint8_t* pixels = Backend->LockTarget(CompositeTarget, pitch);
        if (pixels)
        {
            Backend->UpdateTexture(PresentScratchTexture, pixels, Width, Height, pitch);
            Backend->UnlockTarget(CompositeTarget);
        }
        Window->PresentTexture(PresentScratchTexture, Width, Height);
        return;
    }

    Window->PresentTexture(Backend->GetTargetTexture(CompositeTarget), Width, Height);
}

void DrawDeviceD3D::Update()
{
    RenderFrame();
}

void DrawDeviceD3D::Show()
{
    PresentToWindow();
}

// ===========================================================================
// 脚本 API
// ===========================================================================
void DrawDeviceD3D::update(tjs_real diffTime)
{
    // 转场进度由脚本通过 transState 驱动
    if (TransitionActive)
    {
        TransitionProgress = 1.0 - TransState;
        if (TransitionProgress < 0)
            TransitionProgress = 0;
        if (TransitionProgress > 1)
            TransitionProgress = 1;
    }
    // 驱动各 D3DLayer 的脚本 onUpdate(diff)：
    // D3DAffineLayer 等脚本子类在此计算仿射矩阵（setMatrix）并同步 picture 属性，
    // 与普通 Layer 的 onPaint 链对应（KAG 每帧调用 drawDevice.update(diffTime)）。
    for (auto* l : D3DLayers)
    {
        if (l)
            l->DriveOnUpdate(diffTime);
    }
    RenderFrame();
    PresentToWindow();
}

void DrawDeviceD3D::capture(iTJSDispatch2* layerObj, tjs_int index)
{
    tTJSNI_BaseLayer* layer = nullptr;
    if (layerObj && layerObj->NativeInstanceSupport(TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
                                                    (iTJSNativeInstance**)&layer) < 0)
        return;
    if (!layer)
        return;

    // index 映射到层管理器：0=首个；>=5000000 时为绝对序号（indexBase + 序号）
    size_t mi = 0;
    if (index >= 5000000)
        mi = (size_t)(index - 5000000);
    else
        mi = (size_t)index;
    if (mi >= Managers.size())
        return;
    iTVPLayerManager* mgr = Managers[mi].Manager;
    if (!mgr)
        return;
    mgr->UpdateToDrawDevice();
    iTVPBaseBitmap* buf = mgr->GetDrawBuffer();
    if (!buf)
        return;
    iTVPTexture2D* tex = buf->GetTexture();
    if (!tex)
        return;

    // 拷贝到目标 Layer 的 MainImage（GPU 拷贝；不共享）
    iTVPRenderManager* rm = TVPGetRenderManager();
    iTVPTexture2D* copy = rm->CreateTexture2D(tex->GetWidth(), tex->GetHeight(), tex);
    if (copy)
        layer->AssignTexture(copy);
}

void DrawDeviceD3D::startTransition(iTJSDispatch2* dict)
{
    if (!dict || !Backend || !CompositeTarget)
        return;
    // 保存当前画面快照
    if (PrevCompositeTarget)
    {
        Backend->DestroyTarget(PrevCompositeTarget);
        PrevCompositeTarget = nullptr;
    }
    PrevCompositeTarget = Backend->CreateTarget(Width, Height);
    if (!PrevCompositeTarget)
        return;
    void* curTex = Backend->GetTargetTexture(CompositeTarget);
    if (curTex)
        DrawQuadTo(PrevCompositeTarget, curTex, 0, 0, (float)Width, (float)Height, 2, 1.0f);
    TransitionActive = true;
    TransitionProgress = 0.0f;
    TransState = 1.0f;
}

void DrawDeviceD3D::stopTransition()
{
    TransitionActive = false;
    TransitionProgress = 1.0f;
    TransState = 1.0f;
    if (Backend && PrevCompositeTarget)
    {
        Backend->DestroyTarget(PrevCompositeTarget);
        PrevCompositeTarget = nullptr;
    }
}

void DrawDeviceD3D::setScreenRect(tjs_int x, tjs_int y, tjs_int w, tjs_int h)
{
    ScreenX = x;
    ScreenY = y;
    ScreenW = w;
    ScreenH = h;
}

void DrawDeviceD3D::setPrimarySize(tjs_int w, tjs_int h)
{
    Width = w;
    Height = h;
    if (Backend)
    {
        if (CompositeTarget)
        {
            Backend->DestroyTarget(CompositeTarget);
            CompositeTarget = nullptr;
        }
        stopTransition();
        CompositeTarget = Backend->CreateTarget(Width, Height);
    }
}

void DrawDeviceD3D::setOffset(tjs_int x, tjs_int y)
{
    OffsetX = x;
    OffsetY = y;
}

void DrawDeviceD3D::recreate()
{
    // 设备重建（窗口重建时）：重取后端
    Backend = krkrsdl3::TVPGetRenderBackend();
    if (CompositeTarget)
    {
        Backend->DestroyTarget(CompositeTarget);
        CompositeTarget = nullptr;
    }
}

void DrawDeviceD3D::finalize()
{
    //delete this;
}

tTJSVariant DrawDeviceD3D::getModule(tTJSString name)
{
    if (name == TJS_N("emote"))
    {
        iTJSDispatch2* dict = TJSCreateDictionaryObject();
        tTJSVariant maskMode(MaskMode);
        static tjs_uint hint = 0;
        dict->PropSet(TJS_MEMBERENSURE, TJS_N("maskMode"), &hint, &maskMode, dict);
        return tTJSVariant(dict, dict);
    }
    return tTJSVariant();
}

tTJSVariant DrawDeviceD3D::getPrimaryLayers()
{
    iTJSDispatch2* arr = TJSCreateArrayObject();
    tjs_int idx = 0;
    for (auto& mi : Managers)
    {
        if (mi.Primary)
        {
            iTJSDispatch2* owner = mi.Primary->GetOwnerNoAddRef();
            if (owner)
            {
                tTJSVariant v(owner, owner);
                arr->PropSetByNum(TJS_MEMBERENSURE, idx++, &v, arr);
            }
        }
    }
    return tTJSVariant(arr, arr);
}

// ===========================================================================
// D3DLayer
// ===========================================================================
D3DLayer::D3DLayer(iTJSDispatch2* d3dDeviceObj, iTJSDispatch2* scriptObject)
{
    DrawDeviceD3D* dev = GetDrawDeviceInstance(d3dDeviceObj);
    if (dev)
    {
        Device = dev;
        Device->RegisterD3DLayer(this);
    }
    if (scriptObject)
    {
        ScriptObject = scriptObject;
        ScriptObject->AddRef();
    }
}

void D3DLayer::DriveOnUpdate(tjs_real diffTime)
{
    // D3DAffineLayer 等脚本子类在 onUpdate(diff) 里计算并 setMatrix / 更新 picture
    // 属性（见 out/data/system/D3D.tjs）。原生 D3DLayer 无此方法时静默跳过。
    if (!ScriptObject)
        return;
    try
    {
        tTJSVariant vDiff(diffTime);
        tTJSVariant* p = &vDiff;
        ScriptObject->FuncCall(0, TJS_N("onUpdate"), nullptr, nullptr, 1, &p, ScriptObject);
    }
    catch (...)
    {
        // onUpdate 不存在或抛异常：非致命
    }
}

D3DLayer::~D3DLayer()
{
    if (ScriptObject)
    {
        ScriptObject->Release();
        ScriptObject = nullptr;
    }
    if (Device)
        Device->UnregisterD3DLayer(this);
    // 图片对象由脚本（ncb native instance）持有，这里只解除回指并清空列表，
    // 否则 D3DPicture 析构时会访问已销毁的 Layer（悬垂崩溃）。
    for (auto* p : Pictures)
    {
        if (p)
            p->Layer = nullptr;
    }
    Pictures.clear();
}

void D3DLayer::setMatrix(tjs_real m11, tjs_real m12, tjs_real m13, tjs_real m14, tjs_real m21,
                         tjs_real m22, tjs_real m23, tjs_real m24, tjs_real m31, tjs_real m32,
                         tjs_real m33, tjs_real m34, tjs_real m41, tjs_real m42, tjs_real m43,
                         tjs_real m44)
{
    // 行优先 16 值；2D 仿射用 m0/m1/m4/m5/m12/m13
    Matrix[0] = (float)m11;
    Matrix[1] = (float)m12;
    Matrix[2] = (float)m13;
    Matrix[3] = (float)m14;
    Matrix[4] = (float)m21;
    Matrix[5] = (float)m22;
    Matrix[6] = (float)m23;
    Matrix[7] = (float)m24;
    Matrix[8] = (float)m31;
    Matrix[9] = (float)m32;
    Matrix[10] = (float)m33;
    Matrix[11] = (float)m34;
    Matrix[12] = (float)m41;
    Matrix[13] = (float)m42;
    Matrix[14] = (float)m43;
    Matrix[15] = (float)m44;
}

void D3DLayer::AddPicture(class D3DPicture* pic)
{
    Pictures.push_back(pic);
}

void D3DLayer::RemovePicture(class D3DPicture* pic)
{
    for (auto it = Pictures.begin(); it != Pictures.end(); ++it)
    {
        if (*it == pic)
        {
            Pictures.erase(it);
            break;
        }
    }
}

void D3DLayer::finalize()
{
    // 此函数只用于数据清理，不用于实例删除，否则析构函数会二次删除
    //delete this;
}

// ===========================================================================
// D3DImage
// ===========================================================================
D3DImage::D3DImage(iTJSDispatch2* drawDeviceObj)
{
    DrawDeviceD3D* dev = GetDrawDeviceInstance(drawDeviceObj);
    if (dev)
        Device = dev;
}

D3DImage::~D3DImage()
{
    if (Device && Device->GetBackend() && Texture)
        Device->GetBackend()->DestroyTexture(Texture);
    Texture = nullptr;
}

void D3DImage::load(iTJSDispatch2* layerObj)
{
    tTJSNI_BaseLayer* layer = nullptr;
    if (layerObj && layerObj->NativeInstanceSupport(TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
                                                    (iTJSNativeInstance**)&layer) < 0)
        return;
    if (!layer || !Device || !Device->GetBackend())
        return;
    tTVPBaseTexture* main = layer->GetMainImage();
    if (!main)
        return;
    tjs_int w = main->GetWidth(), h = main->GetHeight();
    if (w <= 0 || h <= 0)
        return;

    if (Texture)
        Device->GetBackend()->DestroyTexture(Texture);
    Texture = Device->GetBackend()->CreateTexture(w, h);
    if (!Texture)
        return;
    Width = w;
    Height = h;

    // 从 Layer 主图像拷贝像素（CPU 回读后上传；load 不频繁可接受）
    iTVPTexture2D* tex = main->GetTexture();
    if (tex)
    {
        tjs_uint8* data = nullptr;
        tjs_int pitch = 0;
        tex->GetTextureData(&data, pitch); // 返回值=是否需要释放，非失败标志
        if (data)
        {
            Device->GetBackend()->UpdateTexture(Texture, data, w, h, pitch);
        }
    }
}

void D3DImage::finalize()
{
    // 此函数只用于数据清理，不用于实例删除，否则析构函数会二次删除
    //delete this;
}

// ===========================================================================
// D3DPicture
// ===========================================================================
D3DPicture::D3DPicture(iTJSDispatch2* layerObj, iTJSDispatch2* imageObj)
{
    Layer = ncbInstanceAdaptor<D3DLayer>::GetNativeInstance(layerObj);
    if (Layer)
        Layer->AddPicture(this);
    Image = ncbInstanceAdaptor<D3DImage>::GetNativeInstance(imageObj); // 引用图片（不拥有）
}

D3DPicture::~D3DPicture()
{
    if (Layer)
        Layer->RemovePicture(this);
}

void D3DPicture::assignImageRange(tjs_int sx, tjs_int sy, tjs_int sw, tjs_int sh, tjs_int dx,
                                  tjs_int dy)
{
    SrcX = sx;
    SrcY = sy;
    SrcW = sw;
    SrcH = sh;
    DstX = dx;
    DstY = dy;
}

void D3DPicture::finalize()
{
    // 此函数只用于数据清理，不用于实例删除，否则析构函数会二次删除
    //delete this;
}

// ===========================================================================
// DrawDeviceD3D 内部
// ===========================================================================
void DrawDeviceD3D::UnregisterD3DLayer(class D3DLayer* layer)
{
    for (auto it = D3DLayers.begin(); it != D3DLayers.end(); ++it)
    {
        if (*it == layer)
        {
            D3DLayers.erase(it);
            break;
        }
    }
}

void DrawDeviceD3D::UnregisterEmotePlayer(class D3DEmotePlayer* p)
{
    for (auto it = EmotePlayers.begin(); it != EmotePlayers.end(); ++it)
    {
        if (*it == p)
        {
            EmotePlayers.erase(it);
            break;
        }
    }
}

} // namespace drawdevice
