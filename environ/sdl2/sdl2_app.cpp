#include <SDL.h>
#ifdef _KRKRSDL3_GL
#include "glad/glad.h"
#else
#include "glad/glad_egl.h"
#endif

#include <map>
#include <vector>
#ifdef _KRKRSDL3_EMSCRIPTEN
#include <emscripten.h>
#endif

#include "TVPApplication.h"
#include "RenderManager.h"
#include "TVPWindow.h"
#include "Platform.h"
#include "WindowManager.h"
#include "TVPSettings.h"
#include "TVPCompositor.h"
#include "TVPDebug.h"

#include "backend/GLRenderBackend.h"
#include "backend/SWRenderBackend.h"

#ifndef _DEBUG
#ifdef _KRKRSDL3_WINDOWS
#include <windows.h>
#endif
#endif

static SDL_Window* tvp_window;
static SDL_Renderer* tvp_renderer = NULL;
static SDL_GLContext tvp_glContext = NULL;
static int winWidth = 1280, winHeight = 720;

#if defined(_KRKRSDL3_ANDROID) || defined(_KRKRSDL3_EMSCRIPTEN)
enum TouchState
{
    STATE_IDLE,
    STATE_SINGLE_FINGER,
    STATE_MULTI_FINGER,
    STATE_MENU
};
struct Finger
{
    SDL_FingerID id;
    float x, y;
    float startX, startY;
    Uint64 downTime;
    bool active;
    bool moved;

    Finger() : id(0), x(0), y(0), startX(0), startY(0), downTime(0), active(false), moved(false) {}
};
static TouchState _state;
static std::map<SDL_FingerID, Finger> fingers;
static float rightClickX, rightClickY;
static Uint64 rightClickStartTime;
static const Uint32 RIGHT_CLICK_CONFIRM_DELAY = 150;
void sendMouseEvent(int button, int eventType, float pX, float pY);
void sendMouseMotion(float pX, float pY);
void handleFingerDown(const SDL_TouchFingerEvent& e)
{
    Finger f;
    f.id = e.fingerId;
    f.x = f.startX = e.x;
    f.y = f.startY = e.y;
    f.downTime = SDL_GetTicks();
    f.active = true;
    f.moved = false;

    fingers[e.fingerId] = f;

    if (fingers.size() == 1)
    {
        _state = STATE_SINGLE_FINGER;
    }
    else if (fingers.size() == 2)
    {
        _state = STATE_MULTI_FINGER;
    }
    else
    {
        _state = STATE_MENU;
        int windowWidth, windowHeight;
        SDL_GetWindowSize(tvp_window, &windowWidth, &windowHeight);
        int pixelX = static_cast<int>(f.x * windowWidth);
        int pixelY = static_cast<int>(f.y * windowHeight);
        TVPInvokeMenu(pixelX, pixelY);
        fingers.clear();
        _state = STATE_IDLE;
    }
}
void handleFingerUp(const SDL_TouchFingerEvent& e)
{
    auto it = fingers.find(e.fingerId);
    if (it == fingers.end())
        return;

    Finger& f = it->second;
    f.active = false;

    if (fingers.size() == 1)
    {
        if (_state == STATE_SINGLE_FINGER)
        {
            if (!f.moved)
                sendMouseEvent(SDL_BUTTON_LEFT, SDL_MOUSEBUTTONDOWN, f.x, f.y);
            sendMouseEvent(SDL_BUTTON_LEFT, SDL_MOUSEBUTTONUP, f.x, f.y);
        }
        else if (_state == STATE_MULTI_FINGER)
        {
            if (!f.moved)
                sendMouseEvent(SDL_BUTTON_RIGHT, SDL_MOUSEBUTTONDOWN, f.x, f.y);
            sendMouseEvent(SDL_BUTTON_RIGHT, SDL_MOUSEBUTTONUP, f.x, f.y);
        }
        _state = STATE_IDLE;
    }

    fingers.erase(it);
}
void handleFingerMotion(const SDL_TouchFingerEvent& e)
{
    auto it = fingers.find(e.fingerId);
    if (it == fingers.end())
        return;

    Finger& f = it->second;

    float dx = e.x - f.startX;
    float dy = e.y - f.startY;
    float moveDist = dx * dx + dy * dy;

    if (moveDist > 0.0001f)
    {
        f.moved = true;
        f.x = e.x;
        f.y = e.y;

        if (_state == STATE_SINGLE_FINGER)
        {
            sendMouseMotion(f.x, f.y);
        }
    }
}

