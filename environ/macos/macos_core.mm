#include "tjsCommHead.h"
#include "Platform.h"

#import <Foundation/Foundation.h>

#include <string>

//---------------------------------------------------------------------------
std::string TVPGetPackageVersionString()
{
    return "macos";
}
//---------------------------------------------------------------------------
ttstr TVPGetOSName()
{
    @autoreleasepool
    {
        NSProcessInfo* info = [NSProcessInfo processInfo];
        NSOperatingSystemVersion ver = [info operatingSystemVersion];
        NSString* name = [info operatingSystemVersionString];
        if (name)
            return std::string([name UTF8String]);

        char buf[128] = {0};
        snprintf(buf, sizeof(buf), "macOS %ld.%ld.%ld", (long)ver.majorVersion,
                 (long)ver.minorVersion, (long)ver.patchVersion);
        return std::string(buf);
    }
}
