//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// TVPTrans : transition handler management & default transition handlers
//
// 渲染后端无关的转场子系统。所有混合操作统一经当前渲染管理器执行
// （TVPGetRenderManager()）：
//   - CPU 路径（默认软件 RenderManager）：RenderMethod + OperateRect，
//     与 krkrz 原版逐像素一致；
//   - GPU 路径：插件经 TVPSetRenderManager() 注入的 GPU RenderManager
//     提供同名渲染方法（ConstAlphaBlend_SD[_d/_a]、UnivTransBlend[_d/_a]、
//     Copy），本模块不做任何后端假设。
//---------------------------------------------------------------------------
#include "tjsCommHead.h"

#include "TVPTrans.h"
#include "TVPGraphicsLoader.h"
#include "tjsHashSearch.h"
#include "TVPMsg.h"
#include "TVPSystem.h"
#include "TVPDebug.h"
#include "RenderManager.h"
#include "Platform.h"
#include "tjsUtils.h"

// tjsNativeLayer.h 中声明的函数
const tTVPBaseTexture& TVPGetInitialBitmap();

//---------------------------------------------------------------------------
// iTVPSimpleOptionProvider implementation
//---------------------------------------------------------------------------
/* this implementation should move into other proper unit */
tTVPSimpleOptionProvider::tTVPSimpleOptionProvider(tTJSVariantClosure object)
{
    RefCount = 1;
    Object = object;
    Object.AddRef();
}
//---------------------------------------------------------------------------
tTVPSimpleOptionProvider::~tTVPSimpleOptionProvider()
{
    Object.Release();
}
//---------------------------------------------------------------------------
tjs_error tTVPSimpleOptionProvider::AddRef()
{
    RefCount++;
    return TJS_S_OK;
}
//---------------------------------------------------------------------------
tjs_error tTVPSimpleOptionProvider::Release()
{
    if (RefCount == 1)
        delete this;
    else
        RefCount--;
    return TJS_S_OK;
}
//---------------------------------------------------------------------------
tjs_error tTVPSimpleOptionProvider::GetAsNumber(
    /*in*/ const tjs_char* name, /*out*/ tjs_int64* value)
{
    try
    {
        tTJSVariant val;
        tjs_error er = Object.PropGet(0, name, NULL, &val, NULL);
        if (TJS_FAILED(er))
            return er;

        if (val.Type() == tvtVoid)
            return TJS_E_MEMBERNOTFOUND;

        if (value)
            *value = val.AsInteger();

        return TJS_S_OK;
    }
    catch (...)
    {
        return TJS_E_FAIL;
    }
}
//---------------------------------------------------------------------------
tjs_error tTVPSimpleOptionProvider::GetAsString(
    /*in*/ const tjs_char* name, /*out*/ const tjs_char** out)
{
    try
    {
        tTJSVariant val;
        tjs_error er = Object.PropGet(0, name, NULL, &val, NULL);
        if (TJS_FAILED(er))
            return er;

        if (val.Type() == tvtVoid)
            return TJS_E_MEMBERNOTFOUND;

        String = val; // to hold string buffer
        if (out)
            *out = String.c_str();

        return TJS_S_OK;
    }
    catch (...)
    {
        return TJS_E_FAIL;
    }
}
//---------------------------------------------------------------------------
tjs_error tTVPSimpleOptionProvider::GetValue(
    /*in*/ const tjs_char* name, /*out*/ tTJSVariant* dest)
{
    try
    {
        if (!dest)
            return TJS_E_FAIL;
        tjs_error er = Object.PropGet(0, name, NULL, dest, NULL);
        if (TJS_FAILED(er))
            return er;
        return TJS_S_OK;
    }
    catch (...)
    {
        return TJS_E_FAIL;
    }
}
//---------------------------------------------------------------------------
tjs_error tTVPSimpleOptionProvider::GetDispatchObject(iTJSDispatch2** dsp)
{
    try
    {
        Object.ObjThis->AddRef();
        if (dsp)
            *dsp = Object.ObjThis;
        return TJS_S_OK;
    }
    catch (...)
    {
        return TJS_E_FAIL;
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// iTVPSimpleImageProvider implementation
//---------------------------------------------------------------------------
tjs_error tTVPSimpleImageProvider::LoadImage(
    /*in*/ const tjs_char* name,
    /*in*/ tjs_int bpp,
    /*in*/ tjs_uint32 key,
    /*in*/ tjs_uint w,
    /*in*/ tjs_uint h,
    /*out*/ iTVPScanLineProvider** scpro)
{
    if (bpp != 8 && bpp != 32)
        return TJS_E_FAIL; // invalid bitmap color depth

    tTVPBaseTexture* bitmap = new tTVPBaseTexture(TVPGetInitialBitmap());

    try
    {
        TVPLoadGraphic(bitmap, name, key, w, h, bpp == 8 ? glmGrayscale : glmNormal);
    }
    catch (...)
    {
        delete bitmap;
        return TJS_E_FAIL;
    }

    if (scpro)
        *scpro = new tTVPScanLineProviderForBaseBitmap(bitmap, true);

    return TJS_S_OK;
}
tTVPSimpleImageProvider TVPSimpleImageProvider;
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// iTVPScanLineProvider implementation for image provider ( holds tTVPBaseBitmap )
//---------------------------------------------------------------------------
tTVPScanLineProviderForBaseBitmap::tTVPScanLineProviderForBaseBitmap(iTVPBaseBitmap* bmp, bool own)
{
    RefCount = 1;
    Own = own;
    Bitmap = bmp;
}
//---------------------------------------------------------------------------
tTVPScanLineProviderForBaseBitmap::~tTVPScanLineProviderForBaseBitmap()
{
    if (Own)
        if (Bitmap)
            delete Bitmap;
}
//---------------------------------------------------------------------------
void tTVPScanLineProviderForBaseBitmap::Attach(iTVPBaseBitmap* bmp)
{
    // attach bitmap
    Bitmap = bmp;
}
//---------------------------------------------------------------------------
tjs_error tTVPScanLineProviderForBaseBitmap::AddRef()
{
    RefCount++;
    return TJS_S_OK;
}
//---------------------------------------------------------------------------
tjs_error tTVPScanLineProviderForBaseBitmap::Release()
{
    if (RefCount == 1)
        delete this;
    else
        RefCount--;
    return TJS_S_OK;
}
//---------------------------------------------------------------------------
tjs_error tTVPScanLineProviderForBaseBitmap::GetWidth(/*in*/ tjs_int* width)
{
    if (width)
        *width = Bitmap->GetWidth();
    return TJS_S_OK;
}
//---------------------------------------------------------------------------
tjs_error tTVPScanLineProviderForBaseBitmap::GetHeight(/*in*/ tjs_int* height)
{
    if (height)
        *height = Bitmap->GetHeight();
    return TJS_S_OK;
}

tjs_error tTVPScanLineProviderForBaseBitmap::GetPixelFormat(
    /*out*/ tjs_int* bpp)
{
    *bpp = Bitmap->GetBPP();
    return TJS_S_OK;
}

tjs_error tTVPScanLineProviderForBaseBitmap::GetPitchBytes(/*out*/ tjs_int* pitch)
{
    *pitch = Bitmap->GetPitchBytes();
    return TJS_S_OK;
}
tjs_error tTVPScanLineProviderForBaseBitmap::GetScanLine(/*in*/ tjs_int line,
                                                         /*out*/ const void** scanline)
{
    *scanline = Bitmap->GetScanLine(line);
    return TJS_S_OK;
}
tjs_error tTVPScanLineProviderForBaseBitmap::GetScanLineForWrite(
    /*in*/ tjs_int line,
    /*out*/ void** scanline)
{
    *scanline = Bitmap->GetScanLineForWrite(line);
    return TJS_S_OK;
}

iTVPTexture2D* tTVPScanLineProviderForBaseBitmap::GetTexture()
{
    return Bitmap->GetTexture();
}

iTVPTexture2D* tTVPScanLineProviderForBaseBitmap::GetTextureForRender()
{
    return Bitmap->GetTextureForRender(false, nullptr);
}

//---------------------------------------------------------------------------
// transition handler management
//---------------------------------------------------------------------------
typedef tTJSRefHolder<iTVPTransHandlerProvider> tTVPTransHandlerProviderHolder;
static tTJSHashTable<ttstr, tTVPTransHandlerProviderHolder> TVPTransHandlerProviders;
bool TVPTransHandlerProviderInit = true;
//---------------------------------------------------------------------------
void TVPAddTransHandlerProvider(iTVPTransHandlerProvider* pro)
{
    // add transition handler provider to the system

    // retrieve transition name
    const tjs_char* cname;
    if (TJS_FAILED(pro->GetName(&cname)))
        TVPThrowExceptionMessage(TVPTransHandlerError,
                                 TJS_N("tTVPTransHandlerProvider::GetName failed"));
    ttstr name = cname;
    tTVPTransHandlerProviderHolder* p = TVPTransHandlerProviders.Find(name);
    if (p)
        TVPThrowExceptionMessage(TVPTransAlreadyRegistered, name);

    tTVPTransHandlerProviderHolder holder(pro);
    TVPTransHandlerProviders.Add(name, holder);
}
//---------------------------------------------------------------------------
void TVPRemoveTransHandlerProvider(iTVPTransHandlerProvider* pro)
{
    // remove transition handler provider from the system

    // retrieve transition name
    const tjs_char* cname;
    if (TJS_FAILED(pro->GetName(&cname)))
        TVPThrowExceptionMessage(TVPTransHandlerError,
                                 TJS_N("tTVPTransHandlerProvider::GetName failed"));
    ttstr name = cname;

    TVPTransHandlerProviders.Delete(name);
}
//---------------------------------------------------------------------------
static void TVPRegisterDefaultTransHandlerProvider();
iTVPTransHandlerProvider* TVPFindTransHandlerProvider(const ttstr& name)
{
    if (TVPTransHandlerProviderInit)
    {
        // we assume that transition that has the same name as the other does
        // not exist
        TVPRegisterDefaultTransHandlerProvider();
        TVPTransHandlerProviderInit = false;
    }

    tTVPTransHandlerProviderHolder* holder = TVPTransHandlerProviders.Find(name);
    if (!holder)
    {
        static bool showed = false;
        if (!showed)
        {
            TVPShowSimpleMessageBox(TVPFormatMessage(TVPCannotFindTransHander, name),
                                    TJS_N("Warning"));
            showed = true;
        }
        holder = TVPTransHandlerProviders.Find(TJS_N("crossfade"));
    }

    iTVPTransHandlerProvider* pro = holder->GetObjectNoAddRef();
    pro->AddRef();
    return pro;
}
//---------------------------------------------------------------------------
static void TVPClearTransHandlerProvider()
{
    // called at system shutdown
    TVPTransHandlerProviders.Clear();
    TVPTransHandlerProviderInit = true;
}
static tTVPAtExit TVPClearTransHandlerProviderAtExit(TVP_ATEXIT_PRI_SHUTDOWN,
                                                     TVPClearTransHandlerProvider);
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTVPTransBlender : 转场混合门面
//
// 全部混合操作统一经当前渲染管理器执行，不做 CPU/GPU 后端假设：
//   - 默认软件 RenderManager：RenderMethod + OperateRect（krkrz 原版语义）；
//   - 插件注入的 GPU RenderManager：同一套 GetRenderMethod + OperateRect 契约，
//     由注入方实现转场混合方法。
// 渲染方法按"渲染管理器实例"缓存，切换渲染管理器（TVPSetRenderManager）后
// 自动重新解析，避免旧实现的函数级 static 缓存跨管理器失效的问题。
//---------------------------------------------------------------------------
class tTVPTransBlender
{
    struct tTVPTransMethodSet
    {
        iTVPRenderManager* Manager = nullptr;
        iTVPRenderMethod* CrossFade = nullptr;  // ConstAlphaBlend_SD
        iTVPRenderMethod* CrossFadeD = nullptr; // ConstAlphaBlend_SD_d
        iTVPRenderMethod* CrossFadeA = nullptr; // ConstAlphaBlend_SD_a
        iTVPRenderMethod* UnivTrans = nullptr;  // UnivTransBlend
        iTVPRenderMethod* UnivTransD = nullptr; // UnivTransBlend_d
        iTVPRenderMethod* UnivTransA = nullptr; // UnivTransBlend_a
        iTVPRenderMethod* Copy = nullptr;       // Copy
        int OpaID = -1;
        int PhaseID = -1;
        int VagueID = -1;

        void Ensure(iTVPRenderManager* mgr)
        {
            if (Manager == mgr)
                return;
            Manager = mgr;
            CrossFade = Require(mgr->GetRenderMethod("ConstAlphaBlend_SD"), "ConstAlphaBlend_SD");
            CrossFadeD =
                Require(mgr->GetRenderMethod("ConstAlphaBlend_SD_d"), "ConstAlphaBlend_SD_d");
            CrossFadeA =
                Require(mgr->GetRenderMethod("ConstAlphaBlend_SD_a"), "ConstAlphaBlend_SD_a");
            UnivTrans = Require(mgr->GetRenderMethod("UnivTransBlend"), "UnivTransBlend");
            UnivTransD = Require(mgr->GetRenderMethod("UnivTransBlend_d"), "UnivTransBlend_d");
            UnivTransA = Require(mgr->GetRenderMethod("UnivTransBlend_a"), "UnivTransBlend_a");
            Copy = Require(mgr->GetRenderMethod("Copy"), "Copy");
            OpaID = CrossFade->EnumParameterID("opacity");
            PhaseID = UnivTrans->EnumParameterID("phase");
            VagueID = UnivTrans->EnumParameterID("vague");
        }

        static iTVPRenderMethod* Require(iTVPRenderMethod* m, const char* name)
        {
            if (!m)
                TVPThrowExceptionMessage(TVPTransHandlerError,
                                         ttstr(TJS_N("transition blend method \"")) + name +
                                             TJS_N("\" is not supported by current render manager"));
            return m;
        }
    };

    tTVPTransMethodSet Methods;

    void Ensure() { Methods.Ensure(TVPGetRenderManager()); }

    static iTVPRenderMethod* PickMethod(tTVPLayerType layertype,
                                        iTVPRenderMethod* normal,
                                        iTVPRenderMethod* alpha,
                                        iTVPRenderMethod* addalpha)
    {
        if (TVPIsTypeUsingAlpha(layertype))
            return alpha;
        if (TVPIsTypeUsingAddAlpha(layertype))
            return addalpha;
        return normal;
    }

public:
    iTVPRenderManager* GetManager()
    {
        Ensure();
        return Methods.Manager;
    }

    // 交叉淡化（phase: 0..255）
    void CrossFade(iTVPScanLineProvider* dest,
                   tjs_int destleft,
                   tjs_int desttop,
                   iTVPScanLineProvider* src1,
                   tjs_int src1left,
                   tjs_int src1top,
                   iTVPScanLineProvider* src2,
                   tjs_int src2left,
                   tjs_int src2top,
                   tjs_int width,
                   tjs_int height,
                   tjs_int phase,
                   tTVPLayerType layertype)
    {
        Ensure();
        iTVPRenderMethod* method =
            PickMethod(layertype, Methods.CrossFade, Methods.CrossFadeD, Methods.CrossFadeA);
        method->SetParameterOpa(Methods.OpaID, phase);

        tRenderTexRectArray::Element src_tex[] = {
            tRenderTexRectArray::Element(src1->GetTexture(),
                                         tTVPRect(src1left, src1top, src1left + width,
                                                  src1top + height)),
            tRenderTexRectArray::Element(src2->GetTexture(),
                                         tTVPRect(src2left, src2top, src2left + width,
                                                  src2top + height))};
        Methods.Manager->OperateRect(method, dest->GetTextureForRender(), nullptr,
                                     tTVPRect(destleft, desttop, destleft + width,
                                              desttop + height),
                                     tRenderTexRectArray(src_tex));
    }

    // 通用转场（rule 图驱动），逐帧设置相位
    void SetUnivTransPhase(tTVPLayerType layertype, tjs_int phase, tjs_int vague)
    {
        Ensure();
        iTVPRenderMethod* method =
            PickMethod(layertype, Methods.UnivTrans, Methods.UnivTransD, Methods.UnivTransA);
        method->SetParameterInt(Methods.VagueID, vague);
        method->SetParameterInt(Methods.PhaseID, phase);
    }

    void Universal(iTVPScanLineProvider* dest,
                   tjs_int destleft,
                   tjs_int desttop,
                   iTVPScanLineProvider* src1,
                   tjs_int src1left,
                   tjs_int src1top,
                   iTVPScanLineProvider* src2,
                   tjs_int src2left,
                   tjs_int src2top,
                   iTVPScanLineProvider* rule,
                   tjs_int ruleleft,
                   tjs_int ruletop,
                   tjs_int width,
                   tjs_int height,
                   tTVPLayerType layertype)
    {
        Ensure();
        iTVPRenderMethod* method =
            PickMethod(layertype, Methods.UnivTrans, Methods.UnivTransD, Methods.UnivTransA);

        tRenderTexRectArray::Element src_tex[] = {
            tRenderTexRectArray::Element(src1->GetTexture(),
                                         tTVPRect(src1left, src1top, src1left + width,
                                                  src1top + height)),
            tRenderTexRectArray::Element(src2->GetTexture(),
                                         tTVPRect(src2left, src2top, src2left + width,
                                                  src2top + height)),
            tRenderTexRectArray::Element(
                rule->GetTexture(),
                tTVPRect(ruleleft, ruletop, ruleleft + width, ruletop + height))};
        Methods.Manager->OperateRect(method, dest->GetTextureForRender(), nullptr,
                                     tTVPRect(destleft, desttop, destleft + width,
                                              desttop + height),
                                     tRenderTexRectArray(src_tex));
    }

    // 矩形拷贝（src==dest 且区域重叠时也安全；32bpp）
    void CopyRect(iTVPScanLineProvider* destimg,
                  tjs_int x,
                  tjs_int y,
                  iTVPScanLineProvider* srcimg,
                  const tTVPRect& srcrect)
    {
        Ensure();
        tRenderTexRectArray::Element src_tex[] = {
            tRenderTexRectArray::Element(srcimg->GetTexture(), srcrect)};

        Methods.Manager->OperateRect(Methods.Copy, destimg->GetTextureForRender(), nullptr,
                                     tTVPRect(x, y, x + srcrect.get_width(),
                                              y + srcrect.get_height()),
                                     tRenderTexRectArray(src_tex));
    }
};
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// Cross fade transition handler
//---------------------------------------------------------------------------
class tTVPCrossFadeTransHandler : public iTVPDivisibleTransHandler
{
    tjs_int RefCount;

protected:
    iTVPSimpleOptionProvider* Options;
    tTVPLayerType DestLayerType;
    tjs_uint64 StartTick;
    tjs_uint64 Time; // time during transition
    bool First;

    tjs_int PhaseMax;
    tjs_int Phase; // current phase (0 thru PhaseMax)

    tTVPTransBlender Blender;

#ifdef TVP_TRANS_SHOW_FPS
    tjs_int Count;
    tjs_uint32 ProcessTick;
    tjs_uint32 ProcessTime;
    tjs_uint32 BlendTick;
    tjs_uint32 BlendTime;
#endif

public:
    tTVPCrossFadeTransHandler(iTVPSimpleOptionProvider* options,
                              tTVPLayerType layertype,
                              tjs_uint64 time,
                              tjs_int phasemax = 255)
    {
        RefCount = 1;
        DestLayerType = layertype;
        Options = options;
        if (Options)
            Options->AddRef();
        Time = time;
        PhaseMax = phasemax;
#ifdef TVP_TRANS_SHOW_FPS
        Count = 0;
        ProcessTime = 0;
        BlendTime = 0;
#endif
        First = true;
    }

    virtual ~tTVPCrossFadeTransHandler()
    {
        if (Options)
            Options->Release();
    }

    tjs_error AddRef()
    {
        RefCount++;
        return TJS_S_OK;
    }

    tjs_error Release()
    {
        if (RefCount == 1)
            delete this;
        else
            RefCount--;
        return TJS_S_OK;
    }

    tjs_error SetOption(
        /*in*/ iTVPSimpleOptionProvider* options // option provider
    )
    {
        if (Options)
            Options->Release();
        Options = options;
        if (Options)
            Options->AddRef();
        return TJS_S_OK;
    }

    tjs_error StartProcess(tjs_uint64 tick);
    tjs_error EndProcess();
    tjs_error Process(
        /*in,out*/ tTVPDivisibleData* data);
    tjs_error MakeFinalImage(
        /*in,out*/ iTVPScanLineProvider** dest,
        /*in*/ iTVPScanLineProvider* src1,
        /*in*/ iTVPScanLineProvider* src2);

    virtual void Blend(tTVPDivisibleData* data);
};
//---------------------------------------------------------------------------

tTVPCrossFadeTransHandlerProvider::tTVPCrossFadeTransHandlerProvider()
{
    RefCount = 1;
}

tTVPCrossFadeTransHandlerProvider::~tTVPCrossFadeTransHandlerProvider()
{
}

tjs_error tTVPCrossFadeTransHandlerProvider::AddRef()
{
    RefCount++;
    return TJS_S_OK;
}

tjs_error tTVPCrossFadeTransHandlerProvider::Release()
{
    if (RefCount == 1)
        delete this;
    else
        RefCount--;
    return TJS_S_OK;
}

tjs_error tTVPCrossFadeTransHandlerProvider::GetName(/*out*/ const tjs_char** name)
{
    if (name)
    {
        *name = TJS_N("crossfade");
        return TJS_S_OK;
    }
    else
    {
        return TJS_E_FAIL;
    }
}

tjs_error tTVPCrossFadeTransHandlerProvider::StartTransition(
    /*in*/ iTVPSimpleOptionProvider* options,
    /* option provider */ /*in*/ iTVPSimpleImageProvider* imagepro,
    /* image provider */ /*in*/ tTVPLayerType layertype,
    /* destination layer type */ /*in*/ tjs_uint src1w,
    tjs_uint src1h,
    /* source 1 size */ /*in*/ tjs_uint src2w,
    tjs_uint src2h,
    /* source 2 size */ /*out*/ tTVPTransType* type,
    /* transition type */ /*out*/ tTVPTransUpdateType* updatetype,
    /* update typwe */ /*out*/ iTVPBaseTransHandler** handler /* transition handler */)
{
    if (type)
        *type = ttExchange; // transition type : exchange
    if (updatetype)
        *updatetype = tutDivisibleFade;
    // update type : divisible fade
    if (!handler)
        return TJS_E_FAIL;
    if (!options)
        return TJS_E_FAIL;

    if (src1w != src2w || src1h != src2h)
        TVPThrowExceptionMessage(TVPTransitionLayerSizeMismatch,
                                 ttstr((tjs_int)src2w) + TJS_N("x") + ttstr((tjs_int)src2h),
                                 ttstr((tjs_int)src1w) + TJS_N("x") + ttstr((tjs_int)src1h));

    *handler = GetTransitionObject(options, imagepro, layertype, src1w, src1h, src2w, src2h);

    return TJS_S_OK;
}

iTVPBaseTransHandler* tTVPCrossFadeTransHandlerProvider::GetTransitionObject(
    /*in*/ iTVPSimpleOptionProvider* options,
    /* option provider */ /*in*/ iTVPSimpleImageProvider* imagepro,
    /* image provider */ /*in*/ tTVPLayerType layertype,
    /*in*/ tjs_uint src1w,
    tjs_uint src1h,
    /* source 1 size */ /*in*/ tjs_uint src2w,
    tjs_uint src2h) // source 2 size
{
    tjs_int64 time;
    tjs_error er = options->GetAsNumber(TJS_N("time"), (tjs_int64*)&time);
    if (TJS_FAILED(er))
        TVPThrowExceptionMessage(TVPSpecifyOption, TJS_N("time"));
    if (time < 2)
        time = 2; // too small time may cause problem

    return (iTVPBaseTransHandler*)(new tTVPCrossFadeTransHandler(options, layertype, time));
}

//---------------------------------------------------------------------------
tjs_error tTVPCrossFadeTransHandler::StartProcess(tjs_uint64 tick)
{
    // notifies starting of the update
    if (First)
    {
        First = false;
        StartTick = tick;
    }

    // compute phase ( 0 thru 255 )
    Phase = static_cast<tjs_int>((tick - StartTick) * PhaseMax / Time);
    if (Phase >= PhaseMax)
        Phase = PhaseMax;

#ifdef TVP_TRANS_SHOW_FPS
    if ((tick - StartTick) >= Time / 2)
    {
        if (Count)
        {
            TVPAddLog(TJS_N("update count : ") + ttstr(Count));
            TVPAddLog(TJS_N("trans time : ") + ttstr((tjs_int)(tick - StartTick)));
            TVPAddLog(TJS_N("process time : ") + ttstr((tjs_int)ProcessTime));
            TVPAddLog(TJS_N("process time / trans time (%) : ") +
                      ttstr((tjs_int)(ProcessTime * 100 / (tick - StartTick))));
            TVPAddLog(TJS_N("blend time / trans time (%) : ") +
                      ttstr((tjs_int)(BlendTime * 100 / (tick - StartTick))));
            tjs_int avgtime;
            avgtime = ProcessTime / Count;
            TVPAddLog(TJS_N("process time / update count : ") + ttstr(avgtime));
            tjs_int fps = Count * 1000 / (tick - StartTick);
            TVPAddLog(TJS_N("fps : ") + ttstr(fps));

            Count = 0;
        }
    }
    else
    {
        Count++;
    }

    ProcessTick = TVPGetRoughTickCount32();
#endif

    return TJS_S_TRUE;
}
//---------------------------------------------------------------------------
tjs_error tTVPCrossFadeTransHandler::EndProcess()
{
    // notifies ending of the update
#ifdef TVP_TRANS_SHOW_FPS
    ProcessTime += TVPGetRoughTickCount32() - ProcessTick;
#endif

    if (Phase >= PhaseMax)
        return TJS_S_FALSE;
    return TJS_S_TRUE;
}
//---------------------------------------------------------------------------
tjs_error tTVPCrossFadeTransHandler::Process(
    /*in,out*/ tTVPDivisibleData* data)
{
    // process divided region of the entire layer bitmap

#ifdef TVP_TRANS_SHOW_FPS
    BlendTick = TVPGetRoughTickCount32();
#endif

    if (Phase == 0)
    {
        // completely source 1
        data->Dest = data->Src1;
        data->DestLeft = data->Src1Left;
        data->DestTop = data->Src1Top;
    }
    else if (Phase == PhaseMax)
    {
        // completety source 2
        data->Dest = data->Src2;
        data->DestLeft = data->Src2Left;
        data->DestTop = data->Src2Top;
    }
    else
    {
        // blend
        Blend(data);
    }

#ifdef TVP_TRANS_SHOW_FPS
    BlendTime += TVPGetRoughTickCount32() - BlendTick;
#endif

    return TJS_S_OK;
}
//---------------------------------------------------------------------------
void tTVPCrossFadeTransHandler::Blend(tTVPDivisibleData* data)
{
    // blend via the current render manager (CPU/GPU path)
    Blender.CrossFade(data->Dest, data->DestLeft, data->DestTop, data->Src1, data->Src1Left,
                      data->Src1Top, data->Src2, data->Src2Left, data->Src2Top, data->Width,
                      data->Height, Phase, DestLayerType);
}
//---------------------------------------------------------------------------
tjs_error tTVPCrossFadeTransHandler::MakeFinalImage(
    /*in,out*/ iTVPScanLineProvider** dest,
    /*in*/ iTVPScanLineProvider* src1,
    /*in*/ iTVPScanLineProvider* src2)
{
    // final image is the source2 bitmap
    *dest = src2;

    return TJS_S_OK;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// universal transition handler
//---------------------------------------------------------------------------
class tTVPUniversalTransHandler : public tTVPCrossFadeTransHandler
{
    typedef tTVPCrossFadeTransHandler inherited;

    tjs_int Vague;
    iTVPScanLineProvider* Rule;

public:
    tTVPUniversalTransHandler(iTVPSimpleOptionProvider* options,
                              tTVPLayerType destlayertype,
                              tjs_uint64 time,
                              tjs_int vague,
                              iTVPScanLineProvider* rule)
      : tTVPCrossFadeTransHandler(options, destlayertype, time, 255 + vague)
    {
        Vague = vague;
        Rule = rule;
        Rule->AddRef();
    }

    ~tTVPUniversalTransHandler() { Rule->Release(); }

    tjs_error StartProcess(tjs_uint64 tick);
    // tTVPCrossFadeTransHandler::blend override
    void Blend(tTVPDivisibleData* data);
    // tTVPCrossFadeTransHandler::blend override
};
//---------------------------------------------------------------------------
class tTVPUniversalTransHandlerProvider : public tTVPCrossFadeTransHandlerProvider
{
public:
    tTVPUniversalTransHandlerProvider() : tTVPCrossFadeTransHandlerProvider() {}
    virtual ~tTVPUniversalTransHandlerProvider(){};

    tjs_error GetName(
        /*out*/ const tjs_char** name)
    {
        if (name)
        {
            *name = TJS_N("universal");
            return TJS_S_OK;
        }
        else
        {
            return TJS_E_FAIL;
        }
    }

    virtual iTVPBaseTransHandler* GetTransitionObject(
        /*in*/ iTVPSimpleOptionProvider* options, // option provider
        /*in*/ iTVPSimpleImageProvider* imagepro, // image provider
        /*in*/ tTVPLayerType layertype,
        /*in*/ tjs_uint src1w,
        tjs_uint src1h, // source 1 size
        /*in*/ tjs_uint src2w,
        tjs_uint src2h) // source 2 size
    {
        tjs_error er;

        // retrieve "time" option
        tjs_int64 time;
        er = options->GetAsNumber(TJS_N("time"), (tjs_int64*)&time);
        if (TJS_FAILED(er))
            TVPThrowExceptionMessage(TVPSpecifyOption, TJS_N("time"));
        if (time < 2)
            time = 2; // too small time may cause problem

        // retrieve "vague" option
        tjs_int64 vague;
        er = options->GetAsNumber(TJS_N("vague"), (tjs_int64*)&vague);
        if (TJS_FAILED(er))
            vague = 64;

        // retrieve "rule" option and load it as an image
        const tjs_char* rulename;
        er = options->GetAsString(TJS_N("rule"), &rulename);
        if (TJS_FAILED(er))
            TVPThrowExceptionMessage(TVPSpecifyOption, TJS_N("rule"));

        iTVPScanLineProvider* scpro;
        er = imagepro->LoadImage(rulename, 8, 0x02ffffff, src1w, src1h, &scpro);
        if (TJS_FAILED(er))
            TVPThrowExceptionMessage(TVPCannotLoadRuleGraphic, rulename);

        // create a transition object
        iTVPBaseTransHandler* ret;
        try
        {
            ret = (iTVPBaseTransHandler*)(new tTVPUniversalTransHandler(
                options, layertype, time, static_cast<tjs_int>(vague), scpro));
        }
        catch (...)
        {
            scpro->Release();
            throw;
        }
        scpro->Release();

        return ret;
    }
};
//---------------------------------------------------------------------------
tjs_error tTVPUniversalTransHandler::StartProcess(tjs_uint64 tick)
{
    tjs_error er;
    er = inherited::StartProcess(tick);
    if (TJS_FAILED(er))
        return er;

    // start one frame of the transition（按当前渲染管理器设置相位/羽化）
    Blender.SetUnivTransPhase(DestLayerType, Phase, Vague);
    return er;
}
//---------------------------------------------------------------------------
void tTVPUniversalTransHandler::Blend(tTVPDivisibleData* data)
{
    // blend the image according with the rule graphic
    Blender.Universal(data->Dest, data->DestLeft, data->DestTop, data->Src1, data->Src1Left,
                      data->Src1Top, data->Src2, data->Src2Left, data->Src2Top, Rule, data->Left,
                      data->Top, data->Width, data->Height, DestLayerType);
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// scroll transition handler
//---------------------------------------------------------------------------
class tTVPScrollTransHandler : public tTVPCrossFadeTransHandler
{
    typedef tTVPCrossFadeTransHandler inherited;

    tTVPScrollTransFrom From;
    tTVPScrollTransStay Stay;

public:
    tTVPScrollTransHandler(iTVPSimpleOptionProvider* options,
                           tTVPLayerType layertype,
                           tjs_uint64 time,
                           tTVPScrollTransFrom from,
                           tTVPScrollTransStay stay,
                           tjs_int maxphase)
      : tTVPCrossFadeTransHandler(options, layertype, time, maxphase)
    {
        From = from;
        Stay = stay;
    }

    ~tTVPScrollTransHandler() {}

    void Blend(tTVPDivisibleData* data);
    // tTVPCrossFadeTransHandler::blend override
};
//---------------------------------------------------------------------------
class tTVPScrollTransHandlerProvider : public tTVPCrossFadeTransHandlerProvider
{
public:
    tTVPScrollTransHandlerProvider() : tTVPCrossFadeTransHandlerProvider() {}
    virtual ~tTVPScrollTransHandlerProvider(){};

    tjs_error GetName(
        /*out*/ const tjs_char** name)
    {
        if (name)
        {
            *name = TJS_N("scroll");
            return TJS_S_OK;
        }
        else
        {
            return TJS_E_FAIL;
        }
    }

    tjs_error StartTransition(
        /*in*/ iTVPSimpleOptionProvider* options, // option provider
        /*in*/ iTVPSimpleImageProvider* imagepro, // image provider
        /*in*/ tTVPLayerType layertype,           // destination layer type
        /*in*/ tjs_uint src1w,
        tjs_uint src1h, // source 1 size
        /*in*/ tjs_uint src2w,
        tjs_uint src2h,                          // source 2 size
        /*out*/ tTVPTransType* type,             // transition type
        /*out*/ tTVPTransUpdateType* updatetype, // update typwe
        /*out*/ iTVPBaseTransHandler** handler   // transition handler
    )
    {
        tjs_error er = tTVPCrossFadeTransHandlerProvider::StartTransition(
            options, imagepro, layertype, src1w, src1h, src2w, src2h, type, updatetype, handler);

        if (TJS_SUCCEEDED(er))
            *updatetype = tutDivisible;

        return er;
    }

    virtual iTVPBaseTransHandler* GetTransitionObject(
        /*in*/ iTVPSimpleOptionProvider* options, // option provider
        /*in*/ iTVPSimpleImageProvider* imagepro, // image provider
        /*in*/ tTVPLayerType layertype,
        /*in*/ tjs_uint src1w,
        tjs_uint src1h, // source 1 size
        /*in*/ tjs_uint src2w,
        tjs_uint src2h) // source 2 size
    {
        tjs_error er;
        tjs_int64 value;

        // retrieve "time" option
        tjs_int64 time;
        er = options->GetAsNumber(TJS_N("time"), &time);
        if (TJS_FAILED(er))
            TVPThrowExceptionMessage(TVPSpecifyOption, TJS_N("time"));
        if (time < 2)
            time = 2; // too small time may cause problem

        // retrieve "from" option
        tTVPScrollTransFrom from;
        er = options->GetAsNumber(TJS_N("from"), &value);
        if (TJS_FAILED(er))
            from = sttLeft;
        else
            from = (tTVPScrollTransFrom)value;

        // retrieve "stay" option
        tTVPScrollTransStay stay;
        er = options->GetAsNumber(TJS_N("stay"), &value);
        if (TJS_FAILED(er))
            stay = ststNoStay;
        else
            stay = (tTVPScrollTransStay)value;

        // determine maximum phase count
        tjs_int maxphase = (from == sttLeft || from == sttRight) ? src1w : src2h;

        // create a transition object
        iTVPBaseTransHandler* ret;
        ret = (iTVPBaseTransHandler*)(new tTVPScrollTransHandler(options, layertype, time, from,
                                                                 stay, maxphase));
        return ret;
    }
};
//---------------------------------------------------------------------------
struct tTVPScrollDivisibleData : public tTVPDivisibleData
{
    void Blend(tTVPScrollTransFrom from, tTVPScrollTransStay stay, tjs_int phase);
};
//---------------------------------------------------------------------------
void tTVPScrollTransHandler::Blend(tTVPDivisibleData* data)
{
    ((tTVPScrollDivisibleData*)data)->Blend(From, Stay, Phase);
}
//---------------------------------------------------------------------------
void tTVPScrollDivisibleData::Blend(tTVPScrollTransFrom from,
                                    tTVPScrollTransStay stay,
                                    tjs_int phase)
{
    // scroll the image
    tjs_int imagewidth;
    tjs_int imageheight;
    Src1->GetWidth(&imagewidth);
    Src1->GetHeight(&imageheight);
    tjs_int src1left = 0;
    tjs_int src1top = 0;
    tjs_int src2left = 0;
    tjs_int src2top = 0;

    switch (from)
    {
        case sttLeft:
            if (stay == ststNoStay)
                src1left = phase, src2left = phase - imagewidth;
            else if (stay == ststStayDest)
                src2left = phase - imagewidth;
            else if (stay == ststStaySrc)
                src1left = phase;
            break;
        case sttTop:
            if (stay == ststNoStay)
                src1top = phase, src2top = phase - imageheight;
            else if (stay == ststStayDest)
                src2top = phase - imageheight;
            else if (stay == ststStaySrc)
                src1top = phase;
            break;
        case sttRight:
            if (stay == ststNoStay)
                src1left = -phase, src2left = imagewidth - phase;
            else if (stay == ststStayDest)
                src2left = imagewidth - phase;
            else if (stay == ststStaySrc)
                src1left = -phase;
            break;
        case sttBottom:
            if (stay == ststNoStay)
                src1top = -phase, src2top = imageheight - phase;
            else if (stay == ststStayDest)
                src2top = imageheight - phase;
            else if (stay == ststStaySrc)
                src1top = -phase;
            break;
    }

    tTVPRect rdest(Left, Top, Width + Left, Height + Top);
    tTVPRect rs1(src1left, src1top, imagewidth + src1left, imageheight + src1top);
    tTVPRect rs2(src2left, src2top, imagewidth + src2left, imageheight + src2top);
    if (stay == ststNoStay)
    {
        // both layers have no priority than another.
        // nothing to do.
    }
    else if (stay == ststStayDest)
    {
        // Src2 has priority.
        if (from == sttLeft || from == sttRight)
        {
            if (rs2.right >= rs1.left && rs2.right < rs1.right)
                rs1.left = rs2.right;
            if (rs2.left >= rs1.left && rs2.left < rs1.right)
                rs1.right = rs2.left;
        }
        else
        {
            if (rs2.bottom >= rs1.top && rs2.bottom < rs1.bottom)
                rs1.top = rs2.bottom;
            if (rs2.top >= rs1.top && rs2.top < rs1.bottom)
                rs1.bottom = rs2.top;
        }
    }
    else if (stay == ststStaySrc)
    {
        // Src1 has priority.
        if (from == sttLeft || from == sttRight)
        {
            if (rs1.right >= rs2.left && rs1.right < rs2.right)
                rs2.left = rs1.right;
            if (rs1.left >= rs2.left && rs1.left < rs2.right)
                rs2.right = rs1.left;
        }
        else
        {
            if (rs1.bottom >= rs2.top && rs1.bottom < rs2.bottom)
                rs2.top = rs1.bottom;
            if (rs1.top >= rs2.top && rs1.top < rs2.bottom)
                rs2.bottom = rs1.top;
        }
    }

    // copy to destination image
    tTVPTransBlender blender;
    tTVPRect d;
    if (TVPIntersectRect(&d, rdest, rs1))
    {
        tjs_int dl = d.left - Left + DestLeft, dt = d.top - Top + DestTop;
        d.add_offsets(-src1left, -src1top);
        blender.CopyRect(Dest, dl, dt, Src1, d);
    }
    if (TVPIntersectRect(&d, rdest, rs2))
    {
        tjs_int dl = d.left - Left + DestLeft, dt = d.top - Top + DestTop;
        d.add_offsets(-src2left, -src2top);
        blender.CopyRect(Dest, dl, dt, Src2, d);
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
static void TVPRegisterDefaultTransHandlerProvider()
{
    // register default internal transition handler providers.

    iTVPTransHandlerProvider* prov;

    prov = new tTVPCrossFadeTransHandlerProvider();
    TVPAddTransHandlerProvider(prov);
    prov->Release();

    prov = new tTVPUniversalTransHandlerProvider();
    TVPAddTransHandlerProvider(prov);
    prov->Release();

    prov = new tTVPScrollTransHandlerProvider();
    TVPAddTransHandlerProvider(prov);
    prov->Release();
}
//---------------------------------------------------------------------------
