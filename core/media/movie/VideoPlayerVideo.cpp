#include "tjsCommHead.h"
#include "VideoPlayerVideo.h"

#include "VideoReferenceClock.h"
extern "C"
{
#include "libavformat/avformat.h"
}

#include "MessageQueue.h"
#include "TVPApplication.h"
#include "CodecVideoFFmpeg.h"

NS_KRMOVIE_BEGIN

class CDVDMsgVideoCodecChange : public CDVDMsg
{
public:
    CDVDMsgVideoCodecChange(const CDVDStreamInfo& hints, CDVDVideoCodec* codec)
      : CDVDMsg(GENERAL_STREAMCHANGE),
        m_codec(codec),
        m_hints(hints)
    {
    }
    ~CDVDMsgVideoCodecChange() { delete m_codec; }
    CDVDVideoCodec* m_codec;
    CDVDStreamInfo m_hints;
};

CVideoPlayerVideo::CVideoPlayerVideo(CDVDClock* pClock,
                                     CDVDMessageQueue& parent,
                                     CBaseRenderer* renderer)
  : tTVPThread("TVPVideoPlayerVideo"),
    IDVDStreamPlayerVideo(),
    m_messageQueue("video"),
    m_messageParent(parent),
    m_pRenderer(renderer),
    m_bTriggerUpdateResolution(false),
    m_bRenderGUI(true),
    m_waitForBufferCount(0),
    m_rendermethod(0),
    m_renderDebug(false),
    m_renderState(STATE_UNCONFIGURED),
    m_displayLatency(0.0),
    m_QueueSize(2),
    m_QueueSkip(0),
    m_width(0),
    m_height(0),
    m_dwidth(0),
    m_dheight(0),
    m_fps(0.0f),
    m_extended_format(0),
    m_orientation(0),
    m_NumberBuffers(0),
    m_lateframes(-1),
    m_presentpts(0.0),
    m_presentstep(PRESENT_IDLE),
    m_forceNext(false),
    m_presentsource(0),
    m_videoDelay(0)
{
    m_pClock = pClock;
    m_pTempOverlayPicture = NULL;
    m_pVideoCodec = NULL;
    m_speed = DVD_PLAYSPEED_NORMAL;

    m_bRenderSubs = false;
    m_stalled = false;
    m_paused = false;
    m_syncState = IDVDStreamPlayer::SYNC_STARTING;
    m_iSubtitleDelay = 0;
    m_iLateFrames = 0;
    m_iDroppedRequest = 0;
    m_fForcedAspectRatio = 0;
    m_messageQueue.SetMaxDataSize(40 * 1024 * 1024);
    m_messageQueue.SetMaxTimeSize(8.0);

    m_iDroppedFrames = 0;
    m_bAllowDrop = false;
    m_bFpsInvalid = false;
}

CVideoPlayerVideo::~CVideoPlayerVideo()
{
    m_bAbortOutput = true;
    StopThread();
}

double CVideoPlayerVideo::GetOutputDelay()
{
    double time = m_messageQueue.GetPacketCount(CDVDMsg::DEMUXER_PACKET);
    if (m_fFrameRate)
        time = (time * DVD_TIME_BASE) / m_fFrameRate;
    else
        time = 0.0;

    if (m_speed != 0)
        time = time * DVD_PLAYSPEED_NORMAL / abs(m_speed);

    return time;
}

bool CVideoPlayerVideo::OpenStream(CDVDStreamInfo& hint)
{
    if (hint.flags & AV_DISPOSITION_ATTACHED_PIC)
        return false;

    CDVDVideoCodec* codec = new CDVDVideoCodecFFmpeg();
    if (!codec->Open(hint))
    {
        delete codec;
        return false;
    }

    if (m_messageQueue.IsInited())
        m_messageQueue.Put(new CDVDMsgVideoCodecChange(hint, codec), 0);
    else
    {
        OpenStream(hint, codec);
        m_messageQueue.Init();
        Resume();
    }
    return true;
}

void CVideoPlayerVideo::OpenStream(CDVDStreamInfo& hint, CDVDVideoCodec* codec)
{
    if (hint.fpsrate && hint.fpsscale)
    {
        m_fFrameRate = hint.fpsrate / hint.fpsscale;
        m_bFpsInvalid = false;
    }
    else
    {
        m_fFrameRate = 25;
        m_bFpsInvalid = true;
    }

    ResetFrameRateCalc();

    m_iDroppedRequest = 0;
    m_iLateFrames = 0;

    if (m_fFrameRate > 120 || m_fFrameRate < 5)
    {
        m_fFrameRate = 25;
    }

    // use aspect in stream if available
    if (hint.forced_aspect)
        m_fForcedAspectRatio = hint.aspect;
    else
        m_fForcedAspectRatio = 0.0;

    if (m_pVideoCodec)
    {
        delete m_pVideoCodec;
    }
    m_pVideoCodec = codec;
    m_hints = hint;
    m_stalled = m_messageQueue.GetPacketCount(CDVDMsg::DEMUXER_PACKET) == 0;
    m_rewindStalled = false;
    m_packets.clear();
    m_syncState = IDVDStreamPlayer::SYNC_STARTING;
}

