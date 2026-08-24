#include "tjsCommHead.h"
#include "tjsNativeVideoOverlay.h"

#include "TVPMsg.h"
#include "TVPSystem.h"
#include "TVPDebug.h"
#include "TVPWindow.h"

#include "krmovie.h"

#include "tjsNativeLayer.h"
#include "tjsNativeWindow.h"

//---------------------------------------------------------------------------
// tTJSNI_BaseVideoOverlay
//---------------------------------------------------------------------------
tTJSNI_BaseVideoOverlay::tTJSNI_BaseVideoOverlay()
{
    ActionOwner.Object = ActionOwner.ObjThis = NULL;
    Status = ssUnload;
    Owner = NULL;
    CanDeliverEvents = true;
}
//---------------------------------------------------------------------------
tjs_error tTJSNI_BaseVideoOverlay::Construct(tjs_int numparams,
                                             tTJSVariant** param,
                                             iTJSDispatch2* tjs_obj)
{
    tjs_error hr = inherited::Construct(numparams, param, tjs_obj);
    if (TJS_FAILED(hr))
        return hr;

    if (numparams < 1)
        return TJS_E_BADPARAMCOUNT;

    tTJSVariantClosure clo = param[0]->AsObjectClosureNoAddRef();
    if (clo.Object == NULL)
        TVPThrowExceptionMessage(TVPSpecifyWindow);
    tTJSNI_Window* win = NULL;
    if (TJS_FAILED(clo.Object->NativeInstanceSupport(TJS_NIS_GETINSTANCE, tTJSNC_Window::ClassID,
                                                     (iTJSNativeInstance**)&win)))
        TVPThrowExceptionMessage(TVPSpecifyWindow);
    if (!win)
        TVPThrowExceptionMessage(TVPSpecifyWindow);
    Window = win;

    Window->RegisterVideoOverlayObject(this);

    ActionOwner = param[0]->AsObjectClosure();
    Owner = tjs_obj;

    return TJS_S_OK;
}
//---------------------------------------------------------------------------
void tTJSNI_BaseVideoOverlay::Invalidate()
{
    Owner = NULL;
    if (Window)
        Window->UnregisterVideoOverlayObject(this);
    ActionOwner.Release();

    inherited::Invalidate();
}
//---------------------------------------------------------------------------
ttstr tTJSNI_BaseVideoOverlay::GetStatusString() const
{
    static ttstr unload(TJS_N("unload"));
    static ttstr play(TJS_N("play"));
    static ttstr stop(TJS_N("stop"));
    static ttstr unknown(TJS_N("unknown"));
    static ttstr pause(TJS_N("pause"));
    static ttstr ready(TJS_N("ready"));

    switch (Status)
    {
        case ssUnload:
            return unload;
        case ssPlay:
            return play;
        case ssStop:
            return stop;
        case ssPause:
            return pause;
        case ssReady:
            return ready;
        default:
            return unknown;
    }
}
//---------------------------------------------------------------------------
void tTJSNI_BaseVideoOverlay::SetStatus(tTVPSoundStatus s)
{
    // this function may call the onStatusChanged event immediately

    if (Status != s)
    {
        Status = s;

        if (Owner)
        {
            // Cancel Previous un-delivered Events
            TVPCancelSourceEvents(Owner);

            // fire
            if (CanDeliverEvents)
            {
                // fire onStatusChanged event
                tTJSVariant param(GetStatusString());
                static ttstr eventname(TJS_N("onStatusChanged"));
                TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 1, &param);
            }
        }
    }
}
//---------------------------------------------------------------------------
void tTJSNI_BaseVideoOverlay::SetStatusAsync(tTVPSoundStatus s)
{
    // this function posts the onStatusChanged event

    if (Status != s)
    {
        Status = s;

        if (Owner)
        {
            // Cancel Previous un-delivered Events
            TVPCancelSourceEvents(Owner);

            // fire
            if (CanDeliverEvents)
            {
                // fire onStatusChanged event
                tTJSVariant param(GetStatusString());
                static ttstr eventname(TJS_N("onStatusChanged"));
                TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_POST, 1, &param);
            }
        }
    }
}
//---------------------------------------------------------------------------
void tTJSNI_BaseVideoOverlay::FireCallbackCommand(const ttstr& command, const ttstr& argument)
{
    // fire call back command event.
    // this is always synchronized event.
    if (Owner)
    {
        // fire
        if (CanDeliverEvents)
        {
            // fire onStatusChanged event
            tTJSVariant param[2] = {command, argument};
            static ttstr eventname(TJS_N("onCallbackCommand"));
            TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 2, param);
        }
    }
}
//---------------------------------------------------------------------------
void tTJSNI_BaseVideoOverlay::FirePeriodEvent(tTVPPeriodEventReason reason)
{
    // fire onPeriod event
    // this is always synchronized event.

    if (Owner)
    {
        // fire
        if (CanDeliverEvents)
        {
            // fire onPeriod event
            tTJSVariant param[1] = {(tjs_int)reason};
            static ttstr eventname(TJS_N("onPeriod"));
            TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 1, param);
        }
    }
}
//---------------------------------------------------------------------------
void tTJSNI_BaseVideoOverlay::FireFrameUpdateEvent(tjs_int frame)
{
    // fire onFrameUpdate event
    // this is always synchronized event.

    if (Owner)
    {
        // fire
        if (CanDeliverEvents)
        {
            // fire onPeriod event
            tTJSVariant param[1] = {frame};
            static ttstr eventname(TJS_N("onFrameUpdate"));
            TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 1, param);
        }
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
static std::vector<tTJSNI_VideoOverlay*> TVPVideoOverlayVector;
//---------------------------------------------------------------------------
static void TVPAddVideOverlay(tTJSNI_VideoOverlay* ovl)
{
    TVPVideoOverlayVector.push_back(ovl);
}
//---------------------------------------------------------------------------
static void TVPRemoveVideoOverlay(tTJSNI_VideoOverlay* ovl)
{
    std::vector<tTJSNI_VideoOverlay*>::iterator i;
    i = std::find(TVPVideoOverlayVector.begin(), TVPVideoOverlayVector.end(), ovl);
    if (i != TVPVideoOverlayVector.end())
        TVPVideoOverlayVector.erase(i);
}
//---------------------------------------------------------------------------
static void TVPShutdownVideoOverlay()
{
    // shutdown all overlay object and release krmovie.dll / krflash.dll
    std::vector<tTJSNI_VideoOverlay*>::iterator i;
    for (i = TVPVideoOverlayVector.begin(); i != TVPVideoOverlayVector.end(); i++)
    {
        (*i)->Shutdown();
    }
}
static tTVPAtExit TVPShutdownVideoOverlayAtExit(TVP_ATEXIT_PRI_PREPARE, TVPShutdownVideoOverlay);
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTJSNI_VideoOverlay
//---------------------------------------------------------------------------
tTJSNI_VideoOverlay::tTJSNI_VideoOverlay() : EventQueue(this, &tTJSNI_VideoOverlay::WndProc)
{
    VideoOverlay = NULL;
    Rect.left = 0;
    Rect.top = 0;
    Rect.right = 320;
    Rect.bottom = 240;
    Visible = false;
    //	OwnerWindow = NULL;
    LocalTempStorageHolder = NULL;

    EventQueue.Allocate();

    Layer1 = NULL;
    Layer2 = NULL;
    Mode = vomOverlay;
    Loop = false;
    IsPrepare = false;
    SegLoopStartFrame = -1;
    SegLoopEndFrame = -1;
    IsEventPast = false;
    EventFrame = -1;

    Bitmap[0] = Bitmap[1] = NULL;
    BmpBits[0] = BmpBits[1] = NULL;
}
//---------------------------------------------------------------------------
tjs_error tTJSNI_VideoOverlay::Construct(tjs_int numparams,
                                         tTJSVariant** param,
                                         iTJSDispatch2* tjs_obj)
{
    tjs_error hr = inherited::Construct(numparams, param, tjs_obj);
    if (TJS_FAILED(hr))
        return hr;

    return TJS_S_OK;
}
//---------------------------------------------------------------------------
void tTJSNI_VideoOverlay::Invalidate()
{
    inherited::Invalidate();

    Close();
    if (CachedOverlay)
    {
        CachedOverlay->Release();
        CachedOverlay = nullptr;
    }
    EventQueue.Deallocate();
}
//---------------------------------------------------------------------------
void tTJSNI_VideoOverlay::Open(const ttstr& _name)
{
    // open

    // first, close
    Close();

    // check window
    if (!Window)
        TVPThrowExceptionMessage(TVPWindowAlreadyMissing);

    // open target storage
    ttstr name(_name);
    ttstr param;

    const tjs_char* param_pos;
    int param_pos_ind;
    param_pos = TJS_strchr(name.c_str(), TJS_N('?'));
    param_pos_ind = (int)(param_pos - name.c_str());
    if (param_pos != NULL)
    {
        param = param_pos;
        name = ttstr(name, param_pos_ind);
    }

    tTJSBinaryStream* istream = NULL;
    long size;
    ttstr ext = TVPExtractStorageExt(name).c_str();
    ext.ToLowerCase();

    {
        try
        {
            istream = TVPCreateStream(name);
            size = (long)istream->GetSize();
        }
        catch (...)
        {
            if (istream)
                delete istream;
            throw;
        }
    }

    // 'istream' is an IStream instance at this point

    // create video overlay object
    try
    {
        if (CachedOverlay && CachedOverlayMode == Mode && CachedPlayingFile == _name)
        {
            VideoOverlay = CachedOverlay;
            CachedOverlay = nullptr;
            VideoOverlay->Rewind();
        }
        else
        {
            if (CachedOverlay)
            {
                CachedOverlay->Release();
                CachedOverlay = nullptr;
            }
            if (Mode == vomLayer)
                GetVideoLayerObject(EventQueue.GetOwner(), istream, name.c_str(), ext.c_str(), size,
                                    &VideoOverlay);
            else if (Mode == vomMixer)
                GetMixingVideoOverlayObject(EventQueue.GetOwner(), istream, name.c_str(),
                                            ext.c_str(), size, &VideoOverlay);
            else if (Mode == vomMFEVR)
                GetMFVideoOverlayObject(EventQueue.GetOwner(), istream, name.c_str(), ext.c_str(),
                                        size, &VideoOverlay);
            else
                GetVideoOverlayObject(EventQueue.GetOwner(), istream, name.c_str(), ext.c_str(),
                                      size, &VideoOverlay);
        }
        if ((Mode == vomOverlay) || (Mode == vomMixer) || (Mode == vomMFEVR))
        {
            ResetOverlayParams();
        }
        else
        { // set font and back buffer to layerVideo
            long width, height;
            long size;
            VideoOverlay->GetVideoSize(&width, &height);

            if (width <= 0 || height <= 0)
                TVPThrowExceptionMessage(TVPErrorInKrMovieDLL,
                                         (const tjs_char*)TVPInvalidVideoSize);

            size = width * height * 4;
            if (Bitmap[0] != NULL)
                delete Bitmap[0];
            if (Bitmap[1] != NULL)
                delete Bitmap[1];
            Bitmap[0] = new tTVPBaseTexture(width, height, 32);
            Bitmap[1] = new tTVPBaseTexture(width, height, 32);
            VideoOverlay->SetVideoBuffer(Bitmap[0], Bitmap[1], size);
        }
    }
    catch (...)
    {
        Close();
        throw;
    }

    // set Status
    ClearWndProcMessages();
    SetStatus(ssStop);
    CachedPlayingFile = _name;
    CachedOverlayMode = Mode;
    if (Loop)
        VideoOverlay->SetLoopSegement(0, -1);
}
//---------------------------------------------------------------------------
void tTJSNI_VideoOverlay::Close()
{
    // close
    // release VideoOverlay object
    if (VideoOverlay)
    {
        if (CachedOverlay)
        {
            CachedOverlay->Release();
            CachedOverlay = nullptr;
        }
        VideoOverlay->SetVisible(false);
        VideoOverlay->Pause();
        CachedOverlay = VideoOverlay;
        VideoOverlay = NULL;
    }
    if (LocalTempStorageHolder)
        delete LocalTempStorageHolder, LocalTempStorageHolder = NULL;
    ClearWndProcMessages();
    SetStatus(ssUnload);

    if (Bitmap[0])
        delete Bitmap[0];
    if (Bitmap[1])
        delete Bitmap[1];

    Bitmap[0] = Bitmap[1] = NULL;
    BmpBits[0] = BmpBits[1] = NULL;
}
//---------------------------------------------------------------------------
void tTJSNI_VideoOverlay::Shutdown()
{
    // shutdown the system
    // this functions closes the overlay object, but must not fire any events.
    bool c = CanDeliverEvents;
    ClearWndProcMessages();
    SetStatus(ssUnload);
    try
    {
        if (VideoOverlay)
        {
            if (CachedOverlay)
            {
                CachedOverlay->Release();
                CachedOverlay = nullptr;
            }
            VideoOverlay->SetVisible(false);
            VideoOverlay->Pause();
            CachedOverlay = VideoOverlay;
            VideoOverlay = NULL;
        }
    }
    catch (...)
    {
        CanDeliverEvents = c;
        throw;
    }
    CanDeliverEvents = c;
}
//---------------------------------------------------------------------------
void tTJSNI_VideoOverlay::Disconnect()
{
    // disconnect the object
    Shutdown();

    Window = NULL;
}
//---------------------------------------------------------------------------
void tTJSNI_VideoOverlay::Play()
{
    // start playing
    if (VideoOverlay)
    {
        VideoOverlay->Play();
        ClearWndProcMessages();
        if (Mode != vomMFEVR)
            SetStatus(ssPlay);
    }
}
//---------------------------------------------------------------------------
void tTJSNI_VideoOverlay::Stop()
{
    // stop playing
    if (VideoOverlay)
    {
        VideoOverlay->Stop();
        ClearWndProcMessages();
        if (Mode != vomMFEVR)
            SetStatus(ssStop);
    }
}
//---------------------------------------------------------------------------
void tTJSNI_VideoOverlay::Pause()
{
    // pause playing
    if (VideoOverlay)
    {
        VideoOverlay->Pause();
        if (Mode != vomMFEVR)
            SetStatus(ssPause);
    }
}
void tTJSNI_VideoOverlay::Rewind()
{
    // rewind playing
    if (VideoOverlay)
    {
        VideoOverlay->Rewind();
        if (Status == ssPlay)
            VideoOverlay->Play();
        ClearWndProcMessages();

        if (EventFrame >= 0 && IsEventPast)
            IsEventPast = false;
    }
}
void tTJSNI_VideoOverlay::Prepare()
{ // prepare movie
    if (VideoOverlay && (Mode == vomLayer))
    {
        Pause();
        Rewind();
        IsPrepare = true;
        Play();
    }
}
void tTJSNI_VideoOverlay::SetSegmentLoop(int comeFrame, int goFrame)
{
    SegLoopStartFrame = comeFrame;
    SegLoopEndFrame = goFrame;
    if (VideoOverlay)
        VideoOverlay->SetLoopSegement(SegLoopStartFrame, SegLoopEndFrame);
}
void tTJSNI_VideoOverlay::SetPeriodEvent(int eventFrame)
{
    EventFrame = eventFrame;

    if (eventFrame <= GetFrame())
        IsEventPast = true;
    else
        IsEventPast = false;
}
//---------------------------------------------------------------------------
void tTJSNI_VideoOverlay::SetRectangleToVideoOverlay()
{
    // set Rectangle to video overlay
    if (VideoOverlay /*&& OwnerWindow*/)
    {
        tjs_int ofsx, ofsy;
        Window->GetVideoOffset(ofsx, ofsy);
        tjs_int l = Rect.left;
        tjs_int t = Rect.top;
        tjs_int r = Rect.right;
        tjs_int b = Rect.bottom;
        TVPAddLog(TJS_N("Video zoom: (") + ttstr(l) + TJS_N(",") + ttstr(t) + TJS_N(")-(") +
                  ttstr(r) + TJS_N(",") + ttstr(b) + TJS_N(") ->"));
        Window->ZoomRectangle(l, t, r, b);
        TVPAddLog(TJS_N("(") + ttstr(l) + TJS_N(",") + ttstr(t) + TJS_N(")-(") + ttstr(r) +
                  TJS_N(",") + ttstr(b) + TJS_N(")"));
        //	RECT rect = {l + ofsx, t + ofsy, r + ofsx, b + ofsy};
        VideoOverlay->SetRect(l + ofsx, t + ofsy, r + ofsx, b + ofsy);
    }
}
//---------------------------------------------------------------------------
void tTJSNI_VideoOverlay::SetPosition(tjs_int left, tjs_int top)
{
    if (Mode == vomLayer)
    {
        if (Layer1 != NULL)
            Layer1->SetPosition(left, top);
        if (Layer2 != NULL)
            Layer2->SetPosition(left, top);
    }
    else
    {
        Rect.set_offsets(left, top);
        SetRectangleToVideoOverlay();
    }
}
//---------------------------------------------------------------------------
void tTJSNI_VideoOverlay::SetSize(tjs_int width, tjs_int height)
{
    if (Mode == vomLayer)
        return;

    Rect.set_size(width, height);
    SetRectangleToVideoOverlay();
}
//---------------------------------------------------------------------------
void tTJSNI_VideoOverlay::SetBounds(const tTVPRect& rect)
{
    if (Mode == vomLayer)
        return;

    Rect = rect;
    SetRectangleToVideoOverlay();
}
//---------------------------------------------------------------------------
void tTJSNI_VideoOverlay::SetLeft(tjs_int l)
{
    if (Mode == vomLayer)
    {
        if (Layer1 != NULL)
            Layer1->SetLeft(l);
        if (Layer2 != NULL)
            Layer2->SetLeft(l);
    }
    else
    {
        Rect.set_offsets(l, Rect.top);
        SetRectangleToVideoOverlay();
    }
}
//---------------------------------------------------------------------------
void tTJSNI_VideoOverlay::SetTop(tjs_int t)
{
    if (Mode == vomLayer)
    {
        if (Layer1 != NULL)
            Layer1->SetTop(t);
        if (Layer2 != NULL)
            Layer2->SetTop(t);
    }
    else
    {
        Rect.set_offsets(Rect.left, t);
        SetRectangleToVideoOverlay();
    }
}
//---------------------------------------------------------------------------
void tTJSNI_VideoOverlay::SetWidth(tjs_int w)
{
    if (Mode == vomLayer)
        return;

    Rect.right = Rect.left + w;
    SetRectangleToVideoOverlay();
}
//---------------------------------------------------------------------------
void tTJSNI_VideoOverlay::SetHeight(tjs_int h)
{
    if (Mode == vomLayer)
        return;

    Rect.bottom = Rect.top + h;
    SetRectangleToVideoOverlay();
}
//---------------------------------------------------------------------------
void tTJSNI_VideoOverlay::SetVisible(bool b)
{
    Visible = b;
    if (VideoOverlay)
    {
        if (Mode == vomLayer)
        {
            if (Layer1 != NULL)
                Layer1->SetVisible(Visible);
            if (Layer2 != NULL)
                Layer2->SetVisible(Visible);
        }
        else
        {
            VideoOverlay->SetVisible(Visible);
        }
    }
}
//---------------------------------------------------------------------------
void tTJSNI_VideoOverlay::ResetOverlayParams()
{
    // retrieve new window information from owner window and
    // set video owner window / message drain window.
    // also sets rectangle and visible state.
    if (VideoOverlay && Window && (Mode == vomOverlay || Mode == vomMixer || Mode == vomMFEVR))
    {
        //		OwnerWindow = Window->GetWindowHandle();
        VideoOverlay->SetWindow(Window);

        //		VideoOverlay->SetMessageDrainWindow(Window->GetSurfaceWindowHandle());

        // set Rectangle
        SetRectangleToVideoOverlay();

        // set Visible
        VideoOverlay->SetVisible(Visible);
    }
}
//---------------------------------------------------------------------------
void tTJSNI_VideoOverlay::DetachVideoOverlay()
{
    if (VideoOverlay && Window && (Mode == vomOverlay || Mode == vomMixer || Mode == vomMFEVR))
    {
        VideoOverlay->SetWindow(NULL);
        VideoOverlay->SetMessageDrainWindow(EventQueue.GetOwner());
        // once set to util window
    }
}
//---------------------------------------------------------------------------
void tTJSNI_VideoOverlay::SetRectOffset(tjs_int ofsx, tjs_int ofsy)
{
    if (VideoOverlay)
    {
        // 		RECT r = {Rect.left + ofsx, Rect.top + ofsy,
        // 			Rect.right + ofsx, Rect.bottom + ofsy};
        VideoOverlay->SetRect(Rect.left + ofsx, Rect.top + ofsy, Rect.right + ofsx,
                              Rect.bottom + ofsy);
    }
}
//---------------------------------------------------------------------------

void tTJSNI_VideoOverlay::WndProc(NativeEvent& ev)
{
    if (VideoOverlay)
    {
        switch (ev.Message)
        {
            case WM_GRAPHNOTIFY:
            {
                long evcode;
                bool got = false;
                do
                {
                    evcode = ev.WParam;
                    switch (evcode)
                    {
                        case EC_COMPLETE:
                            if (Status == ssPlay)
                            {
                                if (Loop)
                                {
                                    Rewind();
                                    FirePeriodEvent(perLoop); // fire period event by loop
                                                              // rewind
                                }
                                else
                                {
                                    // Graph manager seems not to complete
                                    // playing at this point (rewinding the
                                    // movie at the event handler called
                                    // asynchronously from SetStatusAsync makes
                                    // continuing playing, but the graph seems
                                    // to be unstable). We manually stop the
                                    // manager anyway.
                                    VideoOverlay->Stop();
                                    SetStatusAsync(ssStop); // All data has been rendered
                                }
                            }
                            break;
                        case EC_UPDATE:
                            if (Mode == vomLayer && Status == ssPlay)
                            {
                                int curFrame = (int)ev.LParam;
                                if (Layer1 == NULL && Layer2 == NULL) // nothing to do.
                                    return;

                                // 2フレーム以上差があるときはGetFrame()
                                // を現在のフレームとする
                                int frame = GetFrame();
                                if ((frame + 1) < curFrame || (frame - 1) > curFrame)
                                    curFrame = frame;

                                if ((!IsPrepare) && (SegLoopEndFrame > 0) &&
                                    (frame >= SegLoopEndFrame))
                                {
                                    SetFrame(SegLoopStartFrame > 0 ? SegLoopStartFrame : 0);
                                    FirePeriodEvent(perSegLoop); // fire period event by
                                                                 // segment loop rewind
                                    return;                      // Updateを行わない
                                }

                                // get video image size
                                long width, height;
                                VideoOverlay->GetVideoSize(&width, &height);

                                tTJSNI_BaseLayer* l1 = Layer1;
                                tTJSNI_BaseLayer* l2 = Layer2;

                                // Check layer image size
                                if (l1 != NULL)
                                {
                                    if ((long)l1->GetImageWidth() != width ||
                                        (long)l1->GetImageHeight() != height)
                                        l1->SetImageSize(width, height);
                                    if ((long)l1->GetWidth() != width ||
                                        (long)l1->GetHeight() != height)
                                        l1->SetSize(width, height);
                                }
                                if (l2 != NULL)
                                {
                                    if ((long)l2->GetImageWidth() != width ||
                                        (long)l2->GetImageHeight() != height)
                                        l2->SetImageSize(width, height);
                                    if ((long)l2->GetWidth() != width ||
                                        (long)l2->GetHeight() != height)
                                        l2->SetSize(width, height);
                                }
                                tTVPBaseTexture* buff = VideoOverlay->GetFrontBuffer();
                                if (buff == Bitmap[0])
                                {
                                    if (l1)
                                        l1->AssignMainImage(Bitmap[0]);
                                    if (l2)
                                        l2->AssignMainImage(Bitmap[0]);
                                }
                                else // 0じゃなかったら、1とみなす。
                                {
                                    if (l1)
                                        l1->AssignMainImage(Bitmap[1]);
                                    if (l2)
                                        l2->AssignMainImage(Bitmap[1]);
                                }
                                if (l1)
                                    l1->Update();
                                if (l2)
                                    l2->Update();
                                FireFrameUpdateEvent(curFrame);

                                // ! Prepare mode ?
                                if (!IsPrepare)
                                {
                                    // Send period event ?
                                    if (EventFrame >= 0 && !IsEventPast && curFrame >= EventFrame)
                                    {
                                        EventFrame = -1;
                                        FirePeriodEvent(perPeriod); // fire period event by
                                                                    // setPeriodEvent()
                                    }
                                }
                                else
                                {                                // Prepare mode
                                    FirePeriodEvent(perPrepare); // fire period event by
                                                                 // prepare()
                                    Pause();
                                    Rewind();
                                    IsPrepare = false;
                                }
                            }
                            else if (Mode == vomMixer && Status == ssPlay)
                            {
                                int frame = GetFrame();
                                if ((!IsPrepare) && (SegLoopEndFrame > 0) &&
                                    (frame >= SegLoopEndFrame))
                                {
                                    SetFrame(SegLoopStartFrame > 0 ? SegLoopStartFrame : 0);
                                    FirePeriodEvent(perSegLoop); // fire period event by
                                                                 // segment loop rewind
                                    return;
                                }
                                VideoOverlay->PresentVideoImage();
                                FireFrameUpdateEvent(frame);
                                // Send period event ?
                                if (EventFrame >= 0 && !IsEventPast && frame >= EventFrame)
                                {
                                    EventFrame = -1;
                                    FirePeriodEvent(perPeriod); // fire period event by
                                                                // setPeriodEvent()
                                }
                            }
                            break;
                    }
                } while (got);
                return;
            }
            case WM_CALLBACKCMD:
            {
                FireCallbackCommand((tjs_char*)ev.WParam, (tjs_char*)ev.LParam);
                return;
            }
            case WM_STATE_CHANGE:
            {
                switch (ev.WParam)
                {
                    case vsStopped:
                        SetStatusAsync(ssStop);
                        break;
                    case vsPlaying:
                        SetStatusAsync(ssPlay);
                        break;
                    case vsPaused:
                        SetStatusAsync(ssPause);
                        break;
                    case vsReady:
                        SetStatusAsync(ssReady);
                        break;
                    case vsEnded:
                        if (Status == ssPlay)
                        {
                            if (Loop)
                            {
                                VideoOverlay->Play();
                                FirePeriodEvent(perLoop); // fire period event
                                                          // by loop rewind
                            }
                            else
                            {
                                VideoOverlay->Stop();
                                SetStatusAsync(ssStop); // All data has been rendered
                            }
                        }
                        break;
                }
                return;
            }
        }
    }

    EventQueue.HandlerDefault(ev);
}
//---------------------------------------------------------------------------
void tTJSNI_VideoOverlay::SetTimePosition(tjs_uint64 p)
{
    if (VideoOverlay)
    {
        VideoOverlay->SetPosition(p);
    }
}
tjs_uint64 tTJSNI_VideoOverlay::GetTimePosition()
{
    tjs_uint64 result = 0;
    if (VideoOverlay)
    {
        VideoOverlay->GetPosition(&result);
    }
    return result;
}
void tTJSNI_VideoOverlay::SetFrame(tjs_int f)
{
    if (VideoOverlay)
    {
        VideoOverlay->SetFrame(f);

        if (EventFrame >= f && IsEventPast)
            IsEventPast = false;
    }
}
tjs_int tTJSNI_VideoOverlay::GetFrame()
{
    tjs_int result = 0;
    if (VideoOverlay)
    {
        VideoOverlay->GetFrame(&result);
    }
    return result;
}
void tTJSNI_VideoOverlay::SetStopFrame(tjs_int f)
{
    if (VideoOverlay)
    {
        VideoOverlay->SetStopFrame(f);
    }
}
void tTJSNI_VideoOverlay::SetDefaultStopFrame()
{
    if (VideoOverlay)
    {
        VideoOverlay->SetDefaultStopFrame();
    }
}
tjs_int tTJSNI_VideoOverlay::GetStopFrame()
{
    tjs_int result = 0;
    if (VideoOverlay)
    {
        VideoOverlay->GetStopFrame(&result);
    }
    return result;
}
tjs_real tTJSNI_VideoOverlay::GetFPS()
{
    tjs_real result = 0.0;
    if (VideoOverlay)
    {
        VideoOverlay->GetFPS(&result);
    }
    return result;
}
tjs_int tTJSNI_VideoOverlay::GetNumberOfFrame()
{
    tjs_int result = 0;
    if (VideoOverlay)
    {
        VideoOverlay->GetNumberOfFrame(&result);
    }
    return result;
}
tjs_int64 tTJSNI_VideoOverlay::GetTotalTime()
{
    tjs_int64 result = 0;
    if (VideoOverlay)
    {
        VideoOverlay->GetTotalTime(&result);
    }
    return result;
}
void tTJSNI_VideoOverlay::SetLoop(bool b)
{
    Loop = b;
    if (VideoOverlay)
    {
        if (Loop)
            VideoOverlay->SetLoopSegement(0, -1);
        else
            VideoOverlay->SetLoopSegement(-1, -1);
    }
}
void tTJSNI_VideoOverlay::SetLayer1(tTJSNI_BaseLayer* l)
{
    Layer1 = l;
}
void tTJSNI_VideoOverlay::SetLayer2(tTJSNI_BaseLayer* l)
{
    Layer2 = l;
}
void tTJSNI_VideoOverlay::SetMode(tTVPVideoOverlayMode m)
{
    // ビデオオープン後のモード変更は禁止
    if (!VideoOverlay)
    {
        Mode = m;
    }
}

tjs_real tTJSNI_VideoOverlay::GetPlayRate()
{
    tjs_real result = 0.0;
    if (VideoOverlay)
    {
        VideoOverlay->GetPlayRate(&result);
    }
    return result;
}
void tTJSNI_VideoOverlay::SetPlayRate(tjs_real r)
{
    if (VideoOverlay)
    {
        VideoOverlay->SetPlayRate(r);
    }
}

tjs_int tTJSNI_VideoOverlay::GetAudioBalance()
{
    long result = 0;
    if (VideoOverlay)
    {
        VideoOverlay->GetAudioBalance(&result);
    }
    return /*TVPDSAttenuateToPan*/ (result);
}
void tTJSNI_VideoOverlay::SetAudioBalance(tjs_int b)
{
    if (VideoOverlay)
    {
        VideoOverlay->SetAudioBalance(/*TVPPanToDSAttenuate*/ (b));
    }
}
tjs_int tTJSNI_VideoOverlay::GetAudioVolume()
{
    long result = 0;
    if (VideoOverlay)
    {
        VideoOverlay->GetAudioVolume(&result);
    }
    return /*TVPDSAttenuateToVolume*/ (result);
}
void tTJSNI_VideoOverlay::SetAudioVolume(tjs_int b)
{
    if (VideoOverlay)
    {
        VideoOverlay->SetAudioVolume(/*TVPVolumeToDSAttenuate*/ (b));
    }
}
tjs_uint tTJSNI_VideoOverlay::GetNumberOfAudioStream()
{
    unsigned long result = 0;
    if (VideoOverlay)
    {
        VideoOverlay->GetNumberOfAudioStream(&result);
    }
    return result;
}
void tTJSNI_VideoOverlay::SelectAudioStream(tjs_uint n)
{
    if (VideoOverlay)
    {
        VideoOverlay->SelectAudioStream(n);
    }
}
tjs_int tTJSNI_VideoOverlay::GetEnabledAudioStream()
{
    long result = -1;
    if (VideoOverlay)
    {
        VideoOverlay->GetEnableAudioStreamNum(&result);
    }
    return result;
}
void tTJSNI_VideoOverlay::DisableAudioStream()
{
    if (VideoOverlay)
    {
        VideoOverlay->DisableAudioStream();
    }
}

tjs_uint tTJSNI_VideoOverlay::GetNumberOfVideoStream()
{
    unsigned long result = 0;
    if (VideoOverlay)
    {
        VideoOverlay->GetNumberOfVideoStream(&result);
    }
    return result;
}
void tTJSNI_VideoOverlay::SelectVideoStream(tjs_uint n)
{
    if (VideoOverlay)
    {
        VideoOverlay->SelectVideoStream(n);
    }
}
tjs_int tTJSNI_VideoOverlay::GetEnabledVideoStream()
{
    long result = -1;
    if (VideoOverlay)
    {
        VideoOverlay->GetEnableVideoStreamNum(&result);
    }
    return result;
}
void tTJSNI_VideoOverlay::SetMixingLayer(tTJSNI_BaseLayer* l)
{
    if (VideoOverlay)
    {
        if (l)
        {
            if (l->GetVisible())
            {
                float alpha = static_cast<float>(l->GetOpacity()) / 255.0f;
                VideoOverlay->SetMixingBitmap(l->GetMainImage(), alpha);
            }
            else
            {
                VideoOverlay->ResetMixingBitmap();
            }
        }
        else
        {
            VideoOverlay->ResetMixingBitmap();
        }
    }
}
void tTJSNI_VideoOverlay::ResetMixingBitmap()
{
    if (VideoOverlay)
    {
        VideoOverlay->ResetMixingBitmap();
    }
}
void tTJSNI_VideoOverlay::SetMixingMovieAlpha(tjs_real a)
{
    if (VideoOverlay)
    {
        VideoOverlay->SetMixingMovieAlpha(static_cast<float>(a));
    }
}
tjs_real tTJSNI_VideoOverlay::GetMixingMovieAlpha()
{
    float ret = 0.0f;
    if (VideoOverlay)
    {
        VideoOverlay->GetMixingMovieAlpha(&ret);
    }
    return static_cast<tjs_real>(ret);
}
void tTJSNI_VideoOverlay::SetMixingMovieBGColor(tjs_uint col)
{
    if (VideoOverlay)
    {
        VideoOverlay->SetMixingMovieBGColor(col);
    }
}
tjs_uint tTJSNI_VideoOverlay::GetMixingMovieBGColor()
{
    unsigned long ret;
    if (VideoOverlay)
    {
        VideoOverlay->GetMixingMovieBGColor(&ret);
    }
    return static_cast<tjs_uint>(ret);
}

tjs_real tTJSNI_VideoOverlay::GetContrastRangeMin()
{
    float ret = -1.0f;
    if (VideoOverlay)
    {
        VideoOverlay->GetContrastRangeMin(&ret);
    }
    return static_cast<tjs_real>(ret);
}
tjs_real tTJSNI_VideoOverlay::GetContrastRangeMax()
{
    float ret = -1.0f;
    if (VideoOverlay)
    {
        VideoOverlay->GetContrastRangeMax(&ret);
    }
    return static_cast<tjs_real>(ret);
}
tjs_real tTJSNI_VideoOverlay::GetContrastDefaultValue()
{
    float ret = -1.0f;
    if (VideoOverlay)
    {
        VideoOverlay->GetContrastDefaultValue(&ret);
    }
    return static_cast<tjs_real>(ret);
}
tjs_real tTJSNI_VideoOverlay::GetContrastStepSize()
{
    float ret = -1.0f;
    if (VideoOverlay)
    {
        VideoOverlay->GetContrastStepSize(&ret);
    }
    return static_cast<tjs_real>(ret);
}
tjs_real tTJSNI_VideoOverlay::GetContrast()
{
    float ret = -1.0f;
    if (VideoOverlay)
    {
        VideoOverlay->GetContrast(&ret);
    }
    return static_cast<tjs_real>(ret);
}
void tTJSNI_VideoOverlay::SetContrast(tjs_real v)
{
    if (VideoOverlay)
    {
        VideoOverlay->SetContrast(static_cast<float>(v));
    }
}
tjs_real tTJSNI_VideoOverlay::GetBrightnessRangeMin()
{
    float ret = -1.0f;
    if (VideoOverlay)
    {
        VideoOverlay->GetBrightnessRangeMin(&ret);
    }
    return static_cast<tjs_real>(ret);
}
tjs_real tTJSNI_VideoOverlay::GetBrightnessRangeMax()
{
    float ret = -1.0f;
    if (VideoOverlay)
    {
        VideoOverlay->GetBrightnessRangeMax(&ret);
    }
    return static_cast<tjs_real>(ret);
}
tjs_real tTJSNI_VideoOverlay::GetBrightnessDefaultValue()
{
    float ret = -1.0f;
    if (VideoOverlay)
    {
        VideoOverlay->GetBrightnessDefaultValue(&ret);
    }
    return static_cast<tjs_real>(ret);
}
tjs_real tTJSNI_VideoOverlay::GetBrightnessStepSize()
{
    float ret = -1.0f;
    if (VideoOverlay)
    {
        VideoOverlay->GetBrightnessStepSize(&ret);
    }
    return static_cast<tjs_real>(ret);
}
tjs_real tTJSNI_VideoOverlay::GetBrightness()
{
    float ret = -1.0f;
    if (VideoOverlay)
    {
        VideoOverlay->GetBrightness(&ret);
    }
    return static_cast<tjs_real>(ret);
}
void tTJSNI_VideoOverlay::SetBrightness(tjs_real v)
{
    if (VideoOverlay)
    {
        VideoOverlay->SetBrightness(static_cast<float>(v));
    }
}

tjs_real tTJSNI_VideoOverlay::GetHueRangeMin()
{
    float ret = -1.0f;
    if (VideoOverlay)
    {
        VideoOverlay->GetHueRangeMin(&ret);
    }
    return static_cast<tjs_real>(ret);
}
tjs_real tTJSNI_VideoOverlay::GetHueRangeMax()
{
    float ret = -1.0f;
    if (VideoOverlay)
    {
        VideoOverlay->GetHueRangeMax(&ret);
    }
    return static_cast<tjs_real>(ret);
}
tjs_real tTJSNI_VideoOverlay::GetHueDefaultValue()
{
    float ret = -1.0f;
    if (VideoOverlay)
    {
        VideoOverlay->GetHueDefaultValue(&ret);
    }
    return static_cast<tjs_real>(ret);
}
tjs_real tTJSNI_VideoOverlay::GetHueStepSize()
{
    float ret = -1.0f;
    if (VideoOverlay)
    {
        VideoOverlay->GetHueStepSize(&ret);
    }
    return static_cast<tjs_real>(ret);
}
tjs_real tTJSNI_VideoOverlay::GetHue()
{
    float ret = -1.0f;
    if (VideoOverlay)
    {
        VideoOverlay->GetHue(&ret);
    }
    return static_cast<tjs_real>(ret);
}
void tTJSNI_VideoOverlay::SetHue(tjs_real v)
{
    if (VideoOverlay)
    {
        VideoOverlay->SetHue(static_cast<float>(v));
    }
}

tjs_real tTJSNI_VideoOverlay::GetSaturationRangeMin()
{
    float ret = -1.0f;
    if (VideoOverlay)
    {
        VideoOverlay->GetSaturationRangeMin(&ret);
    }
    return static_cast<tjs_real>(ret);
}
tjs_real tTJSNI_VideoOverlay::GetSaturationRangeMax()
{
    float ret = -1.0f;
    if (VideoOverlay)
    {
        VideoOverlay->GetSaturationRangeMax(&ret);
    }
    return static_cast<tjs_real>(ret);
}
tjs_real tTJSNI_VideoOverlay::GetSaturationDefaultValue()
{
    float ret = -1.0f;
    if (VideoOverlay)
    {
        VideoOverlay->GetSaturationDefaultValue(&ret);
    }
    return static_cast<tjs_real>(ret);
}
tjs_real tTJSNI_VideoOverlay::GetSaturationStepSize()
{
    float ret = -1.0f;
    if (VideoOverlay)
    {
        VideoOverlay->GetSaturationStepSize(&ret);
    }
    return static_cast<tjs_real>(ret);
}
tjs_real tTJSNI_VideoOverlay::GetSaturation()
{
    float ret = -1.0f;
    if (VideoOverlay)
    {
        VideoOverlay->GetSaturation(&ret);
    }
    return static_cast<tjs_real>(ret);
}
void tTJSNI_VideoOverlay::SetSaturation(tjs_real v)
{
    if (VideoOverlay)
    {
        VideoOverlay->SetSaturation(static_cast<float>(v));
    }
}
//---------------------------------------------------------------------------
tjs_int tTJSNI_VideoOverlay::GetOriginalWidth()
{
    // retrieve original (coded in the video stream) width size
    if (!VideoOverlay)
        return 0;

    long width, height;
    VideoOverlay->GetVideoSize(&width, &height);

    return (tjs_int)width;
}
//---------------------------------------------------------------------------
tjs_int tTJSNI_VideoOverlay::GetOriginalHeight()
{
    // retrieve original (coded in the video stream) height size

    long width, height;
    VideoOverlay->GetVideoSize(&width, &height);

    return (tjs_int)height;
}
//---------------------------------------------------------------------------
void tTJSNI_VideoOverlay::ClearWndProcMessages()
{
    // clear WndProc's message queue
    EventQueue.Clear(WM_GRAPHNOTIFY);
    EventQueue.Clear(WM_CALLBACKCMD);
}

bool tTJSNI_VideoOverlay::GetVideoSize(tjs_int& w, tjs_int& h) const
{
    if (VideoOverlay)
    {
        long _w, _h;
        VideoOverlay->GetVideoSize(&_w, &_h);
        w = _w;
        h = _h;
        return true;
    }
    return false;
}

//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTJSNC_VideoOverlay : TJS VideoOverlay class
//---------------------------------------------------------------------------
tjs_uint32 tTJSNC_VideoOverlay::ClassID = -1;
tTJSNC_VideoOverlay::tTJSNC_VideoOverlay()
  : tTJSNativeClass(TJS_N("VideoOverlay")){
        // registration of native members

        TJS_BEGIN_NATIVE_MEMBERS(VideoOverlay) // constructor
        TJS_DECL_EMPTY_FINALIZE_METHOD
            //----------------------------------------------------------------------
            TJS_BEGIN_NATIVE_CONSTRUCTOR_DECL(/*var.name*/ _this,
                                              /*var.type*/ tTJSNI_VideoOverlay,
                                              /*TJS class name*/ VideoOverlay){return TJS_S_OK;
}
TJS_END_NATIVE_CONSTRUCTOR_DECL(/*TJS class name*/ VideoOverlay)
//----------------------------------------------------------------------

//-- methods

//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ open)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_VideoOverlay);

    if (numparams < 1)
        return TJS_E_BADPARAMCOUNT;
    _this->Open(*param[0]);

    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ open)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ play)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_VideoOverlay);

    _this->Play();

    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ play)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ stop)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_VideoOverlay);

    _this->Stop();

    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ stop)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ close)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_VideoOverlay);

    _this->Close();

    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ close)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ setPos) // not SetPosition
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_VideoOverlay);

    if (numparams < 2)
        return TJS_E_BADPARAMCOUNT;
    _this->SetPosition(*param[0], *param[1]);

    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ setPos)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ setSize)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_VideoOverlay);

    if (numparams < 2)
        return TJS_E_BADPARAMCOUNT;
    _this->SetSize(*param[0], *param[1]);

    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ setSize)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ setBounds)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_VideoOverlay);

    if (numparams < 4)
        return TJS_E_BADPARAMCOUNT;
    tTVPRect r;
    r.left = *param[0];
    r.top = *param[1];
    r.right = r.left + (tjs_int)*param[2];
    r.bottom = r.top + (tjs_int)*param[3];
    _this->SetBounds(r);

    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ setBounds)
