#include "tjsCommHead.h"

#include "Platform.h"
#include "PlatformFile.h"
#include "TVPMsg.h"
#include "TVPApplication.h"
#include "TVPSystem.h"
#include "WindowManager.h"

#include "tjsNativeMenuItem.h"

#include <sys/utime.h>
#include <Windows.h>
#include <Psapi.h>

//---------------------------------------------------------------------------
// tTVPFileMedia
//---------------------------------------------------------------------------
class tTVPFileMedia : public iTVPStorageMedia
{
    tjs_uint RefCount;

public:
    tTVPFileMedia() { RefCount = 1; }
    ~tTVPFileMedia() { ; }

    void AddRef() { RefCount++; }
    void Release()
    {
        if (RefCount == 1)
            delete this;
        else
            RefCount--;
    }

    void GetName(ttstr& name) { name = TJS_N("file"); }

    void NormalizeDomainName(ttstr& name);
    void NormalizePathName(ttstr& name);
    bool CheckExistentStorage(const ttstr& name);
    tTJSBinaryStream* Open(const ttstr& name, tjs_uint32 flags);
    void GetListAt(const ttstr& name, iTVPStorageLister* lister);
    void GetLocallyAccessibleName(ttstr& name);

public:
    void GetLocalName(ttstr& name);
};
//---------------------------------------------------------------------------
void tTVPFileMedia::NormalizeDomainName(ttstr& name)
{
    // normalize domain name
    // make all characters small
    tjs_char* p = name.Independ();
    while (*p)
    {
        if (*p >= TJS_N('A') && *p <= TJS_N('Z'))
            *p += TJS_N('a') - TJS_N('A');
        p++;
    }
}
//---------------------------------------------------------------------------
void tTVPFileMedia::NormalizePathName(ttstr& name)
{
    // normalize path name
    // make all characters small
    tjs_char* p = name.Independ();
    while (*p)
    {
        if (*p >= TJS_N('A') && *p <= TJS_N('Z'))
            *p += TJS_N('a') - TJS_N('A');
        p++;
    }
}
//---------------------------------------------------------------------------
bool tTVPFileMedia::CheckExistentStorage(const ttstr& name)
{
    if (name.IsEmpty())
        return false;

    ttstr _name(name);
    GetLocalName(_name);

    return TVPCheckExistentLocalFile(_name);
}
//---------------------------------------------------------------------------
tTJSBinaryStream* tTVPFileMedia::Open(const ttstr& name, tjs_uint32 flags)
{
    // open storage named "name".
    // currently only local/network(by OS) storage systems are supported.
    if (name.IsEmpty())
        TVPThrowExceptionMessage(TVPCannotOpenStorage, TJS_N("\"\""));

    ttstr origname = name;
    ttstr _name(name);
    GetLocalName(_name);

    return TVPCreateLocalFileStream(origname, _name, flags);
}
//---------------------------------------------------------------------------
void tTVPFileMedia::GetListAt(const ttstr& _name, iTVPStorageLister* lister)
{
    ttstr name(_name);
    GetLocalName(name);
    TVPGetLocalFileListAt(name,
                          [lister](const ttstr& name, tTVPLocalFileInfo* s)
                          {
                              if (s->Mode & (S_IFREG))
                              {
                                  lister->Add(name);
                              }
                          });
}
//---------------------------------------------------------------------------
void tTVPFileMedia::GetLocallyAccessibleName(ttstr& name)
{
    ttstr newname;

    const tjs_char* ptr = name.c_str();

    if (TJS_strncmp(ptr, TJS_N("./"), 2))
    {
        // differs from "./",
        // this may be a UNC file name.
        // UNC first two chars must be "\\\\" ?
        // AFAIK 32-bit version of Windows assumes that '/' can be used as a path
        // delimiter. Can UNC "\\\\" be replaced by "//" though ?

        newname = ttstr(TJS_N("\\\\")) + ptr;
    }
    else
    {
        ptr += 2; // skip "./"
        if (!*ptr)
        {
            newname = TJS_N("");
        }
        else
        {
            tjs_char dch = *ptr;
            if (*ptr < TJS_N('a') || *ptr > TJS_N('z'))
            {
                newname = TJS_N("");
            }
            else
            {
                ptr++;
                if (*ptr != TJS_N('/'))
                {
                    newname = TJS_N("");
                }
                else
                {
                    newname = ttstr(dch) + TJS_N(":") + ptr;
                }
            }
        }
    }

    // change path delimiter to '\\'
    tjs_char* pp = newname.Independ();
    while (*pp)
    {
        if (*pp == TJS_N('/'))
            *pp = TJS_N('\\');
        pp++;
    }
    name = newname;
}
//---------------------------------------------------------------------------
void tTVPFileMedia::GetLocalName(ttstr& name)
{
    ttstr tmp = name;
    GetLocallyAccessibleName(tmp);
    if (tmp.IsEmpty())
        TVPThrowExceptionMessage(TVPCannotGetLocalName, name);
    name = tmp;
}
//---------------------------------------------------------------------------
iTVPStorageMedia* TVPCreateFileMedia()
{
    return new tTVPFileMedia;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
tTVPMemoryStream* GetResourceStream(const ttstr& filename)
{
    static ttstr resourceBasePath = TVPNormalizeStorageName(TVPNativeExeDir) + TJS_N("Res/");
    tTJSBinaryStream* tmp = TVPCreateBinaryStreamForRead(resourceBasePath + filename, 0);
    if (!tmp) return nullptr;
    tTVPMemoryStream* ret = new tTVPMemoryStream(nullptr, tmp->GetSize());
    tmp->ReadBuffer(ret->GetInternalBuffer(), tmp->GetSize());
    delete tmp;
    return ret;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
bool TVP_utime(const char* name, time_t modtime)
{
    _utimbuf utb;
    utb.modtime = modtime;
    utb.actime = modtime;
    ttstr filename(name);
    return _wutime((const wchar_t*)filename.c_str(), &utb) == 0;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
void TVPGetMemoryInfo(TVPMemoryInfo& m)
{
    MEMORYSTATUS status;
    status.dwLength = sizeof(status);
    GlobalMemoryStatus(&status);

    m.MemTotal = status.dwTotalPhys / 1024;
    m.MemFree = status.dwAvailPhys / 1024;
    m.SwapTotal = status.dwTotalPageFile / 1024;
    m.SwapFree = status.dwAvailPageFile / 1024;
    m.VirtualTotal = status.dwTotalVirtual / 1024;
    m.VirtualUsed = (status.dwTotalVirtual - status.dwAvailVirtual) / 1024;
}
tjs_int TVPGetSystemFreeMemory()
{
    MEMORYSTATUS info;
    GlobalMemoryStatus(&info);
    return info.dwAvailPhys / (1024 * 1024);
}
tjs_int TVPGetSelfUsedMemory()
{
    PROCESS_MEMORY_COUNTERS info;
    GetProcessMemoryInfo(GetCurrentProcess(), &info, sizeof(info));
    return info.WorkingSetSize / (1024 * 1024);
}
void TVPRelinquishCPU()
{
    Sleep(0);
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
std::string TVPGetPackageVersionString()
{
    return "win32";
}
ttstr TVPGetOSName()
{
    std::string result = "Windows";
    HMODULE hMod = GetModuleHandleW(L"ntdll.dll");
    if (hMod)
    {
        typedef LONG(NTAPI * RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);
        RtlGetVersionPtr RtlGetVersion = (RtlGetVersionPtr)GetProcAddress(hMod, "RtlGetVersion");
        if (RtlGetVersion)
        {
            RTL_OSVERSIONINFOW osvi = {sizeof(osvi)};
            if (RtlGetVersion(&osvi) == 0)
            {
                if (osvi.dwMajorVersion == 10 && osvi.dwBuildNumber >= 22000)
                {
                    return "Windows 11";
                }
                switch (osvi.dwMajorVersion)
                {
                    case 10:
                        return "Windows 10";
                    case 6:
                        switch (osvi.dwMinorVersion)
                        {
                            case 3:
                                return "Windows 8.1";
                            case 2:
                                return "Windows 8";
                            case 1:
                                return "Windows 7";
                            case 0:
                                return "Windows Vista";
                        }
                        break;
                    case 5:
                        switch (osvi.dwMinorVersion)
                        {
                            case 2:
                                return "Windows XP x64";
                            case 1:
                                return "Windows XP";
                            case 0:
                                return "Windows 2000";
                        }
                        break;
                }
                return result + " " + std::to_string(osvi.dwMajorVersion) + "." +
                       std::to_string(osvi.dwMinorVersion);
            }
        }
    }

    OSVERSIONINFOW osvi = {sizeof(osvi)};
#pragma warning(push)
#pragma warning(disable : 4996)
    if (GetVersionExW(&osvi))
    {
#pragma warning(pop)
        if (osvi.dwMajorVersion == 10 && osvi.dwBuildNumber >= 22000)
            return "Windows 11";
        return result + " " + std::to_string(osvi.dwMajorVersion) + "." +
               std::to_string(osvi.dwMinorVersion);
    }
    return "Windows";
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
void TVPPreNormalizeStorageName(ttstr& name)
{
    // if the name is an OS's native expression, change it according with the
    // TVP storage system naming rule.
    tjs_int namelen = name.GetLen();
    if (namelen == 0)
        return;
    if (namelen >= 2)
    {
        if ((name[0] >= TJS_N('a') && name[0] <= TJS_N('z') ||
             name[0] >= TJS_N('A') && name[0] <= TJS_N('Z')) &&
            name[1] == TJS_N(':'))
        {
            // Windows drive:path expression
            ttstr newname(TJS_N("file://./"));
            newname += name[0];
            newname += (name.c_str() + 2);
            name = newname;
            return;
        }
    }

    if (namelen >= 3)
    {
        if (name[0] == TJS_N('\\') && name[1] == TJS_N('\\') ||
            name[0] == TJS_N('/') && name[1] == TJS_N('/'))
        {
            // unc expression
            name = ttstr(TJS_N("file:")) + name;
            return;
        }
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
static tTJSNI_MenuItem* sdl_current_menu = NULL;
static HWND sdl_hwnd = NULL;
struct MenuMapping
{
    int cmdId;
    tTJSNI_MenuItem* item;
};
static std::vector<MenuMapping> g_menuMap;

static std::wstring UTF8ToWide(const char* utf8)
{
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    if (len <= 0) return std::wstring();
    std::wstring ret(len - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, &ret[0], len);
    return ret;
}

void BuildMenu(HMENU hMenu, tTJSNI_MenuItem* menuItem, int& idCounter)
{
    int count = menuItem->GetChildren().size();
    for (int i = 0; i < count; i++)
    {
        tTJSNI_MenuItem* subitem = static_cast<tTJSNI_MenuItem*>(menuItem->GetChildren().at(i));
        ttstr caption;
        subitem->GetCaption(caption);

        if (caption.IsEmpty() || caption == TJS_N("+"))
            continue;

        if (caption == TJS_N("-"))
        {
            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
            continue;
        }
        std::wstring wcaption = UTF8ToWide(caption.c_str());
        if (!subitem->GetChildren().empty())
        {
            HMENU hSubMenu = CreatePopupMenu();
            BuildMenu(hSubMenu, subitem, idCounter);
            AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hSubMenu, wcaption.c_str());
        }
        else if (subitem->GetGroup() > 0 || subitem->GetRadio() || subitem->GetChecked())
        {
            UINT flags = MF_STRING;
            if (subitem->GetChecked())
                flags |= MF_CHECKED;
            int cmdId = ++idCounter;
            AppendMenuW(hMenu, flags, cmdId, wcaption.c_str());
            g_menuMap.push_back({cmdId, subitem});
        }
        else
        {
            int cmdId = ++idCounter;
            AppendMenuW(hMenu, MF_STRING, cmdId, wcaption.c_str());
            g_menuMap.push_back({cmdId, subitem});
        }
    }
}
static void ProcessMenuCommand(int cmdId)
{
    if (!sdl_current_menu)
        return;

    for (auto& entry : g_menuMap)
    {
        if (entry.cmdId == cmdId)
        {
            entry.item->OnClick();
            break;
        }
    }
}

void TVPInvokeMenu(int x, int y, void* _menu)
{
    if (_menu)
    {
        sdl_current_menu = static_cast<tTJSNI_MenuItem*>(_menu);
    }
    else
    {
        iTJSDispatch2* menuobj = TVPGetMenuDispatch((tjs_intptr_t)TVPGetActiveWindow());
        if (!menuobj)
            return;
        menuobj->NativeInstanceSupport(TJS_NIS_GETINSTANCE, tTJSNC_MenuItem::ClassID,
                                        (iTJSNativeInstance**)&sdl_current_menu);
        if (sdl_current_menu->GetChildren().empty())
            return;
    }
    if (!sdl_hwnd)
    {
        sdl_hwnd = GetActiveWindow();
        if (!sdl_hwnd)
            return;
    }
    HMENU hMenu = CreatePopupMenu();
    int idCounter = 0;
    g_menuMap.clear();
    BuildMenu(hMenu, sdl_current_menu, idCounter);
    int cmdId = TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD, x, y, 0,
                                sdl_hwnd,
                    NULL);
    DestroyMenu(hMenu);
    if (cmdId > 0)
    {
        ProcessMenuCommand(cmdId);
    }
}

bool TVPTruncateFile(const std::string& path, size_t size)
{
    std::wstring wpath = UTF8ToWide(path.c_str());
    if (wpath.empty()) return false;
    HANDLE h = CreateFileW(wpath.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    LARGE_INTEGER li;
    li.QuadPart = size;
    bool ok = SetFilePointerEx(h, li, NULL, FILE_BEGIN) && SetEndOfFile(h);
    CloseHandle(h);
    return ok;
}

uint16_t TVPGetFileAttributes(const std::string& path)
{
    std::wstring wpath = UTF8ToWide(path.c_str());
    if (wpath.empty()) return 0xFFFF;
    DWORD wa = GetFileAttributesW(wpath.c_str());
    if (wa == INVALID_FILE_ATTRIBUTES)
        return 0xFFFF;
    uint16_t attr = 0;
    if (wa & FILE_ATTRIBUTE_DIRECTORY)
        attr |= 0x10;
    if (wa & FILE_ATTRIBUTE_READONLY)
        attr |= 0x01;
    return attr;
}

bool TVPSetFileAttributes(const std::string& path, uint16_t attr, uint16_t mask)
{
    std::wstring wpath = UTF8ToWide(path.c_str());
    if (wpath.empty()) return false;
    DWORD wa = GetFileAttributesW(wpath.c_str());
    if (wa == INVALID_FILE_ATTRIBUTES)
        return false;
    if (mask & 0x01)
    {
        if (attr & 0x01)
            wa |= FILE_ATTRIBUTE_READONLY;
        else
            wa &= ~FILE_ATTRIBUTE_READONLY;
    }
    return SetFileAttributesW(wpath.c_str(), wa) != 0;
}
//---------------------------------------------------------------------------