void CVideoPlayerVideo::CloseStream(bool bWaitForBuffers)
{
    // wait until buffers are empty
    if (bWaitForBuffers && m_speed > 0)
    {
        m_messageQueue.Put(new CDVDMsg(CDVDMsg::VIDEO_DRAIN), 0);
        m_messageQueue.WaitUntilEmpty();
    }

    m_messageQueue.Abort();

    m_bAbortOutput = true;
    StopThread();

    m_messageQueue.End();

    if (m_pVideoCodec)
    {
        delete m_pVideoCodec;
        m_pVideoCodec = NULL;
    }

    if (m_pTempOverlayPicture)
    {
        av_free(m_pTempOverlayPicture->data[0]);
        delete m_pTempOverlayPicture;
        m_pTempOverlayPicture = NULL;
    }
}

void CVideoPlayerVideo::VideoParamsChange()
{
    m_messageParent.Put(new CDVDMsg(CDVDMsg::PLAYER_AVCHANGE));
}

void CVideoPlayerVideo::GetDebugInfo(std::string& audio, std::string& video, std::string& general)
{
}

void CVideoPlayerVideo::UpdateClockSync(bool enabled)
{
}

bool CVideoPlayerVideo::AcceptsData()
{
    bool full = m_messageQueue.IsFull();
    return !full;
}