void sendMouseEvent(int button, int eventType, float pX, float pY)
{
    int windowWidth, windowHeight;
    SDL_GetWindowSize(tvp_window, &windowWidth, &windowHeight);
    int pixelX = static_cast<int>(pX * windowWidth);
    int pixelY = static_cast<int>(pY * windowHeight);

    tTVPMouseButton tmp = mbX1;
    switch (button)
    {
        case SDL_BUTTON_RIGHT:
            tmp = mbRight;
            break;
        case SDL_BUTTON_MIDDLE:
            tmp = mbMiddle;
            break;
        case SDL_BUTTON_LEFT:
            tmp = mbLeft;
            break;
        default:
            break;
    }

    if (tmp != mbX1)
    {
        if (eventType == SDL_MOUSEBUTTONDOWN)
        {
            krkrsdl3::KRKR_Trig_MouseDown(tmp, pixelX, pixelY);
        }
        else if (eventType == SDL_MOUSEBUTTONUP)
        {
            krkrsdl3::KRKR_Trig_MouseUp(tmp, pixelX, pixelY);
        }
    }
}
void sendMouseMotion(float pX, float pY)
{
    int windowWidth, windowHeight;
    SDL_GetWindowSize(tvp_window, &windowWidth, &windowHeight);
    int pixelX = static_cast<int>(pX * windowWidth);
    int pixelY = static_cast<int>(pY * windowHeight);
    krkrsdl3::KRKR_Trig_MouseMove(pixelX, pixelY);
}
#endif

static void ProcessEvent(SDL_Event* event)
{
    switch (event->type)
    {
        case SDL_QUIT:
        {
            TVPWindow* tmpwind = TVPGetActiveWindow();
            if (tmpwind)
                tmpwind->Close();
            break;
        }
        case SDL_KEYDOWN:
        {
            if (event->key.keysym.scancode == SDL_SCANCODE_F1)
            {
                int x = 0, y = 0;
                SDL_GetWindowPosition(tvp_window, &x, &y);
                TVPInvokeMenu(x, y);
                break;
            }

            krkrsdl3::KRKR_Trig_KeyDown(event->key.keysym.scancode);
            break;
        }
        case SDL_KEYUP:
        {
            krkrsdl3::KRKR_Trig_KeyUp(event->key.keysym.scancode);
            break;
        }
        case SDL_TEXTINPUT:
        {
            std::string data(event->text.text);
            krkrsdl3::KRKR_Trig_TextInput(data);
            break;
        }
#if defined(_KRKRSDL3_WINDOWS) || defined(_KRKRSDL3_LINUX) || defined(_KRKRSDL3_EMSCRIPTEN)  || defined(_KRKRSDL3_MACOS)
        case SDL_MOUSEBUTTONDOWN:
        {
            tTVPMouseButton tmp = mbX1;
            switch (event->button.button)
            {
                case SDL_BUTTON_RIGHT:
                    tmp = mbRight;
                    break;
                case SDL_BUTTON_MIDDLE:
                    tmp = mbMiddle;
                    break;
                case SDL_BUTTON_LEFT:
                    tmp = mbLeft;
                    break;
                default:
                    break;
            }

            if (tmp != mbX1)
            {
                krkrsdl3::KRKR_Trig_MouseDown(tmp, event->button.x, event->button.y);
            }
            break;
        }
        case SDL_MOUSEBUTTONUP:
        {
            tTVPMouseButton tmp = mbX1;
            switch (event->button.button)
            {
                case SDL_BUTTON_RIGHT:
                    tmp = mbRight;
                    break;
                case SDL_BUTTON_MIDDLE:
                    tmp = mbMiddle;
                    break;
                case SDL_BUTTON_LEFT:
                    tmp = mbLeft;
                    break;
                default:
                    break;
            }

            if (tmp != mbX1)
            {
                krkrsdl3::KRKR_Trig_MouseUp(tmp, event->button.x, event->button.y);
            }
            break;
        }
        case SDL_MOUSEMOTION:
        {
            krkrsdl3::KRKR_Trig_MouseMove(event->motion.x, event->motion.y);
            break;
        }
        case SDL_MOUSEWHEEL:
        {
            krkrsdl3::KRKR_Trig_MouseScroll(event->wheel.x, event->wheel.y, event->wheel.x,
                                                event->wheel.y);
            break;
        }
#endif
#if defined(_KRKRSDL3_ANDROID) || defined(_KRKRSDL3_EMSCRIPTEN)
        case SDL_FINGERDOWN:
            handleFingerDown(event->tfinger);
            break;
        case SDL_FINGERUP:
            handleFingerUp(event->tfinger);
            break;
        case SDL_FINGERMOTION:
            handleFingerMotion(event->tfinger);
            break;
#endif
        default:
            break;
    }
}

