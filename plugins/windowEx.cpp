#include "ncbind/ncbind.hpp"

#include "TVPScript.h"
#include "GraphicsLoadThread.h"
#include "tjsNativeWindow.h"
#include "Platform.h"

// 搞一堆空函数就行了，没啥好实现的
#define NCB_MODULE_NAME TJS_N("windowEx.dll")

using namespace TJS;

#define WM_NULL 0x0000
#define WM_CREATE 0x0001
#define WM_DESTROY 0x0002
#define WM_MOVE 0x0003
#define WM_SIZE 0x0005
#define WM_ACTIVATE 0x0006
#define WM_SETFOCUS 0x0007
#define WM_KILLFOCUS 0x0008
#define WM_ENABLE 0x000A
#define WM_SETREDRAW 0x000B
#define WM_SETTEXT 0x000C
#define WM_GETTEXT 0x000D
#define WM_GETTEXTLENGTH 0x000E
#define WM_PAINT 0x000F
#define WM_CLOSE 0x0010
#define WM_QUERYENDSESSION 0x0011
#define WM_QUERYOPEN 0x0013
#define WM_ENDSESSION 0x0016
#define WM_QUIT 0x0012
#define WM_ERASEBKGND 0x0014
#define WM_SYSCOLORCHANGE 0x0015
#define WM_SHOWWINDOW 0x0018
#define WM_WININICHANGE 0x001A
#define WM_SETTINGCHANGE WM_WININICHANGE
#define WM_DEVMODECHANGE 0x001B
#define WM_ACTIVATEAPP 0x001C
#define WM_FONTCHANGE 0x001D
#define WM_TIMECHANGE 0x001E
#define WM_CANCELMODE 0x001F
#define WM_SETCURSOR 0x0020
#define WM_MOUSEACTIVATE 0x0021
#define WM_CHILDACTIVATE 0x0022
#define WM_QUEUESYNC 0x0023
#define WM_GETMINMAXINFO 0x0024
#define WM_PAINTICON 0x0026
#define WM_ICONERASEBKGND 0x0027
#define WM_NEXTDLGCTL 0x0028
#define WM_SPOOLERSTATUS 0x002A
#define WM_DRAWITEM 0x002B
#define WM_MEASUREITEM 0x002C
#define WM_DELETEITEM 0x002D
#define WM_VKEYTOITEM 0x002E
#define WM_CHARTOITEM 0x002F
#define WM_SETFONT 0x0030
#define WM_GETFONT 0x0031
#define WM_SETHOTKEY 0x0032
#define WM_GETHOTKEY 0x0033
#define WM_QUERYDRAGICON 0x0037
#define WM_COMPAREITEM 0x0039
#define WM_GETOBJECT 0x003D
#define WM_COMPACTING 0x0041
#define WM_COMMNOTIFY 0x0044
#define WM_WINDOWPOSCHANGING 0x0046
#define WM_WINDOWPOSCHANGED 0x0047
#define WM_POWER 0x0048
#define WM_COPYDATA 0x004A
#define WM_CANCELJOURNAL 0x004B
#define WM_NOTIFY 0x004E
#define WM_INPUTLANGCHANGEREQUEST 0x0050
#define WM_INPUTLANGCHANGE 0x0051
#define WM_TCARD 0x0052
#define WM_HELP 0x0053
#define WM_USERCHANGED 0x0054
#define WM_NOTIFYFORMAT 0x0055
#define WM_CONTEXTMENU 0x007B
#define WM_STYLECHANGING 0x007C
#define WM_STYLECHANGED 0x007D
#define WM_DISPLAYCHANGE 0x007E
#define WM_GETICON 0x007F
#define WM_SETICON 0x0080
#define WM_NCCREATE 0x0081
#define WM_NCDESTROY 0x0082
#define WM_NCCALCSIZE 0x0083
#define WM_NCHITTEST 0x0084
#define WM_NCPAINT 0x0085
#define WM_NCACTIVATE 0x0086
#define WM_GETDLGCODE 0x0087
#define WM_SYNCPAINT 0x0088
#define WM_NCMOUSEMOVE 0x00A0
#define WM_NCLBUTTONDOWN 0x00A1
#define WM_NCLBUTTONUP 0x00A2
#define WM_NCLBUTTONDBLCLK 0x00A3
#define WM_NCRBUTTONDOWN 0x00A4
#define WM_NCRBUTTONUP 0x00A5
#define WM_NCRBUTTONDBLCLK 0x00A6
#define WM_NCMBUTTONDOWN 0x00A7
#define WM_NCMBUTTONUP 0x00A8
#define WM_NCMBUTTONDBLCLK 0x00A9
#define WM_NCXBUTTONDOWN 0x00AB
#define WM_NCXBUTTONUP 0x00AC
#define WM_NCXBUTTONDBLCLK 0x00AD
#define WM_INPUT_DEVICE_CHANGE 0x00FE
#define WM_INPUT 0x00FF
#define WM_KEYFIRST 0x0100
#define WM_KEYDOWN 0x0100
#define WM_KEYUP 0x0101
#define WM_CHAR 0x0102
#define WM_DEADCHAR 0x0103
#define WM_SYSKEYDOWN 0x0104
#define WM_SYSKEYUP 0x0105
#define WM_SYSCHAR 0x0106
#define WM_SYSDEADCHAR 0x0107
#define WM_UNICHAR 0x0109
#define WM_KEYLAST 0x0109
#define WM_IME_STARTCOMPOSITION 0x010D
#define WM_IME_ENDCOMPOSITION 0x010E
#define WM_IME_COMPOSITION 0x010F
#define WM_IME_KEYLAST 0x010F
#define WM_INITDIALOG 0x0110
#define WM_COMMAND 0x0111
#define WM_SYSCOMMAND 0x0112
#define WM_TIMER 0x0113
#define WM_HSCROLL 0x0114
#define WM_VSCROLL 0x0115
#define WM_INITMENU 0x0116
#define WM_INITMENUPOPUP 0x0117
#define WM_GESTURE 0x0119
#define WM_GESTURENOTIFY 0x011A
#define WM_MENUSELECT 0x011F
#define WM_MENUCHAR 0x0120
#define WM_ENTERIDLE 0x0121
#define WM_MENURBUTTONUP 0x0122
#define WM_MENUDRAG 0x0123
#define WM_MENUGETOBJECT 0x0124
#define WM_UNINITMENUPOPUP 0x0125
#define WM_MENUCOMMAND 0x0126
#define WM_CHANGEUISTATE 0x0127
#define WM_UPDATEUISTATE 0x0128
#define WM_QUERYUISTATE 0x0129
#define WM_CTLCOLORMSGBOX 0x0132
#define WM_CTLCOLOREDIT 0x0133
#define WM_CTLCOLORLISTBOX 0x0134
#define WM_CTLCOLORBTN 0x0135
#define WM_CTLCOLORDLG 0x0136
#define WM_CTLCOLORSCROLLBAR 0x0137
#define WM_CTLCOLORSTATIC 0x0138
#define WM_MOUSEFIRST 0x0200
#define WM_MOUSEMOVE 0x0200
#define WM_LBUTTONDOWN 0x0201
#define WM_LBUTTONUP 0x0202
#define WM_LBUTTONDBLCLK 0x0203
#define WM_RBUTTONDOWN 0x0204
#define WM_RBUTTONUP 0x0205
#define WM_RBUTTONDBLCLK 0x0206
#define WM_MBUTTONDOWN 0x0207
#define WM_MBUTTONUP 0x0208
#define WM_MBUTTONDBLCLK 0x0209
#define WM_MOUSEWHEEL 0x020A
#define WM_XBUTTONDOWN 0x020B
#define WM_XBUTTONUP 0x020C
#define WM_XBUTTONDBLCLK 0x020D
#define WM_MOUSEHWHEEL 0x020E
#define WM_MOUSELAST 0x020E
#define WM_PARENTNOTIFY 0x0210
#define WM_ENTERMENULOOP 0x0211
#define WM_EXITMENULOOP 0x0212
#define WM_NEXTMENU 0x0213
#define WM_SIZING 0x0214
#define WM_CAPTURECHANGED 0x0215
#define WM_MOVING 0x0216
#define WM_POWERBROADCAST 0x0218
#define WM_DEVICECHANGE 0x0219
#define WM_MDICREATE 0x0220
#define WM_MDIDESTROY 0x0221
#define WM_MDIACTIVATE 0x0222
#define WM_MDIRESTORE 0x0223
#define WM_MDINEXT 0x0224
#define WM_MDIMAXIMIZE 0x0225
#define WM_MDITILE 0x0226
#define WM_MDICASCADE 0x0227
#define WM_MDIICONARRANGE 0x0228
#define WM_MDIGETACTIVE 0x0229
#define WM_MDISETMENU 0x0230
#define WM_ENTERSIZEMOVE 0x0231
#define WM_EXITSIZEMOVE 0x0232
#define WM_DROPFILES 0x0233
#define WM_MDIREFRESHMENU 0x0234
#define WM_POINTERDEVICECHANGE 0x238
#define WM_POINTERDEVICEINRANGE 0x239
#define WM_POINTERDEVICEOUTOFRANGE 0x23A
#define WM_TOUCH 0x0240
#define WM_NCPOINTERUPDATE 0x0241
#define WM_NCPOINTERDOWN 0x0242
#define WM_NCPOINTERUP 0x0243
#define WM_POINTERUPDATE 0x0245
#define WM_POINTERDOWN 0x0246
#define WM_POINTERUP 0x0247
#define WM_POINTERENTER 0x0249
#define WM_POINTERLEAVE 0x024A
#define WM_POINTERACTIVATE 0x024B
#define WM_POINTERCAPTURECHANGED 0x024C
#define WM_TOUCHHITTESTING 0x024D
#define WM_POINTERWHEEL 0x024E
#define WM_POINTERHWHEEL 0x024F
#define WM_IME_SETCONTEXT 0x0281
#define WM_IME_NOTIFY 0x0282
#define WM_IME_CONTROL 0x0283
#define WM_IME_COMPOSITIONFULL 0x0284
#define WM_IME_SELECT 0x0285
#define WM_IME_CHAR 0x0286
#define WM_IME_REQUEST 0x0288
#define WM_IME_KEYDOWN 0x0290
#define WM_IME_KEYUP 0x0291
#define WM_MOUSEHOVER 0x02A1
#define WM_MOUSELEAVE 0x02A3
#define WM_NCMOUSEHOVER 0x02A0
#define WM_NCMOUSELEAVE 0x02A2
#define WM_WTSSESSION_CHANGE 0x02B1
#define WM_TABLET_FIRST 0x02c0
#define WM_TABLET_LAST 0x02df
#define WM_DPICHANGED 0x02E0
#define WM_CUT 0x0300
#define WM_COPY 0x0301
#define WM_PASTE 0x0302
#define WM_CLEAR 0x0303
#define WM_UNDO 0x0304
#define WM_RENDERFORMAT 0x0305
#define WM_RENDERALLFORMATS 0x0306
#define WM_DESTROYCLIPBOARD 0x0307
#define WM_DRAWCLIPBOARD 0x0308
#define WM_PAINTCLIPBOARD 0x0309
#define WM_VSCROLLCLIPBOARD 0x030A
#define WM_SIZECLIPBOARD 0x030B
#define WM_ASKCBFORMATNAME 0x030C
#define WM_CHANGECBCHAIN 0x030D
#define WM_HSCROLLCLIPBOARD 0x030E
#define WM_QUERYNEWPALETTE 0x030F
#define WM_PALETTEISCHANGING 0x0310
#define WM_PALETTECHANGED 0x0311
#define WM_HOTKEY 0x0312
#define WM_PRINT 0x0317
#define WM_PRINTCLIENT 0x0318
#define WM_APPCOMMAND 0x0319
#define WM_THEMECHANGED 0x031A
#define WM_CLIPBOARDUPDATE 0x031D
#define WM_DWMCOMPOSITIONCHANGED 0x031E
#define WM_DWMNCRENDERINGCHANGED 0x031F
#define WM_DWMCOLORIZATIONCOLORCHANGED 0x0320
#define WM_DWMWINDOWMAXIMIZEDCHANGE 0x0321
#define WM_DWMSENDICONICTHUMBNAIL 0x0323
#define WM_DWMSENDICONICLIVEPREVIEWBITMAP 0x0326
#define WM_GETTITLEBARINFOEX 0x033F
#define WM_HANDHELDFIRST 0x0358
#define WM_HANDHELDLAST 0x035F
#define WM_AFXFIRST 0x0360
#define WM_AFXLAST 0x037F
#define WM_PENWINFIRST 0x0380
#define WM_PENWINLAST 0x038F

