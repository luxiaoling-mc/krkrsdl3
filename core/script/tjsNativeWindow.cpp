#include "tjsNativeWindow.h"

#include "TVPWindow.h"
#include "tjsDictionary.h"
#include "TVPMsg.h"
#include "TVPDebug.h"

#include "tjsNativeLayer.h"
#include "tjsNativeBasicDrawDevice.h"
#include "tjsNativeVideoOverlay.h"
#include "WindowManager.h"
//---------------------------------------------------------------------------
// tTJSNI_Window
//---------------------------------------------------------------------------
tTJSNI_Window::tTJSNI_Window()
{
}
//---------------------------------------------------------------------------
tTJSNI_Window::~tTJSNI_Window()
{
}
//---------------------------------------------------------------------------
tjs_error tTJSNI_Window::Construct(tjs_int numparams, tTJSVariant** param, iTJSDispatch2* tjs_obj)
{
    // 创建引擎侧窗口（TVPWindow 构造时自动注册到 WindowManager）
    Window = new TVPWindow;
    Window->SetOwner(tjs_obj);

    // 检查父窗口参数（仅校验类型）
    if (numparams >= 1 && param[0]->Type() == tvtObject)
    {
        tTJSVariantClosure clo = param[0]->AsObjectClosureNoAddRef();
        tTJSNI_Window* win = NULL;
        if (clo.Object != NULL)
        {
            if (TJS_FAILED(clo.Object->NativeInstanceSupport(
                    TJS_NIS_GETINSTANCE, tTJSNC_Window::ClassID, (iTJSNativeInstance**)&win)))
                TVPThrowExceptionMessage(TVPSpecifyWindow);
            if (!win)
                TVPThrowExceptionMessage(TVPSpecifyWindow);
        }
    }

    // 设置默认 draw device 对象 "PassThrough"
    {
        iTJSDispatch2* cls = NULL;
        iTJSDispatch2* newobj = NULL;
        try
        {
            cls = new tTJSNC_BasicDrawDevice();
            if (TJS_FAILED(cls->CreateNew(0, NULL, NULL, &newobj, 0, NULL, cls)))
                TVPThrowExceptionMessage(TVPInternalError, TJS_N("tTJSNI_Window::Construct"));
            Window->SetDrawDeviceObject(tTJSVariant(newobj, newobj));
        }
        catch (...)
        {
            if (cls)
                cls->Release();
            if (newobj)
                newobj->Release();
            throw;
        }
        if (cls)
            cls->Release();
        if (newobj)
            newobj->Release();
    }

    return TJS_S_OK;
}
//---------------------------------------------------------------------------
void tTJSNI_Window::Invalidate()
{
    if (Window)
    {
        iTJSDispatch2* owner = Window->GetOwnerNoAddRef();

        // remove all events
        if (owner)
            TVPCancelSourceEvents(owner);
        TVPCancelInputEvents(Window);
        TVPRemoveWindowUpdate(Window);

        // disconnect all VideoOverlay objects
        {
            tObjectListSafeLockHolder<tTJSNI_BaseVideoOverlay> holder(VideoOverlay);
            tjs_int count = VideoOverlay.GetSafeLockedObjectCount();
            for (tjs_int i = 0; i < count; i++)
            {
                tTJSNI_BaseVideoOverlay* item = VideoOverlay.GetSafeLockedObjectAt(i);
                if (!item)
                    continue;

                item->Disconnect();
            }
        }

        // invalidate all registered objects
        ObjectVectorLocked = true;
        std::vector<tTJSVariantClosure>::iterator i;

        for (i = ObjectVector.begin(); i != ObjectVector.end(); i++)
        {
            // invalidate each --
            // objects may throw an exception while invalidating,
            // but here we cannot care for them.
            try
            {
                i->Invalidate(0, NULL, NULL, NULL);
                i->Release();
            }
            catch (eTJSError& e)
            {
                TVPAddLog(e.GetMessage()); // just in case, log the error
            }
        }

        // remove all events (again)
        if (owner)
            TVPCancelSourceEvents(owner);
        TVPCancelInputEvents(Window);

        // release draw device
        Window->SetDrawDeviceObject(tTJSVariant());

        // 若窗口尚未自行销毁（Close 流程中已 NotifyWindowClose），在此销毁
        if (!Window->IsClosed())
            delete Window;
        Window = nullptr;
    }

    inherited::Invalidate();
}
//---------------------------------------------------------------------------
void tTJSNI_Window::Add(tTJSVariantClosure clo)
{
    if (ObjectVectorLocked)
        return;
    if (ObjectVector.end() == std::find(ObjectVector.begin(), ObjectVector.end(), clo))
    {
        ObjectVector.push_back(clo);
        clo.AddRef();
    }
}
//---------------------------------------------------------------------------
void tTJSNI_Window::Remove(tTJSVariantClosure clo)
{
    if (ObjectVectorLocked)
        return;
    std::vector<tTJSVariantClosure>::iterator i;
    i = std::find(ObjectVector.begin(), ObjectVector.end(), clo);
    if (i != ObjectVector.end())
    {
        clo.Release();
        ObjectVector.erase(i);
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// interface to video overlay object
//---------------------------------------------------------------------------
void tTJSNI_Window::RegisterVideoOverlayObject(tTJSNI_BaseVideoOverlay* ovl)
{
    VideoOverlay.Add(ovl);
}
//---------------------------------------------------------------------------
void tTJSNI_Window::UnregisterVideoOverlayObject(tTJSNI_BaseVideoOverlay* ovl)
{
    VideoOverlay.Remove(ovl);
}
//---------------------------------------------------------------------------
void tTJSNI_Window::ReadjustVideoRect()
{
    if (!Window)
        return;

    // re-adjust video rectangle.
    // this reconnects owner window and video offsets.

    tObjectListSafeLockHolder<tTJSNI_BaseVideoOverlay> holder(VideoOverlay);
    tjs_int count = VideoOverlay.GetSafeLockedObjectCount();

    for (tjs_int i = 0; i < count; i++)
    {
        tTJSNI_VideoOverlay* item = (tTJSNI_VideoOverlay*)VideoOverlay.GetSafeLockedObjectAt(i);
        if (!item)
            continue;
        item->ResetOverlayParams();
    }
}
//---------------------------------------------------------------------------
void tTJSNI_Window::WindowMoved()
{
    // inform video overlays that the window has moved.
    // video overlays typically owns Direct3D surface which is not a part of
    // normal window systems and does not matter where the owner window is.
    // so we must inform window moving to overlay window.

    tObjectListSafeLockHolder<tTJSNI_BaseVideoOverlay> holder(VideoOverlay);
    tjs_int count = VideoOverlay.GetSafeLockedObjectCount();
    for (tjs_int i = 0; i < count; i++)
    {
        tTJSNI_VideoOverlay* item = (tTJSNI_VideoOverlay*)VideoOverlay.GetSafeLockedObjectAt(i);
        if (!item)
            continue;
        item->SetRectangleToVideoOverlay();
    }
}
//---------------------------------------------------------------------------
void tTJSNI_Window::DetachVideoOverlay()
{
    // detach video overlay window
    // this is done before the window is being fullscreened or un-fullscreened.
    tObjectListSafeLockHolder<tTJSNI_BaseVideoOverlay> holder(VideoOverlay);
    tjs_int count = VideoOverlay.GetSafeLockedObjectCount();
    for (tjs_int i = 0; i < count; i++)
    {
        tTJSNI_VideoOverlay* item = (tTJSNI_VideoOverlay*)VideoOverlay.GetSafeLockedObjectAt(i);
        if (!item)
            continue;
        item->DetachVideoOverlay();
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// PostInputEvent（脚本 API：向本窗口投递输入事件）
//---------------------------------------------------------------------------
void tTJSNI_Window::PostInputEvent(const ttstr& name, iTJSDispatch2* params)
{
    // posts input event
    if (!Window)
        return;

    static ttstr key_name(TJS_N("key"));
    static ttstr shift_name(TJS_N("shift"));

    // check input event name
    enum tEventType
    {
        etUnknown,
        etOnKeyDown,
        etOnKeyUp,
        etOnKeyPress
    } type;

    if (name == TJS_N("onKeyDown"))
        type = etOnKeyDown;
    else if (name == TJS_N("onKeyUp"))
        type = etOnKeyUp;
    else if (name == TJS_N("onKeyPress"))
        type = etOnKeyPress;
    else
        type = etUnknown;

    if (type == etUnknown)
        TVPThrowExceptionMessage(TVPSpecifiedEventNameIsUnknown, name);

    if (type == etOnKeyDown || type == etOnKeyUp)
    {
        // this needs params, "key" and "shift"
        if (params == NULL)
            TVPThrowExceptionMessage(TVPSpecifiedEventNeedsParameter, name);

        tjs_uint key;
        tjs_uint32 shift = 0;

        tTJSVariant val;
        if (TJS_SUCCEEDED(params->PropGet(0, key_name.c_str(), key_name.GetHint(), &val, params)))
            key = (tjs_int)val;
        else
            TVPThrowExceptionMessage(TVPSpecifiedEventNeedsParameter2, name, TJS_N("key"));

        if (TJS_SUCCEEDED(
                params->PropGet(0, shift_name.c_str(), shift_name.GetHint(), &val, params)))
            shift = (tjs_int)val;
        else
            TVPThrowExceptionMessage(TVPSpecifiedEventNeedsParameter2, name, TJS_N("shift"));

        if (type == etOnKeyDown)
            Window->PostKeyDown((tjs_uint16)key, shift);
        // else: 旧实现中 onKeyUp 投递为空操作，保持行为不变
    }
    else if (type == etOnKeyPress)
    {
        // this needs param, "key"
        if (params == NULL)
            TVPThrowExceptionMessage(TVPSpecifiedEventNeedsParameter, name);

        tjs_uint key;

        tTJSVariant val;
        if (TJS_SUCCEEDED(params->PropGet(0, key_name.c_str(), key_name.GetHint(), &val, params)))
            key = (tjs_int)val;
        else
            TVPThrowExceptionMessage(TVPSpecifiedEventNeedsParameter2, name, TJS_N("key"));

        Window->PostKeyPress((tjs_uint16)key);
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// Mouse Cursor management
//---------------------------------------------------------------------------
static tTJSHashTable<ttstr, tjs_int> TVPCursorTable;
tjs_int TVPGetCursor(const ttstr& name)
{
    // get placed path
    ttstr place(TVPSearchPlacedPath(name));

    // search in cache
    tjs_int* in_hash = TVPCursorTable.Find(place);
    if (in_hash)
        return *in_hash;
    return 0;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTJSNI_Window::SetMaskRegion
//---------------------------------------------------------------------------
void tTJSNI_Window::SetMaskRegion(tjs_int threshold)
{
    if (!Window)
        return;

    iTVPDrawDevice* dd = Window->GetDrawDevice();
    if (!dd)
        TVPThrowExceptionMessage(TVPWindowHasNoLayer);
    tTJSNI_BaseLayer* lay = dd->GetPrimaryLayer();
    if (!lay)
        TVPThrowExceptionMessage(TVPWindowHasNoLayer);
    // mask region creation is not implemented
}
//---------------------------------------------------------------------------

#define MK_SHIFT 4
#define MK_CONTROL 8
#define MK_ALT (0x20)
tjs_uint32 TVP_TShiftState_To_uint32(tjs_uint32 state)
{
    tjs_uint32 result = 0;
    if (state & MK_SHIFT)
    {
        result |= ssShift;
    }
    if (state & MK_CONTROL)
    {
        result |= ssCtrl;
    }
    if (state & MK_ALT)
    {
        result |= ssAlt;
    }
    return result;
}
tjs_uint32 TVP_TShiftState_From_uint32(tjs_uint32 state)
{
    tjs_uint32 result = 0;
    if (state & ssShift)
    {
        result |= MK_SHIFT;
    }
    if (state & ssCtrl)
    {
        result |= MK_CONTROL;
    }
    if (state & ssAlt)
    {
        result |= MK_ALT;
    }
    return result;
}
//---------------------------------------------------------------------------

// tTJSNC_Window : TJS Window class
//---------------------------------------------------------------------------
tjs_uint32 tTJSNC_Window::ClassID = -1;
tTJSNC_Window::tTJSNC_Window()
  : tTJSNativeClass(TJS_N("Window")){
        // registration of native members

        TJS_BEGIN_NATIVE_MEMBERS(Window) // constructor
        TJS_DECL_EMPTY_FINALIZE_METHOD
            //----------------------------------------------------------------------
            TJS_BEGIN_NATIVE_CONSTRUCTOR_DECL(/*var.name*/ _this,
                                              /*var.type*/ tTJSNI_Window,
                                              /*TJS class name*/ Window){return TJS_S_OK;
}
TJS_END_NATIVE_CONSTRUCTOR_DECL(/*TJS class name*/ Window)
//----------------------------------------------------------------------

//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ close)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
    _this->Close();
    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ close)
//----------------------------------------------------------------------
#ifdef USE_OBSOLETE_FUNCTIONS
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ beginMove)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
    _this->BeginMove();
    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ beginMove)
#endif
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ bringToFront)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
    _this->BringToFront();
    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ bringToFront)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ update)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
    tTVPUpdateType type = utNormal;
    if (numparams >= 2 && param[1]->Type() != tvtVoid)
        type = (tTVPUpdateType)(tjs_int)*param[0];
    _this->Update(type);
    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ update)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ showModal)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
    _this->ShowModal();
    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ showModal)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ setMaskRegion)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
    tjs_int threshold = 1;
    if (numparams >= 1 && param[0]->Type() != tvtVoid)
        threshold = (tjs_int)*param[0];
    _this->SetMaskRegion(threshold);
    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ setMaskRegion)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ removeMaskRegion)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
    _this->RemoveMaskRegion();
    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ removeMaskRegion)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ add)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
    if (numparams < 1)
        return TJS_E_BADPARAMCOUNT;
    tTJSVariantClosure clo = param[0]->AsObjectClosureNoAddRef();
    _this->Add(clo);
    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ add)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ remove)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
    if (numparams < 1)
        return TJS_E_BADPARAMCOUNT;
    tTJSVariantClosure clo = param[0]->AsObjectClosureNoAddRef();
    _this->Remove(clo);
    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ remove)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ setSize)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
    if (numparams < 2)
        return TJS_E_BADPARAMCOUNT;
    _this->SetSize(*param[0], *param[1]);
    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ setSize)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ setMinSize)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
    if (numparams < 2)
        return TJS_E_BADPARAMCOUNT;
    _this->SetMinSize(*param[0], *param[1]);
    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ setMinSize)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ setMaxSize)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
    if (numparams < 2)
        return TJS_E_BADPARAMCOUNT;
    _this->SetMaxSize(*param[0], *param[1]);
    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ setMaxSize)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ setPos) // not setPosition
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
    if (numparams < 2)
        return TJS_E_BADPARAMCOUNT;
    _this->SetPosition(*param[0], *param[1]);
    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ setPos)