void CVideoPlayerVideo::Execute()
{
    memset(&m_picture, 0, sizeof(DVDVideoPicture));
    double pts = 0;
    double frametime = (double)DVD_TIME_BASE / m_fFrameRate;

    int iDropped = 0; // frames dropped in a row
    bool bRequestDrop = false;
    int iDropDirective;

    m_droppingStats.Reset();
    m_iDroppedFrames = 0;
    m_rewindStalled = false;

    while (!GetTerminated())
    {
        int iQueueTimeOut = (int)(m_stalled ? frametime : frametime * 10) / 1000;
        int iPriority =
            (m_speed == DVD_PLAYSPEED_PAUSE && m_syncState == IDVDStreamPlayer::SYNC_INSYNC) ? 1
                                                                                             : 0;

        if (m_syncState == IDVDStreamPlayer::SYNC_WAITSYNC)
            iPriority = 1;

        if (m_paused)
            iPriority = 1;

        CDVDMsg* pMsg;
        MsgQueueReturnCode ret = m_messageQueue.Get(&pMsg, iQueueTimeOut, iPriority);

        if (MSGQ_IS_ERROR(ret))
        {
            break;
        }
        else if (ret == MSGQ_TIMEOUT)
        {
            // if we only wanted priority messages, this isn't a stall
            if (iPriority)
                continue;

            // check if decoder has produced some output
            ProcessDecoderOutput(frametime, pts);

            // Okey, start rendering at stream fps now instead, we are likely in a stillframe
            if (!m_stalled)
            {
                m_stalled = true;
                pts += frametime * 4;
            }

            // Waiting timed out, output last picture
            if (m_picture.iFlags & DVP_FLAG_ALLOCATED)
            {
                OutputPicture(&m_picture, pts);
                pts += frametime;
            }

            continue;
        }

        if (pMsg->IsType(CDVDMsg::GENERAL_SYNCHRONIZE))
        {
            if (((CDVDMsgGeneralSynchronize*)pMsg)->Wait(100, SYNCSOURCE_VIDEO))
            {
            }
            else
                m_messageQueue.Put(
                    pMsg->AddRef(),
                    1); /* push back as prio message, to process other prio messages */
            m_droppingStats.Reset();
        }
        else if (pMsg->IsType(CDVDMsg::GENERAL_RESYNC))
        {
            pts = static_cast<CDVDMsgDouble*>(pMsg)->m_value;

            m_syncState = IDVDStreamPlayer::SYNC_INSYNC;
            m_droppingStats.Reset();
            m_rewindStalled = false;
        }
        else if (pMsg->IsType(CDVDMsg::VIDEO_SET_ASPECT))
        {
            m_fForcedAspectRatio = *((CDVDMsgDouble*)pMsg);
        }
        else if (pMsg->IsType(CDVDMsg::GENERAL_RESET))
        {
            if (m_pVideoCodec)
                m_pVideoCodec->Reset();
            m_picture.iFlags &= ~DVP_FLAG_ALLOCATED;
            m_packets.clear();
            m_droppingStats.Reset();
            m_syncState = IDVDStreamPlayer::SYNC_STARTING;
            m_rewindStalled = false;
        }
        else if (pMsg->IsType(CDVDMsg::GENERAL_FLUSH))
        {
            bool sync = static_cast<CDVDMsgBool*>(pMsg)->m_value;
            if (m_pVideoCodec)
                m_pVideoCodec->Reset();
            m_picture.iFlags &= ~DVP_FLAG_ALLOCATED;
            m_packets.clear();
            pts = 0;
            m_rewindStalled = false;

            ResetFrameRateCalc();
            m_droppingStats.Reset();

            m_stalled = true;
            if (sync)
                m_syncState = IDVDStreamPlayer::SYNC_STARTING;

            DiscardBuffer();
        }
        else if (pMsg->IsType(CDVDMsg::PLAYER_SETSPEED))
        {
            m_speed = static_cast<CDVDMsgInt*>(pMsg)->m_value;
            m_droppingStats.Reset();
        }
        else if (pMsg->IsType(CDVDMsg::GENERAL_STREAMCHANGE))
        {
            CDVDMsgVideoCodecChange* msg(static_cast<CDVDMsgVideoCodecChange*>(pMsg));

            while (!GetTerminated() && m_pVideoCodec)
            {
                if (!ProcessDecoderOutput(frametime, pts))
                    break;
            }

            OpenStream(msg->m_hints, msg->m_codec);
            msg->m_codec = NULL;
            m_picture.iFlags &= ~DVP_FLAG_ALLOCATED;
        }
        else if (pMsg->IsType(CDVDMsg::VIDEO_DRAIN))
        {
            while (!GetTerminated() && m_pVideoCodec)
            {
                if (!ProcessDecoderOutput(frametime, pts))
                    break;
            }
        }
        else if (pMsg->IsType(CDVDMsg::GENERAL_PAUSE))
        {
            m_paused = static_cast<CDVDMsgBool*>(pMsg)->m_value;
        }
        else if (pMsg->IsType(CDVDMsg::DEMUXER_PACKET))
        {
            DemuxPacket* pPacket = ((CDVDMsgDemuxerPacket*)pMsg)->GetPacket();
            bool bPacketDrop = ((CDVDMsgDemuxerPacket*)pMsg)->GetPacketDrop();
            if (m_stalled)
            {
                m_stalled = false;
            }

            bRequestDrop = false;
            iDropDirective = CalcDropRequirement(pts);
            if (iDropDirective & EOS_VERYLATE)
            {
                if (m_bAllowDrop)
                {
                    bRequestDrop = true;
                }
            }
            if (iDropDirective & EOS_DROPPED)
            {
                m_iDroppedFrames++;
                iDropped++;
            }

            if (m_messageQueue.GetDataSize() == 0 || m_speed < 0)
            {
                bRequestDrop = false;
                m_iDroppedRequest = 0;
                m_iLateFrames = 0;
            }

            // if player want's us to drop this packet, do so nomatter what
            if (bPacketDrop)
                bRequestDrop = true;

            // tell codec if next frame should be dropped
            // problem here, if one packet contains more than one frame
            // both frames will be dropped in that case instead of just the first
            // decoder still needs to provide an empty image structure, with correct flags
            m_pVideoCodec->SetDropState(bRequestDrop);

            if (m_pVideoCodec->AddData(*pPacket))
            {
                // buffer packets so we can recover should decoder flush for some reason
                if (m_pVideoCodec->GetConvergeCount() > 0)
                {
                    m_packets.emplace_back(pMsg, 0);
                    if (m_packets.size() > m_pVideoCodec->GetConvergeCount() ||
                        m_packets.size() * frametime > DVD_SEC_TO_TIME(10))
                        m_packets.pop_front();
                }

                ProcessDecoderOutput(frametime, pts);
            }
            else
            {
                // decoder input queue is full (EAGAIN): drain output first so the
                // decoder can accept this packet on the next loop iteration
                if (!ProcessDecoderOutput(frametime, pts))
                    Sleep(1);
                pMsg->AddRef();
                m_messageQueue.Put(pMsg, 0, false);
            }
        }

        // all data is used by the decoder, we can safely free it now
        pMsg->Release();
    }
}

