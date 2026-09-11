#include "ncbind/ncbind.hpp"
#include "emoteplayerclass.h"
#include "tjsString.h"

#define NCB_MODULE_NAME TJS_N("emoteplayer.dll")

using namespace emoteplayer;

#define ENUM(n) Variant(#n, (int)n)
#define PROPERTY(name) NCB_PROPERTY(name, get_##name, set_##name);

NCB_REGISTER_SUBCLASS(ResourceManager)
{
    NCB_CONSTRUCTOR((iTJSDispatch2*, tjs_int));
    NCB_METHOD(load);
    NCB_METHOD(unload);
    NCB_METHOD(unloadAll);
    NCB_METHOD(clearCache);
    NCB_METHOD(setEmotePSBDecryptSeed);
    NCB_METHOD(setEmotePSBDecryptFunc);
}

NCB_REGISTER_SUBCLASS(SeparateLayerAdaptor)
{
    NCB_CONSTRUCTOR((iTJSDispatch2*));
    NCB_METHOD(assign);
    NCB_METHOD(clear);
    // Layer的所有属性都应该有，NCB机制或许要换成Hook
    PROPERTY(absolute);
    PROPERTY(isPrimary);
    PROPERTY(parent);
}

NCB_REGISTER_SUBCLASS(D3DAdaptor)
{
    // D3DAdaptor就是一种GPU->CPU的回读机制
    // 它不依赖DrawDeviceD3D
    // 现在的emote已经是这种机制了，所以可以直接套壳
    NCB_CONSTRUCTOR((iTJSDispatch2*, tjs_int, tjs_int, tjs_int, tjs_int));
    NCB_METHOD(setClearColor);
    NCB_METHOD(captureCanvas);
    NCB_METHOD(unloadUnusedTextures);
}

NCB_REGISTER_SUBCLASS(EmotePlayer)
{
    NCB_CONSTRUCTOR((ResourceManager*));
    NCB_METHOD(serialize);
    NCB_METHOD(unserialize);
    NCB_METHOD(play);
    NCB_METHOD(initPhysics);
    NCB_METHOD(clear);
    NCB_METHOD(progress);
    NCB_METHOD(draw);
    NCB_METHOD(assign);
    NCB_METHOD(setCoord);
    NCB_METHOD(setScale);
    NCB_METHOD(setRotate);
    NCB_METHOD(setColor);
    NCB_METHOD(setVariable);
    NCB_METHOD(getVariable);
    NCB_METHOD(setOuterForce);
    NCB_METHOD(setDrawAffineTranslateMatrix);
    NCB_METHOD(setCameraOffset);
    NCB_METHOD(startWind);
    NCB_METHOD(stopWind);
    // contains(label, x, y) / contains(x, y) 两参分发（见 EmotePlayer::cb_contains）
    NCB_METHOD_RAW_CALLBACK(contains, &EmotePlayer::cb_contains, 0);
    NCB_METHOD(skip);
    NCB_METHOD(skipToSync);
    NCB_METHOD(pass);
    NCB_METHOD(playTimeline);
    NCB_METHOD(stopTimeline);
    NCB_METHOD(getTimelinePlaying);
    NCB_METHOD(getLoopTimeline);
    NCB_METHOD(getTimelineTotalFrameCount);
    NCB_METHOD(getMainTimelineLabelList);
    NCB_METHOD(getDiffTimelineLabelList);
    NCB_METHOD(setTimelineBlendRatio);
    NCB_METHOD(fadeInTimeline);
    NCB_METHOD(fadeOutTimeline);
    NCB_METHOD(getPlayingTimelineInfoList);
    NCB_METHOD(getVariableFrameList);
    PROPERTY(animating);
    PROPERTY(playing);
    PROPERTY(allplaying);
    PROPERTY(loopTime);
    PROPERTY(tickCount);
    PROPERTY(speed);
    PROPERTY(outline);
    PROPERTY(motionKey);
    PROPERTY(motion);
    PROPERTY(chara);
    PROPERTY(variableKeys);
    Variant("TimelinePlayFlagParallel", (int)EmotePlayer::TimelinePlayFlagParallel);
    Variant("TimelinePlayFlagDifference", (int)EmotePlayer::TimelinePlayFlagDifference);
}

