// OHOS entry — native render thread, ArkTS-driven touch
// Render loop runs on dedicated pthread, ArkTS main thread handles input

#include "tjsCommHead.h"
#include "TVPApplication.h"
#include "RenderManager.h"
#include "TVPWindow.h"
#include "Platform.h"
#include "TVPSettings.h"
#include "TVPCompositor.h"

#include "backend/GLRenderBackend.h"

#include <EGL/egl.h>
#include <hilog/log.h>
#include <napi/native_api.h>
#include <rawfile/raw_file_manager.h>
#include <rawfile/raw_file.h>
#include <native_window/external_window.h>
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>

#undef LOG_DOMAIN
#undef LOG_TAG
#define LOG_DOMAIN 0x3200
#define LOG_TAG "krkrsdl3"

static int winWidth = 1280, winHeight = 720;
static bool g_running = false;
static bool g_surfaceReady = false;
static EGLDisplay g_eglDisplay = EGL_NO_DISPLAY;
static EGLSurface g_eglSurface = EGL_NO_SURFACE;
static EGLContext g_eglContext = EGL_NO_CONTEXT;
static OHNativeWindow* g_nativeWindow = nullptr;
static NativeResourceManager* g_resourceMgr = nullptr;
static napi_threadsafe_function g_titleCallback = nullptr;
static napi_threadsafe_function g_fullscreenCallback = nullptr;
static napi_threadsafe_function g_imeCallback = nullptr;
static pthread_t g_renderThread = 0;

NativeResourceManager* OHOS_GetResourceManager() { return g_resourceMgr; }

// ─── 渲染线程（EGL context 由本线程创建，thread-local）─────────
static void* RenderThreadProc(void* data)
{
    const char* gamePath = (const char*)data;

    // 等待 native window 就绪
    while (!g_nativeWindow) { usleep(16000); }

    // 在本线程创建 EGL 上下文
    g_eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(g_eglDisplay, nullptr, nullptr);
    EGLint attrs[] = { EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
                       EGL_RED_SIZE,8, EGL_GREEN_SIZE,8, EGL_BLUE_SIZE,8, EGL_ALPHA_SIZE,8, EGL_NONE };
    EGLConfig cfg; EGLint n;
    eglChooseConfig(g_eglDisplay, attrs, &cfg, 1, &n);
    g_eglSurface = eglCreateWindowSurface(g_eglDisplay, cfg, (EGLNativeWindowType)g_nativeWindow, nullptr);
    EGLint ctxAttrs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    g_eglContext = eglCreateContext(g_eglDisplay, cfg, EGL_NO_CONTEXT, ctxAttrs);
    eglMakeCurrent(g_eglDisplay, g_eglSurface, g_eglSurface, g_eglContext);
    eglSwapInterval(g_eglDisplay, 1);
    krkrsdl3::TVPSetRenderBackend(new krkrsdl3::GLRenderBackend());
    krkrsdl3::fetchGLInfo();
    g_surfaceReady = true;
    OH_LOG_INFO(LOG_APP, "EGL ready on render thread");

    // EGL 就绪后启动引擎（glGenTextures 等 GL 调用此时才有效）
    if (gamePath && gamePath[0]) {
        const char* args[2] = {"./krkrsdl3", gamePath};
        TVPParseArguments(2, (char**)args);
    }
    Application = new tTVPApplication;
    if (!::Application->StartApplication()) {
        OH_LOG_ERROR(LOG_APP, "StartApplication failed");
        g_running = false;
        return nullptr;
    }
    g_running = true;

    while (g_running) {
        if(!::Application->Run())
            break;
        // 合成器完成渲染（清屏/绘制/呈现全部由当前渲染后端负责）
        krkrsdl3::TVPRenderOnce(winWidth, winHeight);
    }
    
    ::Application->OnExit();
    delete Application;
    Application = NULL;

    eglMakeCurrent(g_eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(g_eglDisplay, g_eglSurface);
    eglDestroyContext(g_eglDisplay, g_eglContext);
    eglTerminate(g_eglDisplay);
    if(gamePath) free((void*)gamePath);
    TVPClearAllArguments();
    exit(0);
    return nullptr;
}

// ─── NAPI ─────────────────────────────────────────────────────
static napi_value NAPI_InitEngine(napi_env env, napi_callback_info info)
{
    size_t argc = 1; napi_value argv[1];
    napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);
    char arg0[256] = {0}; size_t len;
    napi_get_value_string_utf8(env, argv[0], arg0, sizeof(arg0), &len);

    // 游戏路径传给渲染线程（EGL 就绪后再 StartApplication）
    char* gamePath = strdup((const char*)arg0);
    pthread_create(&g_renderThread, nullptr, RenderThreadProc, gamePath);
    OH_LOG_INFO(LOG_APP, "Engine thread started with path: %{public}s", gamePath);
    napi_value r; napi_get_boolean(env, true, &r); return r;
}