bool CVideoPlayerVideo::ProcessDecoderOutput(double& frametime, double& pts)
{
    if (!m_pVideoCodec->GetPicture(&m_picture))
        return false;

    bool hasTimestamp = true;

    if (m_picture.iDuration == 0.0)
        m_picture.iDuration = frametime;

    // validate picture timing,
    // if both dts/pts invalid, use pts calulated from picture.iDuration
    // if pts invalid use dts, else use picture.pts as passed
    if (m_picture.dts == DVD_NOPTS_VALUE && m_picture.pts == DVD_NOPTS_VALUE)
    {
        m_picture.pts = pts;
        hasTimestamp = false;
    }
    else if (m_picture.pts == DVD_NOPTS_VALUE)
        m_picture.pts = m_picture.dts;

    /* use forced aspect if any */
    if (m_fForcedAspectRatio != 0.0f)
        m_picture.iDisplayWidth = (int)(m_picture.iDisplayHeight * m_fForcedAspectRatio);

    /* if frame has a pts (usually originiating from demux packet), use that */
    if (m_picture.pts != DVD_NOPTS_VALUE)
    {
        pts = m_picture.pts;
    }

    double extraDelay = 0.0;
    if (m_picture.iRepeatPicture)
    {
        extraDelay = m_picture.iRepeatPicture * m_picture.iDuration;
        m_picture.iDuration += extraDelay;
    }

    int iResult = OutputPicture(&m_picture, pts + extraDelay);

    frametime = (double)DVD_TIME_BASE / m_fFrameRate;

    if (m_syncState == IDVDStreamPlayer::SYNC_STARTING && !(iResult & EOS_DROPPED))
    {
        m_syncState = IDVDStreamPlayer::SYNC_WAITSYNC;
        SStartMsg msg;
        msg.player = VideoPlayer_VIDEO;
        msg.cachetime = DVD_MSEC_TO_TIME(50);
        msg.cachetotal = DVD_MSEC_TO_TIME(100);
        msg.timestamp = hasTimestamp ? pts : DVD_NOPTS_VALUE;
        m_messageParent.Put(new CDVDMsgType<SStartMsg>(CDVDMsg::PLAYER_STARTED, msg));
    }

    // guess next frame pts. iDuration is always valid
    if (m_speed != 0)
        pts += m_picture.iDuration * m_speed / abs(m_speed);

    if (iResult & EOS_ABORT)
    {
        // if we break here and we directly try to decode again wihout
        // flushing the video codec things break for some reason
        // i think the decoder (libmpeg2 atleast) still has a pointer
        // to the data, and when the packet is freed that will fail.
        return false;
    }

    if ((iResult & EOS_DROPPED) && !(m_picture.iFlags & DVP_FLAG_DROPPED))
    {
        m_iDroppedFrames++;
    }

    return true;
}

void CVideoPlayerVideo::OnExit()
{
}

void CVideoPlayerVideo::SetSpeed(int speed)
{
    if (m_messageQueue.IsInited())
        m_messageQueue.Put(new CDVDMsgInt(CDVDMsg::PLAYER_SETSPEED, speed), 1);
    else
        m_speed = speed;
}

void CVideoPlayerVideo::Flush(bool sync)
{
    /* flush using message as this get's called from VideoPlayer thread */
    /* and any demux packet that has been taken out of queue need to */
    /* be disposed of before we flush */
    m_messageQueue.Flush();
    m_messageQueue.Put(new CDVDMsgBool(CDVDMsg::GENERAL_FLUSH, sync), 1);
    m_bAbortOutput = true;
}

std::string CVideoPlayerVideo::GetStereoMode()
{
    std::string stereo_mode;
    return stereo_mode;
}

