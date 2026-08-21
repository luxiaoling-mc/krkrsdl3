
#include "tjsCommHead.h"
#include "Platform.h"
#include "PlatformFile.h"
#include "UtilStreams.h"

#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>

#include <SDL3/SDL_hints.h>
#include <atomic>
#include <string>
#include <sys/sysctl.h>
#include <mach/mach.h>

static std::atomic<bool> s_landscapeLocked{false};
extern "C" bool KrkrIsLandscapeLocked(void)
{
    return s_landscapeLocked.load(std::memory_order_acquire);
}

tTVPMemoryStream* GetResourceStream(const ttstr& filename)
{
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
            path = [[resRoot stringByAppendingPathComponent:@"Res"]
                stringByAppendingPathComponent:name];
            data = [NSData dataWithContentsOfFile:path];
        }
        if (!data)
            return nullptr;

        tTVPMemoryStream* ret = new tTVPMemoryStream(nullptr, (tjs_uint)data.length);
        memcpy(ret->GetInternalBuffer(), data.bytes, data.length);
        return ret;
    }
}

std::string TVPGetPackageVersionString()
{
    return "ios";
}

ttstr TVPGetOSName()
{
    @autoreleasepool
    {
        UIDevice* device = [UIDevice currentDevice];
        std::string name = [[device systemName] UTF8String];
        std::string ver = [[device systemVersion] UTF8String];
        return name + " " + ver;
    }
}

void TVPInvokeMenu(int x, int y, void* _menu)
{

}

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
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64, (host_info64_t)&vs, &cnt) ==
        KERN_SUCCESS)
    {

        uint64_t freeBytes = (uint64_t)(vs.free_count + vs.inactive_count) * page;
        m.MemFree = (unsigned long)(freeBytes / 1024);
    }
}

tjs_int TVPGetSystemFreeMemory()
{
    TVPMemoryInfo m;
    TVPGetMemoryInfo(m);

    return (tjs_int)(m.MemFree / 1024 / 2);
}

tjs_int TVPGetSelfUsedMemory()
{
    task_vm_info_data_t info;
    mach_msg_type_number_t cnt = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&info, &cnt) == KERN_SUCCESS)
        return (tjs_int)(info.phys_footprint / (1024 * 1024));
    return 0;
}

int TVPShowSimpleInputBox(ttstr& text,
                          const ttstr& caption,
                          const ttstr& prompt,
                          const std::vector<ttstr>& vecButtons)
{
    return -1;
}

std::string TVPGetDefaultFileDir()
{
    @autoreleasepool
    {
        NSArray* dirs =
            NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
        NSString* docs = [dirs firstObject];
        return docs ? std::string([docs UTF8String]) : std::string();
    }
}

std::vector<std::string> TVPGetAppStoragePath()
{
    std::vector<std::string> ret;
    ret.emplace_back(TVPGetDefaultFileDir());
    return ret;
}

extern "C" void TVPSetGameRunningOrientation(bool running)
{
    s_landscapeLocked.store(running, std::memory_order_release);
    if (running)
        SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    else
        SDL_SetHint(SDL_HINT_ORIENTATIONS, "");

    dispatch_async(dispatch_get_main_queue(), ^{
        UIWindow* keyWin = nil;
        for (UIScene* scene in UIApplication.sharedApplication.connectedScenes)
        {
            if (![scene isKindOfClass:UIWindowScene.class])
                continue;
            UIWindowScene* ws = (UIWindowScene*)scene;
            for (UIWindow* w in ws.windows)
            {
                if (w.isKeyWindow)
                {
                    keyWin = w;
                    break;
                }
            }
            if (!keyWin && ws.windows.count)
                keyWin = ws.windows.firstObject;
            if (keyWin)
                break;
        }
        if (!keyWin)
            return;
        UIViewController* root = keyWin.rootViewController;
#if __IPHONE_OS_VERSION_MAX_ALLOWED >= 160000

        if (@available(iOS 16.0, *))
        {
            [root setNeedsUpdateOfSupportedInterfaceOrientations];
            UIWindowScene* scene = keyWin.windowScene;
            if (scene)
            {
                UIInterfaceOrientationMask mask =
                    running ? UIInterfaceOrientationMaskLandscape : UIInterfaceOrientationMaskAll;
                UIWindowSceneGeometryPreferencesIOS* pref =
                    [[UIWindowSceneGeometryPreferencesIOS alloc]
                        initWithInterfaceOrientations:mask];
                [scene requestGeometryUpdateWithPreferences:pref
                                               errorHandler:^(NSError* e){}];
            }
        }
        else
#endif
        {

            [UIViewController attemptRotationToDeviceOrientation];
        }
    });
}
