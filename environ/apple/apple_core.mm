#include "tjsCommHead.h"
#include "Platform.h"
#include "PlatformFile.h"
#include "UtilStreams.h"

#import <Foundation/Foundation.h>

#include <mach/mach.h>
#include <sys/sysctl.h>

#include <string>

//---------------------------------------------------------------------------
tTVPMemoryStream* GetResourceStream(const ttstr& filename)
{
#if defined(_KRKRSDL3_MACOS)
    // macOS CLI build keeps Res beside the executable, matching Linux/Windows.
    // App bundle builds may place resources under Contents/Resources.
    ttstr exeResourceBase = TVPNormalizeStorageName(TVPNativeExeDir) + TJS_N("Res/");
    try
    {
        tTJSBinaryStream* tmp = TVPCreateBinaryStreamForRead(exeResourceBase + filename, 0);
        tTVPMemoryStream* ret = new tTVPMemoryStream(nullptr, tmp->GetSize());
        tmp->ReadBuffer(ret->GetInternalBuffer(), tmp->GetSize());
        delete tmp;
        return ret;
    }
    catch (...)
    {
    }
#endif

    @autoreleasepool
    {
        NSString* resRoot = [[NSBundle mainBundle] resourcePath];
        if (!resRoot)
            return nullptr;

        std::string fn = filename.AsStdString();
        NSString* name = [NSString stringWithUTF8String:fn.c_str()];
        NSString* path = [resRoot stringByAppendingPathComponent:name];
        NSData* data = [NSData dataWithContentsOfFile:path];
        if (!data)
        {
            path = [[resRoot stringByAppendingPathComponent:@"Res"] stringByAppendingPathComponent:name];
            data = [NSData dataWithContentsOfFile:path];
        }
        if (!data)
            return nullptr;

        tTVPMemoryStream* ret = new tTVPMemoryStream(nullptr, (tjs_uint)data.length);
        memcpy(ret->GetInternalBuffer(), data.bytes, data.length);
        return ret;
    }
}
//---------------------------------------------------------------------------
void TVPInvokeMenu(int x, int y, void* _menu)
{
}
//---------------------------------------------------------------------------
void TVPGetMemoryInfo(TVPMemoryInfo& m)
{
    m.MemTotal = 0;
    m.MemFree = 0;
    m.SwapTotal = 0;
    m.SwapFree = 0;
    m.VirtualTotal = 0;
    m.VirtualUsed = 0;

    uint64_t total = 0;
    size_t len = sizeof(total);
    if (sysctlbyname("hw.memsize", &total, &len, nullptr, 0) == 0)
        m.MemTotal = (unsigned long)(total / 1024);

    vm_size_t page = 0;
    host_page_size(mach_host_self(), &page);
    vm_statistics64_data_t vs;
    mach_msg_type_number_t cnt = HOST_VM_INFO64_COUNT;
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64, (host_info64_t)&vs, &cnt) == KERN_SUCCESS)
    {
        uint64_t freeBytes = (uint64_t)(vs.free_count + vs.inactive_count) * page;
        m.MemFree = (unsigned long)(freeBytes / 1024);
    }
}
//---------------------------------------------------------------------------
tjs_int TVPGetSystemFreeMemory()
{
    TVPMemoryInfo m;
    TVPGetMemoryInfo(m);
#if defined(_KRKRSDL3_IOS)
    return (tjs_int)(m.MemFree / 1024 / 2);
#else
    return (tjs_int)(m.MemFree / 1024);
#endif
}
//---------------------------------------------------------------------------
tjs_int TVPGetSelfUsedMemory()
{
    task_vm_info_data_t info;
    mach_msg_type_number_t cnt = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&info, &cnt) == KERN_SUCCESS)
        return (tjs_int)(info.phys_footprint / (1024 * 1024));
    return 0;
}
