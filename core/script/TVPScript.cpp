//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// TJS2 Script Managing
//---------------------------------------------------------------------------

#include "tjsCommHead.h"
#include "TVPScript.h"

#include "tjs.h"
#include "tjsDebug.h"
#include "tjsArray.h"
#include "TVPEvent.h"
#include "TVPSystem.h"
#include "TVPDebug.h"
#include "TVPMsg.h"
#include "Random.h"
#include "TextStream.h"
#include "Platform.h"
#include "tjsRandomGenerator.h"
#include "TVPApplication.h"
#include "TVPWindow.h"
#include "WindowManager.h"

#include "tjsNativeDebug.h"
#include "tjsNativeLayer.h"
#include "tjsNativeFont.h"
#include "tjsNativeCDDASoundBuffer.h"
#include "tjsNativeMIDISoundBuffer.h"
#include "tjsNativeTimer.h"
#include "tjsNativeAsyncTrigger.h"
#include "tjsNativeSystem.h"
#include "tjsNativeStorages.h"
#include "tjsNativePlugins.h"
#include "tjsNativeVideoOverlay.h"
#include "tjsNativePad.h"
#include "tjsNativeClipboard.h"
#include "tjsNativeScript.h"
#include "tjsNativeRect.h"
#include "tjsNativeBitmap.h"
#include "tjsNativeImageFunction.h"
#include "tjsNativeBitmapLayerTreeOwner.h"
#include "tjsNativeKAGParser.h"
#include "tjsNativeWaveSoundBuffer.h"
#include "tjsNativePhaseVocoder.h"
#include "tjsNativeWindow.h"
#include "tjsNativeBasicDrawDevice.h"
#include "tjsNativeMenuItem.h"
#include "TVPSettings.h"

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
// -- -- -- -- -- - tTVPAtInstallClass
//---------------------------------------------------------------------------
extern void TVPAddClassHandler(const tjs_char* name,
                               iTJSDispatch2* (*handler)(iTJSDispatch2*),
                               const tjs_char* dependences);
struct tTVPAtInstallClass
{
    tTVPAtInstallClass(const tjs_char* name,
                       iTJSDispatch2* (*handler)(iTJSDispatch2*),
                       const tjs_char* dependences)
    {
        TVPAddClassHandler(name, handler, dependences);
    }
};