//----------------------------------------------------------------------
// Start: Add:	2004/08/23	T.Imoto
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ pause)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_VideoOverlay);

    _this->Pause();

    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ pause)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ rewind)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_VideoOverlay);

    _this->Rewind();

    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ rewind)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ prepare)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_VideoOverlay);

    _this->Prepare();

    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ prepare)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ setSegmentLoop)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_VideoOverlay);

    if (numparams < 2)
        return TJS_E_BADPARAMCOUNT;
    _this->SetSegmentLoop(*param[0], *param[1]);

    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ setSegmentLoop)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ cancelSegmentLoop)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_VideoOverlay);

    _this->CancelSegmentLoop();

    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ cancelSegmentLoop)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ setPeriodEvent)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_VideoOverlay);

    if (numparams < 1)
        _this->SetPeriodEvent(-1);
    else if (numparams < 2)
        _this->SetPeriodEvent(*param[0]);

    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ setPeriodEvent)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ cancelPeriodEvent)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_VideoOverlay);

    _this->SetPeriodEvent(-1);

    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ cancelPeriodEvent)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ selectAudioStream)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_VideoOverlay);

    if (numparams < 1)
        return TJS_E_BADPARAMCOUNT;

    _this->SelectAudioStream((tjs_uint)(tjs_int)*param[0]);

    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ selectAudioStream)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ setMixingLayer)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_VideoOverlay);

    if (numparams < 1)
        return TJS_E_BADPARAMCOUNT;

    tTJSNI_BaseLayer* src = NULL;
    tTJSVariantClosure clo = param[0]->AsObjectClosureNoAddRef();
    if (clo.Object)
    {
        if (TJS_FAILED(clo.Object->NativeInstanceSupport(TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
                                                         (iTJSNativeInstance**)&src)))
            TVPThrowExceptionMessage(TVPSpecifyLayer);
        if (!src)
            TVPThrowExceptionMessage(TVPSpecifyLayer);
    }
    _this->SetMixingLayer(src);

    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ setMixingLayer)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ resetMixingLayer)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_VideoOverlay);

    _this->ResetMixingBitmap();

    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ resetMixingLayer)
