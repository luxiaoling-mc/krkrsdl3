#include "tjsCommHead.h"
#include "ncbind/ncbind.hpp"
#include "DrawDeviceD3D.h"
#include "D3DEmotePlayer.h"

#define NCB_MODULE_NAME TJS_N("drawdeviceD3D.dll")

using namespace drawdevice;

//---------------------------------------------------------------------------
// DrawDeviceD3D 脚本类
//---------------------------------------------------------------------------
NCB_REGISTER_CLASS(DrawDeviceD3D)
{
    NCB_CONSTRUCTOR((tjs_int, tjs_int));
    NCB_PROPERTY_RO(interface, getInterface);
    NCB_PROPERTY(width, getWidth, setWidth);
    NCB_PROPERTY(height, getHeight, setHeight);
    NCB_METHOD(setSize);
    NCB_PROPERTY(clearColor, getClearColor, setClearColor);
    NCB_PROPERTY(layerManagerIndex, getLayerManagerIndex, setLayerManagerIndex);
    NCB_PROPERTY(stretchType, getStretchType, setStretchType);
    NCB_PROPERTY(bicubicParam, getBicubicParam, setBicubicParam);
    NCB_PROPERTY(transState, getTransState, setTransState);
    NCB_PROPERTY(maskMode, getMaskMode, setMaskMode);
    NCB_PROPERTY_RO(primaryLayers, getPrimaryLayers);
    NCB_METHOD(checkEnable);
    NCB_METHOD(getModule);
    NCB_METHOD(update);
    NCB_METHOD(capture);
    NCB_METHOD(startTransition);
    NCB_METHOD(stopTransition);
    NCB_METHOD(setScreenRect);
    NCB_METHOD(setPrimarySize);
    NCB_METHOD(setOffset);
    NCB_METHOD(recreate);
    NCB_METHOD(finalize);
}

//---------------------------------------------------------------------------
// D3D 脚本类（KAGEnvPlayer.tjs：new D3D(exWidth, exHeight)）
// 与 DrawDeviceD3D 同接口；原生实例为 DrawDeviceD3D 子类，
// D3DLayer/D3DImage 构造经 GetDrawDeviceInstance 接受该对象。
//---------------------------------------------------------------------------
NCB_REGISTER_CLASS(D3D)
{
    NCB_CONSTRUCTOR((tjs_int, tjs_int));
    NCB_PROPERTY_RO(interface, getInterface);
    NCB_PROPERTY(width, getWidth, setWidth);
    NCB_PROPERTY(height, getHeight, setHeight);
    NCB_METHOD(setSize);
    NCB_PROPERTY(clearColor, getClearColor, setClearColor);
    NCB_PROPERTY(layerManagerIndex, getLayerManagerIndex, setLayerManagerIndex);
    NCB_PROPERTY(stretchType, getStretchType, setStretchType);
    NCB_PROPERTY(bicubicParam, getBicubicParam, setBicubicParam);
    NCB_PROPERTY(transState, getTransState, setTransState);
    NCB_PROPERTY(maskMode, getMaskMode, setMaskMode);
    NCB_PROPERTY_RO(primaryLayers, getPrimaryLayers);
    NCB_METHOD(checkEnable);
    NCB_METHOD(getModule);
    NCB_METHOD(update);
    NCB_METHOD(capture);
    NCB_METHOD(startTransition);
    NCB_METHOD(stopTransition);
    NCB_METHOD(setScreenRect);
    NCB_METHOD(setPrimarySize);
    NCB_METHOD(setOffset);
    NCB_METHOD(recreate);
    NCB_METHOD(finalize);
}

//---------------------------------------------------------------------------
// 设备实例解析：脚本传入的设备对象可以是 DrawDeviceD3D 或 D3D
//（两者原生实例均为 DrawDeviceD3D 或其子类）
//---------------------------------------------------------------------------
namespace drawdevice
{
DrawDeviceD3D* GetDrawDeviceInstance(iTJSDispatch2* obj)
{
    if (DrawDeviceD3D* dev = ncbInstanceAdaptor<DrawDeviceD3D>::GetNativeInstance(obj))
        return dev;
    if (D3D* d3d = ncbInstanceAdaptor<D3D>::GetNativeInstance(obj))
        return d3d;
    return nullptr;
}
} // namespace drawdevice

//---------------------------------------------------------------------------
// D3DLayer 脚本类
//---------------------------------------------------------------------------
// D3DLayer 工厂：捕获脚本对象（objthis）以便每帧驱动脚本 onUpdate
//（D3DAffineLayer.onUpdate → setMatrix / picture 属性，见 out/data/system/D3D.tjs）
static tjs_error D3DLayerFactory(D3DLayer** result, tjs_int numparams, tTJSVariant** param,
                                 iTJSDispatch2* objthis)
{
    if (numparams < 1 || !param[0] || param[0]->Type() != tvtObject)
        return TJS_E_BADPARAMCOUNT;
    D3DLayer* layer = new D3DLayer(param[0]->AsObjectNoAddRef(), objthis);
    *result = layer;
    return TJS_S_OK;
}
NCB_REGISTER_CLASS(D3DLayer)
{
    RawCallback(D3DLayerFactory);
    NCB_METHOD(setMatrix);
    NCB_METHOD(finalize);
    NCB_PROPERTY(drawPlane, getDrawPlane, setDrawPlane);
    NCB_PROPERTY(frontIndex, getFrontIndex, setFrontIndex);
    NCB_PROPERTY(backIndex, getBackIndex, setBackIndex);
    Variant("DrawPlaneBoth", (int)D3DLayer::DrawPlaneBoth);
    Variant("DrawPlaneFront", (int)D3DLayer::DrawPlaneFront);
    Variant("DrawPlaneBack", (int)D3DLayer::DrawPlaneBack);
}

