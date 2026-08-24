#include "tjsCommHead.h"
#include "TVPSettings.h"

#include "TVPWindow.h"

#include "TVPSystem.h"
#include "TVPStorage.h"
#include "TVPDebug.h"
#include "TVPMsg.h"
#include "Platform.h"
#include "PlatformView.h"
#include "PlatformFile.h"

#include "TVPCompositor.h"

TVPGlobalSettings TVPSettings;
static std::vector<ttstr> TVPProgramArguments;
static void TVPDumpOptions()
{
    std::vector<ttstr>::const_iterator i;
    ttstr options("(info) Specified option :");
    if (TVPProgramArguments.size())
    {
        for (i = TVPProgramArguments.begin(); i != TVPProgramArguments.end(); i++)
        {
            options += TJS_N(" ");
            options += *i;
        }
    }
    else
    {
        options += (const tjs_char*)TVPNone;
    }
    TVPAddImportantLog(options);
}
static ttstr TVPParseCommandLineOne(const ttstr& i)
{
    // value is specified
    const tjs_char *p, *o;
    p = o = i.c_str();
    p = TJS_strchr(p, '=');

    if (p == NULL)
    {
        return i + TJS_N("=yes");
    }

    p++;

    ttstr optname(o, (int)(p - o));
    // as a string
    return optname + p;
}
static void TVPInitProgramArguments(int tvp_argc, char* tvp_argv[])
{
    // find options from self executable image
    bool argument_stopped = false;
    int file_argument_count = 0;
    for (tjs_int i = 1; i < tvp_argc; i++)
    {
        if (argument_stopped)
        {
            ttstr arg_name_and_value =
                TJS_N("-arg") + ttstr(file_argument_count) + TJS_N("=") + ttstr(tvp_argv[i]);
            file_argument_count++;
            TVPProgramArguments.push_back(arg_name_and_value);
        }
        else
        {
            if (tvp_argv[i][0] == TJS_N('-'))
            {
                if (tvp_argv[i][1] == TJS_N('-') && tvp_argv[i][2] == 0)
                {
                    // argument stopper
                    argument_stopped = true;
                }
                else
                {
                    ttstr value(tvp_argv[i]);
                    if (!TJS_strchr(value.c_str(), TJS_N('=')))
                        value += TJS_N("=yes");
                    TVPProgramArguments.push_back(TVPParseCommandLineOne(value));
                }
            }
        }
    }
}
static void TVPDetectRender()
{
    // SDL 渲染驱动列表（仅信息展示）
    TVPListAllRenderBackend();
    // 已注册且平台可用的渲染后端（见 core/render/backend/RenderBackend.h）
    std::vector<std::string> backends = krkrsdl3::TVPListRenderBackends();
    auto hasBackend = [&backends](const char* name)
    { return std::find(backends.begin(), backends.end(), name) != backends.end(); };
    // 选择编译后端
    tTJSVariant opt;
    TVPSettings.renderer = "software"; // 软渲染保底
    if (TVPGetCommandLine(TJS_N("-render"), &opt))
    {
        ttstr str(opt);
        if (str == TJS_N("opengl") || str == TJS_N("gl") || str == TJS_N("gpu"))
        {
            if (hasBackend("opengl"))
                TVPSettings.renderer = "opengl";
            else
                TVPAddImportantLog(ttstr(TJS_N("Renderer 'opengl' is not available, using '")) +
                                   ttstr(TVPSettings.renderer) + TJS_N("'"));
        }
        else if (str == TJS_N("vulkan") || str == TJS_N("vk"))
        {
            if (hasBackend("vulkan"))
                TVPSettings.renderer = "vulkan";
            else
                TVPAddImportantLog(ttstr(TJS_N("Renderer 'vulkan' is not available, using '")) +
                                   ttstr(TVPSettings.renderer) + TJS_N("'"));
        }
        else if (str == TJS_N("metal") || str == TJS_N("mtl"))
        {
            if (hasBackend("metal"))
                TVPSettings.renderer = "metal";
            else
                TVPAddImportantLog(ttstr(TJS_N("Renderer 'metal' is not available, using '")) +
                                   ttstr(TVPSettings.renderer) + TJS_N("'"));
        }
        else if (str == TJS_N("d3d11") || str == TJS_N("d3d"))
        {
            if (hasBackend("d3d11"))
                TVPSettings.renderer = "d3d11";
            else
                TVPAddImportantLog(ttstr(TJS_N("Renderer 'd3d11' is not available, using '")) +
                                   ttstr(TVPSettings.renderer) + TJS_N("'"));
        }
        else if (str == TJS_N("d3d12"))
        {
            if (hasBackend("d3d12"))
                TVPSettings.renderer = "d3d12";
            else
                TVPAddImportantLog(ttstr(TJS_N("Renderer 'd3d12' is not available, using '")) +
                                   ttstr(TVPSettings.renderer) + TJS_N("'"));
        }
        else if (str == TJS_N("d3d9"))
        {
            if (hasBackend("d3d9"))
                TVPSettings.renderer = "d3d9";
            else
                TVPAddImportantLog(ttstr(TJS_N("Renderer 'd3d9' is not available, using '")) +
                                   ttstr(TVPSettings.renderer) + TJS_N("'"));
        }
        else if (str == TJS_N("software") || str == TJS_N("sw"))
        {
            TVPSettings.renderer = "software";
        }
        else
        {
            TVPAddImportantLog(ttstr(TJS_N("Unknown renderer '")) + str +
                               TJS_N("', using default '") + ttstr(TVPSettings.renderer) +
                               TJS_N("'"));
        }
    }
    else
    // 未指定时自动选择
    // 默认选择（无需做任何区分，编译时已确认平台）
    // 优先判定平台相关后端，再判定通用后端
    {
        // Apple系列
        if (hasBackend("metal"))
            TVPSettings.renderer = "metal";
        // Windows系列
        else if (hasBackend("d3d11"))
            TVPSettings.renderer = "d3d11";
        else if (hasBackend("d3d12"))
            TVPSettings.renderer = "d3d12";
        else if (hasBackend("d3d9"))
            TVPSettings.renderer = "d3d9";
        // 通用系列
        else if (hasBackend("opengl"))
            TVPSettings.renderer = "opengl";
        else if (hasBackend("vulkan"))
            TVPSettings.renderer = "vulkan";
        else
            TVPSettings.renderer = "software";
    }
    TVPAddImportantLog(ttstr("Selected Render: ") + TVPSettings.renderer);

    // 初始窗口尺寸：-window=WxH（默认 1280x720）
    TVPSettings.window_width = 1280;
    TVPSettings.window_height = 720;
    if (TVPGetCommandLine(TJS_N("-window"), &opt))
    {
        ttstr str(opt);
        const tjs_char* p = str.c_str();
        int w = 0, h = 0;
        while (*p >= '0' && *p <= '9')
        {
            w = w * 10 + (*p - '0');
            p++;
        }
        if (*p == 'x' || *p == 'X')
        {
            p++;
            while (*p >= '0' && *p <= '9')
            {
                h = h * 10 + (*p - '0');
                p++;
            }
        }
        if (w > 0 && h > 0)
        {
            TVPSettings.window_width = w;
            TVPSettings.window_height = h;
            TVPAddImportantLog(ttstr("Window size: ") + ttstr(w) + TJS_N("x") + ttstr(h));
        }
        else
        {
            TVPAddImportantLog(ttstr("Invalid -window value '") + str +
                               TJS_N("', using default 1280x720"));
        }
    }

    // 垂直同步：-vsync=0/1（默认开启）
    TVPSettings.vsync = 1;
    if (TVPGetCommandLine(TJS_N("-vsync"), &opt))
    {
        ttstr str(opt);
        if (str == TJS_N("0"))
            TVPSettings.vsync = 0;
        else if (str == TJS_N("1"))
            TVPSettings.vsync = 1;
        else
            TVPAddImportantLog(ttstr(TJS_N("Invalid -vsync value '")) + str + TJS_N("', using 1"));
    }
}

