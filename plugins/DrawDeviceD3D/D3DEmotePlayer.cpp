#include "D3DEmotePlayer.h"

#include "emoteplayer/emoteplayerclass.h"
#include "Platform.h"
#include "TVPDebug.h"
#include "TVPStorage.h"

#include <set>

//---------------------------------------------------------------------------
// D3DEmotePlayer 实现
//---------------------------------------------------------------------------
namespace drawdevice
{

// emoteplayer::ResourceManager 与 EmotePlayer 的包装（避免头文件耦合）。
// ResourceManager 全局共享（缓存 PSB 文件，多个播放器复用；解密种子为类静态，
// 游戏侧 Motion.ResourceManager.setEmotePSBDecryptSeed 会同步到所有实例）；
// EmotePlayer 每 D3DEmotePlayer 实例独立（各自持有动画状态机）。
class PlayerImpl
{
public:
    emoteplayer::ResourceManager* RM = nullptr; // 共享，不在此析构
    emoteplayer::EmotePlayer* Player = nullptr; // 本实例私有

    PlayerImpl() {}
    ~PlayerImpl() { delete Player; }
};

static emoteplayer::ResourceManager* GetSharedRM()
{
    static emoteplayer::ResourceManager* rm = nullptr;
    if (!rm)
        rm = new emoteplayer::ResourceManager(nullptr, 0);
    return rm;
}

D3DEmotePlayer::D3DEmotePlayer(iTJSDispatch2* d3dlayer)
{
    Layer = ncbInstanceAdaptor<D3DLayer>::GetNativeInstance(d3dlayer);
    if (Layer)
        Device = Layer->Device;
    // 每实例独立动画状态机（多角色/多层各自推进），共享资源缓存
    Impl = new PlayerImpl();
    Impl->RM = GetSharedRM();
    Impl->Player = new emoteplayer::EmotePlayer(Impl->RM);
    // 尺寸与设备虚拟屏一致
    if (Device)
    {
        krkrsdl3::iTVPRenderBackend* backend = Device->GetBackend();
        if (backend)
        {
            Target = backend->CreateTarget(Device->GetWidth(), Device->GetHeight());
            MaskTarget = backend->CreateTarget(Device->GetWidth(), Device->GetHeight());
        }
        Device->RegisterEmotePlayer(this);
    }
}

D3DEmotePlayer::~D3DEmotePlayer()
{
    if (Device)
        Device->UnregisterEmotePlayer(this);
    if (Device && Device->GetBackend())
    {
        if (Target)
            Device->GetBackend()->DestroyTarget(Target);
        if (MaskTarget)
            Device->GetBackend()->DestroyTarget(MaskTarget);
    }
    delete Impl;
    Impl = nullptr;
}

void D3DEmotePlayer::load(tTJSString file)
{
    if (!Impl || !Impl->Player || !Device || !Device->GetBackend())
        return;
    try
    {
        Impl->RM->load(file);
        // motionKey 必须用与 ResourceManager 缓存键一致的放置路径
        //（RM 以 TVPGetPlacedPath 为键，原始名查不到 → play() 会走
        // 兜底分支从缓存取第一个匹配文件，导致多角色加载同一个模型）
        Impl->Player->set_motionKey(TVPGetPlacedPath(file));
        Impl->Player->play(file, 0);
        Animating = true;
    }
    catch (...)
    {
        TVPConsoleLog("D3DEmotePlayer::load failed: %s", file.c_str());
    }
}

tTJSVariant D3DEmotePlayer::clone(iTJSDispatch2* newlayer)
{
    // 转场/建层时 D3DAffineSourceEmote.clone → _player.clone(newlayer)：
    // 为新 D3DLayer 创建一个状态延续的播放器（同一 motion、进度、变量），
    // 并返回对应的脚本对象（ncb 原生实例包装）。
    D3DEmotePlayer* np = new D3DEmotePlayer(newlayer);
    if (Impl && Impl->Player && np->Impl && np->Impl->Player)
    {
        try
        {
            emoteplayer::EmotePlayer* src = Impl->Player;
            emoteplayer::EmotePlayer* dst = np->Impl->Player;
            // 同一 motion 资源（ResourceManager 共享缓存）→ 继续当前 motion
            dst->set_motionKey(src->get_motionKey());
            tTJSString motion = src->get_motion();
            if (!motion.IsEmpty())
                dst->set_motion(motion); // play(name, 0)，重置 clockPassed=0
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
        Impl->Player->progress(frames * 1000.0 / 60.0);
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

void D3DEmotePlayer::Draw(void* compositeTarget)
{
    if (!ShowFlag || !Impl || !Impl->Player || !Device || !Device->GetBackend())
        return;
    krkrsdl3::iTVPRenderBackend* backend = Device->GetBackend();
    if (!Target)
        return;
    // GPU 直通：动画网格绘制到本播放器目标（无 CPU 回读）。
    // dx_ 模型为 2x 高分辨率版，图层矩阵 a 为世界缩放补偿：
    //   渲染窗口 = 屏幕 × (1/a)，originX = (W/2 + tx)/a（世界原点=屏幕中心）。
    float sx = 1.0f, sy = 1.0f;
    if (Layer)
    {
        sx = fabsf(Layer->Matrix[0]);
        sy = fabsf(Layer->Matrix[5]);
        if (sx < 0.01f) sx = 1.0f;
        if (sy < 0.01f) sy = 1.0f;
    }
    tjs_int limitW = (tjs_int)((float)Device->GetWidth() / sx);
    tjs_int limitH = (tjs_int)((float)Device->GetHeight() / sy);
    tjs_int originX = 0, originY = 0;
    if (Layer)
    {
        originX = (tjs_int)(((float)Device->GetWidth() * 0.5f + Layer->Matrix[12]) / sx);
        originY = (tjs_int)(((float)Device->GetHeight() * 0.5f + Layer->Matrix[13]) / sy);
    }
    Impl->Player->drawToTarget(backend, Target, MaskTarget, true, limitW, limitH, originX, originY);
    void* tex = backend->GetTargetTexture(Target);
    if (!tex)
        return;
    backend->SetTarget(compositeTarget);
    backend->ClearTarget(false);
    backend->LayerSetBlend(krkrsdl3::iTVPRenderBackend::LBM_ALPHA, 1.0f, nullptr);
    backend->LayerDrawRect(tex, 0, 0, (float)Device->GetWidth(), (float)Device->GetHeight());
    Animating = Impl->Player->get_animating();
}

void D3DEmotePlayer::finalize()
{
    // 此函数只用于数据清理，不用于实例删除，否则析构函数会二次删除
    // delete this;
}

} // namespace drawdevice