#define HTBORDER 18      // 沒有重設大小框線的視窗框線中。
#define HTBOTTOM 15      // 可重設大小的視窗的下水準框線中（使用者可以按鼠以垂直調整視窗大小）。
#define HTBOTTOMLEFT 16  // 可重設大小的視窗框線左下角（使用者可以按鼠以對角調整視窗大小）。
#define HTBOTTOMRIGHT 17 // 可重設大小的視窗框線右下角（使用者可以按鼠以對角調整視窗大小）。
#define TCAPTION 2       // 標題列中。
#define TCLIENT 1        // 工作區中。
#define TCLOSE 20        // [關閉] 按鈕中。
#define HTERROR -2       // 螢幕背景或視窗之間的分隔線上（與 HTNOWHERE
                         // 相同，不同之處在於 DefWindowProc
                         // 函式會產生系統嗶聲來指出錯誤）。
#define HTGROWBOX 4      // 大小方塊中（與 HTSIZE 相同）。
#define HTHELP 21        // [ 說明] 按鈕中。
#define HTHSCROLL 6      // 水平滾動條中。
#define HTLEFT 10        // 可重設大小的視窗左框線中（使用者可以按鼠水平調整視窗大小）。
#define HTMENU 5         // 功能表中。
#define HTMAXBUTTON 9    // [最大化] 按鈕中。
#define HTMINBUTTON 8    // [最小化] 按鈕中。
#define HTNOWHERE 0      // 畫面背景或視窗之間的分隔線上。
#define HTREDUCE 8       // [最小化] 按鈕中。
#define HTRIGHT 11       // 可重設大小的視窗右框線中（使用者可以按鼠水平調整視窗大小）。
#define HTSIZE 4         // 大小方塊中（與 HTGROWBOX 相同）。
#define HTSYSMENU 3      // 視窗選單或子視窗的 [ 關閉 ] 按鈕中。
#define HTTOP 12         // 視窗的上水平框線中。
#define HTTOPLEFT 13     // 視窗框線的左上角。
#define HTTOPRIGHT 14    // 視窗框線的右上角。
#define HTTRANSPARENT -1 // 目前由相同線程中另一個視窗所涵蓋的視窗中。
#define HTVSCROLL 7      // 垂直滾動條中。
#define HTZOOM 9         // [最大化] 按鈕中。
#define HTCAPTION 2      // In a title bar.
#define HTCLIENT 1       // In a client area.

#define HBMMENU_CALLBACK -1
#define HBMMENU_SYSTEM 1
#define HBMMENU_MBAR_RESTORE 2
#define HBMMENU_MBAR_MINIMIZE 3
#define HBMMENU_MBAR_CLOSE 5
#define HBMMENU_MBAR_CLOSE_D 6
#define HBMMENU_MBAR_MINIMIZE_D 7
#define HBMMENU_POPUP_CLOSE 8
#define HBMMENU_POPUP_RESTORE 9
#define HBMMENU_POPUP_MAXIMIZE 10
#define HBMMENU_POPUP_MINIMIZE 11