//----------------------------------------------------------------------
#ifdef USE_OBSOLETE_FUNCTIONS
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ setLayerPos) // not setLayerPosition
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
    if (numparams < 2)
        return TJS_E_BADPARAMCOUNT;
    _this->SetLayerPosition(*param[0], *param[1]);
    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ setLayerPos)
#endif
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ setInnerSize)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
    if (numparams < 2)
        return TJS_E_BADPARAMCOUNT;
    _this->SetInnerSize(*param[0], *param[1]);
    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ setInnerSize)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ setZoom)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
    if (numparams < 2)
        return TJS_E_BADPARAMCOUNT;
    _this->SetZoom(*param[0], *param[1]);
    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ setZoom)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ hideMouseCursor)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
    _this->HideMouseCursor();
    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ hideMouseCursor)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ postInputEvent)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);

    if (numparams < 1)
        return TJS_E_BADPARAMCOUNT;
    ttstr eventname;
    iTJSDispatch2* eventparams = NULL;

    eventname = *param[0];
    if (numparams >= 2)
        eventparams = param[1]->AsObjectNoAddRef();

    _this->PostInputEvent(eventname, eventparams);

    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ postInputEvent)
//----------------------------------------------------------------------

//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ onResize)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);

    TVP_ACTION_INVOKE_BEGIN(0, "onResize", objthis);
    TVP_ACTION_INVOKE_END(tTJSVariantClosure(objthis, objthis));

    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ onResize)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ onMouseEnter)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);

    TVP_ACTION_INVOKE_BEGIN(0, "onMouseEnter", objthis);
    TVP_ACTION_INVOKE_END(tTJSVariantClosure(objthis, objthis));

    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ onMouseEnter)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ onMouseLeave)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);

    TVP_ACTION_INVOKE_BEGIN(0, "onMouseLeave", objthis);
    TVP_ACTION_INVOKE_END(tTJSVariantClosure(objthis, objthis));

    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ onMouseLeave)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ onClick)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);

    TVP_ACTION_INVOKE_BEGIN(2, "onClick", objthis);
    TVP_ACTION_INVOKE_MEMBER("x");
    TVP_ACTION_INVOKE_MEMBER("y");
    TVP_ACTION_INVOKE_END(tTJSVariantClosure(objthis, objthis));

    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ onClick)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ onDoubleClick)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);

    TVP_ACTION_INVOKE_BEGIN(2, "onDoubleClick", objthis);
    TVP_ACTION_INVOKE_MEMBER("x");
    TVP_ACTION_INVOKE_MEMBER("y");
    TVP_ACTION_INVOKE_END(tTJSVariantClosure(objthis, objthis));

    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ onDoubleClick)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ onMouseDown)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);

    TVP_ACTION_INVOKE_BEGIN(4, "onMouseDown", objthis);
    TVP_ACTION_INVOKE_MEMBER("x");
    TVP_ACTION_INVOKE_MEMBER("y");
    TVP_ACTION_INVOKE_MEMBER("button");
    TVP_ACTION_INVOKE_MEMBER("shift");
    TVP_ACTION_INVOKE_END(tTJSVariantClosure(objthis, objthis));

    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ onMouseDown)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ onMouseUp)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);

    TVP_ACTION_INVOKE_BEGIN(4, "onMouseUp", objthis);
    TVP_ACTION_INVOKE_MEMBER("x");
    TVP_ACTION_INVOKE_MEMBER("y");
    TVP_ACTION_INVOKE_MEMBER("button");
    TVP_ACTION_INVOKE_MEMBER("shift");
    TVP_ACTION_INVOKE_END(tTJSVariantClosure(objthis, objthis));

    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ onMouseUp)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ onMouseMove)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);

    TVP_ACTION_INVOKE_BEGIN(3, "onMouseMove", objthis);
    TVP_ACTION_INVOKE_MEMBER("x");
    TVP_ACTION_INVOKE_MEMBER("y");
    TVP_ACTION_INVOKE_MEMBER("shift");
    TVP_ACTION_INVOKE_END(tTJSVariantClosure(objthis, objthis));

    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ onMouseMove)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ onMouseWheel)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);

    TVP_ACTION_INVOKE_BEGIN(4, "onMouseWheel", objthis);
    TVP_ACTION_INVOKE_MEMBER("shift");
    TVP_ACTION_INVOKE_MEMBER("delta");
    TVP_ACTION_INVOKE_MEMBER("x");
    TVP_ACTION_INVOKE_MEMBER("y");
    TVP_ACTION_INVOKE_END(tTJSVariantClosure(objthis, objthis));

    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ onMouseWheel)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ onTouchDown)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);

    TVP_ACTION_INVOKE_BEGIN(5, "onTouchDown", objthis);
    TVP_ACTION_INVOKE_MEMBER("x");
    TVP_ACTION_INVOKE_MEMBER("y");
    TVP_ACTION_INVOKE_MEMBER("cx");
    TVP_ACTION_INVOKE_MEMBER("cy");
    TVP_ACTION_INVOKE_MEMBER("id");
    TVP_ACTION_INVOKE_END(tTJSVariantClosure(objthis, objthis));

    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ onTouchDown)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ onTouchUp)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);

    TVP_ACTION_INVOKE_BEGIN(5, "onTouchUp", objthis);
    TVP_ACTION_INVOKE_MEMBER("x");
    TVP_ACTION_INVOKE_MEMBER("y");
    TVP_ACTION_INVOKE_MEMBER("cx");
    TVP_ACTION_INVOKE_MEMBER("cy");
    TVP_ACTION_INVOKE_MEMBER("id");
    TVP_ACTION_INVOKE_END(tTJSVariantClosure(objthis, objthis));

    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ onTouchUp)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ onTouchMove)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);

    TVP_ACTION_INVOKE_BEGIN(5, "onTouchMove", objthis);
    TVP_ACTION_INVOKE_MEMBER("x");
    TVP_ACTION_INVOKE_MEMBER("y");
    TVP_ACTION_INVOKE_MEMBER("cx");
    TVP_ACTION_INVOKE_MEMBER("cy");
    TVP_ACTION_INVOKE_MEMBER("id");
    TVP_ACTION_INVOKE_END(tTJSVariantClosure(objthis, objthis));

    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ onTouchMove)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ onTouchScaling)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);

    TVP_ACTION_INVOKE_BEGIN(5, "onTouchScaling", objthis);
    TVP_ACTION_INVOKE_MEMBER("startdistance");
    TVP_ACTION_INVOKE_MEMBER("currentdistance");
    TVP_ACTION_INVOKE_MEMBER("cx");
    TVP_ACTION_INVOKE_MEMBER("cy");
    TVP_ACTION_INVOKE_MEMBER("flag");
    TVP_ACTION_INVOKE_END(tTJSVariantClosure(objthis, objthis));

    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ onTouchScaling)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ onTouchRotate)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);

    TVP_ACTION_INVOKE_BEGIN(6, "onTouchRotate", objthis);
    TVP_ACTION_INVOKE_MEMBER("startangle");
    TVP_ACTION_INVOKE_MEMBER("currentangle");
    TVP_ACTION_INVOKE_MEMBER("distance");
    TVP_ACTION_INVOKE_MEMBER("cx");
    TVP_ACTION_INVOKE_MEMBER("cy");
    TVP_ACTION_INVOKE_MEMBER("flag");
    TVP_ACTION_INVOKE_END(tTJSVariantClosure(objthis, objthis));

    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ onTouchRotate)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ onMultiTouch)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);

    TVP_ACTION_INVOKE_BEGIN(0, "onMultiTouch", objthis);
    TVP_ACTION_INVOKE_END(tTJSVariantClosure(objthis, objthis));

    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ onMultiTouch)

