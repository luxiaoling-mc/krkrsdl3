#include "tjsCommHead.h"

#include "Platform.h"
#include "PlatformFile.h"
#include "Random.h"

#include "TVPStorage.h"
#include "TVPMsg.h"

#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_filesystem.h>

//---------------------------------------------------------------------------
static ttstr TVPLocalExtractFilePath(const ttstr& name)
{
    // this extracts given name's path under local filename rule
    const tjs_char* p = name.c_str();
    tjs_int i = name.GetLen() - 1;
    for (; i >= 0; i--)
    {
        if (p[i] == TJS_N(':') || p[i] == TJS_N('/') || p[i] == TJS_N('\\'))
            break;
    }
    return ttstr(p, i + 1);
}

static bool TVPWriteDataToFile(const ttstr& filepath, const void* data, unsigned int len)
{
    SDL_IOStream* handle = SDL_IOFromFile(filepath.c_str(), "wb");
    if (!handle)
    {
        return false;
    }
    size_t written = SDL_WriteIO(handle, data, len);
    SDL_CloseIO(handle);
    return written == len;
}

static bool TVPCopyFolder(const std::string& from, const std::string& to)
{
    if (!TVPCheckExistentLocalFolder(to) && !TVPCreateFolders(to))
    {
        return false;
    }

    bool success = true;
    TVPListDir(from,
               [&](const std::string& _name, int mask)
               {
                   if (_name == "." || _name == "..")
                       return;
                   if (!success)
                       return;
                   if (mask & S_IFREG)
                   {
                       success = TVPCopyFile(from + "/" + _name, to + "/" + _name);
                   }
                   else if (mask & S_IFDIR)
                   {
                       success = TVPCopyFolder(from + "/" + _name, to + "/" + _name);
                   }
               });
    return success;
}

struct TVPRemoveContext
{
    bool error = false;
    std::vector<std::string> files;
    std::vector<std::string> dirs;
    std::string basepath;
};

static SDL_EnumerationResult SDLCALL TVPRemoveEnumerateCb(void* userdata,
                                                         const char* dirname,
                                                         const char* fname)
{
    TVPRemoveContext* ctx = (TVPRemoveContext*)userdata;

    // 跳过 . 和 ..
    if (fname[0] == '.' && (fname[1] == '\0' || (fname[1] == '.' && fname[2] == '\0')))
    {
        return SDL_ENUM_CONTINUE;
    }

    // 获取完整路径
    std::string fullpath = std::string(dirname) + fname;

    // 获取文件信息
    SDL_PathInfo info;
    if (SDL_GetPathInfo(fullpath.c_str(), &info))
    {
        if (info.type == SDL_PATHTYPE_DIRECTORY)
        {
            ctx->dirs.push_back(fullpath);
        }
        else
        {
            ctx->files.push_back(fullpath);
        }
    }

    return SDL_ENUM_CONTINUE;
}

static bool TVPRemoveDirectory(const std::string& path)
{
    TVPRemoveContext ctx;
    ctx.basepath = path;

    // 确保路径以分隔符结尾
    std::string dirpath = path;
    if (!dirpath.empty() && dirpath.back() != '/' && dirpath.back() != '\\')
    {
        dirpath += '/';
    }

    // 枚举目录内容
    if (!SDL_EnumerateDirectory(dirpath.c_str(), TVPRemoveEnumerateCb, &ctx))
    {
        // 如果目录不存在或无法枚举，尝试直接删除
        return SDL_RemovePath(path.c_str());
    }

    // 先递归删除所有子目录（深度优先）
    for (const auto& dir : ctx.dirs)
    {
        if (!TVPRemoveDirectory(dir))
        {
            return false;
        }
    }

    // 再删除所有文件
    for (const auto& file : ctx.files)
    {
        if (!SDL_RemovePath(file.c_str()))
        {
            return false;
        }
    }

    // 最后删除自身目录
    return SDL_RemovePath(path.c_str());
}

struct TVPListDirContext
{
    std::function<void(const std::string&, int)> cb;
};

static SDL_EnumerationResult SDLCALL TVPListdirCb(void* userdata,
                                                const char* dirname,
                                                const char* fname)
{
    TVPListDirContext* ctx = (TVPListDirContext*)userdata;

    // 跳过 . 和 ..
    if (fname[0] == '.' && (fname[1] == '\0' || (fname[1] == '.' && fname[2] == '\0')))
    {
        return SDL_ENUM_CONTINUE;
    }

    std::string fullpath = std::string(dirname) + fname;
    SDL_PathInfo info;
    int mode = 0;

    if (SDL_GetPathInfo(fullpath.c_str(), &info))
    {
        mode = (info.type == SDL_PATHTYPE_DIRECTORY) ? S_IFDIR : S_IFREG;
    }

    ctx->cb(fname, mode);
    return SDL_ENUM_CONTINUE;
}