// イベント名一覧
#define EXEV_MINIMIZE TJS_N("onMinimize")
#define EXEV_MAXIMIZE TJS_N("onMaximize")
#define EXEV_QUERYMAX TJS_N("onMaximizeQuery")
#define EXEV_SHOW TJS_N("onShow")
#define EXEV_HIDE TJS_N("onHide")
#define EXEV_RESIZING TJS_N("onResizing")
#define EXEV_MOVING TJS_N("onMoving")
#define EXEV_MOVE TJS_N("onMove")
#define EXEV_MVSZBEGIN TJS_N("onMoveSizeBegin")
#define EXEV_MVSZEND TJS_N("onMoveSizeEnd")
#define EXEV_DPICHANGE TJS_N("onDPIChanged")
#define EXEV_DISPCHG TJS_N("onDisplayChanged")
#define EXEV_DEVCHG TJS_N("onDeviceChanged")
#define EXEV_ENTERMENU TJS_N("onEnterMenuLoop")
#define EXEV_EXITMENU TJS_N("onExitMenuLoop")
#define EXEV_ACTIVATE TJS_N("onActivateChanged")
#define EXEV_SCREENSV TJS_N("onScreenSave")
#define EXEV_MONITORPW TJS_N("onMonitorPower")
#define EXEV_NCMSMOVE TJS_N("onNcMouseMove")
#define EXEV_NCMSLEAVE TJS_N("onNcMouseLeave")
#define EXEV_NCMSDOWN TJS_N("onNcMouseDown")
#define EXEV_NCMSUP TJS_N("onNcMouseUp")
#define EXEV_SYSMENU TJS_N("onExSystemMenuSelected")
#define EXEV_KEYMENU TJS_N("onStartKeyMenu")
#define EXEV_ACCELKEY TJS_N("onAccelKeyMenu")
#define EXEV_HOTKEY TJS_N("onHotKeyPressed")
#define EXEV_NCMSEV TJS_N("onNonCapMouseEvent")
#define EXEV_MSGHOOK TJS_N("onWindowsMessageHook")

// 吉里吉里2判定フラグ
static bool IsKirikiri2 = false;
static bool GetGlobalRecursive(const ttstr& name, tTJSVariant& result)
{
    // globalから . 区切りでデータを取得
    result.Clear();
    iTJSDispatch2* global = TVPGetScriptDispatch();
    if (!global)
        return false;
    tTJSVariantClosure clo(global, global);
    global->Release(); // NoAddRef.
    const tjs_char* p = name.c_str();
    const tjs_char* r = p;
    while (*p != 0)
    {
        while (*r && *r != TJS_N('.'))
            r++;
        result.Clear();
        ttstr div(p, r - p);
        if (div.IsEmpty() || TJS_FAILED(clo.PropGet(0, div.c_str(), NULL, &result, NULL)))
            return false;
        if (*r)
        {
            if (result.Type() != tvtObject)
                return false;
            clo = result.AsObjectNoAddRef();
            ++r;
        }
        p = r;
    }
    return true;
}
static bool CheckKirikiri2()
{
    // 吉里吉里2 かどうかを Window.PassThroughDrawDevice の有無で判定する
    tTJSVariant dd;
    return GetGlobalRecursive(TJS_N("Window.PassThroughDrawDevice"), dd) &&
           (dd.Type() == tvtObject);
}

////////////////////////////////////////////////////////////////

struct WindowEx
{
    //--------------------------------------------------------------
    // ユーティリティ

    // ネイティブインスタンスポインタを取得
    static inline WindowEx* GetInstance(iTJSDispatch2* obj)
    {
        return ncbInstanceAdaptor<WindowEx>::GetNativeInstance(obj);
    }

    // ウィンドウハンドルを取得
    static tTJSNI_Window* GetHWND(iTJSDispatch2* obj)
    {
        tTJSVariant val;
        obj->PropGet(0, TJS_N("HWND"), 0, &val, obj);
        return (tTJSNI_Window*)(tjs_int64)(val);
    }

    // RECTを辞書に保存
    static bool SetRect(ncbDictionaryAccessor& rdic, tTVPRect lprect)
    {
        bool r;
        if ((r = rdic.IsValid()))
        {
            rdic.SetValue(TJS_N("x"), lprect.left);
            rdic.SetValue(TJS_N("y"), lprect.top);
            rdic.SetValue(TJS_N("w"), lprect.right - lprect.left);
            rdic.SetValue(TJS_N("h"), lprect.bottom - lprect.top);
        }
        return r;
    }
    // 辞書からRECTに保存
    static void GetRect(tTVPRect& rect, ncbPropAccessor& dict)
    {
        ncbTypedefs::Tag<long> LongTypeTag;
        rect.left = dict.GetValue(TJS_N("x"), LongTypeTag);
        rect.top = dict.GetValue(TJS_N("y"), LongTypeTag);
        rect.right = dict.GetValue(TJS_N("w"), LongTypeTag);
        rect.bottom = dict.GetValue(TJS_N("h"), LongTypeTag);
        rect.right += rect.left;
        rect.bottom += rect.top;
    }

    //--------------------------------------------------------------
    // クラス追加メソッド(RawCallback形式)

    // minimize, maximize, showRestore
    static tjs_error minimize(tTJSVariant* r,
                                              tjs_int n,
                                              tTJSVariant** p,
                                              iTJSDispatch2* obj)
    {
        return TJS_S_OK;
    }
    static tjs_error maximize(tTJSVariant* r,
                                              tjs_int n,
                                              tTJSVariant** p,
                                              iTJSDispatch2* obj)
    {
        return TJS_S_OK;
    }
    static tjs_error showRestore(tTJSVariant* r,
                                                 tjs_int n,
                                                 tTJSVariant** p,
                                                 iTJSDispatch2* obj)
    {
        return TJS_S_OK;
    }
    static tjs_error focusMenuByKey(tTJSVariant* r,
                                                    tjs_int n,
                                                    tTJSVariant** p,
                                                    iTJSDispatch2* obj)
    {
        return TJS_S_OK;
    }

    // resetWindowIcon
    static tjs_error resetWindowIcon(tTJSVariant* r,
                                                     tjs_int n,
                                                     tTJSVariant** p,
                                                     iTJSDispatch2* obj)
    {
        return TJS_S_OK;
    }
    // setWindowIcon
    static tjs_error setWindowIcon(tTJSVariant* r,
                                                   tjs_int n,
                                                   tTJSVariant** p,
                                                   iTJSDispatch2* obj)
    {
        return TJS_S_OK;
    }

    // setWindowCornerPreference
    static tjs_error setWindowCornerPreference(tTJSVariant* r,
                                                               tjs_int n,
                                                               tTJSVariant** p,
                                                               iTJSDispatch2* obj)
    {
        return TJS_S_OK;
    }

    // getWindowRect
    static tjs_error getWindowRect(tTJSVariant* r, tjs_int n, tTJSVariant** p, iTJSDispatch2* obj)
    {
        if (r)
            r->Clear();
        ncbPropAccessor ncbPropAcc{obj};
        tjs_int left = ncbPropAcc.getIntValue(TJS_N("left"));
        tjs_int top = ncbPropAcc.getIntValue(TJS_N("top"));
        tjs_int width = ncbPropAcc.getIntValue(TJS_N("width"));
        tjs_int height = ncbPropAcc.getIntValue(TJS_N("height"));
        tTVPRect rect{left, top, width + left, height + top};
        ncbDictionaryAccessor dict;
        if (SetRect(dict, rect) && r)
            *r = tTJSVariant(dict, dict);
        return TJS_S_OK;
    }

