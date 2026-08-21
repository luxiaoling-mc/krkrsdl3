
#include "tjsCommHead.h"
#include "Platform.h"
#include "PlatformVideo.h"

OverlayVideoPlayer* CreateOverlayVideoPlayer(TVPVideoEventCallback cb, void* cbctx)
{
    TVPConsoleLog(TJS_N("ios_video: video playback not implemented yet (v1)"));
    return nullptr;
}

LayerVideoPlayer* CreateLayerVideoPlayer(TVPVideoEventCallback cb, void* cbctx)
{
    TVPConsoleLog(TJS_N("ios_video: video playback not implemented yet (v1)"));
    return nullptr;
}