int CVideoPlayerVideo::OutputPicture(const DVDVideoPicture* src, double pts)
{
    m_bAbortOutput = false;

    /* picture buffer is not allowed to be modified in this call */
    DVDVideoPicture picture(*src);
    DVDVideoPicture* pPicture = &picture;

    if (!Configure(picture, 0.0 /*config_framerate*/, 0, m_hints.orientation, 0))
    {
        return EOS_ABORT;
    }

    int result = 0;

    if (!m_stalled)
        CalcFrameRate();

    m_pClock->UpdateFramerate(m_fFrameRate);

    // calculate the time we need to delay this picture before displaying
    double iPlayingClock, iCurrentClock;

    iPlayingClock = m_pClock->GetClock(iCurrentClock); // snapshot current clock

    if (m_speed < 0)
    {
        double renderPts;
        int queued, discard;
        int lateframes;
        double inputPts = m_droppingStats.m_lastPts;
        GetStats(lateframes, renderPts, queued, discard);
        if (pts > renderPts || queued > 0)
        {
            if (inputPts >= renderPts)
            {
                m_rewindStalled = true;
                Sleep(50);
            }
            return result | EOS_DROPPED;
        }
        else if (pts < iPlayingClock)
        {
            return result | EOS_DROPPED;
        }
    }
    else if (m_speed > DVD_PLAYSPEED_NORMAL)
    {
        double renderPts;
        int lateframes;
        int bufferLevel, queued, discard;
        GetStats(lateframes, renderPts, queued, discard);
        bufferLevel = queued + discard;

        // estimate the time it will take for the next frame to get rendered
        // drop the frame if it's late in regard to this estimation
        double diff = pts - renderPts;
        double mindiff = DVD_SEC_TO_TIME(1 / m_fFrameRate) * (bufferLevel + 1);
        if (diff < mindiff)
        {
            m_droppingStats.AddOutputDropGain(pts, 1);
            return result | EOS_DROPPED;
        }
    }

    if ((pPicture->iFlags & DVP_FLAG_DROPPED))
    {
        m_droppingStats.AddOutputDropGain(pts, 1);
        return result | EOS_DROPPED;
    }

    int timeToDisplay = DVD_TIME_TO_MSEC(pts - iPlayingClock);
    // make sure waiting time is not negative
    int maxWaitTime = std::min(std::max(timeToDisplay + 500, 50), 500);
    // don't wait when going ff
    if (m_speed > DVD_PLAYSPEED_NORMAL)
        maxWaitTime = std::max(timeToDisplay, 0);
    int buffer = WaitForBuffer(m_bAbortOutput, maxWaitTime);
    if (buffer < 0)
    {
        m_droppingStats.AddOutputDropGain(pts, 1);
        return EOS_DROPPED;
    }

    int index = AddVideoPicture(*pPicture);

    // video device might not be done yet
    while (index < 0 && !m_bAbortOutput &&
           m_pClock->GetAbsoluteClock() < iCurrentClock + DVD_MSEC_TO_TIME(500))
    {
        Sleep(1);
        index = AddVideoPicture(*pPicture);
    }

    if (index < 0)
    {
        m_droppingStats.AddOutputDropGain(pts, 1);
        return EOS_DROPPED;
    }

    FlipPage(m_bAbortOutput, pts, (m_syncState == ESyncState::SYNC_STARTING));

    return result;
}

std::string CVideoPlayerVideo::GetPlayerInfo()
{
    return "";
}

int CVideoPlayerVideo::GetVideoBitrate()
{
    return 0;
}

void CVideoPlayerVideo::ResetFrameRateCalc()
{
}

double CVideoPlayerVideo::GetCurrentPts()
{
    double renderPts;
    int sleepTime;
    int queued, discard;

    // get render stats
    GetStats(sleepTime, renderPts, queued, discard);

    if (renderPts == DVD_NOPTS_VALUE)
        return DVD_NOPTS_VALUE;
    else if (m_stalled)
        return DVD_NOPTS_VALUE;
    else if (m_speed == DVD_PLAYSPEED_NORMAL)
    {
        if (renderPts < 0)
            renderPts = 0;
    }
    return renderPts;
}

#define MAXFRAMERATEDIFF 0.01
#define MAXFRAMESERR 1000

void CVideoPlayerVideo::CalcFrameRate()
{
}

int CVideoPlayerVideo::CalcDropRequirement(double pts)
{
    int result = 0;
    int lateframes;
    double iDecoderPts, iRenderPts;
    int iDroppedFrames = -1;
    int iBufferLevel;
    int queued, discard;

    m_droppingStats.m_lastPts = pts;

    // get decoder stats
    if (!m_pVideoCodec->GetCodecStats(iDecoderPts, iDroppedFrames))
        iDecoderPts = pts;
    if (iDecoderPts == DVD_NOPTS_VALUE)
        iDecoderPts = pts;

    // get render stats
    GetStats(lateframes, iRenderPts, queued, discard);
    iBufferLevel = queued + discard;

    if (iBufferLevel < 0)
        result |= EOS_BUFFER_LEVEL;
    else if (iBufferLevel < 2)
    {
        result |= EOS_BUFFER_LEVEL;
    }

    if (m_bAllowDrop)
    {
        if (iDroppedFrames > 0)
        {
            CDroppingStats::CGain gain;
            gain.frames = iDroppedFrames;
            gain.pts = iDecoderPts;
            m_droppingStats.m_gain.push_back(gain);
            m_droppingStats.m_totalGain += iDroppedFrames;
            result |= EOS_DROPPED;
        }
    }

    // subtract gains
    while (!m_droppingStats.m_gain.empty() && iRenderPts >= m_droppingStats.m_gain.front().pts)
    {
        m_droppingStats.m_totalGain -= m_droppingStats.m_gain.front().frames;
        m_droppingStats.m_gain.pop_front();
    }

    // calculate lateness
    int lateness = lateframes - m_droppingStats.m_totalGain;

    if (lateness > 0 && m_speed)
    {
        result |= EOS_VERYLATE;
    }
    return result;
}

void CDroppingStats::Reset()
{
    m_gain.clear();
    m_totalGain = 0;
}

void CDroppingStats::AddOutputDropGain(double pts, int frames)
{
    CDroppingStats::CGain gain;
    gain.frames = frames;
    gain.pts = pts;
    m_gain.push_back(gain);
    m_totalGain += frames;
}