    // getClientRect
    static tjs_error getClientRect(tTJSVariant* r,
                                                   tjs_int n,
                                                   tTJSVariant** p,
                                                   iTJSDispatch2* obj)
    {
        getWindowRect(r, n, p, obj);
        return TJS_S_OK;
    }

    // setClientRect
    static tjs_error setClientRect(tTJSVariant* r,
                                                   tjs_int n,
                                                   tTJSVariant** p,
                                                   iTJSDispatch2* obj)
    {
        return TJS_S_OK;
    }

    // getNormalRect
    static tjs_error getNormalRect(tTJSVariant* r,
                                                   tjs_int n,
                                                   tTJSVariant** p,
                                                   iTJSDispatch2* obj)
    {
        return TJS_S_OK;
    }

    // property maximized box
    static tjs_error getMaximizeBox(tTJSVariant* r,
                                                    tjs_int n,
                                                    tTJSVariant** p,
                                                    iTJSDispatch2* obj)
    {
        return TJS_S_OK;
    }
    static tjs_error setMaximizeBox(tTJSVariant* r,
                                                    tjs_int n,
                                                    tTJSVariant** p,
                                                    iTJSDispatch2* obj)
    {
        return TJS_S_OK;
    }

    // property minimized box
    static tjs_error getMinimizeBox(tTJSVariant* r,
                                                    tjs_int n,
                                                    tTJSVariant** p,
                                                    iTJSDispatch2* obj)
    {
        return TJS_S_OK;
    }
    static tjs_error setMinimizeBox(tTJSVariant* r,
                                                    tjs_int n,
                                                    tTJSVariant** p,
                                                    iTJSDispatch2* obj)
    {
        return TJS_S_OK;
    }

    // property maximized
    static bool isMaximized(iTJSDispatch2* obj) { return false; }
    static tjs_error getMaximized(tTJSVariant* r,
                                                  tjs_int n,
                                                  tTJSVariant** p,
                                                  iTJSDispatch2* obj)
    {
        if (r)
            *r = isMaximized(obj);
        return TJS_S_OK;
    }
    static tjs_error setMaximized(tTJSVariant* r,
                                                  tjs_int n,
                                                  tTJSVariant** p,
                                                  iTJSDispatch2* obj)
    {
        return TJS_S_OK;
    }

    // property minimized
    static bool isMinimized(iTJSDispatch2* obj) { return false; }
    static tjs_error getMinimized(tTJSVariant* r,
                                                  tjs_int n,
                                                  tTJSVariant** p,
                                                  iTJSDispatch2* obj)
    {
        if (r)
            *r = isMinimized(obj);
        return TJS_S_OK;
    }
    static tjs_error setMinimized(tTJSVariant* r,
                                                  tjs_int n,
                                                  tTJSVariant** p,
                                                  iTJSDispatch2* obj)
    {
        return TJS_S_OK;
    }

    // property disableResize
    static tjs_error getDisableResize(tTJSVariant* r,
                                                      tjs_int n,
                                                      tTJSVariant** p,
                                                      iTJSDispatch2* obj)
    {
        return TJS_S_OK;
    }
    static tjs_error setDisableResize(tTJSVariant* r,
                                                      tjs_int n,
                                                      tTJSVariant** p,
                                                      iTJSDispatch2* obj)
    {
        return TJS_S_OK;
    }

    // property disableMove
    static tjs_error getDisableMove(tTJSVariant* r,
                                                    tjs_int n,
                                                    tTJSVariant** p,
                                                    iTJSDispatch2* obj)
    {
        return TJS_S_OK;
    }
    static tjs_error setDisableMove(tTJSVariant* r,
                                                    tjs_int n,
                                                    tTJSVariant** p,
                                                    iTJSDispatch2* obj)
    {
        return TJS_S_OK;
    }

    // setOverlayBitmap
    static tjs_error setOverlayBitmap(tTJSVariant* r,
                                                      tjs_int n,
                                                      tTJSVariant** p,
                                                      iTJSDispatch2* obj)
    {
        return TJS_S_OK;
    }

    // property exSystemMenu
    static tjs_error getExSystemMenu(tTJSVariant* r,
                                                     tjs_int n,
                                                     tTJSVariant** p,
                                                     iTJSDispatch2* obj)
    {
        return TJS_S_OK;
    }
    static tjs_error setExSystemMenu(tTJSVariant* r,
                                                     tjs_int n,
                                                     tTJSVariant** p,
                                                     iTJSDispatch2* obj)
    {
        return TJS_S_OK;
    }
    // resetExSystemMenu
    static tjs_error resetExSystemMenu(tTJSVariant* r,
                                                       tjs_int n,
                                                       tTJSVariant** p,
                                                       iTJSDispatch2* obj)
    {
        return TJS_S_OK;
    }

    // property enableNCMouseEvent
    static tjs_error getEnNCMEvent(tTJSVariant* r,
                                                   tjs_int n,
                                                   tTJSVariant** p,
                                                   iTJSDispatch2* obj)
    {
        return TJS_S_OK;
    }
    static tjs_error setEnNCMEvent(tTJSVariant* r,
                                                   tjs_int n,
                                                   tTJSVariant** p,
                                                   iTJSDispatch2* obj)
    {
        return TJS_S_OK;
    }

    // ncHitTest
    static tjs_error nonClientHitTest(tTJSVariant* r,
                                                      tjs_int n,
                                                      tTJSVariant** p,
                                                      iTJSDispatch2* obj)
    {
        return TJS_S_OK;
    }

    // setMessageHook
    static tjs_error setMessageHook(tTJSVariant* r,
                                                    tjs_int n,
                                                    tTJSVariant** p,
                                                    iTJSDispatch2* obj)
    {
        return TJS_S_OK;
    }

    // bringTo
    static tjs_error bringTo(tTJSVariant* r,
                                             tjs_int n,
                                             tTJSVariant** p,
                                             iTJSDispatch2* obj)
    {
        return TJS_S_OK;
    }
    static tjs_error sendToBack(tTJSVariant* r,
                                                tjs_int n,
                                                tTJSVariant** p,
                                                iTJSDispatch2* obj)
    {
        return TJS_S_OK;
    }

    //
    static tjs_error registerDeviceChange(tTJSVariant* r,
                                                          tjs_int n,
                                                          tTJSVariant** p,
                                                          iTJSDispatch2* obj)
    {
        return TJS_S_OK;
    }

    static tjs_error registerHotKey(tTJSVariant* r,
                                                    tjs_int n,
                                                    tTJSVariant** p,
                                                    iTJSDispatch2* obj)
    {
        return TJS_S_OK;
    }

    static tjs_error acquireImeControl(tTJSVariant* r,
                                                       tjs_int n,
                                                       tTJSVariant** p,
                                                       iTJSDispatch2* obj)
    {
        return TJS_S_OK;
    }
    static tjs_error resetImeContext(tTJSVariant* r,
                                                     tjs_int n,
                                                     tTJSVariant** p,
                                                     iTJSDispatch2* obj)
    {
        return TJS_S_OK;
    }

    //--------------------------------------------------------------
    // 拡張イベント用

    // ネイティブインスタンスの生成・破棄にあわせてレシーバを登録・解除する
    WindowEx(iTJSDispatch2* obj) : self(obj) {}
    ~WindowEx() {}
    void checkExEvents() {}