//----------------------------------------------------------------------
// End: Add:	2004/08/23	T.Imoto
//----------------------------------------------------------------------

//-- events

//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ onStatusChanged)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_VideoOverlay);

    tTJSVariantClosure obj = _this->GetActionOwnerNoAddRef();
    if (obj.Object)
    {
        TVP_ACTION_INVOKE_BEGIN(1, "onStatusChanged", objthis);
        TVP_ACTION_INVOKE_MEMBER("status");
        TVP_ACTION_INVOKE_END(obj);
    }

    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ onStatusChanged)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ onCallbackCommand)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_VideoOverlay);

    tTJSVariantClosure obj = _this->GetActionOwnerNoAddRef();
    if (obj.Object)
    {
        TVP_ACTION_INVOKE_BEGIN(2, "onCallbackCommand", objthis);
        TVP_ACTION_INVOKE_MEMBER("command");
        TVP_ACTION_INVOKE_MEMBER("arg");
        TVP_ACTION_INVOKE_END(obj);
    }

    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ onCallbackCommand)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ onPeriod)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_VideoOverlay);

    tTJSVariantClosure obj = _this->GetActionOwnerNoAddRef();
    if (obj.Object)
    {
        TVP_ACTION_INVOKE_BEGIN(1, "onPeriod", objthis);
        TVP_ACTION_INVOKE_MEMBER("reason");
        TVP_ACTION_INVOKE_END(obj);
    }

    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ onPeriod)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ onFrameUpdate)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_VideoOverlay);

    tTJSVariantClosure obj = _this->GetActionOwnerNoAddRef();
    if (obj.Object)
    {
        TVP_ACTION_INVOKE_BEGIN(1, "onFrameUpdate", objthis);
        TVP_ACTION_INVOKE_MEMBER("frame");
        TVP_ACTION_INVOKE_END(obj);
    }

    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ onFrameUpdate)