struct TVPFileInfoContext
{
    std::function<void(const std::string&, tTVPLocalFileInfo*)> cb;
    std::vector<std::string> entries;
};

static SDL_EnumerationResult SDLCALL TVPFileinfoCb(void* userdata,
                                                 const char* dirname,
                                                 const char* fname)
{
    TVPFileInfoContext* ctx = (TVPFileInfoContext*)userdata;

    if (fname[0] == '.' && (fname[1] == '\0' || (fname[1] == '.' && fname[2] == '\0')))
    {
        return SDL_ENUM_CONTINUE;
    }

    std::string fullpath = std::string(dirname) + fname;
    SDL_PathInfo info;

    if (SDL_GetPathInfo(fullpath.c_str(), &info))
    {
        tTVPLocalFileInfo fileinfo;
        fileinfo.NativeName = fname;
        fileinfo.Mode = (info.type == SDL_PATHTYPE_DIRECTORY) ? S_IFDIR : S_IFREG;
        fileinfo.Size = info.size;
        fileinfo.AccessTime = info.access_time;
        fileinfo.ModifyTime = info.modify_time;
        fileinfo.CreationTime = info.create_time;

        ttstr lowerFilename(fname);
        // Lowercase the filename for case-insensitive auto-path lookup.
        // NormalizePathName lowercases all lookup keys, so stored keys must match.
        {
            tjs_char* p = lowerFilename.Independ();
            while (*p)
            {
                if (*p >= TJS_N('A') && *p <= TJS_N('Z'))
                    *p += TJS_N('a') - TJS_N('A');
                p++;
            }
        }

        ctx->cb(lowerFilename.AsStdString(), &fileinfo);
    }

    return SDL_ENUM_CONTINUE;
}

//---------------------------------------------------------------------------
// tTVPLocalFileStream — defined internally, header only exposes factory
//---------------------------------------------------------------------------
class tTVPLocalFileStream : public tTJSBinaryStream
{
private:
    void* Handle;
    tTVPMemoryStream* MemBuffer = nullptr;
    ttstr FileName;

public:
    tTVPLocalFileStream(const ttstr& origname, const ttstr& localname, tjs_uint32 flag);
    ~tTVPLocalFileStream();

    tjs_uint64 Seek(tjs_int64 offset, tjs_int whence);
    tjs_uint Read(void* buffer, tjs_uint read_size);
    tjs_uint Write(const void* buffer, tjs_uint write_size);
    bool Flush();
    void SetEndOfStorage();
    tjs_uint64 GetSize();
    const std::string GetFileName() { return FileName.AsStdString(); }
    void* GetHandle() const { return Handle; }
};

tTJSBinaryStream* TVPCreateLocalFileStream(const ttstr& origname,
                                           const ttstr& localname,
                                           tjs_uint32 flag)
{
    return new tTVPLocalFileStream(origname, localname, flag);
}

