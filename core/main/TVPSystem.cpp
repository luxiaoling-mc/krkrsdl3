#include "tjsCommHead.h"
#include "TVPSystem.h"

#include "TVPScript.h"
#include "TVPDebug.h"
#include "TVPMsg.h"
#include "TVPStorage.h"
#include "TVPEvent.h"
#include "TVPApplication.h"
#include "TVPGraphicsLoader.h"

#include "Random.h"
#include "tvpinputdefs.h"
#include "SystemControl.h"
#include "FilePathUtil.h"
#include "Platform.h"
#include "PlatformThread.h"
#include "PlatformFile.h"
#include "XP3Archive.h"
#include "WindowManager.h"
#include "TVPSettings.h"
#include "WaveDecodeThread.h"

#include "tjsLex.h"
#include "tjsNativeLayer.h"

//---------------------------------------------------------------------------
// TVPFireOnApplicationActivateEvent
//---------------------------------------------------------------------------
void TVPFireOnApplicationActivateEvent(bool activate_or_deactivate)
{
    // get the script engine
    tTJS* engine = TVPGetScriptEngine();
    if (!engine)
        return; // the script engine had been shutdown

    // get System.onActivate or System.onDeactivate
    // and call it.
    iTJSDispatch2* global = TVPGetScriptEngine()->GetGlobalNoAddRef();
    if (!global)
        return;

    tTJSVariant val;
    tTJSVariant val2;
    tTJSVariantClosure clo;
    tTJSVariantClosure func;

    try
    {
        tjs_error er;
        er = global->PropGet(TJS_MEMBERMUSTEXIST, TJS_N("System"), NULL, &val, global);
        if (TJS_FAILED(er))
            return;

        if (val.Type() != tvtObject)
            return;

        clo = val.AsObjectClosureNoAddRef();

        if (clo.Object == NULL)
            return;

        clo.PropGet(TJS_MEMBERMUSTEXIST,
                    activate_or_deactivate ? TJS_N("onActivate") : TJS_N("onDeactivate"), NULL,
                    &val2, NULL);

        if (val2.Type() != tvtObject)
            return;

        func = val2.AsObjectClosureNoAddRef();
    }
    catch (const eTJS& e)
    {
        // the system should not throw exceptions during retrieving the function
        TVPAddLog(
            TVPFormatMessage(TVPErrorInRetrievingSystemOnActivateOnDeactivate, e.GetMessage()));
        return;
    }

    if (func.Object != NULL)
        func.FuncCall(0, NULL, NULL, NULL, 0, NULL, NULL);
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPGetAsyncKeyState
//---------------------------------------------------------------------------
bool TVPGetAsyncKeyState(tjs_uint keycode, bool getcurrent)
{
    // get keyboard state asynchronously.
    // return current key state if getcurrent is true.
    // otherwise, return whether the key is pushed during previous call of
    // TVPGetAsyncKeyState at the same keycode.

    if (keycode >= VK_PAD_FIRST && keycode <= VK_PAD_LAST)
    {
        // JoyPad related keys are treated in DInputMgn.cpp
        return TVPGetJoyPadAsyncState(keycode, getcurrent);
    }

    return TVPGetKeyMouseAsyncState(keycode, getcurrent);
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPGetOSBits
//---------------------------------------------------------------------------
tjs_int TVPGetOSBits()
{
    return 64;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPShellExecute
//---------------------------------------------------------------------------
bool TVPShellExecute(const ttstr& target, const ttstr& param)
{
    // open or execute target file
    //	ttstr file = TVPGetNativeName(TVPNormalizeStorageName(target));
    return true;
}
//---------------------------------------------------------------------------

static tTJSVariant RegisterData;
void TVPExecuteStorage(const ttstr& name,
                       tTJSVariant* result,
                       bool isexpression,
                       const tjs_char* modestr);
static void InitRegisterData()
{
    static bool dataInited = false;
    if (!dataInited)
    {
        ttstr regfile = TVPProjectDir + TJS_N("RegisterData.tjs");
        if (TVPIsExistentStorageNoSearch(regfile))
        {
            TVPExecuteStorage(regfile, &RegisterData, true, TJS_N(""));
        }
    }
}

//---------------------------------------------------------------------------
// TVPReadRegValue
//---------------------------------------------------------------------------
void TVPReadRegValue(tTJSVariant& result, const ttstr& key)
{
    // open specified registry key
    if (key.IsEmpty())
    {
        result.Clear();
        return;
    }

    // check whether the key contains root key name
    // HKEY root = HKEY_CURRENT_USER;
    const tjs_char* key_p = key.c_str();

    InitRegisterData();
    // search value name
    tTJSVariant CurrentNode = RegisterData;
    const tjs_char* start = key_p;
    while (*start && CurrentNode.Type() != tvtObject)
    {
        iTJSDispatch2* pObj;

        switch (*key_p)
        {
            case '\\':
            case '/':
                ++key_p;
            case '\0':
                start = key_p;
                if (CurrentNode.Type() != tvtObject)
                {
                    CurrentNode.Clear();
                    break;
                }
                pObj = CurrentNode.AsObject();
                if (!pObj)
                {
                    CurrentNode.Clear();
                    break;
                }
                if (!TJS_SUCCEEDED(pObj->PropGet(TJS_MEMBERMUSTEXIST,
                                                 ttstr(start, key_p - start - 1).c_str(), 0,
                                                 &CurrentNode, pObj)))
                {
                    CurrentNode.Clear();
                    break;
                }
                start = key_p;
                continue;
            default:
                ++key_p;
                continue;
        }
    }
    if (*start)
    {
        CurrentNode.Clear();
        return;
    }
    result = CurrentNode;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPCreateAppLock
//---------------------------------------------------------------------------
bool TVPCreateAppLock(const ttstr& lockname)
{

    // No need to release the mutex object because the mutex is automatically
    // released when the calling thread exits.

    return true;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
enum tTVPTouchDevice
{
    tdNone = 0,
    tdIntegratedTouch = 0x00000001,
    tdExternalTouch = 0x00000002,
    tdIntegratedPen = 0x00000004,
    tdExternalPen = 0x00000008,
    tdMultiInput = 0x00000040,
    tdDigitizerReady = 0x00000080,
    tdMouse = 0x00000100,
    tdMouseWheel = 0x00000200
};
/**
 * �^�b�`�f�o�C�X(�ƃ}�E�X)�̐ڑ���Ԃ��擾����
 **/
int TVPGetSupportTouchDevice()
{
    int result = 0;
    result |= tdMouse;
    result |= tdMouseWheel;
    return result;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// System.onActivate and System.onDeactivate related
//---------------------------------------------------------------------------
static void TVPOnApplicationActivate(bool activate_or_deactivate);
//---------------------------------------------------------------------------
class tTVPOnApplicationActivateEvent : public tTVPBaseInputEvent
{
    static tTVPUniqueTagForInputEvent Tag;
    bool ActivateOrDeactivate; // true for activate; otherwise deactivate
public:
    tTVPOnApplicationActivateEvent(bool activate_or_deactivate)
      : tTVPBaseInputEvent(Application, Tag),
        ActivateOrDeactivate(activate_or_deactivate){};
    void Deliver() const { TVPOnApplicationActivate(ActivateOrDeactivate); }
};
tTVPUniqueTagForInputEvent tTVPOnApplicationActivateEvent::Tag;
//---------------------------------------------------------------------------
void TVPPostApplicationActivateEvent()
{
    TVPPostInputEvent(new tTVPOnApplicationActivateEvent(true), TVP_EPT_REMOVE_POST);
}
//---------------------------------------------------------------------------
void TVPPostApplicationDeactivateEvent()
{
    TVPPostInputEvent(new tTVPOnApplicationActivateEvent(false), TVP_EPT_REMOVE_POST);
}
//---------------------------------------------------------------------------
static void TVPOnApplicationActivate(bool activate_or_deactivate)
{
    // called by event system, to fire System.onActivate or
    // System.onDeactivate event
    if (!TVPSystemControlAlive)
        return;

    // check the state again (because the state may change during the event delivering).
    // but note that this implementation might fire activate events even in the application
    // is already activated (the same as deactivation).
    if (!activate_or_deactivate)
        return;

    // fire the event
    TVPFireOnApplicationActivateEvent(activate_or_deactivate);
}
//---------------------------------------------------------------------------

bool TVPAutoSaveBookMark = false;
extern void TVPDoSaveSystemVariables()
{
    try
    {
        // hack for save system variable
        iTJSDispatch2* global = TVPGetScriptDispatch();
        if (!global)
            return;
        tTJSVariant var;
        if (global->PropGet(0, TJS_N("kag"), nullptr, &var, global) == TJS_S_OK &&
            var.Type() == tvtObject)
        {
            iTJSDispatch2* kag = var.AsObjectNoAddRef();
            if (kag->PropGet(0, TJS_N("saveSystemVariables"), nullptr, &var, kag) == TJS_S_OK)
            {
                iTJSDispatch2* fn = var.AsObjectNoAddRef();
                if (fn->IsInstanceOf(0, 0, 0, TJS_N("Function"), fn))
                {
                    tTJSVariant* args = nullptr;
                    fn->FuncCall(0, nullptr, nullptr, nullptr, 0, &args, kag);
                }
            }
            if (TVPAutoSaveBookMark &&
                kag->PropGet(0, TJS_N("saveBookMark"), nullptr, &var, kag) == TJS_S_OK &&
                var.Type() == tvtObject)
            {
                iTJSDispatch2* fn = var.AsObjectNoAddRef();
                if (fn->IsInstanceOf(0, 0, 0, TJS_N("Function"), fn))
                {
                    tTJSVariant num((tjs_int32)0);
                    tTJSVariant* args = &num;
                    fn->FuncCall(0, nullptr, nullptr, nullptr, 1, &args, kag);
                }
            }
        }
    }
    catch (...)
    {
        ;
    }
}

//---------------------------------------------------------------------------
// TVPSystemInit : Entire System Initialization
//---------------------------------------------------------------------------
void TVPSystemInit(void)
{
    TVPBeforeSystemInit();

    TVPInitTVPGL();

    TVPInitWaveDecodeThread();

    TVPAfterSystemInit();
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPSystemUninit : System shutdown, cleanup, etc...
//---------------------------------------------------------------------------
static void TVPCauseAtExit();
bool TVPSystemUninitCalled = false;
void TVPSystemUninit(void)
{
    if (TVPSystemUninitCalled)
        return;
    TVPSystemUninitCalled = true;

    TVPBeforeSystemUninit();

    TVPUninitTVPGL();

    TVPUninitWaveDecodeThread();

    TVPAfterSystemUninit();

    TVPCauseAtExit();

    TVPSystemUninitCalled = false;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPAddAtExitHandler related (TODO:此处的事件均为静态注册，TVPAtExitInfos应该交给系统回收)
//---------------------------------------------------------------------------
struct tTVPAtExitInfo
{
    tTVPAtExitInfo(tjs_int pri, void (*handler)()) { Priority = pri, Handler = handler; }

    tjs_int Priority;
    void (*Handler)();

    bool operator<(const tTVPAtExitInfo& r) const { return this->Priority < r.Priority; }
    bool operator>(const tTVPAtExitInfo& r) const { return this->Priority > r.Priority; }
    bool operator==(const tTVPAtExitInfo& r) const { return this->Priority == r.Priority; }
};
static std::vector<tTVPAtExitInfo>* TVPAtExitInfos = NULL;
static bool TVPAtExitShutdown = false;
//---------------------------------------------------------------------------
void TVPAddAtExitHandler(tjs_int pri, void (*handler)())
{
    if (TVPAtExitShutdown)
        return;

    if (!TVPAtExitInfos)
        TVPAtExitInfos = new std::vector<tTVPAtExitInfo>();
    TVPAtExitInfos->push_back(tTVPAtExitInfo(pri, handler));
}
//---------------------------------------------------------------------------
static void TVPCauseAtExit()
{
    if (TVPAtExitShutdown)
        return;
    TVPAtExitShutdown = true;

    std::sort(TVPAtExitInfos->begin(), TVPAtExitInfos->end()); // descending sort

    std::vector<tTVPAtExitInfo>::iterator i;
    for (i = TVPAtExitInfos->begin(); i != TVPAtExitInfos->end(); i++)
    {
        i->Handler();
    }

    TVPAtExitShutdown = false;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// global data
//---------------------------------------------------------------------------
bool TVPProjectDirSelected = false;
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// System security options
//---------------------------------------------------------------------------
// system security options are held inside the executable, where
// signature checker will refer. This enables the signature checker
// (or other security modules like XP3 encryption module) to check
// the changes which is not intended by the contents author.
const static char TVPSystemSecurityOptions[] =
    "-- TVPSystemSecurityOptions disablemsgmap(0):forcedataxp3(0):acceptfilenameargument(0) --";
//---------------------------------------------------------------------------
int GetSystemSecurityOption(const char* name)
{
    size_t namelen = TJS_nstrlen(name);
    const char* p = TJS_nstrstr(TVPSystemSecurityOptions, name);
    if (!p)
        return 0;
    if (p[namelen] == '(' && p[namelen + 2] == ')')
        return p[namelen + 1] - '0';
    return 0;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
static void TVPInitRandomGenerator()
{
    tjs_uint64 tick = TVPGetRoughTickCount();
    TVPPushEnvironNoise(&tick, sizeof(tick));
    uint64_t tid = TVPGetCurrentThreadID();
    TVPPushEnvironNoise(&tid, sizeof(tid));
    time_t curtime = time(NULL);
    TVPPushEnvironNoise(&curtime, sizeof(curtime));
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPInitializeBaseSystems
//---------------------------------------------------------------------------
void TVPInitializeBaseSystems()
{
    // set system archive delimiter
    tTJSVariant v;
    if (TVPGetCommandLine(TJS_N("-arcdelim"), &v))
        TVPArchiveDelimiter = ttstr(v)[0];

    // set default current directory
    TVPSetCurrentDirectory(IncludeTrailingBackslash(TVPProjectDir));

    // load message map file
    bool load_msgmap = GetSystemSecurityOption("disablemsgmap") == 0;

    if (load_msgmap)
    {
        const tjs_char name_msgmap[] = TJS_N("msgmap.tjs");
        if (TVPIsExistentStorage(name_msgmap))
            TVPExecuteStorage(name_msgmap, NULL, false, TJS_N(""));
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// system initializer / uninitializer
//---------------------------------------------------------------------------
static tjs_uint64 TVPTotalPhysMemory = 0;
void TVPBeforeSystemInit()
{
    // set system archive delimiter after patch.tjs specified
    tTJSVariant v;
    if (TVPGetCommandLine(TJS_N("-arcdelim"), &v))
        TVPArchiveDelimiter = ttstr(v)[0];

    if (TVPIsExistentStorageNoSearchNoNormalize(TVPProjectData))
    {
        TVPProjectData += TVPArchiveDelimiter;
    }
    TVPSetCurrentDirectory(TVPProjectData);

#ifdef TVP_REPORT_HW_EXCEPTION
    // __dee_hacked_set_getExceptionObjectHook(TVP__dee_hacked_getExceptionObjectHook);
    // register hook function for hardware exceptions
#endif

    // randomize
    TVPInitRandomGenerator();

    // memory usage
    {
        TVPMemoryInfo meminf;
        TVPGetMemoryInfo(meminf);
        TVPPushEnvironNoise(&meminf, sizeof(meminf));

        TVPTotalPhysMemory = meminf.MemTotal * 1024;
        if (TVPTotalPhysMemory > 768 * 1024 * 1024)
        {
            TVPTotalPhysMemory -= 512 * 1024 * 1024; // assume that system reserved 512M memory
        }
        else
        {
            TVPTotalPhysMemory /= 2; // use half memory in small memory devices
        }

        TVPAddImportantLog(
            TVPFormatMessage(TVPInfoTotalPhysicalMemory, tjs_int64(TVPTotalPhysMemory)));
        if (TVPTotalPhysMemory > 256 * 1024 * 1024)
        {
            std::string str = "unlimited";
            if (str == ("low"))
                TVPTotalPhysMemory = 0; // assumes zero
            else if (str == ("medium"))
                TVPTotalPhysMemory = 128 * 1024 * 1024;
            else if (str == ("high"))
                TVPTotalPhysMemory = 256 * 1024 * 1024;
        }
        else
        { // use minimum memory usage if less than 256M(512M physics)
            TVPTotalPhysMemory = 0;
        }

        if (TVPTotalPhysMemory < 128 * 1024 * 1024)
        {
            // very very low memory, forcing to assume zero memory
            TVPTotalPhysMemory = 0;
        }

        if (TVPTotalPhysMemory < 128 * 1024 * 1024)
        {
            // extra low memory
            if (TJSObjectHashBitsLimit > 0)
                TJSObjectHashBitsLimit = 0;
            TVPSegmentCacheLimit = 0;
            TVPFreeUnusedLayerCache = true; // in LayerIntf.cpp
        }
        else if (TVPTotalPhysMemory < 256 * 1024 * 1024)
        {
            // low memory
            if (TJSObjectHashBitsLimit > 4)
                TJSObjectHashBitsLimit = 4;
        }
    }
}
//---------------------------------------------------------------------------
extern bool TVPEnableGlobalHeapCompaction;
extern bool TVPAutoSaveBookMark;
static bool TVPHighTimerPeriod = false;
static uint32_t TVPTimeBeginPeriodRes = 0;
//---------------------------------------------------------------------------
void TVPAfterSystemInit()
{
    TVPAllocGraphicCacheOnHeap = false; // always false since beta 20

    // determine maximum graphic cache limit
    tTJSVariant opt;
    tjs_int64 limitmb = -1;
    if (TVPGetCommandLine(TJS_N("-gclim"), &opt))
    {
        ttstr str(opt);
        if (str == TJS_N("auto"))
            limitmb = -1;
        else
            limitmb = opt.AsInteger();
    }

    if (limitmb == -1)
    {
        if (TVPTotalPhysMemory <= 32 * 1024 * 1024)
            TVPGraphicCacheSystemLimit = 0;
        else if (TVPTotalPhysMemory <= 48 * 1024 * 1024)
            TVPGraphicCacheSystemLimit = 0;
        else if (TVPTotalPhysMemory <= 64 * 1024 * 1024)
            TVPGraphicCacheSystemLimit = 0;
        else if (TVPTotalPhysMemory <= 96 * 1024 * 1024)
            TVPGraphicCacheSystemLimit = 4;
        else if (TVPTotalPhysMemory <= 128 * 1024 * 1024)
            TVPGraphicCacheSystemLimit = 8;
        else if (TVPTotalPhysMemory <= 192 * 1024 * 1024)
            TVPGraphicCacheSystemLimit = 12;
        else if (TVPTotalPhysMemory <= 256 * 1024 * 1024)
            TVPGraphicCacheSystemLimit = 20;
        else if (TVPTotalPhysMemory <= 512 * 1024 * 1024)
            TVPGraphicCacheSystemLimit = 40;
        else
            TVPGraphicCacheSystemLimit =
                tjs_uint64(TVPTotalPhysMemory / (1024 * 1024 * 10)); // cachemem = physmem / 10
        TVPGraphicCacheSystemLimit *= 1024 * 1024;
    }
    else
    {
        TVPGraphicCacheSystemLimit = limitmb * 1024 * 1024;
    }
    // 32bit 側偺偱 512MB 傑偱偵惂尷
    if (TVPGraphicCacheSystemLimit >= 512 * 1024 * 1024)
        TVPGraphicCacheSystemLimit = 512 * 1024 * 1024;

    if (TVPTotalPhysMemory <= 64 * 1024 * 1024)
        TVPSetFontCacheForLowMem();

    //	TVPGraphicCacheSystemLimit = 1*1024*1024; // DEBUG

    if (TVPGetCommandLine(TJS_N("-autosave"), &opt))
    {
        ttstr str(opt);
        if (str == TJS_N("yes"))
        {
            TVPAutoSaveBookMark = true;
        }
    }

    // check TVPGraphicSplitOperation option
    std::string _val = "software";
    if (_val != "software")
    {
        TVPGraphicSplitOperationType = gsotNone;
    }
    else
    {
        TVPDrawThreadNum = 0;
        if (TVPGetCommandLine(TJS_N("-gsplit"), &opt))
        {
            ttstr str(opt);
            if (str == TJS_N("no"))
                TVPGraphicSplitOperationType = gsotNone;
            else if (str == TJS_N("int"))
                TVPGraphicSplitOperationType = gsotInterlace;
            else if (str == TJS_N("yes") || str == TJS_N("simple"))
                TVPGraphicSplitOperationType = gsotSimple;
            else if (str == TJS_N("bidi"))
                TVPGraphicSplitOperationType = gsotBiDirection;
        }
    }

    // check TVPDefaultHoldAlpha option
    if (TVPGetCommandLine(TJS_N("-holdalpha"), &opt))
    {
        ttstr str(opt);
        if (str == TJS_N("yes") || str == TJS_N("true"))
            TVPDefaultHoldAlpha = true;
        else
            TVPDefaultHoldAlpha = false;
    }

    // timer precision
    uint32_t prectick = 1;
    if (TVPGetCommandLine(TJS_N("-timerprec"), &opt))
    {
        ttstr str(opt);
        if (str == TJS_N("high"))
            prectick = 1;
        if (str == TJS_N("higher"))
            prectick = 5;
        if (str == TJS_N("normal"))
            prectick = 10;
    }

    // draw thread num
    tjs_int drawThreadNum = 0;
    if (TVPGetCommandLine(TJS_N("-drawthread"), &opt))
    {
        ttstr str(opt);
        if (str == TJS_N("auto"))
            drawThreadNum = 0;
        else
            drawThreadNum = (tjs_int)opt;
    }
    TVPDrawThreadNum = drawThreadNum;
}
//---------------------------------------------------------------------------
void TVPBeforeSystemUninit()
{
    
}
//---------------------------------------------------------------------------
void TVPAfterSystemUninit()
{
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
bool TVPTerminated = false;
bool TVPTerminateOnWindowClose = true;
bool TVPTerminateOnNoWindowStartup = true;
int TVPTerminateCode = 0;
//---------------------------------------------------------------------------
void TVPTerminateAsync(int code)
{
    // do "A"synchronous temination of application
    TVPTerminated = true;
    TVPTerminateCode = code;

    // posting dummy message will prevent "missing WM_QUIT bug" in Direct3D framework.
    if (TVPSystemControl)
        TVPSystemControl->CallDeliverAllEventsOnIdle();

    Application->Terminate();

    if (TVPSystemControl)
        TVPSystemControl->CallDeliverAllEventsOnIdle();
}
//---------------------------------------------------------------------------
void TVPTerminateSync(int code)
{
    // do synchronous temination of application (never return)
    TVPSystemUninit();
    TVPAbortApplication(code);
}
//---------------------------------------------------------------------------
void TVPMainWindowClosed()
{
    // called from WindowManager.cpp, caused by closing all window.
    if (TVPTerminateOnWindowClose)
        TVPTerminateAsync();
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// GetCommandLine
//---------------------------------------------------------------------------
static std::vector<std::string>* TVPGetEmbeddedOptions()
{
    std::vector<std::string>* ret = NULL;
    return ret;
}
//---------------------------------------------------------------------------
static std::vector<std::string>* TVPGetConfigFileOptions(const ttstr& filename)
{
    std::vector<std::string>* ret = NULL; // new std::vector<std::string>();
    return ret;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
static bool TVPProgramArgumentsInit = false;
static bool TVPDataPathDirectoryEnsured = false;
//---------------------------------------------------------------------------
void TVPEnsureDataPathDirectory()
{
    if (!TVPDataPathDirectoryEnsured)
    {
        TVPDataPathDirectoryEnsured = true;
        // ensure data path existence
        if (!TVPCheckExistentLocalFolder(TVPNativeDataPath.c_str()))
        {
            if (TVPCreateFolders(TVPNativeDataPath.c_str()))
                TVPAddImportantLog(TVPFormatMessage(TVPInfoDataPathDoesNotExistTryingToMakeIt,
                                                    (const tjs_char*)TVPOk));
            else
                TVPAddImportantLog(TVPFormatMessage(TVPInfoDataPathDoesNotExistTryingToMakeIt,
                                                    (const tjs_char*)TVPFaild));
        }
    }
}
//---------------------------------------------------------------------------