//---------------------------------------------------------------------------
// TVPAddClassHandler related
//---------------------------------------------------------------------------
struct tTVPAtClassInstallInfo
{
    tTVPAtClassInstallInfo(const tjs_char* name,
                           iTJSDispatch2* (*handler)(iTJSDispatch2*),
                           const tjs_char* dependences)
    {
        Name = name, Handler = handler;
        if (dependences)
        {
            ttstr dep(dependences);
            const tjs_char* start = dependences;
            const tjs_char* cur = dependences;
            while (*cur)
            {
                if ((*cur) == TJS_N(','))
                {
                    if (start != cur)
                    {
                        Dependences.push_back(ttstr(start, cur - start));
                    }
                    start = cur + 1;
                }
                cur++;
            }
            if (start != cur)
            {
                Dependences.push_back(ttstr(start, cur - start));
            }
        }
    }
    const tjs_char* Name;
    iTJSDispatch2* (*Handler)(iTJSDispatch2*);
    std::vector<ttstr> Dependences; // 依存クラスリスト
};
static std::vector<tTVPAtClassInstallInfo>* TVPAtClassInstallInfos = NULL;
static bool TVPAtInstallClass = false;
//---------------------------------------------------------------------------
void TVPAddClassHandler(const tjs_char* name,
                        iTJSDispatch2* (*handler)(iTJSDispatch2*),
                        const tjs_char* dependences)
{
    if (TVPAtInstallClass)
        return;

    if (!TVPAtClassInstallInfos)
        TVPAtClassInstallInfos = new std::vector<tTVPAtClassInstallInfo>();
    TVPAtClassInstallInfos->push_back(tTVPAtClassInstallInfo(name, handler, dependences));
}
//---------------------------------------------------------------------------
/*

以下のような書式で、cpp に入れておくと、VM初期化後、クラスを追加する段階で追加してくれます。
静的リンクするとクラス追加されるpluginの様な機能です。
pluginは実行時にクラス等追加しますが、extensionはビルドする時に、入れるクラスを選択する形です。
static tTVPAtInstallClass TVPInstallClassFoo
        (TJS_N("ClassFoo"), TVPCreateNativeClass_ClassFoo,TJS_N("Window,Layer"));
呼び出される関数にはglobalが渡されるので、必要であればそこからメンバなど取得します。
登録は呼び出し側が返り値のiTJSDispatch2をglobalに登録するので、呼び出された関数内で登録する必要
はありません。
登録時依存クラスを3番目に指定可能ですが、現在のところ無視されています。
*/
void TVPCauseAtInstallExtensionClass(iTJSDispatch2* global)
{
    if (TVPAtInstallClass)
        return;
    TVPAtInstallClass = true;

    if (TVPAtClassInstallInfos)
    {
        iTJSDispatch2* dsp;
        tTJSVariant val;
        std::vector<tTVPAtClassInstallInfo>::iterator i;
        for (i = TVPAtClassInstallInfos->begin(); i != TVPAtClassInstallInfos->end(); i++)
        {
            dsp = i->Handler(global);
            val = tTJSVariant(dsp /*, dsp*/);
            dsp->Release();
            global->PropSet(TJS_MEMBERENSURE | TJS_IGNOREPROP, i->Name, NULL, &val, global);
        }
        delete TVPAtClassInstallInfos;
        TVPAtClassInstallInfos = NULL;
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// Script system initialization script
//---------------------------------------------------------------------------
static const tjs_char* TVPInitTJSScript =
    // note that this script is stored as narrow string
    "const\
\
/* constants */\
 /* tTVPBorderStyle */ bsNone=0,  bsSingle=1,  bsSizeable=2,  bsDialog=3,  bsToolWindow=4,  bsSizeToolWin=5,\
 /* tTVPUpdateType */ utNormal=0,  utEntire =1,\
 /* tTVPMouseButton */  mbLeft=0,  mbRight=1,  mbMiddle=2, mbX1=3, mbX2=4,\
 /* tTVPMouseCursorState */ mcsVisible=0, mcsTempHidden=1, mcsHidden=2,\
 /* tTVPImeMode */ imDisable=0, imClose=1, imOpen=2, imDontCare=3, imSAlpha=4, imAlpha=5, imHira=6, imSKata=7, imKata=8, imChinese=9, imSHanguel=10, imHanguel=11,\
 /* Set of shift state */  ssShift=(1<<0),  ssAlt=(1<<1),  ssCtrl=(1<<2),  ssLeft=(1<<3),  ssRight=(1<<4),  ssMiddle=(1<<5),  ssDouble =(1<<6),  ssRepeat = (1<<7),\
 /* TVP_FSF_???? */ fsfFixedPitch=1, fsfSameCharSet=2, fsfNoVertical=4, \
	fsfTrueTypeOnly=8, fsfUseFontFace=0x100, fsfIgnoreSymbol=0x10,\
 /* tTVPLayerType */ ltBinder=0, ltCoverRect=1, ltOpaque=1, ltTransparent=2, ltAlpha=2, ltAdditive=3, ltSubtractive=4, ltMultiplicative=5, ltEffect=6, ltFilter=7, ltDodge=8, ltDarken=9, ltLighten=10, ltScreen=11, ltAddAlpha = 12,\
	ltPsNormal = 13, ltPsAdditive = 14, ltPsSubtractive = 15, ltPsMultiplicative = 16, ltPsScreen = 17, ltPsOverlay = 18, ltPsHardLight = 19, ltPsSoftLight = 20, ltPsColorDodge = 21, ltPsColorDodge5 = 22, ltPsColorBurn = 23, ltPsLighten = 24, ltPsDarken = 25, ltPsDifference = 26, ltPsDifference5 = 27, ltPsExclusion = 28, \
 /* tTVPBlendOperationMode */ omPsNormal = ltPsNormal,omPsAdditive = ltPsAdditive,omPsSubtractive = ltPsSubtractive,omPsMultiplicative = ltPsMultiplicative,omPsScreen = ltPsScreen,omPsOverlay = ltPsOverlay,omPsHardLight = ltPsHardLight,omPsSoftLight = ltPsSoftLight,omPsColorDodge = ltPsColorDodge,omPsColorDodge5 = ltPsColorDodge5,omPsColorBurn = ltPsColorBurn,omPsLighten = ltPsLighten,omPsDarken = ltPsDarken,omPsDifference = ltPsDifference,omPsDifference5 = ltPsDifference5,omPsExclusion = ltPsExclusion, \
	omAdditive=ltAdditive, omSubtractive=ltSubtractive, omMultiplicative=ltMultiplicative, omDodge=ltDodge, omDarken=ltDarken, omLighten=ltLighten, omScreen=ltScreen, omAddAlpha=ltAddAlpha, omOpaque=ltOpaque, omAlpha=ltAlpha, omAuto = 128,\
 /* tTVPDrawFace */ dfBoth=0, dfAlpha = dfBoth, dfAddAlpha = 4, dfMain=1, dfOpaque = dfMain, dfMask=2, dfProvince=3, dfAuto=128,\
 /* tTVPHitType */ htMask=0, htProvince=1,\
 /* tTVPScrollTransFrom */ sttLeft=0, sttTop=1, sttRight=2, sttBottom=3,\
 /* tTVPScrollTransStay */ ststNoStay=0, ststStayDest=1, ststStaySrc=2, \
 /* tTVPKAGDebugLevel */ tkdlNone=0, tkdlSimple=1, tkdlVerbose=2, \
 /* tTVPAsyncTriggerMode */	atmNormal=0, atmExclusive=1, atmAtIdle=2, \
 /* tTVPBBStretchType */ stNearest=0, stFastLinear=1, stLinear=2, stCubic=3, stSemiFastLinear = 4, stFastCubic = 5, stLanczos2 = 6, stFastLanczos2 = 7, stLanczos3 = 8, stFastLanczos3 = 9, stSpline16 = 10, stFastSpline16 = 11, stSpline36 = 12, stFastSpline36 = 13, stAreaAvg = 14, stFastAreaAvg = 15, stGaussian = 16, stFastGaussian = 17, stBlackmanSinc = 18, stFastBlackmanSinc = 19, stRefNoClip = 0x10000,\
 /* tTVPClipboardFormat */ cbfText = 1,\
 /* TVP_COMPACT_LEVEL_???? */ clIdle = 5, clDeactivate = 10, clMinimize = 15, clAll = 100,\
 /* tTVPVideoOverlayMode Add: T.Imoto */ vomOverlay=0, vomLayer=1, vomMixer=2, vomMFEVR=3,\
 /* tTVPPeriodEventReason */ perLoop = 0, perPeriod = 1, perPrepare = 2, perSegLoop = 3,\
 /* tTVPSoundGlobalFocusMode */ sgfmNeverMute = 0, sgfmMuteOnMinimize = 1, sgfmMuteOnDeactivate = 2,\
 /* tTVPTouchDevice */ tdNone=0, tdIntegratedTouch=0x01, tdExternalTouch=0x02, tdIntegratedPen=0x04, tdExternalPen=0x08, tdMultiInput=0x40, tdDigitizerReady=0x80,\
    tdMouse=0x0100, tdMouseWheel=0x0200,\
 /* Display Orientation */ oriUnknown=0, oriPortrait=1, oriLandscape=2,\
\
/* file attributes */\
 faReadOnly=0x01, faHidden=0x02, faSysFile=0x04, faVolumeID=0x08, faDirectory=0x10, faArchive=0x20, faAnyFile=0x3f,\
/* mouse cursor constants */\
 crDefault = 0x0,\
 crNone = -1,\
 crArrow = -2,\
 crCross = -3,\
 crIBeam = -4,\
 crSize = -5,\
 crSizeNESW = -6,\
 crSizeNS = -7,\
 crSizeNWSE = -8,\
 crSizeWE = -9,\
 crUpArrow = -10,\
 crHourGlass = -11,\
 crDrag = -12,\
 crNoDrop = -13,\
 crHSplit = -14,\
 crVSplit = -15,\
 crMultiDrag = -16,\
 crSQLWait = -17,\
 crNo = -18,\
 crAppStart = -19,\
 crHelp = -20,\
 crHandPoint = -21,\
 crSizeAll = -22,\
 crHBeam = 1,\
/* color constants */\
 clScrollBar = 0x80000000,\
 clBackground = 0x80000001,\
 clActiveCaption = 0x80000002,\
 clInactiveCaption = 0x80000003,\
 clMenu = 0x80000004,\
 clWindow = 0x80000005,\
 clWindowFrame = 0x80000006,\
 clMenuText = 0x80000007,\
 clWindowText = 0x80000008,\
 clCaptionText = 0x80000009,\
 clActiveBorder = 0x8000000a,\
 clInactiveBorder = 0x8000000b,\
 clAppWorkSpace = 0x8000000c,\
 clHighlight = 0x3399ff,\
 clHighlightText = 0x8000000e,\
 clBtnFace = 0xf0f0f0,\
 clBtnShadow = 0x787878,\
 clGrayText = 0x80000011,\
 clBtnText = 0x000000,\
 clInactiveCaptionText = 0x80000013,\
 clBtnHighlight = 0x80000014,\
 cl3DDkShadow = 0x80000015,\
 cl3DLight = 0x80000016,\
 clInfoText = 0x80000017,\
 clInfoBk = 0x80000018,\
 clNone = 0x1fffffff,\
 clAdapt= 0x01ffffff,\
 clPalIdx = 0x3000000,\
 clAlphaMat = 0x4000000,\
/* for Menu.trackPopup (see winuser.h) */\
 tpmLeftButton      = 0x0000,\
 tpmRightButton     = 0x0002,\
 tpmLeftAlign       = 0x0000,\
 tpmCenterAlign     = 0x0004,\
 tpmRightAlign      = 0x0008,\
 tpmTopAlign        = 0x0000,\
 tpmVCenterAlign    = 0x0010,\
 tpmBottomAlign     = 0x0020,\
 tpmHorizontal      = 0x0000,\
 tpmVertical        = 0x0040,\
 tpmNoNotify        = 0x0080,\
 tpmReturnCmd       = 0x0100,\
 tpmRecurse         = 0x0001,\
 tpmHorPosAnimation = 0x0400,\
 tpmHorNegAnimation = 0x0800,\
 tpmVerPosAnimation = 0x1000,\
 tpmVerNegAnimation = 0x2000,\
 tpmNoAnimation     = 0x4000,\
/* for Pad.showScrollBars (see Vcl/stdctrls.hpp :: enum TScrollStyle) */\
 ssNone       = 0,\
 ssHorizontal = 1,\
 ssVertical   = 2,\
 ssBoth       = 3,\
/* virtual keycodes */\
 VK_LBUTTON =0x01,\
 VK_RBUTTON =0x02,\
 VK_CANCEL =0x03,\
 VK_MBUTTON =0x04,\
 VK_BACK =0x08,\
 VK_TAB =0x09,\
 VK_CLEAR =0x0C,\
 VK_RETURN =0x0D,\
 VK_SHIFT =0x10,\
 VK_CONTROL =0x11,\
 VK_MENU =0x12,\
 VK_PAUSE =0x13,\
 VK_CAPITAL =0x14,\
 VK_KANA =0x15,\
 VK_HANGEUL =0x15,\
 VK_HANGUL =0x15,\
 VK_JUNJA =0x17,\
 VK_FINAL =0x18,\
 VK_HANJA =0x19,\
 VK_KANJI =0x19,\
 VK_ESCAPE =0x1B,\
 VK_CONVERT =0x1C,\
 VK_NONCONVERT =0x1D,\
 VK_ACCEPT =0x1E,\
 VK_MODECHANGE =0x1F,\
 VK_SPACE =0x20,\
 VK_PRIOR =0x21,\
 VK_NEXT =0x22,\
 VK_END =0x23,\
 VK_HOME =0x24,\
 VK_LEFT =0x25,\
 VK_UP =0x26,\
 VK_RIGHT =0x27,\
 VK_DOWN =0x28,\
 VK_SELECT =0x29,\
 VK_PRINT =0x2A,\
 VK_EXECUTE =0x2B,\
 VK_SNAPSHOT =0x2C,\
 VK_INSERT =0x2D,\
 VK_DELETE =0x2E,\
 VK_HELP =0x2F,\
 VK_0 =0x30,\
 VK_1 =0x31,\
 VK_2 =0x32,\
 VK_3 =0x33,\
 VK_4 =0x34,\
 VK_5 =0x35,\
 VK_6 =0x36,\
 VK_7 =0x37,\
 VK_8 =0x38,\
 VK_9 =0x39,\
 VK_A =0x41,\
 VK_B =0x42,\
 VK_C =0x43,\
 VK_D =0x44,\
 VK_E =0x45,\
 VK_F =0x46,\
 VK_G =0x47,\
 VK_H =0x48,\
 VK_I =0x49,\
 VK_J =0x4A,\
 VK_K =0x4B,\
 VK_L =0x4C,\
 VK_M =0x4D,\
 VK_N =0x4E,\
 VK_O =0x4F,\
 VK_P =0x50,\
 VK_Q =0x51,\
 VK_R =0x52,\
 VK_S =0x53,\
 VK_T =0x54,\
 VK_U =0x55,\
 VK_V =0x56,\
 VK_W =0x57,\
 VK_X =0x58,\
 VK_Y =0x59,\
 VK_Z =0x5A,\
 VK_LWIN =0x5B,\
 VK_RWIN =0x5C,\
 VK_APPS =0x5D,\
 VK_NUMPAD0 =0x60,\
 VK_NUMPAD1 =0x61,\
 VK_NUMPAD2 =0x62,\
 VK_NUMPAD3 =0x63,\
 VK_NUMPAD4 =0x64,\
 VK_NUMPAD5 =0x65,\
 VK_NUMPAD6 =0x66,\
 VK_NUMPAD7 =0x67,\
 VK_NUMPAD8 =0x68,\
 VK_NUMPAD9 =0x69,\
 VK_MULTIPLY =0x6A,\
 VK_ADD =0x6B,\
 VK_SEPARATOR =0x6C,\
 VK_SUBTRACT =0x6D,\
 VK_DECIMAL =0x6E,\
 VK_DIVIDE =0x6F,\
 VK_F1 =0x70,\
 VK_F2 =0x71,\
 VK_F3 =0x72,\
 VK_F4 =0x73,\
 VK_F5 =0x74,\
 VK_F6 =0x75,\
 VK_F7 =0x76,\
 VK_F8 =0x77,\
 VK_F9 =0x78,\
 VK_F10 =0x79,\
 VK_F11 =0x7A,\
 VK_F12 =0x7B,\
 VK_F13 =0x7C,\
 VK_F14 =0x7D,\
 VK_F15 =0x7E,\
 VK_F16 =0x7F,\
 VK_F17 =0x80,\
 VK_F18 =0x81,\
 VK_F19 =0x82,\
 VK_F20 =0x83,\
 VK_F21 =0x84,\
 VK_F22 =0x85,\
 VK_F23 =0x86,\
 VK_F24 =0x87,\
 VK_NUMLOCK =0x90,\
 VK_SCROLL =0x91,\
 VK_LSHIFT =0xA0,\
 VK_RSHIFT =0xA1,\
 VK_LCONTROL =0xA2,\
 VK_RCONTROL =0xA3,\
 VK_LMENU =0xA4,\
 VK_RMENU =0xA5,\
/* VK_PADXXXX are KIRIKIRI specific */\
 VK_PADLEFT =0x1B5,\
 VK_PADUP =0x1B6,\
 VK_PADRIGHT =0x1B7,\
 VK_PADDOWN =0x1B8,\
 VK_PAD1 =0x1C0,\
 VK_PAD2 =0x1C1,\
 VK_PAD3 =0x1C2,\
 VK_PAD4 =0x1C3,\
 VK_PAD5 =0x1C4,\
 VK_PAD6 =0x1C5,\
 VK_PAD7 =0x1C6,\
 VK_PAD8 =0x1C7,\
 VK_PAD9 =0x1C8,\
 VK_PAD10 =0x1C9,\
 VK_PADANY = 0x1DF,\
 VK_PROCESSKEY =0xE5,\
 VK_ATTN =0xF6,\
 VK_CRSEL =0xF7,\
 VK_EXSEL =0xF8,\
 VK_EREOF =0xF9,\
 VK_PLAY =0xFA,\
 VK_ZOOM =0xFB,\
 VK_NONAME =0xFC,\
 VK_PA1 =0xFD,\
 VK_OEM_CLEAR =0xFE,\
 frFreeType=0,\
 frGDI=1,\
/* graphic cache system */\
 gcsAuto=-1,\
/* image 'mode' tag (mainly is generated by image format converter) constants */\
 imageTagLayerType = %[\
opaque		:%[type:ltOpaque			],\
rect		:%[type:ltOpaque			],\
alpha		:%[type:ltAlpha				],\
transparent	:%[type:ltAlpha				],\
addalpha	:%[type:ltAddAlpha			],\
add			:%[type:ltAdditive			],\
sub			:%[type:ltSubtractive		],\
mul			:%[type:ltMultiplicative	],\
dodge		:%[type:ltDodge				],\
darken		:%[type:ltDarken			],\
lighten		:%[type:ltLighten			],\
screen		:%[type:ltScreen			],\
psnormal	:%[type:ltPsNormal			],\
psadd		:%[type:ltPsAdditive		],\
pssub		:%[type:ltPsSubtractive		],\
psmul		:%[type:ltPsMultiplicative	],\
psscreen	:%[type:ltPsScreen			],\
psoverlay	:%[type:ltPsOverlay			],\
pshlight	:%[type:ltPsHardLight		],\
psslight	:%[type:ltPsSoftLight		],\
psdodge		:%[type:ltPsColorDodge		],\
psdodge5	:%[type:ltPsColorDodge5		],\
psburn		:%[type:ltPsColorBurn		],\
pslighten	:%[type:ltPsLighten			],\
psdarken	:%[type:ltPsDarken			],\
psdiff		:%[type:ltPsDifference		],\
psdiff5		:%[type:ltPsDifference5		],\
psexcl		:%[type:ltPsExclusion		],\
],\
/* draw thread num */\
 dtnAuto=0\
;";
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// local variables
//---------------------------------------------------------------------------
static tTJS* TVPScriptEngine = NULL;
static ttstr TVPStartupScriptName(TJS_N("startup.tjs"));
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// Garbage Collection stuff
//---------------------------------------------------------------------------
class tTVPTJSGCCallback : public tTVPCompactEventCallbackIntf
{
    void OnCompact(tjs_int level)
    {
        // OnCompact method from tTVPCompactEventCallbackIntf
        // called when the application is idle, deactivated, minimized, or etc...
        if (TVPScriptEngine)
        {
            if (level >= TVP_COMPACT_LEVEL_IDLE)
            {
                TVPScriptEngine->DoGarbageCollection();
            }
        }
    }
} static TVPTJSGCCallback;
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPInitScriptEngine
//---------------------------------------------------------------------------
void TVPInitScriptEngine()
{
    tTJSVariant val;

    // Set eval expression mode
    if (TVPGetCommandLine(TJS_N("-evalcontext"), &val))
    {
        ttstr str(val);
        if (str == TJS_N("global"))
        {
            TJSEvalOperatorIsOnGlobal = true;
            TJSWarnOnNonGlobalEvalOperator = true;
        }
    }

    // Set igonre-prop compat mode
    if (TVPGetCommandLine(TJS_N("-unaryaster"), &val))
    {
        ttstr str(val);
        if (str == TJS_N("compat"))
        {
            TJSUnaryAsteriskIgnoresPropAccess = true;
        }
    }

    // Set debug mode
    if (TVPGetCommandLine(TJS_N("-debug"), &val))
    {
        ttstr str(val);
        if (str == TJS_N("yes"))
        {
            TJSEnableDebugMode = true;
            TVPAddImportantLog((const tjs_char*)TVPWarnDebugOptionEnabled);
            //			if(TVPGetCommandLine(TJS_N("-warnrundelobj"), &val) )
            //			{
            //				str = val;
            //				if(str == TJS_N("yes"))
            //				{
            TJSWarnOnExecutionOnDeletingObject = true;
            //				}
            //			}
        }
    }
    // Set startup script name
    if (TVPGetCommandLine(TJS_N("-startup"), &val))
    {
        ttstr str(val);
        TVPStartupScriptName = str;
    }

    // create script engine object
    TVPScriptEngine = new tTJS();

    // add kirikiriz
    // TVPScriptEngine->SetPPValue( TJS_N("kirikiriz"), 1);

    // set TJSGetRandomBits128
    TJSGetRandomBits128 = TVPGetRandomBits128;

    // script system initialization
    TVPScriptEngine->ExecScript(ttstr(TVPInitTJSScript));

    // set console output gateway handler
    TVPScriptEngine->SetConsoleOutput(TVPGetTJS2ConsoleOutputGateway());

    // set text stream functions
    TJSCreateTextStreamForRead = TVPCreateTextStreamForRead;
    TJSCreateTextStreamForWrite = TVPCreateTextStreamForWrite;

    // set binary stream functions
    TJSCreateBinaryStreamForRead = TVPCreateBinaryStreamForRead;
    TJSCreateBinaryStreamForWrite = TVPCreateBinaryStreamForWrite;

    // register some TVP classes/objects/functions/propeties
    iTJSDispatch2* dsp;
    iTJSDispatch2* global = TVPScriptEngine->GetGlobalNoAddRef();

#define REGISTER_OBJECT(classname, instance) \
    dsp = (instance); \
    val = tTJSVariant(dsp /*, dsp*/); \
    dsp->Release(); \
    global->PropSet(TJS_MEMBERENSURE | TJS_IGNOREPROP, TJS_N(#classname), NULL, &val, global);

    /* classes */
    REGISTER_OBJECT(Debug, TVPCreateNativeClass_Debug());
    REGISTER_OBJECT(Font, TVPCreateNativeClass_Font());
    REGISTER_OBJECT(Layer, TVPCreateNativeClass_Layer());
    REGISTER_OBJECT(CDDASoundBuffer, TVPCreateNativeClass_CDDASoundBuffer());
    REGISTER_OBJECT(MIDISoundBuffer, TVPCreateNativeClass_MIDISoundBuffer());
    REGISTER_OBJECT(Timer, TVPCreateNativeClass_Timer());
    REGISTER_OBJECT(AsyncTrigger, TVPCreateNativeClass_AsyncTrigger());
    REGISTER_OBJECT(System, TVPCreateNativeClass_System());
    REGISTER_OBJECT(Storages, TVPCreateNativeClass_Storages());
    REGISTER_OBJECT(Plugins, TVPCreateNativeClass_Plugins());
    REGISTER_OBJECT(VideoOverlay, TVPCreateNativeClass_VideoOverlay());
    REGISTER_OBJECT(Pad, TVPCreateNativeClass_Pad());
    REGISTER_OBJECT(Clipboard, TVPCreateNativeClass_Clipboard());
    REGISTER_OBJECT(Scripts, TVPCreateNativeClass_Scripts()); // declared in this file
    REGISTER_OBJECT(Rect, TVPCreateNativeClass_Rect());
    REGISTER_OBJECT(Bitmap, TVPCreateNativeClass_Bitmap());
    REGISTER_OBJECT(ImageFunction, TVPCreateNativeClass_ImageFunction());
    REGISTER_OBJECT(BitmapLayerTreeOwner, TVPCreateNativeClass_BitmapLayerTreeOwner());

    /* KAG special support */
    REGISTER_OBJECT(KAGParser, TVPCreateNativeClass_KAGParser());

    /* WaveSoundBuffer and its filters */
    iTJSDispatch2* waveclass = NULL;
    REGISTER_OBJECT(WaveSoundBuffer, (waveclass = TVPCreateNativeClass_WaveSoundBuffer()));
    dsp = new tTJSNC_PhaseVocoder();
    val = tTJSVariant(dsp);
    dsp->Release();
    waveclass->PropSet(TJS_MEMBERENSURE | TJS_IGNOREPROP | TJS_STATICMEMBER, TJS_N("PhaseVocoder"),
                       NULL, &val, waveclass);

    /* Window and its drawdevices */
    iTJSDispatch2* windowclass = NULL;
    REGISTER_OBJECT(Window, (windowclass = TVPCreateNativeClass_Window()));
    dsp = new tTJSNC_BasicDrawDevice();
    val = tTJSVariant(dsp);
    dsp->Release();
    windowclass->PropSet(TJS_MEMBERENSURE | TJS_IGNOREPROP | TJS_STATICMEMBER,
                         TJS_N("BasicDrawDevice"), NULL, &val, windowclass);

    windowclass->PropSet(TJS_MEMBERENSURE | TJS_IGNOREPROP | TJS_STATICMEMBER,
                         TJS_N("PassThroughDrawDevice"), NULL, &val,
                         windowclass);                          // compatible for old version kr2
    REGISTER_OBJECT(MenuItem, TVPCreateNativeClass_MenuItem()); // register "menu" to windowclass

    // Add Extension Classes
    TVPCauseAtInstallExtensionClass(global);

    // Garbage Collection Hook
    TVPAddCompactEventHook(&TVPTJSGCCallback);
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPUninitScriptEngine
//---------------------------------------------------------------------------
void TVPUninitScriptEngine()
{
    TVPRemoveCompactEventHook(&TVPTJSGCCallback);
    
    TVPScriptEngine->DoGarbageCollection();
    delete TVPScriptEngine;
    // 回收不了的object以后想办法
    /*
            Objects, theirs lives are contolled by reference counter, may not be all
            freed here in some occations.
    */
    TVPScriptEngine = NULL;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPRestartScriptEngine
//---------------------------------------------------------------------------
void TVPRestartScriptEngine()
{
    TVPUninitScriptEngine();
    TVPInitScriptEngine();
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPGetScriptEngine
//---------------------------------------------------------------------------
tTJS* TVPGetScriptEngine()
{
    return TVPScriptEngine;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPGetScriptDispatch
//---------------------------------------------------------------------------
iTJSDispatch2* TVPGetScriptDispatch()
{
    if (TVPScriptEngine)
        return TVPScriptEngine->GetGlobal();
    else
        return NULL;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPExecuteScript
//---------------------------------------------------------------------------
void TVPExecuteScript(const ttstr& content, tTJSVariant* result)
{
    if (TVPScriptEngine)
        TVPScriptEngine->ExecScript(content, result);
    else
        TVPThrowInternalError;
}
//---------------------------------------------------------------------------
void TVPExecuteScript(const ttstr& content, const ttstr& name, tjs_int lineofs, tTJSVariant* result)
{
    if (TVPScriptEngine)
        TVPScriptEngine->ExecScript(content, result, NULL, &name, lineofs);
    else
        TVPThrowInternalError;
}
//---------------------------------------------------------------------------
void TVPExecuteScript(const ttstr& content, iTJSDispatch2* context, tTJSVariant* result)
{
    if (TVPScriptEngine)
        TVPScriptEngine->ExecScript(content, result, context);
    else
        TVPThrowInternalError;
}
//---------------------------------------------------------------------------
void TVPExecuteScript(const ttstr& content,
                      const ttstr& name,
                      tjs_int lineofs,
                      iTJSDispatch2* context,
                      tTJSVariant* result)
{
    if (TVPScriptEngine)
        TVPScriptEngine->ExecScript(content, result, context, &name, lineofs);
    else
        TVPThrowInternalError;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPExecuteExpression
//---------------------------------------------------------------------------
void TVPExecuteExpression(const ttstr& content, tTJSVariant* result)
{
    TVPExecuteExpression(content, NULL, result);
}
//---------------------------------------------------------------------------
void TVPExecuteExpression(const ttstr& content,
                          const ttstr& name,
                          tjs_int lineofs,
                          tTJSVariant* result)
{
    TVPExecuteExpression(content, name, lineofs, NULL, result);
}
//---------------------------------------------------------------------------
void TVPExecuteExpression(const ttstr& content, iTJSDispatch2* context, tTJSVariant* result)
{
    if (TVPScriptEngine)
    {
        iTJSConsoleOutput* output = TVPScriptEngine->GetConsoleOutput();
        TVPScriptEngine->SetConsoleOutput(NULL); // once set TJS console to null
        try
        {
            TVPScriptEngine->EvalExpression(content, result, context);
        }
        catch (...)
        {
            TVPScriptEngine->SetConsoleOutput(output);
            throw;
        }
        TVPScriptEngine->SetConsoleOutput(output);
    }
    else
    {
        TVPThrowInternalError;
    }
}
//---------------------------------------------------------------------------
void TVPExecuteExpression(const ttstr& content,
                          const ttstr& name,
                          tjs_int lineofs,
                          iTJSDispatch2* context,
                          tTJSVariant* result)
{
    if (TVPScriptEngine)
    {
        iTJSConsoleOutput* output = TVPScriptEngine->GetConsoleOutput();
        TVPScriptEngine->SetConsoleOutput(NULL); // once set TJS console to null
        try
        {
            TVPScriptEngine->EvalExpression(content, result, context, &name, lineofs);
        }
        catch (...)
        {
            TVPScriptEngine->SetConsoleOutput(output);
            throw;
        }
        TVPScriptEngine->SetConsoleOutput(output);
    }
    else
    {
        TVPThrowInternalError;
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPExecuteBytecode
//---------------------------------------------------------------------------
void TVPExecuteBytecode(const tjs_uint8* content,
                        size_t len,
                        iTJSDispatch2* context,
                        tTJSVariant* result,
                        const tjs_char* name)
{
    if (!TVPScriptEngine)
        TVPThrowInternalError;

    TVPScriptEngine->LoadByteCode(content, len, result, context, name);
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
void TVPExecuteStorage(const ttstr& name,
                       tTJSVariant* result,
                       bool isexpression,
                       const tjs_char* modestr)
{
    TVPExecuteStorage(name, NULL, result, isexpression, modestr);
}
//---------------------------------------------------------------------------
void TVPExecuteStorage(const ttstr& name,
                       iTJSDispatch2* context,
                       tTJSVariant* result,
                       bool isexpression,
                       const tjs_char* modestr)
{
    // execute storage which contains script
    if (!TVPScriptEngine)
        TVPThrowInternalError;

    { // for bytecode
        ttstr place(TVPSearchPlacedPath(name));
        ttstr shortname(TVPExtractStorageName(place));
        tTJSBinaryStream* stream = TVPCreateBinaryStreamForRead(place, modestr);
        if (stream)
        {
            bool isbytecode = false;
            try
            {
                isbytecode =
                    TVPScriptEngine->LoadByteCode(stream, result, context, shortname.c_str());
            }
            catch (...)
            {
                delete stream;
                throw;
            }
            delete stream;
            if (isbytecode)
                return;
        }
    }

    ttstr place(TVPSearchPlacedPath(name));
    ttstr shortname(TVPExtractStorageName(place));

    iTJSTextReadStream* stream = TVPCreateTextStreamForRead(place, modestr);
    ttstr buffer;
    try
    {
        stream->Read(buffer, 0);
    }
    catch (...)
    {
        stream->Destruct();
        throw;
    }
    stream->Destruct();

    if (TVPScriptEngine)
    {
        if (!isexpression)
            TVPScriptEngine->ExecScript(buffer, result, context, &shortname);
        else
            TVPScriptEngine->EvalExpression(buffer, result, context, &shortname);
    }
}

void TVPExecuteBinaryStream(tTJSBinaryStream* stream,
                            const ttstr& fileName,
                            tTJSVariant* result,
                            bool isexpression,
                            const tjs_char* modestr)
{
    // execute storage which contains script
    if (!TVPScriptEngine)
        TVPThrowInternalError;

    { // for bytecode
        if (stream)
        {
            bool isbytecode = false;
            try
            {
                isbytecode = TVPScriptEngine->LoadByteCode(stream, result, NULL, fileName.c_str());
            }
            catch (...)
            {
                delete stream;
                throw;
            }
            if (isbytecode)
            {
                delete stream;
                return;
            }
        }
    }

    iTJSTextReadStream* rstream = TVPCreateTextStreamForRead(stream, modestr);
    ttstr buffer;
    try
    {
        rstream->Read(buffer, 0);
    }
    catch (...)
    {
        rstream->Destruct();
        throw;
    }
    rstream->Destruct();

    if (TVPScriptEngine)
    {
        if (!isexpression)
            TVPScriptEngine->ExecScript(buffer, result, NULL, &fileName);
        else
            TVPScriptEngine->EvalExpression(buffer, result, NULL, &fileName);
    }
}
//---------------------------------------------------------------------------
void TVPCompileStorage(const ttstr& name,
                       bool isrequestresult,
                       bool outputdebug,
                       bool isexpression,
                       const ttstr& outputpath)
{
    // execute storage which contains script
    if (!TVPScriptEngine)
        TVPThrowInternalError;

    ttstr place(TVPSearchPlacedPath(name));
    ttstr shortname(TVPExtractStorageName(place));
    iTJSTextReadStream* stream = TVPCreateTextStreamForRead(place, TJS_N(""));

    ttstr buffer;
    try
    {
        stream->Read(buffer, 0);
    }
    catch (...)
    {
        stream->Destruct();
        throw;
    }
    stream->Destruct();

    tTJSBinaryStream* outputstream = TVPCreateStream(outputpath, TJS_BS_WRITE);
    if (TVPScriptEngine)
    {
        try
        {
            TVPScriptEngine->CompileScript(buffer.c_str(), outputstream, isrequestresult,
                                           outputdebug, isexpression, name.c_str(), 0);
        }
        catch (...)
        {
            delete outputstream;
            throw;
        }
    }
    delete outputstream;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPCreateMessageMapFile
//---------------------------------------------------------------------------
void TVPCreateMessageMapFile(const ttstr& filename)
{
    ttstr script(TJS_N("{\n\tvar r = System.assignMessage;\n"));

    script += TJSCreateMessageMapString();

    script += TJS_N("}");

    iTJSTextWriteStream* stream = TVPCreateTextStreamForWrite(filename, TJS_N(""));
    try
    {
        stream->Write(script);
    }
    catch (...)
    {
        stream->Destruct();
        throw;
    }

    stream->Destruct();
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPDumpScriptEngine
//---------------------------------------------------------------------------
void TVPDumpScriptEngine()
{
    TVPTJS2StartDump();
    TVPScriptEngine->SetConsoleOutput(TVPGetTJS2DumpOutputGateway());
    try
    {
        TVPScriptEngine->Dump();
    }
    catch (...)
    {
        TVPTJS2EndDump();
        TVPScriptEngine->SetConsoleOutput(TVPGetTJS2ConsoleOutputGateway());
        throw;
    }
    TVPScriptEngine->SetConsoleOutput(TVPGetTJS2ConsoleOutputGateway());
    TVPTJS2EndDump();
}
//---------------------------------------------------------------------------

bool TVPStartupSuccess = false;
//---------------------------------------------------------------------------
// TVPExecuteStartupScript
//---------------------------------------------------------------------------
void TVPExecuteStartupScript()
{
    // execute "patch.tjs"
    ttstr strPatchError;
    try
    {
        ttstr patch = TVPProjectDir + "patch.tjs";
        TVPAddLog(TJS_N("(info) Loading patch script : ") + patch);
        if (TVPIsExistentStorageNoSearch(patch))
            TVPExecuteStorage(patch);
    }
    catch (const TJS::eTJSScriptError& e)
    {
        ttstr& msg = strPatchError;
        msg += e.GetMessage();
        const tjs_char* pszBlockName = e.GetBlockName();
        if (pszBlockName && *pszBlockName)
        {
            msg += TJS_N("\n@line(");
            tjs_char tmp[34];
            msg += TJS_int_to_str(e.GetSourceLine(), tmp);
            msg += TJS_N(") ");
            msg += pszBlockName;
        }
        msg += TJS_N("\n");
        msg += e.GetTrace();
    }
    catch (const TJS::eTJS& e)
    {
        if (!TVPSystemUninitCalled)
            strPatchError = e.GetMessage();
    }
    catch (const std::exception& e)
    {
        strPatchError = e.what();
    }
    catch (const tjs_char* e)
    {
        strPatchError = e;
    }

    if (!strPatchError.IsEmpty())
    {
        ttstr msg = "startup_patch_fail\n";
        msg += strPatchError;
        std::vector<ttstr> btns;
        btns.emplace_back("忽略");
        btns.emplace_back("浏览补丁");
        if (TVPShowSimpleMessageBox(msg, TVPGetPackageVersionString(), btns) == 1)
        {
            TVPOpenPatchLibUrl();
        }
    }

    // execute "startup.tjs"
    try
    {
        ttstr place(TVPSearchPlacedPath(TVPStartupScriptName));
        TVPAddLog(TJS_N("(info) Loading startup script : ") + place);
        TVPStartupSuccess = false;
        try
        {
            iTJSTextReadStream* stream = TVPCreateTextStreamForRead(place, "");
            stream->Destruct();
            TVPExecuteStorage(TVPStartupScriptName);
            TVPStartupSuccess = true;
        }
        catch (...)
        {
            if (!TVPIsExistentStorage(TJS_N("System/Initialize.tjs")))
            {
                throw;
            }
        }
        if (TVPStartupSuccess)
        {
        }
        else
        {
            // try direct execute initialize.tjs to compatible for some patch
            TVPExecuteStorage(TJS_N("System/Initialize.tjs"));
            TVPStartupSuccess = true;
        }
        TVPAddLog(TJS_N("(info) Startup script ended."));
        try
        {
            ttstr patch = TVPProjectDir + "AfterStartup.tjs";
            if (TVPIsExistentStorageNoSearch(patch))
                TVPExecuteStorage(patch);
        }
        catch (...)
        {
        }
    }
    TJS_CONVERT_TO_TJS_EXCEPTION
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// unhandled exception handler related
//---------------------------------------------------------------------------
static bool TJSGetSystem_exceptionHandler_Object(tTJSVariantClosure& dest)
{
    // get System.exceptionHandler
    iTJSDispatch2* global = TVPGetScriptEngine()->GetGlobalNoAddRef();
    if (!global)
        return false;

    tTJSVariant val;
    tTJSVariant val2;
    tTJSVariantClosure clo;

    tjs_error er;
    er = global->PropGet(TJS_MEMBERMUSTEXIST, TJS_N("System"), NULL, &val, global);
    if (TJS_FAILED(er))
        return false;

    if (val.Type() != tvtObject)
        return false;

    clo = val.AsObjectClosureNoAddRef();

    if (clo.Object == NULL)
        return false;

    clo.PropGet(TJS_MEMBERMUSTEXIST, TJS_N("exceptionHandler"), NULL, &val2, NULL);

    if (val2.Type() != tvtObject)
        return false;

    dest = val2.AsObjectClosure();

    if (!dest.Object)
    {
        dest.Release();
        return false;
    }

    return true;
}
//---------------------------------------------------------------------------
bool TVPProcessUnhandledException(eTJSScriptException& e)
{
    bool result;
    tTJSVariantClosure clo;
    clo.Object = clo.ObjThis = NULL;

    try
    {
        // get the script engine
        tTJS* engine = TVPGetScriptEngine();
        if (!engine)
            return false; // the script engine had been shutdown

        // get System.exceptionHandler
        if (!TJSGetSystem_exceptionHandler_Object(clo))
            return false; // System.exceptionHandler cannot be retrieved

        // execute clo
        tTJSVariant obj(e.GetValue());

        tTJSVariant* pval[] = {&obj};

        tTJSVariant res;

        clo.FuncCall(0, NULL, NULL, &res, 1, pval, NULL);

        result = res.operator bool();
    }
    catch (eTJSScriptError& e)
    {
        clo.Release();
        TVPShowScriptException(e);
    }
    catch (eTJS& e)
    {
        clo.Release();
        TVPShowScriptException(e);
    }
    catch (...)
    {
        clo.Release();
        throw;
    }
    clo.Release();

    return result;
}
//---------------------------------------------------------------------------
bool TVPProcessUnhandledException(eTJSScriptError& e)
{
    bool result;
    tTJSVariantClosure clo;
    clo.Object = clo.ObjThis = NULL;

    try
    {
        // get the script engine
        tTJS* engine = TVPGetScriptEngine();
        if (!engine)
            return false; // the script engine had been shutdown

        // get System.exceptionHandler
        if (!TJSGetSystem_exceptionHandler_Object(clo))
            return false; // System.exceptionHandler cannot be retrieved

        // execute clo
        tTJSVariant obj;
        tTJSVariant msg(e.GetMessage());
        tTJSVariant trace(e.GetTrace());
        TJSGetExceptionObject(engine, &obj, msg, &trace);

        tTJSVariant* pval[] = {&obj};

        tTJSVariant res;

        clo.FuncCall(0, NULL, NULL, &res, 1, pval, NULL);

        result = res.operator bool();
    }
    catch (eTJSScriptError& e)
    {
        clo.Release();
        TVPShowScriptException(e);
    }
    catch (eTJS& e)
    {
        clo.Release();
        TVPShowScriptException(e);
    }
    catch (...)
    {
        clo.Release();
        throw;
    }
    clo.Release();

    return result;
}
//---------------------------------------------------------------------------
bool TVPProcessUnhandledException(eTJS& e)
{
    bool result;
    tTJSVariantClosure clo;
    clo.Object = clo.ObjThis = NULL;

    try
    {
        // get the script engine
        tTJS* engine = TVPGetScriptEngine();
        if (!engine)
            return false; // the script engine had been shutdown

        // get System.exceptionHandler
        if (!TJSGetSystem_exceptionHandler_Object(clo))
            return false; // System.exceptionHandler cannot be retrieved

        // execute clo
        tTJSVariant obj;
        tTJSVariant msg(e.GetMessage());
        TJSGetExceptionObject(engine, &obj, msg);

        tTJSVariant* pval[] = {&obj};

        tTJSVariant res;

        clo.FuncCall(0, NULL, NULL, &res, 1, pval, NULL);

        result = res.operator bool();
    }
    catch (eTJSScriptError& e)
    {
        clo.Release();
        TVPShowScriptException(e);
    }
    catch (eTJS& e)
    {
        clo.Release();
        TVPShowScriptException(e);
    }
    catch (...)
    {
        clo.Release();
        throw;
    }
    clo.Release();

    return result;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
void TVPStartObjectHashMap()
{
    // addref ObjectHashMap if the program is being debugged.
    if (TJSEnableDebugMode)
        TJSAddRefObjectHashMap();
}

//---------------------------------------------------------------------------
// TVPBeforeProcessUnhandledException
//---------------------------------------------------------------------------
void TVPBeforeProcessUnhandledException()
{
    
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPShowScriptException
//---------------------------------------------------------------------------
/*
        These functions display the error location, reason, etc.
        And disable the script event dispatching to avoid massive occurrence of
        errors.
*/
extern ttstr TVPGetErrorDialogTitle();
//---------------------------------------------------------------------------
void TVPShowScriptException(eTJS& e)
{
    TVPSetSystemEventDisabledState(true);
    TVPOnError();

    if (!TVPSystemUninitCalled)
    {
        ttstr errstr = (ttstr(TVPScriptExceptionRaised) + TJS_N("\n") + e.GetMessage());
        TVPAddLog(ttstr(TVPScriptExceptionRaised) + TJS_N("\n") + e.GetMessage());
        TVPShowSimpleMessageBox(errstr, TVPGetErrorDialogTitle());
        // Application->MessageDlg( errstr.AsStdString(), std::wstring(), mtError, mbOK );
        TVPTerminateSync(1);
    }
}
//---------------------------------------------------------------------------
void TVPShowScriptException(eTJSScriptError& e)
{
    TVPSetSystemEventDisabledState(true);
    TVPOnError();

    if (!TVPSystemUninitCalled)
    {
        ttstr errstr = (ttstr(TVPScriptExceptionRaised) + TJS_N("\n") + e.GetMessage());
        TVPAddLog(ttstr(TVPScriptExceptionRaised) + TJS_N("\n") + e.GetMessage());
        if (e.GetTrace().GetLen() != 0)
            TVPAddLog(ttstr(TJS_N("trace : ")) + e.GetTrace());
        TVPShowSimpleMessageBox(errstr, TVPGetErrorDialogTitle());
        //	Application->MessageDlg( errstr.AsStdString(), Application->GetTitle(), mtStop, mbOK
        //);

#ifdef TVP_ENABLE_EXECUTE_AT_EXCEPTION
        const tjs_char* scriptName = e.GetBlockNoAddRef()->GetName();
        if (scriptName != NULL && scriptName[0] != 0)
        {
            ttstr path(scriptName);
            try
            {
                ttstr newpath = TVPGetPlacedPath(path);
                if (newpath.IsEmpty())
                {
                    path = TVPNormalizeStorageName(path);
                }
                else
                {
                    path = newpath;
                }
                TVPGetLocalName(path);
                std::wstring scriptPath(path.AsStdString());
                tjs_int lineno = 1 + e.GetBlockNoAddRef()->SrcPosToLine(e.GetPosition()) -
                                 e.GetBlockNoAddRef()->GetLineOffset();

#if defined(WIN32) && defined(_DEBUG) && !defined(ENABLE_DEBUGGER)
                // 僨僶僢僈幚峴偝傟偰偄傞帪丄Visual Studio
                // 偱峴僕儍儞僾偡傞帪偺巜掕傪僨僶僢僌弌椡偵弌偟偰丄break 偱掆巭偡傞
                if (::IsDebuggerPresent())
                {
                    std::wstring debuglile(std::wstring(L"2>") + path.AsStdString() + L"(" +
                                           std::to_wstring(lineno) + L"): error :" +
                                           errstr.AsStdString());
                    ::OutputDebugString(debuglile.c_str());
                    // 偙偙偱
                    // break偱掆巭偟偨帪丄捈慜偺弌椡峴傪僟僽儖僋儕僢僋偡傟偽丄椺奜売強偺僗僋儕僾僩傪Visual
                    // Studio偱奐偗傞
                    ::DebugBreak();
                }
#endif
                scriptPath = std::wstring(L"\"") + scriptPath + std::wstring(L"\"");
                tTJSVariant val;
                if (TVPGetCommandLine(TJS_N("-exceptionexe"), &val))
                {
                    ttstr exepath(val);
                    // exepath = ttstr(TJS_N("\"")) + exepath + ttstr(TJS_N("\""));
                    if (TVPGetCommandLine(TJS_N("-exceptionarg"), &val))
                    {
                        ttstr arg(val);
                        if (!exepath.IsEmpty() && !arg.IsEmpty())
                        {
                            std::wstring str(arg.AsStdString());
                            str = ApplicationSpecialPath::ReplaceStringAll(
                                str, std::wstring(L"%filepath%"), scriptPath);
                            str = ApplicationSpecialPath::ReplaceStringAll(
                                str, std::wstring(L"%line%"), std::to_wstring(lineno));
                            // exepath = exepath + ttstr(str);
                            //_wsystem( exepath.c_str() );
                            arg = ttstr(str);
                            TVPAddLog(ttstr(TJS_N("(execute) ")) + exepath + ttstr(TJS_N(" ")) +
                                      arg);
                            TVPShellExecute(exepath, arg);
                        }
                    }
                }
            }
            catch (...)
            {
            }
        }
#endif
        TVPTerminateSync(1);
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPInitializeStartupScript
//---------------------------------------------------------------------------
void TVPInitializeStartupScript()
{
    TVPStartObjectHashMap();

    TVPExecuteStartupScript();
    if (TVPTerminateOnNoWindowStartup && TVPGetWindowCount() == 0)
    {
        // no window is created and main window is invisible
        Application->Terminate();
    }
}
//---------------------------------------------------------------------------