float CVideoPlayerVideo::GetAspectRatio()
{
    return 1.0f;
}

bool CVideoPlayerVideo::Configure(
    DVDVideoPicture& picture, float fps, unsigned flags, unsigned int orientation, int buffers)
{

    // check if something has changed
    {
        tTJSUniqueLock lock(m_statelock);

        if (m_width == picture.iWidth && m_height == picture.iHeight &&
            m_dwidth == picture.iDisplayWidth && m_dheight == picture.iDisplayHeight &&
            m_fps == fps && m_extended_format == picture.extended_format &&
            m_orientation == orientation && m_NumberBuffers == buffers && m_pRenderer != NULL)
        {
            return true;
        }
    }

    // make sure any queued frame was fully presented
    {
        tTJSUniqueLock lock(m_presentlock);
        TVPElapsedTimer endtime(5000);
        while (m_presentstep != PRESENT_IDLE)
        {
            if (endtime.IsTimePast())
            {
                return false;
            }
            m_presentevent.WaitFor(m_presentlock, endtime.MillisLeft());
        }
    }

    {
        tTJSUniqueLock lock(m_statelock);
        m_width = picture.iWidth;
        m_height = picture.iHeight, m_dwidth = picture.iDisplayWidth;
        m_dheight = picture.iDisplayHeight;
        m_fps = fps;
        m_flags = flags;
        m_extended_format = picture.extended_format;
        m_orientation = orientation;
        m_NumberBuffers = buffers;
        m_renderState = STATE_CONFIGURING;

        CheckEnableClockSync();

        tTJSUniqueLock lock2(m_presentlock);
        m_presentstep = PRESENT_READY;
        m_presentevent.notify_all();
    }

    return true;

    tTJSUniqueLock stateLock(m_stateMutex);
    if (!m_stateEvent.WaitFor(m_stateMutex, 1000))
    {
        return false;
    }

    tTJSUniqueLock lock(m_statelock);
    if (m_renderState != STATE_CONFIGURED)
    {
        return false;
    }

    return true;
}

bool CVideoPlayerVideo::Configure()
{
    // lock all interfaces
    tTJSUniqueLock lock(m_statelock);
    tTJSUniqueLock lock2(m_presentlock);
    tTJSUniqueLock lock3(m_datalock);

    if (!m_pRenderer)
    {
        CreateRenderer();
        if (!m_pRenderer)
            return false;
    }

    m_queued.clear();
    m_discard.clear();
    m_free.clear();
    m_presentsource = 0;
    for (int i = 1; i < m_QueueSize; i++)
        m_free.push_back(i);

    m_bRenderGUI = true;
    m_waitForBufferCount = 0;
    m_bTriggerUpdateResolution = true;
    m_presentstep = PRESENT_IDLE;
    m_presentpts = DVD_NOPTS_VALUE;
    m_lateframes = -1.0;
    m_presentevent.notify_all();
    m_renderDebug = false;
    m_clockSync.Reset();

    m_renderState = STATE_CONFIGURED;

    m_stateEvent.notify_all();
    VideoParamsChange();
    return true;
}

bool CVideoPlayerVideo::IsConfigured()
{
    tTJSUniqueLock lock(m_statelock);
    if (m_renderState == STATE_CONFIGURED)
        return true;
    else
        return false;
}

void CVideoPlayerVideo::FrameWait(int ms)
{
    TVPElapsedTimer timeout(ms);
    tTJSUniqueLock lock(m_presentlock);
    while (m_presentstep == PRESENT_IDLE && !timeout.IsTimePast())
        m_presentevent.WaitFor(m_presentlock, timeout.MillisLeft());
}

bool CVideoPlayerVideo::HasFrame()
{
    if (!IsConfigured())
        return false;

    tTJSUniqueLock lock(m_presentlock);
    if (m_presentstep == PRESENT_READY || m_presentstep == PRESENT_FRAME ||
        m_presentstep == PRESENT_FRAME2)
        return true;
    else
        return false;
}

void CVideoPlayerVideo::FrameMove()
{
    {
        tTJSUniqueLock lock(m_statelock);

        if (m_renderState == STATE_UNCONFIGURED)
            return;
        else if (m_renderState == STATE_CONFIGURING)
        {
            lock.unlock();
            if (!Configure())
                return;

            FrameWait(50);
        }
    }
    {
        tTJSUniqueLock lock2(m_presentlock);

        if (m_queued.empty())
        {
            m_presentstep = PRESENT_IDLE;
        }

        if (m_presentstep == PRESENT_READY)
            PrepareNextRender();

        if (m_presentstep == PRESENT_FLIP)
        {
            m_presentstep = PRESENT_FRAME;
            m_presentevent.notify_all();
        }

        // release all previous
        for (std::deque<int>::iterator it = m_discard.begin(); it != m_discard.end();)
        {
            // renderer may want to keep the frame for postprocessing
            if (!m_bRenderGUI)
            {
                m_free.push_back(*it);
                it = m_discard.erase(it);
            }
            else
                ++it;
        }

        m_bRenderGUI = true;
    }
}