    static tjs_int getWindowNotificationNum(ttstr key)
    {
        tTJSVariant tmp;
        if (!_getNotificationVariant(tmp))
            TVPThrowExceptionMessage(TJS_N("cache setup failed."));
        ncbPropAccessor nf(tmp);
        return nf.getIntValue(key.c_str(), -1);
    }
    static ttstr getWindowNotificationName(tjs_int num)
    {
        tTJSVariant tmp;
        if (!_getNotificationVariant(tmp))
            TVPThrowExceptionMessage(TJS_N("cache setup failed."));
        ncbPropAccessor nf(tmp);
        return nf.getStrValue(num);
    }

protected:
    static bool _getNotificationVariant(tTJSVariant& tmp)
    {
        iTJSDispatch2* obj = TVPGetScriptDispatch();
        tmp.Clear();
        bool hasval =
            TJS_SUCCEEDED(obj->PropGet(TJS_MEMBERMUSTEXIST, TJS_N("Window"), 0, &tmp, obj));
        obj->Release();
        if (!hasval)
            return false;

        obj = tmp.AsObjectNoAddRef();
        tmp.Clear();
        if (TJS_FAILED(obj->PropGet(TJS_MEMBERMUSTEXIST, TJS_N("_Notifications"), 0, &tmp, obj)))
        {
            ncbDictionaryAccessor dict;
#ifndef WM_CTLCOLOR
#define WM_CTLCOLOR 0x0019
#endif
#define WM(key) dict.SetValue(TJS_N(#key), WM_##key), dict.SetValue(WM_##key, ttstr(TJS_N(#key)))
            WM(NULL), WM(CREATE), WM(DESTROY), WM(MOVE), WM(SIZE), WM(ACTIVATE), WM(SETFOCUS),
                WM(KILLFOCUS), WM(ENABLE), WM(SETREDRAW), WM(SETTEXT), WM(GETTEXT),
                WM(GETTEXTLENGTH), WM(PAINT), WM(CLOSE), WM(QUERYENDSESSION), WM(QUERYOPEN),
                WM(ENDSESSION), WM(QUIT), WM(ERASEBKGND), WM(SYSCOLORCHANGE), WM(SHOWWINDOW),
                WM(CTLCOLOR), WM(WININICHANGE), WM(SETTINGCHANGE), WM(DEVMODECHANGE),
                WM(ACTIVATEAPP), WM(FONTCHANGE), WM(TIMECHANGE), WM(CANCELMODE), WM(SETCURSOR),
                WM(MOUSEACTIVATE), WM(CHILDACTIVATE), WM(QUEUESYNC), WM(GETMINMAXINFO),
                WM(PAINTICON), WM(ICONERASEBKGND), WM(NEXTDLGCTL), WM(SPOOLERSTATUS), WM(DRAWITEM),
                WM(MEASUREITEM), WM(DELETEITEM), WM(VKEYTOITEM), WM(CHARTOITEM), WM(SETFONT),
                WM(GETFONT), WM(SETHOTKEY), WM(GETHOTKEY), WM(QUERYDRAGICON), WM(COMPAREITEM),
                WM(GETOBJECT), WM(COMPACTING), WM(COMMNOTIFY), WM(WINDOWPOSCHANGING),
                WM(WINDOWPOSCHANGED), WM(POWER), WM(COPYDATA), WM(CANCELJOURNAL), WM(NOTIFY),
                WM(INPUTLANGCHANGEREQUEST), WM(INPUTLANGCHANGE), WM(TCARD), WM(HELP),
                WM(USERCHANGED), WM(NOTIFYFORMAT), WM(CONTEXTMENU), WM(STYLECHANGING),
                WM(STYLECHANGED), WM(DISPLAYCHANGE), WM(GETICON), WM(SETICON), WM(NCCREATE),
                WM(NCDESTROY), WM(NCCALCSIZE), WM(NCHITTEST), WM(NCPAINT), WM(NCACTIVATE),
                WM(GETDLGCODE), WM(SYNCPAINT), WM(NCMOUSEMOVE), WM(NCLBUTTONDOWN), WM(NCLBUTTONUP),
                WM(NCLBUTTONDBLCLK), WM(NCRBUTTONDOWN), WM(NCRBUTTONUP), WM(NCRBUTTONDBLCLK),
                WM(NCMBUTTONDOWN), WM(NCMBUTTONUP), WM(NCMBUTTONDBLCLK), WM(NCXBUTTONDOWN),
                WM(NCXBUTTONUP), WM(NCXBUTTONDBLCLK), WM(INPUT_DEVICE_CHANGE), WM(INPUT),
                WM(KEYFIRST), WM(KEYDOWN), WM(KEYUP), WM(CHAR), WM(DEADCHAR), WM(SYSKEYDOWN),
                WM(SYSKEYUP), WM(SYSCHAR), WM(SYSDEADCHAR), WM(UNICHAR), WM(KEYLAST), WM(KEYLAST),
                WM(IME_STARTCOMPOSITION), WM(IME_ENDCOMPOSITION), WM(IME_COMPOSITION),
                WM(IME_KEYLAST), WM(INITDIALOG), WM(COMMAND), WM(SYSCOMMAND), WM(TIMER),
                WM(HSCROLL), WM(VSCROLL), WM(INITMENU), WM(INITMENUPOPUP), WM(MENUSELECT),
                WM(MENUCHAR), WM(ENTERIDLE), WM(MENURBUTTONUP), WM(MENUDRAG), WM(MENUGETOBJECT),
                WM(UNINITMENUPOPUP), WM(MENUCOMMAND), WM(CHANGEUISTATE), WM(UPDATEUISTATE),
                WM(QUERYUISTATE), WM(CTLCOLORMSGBOX), WM(CTLCOLOREDIT), WM(CTLCOLORLISTBOX),
                WM(CTLCOLORBTN), WM(CTLCOLORDLG), WM(CTLCOLORSCROLLBAR), WM(CTLCOLORSTATIC),
                WM(MOUSEFIRST), WM(MOUSEMOVE), WM(LBUTTONDOWN), WM(LBUTTONUP), WM(LBUTTONDBLCLK),
                WM(RBUTTONDOWN), WM(RBUTTONUP), WM(RBUTTONDBLCLK), WM(MBUTTONDOWN), WM(MBUTTONUP),
                WM(MBUTTONDBLCLK), WM(MOUSEWHEEL), WM(XBUTTONDOWN), WM(XBUTTONUP),
                WM(XBUTTONDBLCLK), WM(MOUSEHWHEEL), WM(MOUSELAST), WM(MOUSELAST), WM(MOUSELAST),
                WM(MOUSELAST), WM(PARENTNOTIFY), WM(ENTERMENULOOP), WM(EXITMENULOOP), WM(NEXTMENU),
                WM(SIZING), WM(CAPTURECHANGED), WM(MOVING), WM(POWERBROADCAST), WM(DEVICECHANGE),
                WM(MDICREATE), WM(MDIDESTROY), WM(MDIACTIVATE), WM(MDIRESTORE), WM(MDINEXT),
                WM(MDIMAXIMIZE), WM(MDITILE), WM(MDICASCADE), WM(MDIICONARRANGE), WM(MDIGETACTIVE),
                WM(MDISETMENU), WM(ENTERSIZEMOVE), WM(EXITSIZEMOVE), WM(DROPFILES),
                WM(MDIREFRESHMENU), WM(IME_SETCONTEXT), WM(IME_NOTIFY), WM(IME_CONTROL),
                WM(IME_COMPOSITIONFULL), WM(IME_SELECT), WM(IME_CHAR), WM(IME_REQUEST),
                WM(IME_KEYDOWN), WM(IME_KEYUP), WM(MOUSEHOVER), WM(MOUSELEAVE), WM(NCMOUSEHOVER),
                WM(NCMOUSELEAVE), WM(WTSSESSION_CHANGE), WM(TABLET_FIRST), WM(TABLET_LAST), WM(CUT),
                WM(COPY), WM(PASTE), WM(CLEAR), WM(UNDO), WM(RENDERFORMAT), WM(RENDERALLFORMATS),
                WM(DESTROYCLIPBOARD), WM(DRAWCLIPBOARD), WM(PAINTCLIPBOARD), WM(VSCROLLCLIPBOARD),
                WM(SIZECLIPBOARD), WM(ASKCBFORMATNAME), WM(CHANGECBCHAIN), WM(HSCROLLCLIPBOARD),
                WM(QUERYNEWPALETTE), WM(PALETTEISCHANGING), WM(PALETTECHANGED), WM(HOTKEY),
                WM(PRINT), WM(PRINTCLIENT), WM(APPCOMMAND), WM(THEMECHANGED), WM(CLIPBOARDUPDATE),
                WM(DWMCOMPOSITIONCHANGED), WM(DWMNCRENDERINGCHANGED),
                WM(DWMCOLORIZATIONCOLORCHANGED), WM(DWMWINDOWMAXIMIZEDCHANGE),
                WM(GETTITLEBARINFOEX), WM(HANDHELDFIRST), WM(HANDHELDLAST), WM(AFXFIRST),
                WM(AFXLAST), WM(PENWINFIRST), WM(PENWINLAST),
#undef WM
                tmp = dict;
            if (TJS_FAILED(obj->PropSet(TJS_MEMBERENSURE, TJS_N("_Notifications"), 0, &tmp, obj)))
                return false;
        }
        return true;
    }

private:
    iTJSDispatch2* self;
};