static napi_value NAPI_SetResourceManager(napi_env env, napi_callback_info info)
{
    size_t argc = 1; napi_value argv[1];
    napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);
    g_resourceMgr = OH_ResourceManager_InitNativeResourceManager(env, argv[0]);
    napi_value r; napi_get_boolean(env, g_resourceMgr != nullptr, &r); return r;
}

static napi_value NAPI_SetSurface(napi_env env, napi_callback_info info)
{
    size_t argc = 1; napi_value argv[1];
    napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);
    char idStr[256] = {0}; size_t strLen;
    napi_get_value_string_utf8(env, argv[0], idStr, sizeof(idStr), &strLen);
    uint64_t surfaceId = strtoull(idStr, nullptr, 10);

    OHNativeWindow* nw = nullptr;
    if (OH_NativeWindow_CreateNativeWindowFromSurfaceId(surfaceId, &nw) != 0 || !nw) {
        napi_value r; napi_get_boolean(env, false, &r); return r;
    }
    g_nativeWindow = nw; // 渲染线程轮询此变量，创建 EGL
    napi_value r; napi_get_boolean(env, true, &r); return r;
}

static napi_value NAPI_Shutdown(napi_env env, napi_callback_info info)
{
    g_running = false;
    if (g_renderThread) { pthread_join(g_renderThread, nullptr); g_renderThread = 0; }
    if (g_nativeWindow) { OH_NativeWindow_DestroyNativeWindow(g_nativeWindow); g_nativeWindow = nullptr; }
    napi_value r; napi_get_undefined(env, &r); return r;
}

static napi_value NAPI_SendMouseEvent(napi_env env, napi_callback_info info)
{
    size_t argc = 4; napi_value argv[4];
    napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);
    int32_t type, x, y, button;
    napi_get_value_int32(env, argv[0], &type); napi_get_value_int32(env, argv[1], &x);
    napi_get_value_int32(env, argv[2], &y); napi_get_value_int32(env, argv[3], &button);
    tTVPMouseButton mb = mbLeft;
    if (button == 2) mb = mbRight; else if (button == 3) mb = mbMiddle;
    if (type == 0) krkrsdl3::KRKR_Trig_MouseDown(mb, x, y);
    else if (type == 1) krkrsdl3::KRKR_Trig_MouseUp(mb, x, y);
    else krkrsdl3::KRKR_Trig_MouseMove(x, y);
    napi_value r; napi_get_undefined(env, &r); return r;
}

static napi_value NAPI_RegisterCallbacks(napi_env env, napi_callback_info info)
{
    size_t argc = 3; napi_value argv[3];
    napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);

    napi_value rn;
    napi_create_string_utf8(env, "T", NAPI_AUTO_LENGTH, &rn);
    napi_create_threadsafe_function(env, argv[0], nullptr, rn, 0, 1, nullptr, nullptr, nullptr,
        [](napi_env e, napi_value cb, void*, void* d) {
            napi_value a; napi_create_string_utf8(e, (const char*)d, NAPI_AUTO_LENGTH, &a);
            napi_call_function(e, nullptr, cb, 1, &a, nullptr);
        }, &g_titleCallback);

    napi_create_string_utf8(env, "F", NAPI_AUTO_LENGTH, &rn);
    napi_create_threadsafe_function(env, argv[1], nullptr, rn, 0, 1, nullptr, nullptr, nullptr,
        [](napi_env e, napi_value cb, void*, void* d) {
            napi_value a; napi_get_boolean(e, *(bool*)d, &a);
            napi_call_function(e, nullptr, cb, 1, &a, nullptr);
        }, &g_fullscreenCallback);

    napi_create_string_utf8(env, "I", NAPI_AUTO_LENGTH, &rn);
    napi_create_threadsafe_function(env, argv[2], nullptr, rn, 0, 1, nullptr, nullptr, nullptr,
       [](napi_env e, napi_value cb, void*, void* d) {
            int* p = (int*)d;
            napi_value args[5];
            napi_get_boolean(e, p[0] != 0, &args[0]);
            for (int i = 1; i < 5; i++) napi_create_int32(e, p[i], &args[i]);
            napi_call_function(e, nullptr, cb, 5, args, nullptr);
            delete[] p;
        }, &g_imeCallback);
        
    napi_value r; napi_get_undefined(env, &r); return r;
}