static bool RenderFrame()
{
    if(!::Application->Run())
        return false;
    int RW = 1280, RH = 720;
    SDL_GetWindowSize(tvp_window, &RW, &RH);
    // 合成器完成渲染（清屏/绘制/呈现全部由当前渲染后端负责）
    krkrsdl3::TVPRenderOnce(RW, RH);
    return true;
}

int SDL_main(int argc, char* argv[])
{
    if (argc < 2)
    {
        SDL_Log("At least two parameters are required.");
        return 1;
    }

    if (!TVPParseArguments(argc, argv))
        return 1;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0)
    {
        SDL_Log("Fail to initialize SDL.");
        return 1;
    }

    Uint32 windowFlags = SDL_WINDOW_RESIZABLE;
    if (TVPSettings.renderer == "opengl")
        windowFlags |= SDL_WINDOW_OPENGL;

    tvp_window = SDL_CreateWindow("TVP Engine",
                                  SDL_WINDOWPOS_CENTERED,
                                  SDL_WINDOWPOS_CENTERED,
                                  winWidth, winHeight,
                                  windowFlags);

    if (TVPSettings.renderer == "opengl")
    {
#ifdef _KRKRSDL3_GL
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#else
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
#endif
        tvp_glContext = SDL_GL_CreateContext(tvp_window);
        if (tvp_glContext == NULL)
            return 1;
#if defined(_KRKRSDL3_GL)
        if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress))
#elif !defined(_KRKRSDL3_EMSCRIPTEN)
        if (!gladLoadEGLLoader((GLADloadproc)SDL_GL_GetProcAddress))
#endif
#if !defined(_KRKRSDL3_EMSCRIPTEN) || defined(_KRKRSDL3_GL)
        {
            SDL_Log("Failed to initialize GLAD");
            return 1;
        }
#endif
        SDL_GL_MakeCurrent(tvp_window, tvp_glContext);
        SDL_GL_SetSwapInterval(1);
        krkrsdl3::TVPSetRenderBackend(new krkrsdl3::GLRenderBackend());
        krkrsdl3::fetchGLInfo();
    }
    else
    {
        tvp_renderer = SDL_CreateRenderer(tvp_window, -1, SDL_RENDERER_ACCELERATED);
        if (tvp_renderer)
            SDL_RenderSetVSync(tvp_renderer, 1);
        else
            tvp_renderer = SDL_CreateRenderer(tvp_window, -1, SDL_RENDERER_SOFTWARE);

        SDL_RendererInfo info;
        if (SDL_GetRendererInfo(tvp_renderer, &info) == 0)
            SDL_Log("SWRender Backend: %s", info.name);
        krkrsdl3::TVPSetRenderBackend(new krkrsdl3::SWRenderBackend());
    }

    SDL_HideWindow(tvp_window);

    Application = new tTVPApplication;
    if (!::Application->StartApplication())
    {
        SDL_Log("Game Start Failed.");
        return 1;
    }

#ifndef _DEBUG
#ifdef _KRKRSDL3_WINDOWS
    ShowWindow(GetConsoleWindow(), SW_HIDE);
