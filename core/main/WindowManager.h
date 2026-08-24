#pragma once

#include "TVPWindow.h"

//---------------------------------------------------------------------------
// Window List Management
// 所有窗口由 WindowManager 统一管理，并负责分发平台层传来的事件。
//---------------------------------------------------------------------------
extern TVPWindow* TVPMainWindow; // 主窗口（第一个创建的窗口）

// 由 TVPWindow 构造/析构时调用
extern void TVPRegisterWindowToList(TVPWindow* window);
extern void TVPUnregisterWindowToList(TVPWindow* window);

extern TVPWindow* TVPGetWindowListAt(tjs_int idx);
extern tjs_int TVPGetWindowCount();
extern void TVPClearAllWindowInputEvents();
extern void TVPClearAllWindows();

// 当前激活（最前）窗口
extern TVPWindow* TVPGetActiveWindow();
extern void TVPSetActiveWindow(TVPWindow* window);
extern void TVPWindowHidden(TVPWindow* window);
extern bool TVPIsFirstWindow(TVPWindow* window);

// 异步按键状态（scancode 状态表由 WindowManager 维护）
extern bool TVPGetKeyMouseAsyncState(tjs_uint keycode, bool getcurrent);
extern bool TVPGetJoyPadAsyncState(tjs_uint keycode, bool getcurrent);

//---------------------------------------------------------------------------
// 平台层事件入口
//---------------------------------------------------------------------------
namespace krkrsdl3
{
TVPSprite* KRKR_Get_Current_Sprite();
void KRKR_Trig_MouseDown(tTVPMouseButton mouseId, int x, int y);
void KRKR_Trig_MouseUp(tTVPMouseButton mouseId, int x, int y);
void KRKR_Trig_MouseMove(int x, int y);
void KRKR_Trig_MouseScroll(int dx, int dy, int x, int y);
void KRKR_Trig_KeyDown(int vk);
void KRKR_Trig_KeyUp(int vk);
void KRKR_Trig_TextInput(std::string text);
}