bool CVideoPlayerVideo::Flush()
{
    if (!m_pRenderer)
        return true;

    tTJSUniqueLock lock(m_statelock);
    tTJSUniqueLock lock2(m_presentlock);
    tTJSUniqueLock lock3(m_datalock);

    if (m_pRenderer)
    {
        m_pRenderer->Flush();

        m_queued.clear();
        m_discard.clear();
        m_free.clear();
        m_presentsource = 0;
        m_presentstep = PRESENT_IDLE;
        for (int i = 1; i < m_QueueSize; i++)
            m_free.push_back(i);

        m_flushEvent.Set();
    }
    return true;
}

void CVideoPlayerVideo::DeleteRenderer()
{
    if (m_pRenderer)
    {
        m_pRenderer = NULL;
    }
}

void CVideoPlayerVideo::SetViewMode(int iViewMode)
{
    tTJSUniqueLock lock(m_statelock);
    VideoParamsChange();
}

void CVideoPlayerVideo::FlipPage(volatile std::atomic_bool& bStop, double pts, bool wait)
{
    {
        tTJSUniqueLock lock(m_statelock);

        if (bStop)
            return;

        if (!m_pRenderer)
            return;
    }

    EPRESENTMETHOD presentmethod;
    tTJSUniqueLock lock(m_presentlock);

    if (m_free.empty())
        return;

    int source = m_free.front();

    SPresent& m = m_Queue[source];
    m.pts = pts;

    m_queued.push_back(m_free.front());
    m_free.pop_front();

    if (m_presentstep == PRESENT_IDLE)
    {
        m_presentstep = PRESENT_READY;
        m_presentevent.notify_all();
    }

    if (wait)
    {
        m_forceNext = true;
        TVPElapsedTimer endtime(200);
        while (m_presentstep == PRESENT_READY)
        {
            m_presentevent.WaitFor(m_presentlock, 20);
            if (endtime.IsTimePast() || bStop)
            {
                break;
            }
        }
        m_forceNext = false;
    }
}

void CVideoPlayerVideo::Render(bool clear, uint32_t flags, uint32_t alpha, bool gui)
{
    {
        tTJSUniqueLock lock(m_statelock);
        if (m_renderState != STATE_CONFIGURED)
            return;
    }

    if (!gui)
        return;

    SPresent& m = m_Queue[m_presentsource];

    if (gui)
    {
        if (m_renderDebug)
        {
            std::string audio, video, player, vsync;

            GetDebugInfo(audio, video, player);

            double refreshrate, clockspeed;
            int missedvblanks;
            m_debugTimer.Set(1000);
        }
    }

    m = m_Queue[m_presentsource];

    {
        tTJSUniqueLock lock(m_presentlock);

        if (m_presentstep == PRESENT_FRAME)
        {
            if (m.presentmethod == PRESENT_METHOD_BOB || m.presentmethod == PRESENT_METHOD_WEAVE)
                m_presentstep = PRESENT_FRAME2;
            else
                m_presentstep = PRESENT_IDLE;
        }
        else if (m_presentstep == PRESENT_FRAME2)
            m_presentstep = PRESENT_IDLE;

        if (m_presentstep == PRESENT_IDLE)
        {
            if (!m_queued.empty())
                m_presentstep = PRESENT_READY;
        }

        m_presentevent.notify_all();
    }
}

bool CVideoPlayerVideo::IsGuiLayer()
{
    {
        tTJSUniqueLock lock(m_statelock);

        if (!m_pRenderer)
            return false;

        if ((HasFrame()) /*||
		m_renderedOverlay || m_overlays.HasOverlay(m_presentsource)*/)
            return true;

        if (m_renderDebug && m_debugTimer.IsTimePast())
            return true;
    }
    return false;
}

bool CVideoPlayerVideo::IsVideoLayer()
{
    {
        tTJSUniqueLock lock(m_statelock);

        if (!m_pRenderer)
            return false;
    }
    return false;
}

void CVideoPlayerVideo::UpdateDisplayLatency()
{
    m_displayLatency = 0; // (double)g_advancedSettings.GetDisplayLatency(refresh);

    int buffers = 2 /*g_Windowing.NoOfBuffers()*/;
    m_displayLatency += (buffers - 1) / 60;
}

void CVideoPlayerVideo::TriggerUpdateResolution(float fps, int width, int flags)
{
    if (width)
    {
        m_fps = fps;
        m_width = width;
        m_flags = flags;
    }
    m_bTriggerUpdateResolution = true;
}