//---------------------------------------------------------------------------
// D3DImage 脚本类
//---------------------------------------------------------------------------
NCB_REGISTER_CLASS(D3DImage)
{
    NCB_CONSTRUCTOR((iTJSDispatch2*));
    NCB_METHOD(load);
    NCB_METHOD(finalize);
    NCB_PROPERTY_RO(width, getWidth);
    NCB_PROPERTY_RO(height, getHeight);
}

//---------------------------------------------------------------------------
// D3DPicture 脚本类
//---------------------------------------------------------------------------
NCB_REGISTER_CLASS(D3DPicture)
{
    NCB_CONSTRUCTOR((iTJSDispatch2*, iTJSDispatch2*));
    NCB_METHOD(assignImageRange);
    NCB_METHOD(setCoord);
    NCB_METHOD(finalize);
    NCB_PROPERTY(blendMode, getBlendMode, setBlendMode);
    NCB_PROPERTY(opacity, getOpacity, setOpacity);
}

//---------------------------------------------------------------------------
// D3DEmotePlayer 脚本类（包装 emoteplayer 的 GPU 直通播放器）
//---------------------------------------------------------------------------
NCB_REGISTER_CLASS(D3DEmotePlayer)
{
    NCB_CONSTRUCTOR((iTJSDispatch2*));
    NCB_METHOD(load);
    NCB_METHOD(show);
    NCB_METHOD(clone);
    NCB_METHOD(skip);
    NCB_METHOD(pass);
    NCB_METHOD(progress);
    NCB_METHOD(setCoord);
    NCB_METHOD(setRot);
    NCB_METHOD(setScale);
    NCB_METHOD(setColor);
    NCB_METHOD(setVariable);
    NCB_METHOD(getVariable);
    NCB_METHOD(startWind);
    NCB_METHOD(stopWind);
    NCB_METHOD(playTimeline);
    NCB_METHOD(stopTimeline);
    NCB_METHOD(fadeOutTimeline);
    NCB_METHOD(setTimelineBlendRatio);
    NCB_METHOD(isTimelinePlaying);
    NCB_METHOD(getTimelineBlendRatio);
    NCB_METHOD(countMainTimelines);
    NCB_METHOD(getMainTimelineLabelAt);
    NCB_METHOD(countDiffTimelines);
    NCB_METHOD(getDiffTimelineLabelAt);
    NCB_METHOD(countPlayingTimelines);
    NCB_METHOD(getPlayingTimelineLabelAt);
    NCB_METHOD(getPlayingTimelineFlagsAt);
    NCB_METHOD(finalize);
    NCB_PROPERTY(animating, getAnimating, setAnimating);
    NCB_PROPERTY(smoothing, getSmoothing, setSmoothing);
    NCB_PROPERTY(meshDivisionRatio, getMeshDivisionRatio, setMeshDivisionRatio);
    NCB_PROPERTY(bustScale, getBustScale, setBustScale);
    NCB_PROPERTY(hairScale, getHairScale, setHairScale);
    NCB_PROPERTY(partsScale, getPartsScale, setPartsScale);
}

//---------------------------------------------------------------------------
// Layer 附加属性：drawPlane / frontIndex / backIndex
//（脚本在 D3D 分支直接给普通 Layer 赋值，见 MainWindow.tjs / BaseLayer.tjs）
//---------------------------------------------------------------------------
NCB_GET_INSTANCE_HOOK(LayerD3DAttach)
{
    NCB_INSTANCE_GETTER(objthis)
    {
        LayerD3DAttach* obj = GetNativeInstance(objthis);
        if (!obj)
        {
            obj = new LayerD3DAttach();
            SetNativeInstance(objthis, obj);
        }
        return obj;
    }
};
NCB_ATTACH_CLASS_WITH_HOOK(LayerD3DAttach, Layer)
{
    NCB_PROPERTY(drawPlane, getDrawPlane, setDrawPlane);
    NCB_PROPERTY(frontIndex, getFrontIndex, setFrontIndex);
    NCB_PROPERTY(backIndex, getBackIndex, setBackIndex);
}

//---------------------------------------------------------------------------
// 插件加载
//---------------------------------------------------------------------------
// 说明：D3D.tjs / D3DAffineSource*.tjs / D3DEmote.tjs 等脚本类由游戏方负责
// 加载（游戏自己的归档内通常自带，或经 KAG 的 KAGLoadScript/Scripts.execStorage
// 回落到系统脚本目录）。这些脚本依赖游戏运行时环境（convertMode、extSourceMap
// 等由游戏的 Initialize.tjs / AffineSourceLayer.tjs 定义），在插件链接时刻
// （patch.tjs 阶段）执行必然失败，因此这里只注册原生类，不自动执行脚本。

void DrawDeviceD3D_init()
{
    try
    {
        // 加载 emoteplayer 插件（D3DEmotePlayer 依赖其 Player 实现）
        ncbAutoRegister::LoadModule(TJS_N("emoteplayer.dll"));
    }
    catch (...)
    {
    }
}

void DrawDeviceD3D_done()
{
}

NCB_PRE_REGIST_CALLBACK(DrawDeviceD3D_init);
NCB_POST_UNREGIST_CALLBACK(DrawDeviceD3D_done);