static napi_value NAPI_UpdateWindowSize(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value argv[2];
    napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);
    napi_get_value_int32(env, argv[0], &winWidth);
    napi_get_value_int32(env, argv[1], &winHeight);
    napi_value r; napi_get_undefined(env, &r); return r;
}

// ─── 模块注册 ──────────────────────────────────────────────────
EXTERN_C_START
static napi_value Init(napi_env env, napi_value exports)
{
    napi_property_descriptor desc[] = {
        {"initEngine", nullptr, NAPI_InitEngine, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"setResourceManager", nullptr, NAPI_SetResourceManager, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"setSurface", nullptr, NAPI_SetSurface, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"shutdown", nullptr, NAPI_Shutdown, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"sendMouseEvent", nullptr, NAPI_SendMouseEvent, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"registerCallbacks", nullptr, NAPI_RegisterCallbacks, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"updateWindowSize", nullptr, NAPI_UpdateWindowSize, nullptr, nullptr, nullptr, napi_default, nullptr},
    };
    napi_define_properties(env, exports, sizeof(desc)/sizeof(desc[0]), desc);
    return exports;
}
EXTERN_C_END

static napi_module demoModule = {
    .nm_version = 1, .nm_flags = 0, .nm_filename = nullptr,
    .nm_register_func = Init, .nm_modname = "krkrsdl3_napi",
    .nm_priv = nullptr, .reserved = {0},
};

extern "C" __attribute__((constructor)) void RegisterKrkrsdl3Module(void)
{
    napi_module_register(&demoModule);
}

// ─── 平台函数 ──────────────────────────────────────────────────
void TVPSetWindowTitle(const char* t) {
    if (g_titleCallback) napi_call_threadsafe_function(g_titleCallback, (void*)t, napi_tsfn_blocking);
}
std::string TVPGetWindowTitle() { return "krkrsdl3 OHOS"; }
void TVPSetWindowFullscreen(bool full) {
    if (g_fullscreenCallback) {
        bool* d = new bool(full);
        napi_call_threadsafe_function(g_fullscreenCallback, d, napi_tsfn_blocking);
    }
}
void TVPGetWindowSize(int* w, int* h) { *w = winWidth; *h = winHeight; }
void TVPSetWindowSize(int w, int h) {}
void TVPShowIME(int x, int y, int w, int h) {
    if (g_imeCallback) {
        int* p = new int[5]{1, x, y, w, h};
        napi_call_threadsafe_function(g_imeCallback, p, napi_tsfn_blocking);
    }
}
void TVPHideIME() {
    if (g_imeCallback) {
        int* p = new int[5]{0, 0, 0, 0, 0};
        napi_call_threadsafe_function(g_imeCallback, p, napi_tsfn_blocking);
    }
}
int TVPDrawSceneOnce(int interval) {
    static tjs_uint64 lastTick = TVPGetRoughTickCount();
    tjs_uint64 curTick = TVPGetRoughTickCount();
    int remain = interval - (curTick - lastTick);
    if (remain <= 0)
    {
        eglMakeCurrent(g_eglDisplay, g_eglSurface, g_eglSurface, g_eglContext);
        ::Application->Run();
        iTVPTexture2D::RecycleProcess();
        // 合成器完成渲染（清屏/绘制/呈现全部由当前渲染后端负责）
        krkrsdl3::TVPRenderOnce(winWidth, winHeight);
        lastTick = curTick;
        return 0;
        }
    else
    {
        return remain;
    }
}
std::vector<std::string> TVPListAllRenderBackend() { return {"opengl"}; }
int TVPConvertKeyCodeToVKCode(int k) { return k; }

// GL 呈现钩子（eglSwapBuffers），由 GLRenderBackend 调用
// （软渲染平台钩子为空实现，见 ohos_core.cpp；OHOS 无软渲染路径）
void TVPSwapBuffersBackend()
{
    eglSwapBuffers(g_eglDisplay, g_eglSurface);
}
