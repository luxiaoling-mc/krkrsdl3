#include "tjsCommHead.h"
#include "Platform.h"
#include "PlatformFile.h"
#include "PlatformVideo.h"

#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>

#include <SDL3/SDL_hints.h>
#include <atomic>
#include <string>

static std::atomic<bool> s_landscapeLocked{false};
extern "C" bool KrkrIsLandscapeLocked(void)
{
    return s_landscapeLocked.load(std::memory_order_acquire);
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