// 拡張イベント用ネイティブインスタンスゲッタ
NCB_GET_INSTANCE_HOOK(WindowEx){/**/ NCB_GET_INSTANCE_HOOK_CLASS(){}
                                /**/ ~NCB_GET_INSTANCE_HOOK_CLASS(){} NCB_INSTANCE_GETTER(objthis){
                                    ClassT* obj = GetNativeInstance(objthis);
if (!obj)
    SetNativeInstance(objthis, (obj = new ClassT(objthis)));
return obj;
}
};

// メソッド追加
NCB_ATTACH_CLASS_WITH_HOOK(WindowEx, Window)
{
    Variant(TJS_N("nchtError"), (tjs_int)(HTERROR & 0xFFFF));
    Variant(TJS_N("nchtTransparent"), (tjs_int)(HTTRANSPARENT & 0xFFFF));
    Variant(TJS_N("nchtNoWhere"), (tjs_int)HTNOWHERE);
    Variant(TJS_N("nchtClient"), (tjs_int)HTCLIENT);
    Variant(TJS_N("nchtCaption"), (tjs_int)HTCAPTION);
    Variant(TJS_N("nchtSysMenu"), (tjs_int)HTSYSMENU);
    Variant(TJS_N("nchtSize"), (tjs_int)HTSIZE);
    Variant(TJS_N("nchtGrowBox"), (tjs_int)HTGROWBOX);
    Variant(TJS_N("nchtMenu"), (tjs_int)HTMENU);
    Variant(TJS_N("nchtHScroll"), (tjs_int)HTHSCROLL);
    Variant(TJS_N("nchtVScroll"), (tjs_int)HTVSCROLL);
    Variant(TJS_N("nchtMinButton"), (tjs_int)HTMINBUTTON);
    Variant(TJS_N("nchtReduce"), (tjs_int)HTREDUCE);
    Variant(TJS_N("nchtMaxButton"), (tjs_int)HTMAXBUTTON);
    Variant(TJS_N("nchtZoom"), (tjs_int)HTZOOM);
    Variant(TJS_N("nchtLeft"), (tjs_int)HTLEFT);
    Variant(TJS_N("nchtRight"), (tjs_int)HTRIGHT);
    Variant(TJS_N("nchtTop"), (tjs_int)HTTOP);
    Variant(TJS_N("nchtTopLeft"), (tjs_int)HTTOPLEFT);
    Variant(TJS_N("nchtTopRight"), (tjs_int)HTTOPRIGHT);
    Variant(TJS_N("nchtBottom"), (tjs_int)HTBOTTOM);
    Variant(TJS_N("nchtBottomLeft"), (tjs_int)HTBOTTOMLEFT);
    Variant(TJS_N("nchtBottomRight"), (tjs_int)HTBOTTOMRIGHT);
    Variant(TJS_N("nchtBorder"), (tjs_int)HTBORDER);

    RawCallback(TJS_N("minimize"), &Class::minimize, 0);
    RawCallback(TJS_N("maximize"), &Class::maximize, 0);
    RawCallback(TJS_N("maximizeBox"), &Class::getMaximizeBox, &Class::setMaximizeBox, 0);
    RawCallback(TJS_N("minimizeBox"), &Class::getMinimizeBox, &Class::setMinimizeBox, 0);
    RawCallback(TJS_N("maximized"), &Class::getMaximized, &Class::setMaximized, 0);
    RawCallback(TJS_N("minimized"), &Class::getMinimized, &Class::setMinimized, 0);
    RawCallback(TJS_N("showRestore"), &Class::showRestore, 0);
    RawCallback(TJS_N("resetWindowIcon"), &Class::resetWindowIcon, 0);
    RawCallback(TJS_N("setWindowIcon"), &Class::setWindowIcon, 0);
    RawCallback(TJS_N("getWindowRect"), &Class::getWindowRect, 0);
    RawCallback(TJS_N("getClientRect"), &Class::getClientRect, 0);
    RawCallback(TJS_N("setClientRect"), &Class::setClientRect, 0);
    RawCallback(TJS_N("getNormalRect"), &Class::getNormalRect, 0);
    RawCallback(TJS_N("disableResize"), &Class::getDisableResize, &Class::setDisableResize, 0);
    RawCallback(TJS_N("disableMove"), &Class::getDisableMove, &Class::setDisableMove, 0);
    RawCallback(TJS_N("setOverlayBitmap"), &Class::setOverlayBitmap, 0);
    RawCallback(TJS_N("exSystemMenu"), &Class::getExSystemMenu, &Class::setExSystemMenu, 0);
    RawCallback(TJS_N("resetExSystemMenu"), &Class::resetExSystemMenu, 0);
    RawCallback(TJS_N("enableNCMouseEvent"), &Class::getEnNCMEvent, &Class::setEnNCMEvent, 0);
    RawCallback(TJS_N("ncHitTest"), &Class::nonClientHitTest, 0);
    RawCallback(TJS_N("focusMenuByKey"), &Class::focusMenuByKey, 0);
    RawCallback(TJS_N("setMessageHook"), &Class::setMessageHook, 0);
    RawCallback(TJS_N("bringTo"), &Class::bringTo, 0);
    RawCallback(TJS_N("sendToBack"), &Class::sendToBack, 0);
    RawCallback(TJS_N("registerDeviceChange"), &Class::registerDeviceChange, 0);
    RawCallback(TJS_N("registerHotKey"), &Class::registerHotKey, 0);
    RawCallback(TJS_N("acquireImeControl"), &Class::acquireImeControl, 0);
    RawCallback(TJS_N("resetImeContext"), &Class::resetImeContext, 0);
    RawCallback(TJS_N("setWindowCornerPreference"), &Class::setWindowCornerPreference, 0);

    Method(TJS_N("registerExEvent"), &Class::checkExEvents);
    Method(TJS_N("getNotificationNum"), &Class::getWindowNotificationNum);
    Method(TJS_N("getNotificationName"), &Class::getWindowNotificationName);
}

////////////////////////////////////////////////////////////////
struct MenuItemEx
{
    // property rightJustify
    tjs_int getRightJustify() const { return 0; }
    void setRightJustify(tTJSVariant v) {}

    // property bmpItem
    tTVInteger getBmpItem() const { return 0; }
    void setBmpItem(tTJSVariant v) {}

    // property bmpChecked
    tTVInteger getBmpChecked() const { return 0; }
    void setBmpChecked(tTJSVariant v) {}

    // property bmpUnchecked
    tTVInteger getBmpUnchecked() const { return 0; }
    void setBmpUnchecked(tTJSVariant v) {}