#endif
#endif
    SDL_ShowWindow(tvp_window);

    RenderFrame();

    bool running = true;
    while (running)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_QUIT)
                running = false;
            ProcessEvent(&event);
        }
        if(!RenderFrame())
            break;
    }

    ::Application->OnExit();
    delete Application;
    Application = NULL;
    SDL_DestroyWindow(tvp_window);
    SDL_Log("KRKRSDL2 quit successfully!");
    SDL_Quit();
    TVPClearAllArguments();
    return 0;
}

void TVPSetWindowTitle(const char* title)
{
    SDL_SetWindowTitle(tvp_window, title);
}

std::string TVPGetWindowTitle()
{
    return SDL_GetWindowTitle(tvp_window);
}

void TVPSetWindowFullscreen(bool isFullscreen)
{
    SDL_SetWindowFullscreen(tvp_window, isFullscreen ? SDL_WINDOW_FULLSCREEN : 0);
}

void TVPGetWindowSize(int* w, int* h)
{
    SDL_GetWindowSize(tvp_window, w, h);
}

void TVPSetWindowSize(int w, int h)
{
    SDL_SetWindowSize(tvp_window, w, h);
}

int TVPDrawSceneOnce(int interval)
{
    static tjs_uint64 lastTick = TVPGetRoughTickCount();
    tjs_uint64 curTick = TVPGetRoughTickCount();
    int remain = interval - (curTick - lastTick);
    if (remain <= 0)
    {
        RenderFrame();
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ProcessEvent(&event);
        }
        lastTick = curTick;
        return 0;
    }
    else
    {
        return remain;
    }
}

void TVPShowIME(int x, int y, int w, int h)
{
    SDL_StartTextInput();
}

void TVPHideIME()
{
    SDL_StopTextInput();
}

int TVPConvertKeyCodeToVKCode(int keyCode)
{
#define CASE(x) \
    case SDL_SCANCODE_##x: \
        return VK_##x

    SDL_Scancode tmp = (SDL_Scancode)keyCode;
    switch (tmp)
    {
        CASE(0);
        CASE(1);
        CASE(2);
        CASE(3);
        CASE(4);
        CASE(5);
        CASE(6);
        CASE(7);
        CASE(8);
        CASE(9);
        CASE(A);
        CASE(B);
        CASE(C);
        CASE(D);
        CASE(E);
        CASE(F);
        CASE(G);
        CASE(H);
        CASE(I);
        CASE(J);
        CASE(K);
        CASE(L);
        CASE(M);
        CASE(N);
        CASE(O);
        CASE(P);
        CASE(Q);
        CASE(R);
        CASE(S);
        CASE(T);
        CASE(U);
        CASE(V);
        CASE(W);
        CASE(X);
        CASE(Y);
        CASE(Z);
        CASE(F1);
        CASE(F2);
        CASE(F3);
        CASE(F4);
        CASE(F5);
        CASE(F6);
        CASE(F7);
        CASE(F8);
        CASE(F9);
        CASE(F10);
        CASE(F11);
        CASE(F12);
        CASE(PAUSE);
        CASE(ESCAPE);
        CASE(CANCEL);
        CASE(INSERT);
        CASE(HOME);
        CASE(DELETE);
        CASE(END);
        CASE(SPACE);
        case SDL_SCANCODE_PRINTSCREEN:
            return VK_PRINT;
            CASE(TAB);
            CASE(RETURN);
        case SDL_SCANCODE_SCROLLLOCK:
            return VK_SCROLL;
        case SDL_SCANCODE_SYSREQ:
            return VK_SNAPSHOT;
        case SDL_SCANCODE_BACKSPACE:
            return VK_BACK;
        case SDL_SCANCODE_CAPSLOCK:
            return VK_CAPITAL;
        case SDL_SCANCODE_LSHIFT:
            return VK_SHIFT;
        case SDL_SCANCODE_RSHIFT:
            return VK_SHIFT;
        case SDL_SCANCODE_LCTRL:
            return VK_CONTROL;
        case SDL_SCANCODE_RCTRL:
            return VK_CONTROL;
        case SDL_SCANCODE_LALT:
            return VK_MENU;
        case SDL_SCANCODE_RALT:
            return VK_MENU;
        case SDL_SCANCODE_MENU:
            return VK_APPS;
        case SDL_SCANCODE_PAGEUP:
            return VK_PRIOR;
        case SDL_SCANCODE_PAGEDOWN:
            return VK_NEXT;
        case SDL_SCANCODE_LEFT:
            return VK_LEFT;
        case SDL_SCANCODE_RIGHT:
            return VK_RIGHT;
        case SDL_SCANCODE_UP:
            return VK_UP;
        case SDL_SCANCODE_DOWN:
            return VK_DOWN;
        case SDL_SCANCODE_NUMLOCKCLEAR:
            return VK_NUMLOCK;
        case SDL_SCANCODE_KP_PLUS:
            return VK_ADD;
        case SDL_SCANCODE_KP_MINUS:
            return VK_SUBTRACT;
        case SDL_SCANCODE_KP_MULTIPLY:
            return VK_MULTIPLY;
        case SDL_SCANCODE_KP_DIVIDE:
            return VK_DIVIDE;
        case SDL_SCANCODE_KP_ENTER:
            return VK_RETURN;
        case SDL_SCANCODE_COMMA:
            return VK_OEM_COMMA;
        case SDL_SCANCODE_MINUS:
            return VK_OEM_MINUS;
        case SDL_SCANCODE_PERIOD:
            return VK_OEM_PERIOD;
        case SDL_SCANCODE_EQUALS:
            return VK_OEM_PLUS;
        case SDL_SCANCODE_SLASH:
            return VK_OEM_2;
        case SDL_SCANCODE_SEMICOLON:
            return VK_OEM_1;
        case SDL_SCANCODE_BACKSLASH:
            return VK_OEM_5;
        case SDL_SCANCODE_LEFTBRACKET:
            return VK_OEM_4;
        case SDL_SCANCODE_RIGHTBRACKET:
            return VK_OEM_6;
        case SDL_SCANCODE_AUDIOPLAY:
            return VK_PLAY;
        default:
            return 0;
    }
}

