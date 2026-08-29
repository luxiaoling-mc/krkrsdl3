#include "tjsCommHead.h"
#include "Platform.h"
#include "PlatformVideo.h"

static const char* TVPAppleVideoBackendName()
{
#if defined(_KRKRSDL3_IOS)
    return "ios_video";
#else
    return "macos_video";
#endif
}

OverlayVideoPlayer* CreateOverlayVideoPlayer(TVPVideoEventCallback cb, void* cbctx)
{
    TVPConsoleLog(TJS_N("%s: system video playback not implemented yet"), TVPAppleVideoBackendName());
    return nullptr;
}

LayerVideoPlayer* CreateLayerVideoPlayer(TVPVideoEventCallback cb, void* cbctx)
{
    TVPConsoleLog(TJS_N("%s: system video playback not implemented yet"), TVPAppleVideoBackendName());
    return nullptr;
}