//----------------------------------------------------------------------

//-- properties

//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(position){
    // Start: Add:	T.Imoto
    TJS_BEGIN_NATIVE_PROP_GETTER{
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_VideoOverlay);

*result = (tjs_int64)_this->GetTimePosition();

return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_VideoOverlay);

    _this->SetTimePosition((tjs_int64)*param);

    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
// End: Add:	T.Imoto
}
TJS_END_NATIVE_PROP_DECL(position)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(left){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_VideoOverlay);

*result = _this->GetLeft();

return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_VideoOverlay);

    _this->SetLeft(*param);

    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(left)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(top){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_VideoOverlay);

*result = _this->GetTop();

return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_VideoOverlay);

    _this->SetTop(*param);

    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(top)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(width){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_VideoOverlay);

*result = _this->GetWidth();

return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_VideoOverlay);

    _this->SetWidth(*param);

    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(width)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(height){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_VideoOverlay);

*result = _this->GetHeight();

return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_VideoOverlay);

    _this->SetHeight(*param);

    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(height)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(originalWidth){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_VideoOverlay);

*result = _this->GetOriginalWidth();

return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(originalWidth)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(originalHeight){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_VideoOverlay);

*result = _this->GetOriginalHeight();

return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(originalHeight)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(visible){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_VideoOverlay);