std::vector<std::string> TVPListAllRenderBackend()
{
    ttstr log(TJS_N("Available Render:"));
    std::vector<std::string> backends;
    int count = SDL_GetNumRenderDrivers();
    for (int i = 0; i < count; i++)
    {
        SDL_RendererInfo info;
        if (SDL_GetRenderDriverInfo(i, &info) == 0 && info.name)
        {
            backends.push_back(info.name);
            log += " " + ttstr((const char*)info.name);
        }
    }
    TVPAddImportantLog(log);

    return backends;
}

void TVPCreateTextureBackend(TVPSprite& sp)
{
    sp.texture = SDL_CreateTexture(tvp_renderer, SDL_PIXELFORMAT_ABGR8888,
                                   SDL_TEXTUREACCESS_STREAMING, sp.width, sp.height);
    SDL_SetTextureBlendMode((SDL_Texture*)sp.texture, SDL_BLENDMODE_BLEND);
}

void TVPUpdateTextureBackend(TVPSprite* sp, uint8_t* buff, int width, int height, int pitch)
{
    SDL_UpdateTexture((SDL_Texture*)sp->texture, nullptr, buff, pitch);
}

void TVPDestroyTextureBackend(TVPSprite* sp)
{
    SDL_DestroyTexture((SDL_Texture*)sp->texture);
}

void TVPRenderTextureBackend(TVPSprite* sp, int posX, int posY, int width, int height)
{
    SDL_Rect rectBuff;
    rectBuff.x = posX;
    rectBuff.y = posY;
    rectBuff.w = width;
    rectBuff.h = height;
    SDL_RenderCopy(tvp_renderer, (SDL_Texture*)sp->texture, NULL, &rectBuff);
}

//---------------------------------------------------------------------------
// 渲染后端平台钩子（由 core/render/backend 的 SW/GL 后端调用）
//---------------------------------------------------------------------------
bool TVPSoftwareRenderBackendAvailable()
{
    return true;
}

void TVPRenderClearBackend()
{
    SDL_SetRenderDrawColor(tvp_renderer, 0, 0, 0, 0);
    SDL_RenderClear(tvp_renderer);
}

void TVPRenderPresentBackend()
{
    SDL_RenderPresent(tvp_renderer);
}

void TVPSwapBuffersBackend()
{
    SDL_GL_SwapWindow(tvp_window);
}