//---------------------------------------------------------------------------
// tTVPLocalFileStream implementation
//---------------------------------------------------------------------------
tTVPLocalFileStream::tTVPLocalFileStream(const ttstr& origname,
                                         const ttstr& localname,
                                         tjs_uint32 flag)
  : FileName(localname),
    Handle(nullptr)
{
    tjs_uint32 access = flag & TJS_BS_ACCESS_MASK;
    if (access == TJS_BS_WRITE)
    {
        if (!TVPCheckExistentLocalFile(localname))
        {
            ttstr dirpath = TVPLocalExtractFilePath(localname);
            const tjs_char* p = dirpath.c_str();
            tjs_int i = dirpath.GetLen();
            if (p[i - 1] == TJS_N('/') || p[i - 1] == TJS_N('\\'))
                i--;
            dirpath = dirpath.SubString(0, i);
            if (!TVPCheckExistentLocalFolder(dirpath) && !TVPCreateFolders(dirpath))
            {
                TVPThrowExceptionMessage(TVPCannotOpenStorage, origname);
            }
        }
    }

    const char* mode = nullptr;
    switch (access)
    {
        case TJS_BS_READ:
            mode = "rb";
            break;
        case TJS_BS_WRITE:
            mode = "wb+";
            break;
        case TJS_BS_APPEND:
            mode = "ab+";
            break;
        case TJS_BS_UPDATE:
            mode = "rb+";
            break;
    }

    Handle = SDL_IOFromFile(localname.c_str(), mode);

    if (!Handle)
    {
        if (access == TJS_BS_APPEND || access == TJS_BS_UPDATE)
        {
            Handle = SDL_IOFromFile(localname.c_str(), "rb");
            if (Handle)
            {
                Sint64 size = SDL_GetIOSize((SDL_IOStream*)Handle);
                if (size > 0 && size < 4 * 1024 * 1024)
                {
                    MemBuffer = new tTVPMemoryStream();
                    MemBuffer->SetSize(static_cast<tjs_uint64>(size));
                    SDL_ReadIO((SDL_IOStream*)Handle, MemBuffer->GetInternalBuffer(), size);
                }
                SDL_CloseIO((SDL_IOStream*)Handle);
            }
            if (!MemBuffer)
                TVPThrowExceptionMessage(TVPCannotOpenStorage, origname);
        }
    }

    // push current tick as an environment noise
    uint64_t tick = TVPGetRoughTickCount();
    TVPPushEnvironNoise(&tick, sizeof(tick));
}
//---------------------------------------------------------------------------
tTVPLocalFileStream::~tTVPLocalFileStream()
{
    if (MemBuffer)
    {
        if (!TVPWriteDataToFile(FileName, MemBuffer->GetInternalBuffer(), MemBuffer->GetSize()))
        {
            delete MemBuffer;
            ttstr filename(FileName);
            FileName.~tTJSString();
            free(this);
            TVPThrowExceptionMessage(TJS_N("File Writing Error: %1"), filename);
        }
        delete MemBuffer;
    }
    if (Handle)
    {
        SDL_CloseIO((SDL_IOStream*)Handle);
    }

    // push current tick as an environment noise
    // (timing information from file accesses may be good noises)
    uint64_t tick = TVPGetRoughTickCount();
    TVPPushEnvironNoise(&tick, sizeof(tick));
}
//---------------------------------------------------------------------------
tjs_uint64 tTVPLocalFileStream::Seek(tjs_int64 offset, tjs_int whence)
{
    if (MemBuffer)
    {
        return MemBuffer->Seek(offset, whence);
    }
    SDL_IOWhence sdl_whence;
    switch (whence)
    {
        case SEEK_SET:
            sdl_whence = SDL_IO_SEEK_SET;
            break;
        case SEEK_CUR:
            sdl_whence = SDL_IO_SEEK_CUR;
            break;
        case SEEK_END:
            sdl_whence = SDL_IO_SEEK_END;
            break;
        default:
            sdl_whence = SDL_IO_SEEK_SET;
            break;
    }
    return static_cast<tjs_uint64>(SDL_SeekIO((SDL_IOStream*)Handle, offset, sdl_whence));
}
//---------------------------------------------------------------------------
tjs_uint tTVPLocalFileStream::Read(void* buffer, tjs_uint read_size)
{
    if (MemBuffer)
    {
        return MemBuffer->Read(buffer, read_size);
    }
    return static_cast<tjs_uint>(SDL_ReadIO((SDL_IOStream*)Handle, buffer, read_size));
}
//---------------------------------------------------------------------------
tjs_uint tTVPLocalFileStream::Write(const void* buffer, tjs_uint write_size)
{
    if (MemBuffer)
    {
        return MemBuffer->Write(buffer, write_size);
    }
    return static_cast<tjs_uint>(SDL_WriteIO((SDL_IOStream*)Handle, buffer, write_size));
}
//---------------------------------------------------------------------------
bool tTVPLocalFileStream::Flush()
{
    if (MemBuffer)
    {
        return MemBuffer->Flush();
    }
    return SDL_FlushIO((SDL_IOStream*)Handle);
}
//---------------------------------------------------------------------------
void tTVPLocalFileStream::SetEndOfStorage()
{
    if (MemBuffer)
    {
        return MemBuffer->SetEndOfStorage();
    }

    SDL_SeekIO((SDL_IOStream*)Handle, 0, SDL_IO_SEEK_END);
}
//---------------------------------------------------------------------------
tjs_uint64 tTVPLocalFileStream::GetSize()
{
    if (MemBuffer)
    {
        return MemBuffer->GetSize();
    }
    return static_cast<tjs_uint64>(SDL_GetIOSize((SDL_IOStream*)Handle));
}
//---------------------------------------------------------------------------

#if !defined(_KRKRSDL3_IOS)
std::string TVPGetDefaultFileDir()
{
    char* path = SDL_GetCurrentDirectory();
    if (!path)
    {
        return std::string();
    }
    std::string result(path);
    SDL_free(path);
    return result;
}

std::vector<std::string> TVPGetAppStoragePath()
{
    std::vector<std::string> ret;
    ret.emplace_back(TVPGetDefaultFileDir());
    return ret;
}
#endif // !_KRKRSDL3_IOS

bool TVPCheckExistentLocalFile(const ttstr& name)
{
    if (name.IsEmpty()) return false;
    
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(name.c_str(), &info)) {
        return false;
    }
    return info.type == SDL_PATHTYPE_FILE;
}

