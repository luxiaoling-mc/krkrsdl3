#include "tjsCommHead.h"
#include "ncbind/ncbind.hpp"
#include "LayerExBase.hpp"
#include "TVPStorage.h"
#include "TVPApplication.h"

#include "TVPColor.h"

#define NCB_MODULE_NAME TJS_N("PackinOne.dll")

static void InitPlugin_PackinOne()
{
    try
    {
        // 我们并不知道这个插件是干啥的，只能根据情况猜一个
        ncbAutoRegister::LoadModule(TJS_N("LayerExMovie.dll"));
        // 以全局对象为上下文执行，使脚本内 typeof extSourceMap 等全局引用可用
        TVPExecuteScript(TJS_N("\
class AffineSourceMovie extends AffineSource {\
	var _movie;\
	var _width = 0;\
	var _height = 0;\
	var _lastOwner = true;\
	function AffineSourceMovie(window) {\
        super.AffineSource(window);\
	}\
	function createLayer(orig=void) {\
        var src = new global.Layer(_window, _pool);\
        if (orig != void)\
        {\
            src.assignImages(orig);\
            src.width = orig.width;\
            src.height = orig.height;\
            src.scale = orig.scale;\
        }\
        else\
        {\
            src.scale = 1.0;\
        }\
        return src;\
	}\
	function finalize()\
	{\
        if (_lastOwner)\
        {\
            clear();\
            invalidate _movie;\
        }\
	}\
	function clear()\
	{\
        notifyOwner(\"onMotionStop\");\
        onMovieStop();\
        kag.conductor.trigger(\"movie_world_foremovie\");\
	}\
	function clone(newwindow, instance) {\
        if (newwindow === void)\
        {\
            newwindow = _window;\
        }\
        if (instance === void)\
        {\
            instance = new global.AffineSourceMovie(newwindow);\
        }\
        instance._movie = _movie;\
        instance._width = _width;\
        instance._height = _height;\
        _lastOwner = false;\
        super.clone(newwindow, instance);\
        return instance;\
	}\
	function canWaitMovie() {\
        return _movie.isPlayingMovie();\
	}\
	function isFlip() {\
        if (_movie.isPlayingMovie())\
        {\
            return true;\
        }\
        else\
        {\
            clear();\
            return false;\
        }\
	}\
	function stopMovie() {\
        _movie.stopMovie();\
	}\
	function drawAffine(target, mtx, src) {\
        (global.Layer.copyRect incontextof target)(0, 0, _movie, 0, 0, _width, _height);\
	}\
	function loadImages(storage,colorKey=clNone,options=void) {\
        _movie = createLayer();\
        _movie.openMovie(storage, false);\
        _movie.setSizeToImageSize();\
        _width = _movie.width;\
        _height = _movie.height;\
        _movie.startMovie(false);\
	}\
};\
if (typeof global.extSourceMap != \"undefined\") {\
    global.extSourceMap[\".WMV\"] = AffineSourceMovie;\
    global.extSourceMap[\".MPG\"] = AffineSourceMovie;\
    global.extSourceMap[\".MPEG\"] = AffineSourceMovie;\
}\
        "), TVPGetScriptEngine()->GetGlobalNoAddRef());
    } catch (...) { };
}

NCB_PRE_REGIST_CALLBACK(InitPlugin_PackinOne);