*result = _this->GetVisible();

return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_VideoOverlay);

    _this->SetVisible(param->operator bool());

    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(visible)
//----------------------------------------------------------------------
// Start: Add:		T.Imoto
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(loop){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_VideoOverlay);

*result = _this->GetLoop();

return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_VideoOverlay);

    _this->SetLoop(param->operator bool());

    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(loop)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(frame){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_VideoOverlay);

*result = _this->GetFrame();

return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_VideoOverlay);

    _this->SetFrame(*param);

    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(frame)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(fps){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_VideoOverlay);

*result = _this->GetFPS();

return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(fps)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(numberOfFrame){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_VideoOverlay);

*result = _this->GetNumberOfFrame();

return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(numberOfFrame)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(totalTime){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_VideoOverlay);

*result = _this->GetTotalTime();

return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(totalTime)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(layer1){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_VideoOverlay);

tTJSNI_BaseLayer* layer1 = _this->GetLayer1();
if (layer1)
{
    iTJSDispatch2* dsp = layer1->GetOwnerNoAddRef();
    *result = tTJSVariant(dsp, dsp);
}
else
{
    *result = tTJSVariant((iTJSDispatch2*)NULL);
}

return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_VideoOverlay);

    tTJSNI_BaseLayer* src = NULL;
    tTJSVariantClosure clo = param->AsObjectClosureNoAddRef();
    if (clo.Object)
    {
        if (TJS_FAILED(clo.Object->NativeInstanceSupport(TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
                                                         (iTJSNativeInstance**)&src)))
            TVPThrowExceptionMessage(TVPSpecifyLayer);

        if (!src)
            TVPThrowExceptionMessage(TVPSpecifyLayer);
    }

    _this->SetLayer1(src);

    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(layer1)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(layer2){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_VideoOverlay);