    MenuItemEx(iTJSDispatch2* _obj) : obj(_obj) {}
    ~MenuItemEx() {}

private:
    iTJSDispatch2* obj;

public:
    // MenuItem.popupEx(flags, x=cursorX, y=cursorY, hwnd=this.root.window, rect,
    // menulist=this.children)
    static tjs_error popupEx(tTJSVariant* r,
                                             tjs_int n,
                                             tTJSVariant** p,
                                             iTJSDispatch2* objthis)
    {
        return TJS_S_OK;
    }
};
NCB_GET_INSTANCE_HOOK(MenuItemEx){
    /**/ NCB_GET_INSTANCE_HOOK_CLASS(){}
    /**/ ~NCB_GET_INSTANCE_HOOK_CLASS(){} NCB_INSTANCE_GETTER(objthis){
        ClassT* obj = GetNativeInstance(objthis);
if (!obj)
    SetNativeInstance(objthis, (obj = new ClassT(objthis)));
return obj;
}
}
;
NCB_ATTACH_CLASS_WITH_HOOK(MenuItemEx, MenuItem)
{
    Variant(TJS_N("biSystem"), (tjs_int64)HBMMENU_SYSTEM);
    Variant(TJS_N("biRestore"), (tjs_int64)HBMMENU_MBAR_RESTORE);
    Variant(TJS_N("biMinimize"), (tjs_int64)HBMMENU_MBAR_MINIMIZE);
    Variant(TJS_N("biClose"), (tjs_int64)HBMMENU_MBAR_CLOSE);
    Variant(TJS_N("biCloseDisabled"), (tjs_int64)HBMMENU_MBAR_CLOSE_D);
    Variant(TJS_N("biMinimizeDisabled"), (tjs_int64)HBMMENU_MBAR_MINIMIZE_D);
    Variant(TJS_N("biPopupClose"), (tjs_int64)HBMMENU_POPUP_CLOSE);
    Variant(TJS_N("biPopupRestore"), (tjs_int64)HBMMENU_POPUP_RESTORE);
    Variant(TJS_N("biPopupMaximize"), (tjs_int64)HBMMENU_POPUP_MAXIMIZE);
    Variant(TJS_N("biPopupMinimize"), (tjs_int64)HBMMENU_POPUP_MINIMIZE);

    Property(TJS_N("rightJustify"), &Class::getRightJustify, &Class::setRightJustify);
    Property(TJS_N("bmpItem"), &Class::getBmpItem, &Class::setBmpItem);
    Property(TJS_N("bmpChecked"), &Class::getBmpChecked, &Class::setBmpChecked);
    Property(TJS_N("bmpUnchecked"), &Class::getBmpUnchecked, &Class::setBmpUnchecked);
}
NCB_ATTACH_FUNCTION(popupEx, MenuItem, MenuItemEx::popupEx);

////////////////////////////////////////////////////////////////
struct ConsoleEx
{
    // restoreMaximize
    static tjs_error restoreMaximize(tTJSVariant* r, tjs_int n, tTJSVariant** p, iTJSDispatch2* obj)
    {
        return TJS_S_OK;
    }
    static tjs_error maximize(tTJSVariant* r, tjs_int n, tTJSVariant** p, iTJSDispatch2* obj)
    {
        return TJS_S_OK;
    }
    // getPlacement
    static tjs_error getPlacement(tTJSVariant* r, tjs_int n, tTJSVariant** p, iTJSDispatch2* obj)
    {
        return TJS_S_OK;
    }
    // setPlacement
    static tjs_error setPlacement(tTJSVariant* r, tjs_int n, tTJSVariant** p, iTJSDispatch2* obj)
    {
        return TJS_S_OK;
    }
    // getRect
    static tjs_error getRect(tTJSVariant* r, tjs_int n, tTJSVariant** p, iTJSDispatch2* obj)
    {
        return TJS_S_OK;
    }
    // setPos
    static tjs_error setPos(tTJSVariant* r, tjs_int n, tTJSVariant** p, iTJSDispatch2* obj)
    {
        return TJS_S_OK;
    }
    static tjs_error bringAfter(tTJSVariant* r, tjs_int n, tTJSVariant** p, iTJSDispatch2* obj)
    {
        return TJS_S_OK;
    }
};
NCB_ATTACH_FUNCTION_WITHTAG(restoreMaximize,
                            Debug_console,
                            Debug.console,
                            ConsoleEx::restoreMaximize);
NCB_ATTACH_FUNCTION_WITHTAG(maximize, Debug_console, Debug.console, ConsoleEx::maximize);
NCB_ATTACH_FUNCTION_WITHTAG(getRect, Debug_console, Debug.console, ConsoleEx::getRect);
NCB_ATTACH_FUNCTION_WITHTAG(setPos, Debug_console, Debug.console, ConsoleEx::setPos);
NCB_ATTACH_FUNCTION_WITHTAG(getPlacement, Debug_console, Debug.console, ConsoleEx::getPlacement);
NCB_ATTACH_FUNCTION_WITHTAG(setPlacement, Debug_console, Debug.console, ConsoleEx::setPlacement);
NCB_ATTACH_FUNCTION_WITHTAG(bringAfter, Debug_console, Debug.console, ConsoleEx::bringAfter);

////////////////////////////////////////////////////////////////
struct PadEx
{
    PadEx(iTJSDispatch2* obj) : self(obj) {}
    ~PadEx() {}
    void registerExEvents() {}

private:
    iTJSDispatch2* self;
};

NCB_GET_INSTANCE_HOOK(PadEx){/**/ NCB_GET_INSTANCE_HOOK_CLASS(){}
                             /**/ ~NCB_GET_INSTANCE_HOOK_CLASS(){} NCB_INSTANCE_GETTER(objthis){
                                 ClassT* obj = GetNativeInstance(objthis);
if (!obj)
    SetNativeInstance(objthis, (obj = new ClassT(objthis)));
return obj;
}
};
NCB_ATTACH_CLASS_WITH_HOOK(PadEx, Pad)
{
    Method(TJS_W("registerExEvent"), &Class::registerExEvents);
}
////////////////////////////////////////////////////////////////

struct System
{
    //--------------------------------------------------------------
    // クラス追加メソッド

    // System.getDisplayMonitors
    static tjs_error getDisplayMonitors(tTJSVariant* result,
                                                        tjs_int numparams,
                                                        tTJSVariant** param,
                                                        iTJSDispatch2* objthis)
    {
        tjs_int w = tTVPScreen::GetDesktopWidth();
        tjs_int h = tTVPScreen::GetDesktopHeight();

        ncbDictionaryAccessor monDict;
        monDict.SetValue(TJS_N("x"), 0);
        monDict.SetValue(TJS_N("y"), 0);
        monDict.SetValue(TJS_N("w"), w);
        monDict.SetValue(TJS_N("h"), h);
        monDict.SetValue(TJS_N("primary"), 1);

        iTJSDispatch2* arr = TJSCreateArrayObject();
        tTJSVariant monVar(monDict.GetDispatch(), monDict.GetDispatch());
        tTJSVariant idx(0);
        arr->PropSetByNum(TJS_MEMBERENSURE, 0, &monVar, arr);
        if (result)
            result->SetObject(arr, arr);
        arr->Release();
        return TJS_S_OK;
    }

    // System.getMonitorInfo
    static tjs_error getMonitorInfo(tTJSVariant* result,
                                                    tjs_int numparams,
                                                    tTJSVariant** param,
                                                    iTJSDispatch2* objthis)
    {
        tjs_int w = tTVPScreen::GetDesktopWidth();
        tjs_int h = tTVPScreen::GetDesktopHeight();

        ncbDictionaryAccessor monDict;
        monDict.SetValue(TJS_N("x"), 0);
        monDict.SetValue(TJS_N("y"), 0);
        monDict.SetValue(TJS_N("w"), w);
        monDict.SetValue(TJS_N("h"), h);

        ncbDictionaryAccessor workDict;
        workDict.SetValue(TJS_N("x"), 0);
        workDict.SetValue(TJS_N("y"), 0);
        workDict.SetValue(TJS_N("w"), w);
        workDict.SetValue(TJS_N("h"), h);

        ncbDictionaryAccessor resultDict;
        tTJSVariant monVar(monDict.GetDispatch(), monDict.GetDispatch());
        resultDict.SetValue(TJS_N("monitor"), monVar);
        tTJSVariant workVar(workDict.GetDispatch(), workDict.GetDispatch());
        resultDict.SetValue(TJS_N("work"), workVar);

        auto* dis = resultDict.GetDispatch();
        if (result)
            result->SetObject(dis, dis);
        return TJS_S_OK;
    }

