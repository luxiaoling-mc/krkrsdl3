//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// System Initialization and Uninitialization
//---------------------------------------------------------------------------
#ifndef SysInitImplH
#define SysInitImplH

//---------------------------------------------------------------------------
extern void TVPInitializeBaseSystems();

extern bool TVPProjectDirSelected;
extern void TVPEnsureDataPathDirectory();

extern bool TVPTerminated;
extern bool TVPTerminateOnWindowClose;
extern bool TVPTerminateOnNoWindowStartup;
extern int TVPTerminateCode;

//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// System initialization and uninitialization
//---------------------------------------------------------------------------

//-- implementation in this unit
extern void TVPSystemInit(void);
extern void TVPSystemUninit(void);

//-- implement in each platform
extern void TVPBeforeSystemInit(); // this must set TVPProjectDir
extern void TVPAfterSystemInit();
extern void TVPBeforeSystemUninit();
extern void TVPAfterSystemUninit();

extern void TVPTerminateAsync(int code = 0); // do acynchronous teminating of application
extern void TVPTerminateSync(
    int code = 0);                 // do synchronous teminating of application(never return)
extern void TVPMainWindowClosed(); // called from WindowManager.cpp, caused by closing main window.
// this function must shutdown the application, unless the controller window is visible.
//---------------------------------------------------------------------------

extern bool TVPSystemUninitCalled;
// whether TVPSystemUninit is called or not

//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// AtExit related
//---------------------------------------------------------------------------
void TVPAddAtExitHandler(tjs_int pri, void (*handler)());
struct tTVPAtExit
{
    tTVPAtExit(tjs_int pri, void (*handler)()) { TVPAddAtExitHandler(pri, handler); }
};
#define TVP_ATEXIT_PRI_PREPARE 10
#define TVP_ATEXIT_PRI_SHUTDOWN 100
#define TVP_ATEXIT_PRI_RELEASE 1000
#define TVP_ATEXIT_PRI_CLEANUP 10000
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
extern bool TVPGetAsyncKeyState(tjs_uint keycode, bool getcurrent = true);
//---------------------------------------------------------------------------
extern void TVPPostApplicationActivateEvent();
extern void TVPPostApplicationDeactivateEvent();
extern bool TVPShellExecute(const ttstr& target, const ttstr& param);
extern void TVPDoSaveSystemVariables();
//---------------------------------------------------------------------------
extern void TVPReadRegValue(tTJSVariant& result, const ttstr& key);
extern bool TVPCreateAppLock(const ttstr& lockname);
extern int TVPGetSupportTouchDevice();

//---------------------------------------------------------------------------
extern void TVPFireOnApplicationActivateEvent(bool activate_or_deactivate);
extern tjs_int TVPGetOSBits();
//---------------------------------------------------------------------------

#endif