void CVideoPlayerVideo::ToggleDebug()
{
    m_renderDebug = !m_renderDebug;
    m_debugTimer.SetExpired();
}

int CVideoPlayerVideo::AddVideoPicture(DVDVideoPicture& pic)
{
    return m_pRenderer->AddVideoPicture(pic, 0);
}

int CVideoPlayerVideo::WaitForBuffer(volatile std::atomic_bool& bStop, int timeout)
{
    return m_pRenderer->WaitForBuffer(bStop, timeout);
}

void CVideoPlayerVideo::PrepareNextRender()
{
    if (m_queued.empty())
    {
        m_presentstep = PRESENT_IDLE;
        m_presentevent.notify_all();
        return;
    }

    double frameOnScreen = m_pClock->GetClock();
    double frametime = 1.0 / 60 * DVD_TIME_BASE;

    // correct display latency
    // internal buffers of driver, assume that driver lets us go one frame in advance
    double totalLatency =
        DVD_SEC_TO_TIME(m_displayLatency) - DVD_MSEC_TO_TIME(m_videoDelay) + 2 * frametime;

    double renderPts = frameOnScreen + totalLatency;

    double nextFramePts = m_Queue[m_queued.front()].pts;
    if (m_pClock->GetClockSpeed() < 0)
        nextFramePts = renderPts;

    if (m_clockSync.m_enabled)
    {
        double err = fmod(renderPts - nextFramePts, frametime);
        m_clockSync.m_error += err;
        m_clockSync.m_errCount++;
        if (m_clockSync.m_errCount > 30)
        {
            double average = m_clockSync.m_error / m_clockSync.m_errCount;
            m_clockSync.m_syncOffset = average;
            m_clockSync.m_error = 0;
            m_clockSync.m_errCount = 0;
        }
        renderPts += frametime / 2 - m_clockSync.m_syncOffset;
    }

    if (renderPts >= nextFramePts || m_forceNext)
    {
        // see if any future queued frames are already due
        auto iter = m_queued.begin();
        int idx = *iter;
        ++iter;
        while (iter != m_queued.end())
        {
            // the slot for rendering in time is [pts .. (pts +  x * frametime)]
            // renderer/drivers have internal queues, being slightliy late here does not mean that
            // we are really late. The likelihood that we recover decreases the greater m_lateframes
            // get. Skipping a frame is easier than having decoder dropping one (lateframes > 10)
            double x = (m_lateframes <= 6) ? 0.98 : 0;
            if (renderPts < m_Queue[*iter].pts + x * frametime)
                break;
            idx = *iter;
            ++iter;
        }

        // skip late frames
        while (m_queued.front() != idx)
        {
            m_discard.push_back(m_queued.front());
            m_queued.pop_front();
            m_QueueSkip++;
        }

        int lateframes = (renderPts - m_Queue[idx].pts) * m_fps / DVD_TIME_BASE;
        if (lateframes)
            m_lateframes += lateframes;
        else
            m_lateframes = 0;

        m_presentstep = PRESENT_FLIP;
        m_discard.push_back(m_presentsource);
        m_presentsource = idx;
        m_queued.pop_front();
        m_presentpts = m_Queue[idx].pts - totalLatency;
        m_presentevent.notify_all();
    }
}

void CVideoPlayerVideo::DiscardBuffer()
{
    m_pRenderer->Flush();
    tTJSUniqueLock lock2(m_presentlock);

    while (!m_queued.empty())
    {
        m_discard.push_back(m_queued.front());
        m_queued.pop_front();
    }

    if (m_presentstep == PRESENT_READY)
        m_presentstep = PRESENT_IDLE;
    m_presentevent.notify_all();
}

bool CVideoPlayerVideo::GetStats(int& lateframes, double& pts, int& queued, int& discard)
{
    tTJSUniqueLock lock(m_presentlock);
    lateframes = m_lateframes / 10;
    pts = m_pClock->GetClock();
    queued = m_queued.size();
    discard = m_discard.size();
    return true;
}

void CVideoPlayerVideo::CheckEnableClockSync()
{
    // refresh rate can be a multiple of video fps
    double diff = 1.0;

    if (m_fps != 0)
    {
        float fps = m_fps;

        if (60 >= fps)
            diff = fmod(60, fps);
        else
            diff = fps - 60;
    }

    if (diff < 0.01)
    {
        m_clockSync.m_enabled = true;
    }
    else
    {
        m_clockSync.m_enabled = false;
    }

    UpdateClockSync(m_clockSync.m_enabled);
}

void CVideoPlayerVideo::CClockSync::Reset()
{
    m_error = 0;
    m_errCount = 0;
    m_syncOffset = 0;
    m_enabled = false;
}

NS_KRMOVIE_END