//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ onKeyDown)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);

    TVP_ACTION_INVOKE_BEGIN(2, "onKeyDown", objthis);
    TVP_ACTION_INVOKE_MEMBER("key");
    TVP_ACTION_INVOKE_MEMBER("shift");
    TVP_ACTION_INVOKE_END(tTJSVariantClosure(objthis, objthis));

    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ onKeyDown)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ onKeyUp)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);

    TVP_ACTION_INVOKE_BEGIN(2, "onKeyUp", objthis);
    TVP_ACTION_INVOKE_MEMBER("key");
    TVP_ACTION_INVOKE_MEMBER("shift");
    TVP_ACTION_INVOKE_END(tTJSVariantClosure(objthis, objthis));

    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ onKeyUp)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ onKeyPress)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);

    TVP_ACTION_INVOKE_BEGIN(1, "onKeyPress", objthis);
    TVP_ACTION_INVOKE_MEMBER("key");
    TVP_ACTION_INVOKE_END(tTJSVariantClosure(objthis, objthis));

    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ onKeyPress)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ onFileDrop)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);

    TVP_ACTION_INVOKE_BEGIN(1, "onFileDrop", objthis);
    TVP_ACTION_INVOKE_MEMBER("files");
    TVP_ACTION_INVOKE_END(tTJSVariantClosure(objthis, objthis));

    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ onFileDrop)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ onCloseQuery)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
    /*
            // this event does not call "action" method
            TVP_ACTION_INVOKE_BEGIN(1, "onCloseQuery", objthis);
            TVP_ACTION_INVOKE_MEMBER("canClose");
            TVP_ACTION_INVOKE_END(tTJSVariantClosure(objthis, objthis));
    */
    _this->OnCloseQueryCalled(0 != (tjs_int)*param[0]);

    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ onCloseQuery)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ onPopupHide)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);

    TVP_ACTION_INVOKE_BEGIN(0, "onPopupHide", objthis);
    TVP_ACTION_INVOKE_END(tTJSVariantClosure(objthis, objthis));

    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ onPopupHide)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ onActivate)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);

    TVP_ACTION_INVOKE_BEGIN(0, "onActivate", objthis);
    TVP_ACTION_INVOKE_END(tTJSVariantClosure(objthis, objthis));

    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ onActivate)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ onDeactivate)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);

    TVP_ACTION_INVOKE_BEGIN(0, "onDeactivate", objthis);
    TVP_ACTION_INVOKE_END(tTJSVariantClosure(objthis, objthis));

    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ onDeactivate)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ onDisplayRotate)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);

    TVP_ACTION_INVOKE_BEGIN(5, "onDisplayRotate", objthis);
    TVP_ACTION_INVOKE_MEMBER("orientation");
    TVP_ACTION_INVOKE_MEMBER("angle");
    TVP_ACTION_INVOKE_MEMBER("bpp");
    TVP_ACTION_INVOKE_MEMBER("width");
    TVP_ACTION_INVOKE_MEMBER("height");
    TVP_ACTION_INVOKE_END(tTJSVariantClosure(objthis, objthis));

    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ onDisplayRotate)