bool TVPCheckExistentLocalFolder(const ttstr& name)
{
    if (name.IsEmpty()) return false;
    
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(name.c_str(), &info)) {
        return false;
    }
    return info.type == SDL_PATHTYPE_DIRECTORY;
}

std::string TVPSearchPath(const std::string& filename, const std::string& searchpath)
{
    std::vector<std::string> paths;
    std::string pathStr =
        searchpath.empty() ? (SDL_getenv("PATH") ? SDL_getenv("PATH") : "") : searchpath;
    if (pathStr.empty())
        return "";

    std::stringstream ss(pathStr);
    std::string p;
    while (std::getline(ss, p, ';'))
    {
        if (!p.empty())
            paths.push_back(p);
    }

    for (const auto& p : paths)
    {
        std::string fullpath = p;
        if (!fullpath.empty() && fullpath.back() != '/' && fullpath.back() != '\\')
        {
#ifdef _KRKRSDL3_WINDOWS
            fullpath += '\\';
#else
            fullpath += '/';
#endif
        }
        fullpath += filename;

        SDL_PathInfo info;
        if (SDL_GetPathInfo(fullpath.c_str(), &info) && info.type == SDL_PATHTYPE_FILE)
        {
            return fullpath;
        }
    }
    return "";
}

bool TVPDeleteFile(const std::string& filename)
{
    return SDL_RemovePath(filename.c_str());
}

bool TVPDeleteFolder(const std::string& foldername)
{
    return TVPRemoveDirectory(foldername);
}

bool TVPRenameFile(const std::string& from, const std::string& to)
{
    return SDL_RenamePath(from.c_str(), to.c_str());
}

bool TVPCopyFile(const std::string& from, const std::string& to)
{
    SDL_IOStream* fFrom = SDL_IOFromFile(from.c_str(), "rb");
    if (!fFrom)
    {
        return TVPCopyFolder(from, to);
    }
    SDL_IOStream* fTo = SDL_IOFromFile(to.c_str(), "wb");
    if (!fTo)
    {
        SDL_CloseIO(fFrom);
        return false;
    }
    const int bufSize = 1 * 1024 * 1024;
    std::vector<char> buffer(bufSize);
    size_t bytesRead;
    while ((bytesRead = SDL_ReadIO(fFrom, buffer.data(), bufSize)) > 0)
    {
        if (SDL_WriteIO(fTo, buffer.data(), bytesRead) != bytesRead)
        {
            SDL_CloseIO(fFrom);
            SDL_CloseIO(fTo);
            return false;
        }
    }
    if (SDL_GetIOStatus(fFrom) != SDL_IO_STATUS_EOF)
    {
        SDL_CloseIO(fFrom);
        SDL_CloseIO(fTo);
        return false;
    }

    SDL_CloseIO(fFrom);
    SDL_CloseIO(fTo);
    return true;
}

void TVPListDir(const std::string& folder, std::function<void(const std::string&, int)> cb)
{
    std::string dirpath = folder;
    if (!dirpath.empty() && dirpath.back() != '/' && dirpath.back() != '\\')
    {
        dirpath += '/';
    }

    TVPListDirContext ctx;
    ctx.cb = cb;
    SDL_EnumerateDirectory(dirpath.c_str(), TVPListdirCb, &ctx);
}

void TVPGetLocalFileListAt(const ttstr& name,
                           const std::function<void(const ttstr&, tTVPLocalFileInfo*)>& cb)
{
    std::string dirpath = name.AsStdString();
    if (!dirpath.empty() && dirpath.back() != '/' && dirpath.back() != '\\')
    {
        dirpath += '/';
    }

    TVPFileInfoContext ctx;
    ctx.cb = cb;
    SDL_EnumerateDirectory(dirpath.c_str(), TVPFileinfoCb, &ctx);
}

bool TVPCreateFolders(const ttstr& folderttstr)
{
    std::string folder = folderttstr.AsStdString();
    if (folder.empty())
        return true;

    // 检查是否已存在
    SDL_PathInfo info;
    if (SDL_GetPathInfo(folder.c_str(), &info) && info.type == SDL_PATHTYPE_DIRECTORY)
    {
        return true;
    }

    // 递归创建父目录
    size_t pos = folder.find_last_of("/\\");
    if (pos != std::string::npos && pos > 0)
    {
        std::string parent = folder.substr(0, pos);
        // 如果是盘符（如 C:）则跳过
        if (!(parent.size() == 2 && parent[1] == ':'))
        {
            if (!TVPCreateFolders(parent))
            {
                return false;
            }
        }
    }

    return SDL_CreateDirectory(folder.c_str());
}