NCB_REGISTER_SUBCLASS(Player)
{
    NCB_CONSTRUCTOR((ResourceManager*));
    NCB_METHOD(serialize);
    NCB_METHOD(unserialize);
    NCB_METHOD(play);
    NCB_METHOD(initPhysics);
    NCB_METHOD(clear);
    NCB_METHOD(progress);
    NCB_METHOD(draw);
    NCB_METHOD(assign);
    NCB_METHOD(setCoord);
    NCB_METHOD(setScale);
    NCB_METHOD(setRotate);
    NCB_METHOD(setColor);
    NCB_METHOD(setVariable);
    NCB_METHOD(getVariable);
    NCB_METHOD(setOuterForce);
    NCB_METHOD(setDrawAffineTranslateMatrix);
    NCB_METHOD(setCameraOffset);
    NCB_METHOD(startWind);
    NCB_METHOD(stopWind);
    // contains(label, x, y) / contains(x, y) 两参分发（见 EmotePlayer::cb_contains）
    NCB_METHOD_RAW_CALLBACK(contains, &Player::cb_contains, 0);
    NCB_METHOD(skip);
    NCB_METHOD(skipToSync);
    NCB_METHOD(stop);
    NCB_METHOD(pass);
    NCB_METHOD(playTimeline);
    NCB_METHOD(stopTimeline);
    NCB_METHOD(getTimelinePlaying);
    NCB_METHOD(getLoopTimeline);
    NCB_METHOD(getTimelineTotalFrameCount);
    NCB_METHOD(getMainTimelineLabelList);
    NCB_METHOD(getDiffTimelineLabelList);
    NCB_METHOD(setTimelineBlendRatio);
    NCB_METHOD(fadeInTimeline);
    NCB_METHOD(fadeOutTimeline);
    NCB_METHOD(getPlayingTimelineInfoList);
    NCB_METHOD(getVariableFrameList);
    NCB_METHOD(getCommandList);
    NCB_METHOD(getLayerGetter);
    NCB_METHOD(getLayerMotion);
    NCB_METHOD(setFlip);
    NCB_METHOD(setSlant);
    NCB_METHOD(setZoom);
    PROPERTY(animating);
    PROPERTY(playing);
    PROPERTY(allplaying);
    PROPERTY(loopTime);
    PROPERTY(tickCount);
    PROPERTY(speed);
    PROPERTY(outline);
    PROPERTY(motionKey);
    PROPERTY(motion);
    PROPERTY(chara);
    PROPERTY(variableKeys);
    PROPERTY(tags);
    PROPERTY(useD3D); // 脚本侧判断 D3D emote 可用性（见 AffineSourceMotion）
}

NCB_REGISTER_CLASS(Motion)
{
    NCB_METHOD(getD3DAvailable);
    NCB_PROPERTY(enableD3D, getEnableD3D, setEnableD3D);
    ENUM(MaskModeAlpha);
    ENUM(PlayFlagForce);
    NCB_SUBCLASS(ResourceManager, ResourceManager);
    NCB_SUBCLASS(EmotePlayer, EmotePlayer);
    NCB_SUBCLASS(Player, Player);
    NCB_SUBCLASS(SeparateLayerAdaptor, SeparateLayerAdaptor);
    NCB_SUBCLASS(D3DAdaptor, D3DAdaptor);
}

static void emoteplayer_init()
{
}

static void emoteplayer_done()
{
}

NCB_PRE_REGIST_CALLBACK(emoteplayer_init);
NCB_POST_UNREGIST_CALLBACK(emoteplayer_done);