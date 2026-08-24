#include "tjsCommHead.h"
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
#include "SystemControl.h"
#include "TVPEvent.h"
#include "TVPMsg.h"
#include "TVPSystem.h"
#include "TVPScript.h"
#include "TVPWindow.h"
#include "WindowManager.h"
#include "TVPStorage.h"
#include "TVPDebug.h"
#include "TVPApplication.h"
#include "Platform.h"
#include "Random.h"
#include "TVPSettings.h"

tTVPSystemControl* TVPSystemControl;
bool TVPSystemControlAlive = false;

//---------------------------------------------------------------------------
// Get whether to control main thread priority or to insert wait
//---------------------------------------------------------------------------
static bool TVPMainThreadPriorityControlInit = false;
static bool TVPMainThreadPriorityControl = false;
static bool TVPGetMainThreadPriorityControl()
{
    if (TVPMainThreadPriorityControlInit)
        return TVPMainThreadPriorityControl;
    tTJSVariant val;
    if (TVPGetCommandLine(TJS_N("-lowpri"), &val))
    {
        ttstr str(val);
        if (str == TJS_N("yes"))
            TVPMainThreadPriorityControl = true;
    }

    TVPMainThreadPriorityControlInit = true;

    return TVPMainThreadPriorityControl;
}

tTVPSystemControl::tTVPSystemControl() : EventEnable(true)
{
    ContinuousEventCalling = false;
    AutoShowConsoleOnError = false;

    LastCompactedTick = 0;
    LastCloseClickedTick = 0;
    LastShowModalWindowSentTick = 0;
    LastRehashedTick = 0;

    MixedIdleTick = 0;

    TVPSystemControlAlive = true;
}
void tTVPSystemControl::InvokeEvents()
{
    CallDeliverAllEventsOnIdle();
}
void tTVPSystemControl::CallDeliverAllEventsOnIdle()
{
}

void tTVPSystemControl::BeginContinuousEvent()
{
    if (!ContinuousEventCalling)
    {
        ContinuousEventCalling = true;
        InvokeEvents();
        if (TVPGetMainThreadPriorityControl())
        {
            // make main thread priority lower
            //			SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
        }
    }
}
void tTVPSystemControl::EndContinuousEvent()
{
    if (ContinuousEventCalling)
    {
        ContinuousEventCalling = false;
        if (TVPGetMainThreadPriorityControl())
        {
            // make main thread priority normal
            //			SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
        }
    }
}
//---------------------------------------------------------------------------
void tTVPSystemControl::NotifyCloseClicked()
{
    // close Button is clicked
    LastCloseClickedTick = TVPGetRoughTickCount();
}

void tTVPSystemControl::NotifyEventDelivered()
{
    // called from event system, notifying the event is delivered.
    LastCloseClickedTick = 0;
    // if(TVPHaltWarnForm) delete TVPHaltWarnForm, TVPHaltWarnForm = NULL;
}

bool tTVPSystemControl::ApplicationIdle()
{
    DeliverEvents();
    bool cont = !ContinuousEventCalling;
    MixedIdleTick += TVPGetRoughTickCount();
    return cont;
}

void tTVPSystemControl::DeliverEvents()
{
    if (ContinuousEventCalling)
        TVPProcessContinuousHandlerEventFlag = true; // set flag

    if (EventEnable)
    {
        TVPDeliverAllEvents();
    }
}

void tTVPSystemControl::SystemWatchTimerTimer()
{
    if (TVPTerminated)
    {
        Application->Terminate();
    }

    // call events
    uint64_t tick = TVPGetRoughTickCount();
    // push environ noise
    TVPPushEnvironNoise(&tick, sizeof(tick));
    TVPPushEnvironNoise(&LastCompactedTick, sizeof(LastCompactedTick));
    TVPPushEnvironNoise(&LastShowModalWindowSentTick, sizeof(LastShowModalWindowSentTick));
    TVPPushEnvironNoise(&MixedIdleTick, sizeof(MixedIdleTick));

    // check status and deliver events
    DeliverEvents();

    // call TickBeat
    tjs_int count = TVPGetWindowCount();
    for (tjs_int i = 0; i < count; i++)
    {
        TVPWindow* win = TVPGetWindowListAt(i);
        win->TickBeat();
    }

    if (!ContinuousEventCalling && tick - LastCompactedTick > 4000)
    {
        // idle state over 4 sec.
        LastCompactedTick = tick;

        // fire compact event
        TVPDeliverCompactEvent(TVP_COMPACT_LEVEL_IDLE);
    }
    if (!ContinuousEventCalling && tick > LastRehashedTick + 1500)
    {
        // TJS2 object rehash
        LastRehashedTick = tick;
        TJSDoRehash();
    }
    // ensure modal window visible
    if (tick > LastShowModalWindowSentTick + 4100)
    {
        //	::PostMessage(Handle, WM_USER+0x32, 0, 0);
        // This is currently disabled because IME composition window
        // hides behind the window which is bringed top by the
        // window-rearrangement.
        LastShowModalWindowSentTick = tick;
    }
}