//----------------------------------------------------------------------

//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(visible){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
*result = _this->GetVisible();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
    _this->SetVisible(*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(visible)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(caption){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
ttstr res;
_this->GetCaption(res);
*result = res;
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
    _this->SetCaption(*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(caption)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(width){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
*result = _this->GetWidth();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
    _this->SetWidth(*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(width)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(height){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
*result = _this->GetHeight();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
    _this->SetHeight(*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(height)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(minWidth){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
*result = _this->GetMinWidth();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
    _this->SetMinWidth(*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(minWidth)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(minHeight){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
*result = _this->GetMinHeight();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
    _this->SetMinHeight(*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(minHeight)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(maxWidth){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
*result = _this->GetMaxWidth();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
    _this->SetMaxWidth(*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(maxWidth)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(maxHeight){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
*result = _this->GetMaxHeight();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
    _this->SetMaxHeight(*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(maxHeight)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(left){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
*result = _this->GetLeft();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
    _this->SetLeft(*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(left)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(top){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
*result = _this->GetTop();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
    _this->SetTop(*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(top)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(focusable){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
*result = (tjs_int)_this->GetFocusable();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
    _this->SetFocusable(0 != (tjs_int)*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(focusable)
//----------------------------------------------------------------------
#ifdef USE_OBSOLETE_FUNCTIONS
TJS_BEGIN_NATIVE_PROP_DECL(layerLeft){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
*result = _this->GetLayerLeft();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
    _this->SetLayerLeft(*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(layerLeft)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(layerTop){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
*result = _this->GetLayerTop();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
    _this->SetLayerTop(*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(layerTop)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(innerSunken){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
*result = _this->GetInnerSunken();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
    _this->SetInnerSunken(param->operator bool());
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(innerSunken)
#endif
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(innerWidth){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
*result = _this->GetInnerWidth();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
    _this->SetInnerWidth(*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(innerWidth)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(innerHeight){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
*result = _this->GetInnerHeight();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
    _this->SetInnerHeight(*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(innerHeight)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(zoomNumer){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
*result = _this->GetZoomNumer();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
    _this->SetZoomNumer(*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(zoomNumer)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(zoomDenom){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
*result = _this->GetZoomDenom();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
    _this->SetZoomDenom(*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(zoomDenom)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(borderStyle){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
*result = (tjs_int)_this->GetBorderStyle();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
    _this->SetBorderStyle((tTVPBorderStyle)(tjs_int)*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(borderStyle)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(stayOnTop){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
*result = _this->GetStayOnTop();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
    _this->SetStayOnTop(param->operator bool());
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(stayOnTop)
//----------------------------------------------------------------------
#ifdef USE_OBSOLETE_FUNCTIONS
TJS_BEGIN_NATIVE_PROP_DECL(showScrollBars){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
*result = _this->GetShowScrollBars();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
    _this->SetShowScrollBars(param->operator bool());
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(showScrollBars)
#endif
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(useMouseKey){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
*result = _this->GetUseMouseKey();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
    _this->SetUseMouseKey(param->operator bool());
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(useMouseKey)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(trapKey){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
*result = _this->GetTrapKey();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
    _this->SetTrapKey(param->operator bool());
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(trapKey)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(imeMode) // not defaultImeMode
{TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
*result = (tjs_int)_this->GetDefaultImeMode();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
    _this->SetDefaultImeMode((tTVPImeMode)(tjs_int)*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(imeMode)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(mouseCursorState){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
*result = (tjs_int)_this->GetMouseCursorState();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
    _this->SetMouseCursorState((tTVPMouseCursorState)(tjs_int)*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(mouseCursorState)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(fullScreen){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
*result = _this->GetFullScreen();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
    _this->SetFullScreen(0 != (tjs_int)*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(fullScreen)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(mainWindow) /* static */
{TJS_BEGIN_NATIVE_PROP_GETTER{
    if (TVPMainWindow){iTJSDispatch2* dsp = TVPMainWindow->GetOwnerNoAddRef();
*result = tTJSVariant(dsp, dsp);
}
else
{
    *result = tTJSVariant((iTJSDispatch2*)NULL);
}
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(mainWindow)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(focusedLayer){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
tTJSNI_BaseLayer* lay = _this->GetDrawDevice()->GetFocusedLayer();
if (lay && lay->GetOwnerNoAddRef())
    *result = tTJSVariant(lay->GetOwnerNoAddRef(), lay->GetOwnerNoAddRef());
else
    *result = tTJSVariant((iTJSDispatch2*)NULL);
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);

    tTJSNI_BaseLayer* to = NULL;

    if (param->Type() != tvtVoid)
    {
        tTJSVariantClosure clo = param->AsObjectClosureNoAddRef();
        if (clo.Object)
        {
            if (TJS_FAILED(clo.Object->NativeInstanceSupport(
                    TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID, (iTJSNativeInstance**)&to)))
                TVPThrowExceptionMessage(TVPSpecifyLayer);
        }
    }

    _this->GetDrawDevice()->SetFocusedLayer(to);

    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(focusedLayer)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(primaryLayer){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
tTJSNI_BaseLayer* pri = _this->GetDrawDevice()->GetPrimaryLayer();
if (!pri)
    TVPThrowExceptionMessage(TVPWindowHasNoLayer);

if (pri && pri->GetOwnerNoAddRef())
    *result = tTJSVariant(pri->GetOwnerNoAddRef(), pri->GetOwnerNoAddRef());
else
    *result = tTJSVariant((iTJSDispatch2*)NULL);
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(primaryLayer)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(waitVSync){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
*result = _this->GetWaitVSync() ? 1 : 0;
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
    _this->SetWaitVSync(((tjs_int)*param) ? true : false);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(waitVSync)
//---------------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(layerTreeOwnerInterface){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
*result = reinterpret_cast<tjs_int64>(static_cast<iTVPLayerTreeOwner*>(_this->GetWindow()));
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(layerTreeOwnerInterface)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(maximized){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
*result = false;
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(maximized)
//---------------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(minimized){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
*result = false;
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(minimized)
//---------------------------------------------------------------------------

TJS_END_NATIVE_MEMBERS
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTJSNC_Window::CreateNativeInstance : returns proper instance object
//---------------------------------------------------------------------------
tTJSNativeInstance* tTJSNC_Window::CreateNativeInstance()
{
    return new tTJSNI_Window();
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPCreateNativeClass_Window
//---------------------------------------------------------------------------
tTJSNativeClass* TVPCreateNativeClass_Window()
{
    tTJSNativeClass* cls = new tTJSNC_Window();
    static tjs_uint32 TJS_NCM_CLASSID;
    TJS_NCM_CLASSID = tTJSNC_Window::ClassID;
    //---------------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(findFullScreenCandidates)
    {
        if (numparams < 5)
            return TJS_E_BADPARAMCOUNT;

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL_OUTER(cls, findFullScreenCandidates)
    //---------------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(registerMessageReceiver)
    {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
        if (numparams < 3)
            return TJS_E_BADPARAMCOUNT;

        _this->RegisterWindowMessageReceiver((tTVPWMRRegMode)((tjs_int)*param[0]),
                                             reinterpret_cast<void*>(param[1]->AsInteger()),
                                             reinterpret_cast<const void*>(param[2]->AsInteger()));

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL_OUTER(cls, registerMessageReceiver)
    //---------------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(getTouchPoint)
    {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
        if (numparams < 1)
            return TJS_E_BADPARAMCOUNT;

        tjs_int index = (tjs_int)*param[0];
        if (index < _this->GetTouchPointCount())
        {
            if (result)
            {
                iTJSDispatch2* object = TJSCreateDictionaryObject();

                static ttstr startX_name(TJS_N("startX"));
                static ttstr startY_name(TJS_N("startY"));
                static ttstr X_name(TJS_N("x"));
                static ttstr Y_name(TJS_N("y"));
                static ttstr ID_name(TJS_N("ID"));
                {
                    tTJSVariant val(_this->GetTouchPointStartX(index));
                    if (TJS_FAILED(object->PropSet(TJS_MEMBERENSURE | TJS_IGNOREPROP,
                                                   startX_name.c_str(), startX_name.GetHint(), &val,
                                                   object)))
                        TVPThrowInternalError;
                }
                {
                    tTJSVariant val(_this->GetTouchPointStartY(index));
                    if (TJS_FAILED(object->PropSet(TJS_MEMBERENSURE | TJS_IGNOREPROP,
                                                   startY_name.c_str(), startY_name.GetHint(), &val,
                                                   object)))
                        TVPThrowInternalError;
                }
                {
                    tTJSVariant val(_this->GetTouchPointX(index));
                    if (TJS_FAILED(object->PropSet(TJS_MEMBERENSURE | TJS_IGNOREPROP,
                                                   X_name.c_str(), X_name.GetHint(), &val, object)))
                        TVPThrowInternalError;
                }
                {
                    tTJSVariant val(_this->GetTouchPointY(index));
                    if (TJS_FAILED(object->PropSet(TJS_MEMBERENSURE | TJS_IGNOREPROP,
                                                   Y_name.c_str(), Y_name.GetHint(), &val, object)))
                        TVPThrowInternalError;
                }
                {
                    tTJSVariant val(_this->GetTouchPointID(index));
                    if (TJS_FAILED(object->PropSet(TJS_MEMBERENSURE | TJS_IGNOREPROP,
                                                   ID_name.c_str(), ID_name.GetHint(), &val,
                                                   object)))
                        TVPThrowInternalError;
                }
                tTJSVariant objval(object, object);
                object->Release();
                *result = objval;
            }
        }
        else
        {
            return TJS_E_INVALIDPARAM;
        }
        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL_OUTER(cls, getTouchPoint)
    //---------------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(getTouchVelocity)
    {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
        if (numparams < 4)
            return TJS_E_BADPARAMCOUNT;

        tjs_int id = (tjs_int)*param[0];
        float x, y, speed;
        bool ret = _this->GetTouchVelocity(id, x, y, speed);
        if (result)
        {
            *result = ret ? (tjs_int)1 : (tjs_int)0;
        }
        if (ret)
        {
            (*param[1]) = (tjs_real)x;
            (*param[2]) = (tjs_real)y;
            (*param[3]) = (tjs_real)speed;
        }
        else
        {
            (*param[1]) = (tjs_real)0.0;
            (*param[2]) = (tjs_real)0.0;
            (*param[3]) = (tjs_real)0.0;
        }

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL_OUTER(cls, getTouchVelocity)
    //---------------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(getMouseVelocity)
    {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
        if (numparams < 3)
            return TJS_E_BADPARAMCOUNT;

        float x, y, speed;
        bool ret = _this->GetMouseVelocity(x, y, speed);
        if (result)
        {
            *result = ret ? (tjs_int)1 : (tjs_int)0;
        }
        if (ret)
        {
            (*param[0]) = (tjs_real)x;
            (*param[1]) = (tjs_real)y;
            (*param[2]) = (tjs_real)speed;
        }
        else
        {
            (*param[0]) = (tjs_real)0.0;
            (*param[1]) = (tjs_real)0.0;
            (*param[2]) = (tjs_real)0.0;
        }

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL_OUTER(cls, getMouseVelocity)
    //---------------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(resetMouseVelocity)
    {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
        _this->ResetMouseVelocity();
        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL_OUTER(cls, resetMouseVelocity)
    //---------------------------------------------------------------------------
    TJS_BEGIN_NATIVE_PROP_DECL(HWND){TJS_BEGIN_NATIVE_PROP_GETTER{
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
    *result = (tTVInteger)(tjs_intptr_t)_this /*->GetWindowHandleForPlugin()*/;
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL_OUTER(cls, HWND)
//---------------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(drawDevice){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
*result = _this->GetDrawDeviceObject();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
    _this->SetDrawDeviceObject(*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL_OUTER(cls, drawDevice)
//---------------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(touchScaleThreshold){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
*result = _this->GetTouchScaleThreshold();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
    _this->SetTouchScaleThreshold(*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL_OUTER(cls, touchScaleThreshold)
//---------------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(touchRotateThreshold){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
*result = _this->GetTouchRotateThreshold();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
    _this->SetTouchRotateThreshold(*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL_OUTER(cls, touchRotateThreshold)
//---------------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(touchPointCount){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
*result = _this->GetTouchPointCount();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL_OUTER(cls, touchPointCount)
//---------------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(hintDelay){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
*result = _this->GetHintDelay();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
    _this->SetHintDelay(*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL_OUTER(cls, hintDelay)
//---------------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(enableTouch){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
*result = _this->GetEnableTouch() ? 1 : 0;
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
    _this->SetEnableTouch(((tjs_int)*param) ? true : false);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL_OUTER(cls, enableTouch)
//---------------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(displayOrientation){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
*result = _this->GetDisplayOrientation();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL_OUTER(cls, displayOrientation)
//---------------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(displayRotate){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Window);
*result = _this->GetDisplayRotate();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL_OUTER(cls, displayRotate)
//---------------------------------------------------------------------------

// TVPGetDisplayColorFormat(); // this will be ran only once here

return cls;
}
//---------------------------------------------------------------------------