tTJSNI_BaseLayer* layer2 = _this->GetLayer2();
if (layer2)
{
    iTJSDispatch2* dsp = layer2->GetOwnerNoAddRef();
    *result = tTJSVariant(dsp, dsp);
}
else
{
    *result = tTJSVariant((iTJSDispatch2*)NULL);
}

return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_VideoOverlay);

    tTJSNI_BaseLayer* src = NULL;
    tTJSVariantClosure clo = param->AsObjectClosureNoAddRef();
    if (clo.Object)
    {
        if (TJS_FAILED(clo.Object->NativeInstanceSupport(TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
                                                         (iTJSNativeInstance**)&src)))
            TVPThrowExceptionMessage(TVPSpecifyLayer);

        if (!src)
            TVPThrowExceptionMessage(TVPSpecifyLayer);
    }

    _this->SetLayer2(src);

    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(layer2)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(mode){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_VideoOverlay);
*result = (tjs_int)_this->GetMode();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_VideoOverlay);

    _this->SetMode((tTVPVideoOverlayMode)(tjs_int)*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(mode)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(playRate){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_VideoOverlay);
*result = (tjs_real)_this->GetPlayRate();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_VideoOverlay);

    _this->SetPlayRate((tjs_real)*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(playRate)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(segmentLoopStartFrame){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_VideoOverlay);
*result = (tjs_int)_this->GetSegmentLoopStartFrame();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(segmentLoopStartFrame)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(segmentLoopEndFrame){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_VideoOverlay);
*result = (tjs_int)_this->GetSegmentLoopEndFrame();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(segmentLoopEndFrame)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(periodEventFrame){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_VideoOverlay);
*result = (tjs_int)_this->GetPeriodEventFrame();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_VideoOverlay);
    _this->SetPeriodEvent((tjs_int)*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(periodEventFrame)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(audioBalance){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_VideoOverlay);
*result = (tjs_int)_this->GetAudioBalance();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_VideoOverlay);
    _this->SetAudioBalance((tjs_int)*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(audioBalance)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(audioVolume){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_VideoOverlay);
*result = (tjs_int)_this->GetAudioVolume();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_VideoOverlay);
    _this->SetAudioVolume((tjs_int)*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(audioVolume)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(numberOfAudioStream){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_VideoOverlay);
*result = (tjs_int)_this->GetNumberOfAudioStream();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(numberOfAudioStream)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(enabledAudioStream){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_VideoOverlay);
*result = (tjs_int)_this->GetEnabledAudioStream();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_VideoOverlay);
    _this->SelectAudioStream((tjs_uint)(tjs_int)*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(enabledAudioStream)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(numberOfVideoStream){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_VideoOverlay);
*result = (tjs_int)_this->GetNumberOfVideoStream();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(numberOfVideoStream)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(enabledVideoStream){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_VideoOverlay);
*result = (tjs_int)_this->GetEnabledVideoStream();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_VideoOverlay);
    _this->SelectVideoStream((tjs_uint)(tjs_int)*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(enabledVideoStream)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(mixingMovieAlpha){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_VideoOverlay);
*result = (tjs_real)_this->GetMixingMovieAlpha();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_VideoOverlay);
    _this->SetMixingMovieAlpha((tjs_real)*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(mixingMovieAlpha)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(mixingMovieBGColor){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_VideoOverlay);
*result = (tjs_int)_this->GetMixingMovieBGColor();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_VideoOverlay);
    _this->SetMixingMovieBGColor((tjs_uint)(tjs_int)*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(mixingMovieBGColor)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(contrastRangeMin){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_VideoOverlay);
*result = (tjs_real)_this->GetContrastRangeMin();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(contrastRangeMin)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(contrastRangeMax){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_VideoOverlay);
*result = (tjs_real)_this->GetContrastRangeMax();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(contrastRangeMax)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(contrastDefaultValue){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_VideoOverlay);
*result = (tjs_real)_this->GetContrastDefaultValue();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(contrastDefaultValue)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(contrastStepSize){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_VideoOverlay);
*result = (tjs_real)_this->GetContrastStepSize();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(contrastStepSize)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(contrast){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_VideoOverlay);
*result = (tjs_real)_this->GetContrast();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_VideoOverlay);
    _this->SetContrast((tjs_real)*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(contrast)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(brightnessRangeMin){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_VideoOverlay);
*result = (tjs_real)_this->GetBrightnessRangeMin();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(brightnessRangeMin)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(brightnessRangeMax){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_VideoOverlay);
*result = (tjs_real)_this->GetBrightnessRangeMax();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(brightnessRangeMax)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(brightnessDefaultValue){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_VideoOverlay);
*result = (tjs_real)_this->GetBrightnessDefaultValue();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(brightnessDefaultValue)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(brightnessStepSize){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_VideoOverlay);
*result = (tjs_real)_this->GetBrightnessStepSize();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(brightnessStepSize)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(brightness){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_VideoOverlay);
*result = (tjs_real)_this->GetBrightness();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_VideoOverlay);
    _this->SetBrightness((tjs_real)*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(brightness)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(hueRangeMin){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_VideoOverlay);
*result = (tjs_real)_this->GetHueRangeMin();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(hueRangeMin)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(hueRangeMax){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_VideoOverlay);
*result = (tjs_real)_this->GetHueRangeMax();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(hueRangeMax)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(hueDefaultValue){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_VideoOverlay);
*result = (tjs_real)_this->GetHueDefaultValue();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(hueDefaultValue)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(hueStepSize){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_VideoOverlay);
*result = (tjs_real)_this->GetHueStepSize();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(hueStepSize)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(hue){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_VideoOverlay);
*result = (tjs_real)_this->GetHue();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_VideoOverlay);
    _this->SetHue((tjs_real)*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(hue)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(saturationRangeMin){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_VideoOverlay);
*result = (tjs_real)_this->GetSaturationRangeMin();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(saturationRangeMin)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(saturationRangeMax){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_VideoOverlay);
*result = (tjs_real)_this->GetSaturationRangeMax();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(saturationRangeMax)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(saturationDefaultValue){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_VideoOverlay);
*result = (tjs_real)_this->GetSaturationDefaultValue();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(saturationDefaultValue)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(saturationStepSize){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_VideoOverlay);
*result = (tjs_real)_this->GetSaturationStepSize();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(saturationStepSize)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(saturation){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_VideoOverlay);
*result = (tjs_real)_this->GetSaturation();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_VideoOverlay);
    _this->SetSaturation((tjs_real)*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(saturation)
//----------------------------------------------------------------------
// End: Add:	T.Imoto
//----------------------------------------------------------------------

TJS_END_NATIVE_MEMBERS

//----------------------------------------------------------------------
}

//---------------------------------------------------------------------------
// tTJSNC_VideoOverlay::CreateNativeInstance : returns proper instance object
//---------------------------------------------------------------------------
tTJSNativeInstance* tTJSNC_VideoOverlay::CreateNativeInstance()
{
    return new tTJSNI_VideoOverlay();
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPCreateNativeClass_VideoOverlay
//---------------------------------------------------------------------------
tTJSNativeClass* TVPCreateNativeClass_VideoOverlay()
{
    return new tTJSNC_VideoOverlay();
}
//---------------------------------------------------------------------------