bool TVPParseArguments(int argc, char* argv[])
{
    // exeName
    TVPNativeExeName = argv[0];
    TVPNativeExeDir = TVPExtractStoragePath(TVPNativeExeName);
    TVPAddImportantLog(TVPFormatMessage("(info) Exe path : %1", TVPNativeExeName));
    // datas
    TVPNativeProjectData = argv[1];
    if (TVPCheckExistentLocalFolder(TVPNativeProjectData))
    {
        TVPNativeProjectDir = TVPNativeProjectData;
    }
    else if (TVPCheckExistentLocalFile(TVPNativeProjectData))
    {
        TVPNativeProjectDir = TVPExtractStoragePath(TVPNativeProjectData);
    }
    else
    {
        TVPAddImportantLog(TVPFormatMessage("%1 not found.", TVPNativeProjectData));
        return false;
    }
    // TVPNormalizeStorageName->file://
    TVPProjectData = TVPNormalizeStorageName(TVPNativeProjectData);
    TVPProjectDir = TVPNormalizeStorageName(TVPNativeProjectDir);
    TVPAddImportantLog(TVPFormatMessage("(info) Game path : %1", TVPProjectData));

    // savedata path
    TVPDataPath = TVPProjectDir + TJS_N("savedata/");
    TVPNativeDataPath = TVPGetLocallyAccessibleName(TVPDataPath);
    TVPAddImportantLog(TVPFormatMessage("(info) Savedata path : %1", TVPDataPath));

    // set log output directory
    TVPSetLogLocation(TVPNativeDataPath);

    // args 
    TVPInitProgramArguments(argc, argv);
    TVPDumpOptions();

    // check CPU type
    TVPDetectCPU();

    // check render
    TVPDetectRender();

    // 其他设置
    TVPSettings.ogl_accurate_render = false;
    TVPSettings.default_font = "";
    TVPSettings.force_default_font = false;
    TVPSettings.software_draw_thread = 0;

    return true;
}
void TVPClearAllArguments()
{
    TVPProgramArguments.clear();
    TVPNativeExeName.Clear();
    TVPNativeExeDir.Clear();
    TVPNativeProjectData.Clear();
    TVPNativeProjectDir.Clear();
    TVPNativeDataPath.Clear();
    TVPProjectData.Clear();
    TVPProjectDir.Clear();
    TVPDataPath.Clear();
}
bool TVPGetCommandLine(const tjs_char* name, tTJSVariant* value)
{
    tjs_int namelen = (tjs_int)TJS_strlen(name);
    std::vector<ttstr>::const_iterator i;
    for (i = TVPProgramArguments.begin(); i != TVPProgramArguments.end(); i++)
    {
        if (!TJS_strncmp(i->c_str(), name, namelen))
        {
            if (i->c_str()[namelen] == TJS_N('='))
            {
                // value is specified
                const tjs_char* p = i->c_str() + namelen + 1;
                if (value)
                    *value = p;
                return true;
            }
            else if (i->c_str()[namelen] == 0)
            {
                // value is not specified
                if (value)
                    *value = TJS_N("yes");
                return true;
            }
        }
    }
    return false;
}
void TVPSetCommandLine(const tjs_char* name, const ttstr& value)
{
    tjs_int namelen = (tjs_int)TJS_strlen(name);
    std::vector<ttstr>::iterator i;
    for (i = TVPProgramArguments.begin(); i != TVPProgramArguments.end(); i++)
    {
        if (!TJS_strncmp(i->c_str(), name, namelen))
        {
            if (i->c_str()[namelen] == TJS_N('=') || i->c_str()[namelen] == 0)
            {
                // value found
                *i = ttstr(i->c_str(), namelen) + TJS_N("=") + value;
                return;
            }
        }
    }

    // value not found; insert argument into front
    TVPProgramArguments.insert(TVPProgramArguments.begin(), ttstr(name) + TJS_N("=") + value);
}