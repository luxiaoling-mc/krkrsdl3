#include "D3DEmotePlayer.h"

#include "emoteplayer/emoteplayerclass.h"
#include "Platform.h"
#include "TVPDebug.h"
#include "TVPStorage.h"

#include <algorithm>
#include <set>
#include <vector>

//---------------------------------------------------------------------------
// D3DEmotePlayer 实现
//---------------------------------------------------------------------------
namespace drawdevice
{

// emoteplayer::ResourceManager 与 EmotePlayer 的包装（避免头文件耦合）。
// ResourceManager 全局共享以支持不同 emote 文件之间互相引用 source/motion；
// PlayerImpl 记录本播放器实际 load 过的文件，析构时逐个释放引用计数，
// 引用归零的 file 会从 ResourceManager 的 source/motion 索引中移除，避免下次同名冲突。
class PlayerImpl
{
public:
    emoteplayer::ResourceManager* RM = nullptr;
    emoteplayer::EmotePlayer* Player = nullptr;

    PlayerImpl()
    {
        RM = new emoteplayer::ResourceManager(nullptr, 0);
        Player = new emoteplayer::EmotePlayer(RM);
    }
    PlayerImpl(emoteplayer::ResourceManager* shared, emoteplayer::EmotePlayer* sharedP)
    {
        RM = shared;
        RM->AddRef();
        Player = sharedP;
        Player->AddRef();
    }
    ~PlayerImpl()
    {
        Player->Release();
        Player = nullptr;
        RM->Release();
        RM = nullptr;
    }
};

D3DEmotePlayer::D3DEmotePlayer(iTJSDispatch2* d3dlayer)
{
    Layer = ncbInstanceAdaptor<D3DLayer>::GetNativeInstance(d3dlayer);
    if (Layer)
        Device = Layer->Device;
    // 每实例独立动画状态机；资源缓存/跨文件索引由共享 ResourceManager 管理
    Impl = new PlayerImpl();
}

D3DEmotePlayer::D3DEmotePlayer(iTJSDispatch2* d3dlayer, emoteplayer::ResourceManager* sharedRM, emoteplayer::EmotePlayer* sharedPlayer)
{
    Layer = ncbInstanceAdaptor<D3DLayer>::GetNativeInstance(d3dlayer);
    if (Layer)
        Device = Layer->Device;
    Impl = (sharedRM && sharedPlayer) ? new PlayerImpl(sharedRM, sharedPlayer) : new PlayerImpl();
}

D3DEmotePlayer::~D3DEmotePlayer()
{
    delete Impl;
    Impl = nullptr;
}

tjs_error D3DEmotePlayer::load(tTJSVariant* result,
                          tjs_int numparams,
                          tTJSVariant** param,
                          D3DEmotePlayer* objthis)
{
    if (!objthis || !objthis->Impl || !objthis->Impl->Player || !objthis->Impl->RM ||
        !objthis->Device || !objthis->Device->GetBackend())
        return TJS_S_OK;
    ttstr tmpname;
    for (int i = 0; i < numparams; i++)
    {
        tmpname = *param[i];
        objthis->Impl->RM->load(tmpname);
    }
    try
    {
        objthis->Impl->Player->play("", 0);
        objthis->Animating = true;
    }
    
    catch (...)
    {
        TVPConsoleLog("D3DEmotePlayer::load failed");
    }
    return TJS_S_OK;
}

tTJSVariant D3DEmotePlayer::clone(iTJSDispatch2* newlayer)
{
    // 转场/建层时 D3DAffineSourceEmote.clone → _player.clone(newlayer)：
    // 为新 D3DLayer 创建一个状态延续的播放器（同一 motion、进度、变量），
    // 并返回对应的脚本对象（ncb 原生实例包装）。
    D3DEmotePlayer* np = new D3DEmotePlayer(newlayer, Impl->RM, Impl->Player);
    if (Impl && Impl->Player && np->Impl && np->Impl->Player)
    {
        try
        {
            emoteplayer::EmotePlayer* src = Impl->Player;
            emoteplayer::EmotePlayer* dst = np->Impl->Player;
            // clone 先给新播放器加载同一文件并增加引用计数，再恢复 motion/状态
            dst->set_motionKey(src->get_motionKey());
            tTJSString motion = src->get_motion();
            np->Impl->Player->play("", 0);
            // 播放进度/速度与变换状态
            dst->set_tickCount(src->get_tickCount());
            dst->set_speed(src->get_speed());
            dst->unserialize(src->serialize());
            // 变量复制（按当前 motion 的变量表枚举）
            tTJSVariant keys = src->get_variableKeys();
            if (keys.Type() == tvtObject)
            {
                iTJSDispatch2* arr = keys.AsObjectNoAddRef();
                tTJSVariant count;
                if (TJS_SUCCEEDED(arr->PropGet(0, TJS_N("count"), nullptr, &count, arr)))
                {
                    for (tjs_int i = 0; i < (tjs_int)count; i++)
                    {
                        tTJSVariant item;
                        if (TJS_SUCCEEDED(arr->PropGetByNum(0, i, &item, arr)))
                        {
                            tTJSString name(item);
                            dst->setVariable(name, src->getVariable(name));
                        }
                    }
                }
            }
        }
        catch (...)
        {
            // 状态复制失败不致命：新播放器至少能以默认状态播放同一 motion
        }
    }
    np->ShowFlag = ShowFlag;
    np->Smoothing = Smoothing;
    np->MeshDivisionRatio = MeshDivisionRatio;
    np->BustScale = BustScale;
    np->HairScale = HairScale;
    np->PartsScale = PartsScale;
    np->Animating = Animating;

    iTJSDispatch2* obj = ncbInstanceAdaptor<D3DEmotePlayer>::CreateAdaptor(np, false, true);
    if (!obj)
    {
        delete np;
        return tTJSVariant();
    }
    tTJSVariant v(obj, obj);
    obj->Release();
    return v;
}

void D3DEmotePlayer::skip()
{
    if (Impl && Impl->Player)
        Impl->Player->skip();
}

void D3DEmotePlayer::pass()
{
    if (Impl && Impl->Player)
        Impl->Player->pass();
}

void D3DEmotePlayer::progress(tjs_real frames)
{
    // 脚本按 60fps 帧数传入 → 换算毫秒
    if (Impl && Impl->Player)
    {
        Impl->Player->progress(frames * 1000.0 / 60.0);
        if (Layer && Device && Device->GetBackend())
        {
            if (!Layer->EmoteTarget)
                Layer->EmoteTarget = Device->GetBackend()->CreateTarget(Device->GetWidth(), Device->GetHeight());
            if (!Layer->EmoteMaskTarget)
                Layer->EmoteMaskTarget = Device->GetBackend()->CreateTarget(Device->GetWidth(), Device->GetHeight());
            DrawToTarget(Layer->EmoteTarget, Layer->EmoteMaskTarget);
        }
    }
}

void D3DEmotePlayer::setCoord(tjs_real x, tjs_real y, tjs_real z, tjs_real t)
{
    if (Impl && Impl->Player)
        Impl->Player->setCoord(x, y);
}

void D3DEmotePlayer::setRot(tjs_real angle, tjs_real time, tjs_real easing)
{
    if (Impl && Impl->Player)
        Impl->Player->setRotate(angle * 360.0 / (3.14159265358979 * 2.0));
}

void D3DEmotePlayer::setScale(tjs_real scale, tjs_real time, tjs_real easing)
{
    if (Impl && Impl->Player)
        Impl->Player->setScale(scale);
}

void D3DEmotePlayer::setColor(tjs_uint32 color, tjs_real time, tjs_real easing)
{
    if (Impl && Impl->Player)
        Impl->Player->setColor(color);
}

void D3DEmotePlayer::setVariable(tTJSString name, tjs_real value, tjs_real time, tjs_real accel)
{
    if (Impl && Impl->Player)
        Impl->Player->setVariable(name, value);
}

tjs_real D3DEmotePlayer::getVariable(tTJSString name)
{
    if (Impl && Impl->Player)
        return Impl->Player->getVariable(name);
    return 0;
}

bool D3DEmotePlayer::containsLabel(tTJSString label, tjs_real x, tjs_real y)
{
    if (Impl && Impl->Player)
    {
        // 调用方（脚本侧层）传入的 (x,y) 是经 revmtx 逆变换后的"层本地坐标"：
        //   x2 = revmtx.a*x + revmtx.c*y + revmtx.tx
        //   y2 = revmtx.b*x + revmtx.d*y + revmtx.ty
        // 而引擎 shape 判定区域的收集空间是虚拟屏设备像素（左上原点、y 向下），
        // 两者相差层的仿射变换，直接透传会导致判定区域与画面错位（层无变换时偏移半屏）。
        // 这里用 Layer->Matrix（脚本 setMatrix 存入的同一矩阵，即 revmtx 的正变换）
        // 把层本地坐标还原为 primary 中心坐标，再加半屏转成设备像素。
        // Matrix 与 revmtx 严格互逆，缩放/旋转/剪切层均正确；层无变换时退化为 x + W/2。
        float lx = (float)x, ly = (float)y;
        float wx = lx, wy = ly;
        if (Layer && Device)
        {
            wx = Layer->Matrix[0] * lx + Layer->Matrix[1] * ly + Layer->Matrix[12] +
                 Device->GetWidth() * 0.5f;
            wy = Layer->Matrix[4] * lx + Layer->Matrix[5] * ly + Layer->Matrix[13] +
                 Device->GetHeight() * 0.5f;
        }
        return Impl->Player->containsLabel(label, wx, wy);
    }
    return false;
}

bool D3DEmotePlayer::contains(tjs_real x, tjs_real y)
{
    if (Impl && Impl->Player)
    {
        // 与 containsLabel 相同的坐标空间换算（层本地 → 设备像素）
        float lx = (float)x, ly = (float)y;
        float wx = lx, wy = ly;
        if (Layer && Device)
        {
            wx = Layer->Matrix[0] * lx + Layer->Matrix[1] * ly + Layer->Matrix[12] +
                 Device->GetWidth() * 0.5f;
            wy = Layer->Matrix[4] * lx + Layer->Matrix[5] * ly + Layer->Matrix[13] +
                 Device->GetHeight() * 0.5f;
        }
        return Impl->Player->contains(wx, wy);
    }
    return false;
}

tjs_error D3DEmotePlayer::cb_contains(
    tTJSVariant* result, tjs_int numparams, tTJSVariant** param, D3DEmotePlayer* objthis)
{
    // contains(label, x, y)（带标签命中判定）/ contains(x, y) 两参分发
    if (objthis == nullptr)
        return TJS_E_FAIL;
    if (numparams >= 3 && param[0]->Type() != tvtReal)
    {
        if (result)
            *result = objthis->containsLabel(tTJSString(*param[0]), (tjs_real)*param[1],
                                             (tjs_real)*param[2]);
        return TJS_S_OK;
    }
    if (numparams < 2)
        return TJS_E_BADPARAMCOUNT;
    if (result)
        *result = objthis->contains((tjs_real)*param[0], (tjs_real)*param[1]);
    return TJS_S_OK;
}

void D3DEmotePlayer::startWind(tjs_real start, tjs_real goal, tjs_real speed, tjs_real powMin,
                               tjs_real powMax)
{
    if (Impl && Impl->Player)
        Impl->Player->startWind(start, goal, speed, powMin, powMax);
}

void D3DEmotePlayer::stopWind()
{
    if (Impl && Impl->Player)
        Impl->Player->stopWind();
}

void D3DEmotePlayer::playTimeline(tTJSString name, tjs_int flags)
{
    if (Impl && Impl->Player)
        Impl->Player->playTimeline(name, flags);
}

void D3DEmotePlayer::stopTimeline(tTJSString name)
{
    if (Impl && Impl->Player)
        Impl->Player->stopTimeline(name);
}

void D3DEmotePlayer::fadeOutTimeline(tTJSString name, tjs_real time, tjs_real easing)
{
    if (Impl && Impl->Player)
        Impl->Player->fadeOutTimeline(name, time, easing);
}

void D3DEmotePlayer::setTimelineBlendRatio(tTJSString name, tjs_real ratio, tjs_real time,
                                           tjs_real easing, tjs_int flag)
{
    if (Impl && Impl->Player)
        Impl->Player->setTimelineBlendRatio(name, ratio, time, easing);
}

bool D3DEmotePlayer::isTimelinePlaying(tTJSString name)
{
    if (Impl && Impl->Player)
        return Impl->Player->getTimelinePlaying(name);
    return false;
}

tjs_real D3DEmotePlayer::getTimelineBlendRatio(tTJSString name)
{
    // 通过 getPlayingTimelineInfoList 查询
    if (Impl && Impl->Player)
    {
        tTJSVariant list = Impl->Player->getPlayingTimelineInfoList();
        if (list.Type() == tvtObject)
        {
            iTJSDispatch2* arr = list.AsObjectNoAddRef();
            tTJSVariant count;
            if (TJS_SUCCEEDED(arr->PropGet(0, TJS_N("count"), nullptr, &count, arr)))
            {
                tjs_int n = (tjs_int)count;
                for (tjs_int i = 0; i < n; i++)
                {
                    tTJSVariant item;
                    if (TJS_SUCCEEDED(arr->PropGetByNum(0, i, &item, arr)) &&
                        item.Type() == tvtObject)
                    {
                        iTJSDispatch2* dict = item.AsObjectNoAddRef();
                        tTJSVariant label, ratio;
                        if (TJS_SUCCEEDED(dict->PropGet(0, TJS_N("label"), nullptr, &label, dict)) &&
                            label == name &&
                            TJS_SUCCEEDED(dict->PropGet(0, TJS_N("blendRatio"), nullptr, &ratio, dict)))
                            return (tjs_real)ratio;
                    }
                }
            }
        }
    }
    return 0;
}

tjs_int D3DEmotePlayer::countMainTimelines()
{
    if (Impl && Impl->Player)
    {
        tTJSVariant list = Impl->Player->getMainTimelineLabelList();
        if (list.Type() == tvtObject)
        {
            tTJSVariant count;
            if (TJS_SUCCEEDED(list.AsObjectNoAddRef()->PropGet(0, TJS_N("count"), nullptr, &count,
                                                               list.AsObjectNoAddRef())))
                return (tjs_int)count;
        }
    }
    return 0;
}

tTJSString D3DEmotePlayer::getMainTimelineLabelAt(tjs_int i)
{
    if (Impl && Impl->Player)
    {
        tTJSVariant list = Impl->Player->getMainTimelineLabelList();
        if (list.Type() == tvtObject)
        {
            tTJSVariant item;
            if (TJS_SUCCEEDED(list.AsObjectNoAddRef()->PropGetByNum(0, i, &item,
                                                                    list.AsObjectNoAddRef())))
                return tTJSString(item);
        }
    }
    return tTJSString();
}

tjs_int D3DEmotePlayer::countDiffTimelines()
{
    if (Impl && Impl->Player)
    {
        tTJSVariant list = Impl->Player->getDiffTimelineLabelList();
        if (list.Type() == tvtObject)
        {
            tTJSVariant count;
            if (TJS_SUCCEEDED(list.AsObjectNoAddRef()->PropGet(0, TJS_N("count"), nullptr, &count,
                                                               list.AsObjectNoAddRef())))
                return (tjs_int)count;
        }
    }
    return 0;
}

tTJSString D3DEmotePlayer::getDiffTimelineLabelAt(tjs_int i)
{
    if (Impl && Impl->Player)
    {
        tTJSVariant list = Impl->Player->getDiffTimelineLabelList();
        if (list.Type() == tvtObject)
        {
            tTJSVariant item;
            if (TJS_SUCCEEDED(list.AsObjectNoAddRef()->PropGetByNum(0, i, &item,
                                                                    list.AsObjectNoAddRef())))
                return tTJSString(item);
        }
    }
    return tTJSString();
}

tjs_int D3DEmotePlayer::countPlayingTimelines()
{
    if (Impl && Impl->Player)
    {
        tTJSVariant list = Impl->Player->getPlayingTimelineInfoList();
        if (list.Type() == tvtObject)
        {
            tTJSVariant count;
            if (TJS_SUCCEEDED(list.AsObjectNoAddRef()->PropGet(0, TJS_N("count"), nullptr, &count,
                                                               list.AsObjectNoAddRef())))
                return (tjs_int)count;
        }
    }
    return 0;
}

tTJSString D3DEmotePlayer::getPlayingTimelineLabelAt(tjs_int i)
{
    if (Impl && Impl->Player)
    {
        tTJSVariant list = Impl->Player->getPlayingTimelineInfoList();
        if (list.Type() == tvtObject)
        {
            tTJSVariant item;
            if (TJS_SUCCEEDED(list.AsObjectNoAddRef()->PropGetByNum(0, i, &item,
                                                                    list.AsObjectNoAddRef())) &&
                item.Type() == tvtObject)
            {
                tTJSVariant label;
                if (TJS_SUCCEEDED(item.AsObjectNoAddRef()->PropGet(0, TJS_N("label"), nullptr,
                                                                   &label, item.AsObjectNoAddRef())))
                    return tTJSString(label);
            }
        }
    }
    return tTJSString();
}

tjs_int D3DEmotePlayer::getPlayingTimelineFlagsAt(tjs_int i)
{
    if (Impl && Impl->Player)
    {
        tTJSVariant list = Impl->Player->getPlayingTimelineInfoList();
        if (list.Type() == tvtObject)
        {
            tTJSVariant item;
            if (TJS_SUCCEEDED(list.AsObjectNoAddRef()->PropGetByNum(0, i, &item,
                                                                    list.AsObjectNoAddRef())) &&
                item.Type() == tvtObject)
            {
                tTJSVariant flags;
                if (TJS_SUCCEEDED(item.AsObjectNoAddRef()->PropGet(0, TJS_N("flags"), nullptr,
                                                                   &flags, item.AsObjectNoAddRef())))
                    return (tjs_int)flags;
            }
        }
    }
    return 0;
}

void D3DEmotePlayer::DrawToTarget(void* target, void* maskTarget)
{
    if (!ShowFlag || !Impl || !Impl->Player || !Device || !Device->GetBackend() || !target)
        return;
    krkrsdl3::iTVPRenderBackend* backend = Device->GetBackend();
    float sx = Layer ? fabsf(Layer->Matrix[0]) : 1.0f;
    float sy = Layer ? fabsf(Layer->Matrix[5]) : 1.0f;
    if (sx < 0.01f) sx = 1.0f;
    if (sy < 0.01f) sy = 1.0f;
    tjs_int limitW = (tjs_int)((float)Device->GetWidth() / sx);
    tjs_int limitH = (tjs_int)((float)Device->GetHeight() / sy);
    tjs_int originX = Layer ? (tjs_int)(((float)Device->GetWidth() * 0.5f + Layer->Matrix[12]) / sx) : 0;
    tjs_int originY = Layer ? (tjs_int)(((float)Device->GetHeight() * 0.5f + Layer->Matrix[13]) / sy) : 0;
    // 把真实渲染视口尺寸（设备宽高）传给 drawToTarget：
    // shape 判定区域收集的 NDC→像素换算须与实际渲染视口一致，
    // 否则判定区域与画面错位
    Impl->Player->drawToTarget(backend, target, maskTarget, true, limitW, limitH, originX, originY,
                               Device->GetWidth(), Device->GetHeight());
    Animating = Impl->Player->get_animating();
}

void D3DEmotePlayer::finalize()
{
    // 此函数只用于数据清理，不用于实例删除，否则析构函数会二次删除
    // delete this;
}

} // namespace drawdevice
