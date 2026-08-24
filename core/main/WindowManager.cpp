//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// WindowManager : 管理所有窗口，并分发平台层传来的事件
//---------------------------------------------------------------------------

#include "tjsCommHead.h"
#include "WindowManager.h"
#include "TVPWindow.h"
#include "TVPSystem.h"
#include "TVPApplication.h"
#include "Random.h"

//---------------------------------------------------------------------------
// Window List
//---------------------------------------------------------------------------
TVPWindow* TVPMainWindow = NULL; // main window
static std::vector<TVPWindow*> TVPWindowVector;
static TVPWindow* CurrentWindowLayer = NULL; // 当前激活（最前）窗口

static tTVPMouseButton _mouseBtn;

static tjs_uint8 _scancode[0x200];

//---------------------------------------------------------------------------
bool TVPGetKeyMouseAsyncState(tjs_uint keycode, bool getcurrent)
{
    if (keycode >= sizeof(_scancode) / sizeof(_scancode[0]))
        return false;
    tjs_uint8 code = _scancode[keycode];
    _scancode[keycode] &= 1;
    return code & (getcurrent ? 1 : 0x10);
}
//---------------------------------------------------------------------------
bool TVPGetJoyPadAsyncState(tjs_uint keycode, bool getcurrent)
{
    if (keycode >= sizeof(_scancode) / sizeof(_scancode[0]))
        return false;
    tjs_uint8 code = _scancode[keycode];
    _scancode[keycode] &= 1;
    return code & (getcurrent ? 1 : 0x10);
}
//---------------------------------------------------------------------------
static int TVPConvertMouseBtnToVKCode(tTVPMouseButton mouseBtn)
{
    int btncode;
    switch (mouseBtn)
    {
        case mbLeft:
            btncode = VK_LBUTTON;
            break;
        case mbMiddle:
            btncode = VK_MBUTTON;
            break;
        case mbRight:
            btncode = VK_RBUTTON;
            break;
        default:
            btncode = 0;
            break;
    }
    return btncode;
}
//---------------------------------------------------------------------------
static tjs_uint32 TVPGetCurrentShiftKeyState()
{
    tjs_uint32 f = 0;

    if (_scancode[VK_SHIFT] & 1)
        f |= ssShift;
    if (_scancode[VK_MENU] & 1)
        f |= ssAlt;
    if (_scancode[VK_CONTROL] & 1)
        f |= ssCtrl;
    if (_scancode[VK_LBUTTON] & 1)
        f |= ssLeft;
    if (_scancode[VK_RBUTTON] & 1)
        f |= ssRight;
    return f;
}
//---------------------------------------------------------------------------
void TVPRegisterWindowToList(TVPWindow* window)
{
    if (TVPMainWindow == NULL && TVPWindowVector.size() == 0)
    {
        // first time the window is registered
        TVPMainWindow = window; // set as main window
        TJSAddStaticToRegisterHeap(
            [](void*)
            {
                TVPMainWindow = NULL;
                TVPWindowVector.clear();
                CurrentWindowLayer = NULL;
            },
            NULL);
    }
    TVPWindowVector.push_back(window);

    // notify that the layer must lost capture state
    std::vector<TVPWindow*>::iterator i;
    for (i = TVPWindowVector.begin(); i != TVPWindowVector.end(); i++)
    {
        (*i)->PostReleaseCaptureEvent();
    }
}
//---------------------------------------------------------------------------
void TVPUnregisterWindowToList(TVPWindow* window)
{
    std::vector<TVPWindow*>::iterator i;
    i = std::find(TVPWindowVector.begin(), TVPWindowVector.end(), window);
    if (i != TVPWindowVector.end())
    {
        bool flag = false;
        if (*i == TVPMainWindow)
            flag = true;

        TVPWindowVector.erase(i);

        if (flag)
        {
            TVPMainWindowClosed(); // MainWindow had been closed
            TVPMainWindow = NULL;
        }
    }

    if (CurrentWindowLayer == window)
    {
        // window 已从列表移除；从后向前选择下一个可见窗口作为当前窗口
        TVPWindow* anotherWin = NULL;
        for (size_t n = TVPWindowVector.size(); n > 0; n--)
        {
            TVPWindow* w = TVPWindowVector[n - 1];
            if (w->GetVisible())
            {
                anotherWin = w;
                break;
            }
        }
        if (anotherWin)
        {
            anotherWin->SetPosition(0, 0);
        }
        CurrentWindowLayer = anotherWin;
    }
}
//---------------------------------------------------------------------------
TVPWindow* TVPGetWindowListAt(tjs_int idx)
{
    return TVPWindowVector[idx];
}
//---------------------------------------------------------------------------
tjs_int TVPGetWindowCount()
{
    return (tjs_int)TVPWindowVector.size();
}
//---------------------------------------------------------------------------
void TVPClearAllWindowInputEvents()
{
    std::vector<TVPWindow*>::iterator i;
    for (i = TVPWindowVector.begin(); i != TVPWindowVector.end(); i++)
    {
        (*i)->ClearInputEvents();
    }
}
//---------------------------------------------------------------------------
TVPWindow* TVPGetActiveWindow()
{
    return CurrentWindowLayer;
}
//---------------------------------------------------------------------------
void TVPSetActiveWindow(TVPWindow* window)
{
    CurrentWindowLayer = window;
}
//---------------------------------------------------------------------------
void TVPWindowHidden(TVPWindow* window)
{
    if (CurrentWindowLayer == window)
    {
        // 从后向前寻找可见窗口
        TVPWindow* anotherWin = NULL;
        for (size_t n = TVPWindowVector.size(); n > 0; n--)
        {
            TVPWindow* w = TVPWindowVector[n - 1];
            if (w != window && w->GetVisible())
            {
                anotherWin = w;
                break;
            }
        }
        CurrentWindowLayer = anotherWin;
    }
}
//---------------------------------------------------------------------------
bool TVPIsFirstWindow(TVPWindow* window)
{
    return !TVPWindowVector.empty() && TVPWindowVector[0] == window;
}
//---------------------------------------------------------------------------
void TVPClearAllWindows()
{
    while (!TVPWindowVector.empty())
    {
        delete TVPWindowVector[0];
    }

    // 确保所有指针都被置空
    TVPMainWindow = nullptr;
    CurrentWindowLayer = nullptr;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// SDL 平台层事件分发
//---------------------------------------------------------------------------
namespace krkrsdl3
{
TVPSprite* KRKR_Get_Current_Sprite()
{
    TVPWindow* win = TVPGetActiveWindow();
    if (win != NULL)
        return win->pSprite;
    return NULL;
}
//---------------------------------------------------------------------------
void KRKR_Trig_MouseDown(tTVPMouseButton mouseId, int x, int y)
{
    TVPWindow* win = TVPGetActiveWindow();
    if (win == NULL)
        return;
    TVPSprite* tmp = win->pSprite;
    if (tmp == NULL)
        return;

    _mouseBtn = mouseId;
    _scancode[TVPConvertMouseBtnToVKCode(mouseId)] = 0x11;
    TVPPostInputEvent(new tTVPOnMouseDownInputEvent(win, (x - tmp->xPos) / tmp->scale,
                                                    (y - tmp->yPos) / tmp->scale, mouseId,
                                                    TVPGetCurrentShiftKeyState()));
}
//---------------------------------------------------------------------------
void KRKR_Trig_MouseUp(tTVPMouseButton mouseId, int x, int y)
{
    TVPWindow* win = TVPGetActiveWindow();
    if (win == NULL)
        return;
    TVPSprite* tmp = win->pSprite;
    if (tmp == NULL)
        return;

    int lx = (x - tmp->xPos) / tmp->scale;
    int ly = (y - tmp->yPos) / tmp->scale;

    _mouseBtn = mouseId;
    if (mouseId == mbLeft)
    {
        TVPPostInputEvent(new tTVPOnClickInputEvent(win, lx, ly));
    }
    _scancode[TVPConvertMouseBtnToVKCode(mouseId)] &= 0x10;
    TVPPostInputEvent(
        new tTVPOnMouseUpInputEvent(win, lx, ly, mouseId, TVPGetCurrentShiftKeyState()));
}
//---------------------------------------------------------------------------
void KRKR_Trig_MouseMove(int x, int y)
{
    TVPWindow* win = TVPGetActiveWindow();
    if (win == NULL)
        return;
    TVPSprite* tmp = win->pSprite;
    if (tmp == NULL)
        return;

    int lx = (x - tmp->xPos) / tmp->scale;
    int ly = (y - tmp->yPos) / tmp->scale;
    TVPPostInputEvent(
        new tTVPOnMouseMoveInputEvent(win, lx, ly, TVPGetCurrentShiftKeyState()), TVP_EPT_DISCARDABLE);
    int pos = (ly << 16) + lx;
    TVPPushEnvironNoise(&pos, sizeof(pos));
}
//---------------------------------------------------------------------------
void KRKR_Trig_MouseScroll(int dx, int dy, int x, int y)
{
    TVPWindow* win = TVPGetActiveWindow();
    if (win == NULL)
        return;
    win->OnMouseWheel(TVPGetCurrentShiftKeyState(), dy > 0 ? -120 : 120, x, y);
}
//---------------------------------------------------------------------------
void KRKR_Trig_KeyDown(int vk)
{
    TVPWindow* win = TVPGetActiveWindow();
    if (win == NULL)
        return;

    unsigned int code = TVPConvertKeyCodeToVKCode(vk);
    if (!code || code >= 0x200)
        return;

    _scancode[code] = 0x11;
    TVPPostInputEvent(
        new tTVPOnKeyDownInputEvent(win, code, TVPGetCurrentShiftKeyState()));
}
//---------------------------------------------------------------------------
void KRKR_Trig_KeyUp(int vk)
{
    TVPWindow* win = TVPGetActiveWindow();
    if (win == NULL)
        return;

    unsigned int code = TVPConvertKeyCodeToVKCode(vk);
    if (!code || code >= 0x200)
        return;

    bool isPressed = _scancode[code] & 1;
    _scancode[code] &= 0x10;

    if (isPressed)
    {
        TVPPostInputEvent(
            new tTVPOnKeyUpInputEvent(win, code, TVPGetCurrentShiftKeyState()));
    }
}
//---------------------------------------------------------------------------
void KRKR_Trig_TextInput(std::string text)
{
    TVPWindow* win = TVPGetActiveWindow();
    if (win == NULL)
        return;

    tjs_wchar chwd = 0;
    const char* ptr = text.data();
    const char* ptrEnd = text.data() + text.size();
    while (ptr < ptrEnd)
    {
        int len = utf8_char_len(ptr);
        if (TVP_utf8_to_utf16(ptr, &chwd))
            win->PostKeyPress(chwd);
        if (len <= 0)
            len = 1;
        ptr += len;
    }
}
} // namespace krkrsdl3
