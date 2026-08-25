#pragma once

#include "tjsCommHead.h"
#include "DrawDeviceD3D.h"

//---------------------------------------------------------------------------
// D3DEmotePlayer —— emoteplayer 的 D3D 直通播放器
//
// 包装 emoteplayer 插件的 EmotePlayer：
//   - 动画状态机/网格完全复用 emoteplayer
//   - 绘制输出为"GPU 直通"：绘制到本播放器的离屏目标（不经引擎 Layer，
//     无 CPU 回读），由 DrawDeviceD3D 在合成 Front 平面时把该目标作为
//     纹理混入画面
//   - 脚本接口与 D3DaffineSourceEmote.tjs 的调用面一致
//     （progress 以"60fps 帧"为单位传入，内部换算为毫秒）
//---------------------------------------------------------------------------
namespace drawdevice
{

class D3DEmotePlayer
{
    class D3DLayer* Layer = nullptr;
    DrawDeviceD3D* Device = nullptr;

    void* Target = nullptr;      // 本播放器的离屏目标（合成 Front 平面时采样）
    void* MaskTarget = nullptr;  // 蒙版目标

    class PlayerImpl* Impl = nullptr; // emoteplayer::EmotePlayer 包装（见 cpp）

    bool ShowFlag = false;
    bool Smoothing = true;
    tjs_real MeshDivisionRatio = 1.0;
    tjs_real BustScale = 1.0;
    tjs_real HairScale = 1.0;
    tjs_real PartsScale = 1.0;
    bool Animating = false;

public:
    D3DEmotePlayer(iTJSDispatch2* d3dlayer);
    ~D3DEmotePlayer();

    void load(tTJSString file);
    void show() { ShowFlag = true; }
    tTJSVariant clone(iTJSDispatch2* newlayer);
    void skip();
    void pass();
    void progress(tjs_real frames);
    void setCoord(tjs_real x, tjs_real y, tjs_real z = 0, tjs_real t = 0);
    void setRot(tjs_real angle, tjs_real time = 0, tjs_real easing = 0);
    void setScale(tjs_real scale, tjs_real time = 0, tjs_real easing = 0);
    void setColor(tjs_uint32 color, tjs_real time = 0, tjs_real easing = 0);
    void setVariable(tTJSString name, tjs_real value, tjs_real time = 0, tjs_real accel = 0);
    tjs_real getVariable(tTJSString name);
    void startWind(tjs_real start, tjs_real goal, tjs_real speed, tjs_real powMin, tjs_real powMax);
    void stopWind();

    void playTimeline(tTJSString name, tjs_int flags = 0);
    void stopTimeline(tTJSString name = "");
    void fadeOutTimeline(tTJSString name, tjs_real time = 0, tjs_real easing = 0);
    void setTimelineBlendRatio(tTJSString name, tjs_real ratio, tjs_real time = 0,
                               tjs_real easing = 0, tjs_int flag = 0);
    bool isTimelinePlaying(tTJSString name);
    tjs_real getTimelineBlendRatio(tTJSString name);
    tjs_int countMainTimelines();
    tTJSString getMainTimelineLabelAt(tjs_int i);
    tjs_int countDiffTimelines();
    tTJSString getDiffTimelineLabelAt(tjs_int i);
    tjs_int countPlayingTimelines();
    tTJSString getPlayingTimelineLabelAt(tjs_int i);
    tjs_int getPlayingTimelineFlagsAt(tjs_int i);

    bool getAnimating() { return Animating; }
    void setAnimating(bool v) { Animating = v; }
    bool getSmoothing() { return Smoothing; }
    void setSmoothing(bool v) { Smoothing = v; }
    tjs_real getMeshDivisionRatio() { return MeshDivisionRatio; }
    void setMeshDivisionRatio(tjs_real v) { MeshDivisionRatio = v; }
    tjs_real getBustScale() { return BustScale; }
    void setBustScale(tjs_real v) { BustScale = v; }
    tjs_real getHairScale() { return HairScale; }
    void setHairScale(tjs_real v) { HairScale = v; }
    tjs_real getPartsScale() { return PartsScale; }
    void setPartsScale(tjs_real v) { PartsScale = v; }

    void finalize();

    // ---- 供 DrawDeviceD3D 合成 ----
    void Draw(void* compositeTarget);
    void* GetTarget() { return Target; }
    bool IsActive() { return ShowFlag; }
};

} // namespace drawdevice