    // System.getMouseCursorPos
    static tjs_error getCursorPos(tTJSVariant* r,
                                                  tjs_int n,
                                                  tTJSVariant** p,
                                                  iTJSDispatch2* obj)
    {
        return TJS_S_OK;
    }
    // System.setMouseCursorPos
    static tjs_error setCursorPos(tTJSVariant* r,
                                                  tjs_int n,
                                                  tTJSVariant** p,
                                                  iTJSDispatch2* obj)
    {
        return TJS_S_OK;
    }

    static tjs_error setClipCursor(tTJSVariant* r,
                                                   tjs_int n,
                                                   tTJSVariant** p,
                                                   iTJSDispatch2* obj)
    {
        return TJS_S_OK;
    }

    // System.getSystemMetrics
    static tjs_error getSystemMetrics(tTJSVariant* r,
                                                      tjs_int n,
                                                      tTJSVariant** p,
                                                      iTJSDispatch2* objthis)
    {
        return TJS_S_OK;
    }

    // System.readEnvValue
    static tjs_error readEnvValue(tTJSVariant* r,
                                                  tjs_int n,
                                                  tTJSVariant** p,
                                                  iTJSDispatch2* objthis)
    {
        return TJS_S_OK;
    }
    // System.expandEnvString
    static tjs_error expandEnvString(tTJSVariant* r,
                                                     tjs_int n,
                                                     tTJSVariant** p,
                                                     iTJSDispatch2* objthis)
    {
        return TJS_S_OK;
    }

    // System.setApplicationIcon
    static tjs_error setApplicationIcon(tTJSVariant* r,
                                                        tjs_int n,
                                                        tTJSVariant** p,
                                                        iTJSDispatch2* obj)
    {
        return TJS_S_OK;
    }
    static bool setIconicPreview(bool en) { return true; }

    static tjs_int getDoubleClickTime() { return 0; }

    // SetThreadDpiAwarenessContextラッパー
    static tTVInteger setThreadDpiAwarenessContext(tTVInteger context) { return 0; }

    // System.findWindowEx(winname=void, clsname=void, parwin=void, childafter=void);
    static tjs_error findWindowEx(tTJSVariant* r,
                                                  tjs_int n,
                                                  tTJSVariant** p,
                                                  iTJSDispatch2* obj)
    {
        return TJS_S_OK;
    }
    // System.loadCursor(idc_or_res, hmodule=void)
    static tjs_error loadCursor(tTJSVariant* r,
                                                tjs_int n,
                                                tTJSVariant** p,
                                                iTJSDispatch2* obj)
    {
        return TJS_S_OK;
    }
    // System.classLongPtr(win, key, setvalue_or_getasvoid=void);
    static tjs_error classLongPtr(tTJSVariant* r,
                                                  tjs_int n,
                                                  tTJSVariant** p,
                                                  iTJSDispatch2* obj)
    {
        return TJS_S_OK;
    }
    // System.mapVirtualKey(code, maptype)
    static tjs_uint mapVirtualKey(tjs_int code, tjs_int maptype) { return TJS_S_OK; }
};

// Systemに関数を追加
NCB_ATTACH_FUNCTION(getDisplayMonitors, System, System::getDisplayMonitors);
NCB_ATTACH_FUNCTION(getMonitorInfo, System, System::getMonitorInfo);
NCB_ATTACH_FUNCTION(getCursorPos, System, System::getCursorPos);
NCB_ATTACH_FUNCTION(setCursorPos, System, System::setCursorPos);
NCB_ATTACH_FUNCTION(setClipCursor, System, System::setClipCursor);
NCB_ATTACH_FUNCTION(getSystemMetrics, System, System::getSystemMetrics);
NCB_ATTACH_FUNCTION(readEnvValue, System, System::readEnvValue);
NCB_ATTACH_FUNCTION(expandEnvString, System, System::expandEnvString);
NCB_ATTACH_FUNCTION(setApplicationIcon, System, System::setApplicationIcon);
NCB_ATTACH_FUNCTION(setIconicPreview, System, System::setIconicPreview);
NCB_ATTACH_FUNCTION(getDoubleClickTime, System, System::getDoubleClickTime);
NCB_ATTACH_FUNCTION(setDpiAwareness, System, System::setThreadDpiAwarenessContext);
NCB_ATTACH_FUNCTION(findWindowEx, System, System::findWindowEx);
NCB_ATTACH_FUNCTION(classLongPtr, System, System::classLongPtr);
NCB_ATTACH_FUNCTION(loadCursor, System, System::loadCursor);
NCB_ATTACH_FUNCTION(mapVirtualKey, System, System::mapVirtualKey);
NCB_ATTACH_FUNCTION(breathe, System, TVPBreathe);
NCB_ATTACH_FUNCTION(isBreathing, System, TVPGetBreathing);
NCB_ATTACH_FUNCTION(clearGraphicCache, System, TVPClearGraphicCache);
NCB_ATTACH_FUNCTION(getAboutString, System, TVPGetAboutString);
NCB_ATTACH_FUNCTION(getCPUType, System, TVPGetCPUType);

////////////////////////////////////////////////////////////////

struct Scripts
{
    static bool outputErrorLogOnEval;

    // property Scripts.outputErrorLogOnEval
    static bool setEvalErrorLog(bool v)
    {
        bool ret = outputErrorLogOnEval;
        /**/ outputErrorLogOnEval = v;
        return ret;
    }

    // Scripts.eval オーバーライド
    static tjs_error eval(tTJSVariant* r, tjs_int n, tTJSVariant** p, iTJSDispatch2* objthis)
    {
        if (outputErrorLogOnEval)
            return evalOrig->FuncCall(0, NULL, NULL, r, n, p, objthis);

        if (n < 1)
            return TJS_E_BADPARAMCOUNT;
        ttstr content = *p[0], name;
        tjs_int lineofs = 0;
        if (n >= 2)
            name = *p[1];
        if (n >= 3)
            lineofs = *p[2];

        TVPExecuteExpression(content, name, lineofs, r);
        return TJS_S_OK;
    }
    // 元の Scripts.eval を保存・復帰
    static void Regist()
    {
        tTJSVariant var;
        TVPExecuteExpression(TJS_N("Scripts.eval"), &var);
        evalOrig = var.AsObject();
    }
    static void UnRegist()
    {
        if (evalOrig)
            evalOrig->Release();
        evalOrig = NULL;
    }
    static iTJSDispatch2* evalOrig;
};
iTJSDispatch2* Scripts::evalOrig = NULL;   // Scripts.evalの元のオブジェクト
bool Scripts::outputErrorLogOnEval = true; // 切り替えフラグ

// Scriptsに関数を追加
NCB_ATTACH_FUNCTION(eval, Scripts, Scripts::eval);
NCB_ATTACH_FUNCTION(setEvalErrorLog, Scripts, Scripts::setEvalErrorLog);

////////////////////////////////////////////////////////////////
// コールバック指定
static void InitPlugin_WINDOWEx()
{
    Scripts::Regist();
}
static void UnInitPlugin_WINDOWEx()
{
    Scripts::UnRegist();
}

NCB_PRE_REGIST_CALLBACK(InitPlugin_WINDOWEx);
NCB_POST_UNREGIST_CALLBACK(UnInitPlugin_WINDOWEx);