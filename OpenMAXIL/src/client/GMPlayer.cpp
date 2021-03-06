/**
 *  Copyright (c) 2009-2013, Freescale Semiconductor Inc.,
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of Freescale Semiconductor Inc.,
 *  and contain its proprietary and confidential information.
 *
 */

#include <ctype.h>
#include "GMPlayer.h"
#include "MediaInspector.h"
#include "OMX_ImageConvert.h"

#include "GMSubtitlePlayer.h"

#ifdef ANDROID_BUILD
#include "PMemory.h"
#include "PlatformResourceMgrItf.h"
#endif

#define AUDIO_CLOCK_PORT 0
#define VIDEO_CLOCK_PORT 1
#define PARSER_CLOCK_PORT 2
#define FILTER_INPUT_PORT 0
#define FILTER_OUT_PORT 1

#define AUDIO_PROCESSOR "audio_processor.volume"
#define VIDEO_PROCESSOR "video_processor.ipu"

#ifdef ANDROID_BUILD
#define AUDIO_RENDER "audio_render.android"
#else
#define AUDIO_RENDER "audio_render.alsa"
#endif
#define AUDIO_RENDER_FAKE "audio_render.fake"

#define GM_QUIT_EVENT ((OMX_EVENTTYPE)(OMX_EventMax - 1))

#define WAIT_MARKBUFFER(to) \
    do { \
        OMX_U32 WaitCnt = 0; \
        OMX_U32 ToCnt = to / 10000; \
        while(1) { \
            if(bAudioMarkDone == OMX_TRUE && bVideoMarkDone == OMX_TRUE) \
            break; \
            if(bError == OMX_TRUE) \
            break; \
            fsl_osal_cond_timedwait(Cond, 10000); \
            if(bBuffering == OMX_TRUE) { \
                WaitCnt = 0; \
            } \
            WaitCnt ++; \
            if(WaitCnt >= ToCnt) { \
                ret = OMX_ErrorTimeout; \
                break; \
            } \
        } \
        LOG_DEBUG("bAudioMarkDone: %d, bVideoMarkDone: %d\n", bAudioMarkDone, bVideoMarkDone); \
        bAudioMarkDone = bVideoMarkDone = OMX_FALSE; \
    }while(0);

#define swap_value(a,b) \
    do { \
        OMX_U32 temp; \
        temp = a; \
        a = b; \
        b = temp; \
    } while(0);


#ifdef MX37
#define DISP_WIDTH 480
#define DISP_HEIGHT 640
#else
#define DISP_WIDTH 1024
#define DISP_HEIGHT 768
#endif

static OMX_BOOL char_tolower(OMX_STRING string)
{
    while(*string != '\0')
    {
        *string = tolower(*string);
        string++;
    }

    return OMX_TRUE;
}

static OMX_STRING GetAudioRole(OMX_AUDIO_CODINGTYPE type)
{
    OMX_STRING role = NULL;

    switch((int)type) {
        case OMX_AUDIO_CodingPCM:
            role = (OMX_STRING)"audio_decoder.pcm";
            break;
        case OMX_AUDIO_CodingAMR:
            role = (OMX_STRING)"audio_decoder.amr";
            break;
        case OMX_AUDIO_CodingGSMFR:
            break;
        case OMX_AUDIO_CodingGSMEFR:
            break;
        case OMX_AUDIO_CodingGSMHR:
            break;
        case OMX_AUDIO_CodingPDCFR:
            break;
        case OMX_AUDIO_CodingPDCEFR:
            break;
        case OMX_AUDIO_CodingPDCHR:
            break;
        case OMX_AUDIO_CodingTDMAFR:
            break;
        case OMX_AUDIO_CodingTDMAEFR:
            break;
        case OMX_AUDIO_CodingQCELP8:
            break;
        case OMX_AUDIO_CodingQCELP13:
            break;
        case OMX_AUDIO_CodingEVRC:
            break;
        case OMX_AUDIO_CodingSMV:
            break;
        case OMX_AUDIO_CodingG711:
            break;
        case OMX_AUDIO_CodingG723:
            break;
        case OMX_AUDIO_CodingG726:
            break;
        case OMX_AUDIO_CodingG729:
            break;
        case OMX_AUDIO_CodingAC3:
            role = (OMX_STRING)"audio_decoder.ac3";
            break;
        case OMX_AUDIO_CodingFLAC:
            role = (OMX_STRING)"audio_decoder.flac";
            break;
        case OMX_AUDIO_CodingAAC:
            role = (OMX_STRING)"audio_decoder.aac";
            break;
        case OMX_AUDIO_CodingMP3:
            role = (OMX_STRING)"audio_decoder.mp3";
            break;
        case OMX_AUDIO_CodingSBC:
            role = (OMX_STRING)"audio_decoder.sbc";
            break;
        case OMX_AUDIO_CodingVORBIS:
            role = (OMX_STRING)"audio_decoder.vorbis";
            break;
        case OMX_AUDIO_CodingWMA:
            role = (OMX_STRING)"audio_decoder.wma";
            break;
        case OMX_AUDIO_CodingRA:
            role = (OMX_STRING)"audio_decoder.ra";
            break;
        case OMX_AUDIO_CodingMIDI:
            break;
        default:
            break;
    }

    return role;
}

static OMX_STRING GetVideoRole(OMX_VIDEO_CODINGTYPE type)
{
    OMX_STRING role = NULL;

    switch((int)type) {
        case OMX_VIDEO_CodingMPEG2:
            role = (OMX_STRING)"video_decoder.mpeg2";
            break;
        case OMX_VIDEO_CodingH263:
            role = (OMX_STRING)"video_decoder.h263";
            break;
        case OMX_VIDEO_CodingMPEG4:
            role = (OMX_STRING)"video_decoder.mpeg4";
            break;
        case OMX_VIDEO_CodingWMV9:
            role = (OMX_STRING)"video_decoder.wmv9";
            break;
        case OMX_VIDEO_SORENSON263:
            role = (OMX_STRING)"video_decoder.sorenson";
            break;
        case OMX_VIDEO_CodingWMV:
            role = (OMX_STRING)"video_decoder.wmv";
            break;
        case OMX_VIDEO_CodingRV:
            role = (OMX_STRING)"video_decoder.rv";
            break;
        case OMX_VIDEO_CodingAVC:
            role = (OMX_STRING)"video_decoder.avc";
            break;
        case OMX_VIDEO_CodingMJPEG:
            role = (OMX_STRING)"video_decoder.mjpeg";
            break;
		case OMX_VIDEO_CodingDIVX:
			role = (OMX_STRING)"video_decoder.divx";
			break;
		case OMX_VIDEO_CodingDIV3:
			role = (OMX_STRING)"video_decoder.div3";
			break;
		case OMX_VIDEO_CodingDIV4:
			role = (OMX_STRING)"video_decoder.div4";
			break;
		case OMX_VIDEO_CodingXVID:
			role = (OMX_STRING)"video_decoder.xvid";
			break;
        case OMX_VIDEO_CodingVP8:
            role = (OMX_STRING)"video_decoder.vp8";
            break;
        default:
            break;
    }

    return role;
}

typedef struct {
    OMX_U32 type;
    OMX_BOOL IsDecoderSupportNV12Fmt;
    OMX_U32 buffer_usage;
} VIDEO_PROPERTY;

static VIDEO_PROPERTY VideoProperty[] = {
    {OMX_VIDEO_CodingMPEG2, OMX_TRUE, BUFFER_SW_READ_NEVER | BUFFER_SW_WRITE_NEVER | BUFFER_PHY_CONTINIOUS},
    {OMX_VIDEO_CodingH263, OMX_TRUE, BUFFER_SW_READ_NEVER | BUFFER_SW_WRITE_NEVER | BUFFER_PHY_CONTINIOUS},
    {OMX_VIDEO_CodingMPEG4, OMX_TRUE, BUFFER_SW_READ_NEVER | BUFFER_SW_WRITE_NEVER | BUFFER_PHY_CONTINIOUS},
    {OMX_VIDEO_CodingWMV9, OMX_TRUE, BUFFER_SW_READ_NEVER | BUFFER_SW_WRITE_NEVER | BUFFER_PHY_CONTINIOUS},
    {OMX_VIDEO_CodingRV, OMX_TRUE, BUFFER_SW_READ_NEVER | BUFFER_SW_WRITE_NEVER | BUFFER_PHY_CONTINIOUS},
    {OMX_VIDEO_CodingAVC, OMX_TRUE, BUFFER_SW_READ_NEVER | BUFFER_SW_WRITE_NEVER | BUFFER_PHY_CONTINIOUS},
    {OMX_VIDEO_CodingDIVX, OMX_TRUE, BUFFER_SW_READ_NEVER | BUFFER_SW_WRITE_NEVER | BUFFER_PHY_CONTINIOUS},
    {OMX_VIDEO_CodingDIV3, OMX_TRUE, BUFFER_SW_READ_NEVER | BUFFER_SW_WRITE_NEVER | BUFFER_PHY_CONTINIOUS},
    {OMX_VIDEO_CodingDIV4, OMX_TRUE, BUFFER_SW_READ_NEVER | BUFFER_SW_WRITE_NEVER | BUFFER_PHY_CONTINIOUS},
    {OMX_VIDEO_CodingXVID, OMX_TRUE, BUFFER_SW_READ_NEVER | BUFFER_SW_WRITE_NEVER | BUFFER_PHY_CONTINIOUS},
    {OMX_VIDEO_CodingVP8, OMX_TRUE, BUFFER_SW_READ_NEVER | BUFFER_SW_WRITE_NEVER | BUFFER_PHY_CONTINIOUS},

#ifdef MX6X
    {OMX_VIDEO_CodingMJPEG, OMX_TRUE, BUFFER_SW_READ_NEVER | BUFFER_SW_WRITE_NEVER | BUFFER_PHY_CONTINIOUS},
#else
    {OMX_VIDEO_CodingMJPEG, OMX_FALSE, BUFFER_SW_READ_NEVER | BUFFER_SW_WRITE_NEVER | BUFFER_PHY_CONTINIOUS},
#endif

    {OMX_VIDEO_SORENSON263, OMX_FALSE, BUFFER_SW_READ_NEVER | BUFFER_SW_WRITE_OFTEN},
    {OMX_VIDEO_CodingWMV, OMX_FALSE, BUFFER_SW_READ_NEVER | BUFFER_SW_WRITE_OFTEN},
};

static OMX_ERRORTYPE SysEventCallback(
        GMComponent *pComponent,
        OMX_PTR pAppData,
        OMX_EVENTTYPE eEvent,
        OMX_U32 nData1,
        OMX_U32 nData2,
        OMX_PTR pEventData)
{
    GMPlayer *Player = NULL;
    GM_SYSEVENT SysEvent;

    Player = (GMPlayer*)pAppData;

    SysEvent.pComponent = pComponent;
    SysEvent.eEvent = eEvent;
    SysEvent.nData1 = nData1;
    SysEvent.nData2 = nData2;
    SysEvent.pEventData = pEventData;
    if(eEvent == OMX_EventMark)
        Player->SysEventHandler(&SysEvent);
    else
        Player->AddSysEvent(&SysEvent);

    return OMX_ErrorNone;
}

static fsl_osal_ptr SysEventThreadFunc(fsl_osal_ptr arg)
{
    GMPlayer *Player = (GMPlayer*)arg;
    GM_SYSEVENT SysEvent;

    while(1) {
        Player->GetSysEvent(&SysEvent);
        if(SysEvent.eEvent == GM_QUIT_EVENT)
            break;
        Player->SysEventHandler(&SysEvent);
    }

    return NULL;
}

static OMX_ERRORTYPE GMGetBuffer(
        OMX_HANDLETYPE hComponent,
        OMX_U32 nPortIndex,
        OMX_PTR *pBuffer,
        GMBUFFERINFO *pInfo,
        OMX_PTR pAppData)
{
    GMPlayer *player = (GMPlayer*)pAppData;

    *pBuffer = NULL;

    switch(pInfo->type) {
        case GM_VIRTUAL:
            break;
        case GM_PHYSIC:
#ifdef ANDROID_BUILD
            PMEMADDR sMemAddr;
            sMemAddr = GetPMBuffer(player->PmContext, pInfo->nSize, pInfo->nNum);
            if(sMemAddr.pVirtualAddr == NULL)
                return OMX_ErrorInsufficientResources;
            AddHwBuffer(sMemAddr.pPhysicAddr, sMemAddr.pVirtualAddr);
            *pBuffer = sMemAddr.pVirtualAddr;
#endif
            break;
        case GM_EGLIMAGE:
#ifdef EGL_RENDER
            GMCreateEglImage(player->EglContext, pBuffer);
#endif
            break;
        default:
            break;
    }

    return OMX_ErrorNone;
}

static OMX_ERRORTYPE GMFreeBuffer(
        OMX_HANDLETYPE hComponent,
        OMX_U32 nPortIndex,
        OMX_PTR pBuffer,
        GMBUFFERINFO *pInfo,
        OMX_PTR pAppData)
{
    GMPlayer *player = (GMPlayer*)pAppData;

    switch(pInfo->type) {
        case GM_VIRTUAL:
            break;
        case GM_PHYSIC:
#ifdef ANDROID_BUILD
            RemoveHwBuffer(pBuffer);
            FreePMBuffer(player->PmContext, pBuffer);
#endif
            break;
        default:
            break;
    }

    return OMX_ErrorNone;
}

static GM_BUFFERCB gBufferCb = {
    GMGetBuffer,
    GMFreeBuffer
};

#ifdef EGL_RENDER
static OMX_ERRORTYPE AppShowFrame(
        GMComponent *pComponent,
        OMX_BUFFERHEADERTYPE *pBufferHdr,
        OMX_U32 nPortIndex,
        OMX_PTR pAppData)
{
    GMPlayer *player = (GMPlayer*)pAppData;
    OMX_BUFFERHEADERTYPE *pBuffer = NULL;

    pBuffer = (OMX_BUFFERHEADERTYPE*)pBufferHdr->pBuffer;
    GMEglDrawImage(player->EglContext, pBuffer->pPlatformPrivate);

    return OMX_ErrorNone;
}
#endif

const VIDEO_RENDER_MAP_ENTRY GMPlayer::VideoRenderRoleTable[OMX_VIDEO_RENDER_NUM] ={
    {OMX_VIDEO_RENDER_V4L,       "video_render.v4l"},
    {OMX_VIDEO_RENDER_IPULIB,    "video_render.ipulib"},
    {OMX_VIDEO_RENDER_SURFACE,   "video_render.surface"},
    {OMX_VIDEO_RENDER_FB,        "video_render.fb"},
    {OMX_VIDEO_RENDER_EGL,       "video_render.fake"},
    {OMX_VIDEO_RENDER_OVERLAY,   "video_render.overlay"},
};


OMX_ERRORTYPE GMPlayer::Init()
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;

    Content = NULL;
    Pipeline = NULL;
    AudioTrackInfo = NULL;
    SubtitleTrackInfo = NULL;
    SysEventQueue = NULL;
    SysEventThread = NULL;
    Lock = NULL;
    Cond = NULL;

    mAudioSink = NULL;
    hPipe = NULL;

    pSubtitlePlayer = NULL;
    InbandSubtitleSource = NULL;
    OutbandSubtitleSource = NULL;

    SysEventQueue = FSL_NEW(Queue, ());
    if(SysEventQueue == NULL)
        return OMX_ErrorInsufficientResources;

    if(QUEUE_SUCCESS != SysEventQueue->Create(128, sizeof(GM_SYSEVENT), E_FSL_OSAL_TRUE)) {
        ret = OMX_ErrorInsufficientResources;
        goto err;
    }

    if(E_FSL_OSAL_SUCCESS != fsl_osal_thread_create(&SysEventThread, NULL, SysEventThreadFunc, this)) {
        ret = OMX_ErrorInsufficientResources;
        goto err;
    }

    if(E_FSL_OSAL_SUCCESS != fsl_osal_mutex_init(&Lock, fsl_osal_mutex_normal)) {
        ret = OMX_ErrorInsufficientResources;
        goto err;
    }

    if(E_FSL_OSAL_SUCCESS != fsl_osal_cond_create(&Cond)) {
        ret = OMX_ErrorInsufficientResources;
        goto err;
    }

    Pipeline = FSL_NEW(List<GMComponent>, ());
    if(Pipeline == NULL) {
        ret = OMX_ErrorInsufficientResources;
        goto err;
    }

    AudioTrackInfo = FSL_NEW(List<sAUDIOTRACKINFO>, ());
    if(AudioTrackInfo == NULL) {
        ret = OMX_ErrorInsufficientResources;
        goto err;
    }

    SubtitleTrackInfo = FSL_NEW(List<sSUBTITLETRACKINFO>, ());
    if(SubtitleTrackInfo == NULL) {
        ret = OMX_ErrorInsufficientResources;
        goto err;
    }

    OutbandSubtitleSource = FSL_NEW(List<GMSubtitleSource>, ());
    if(OutbandSubtitleSource == NULL) {
        ret = OMX_ErrorInsufficientResources;
        goto err;
    }

    sDispWindow.nTop = 0;
    sDispWindow.nLeft = 0;
    sDispWindow.nWidth = DISP_WIDTH;
    sDispWindow.nHeight = DISP_HEIGHT;
    fsl_osal_memcpy(&sRectOut, &sDispWindow, sizeof(OMX_CONFIG_RECTTYPE));

    Reset();

    return OMX_ErrorNone;

err:
    DeInit();
    return ret;
}

OMX_ERRORTYPE GMPlayer::DeInit()
{
    if(SysEventThread != NULL) {
        GM_SYSEVENT SysEvent;
        SysEvent.eEvent = GM_QUIT_EVENT;
        AddSysEvent(&SysEvent);
        fsl_osal_thread_destroy(SysEventThread);
    }

    if(SysEventQueue != NULL) {
        SysEventQueue->Free();
        FSL_DELETE(SysEventQueue);
    }

    if(Lock != NULL) {
        fsl_osal_mutex_destroy(Lock);
        Lock = NULL;
    }

    if(Cond != NULL) {
        fsl_osal_cond_destroy(Cond);
        Cond = NULL;
    }

    if(Pipeline != NULL)
        FSL_DELETE(Pipeline);

    if(AudioTrackInfo != NULL)
        FSL_DELETE(AudioTrackInfo);

    if(SubtitleTrackInfo != NULL)
        FSL_DELETE(SubtitleTrackInfo);

    if(OutbandSubtitleSource != NULL)
        FSL_DELETE(OutbandSubtitleSource);

    return OMX_ErrorNone;
}

OMX_ERRORTYPE GMPlayer::Reset()
{
    bEnableAudio = OMX_TRUE;
    bEnableVideo = OMX_TRUE;
    bEnableSubtitle = OMX_TRUE;
    Parser = NULL;
    AudioDecoder = NULL;
    VideoDecoder = NULL;
    AudioPostProcess = NULL;
    VideoPostProcess = NULL;
    AudioRender = NULL;
    VideoRender = NULL;
    Clock = NULL;
    bHasAudio = OMX_FALSE;
    bHasVideo = OMX_FALSE;
    bHasSubtitle = OMX_FALSE;
    nAudioTrackCnt = 0;
    nVideoTrackCnt = 0;
    nInbandSubtitleTrackCnt = 0;
    nOutbandSubtitleTrackCnt = 0;
    nCurSubtitleTrack = -1;
    AudioFmt = OMX_AUDIO_CodingMax;
    VideoFmt = OMX_VIDEO_CodingMax;
    bEnableAudioPP = OMX_FALSE;
	bGetMetadata = OMX_FALSE;
    bEnableVideoPP = OMX_FALSE;

    bAddRemoveComponentPostponed = OMX_FALSE;
    bAddRemoveComponentReceived = OMX_FALSE;
    bAudioMarkDone = OMX_FALSE;
    bVideoMarkDone = OMX_FALSE;
    bAudioEos = OMX_FALSE;
    bVideoEos = OMX_FALSE;
    bVideoFFEos = OMX_FALSE;
    bMediaEos = OMX_FALSE;
    bUseFakeAudioSink = OMX_FALSE;
    MediaDuration = 0;
    MediaStartTime = 0;
    DelayedSpeed = 0;
    bError = OMX_FALSE;
    bBuffering = OMX_FALSE;
    bStreamingSource = OMX_FALSE;
    bLiveSource = OMX_FALSE;
    bLiveMode = OMX_FALSE;
    bAbort = OMX_FALSE;
    bSuspend = OMX_FALSE;
    State = GM_STATE_STOP;
    mRotate = 0;
    mSurface = NULL;
    mScalingMode = -1;
    mVideoRenderType = OMX_VIDEO_RENDER_IPULIB;
    bVideoDirectRendering = OMX_FALSE;
    mSurfaceType = OMX_VIDEO_SURFACE_NONE;

    OMX_INIT_STRUCT(&sOutputMode, OMX_CONFIG_OUTPUTMODE);
    OMX_INIT_STRUCT(&sVideoRect, OMX_CONFIG_OUTPUTMODE);
    eScreen = SCREEN_NONE;
    bRotate = OMX_FALSE;
    bAudioPassthrough = OMX_FALSE;
    bPreLoaded = OMX_FALSE;

    if(Content != NULL)
        FSL_FREE(Content);

    while(SysEventQueue->Size() > 0) {
        GM_SYSEVENT SysEvent;
        SysEventQueue->Get(&SysEvent);
    }

#ifdef EGL_RENDER
    if(mVideoRenderType == OMX_VIDEO_RENDER_EGL){
        if(EglContext != NULL) {
            GMDestroyEglContext(EglContext);
            EglContext = NULL;
        }
    }
#endif
    
#ifdef ANDROID_BUILD
    if(PmContext != NULL) {
        DestroyPMeoryContext(PmContext);
        PmContext = NULL;
    }
#endif

    if(pSubtitlePlayer)
        DestroySubtitlePlayer();

    if(InbandSubtitleSource) {
        FSL_DELETE(InbandSubtitleSource);
        InbandSubtitleSource = NULL;
    }

    GMOutbandSubtitleSource *pOutbandSource = NULL;
    OMX_S32 nOutbandSourceCnt = OutbandSubtitleSource->GetNodeCnt();
    for(OMX_S32 i=0; i<nOutbandSourceCnt; i++) {
        pOutbandSource = (GMOutbandSubtitleSource*)OutbandSubtitleSource->GetNode(0);
        OutbandSubtitleSource->Remove(pOutbandSource);
        pOutbandSource->DeInit();
        FSL_DELETE(pOutbandSource);
    }

    return OMX_ErrorNone;
}

OMX_ERRORTYPE GMPlayer::PreLoad(OMX_STRING contentURI)
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;
    OMX_STRING role = NULL;
    OMX_U32 len;
    OMX_U8 *ptr = NULL;

    if(contentURI == NULL)
        return OMX_ErrorBadParameter;

    fsl_osal_mutex_lock(Lock);

    if(State != GM_STATE_STOP) {
        fsl_osal_mutex_unlock(Lock);
        return OMX_ErrorNone;
    }

    if(fsl_osal_strncmp(contentURI, "sharedfd://", 11) != 0) {
        LOG_ERROR("preload for non sharedfd source is not allowed.\n");
        fsl_osal_mutex_unlock(Lock);
        return OMX_ErrorNone;
    }

    len = fsl_osal_strlen(contentURI) + 1;
    ptr = (OMX_U8*)FSL_MALLOC(sizeof(OMX_PARAM_CONTENTURITYPE) + len);
    if(ptr == NULL) {
        fsl_osal_mutex_unlock(Lock);
        return OMX_ErrorInsufficientResources;
    }

    Content = (OMX_PARAM_CONTENTURITYPE*)ptr;
    fsl_osal_strcpy((fsl_osal_char*)(&Content->contentURI), contentURI);
    printf("Loading content: %s\n", (fsl_osal_char*)(&Content->contentURI));

    role = MediaTypeInspect();
    if(role == NULL) {
        LOG_ERROR("Can't inspec content format.\n");
        FSL_FREE(Content);
        fsl_osal_mutex_unlock(Lock);
        return OMX_ErrorUndefined;
    }

    State = GM_STATE_LOADING;

    ret = LoadParser(role);
    if(ret != OMX_ErrorNone) {
        LOG_ERROR("Can't load content %s\n", contentURI);
        fsl_osal_mutex_unlock(Lock);
        return ret;
    }

    bPreLoaded = OMX_TRUE;

    fsl_osal_mutex_unlock(Lock);

    return OMX_ErrorNone;
}

OMX_ERRORTYPE GMPlayer::Load(OMX_STRING contentURI)
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;
	OMX_STRING role = NULL;
    OMX_U32 len;
    OMX_U8 *ptr = NULL;

    if(contentURI == NULL)
        return OMX_ErrorBadParameter;

    fsl_osal_mutex_lock(Lock);

    if(bPreLoaded != OMX_TRUE) {
        if(State != GM_STATE_STOP) {
            fsl_osal_mutex_unlock(Lock);
            return OMX_ErrorNone;
        }

        len = fsl_osal_strlen(contentURI) + 1;
        ptr = (OMX_U8*)FSL_MALLOC(sizeof(OMX_PARAM_CONTENTURITYPE) + len);
        if(ptr == NULL) {
            fsl_osal_mutex_unlock(Lock);
            return OMX_ErrorInsufficientResources;
        }

        Content = (OMX_PARAM_CONTENTURITYPE*)ptr;
        if(fsl_osal_strncmp(contentURI, "file://", 7) == 0)
            fsl_osal_strcpy((fsl_osal_char*)(&Content->contentURI), contentURI + 7);
        else
            fsl_osal_strcpy((fsl_osal_char*)(&Content->contentURI), contentURI);

        printf("Loading content: %s\n", (fsl_osal_char*)(&Content->contentURI));


        if(fsl_osal_strncmp(contentURI, "http://", 7) == 0
                || fsl_osal_strncmp(contentURI, "mms://", 6) == 0) {
            bStreamingSource = OMX_TRUE;
        }
        else if(fsl_osal_strncmp(contentURI, "rtsp://", 7) == 0) {
            bStreamingSource = OMX_TRUE;
            bLiveMode = OMX_TRUE;
        }
        else if(fsl_osal_strncmp(contentURI, "rtp://", 6) == 0
                || fsl_osal_strncmp(contentURI, "udp://", 6) == 0) {
            bStreamingSource = OMX_TRUE;
            bLiveSource = OMX_TRUE;
            bLiveMode = OMX_TRUE;
        }

        role = MediaTypeInspect();
        if(role == NULL) {
            LOG_ERROR("Can't inspec content format.\n");
            FSL_FREE(Content);
            fsl_osal_mutex_unlock(Lock);
            return OMX_ErrorUndefined;
        }

        State = GM_STATE_LOADING;

        ret = LoadParser(role);
        if(ret != OMX_ErrorNone) {
            LOG_ERROR("Can't load content %s\n", contentURI);
            fsl_osal_mutex_unlock(Lock);
            return ret;
        }
    }

    if(mVideoRenderType == OMX_VIDEO_RENDER_SURFACE || mVideoRenderType == OMX_VIDEO_RENDER_OVERLAY) {
        if(mSurface == NULL && bHasVideo == OMX_TRUE) {
            DisableParserVideoPort();
        }
    }

    ret = SetupPipeline();
    if(ret != OMX_ErrorNone){
        LOG_ERROR("SetupPipeline faint\n");
        goto err;
    }

    LoadClock();
    AttachClock(PARSER_CLOCK_PORT, Parser, 2);

    ret = StartPipeline();
    if(ret != OMX_ErrorNone){
        LOG_ERROR("StartPipeline faint\n");
        goto err;
    }

    State = GM_STATE_LOADED;
    fsl_osal_mutex_unlock(Lock);

    return OMX_ErrorNone;

err:
    fsl_osal_mutex_unlock(Lock);
    LOG_ERROR("Load faint and stop\n");
    Stop();

    return ret;
}

OMX_ERRORTYPE GMPlayer::Start()
{
    OMX_TIME_CONFIG_CLOCKSTATETYPE ClockState;
    OMX_U32 nCnt = 0;

    fsl_osal_mutex_lock(Lock);

    if(State != GM_STATE_LOADED) {
        fsl_osal_mutex_unlock(Lock);
        return OMX_ErrorIncorrectStateOperation;
    }

    printf("start player.\n");

    State = GM_STATE_PLAYING;

    if(bBuffering == OMX_TRUE) {
        fsl_osal_mutex_unlock(Lock);
        return OMX_ErrorNone;
    }

    if(bHasAudio == OMX_TRUE)
        AudioRender->StateTransUpWard(OMX_StateExecuting);
    if(bHasVideo == OMX_TRUE)
        VideoRender->StateTransUpWard(OMX_StateExecuting);

    OMX_INIT_STRUCT(&ClockState, OMX_TIME_CONFIG_CLOCKSTATETYPE);
    while(1) {
        OMX_GetConfig(Clock->hComponent, OMX_IndexConfigTimeClockState, &ClockState);
        if(ClockState.eState == OMX_TIME_ClockStateRunning)
            break;
        fsl_osal_sleep(1000);
        nCnt ++;
        if(nCnt >= 5*1000) {
            LOG_ERROR("Start player timeout.\n");
            break;
        }
    }

    fsl_osal_mutex_unlock(Lock);

    return OMX_ErrorNone;
}

OMX_ERRORTYPE GMPlayer::Stop()
{
    GMComponent *pComponent = NULL;
    OMX_U32 ComponentNum = 0;
    OMX_S32 i;

    if(bStreamingSource == OMX_TRUE) {
        bAbort = OMX_TRUE;
        fsl_osal_cond_broadcast(Cond);
        if(Parser != NULL && Parser->hComponent != NULL) {
            OMX_CONFIG_ABORTBUFFERING sAbortBuffering;
            OMX_INIT_STRUCT(&sAbortBuffering, OMX_CONFIG_ABORTBUFFERING);
            sAbortBuffering.bAbort = OMX_TRUE; 
            OMX_SetConfig(Parser->hComponent, OMX_IndexConfigAbortBuffering, &sAbortBuffering);
        }

        /* This is the workaround to avoid ipu busy if stop slow in streaming case */
        if(VideoRender != NULL && VideoRender->hComponent != NULL) {
            OMX_CONFIG_SYSSLEEP sConfSysSleep;
            OMX_INIT_STRUCT(&sConfSysSleep,OMX_CONFIG_SYSSLEEP);
            sConfSysSleep.nPortIndex = 0;
            sConfSysSleep.bSleep = OMX_TRUE;
            OMX_SetConfig(VideoRender->hComponent,OMX_IndexSysSleep,&sConfSysSleep);
        }
    }

    fsl_osal_mutex_lock(Lock);

    if(State == GM_STATE_STOP) {
        fsl_osal_mutex_unlock(Lock);
        return OMX_ErrorIncorrectStateOperation;
    }

    printf("Stop Player.\n");

    State = GM_STATE_STOP;

    //pause render first to avoid noise popup.
    if(AudioRender != NULL)
        AudioRender->StateTransDownWard(OMX_StatePause);
    if(VideoRender != NULL)
        VideoRender->StateTransDownWard(OMX_StatePause);
    if(Clock != NULL)
        Clock->StateTransDownWard(OMX_StatePause);

    //stop clock
    if(Clock != NULL) {
        OMX_TIME_CONFIG_CLOCKSTATETYPE ClockState;
        OMX_INIT_STRUCT(&ClockState, OMX_TIME_CONFIG_CLOCKSTATETYPE);
        OMX_GetConfig(Clock->hComponent, OMX_IndexConfigTimeClockState, &ClockState);
        ClockState.eState = OMX_TIME_ClockStateStopped;
        OMX_SetConfig(Clock->hComponent, OMX_IndexConfigTimeClockState, &ClockState);
    }

    ComponentNum = Pipeline->GetNodeCnt();
    for(i=0; i<(OMX_S32)ComponentNum; i++) {
        pComponent = Pipeline->GetNode(i);
        LOG_DEBUG("Stop %s step1.\n", pComponent->name);
        pComponent->StateTransDownWard(OMX_StateIdle);
    }

    RemoveClock();

    for(i=0; i<(OMX_S32)ComponentNum; i++) {
        pComponent = Pipeline->GetNode(0);
        LOG_DEBUG("Stop %s step2.\n", pComponent->name);
        Pipeline->Remove(pComponent);
        pComponent->StateTransDownWard(OMX_StateLoaded);
        pComponent->UnLoad();
        FSL_DELETE(pComponent);
    }

    OMX_U32 nAudioTrackCnt;
    sAUDIOTRACKINFO *pTrackInfo = NULL;
    nAudioTrackCnt = AudioTrackInfo->GetNodeCnt();
    for (i = nAudioTrackCnt - 1 ; i >= 0; i --)
    {
        pTrackInfo = AudioTrackInfo->GetNode(i);
        AudioTrackInfo->Remove(pTrackInfo);
        LOG_DEBUG("Free audio track info.\n");
        FSL_FREE(pTrackInfo);
    }

    OMX_U32 nSubtitleTrackCnt;
    sSUBTITLETRACKINFO *pSubTrackInfo = NULL;
    nSubtitleTrackCnt = SubtitleTrackInfo->GetNodeCnt();
    for (i = nSubtitleTrackCnt - 1 ; i >= 0; i --)
    {
        pSubTrackInfo = SubtitleTrackInfo->GetNode(i);
        SubtitleTrackInfo->Remove(pSubTrackInfo);
        LOG_DEBUG("Free subtitle track info.\n");
        FSL_FREE(pSubTrackInfo);
    }

    Reset();

    fsl_osal_mutex_unlock(Lock);
	fsl_osal_sleep(1000);

    return OMX_ErrorNone;
}

OMX_ERRORTYPE GMPlayer::Pause()
{
    if(bBuffering == OMX_TRUE && State == GM_STATE_PLAYING) {
        State = GM_STATE_PAUSE;
        return OMX_ErrorNone;
    }

    fsl_osal_mutex_lock(Lock);

    if(State != GM_STATE_PLAYING) {
        fsl_osal_mutex_unlock(Lock);
        return OMX_ErrorIncorrectStateOperation;
    }

    printf("Pause player.\n");

    State = GM_STATE_PAUSE;

    if(AudioRender != NULL)
        AudioRender->StateTransDownWard(OMX_StatePause);
    if(VideoRender != NULL)
        VideoRender->StateTransDownWard(OMX_StatePause);
    if(Clock != NULL)
        Clock->StateTransDownWard(OMX_StatePause);

    fsl_osal_mutex_unlock(Lock);

    return OMX_ErrorNone;
}

OMX_ERRORTYPE GMPlayer::Resume()
{
    if(bBuffering == OMX_TRUE && State == GM_STATE_PAUSE) {
        State = GM_STATE_PLAYING;
        return OMX_ErrorNone;
    }

    fsl_osal_mutex_lock(Lock);

    if(State != GM_STATE_PAUSE) {
        fsl_osal_mutex_unlock(Lock);
        return OMX_ErrorIncorrectStateOperation;
    }

    printf("Resume player.\n");

    if(DelayedSpeed > 0) {
        SpeedControl(DelayedSpeed);
        DelayedSpeed = 0;
    }

    if(Clock != NULL)
        Clock->StateTransUpWard(OMX_StateExecuting);
    if(AudioRender != NULL)
        AudioRender->StateTransUpWard(OMX_StateExecuting);
    if(VideoRender != NULL)
        VideoRender->StateTransUpWard(OMX_StateExecuting);

    State = GM_STATE_PLAYING;

    fsl_osal_mutex_unlock(Lock);

	/* Check if need Add/Remove component */
	if (bAddRemoveComponentReceived == OMX_TRUE)
	{
		AddRemoveAudioPP(bAddRemoveComponentPostponed);
		bAddRemoveComponentReceived = OMX_FALSE;
	}

    return OMX_ErrorNone;
}

OMX_ERRORTYPE GMPlayer::Seek(
        OMX_TIME_SEEKMODETYPE mode,
        OMX_TICKS position)
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;

    fsl_osal_mutex_lock(Lock);

    if(State == GM_STATE_STOP) {
        fsl_osal_mutex_unlock(Lock);
        return OMX_ErrorIncorrectStateOperation;
    }

    if(bLiveSource == OMX_TRUE) {
        fsl_osal_mutex_unlock(Lock);
        return OMX_ErrorUndefined;
    }

    printf("Seek to %lld\n", position);

    ret = DoSeek(mode, position);
    if(ret == OMX_ErrorTimeout && mode == OMX_TIME_SeekModeAccurate)
        ret = DoSeek(OMX_TIME_SeekModeFast, position);

    if(State == GM_STATE_LOADED) {
        OMX_TIME_CONFIG_TIMESTAMPTYPE sCur;
        OMX_INIT_STRUCT(&sCur, OMX_TIME_CONFIG_TIMESTAMPTYPE);
        sCur.nPortIndex = OMX_ALL;
        OMX_GetConfig(Clock->hComponent, OMX_IndexConfigTimeCurrentMediaTime, &sCur);
        MediaStartTime = sCur.nTimestamp;
    }

    fsl_osal_mutex_unlock(Lock);

    return ret;
}

OMX_ERRORTYPE GMPlayer::SetPlaySpeed(
        OMX_S32 Scale)
{
    OMX_TIME_CONFIG_SCALETYPE sScale;
    OMX_S32 CurScale;

    if(bStreamingSource == OMX_TRUE)
        return OMX_ErrorNotImplemented;

    fsl_osal_mutex_lock(Lock);

    if(State == GM_STATE_STOP) {
        fsl_osal_mutex_unlock(Lock);
        return OMX_ErrorIncorrectStateOperation;
    }

    if(bLiveSource == OMX_TRUE) {
        fsl_osal_mutex_unlock(Lock);
        return OMX_ErrorUndefined;
    }

    printf("Set Player Speed to %f.\n", (float)Scale / (float)Q16_SHIFT);

    if(bHasVideo != OMX_TRUE) {
        if(!IS_NORMAL_PLAY(Scale)){
            fsl_osal_mutex_unlock(Lock);
            return OMX_ErrorIncorrectStateOperation;
        }
    }
    else {
        if(Scale < -(MAX_TRICK_MODE_RATE * Q16_SHIFT) || Scale > (MAX_TRICK_MODE_RATE * Q16_SHIFT)) {
            fsl_osal_mutex_unlock(Lock);
            return OMX_ErrorIncorrectStateOperation;
        }
        if(Scale < 0 && Scale > MIN_TRICK_REWIND_RATE * Q16_SHIFT)
            Scale = MIN_TRICK_REWIND_RATE * Q16_SHIFT;
        if(Scale >= 0 && Scale < (OMX_S32)(MIN_RATE * Q16_SHIFT))
            Scale = MIN_RATE * Q16_SHIFT;
        if(Scale > (OMX_S32)(MAX_RATE * Q16_SHIFT) && Scale < MIN_TRICK_FORWARD_RATE * Q16_SHIFT)
            Scale = MAX_RATE * Q16_SHIFT;
    }

    OMX_INIT_STRUCT(&sScale, OMX_TIME_CONFIG_SCALETYPE);
    OMX_GetConfig(Clock->hComponent, OMX_IndexConfigTimeScale, &sScale);
    CurScale = sScale.xScale;
    if(Scale == CurScale) {
        printf("Setting same speed scale.\n");
        fsl_osal_mutex_unlock(Lock);
        return OMX_ErrorNone;
    }

    OMX_BOOL seamless;

    if(IS_NORMAL_PLAY(CurScale) && IS_NORMAL_PLAY(Scale))
        seamless = OMX_TRUE;
    else if(IS_TRICK_REWIND(CurScale) && IS_TRICK_REWIND(Scale))
        seamless = OMX_TRUE;
    else if(IS_TRICK_FORWARD(CurScale) && IS_TRICK_FORWARD(Scale))
        seamless = OMX_TRUE;
    else
        seamless = OMX_FALSE;
    
    if(seamless == OMX_TRUE) {
        if(State == GM_STATE_PAUSE) {
            DelayedSpeed = Scale;
            fsl_osal_mutex_unlock(Lock);
            return OMX_ErrorNone;
        }
        SpeedControl(Scale);
    }
    else {
        DelayedSpeed = 0;
        if(IS_TRICK_REWIND(Scale))
            bVideoFFEos = OMX_FALSE;
        if(IS_TRICK_PLAY(CurScale) && IS_NORMAL_PLAY(Scale))
            Trick2Normal(Scale);
        else // normal -> trick or fast rewind <-> fast forward
            Normal2Trick(Scale);
    }

    fsl_osal_mutex_unlock(Lock);

    return OMX_ErrorNone;
}

OMX_ERRORTYPE GMPlayer::GetMediaDuration(
        OMX_TICKS *pDur)
{
    OMX_TICKS AudioDur, VideoDur;

    *pDur = 0;

    if(Parser == NULL)
        return OMX_ErrorNotReady;

    if(State == GM_STATE_NULL || State == GM_STATE_LOADING || State == GM_STATE_STOP)
        return OMX_ErrorNotReady;

    AudioDur = VideoDur = 0;
    GetStreamDuration(&AudioDur, Parser->PortPara[AUDIO_PORT_PARA].nStartPortNumber);
    GetStreamDuration(&VideoDur, Parser->PortPara[VIDEO_PORT_PARA].nStartPortNumber);
    MediaDuration = MAX(AudioDur, VideoDur);
    *pDur = MediaDuration;

    return OMX_ErrorNone;
}

OMX_ERRORTYPE GMPlayer::GetState(
        GM_STATE *pState)
{
    *pState = State;
    if(bMediaEos == OMX_TRUE) {
		*pState = GM_STATE_EOS;
	}

    return OMX_ErrorNone;
}

OMX_ERRORTYPE GMPlayer::GetMediaPosition(
        OMX_TICKS *pCur)
{
    OMX_TIME_CONFIG_TIMESTAMPTYPE sCur;

    fsl_osal_mutex_lock(Lock);

    if(Clock == NULL) {
        *pCur = 0;
        fsl_osal_mutex_unlock(Lock);
        return OMX_ErrorUndefined;
    }

    if(State == GM_STATE_LOADED) {
        *pCur = MediaStartTime;
        fsl_osal_mutex_unlock(Lock);
        return OMX_ErrorNone;
    }

    if(bMediaEos == OMX_TRUE) {
        *pCur = MediaDuration;
        fsl_osal_mutex_unlock(Lock);
        return OMX_ErrorNone;
    }

    OMX_INIT_STRUCT(&sCur, OMX_TIME_CONFIG_TIMESTAMPTYPE);
    sCur.nPortIndex = OMX_ALL;
    OMX_GetConfig(Clock->hComponent, OMX_IndexConfigTimeCurrentMediaTime, &sCur);
    *pCur = sCur.nTimestamp;

    fsl_osal_mutex_unlock(Lock);

    return OMX_ErrorNone;
}

OMX_ERRORTYPE GMPlayer::GetMediaPositionNoLock(
        OMX_TICKS *pCur)
{
    OMX_TIME_CONFIG_TIMESTAMPTYPE sCur;

    if(Clock == NULL) {
        *pCur = 0;
        return OMX_ErrorUndefined;
    }

    OMX_INIT_STRUCT(&sCur, OMX_TIME_CONFIG_TIMESTAMPTYPE);
    sCur.nPortIndex = OMX_ALL;
    OMX_GetConfig(Clock->hComponent, OMX_IndexConfigTimeCurrentMediaTime, &sCur);
    *pCur = sCur.nTimestamp;

    return OMX_ErrorNone;
}

OMX_ERRORTYPE GMPlayer::GetStreamInfo(
        OMX_U32 StreamIndex,
        GM_STREAMINFO *pInfo)
{
    OMX_PARAM_PORTDEFINITIONTYPE sPortDef;

    if(pInfo == NULL)
        return OMX_ErrorBadParameter;

    fsl_osal_mutex_lock(Lock);

    OMX_INIT_STRUCT(&sPortDef, OMX_PARAM_PORTDEFINITIONTYPE);
    sPortDef.nPortIndex = 0;

    if(StreamIndex == GM_AUDIO_STREAM_INDEX) {
        if(bHasAudio != OMX_TRUE) {
            fsl_osal_mutex_unlock(Lock);
            return OMX_ErrorBadPortIndex;
        }

        OMX_GetParameter(AudioDecoder->hComponent, OMX_IndexParamPortDefinition, &sPortDef);
        switch((int)(sPortDef.format.audio.eEncoding)) {
            case OMX_AUDIO_CodingPCM:
                {
                    OMX_AUDIO_PARAM_PCMMODETYPE sParam;
                    OMX_INIT_STRUCT(&sParam, OMX_AUDIO_PARAM_PCMMODETYPE);
                    sParam.nPortIndex= 0;
                    OMX_GetParameter(AudioDecoder->hComponent, OMX_IndexParamAudioPcm, &sParam);

                    pInfo->streamIndex = StreamIndex;
                    pInfo->streamType = OMX_PortDomainAudio;
                    pInfo->streamFormat.audio_info.nChannels = sParam.nChannels;
                    pInfo->streamFormat.audio_info.nBitRate = sParam.nChannels*sParam.nBitPerSample*sParam.nSamplingRate;
                    pInfo->streamFormat.audio_info.nSampleRate = sParam.nSamplingRate;
                    pInfo->streamFormat.audio_info.eEncoding = OMX_AUDIO_CodingPCM;
                }
                break;
            case OMX_AUDIO_CodingMP3:
                {
                    OMX_AUDIO_PARAM_MP3TYPE sParam;
                    OMX_INIT_STRUCT(&sParam, OMX_AUDIO_PARAM_MP3TYPE);
                    sParam.nPortIndex= 0;
                    OMX_GetParameter(AudioDecoder->hComponent, OMX_IndexParamAudioMp3, &sParam);

                    pInfo->streamIndex = StreamIndex;
                    pInfo->streamType = OMX_PortDomainAudio;
                    pInfo->streamFormat.audio_info.nChannels = sParam.nChannels;
                    pInfo->streamFormat.audio_info.nBitRate = sParam.nBitRate;
                    pInfo->streamFormat.audio_info.nSampleRate = sParam.nSampleRate;
                    pInfo->streamFormat.audio_info.eEncoding = OMX_AUDIO_CodingMP3;
                }
                break;
            case OMX_AUDIO_CodingAMR:
                {
                    OMX_AUDIO_PARAM_AMRTYPE sParam;
                    OMX_INIT_STRUCT(&sParam, OMX_AUDIO_PARAM_AMRTYPE);
                    sParam.nPortIndex= 0;
                    OMX_GetParameter(AudioDecoder->hComponent, OMX_IndexParamAudioAmr, &sParam);
                    OMX_AUDIO_PARAM_PCMMODETYPE sParamPCM;
                    OMX_INIT_STRUCT(&sParamPCM, OMX_AUDIO_PARAM_PCMMODETYPE);
                    sParamPCM.nPortIndex= 1;
                    OMX_GetParameter(AudioDecoder->hComponent, OMX_IndexParamAudioPcm, &sParamPCM);

                    pInfo->streamIndex = StreamIndex;
                    pInfo->streamType = OMX_PortDomainAudio;
                    pInfo->streamFormat.audio_info.nChannels = sParamPCM.nChannels;
                    pInfo->streamFormat.audio_info.nBitRate = sParam.nBitRate;
                    pInfo->streamFormat.audio_info.nSampleRate = sParamPCM.nSamplingRate;
                    pInfo->streamFormat.audio_info.eEncoding = OMX_AUDIO_CodingAMR;
                }
                break;
            case OMX_AUDIO_CodingWMA:
                {
                    OMX_AUDIO_PARAM_WMATYPE sParam;
                    sParam.nPortIndex= 0;
                    OMX_INIT_STRUCT(&sParam, OMX_AUDIO_PARAM_WMATYPE);
                    OMX_GetParameter(AudioDecoder->hComponent, OMX_IndexParamAudioWma, &sParam);

                    pInfo->streamIndex = StreamIndex;
                    pInfo->streamType = OMX_PortDomainAudio;
                    pInfo->streamFormat.audio_info.nChannels = sParam.nChannels;
                    pInfo->streamFormat.audio_info.nBitRate = sParam.nBitRate;
                    pInfo->streamFormat.audio_info.nSampleRate = sParam.nSamplingRate;
                    pInfo->streamFormat.audio_info.eEncoding = OMX_AUDIO_CodingWMA;
                }
                break;
            case OMX_AUDIO_CodingAAC:
                {
                    OMX_AUDIO_PARAM_AACPROFILETYPE sParam;
                    sParam.nPortIndex= 0;
                    OMX_INIT_STRUCT(&sParam, OMX_AUDIO_PARAM_AACPROFILETYPE);
                    OMX_GetParameter(AudioDecoder->hComponent, OMX_IndexParamAudioAac, &sParam);

                    pInfo->streamIndex = StreamIndex;
                    pInfo->streamType = OMX_PortDomainAudio;
                    pInfo->streamFormat.audio_info.nChannels = sParam.nChannels;
                    pInfo->streamFormat.audio_info.nBitRate = sParam.nBitRate;
                    pInfo->streamFormat.audio_info.nSampleRate = sParam.nSampleRate;
                    pInfo->streamFormat.audio_info.eEncoding = OMX_AUDIO_CodingAAC;
                }
                break;
#if 0
            case OMX_AUDIO_CodingBSAC:
                {
                    OMX_AUDIO_PARAM_BSACTYPE sParam;
                    sParam.nPortIndex= 0;
                    OMX_INIT_STRUCT(&sParam, OMX_AUDIO_PARAM_BSACTYPE);
                    OMX_GetParameter(AudioDecoder->hComponent, OMX_IndexParamAudioBsac, &sParam);

                    pInfo->streamIndex = StreamIndex;
                    pInfo->streamType = OMX_PortDomainAudio;
                    pInfo->streamFormat.audio_info.nChannels = sParam.nChannels;
                    pInfo->streamFormat.audio_info.nBitRate = sParam.nBitRate;
                    pInfo->streamFormat.audio_info.nSampleRate = sParam.nSampleRate;
                    pInfo->streamFormat.audio_info.eEncoding = OMX_AUDIO_CodingBSAC;
                }
                break;
#endif
            case OMX_AUDIO_CodingAC3:
                {
                    OMX_AUDIO_PARAM_AC3TYPE sParam;
                    sParam.nPortIndex= 0;
                    OMX_INIT_STRUCT(&sParam, OMX_AUDIO_PARAM_AC3TYPE);
                    OMX_GetParameter(AudioDecoder->hComponent, OMX_IndexParamAudioAc3, &sParam);

                    pInfo->streamIndex = StreamIndex;
                    pInfo->streamType = OMX_PortDomainAudio;
                    pInfo->streamFormat.audio_info.nChannels = sParam.nChannels;
                    pInfo->streamFormat.audio_info.nBitRate = sParam.nBitRate;
                    pInfo->streamFormat.audio_info.nSampleRate = sParam.nSampleRate;
                    pInfo->streamFormat.audio_info.eEncoding = (OMX_AUDIO_CODINGTYPE)OMX_AUDIO_CodingAC3;
                }
                break;
            case OMX_AUDIO_CodingFLAC:
                {
                    OMX_AUDIO_PARAM_FLACTYPE sParam;
                    sParam.nPortIndex= 0;
                    OMX_INIT_STRUCT(&sParam, OMX_AUDIO_PARAM_FLACTYPE);
                    OMX_GetParameter(AudioDecoder->hComponent, OMX_IndexParamAudioFlac, &sParam);

                    pInfo->streamIndex = StreamIndex;
                    pInfo->streamType = OMX_PortDomainAudio;
                    pInfo->streamFormat.audio_info.nChannels = sParam.nChannels;
                    pInfo->streamFormat.audio_info.nBitRate = sParam.nBitRate;
                    pInfo->streamFormat.audio_info.nSampleRate = sParam.nSampleRate;
                    pInfo->streamFormat.audio_info.eEncoding = (OMX_AUDIO_CODINGTYPE)OMX_AUDIO_CodingFLAC;
                }
                break;
            case OMX_AUDIO_CodingVORBIS:
                {
                    OMX_AUDIO_PARAM_VORBISTYPE sParam;
                    sParam.nPortIndex= 0;
                    OMX_INIT_STRUCT(&sParam, OMX_AUDIO_PARAM_VORBISTYPE);
                    OMX_GetParameter(AudioDecoder->hComponent, OMX_IndexParamAudioVorbis, &sParam);

                    pInfo->streamIndex = StreamIndex;
                    pInfo->streamType = OMX_PortDomainAudio;
                    pInfo->streamFormat.audio_info.nChannels = sParam.nChannels;
                    pInfo->streamFormat.audio_info.nBitRate = sParam.nBitRate;
                    pInfo->streamFormat.audio_info.nSampleRate = sParam.nSampleRate;
                    pInfo->streamFormat.audio_info.eEncoding = OMX_AUDIO_CodingVORBIS;
                }
                break;
            case OMX_AUDIO_CodingRA:
                {
                    OMX_AUDIO_PARAM_RATYPE sParam;
                    sParam.nPortIndex= 0;
                    OMX_INIT_STRUCT(&sParam, OMX_AUDIO_PARAM_RATYPE);
                    OMX_GetParameter(AudioDecoder->hComponent, OMX_IndexParamAudioRa, &sParam);

                    pInfo->streamIndex = StreamIndex;
                    pInfo->streamType = OMX_PortDomainAudio;
                    pInfo->streamFormat.audio_info.nChannels = sParam.nChannels;
                    pInfo->streamFormat.audio_info.nBitRate = sParam.nBitsPerFrame * sParam.nSamplingRate/ sParam.nSamplePerFrame;
                    pInfo->streamFormat.audio_info.nSampleRate = sParam.nSamplingRate;
                    pInfo->streamFormat.audio_info.eEncoding = OMX_AUDIO_CodingRA;
                }
                break;
            default:
                break;
        }
    }

    if(StreamIndex == GM_VIDEO_STREAM_INDEX) {
        if(bHasVideo != OMX_TRUE) {
            fsl_osal_mutex_unlock(Lock);
            return OMX_ErrorBadPortIndex;
        }

        OMX_GetParameter(VideoDecoder->hComponent, OMX_IndexParamPortDefinition, &sPortDef);
        pInfo->streamIndex = StreamIndex;
        pInfo->streamType = OMX_PortDomainVideo;
        //pInfo->streamFormat.video_info.nBitrate = sPortDef.format.video.nBitrate;
        pInfo->streamFormat.video_info.xFramerate = (float)sPortDef.format.video.xFramerate / (float)Q16_SHIFT;
        pInfo->streamFormat.video_info.eCompressionFormat = sPortDef.format.video.eCompressionFormat;
        
        if(mRotate == 90 || mRotate == 270){
            pInfo->streamFormat.video_info.nFrameWidth = sPortDef.format.video.nFrameHeight;
            pInfo->streamFormat.video_info.nFrameHeight = sPortDef.format.video.nFrameWidth;
        }else{
            pInfo->streamFormat.video_info.nFrameWidth = sPortDef.format.video.nFrameWidth;
            pInfo->streamFormat.video_info.nFrameHeight = sPortDef.format.video.nFrameHeight; 
        }
        LOG_DEBUG("GetStreamInfo mRotate=%d,width=%d,height=%d",mRotate
            ,pInfo->streamFormat.video_info.nFrameWidth,pInfo->streamFormat.video_info.nFrameHeight);
        //OMX_CONFIG_SCALEFACTORTYPE sPixelRatio;
        //OMX_INIT_STRUCT(&sPixelRatio, OMX_CONFIG_SCALEFACTORTYPE);
        //sPixelRatio.nPortIndex = 1;
        //if(OMX_ErrorNone == OMX_GetConfig(VideoDecoder->hComponent,OMX_IndexConfigCommonScale, &sPixelRatio)) {
        //}
    }

    fsl_osal_mutex_unlock(Lock);

    return OMX_ErrorNone;
}

OMX_ERRORTYPE GMPlayer::DisableStream(
        OMX_U32 StreamIndex)
{
    if(State != GM_STATE_STOP)
        return OMX_ErrorIncorrectStateOperation;

    if(StreamIndex == GM_AUDIO_STREAM_INDEX)
        bEnableAudio = OMX_FALSE;
    if(StreamIndex == GM_VIDEO_STREAM_INDEX)
        bEnableVideo = OMX_FALSE;

    return OMX_ErrorNone;
}

OMX_ERRORTYPE GMPlayer::AddSysEvent(
        GM_SYSEVENT *pSysEvent)
{
    SysEventQueue->Add((fsl_osal_ptr)pSysEvent);
    return OMX_ErrorNone;
}

OMX_ERRORTYPE GMPlayer::GetSysEvent(
        GM_SYSEVENT *pSysEvent)
{
    SysEventQueue->Get((fsl_osal_ptr)pSysEvent);
    return OMX_ErrorNone;
}

OMX_ERRORTYPE GMPlayer::SysEventHandler(
        GM_SYSEVENT *pSysEvent)
{
    GMComponent *pComponent = pSysEvent->pComponent;
    OMX_EVENTTYPE eEvent = pSysEvent->eEvent;
    OMX_U32 nData1 = pSysEvent->nData1;
    OMX_U32 nData2 = pSysEvent->nData2;

    switch((int)eEvent) {
        case OMX_EventPortFormatDetected:
            {
                if(pComponent == Parser) {
                    if(nData1 == Parser->PortPara[AUDIO_PORT_PARA].nStartPortNumber) {
                        LOG_DEBUG("Audio format detected: %d\n", AudioFmt);
                        OMX_AUDIO_PARAM_PORTFORMATTYPE sAudioPortFmt;

                        AudioFmt = OMX_AUDIO_CodingMax;
                        OMX_INIT_STRUCT(&sAudioPortFmt, OMX_AUDIO_PARAM_PORTFORMATTYPE);
                        sAudioPortFmt.nPortIndex = Parser->PortPara[AUDIO_PORT_PARA].nStartPortNumber;
                        OMX_GetParameter(Parser->hComponent, OMX_IndexParamAudioPortFormat, &sAudioPortFmt);
                        AudioFmt = sAudioPortFmt.eEncoding;
                    }
                    if(nData1 == Parser->PortPara[VIDEO_PORT_PARA].nStartPortNumber) {
                        LOG_DEBUG("Video format detected: %d\n", VideoFmt);
                        OMX_VIDEO_PARAM_PORTFORMATTYPE sVideoPortFmt;

                        VideoFmt = OMX_VIDEO_CodingMax;
                        OMX_INIT_STRUCT(&sVideoPortFmt, OMX_VIDEO_PARAM_PORTFORMATTYPE);
                        sVideoPortFmt.nPortIndex = Parser->PortPara[VIDEO_PORT_PARA].nStartPortNumber;
                        OMX_GetParameter(Parser->hComponent, OMX_IndexParamVideoPortFormat, &sVideoPortFmt);
                        VideoFmt = sVideoPortFmt.eCompressionFormat;
                    }
                    fsl_osal_cond_signal(Cond);
                }
            }
            break;
        case OMX_EventBufferFlag:
            {
                if(nData2 == OMX_BUFFERFLAG_EOS) {
                    if(pComponent == AudioRender) {
                        LOG_DEBUG("Player get Audio EOS event.\n");
                        bAudioEos = OMX_TRUE;
                    }
                    else if(pComponent == VideoRender) {
                        LOG_DEBUG("Player get Video EOS event.\n");
                        bVideoEos = OMX_TRUE;
                    }
                    else
                        break;

                    OMX_TIME_CONFIG_SCALETYPE sScale;
                    OMX_INIT_STRUCT(&sScale, OMX_TIME_CONFIG_SCALETYPE);
                    if(Clock != NULL) {
                        OMX_GetConfig(Clock->hComponent, OMX_IndexConfigTimeScale, &sScale);
                        if(bVideoEos == OMX_TRUE && sScale.xScale >= MIN_TRICK_FORWARD_RATE*Q16_SHIFT) {
                            bVideoEos = OMX_FALSE;
                            SetPlaySpeed(Q16_SHIFT);
                            Resume();
                            bVideoFFEos = OMX_TRUE;
                            if(pAppCallback != NULL)
                                (*pAppCallback)(pAppData, GM_EVENT_FF_EOS, 0);
                        }
                        else if(bVideoEos == OMX_TRUE && sScale.xScale <= MIN_TRICK_REWIND_RATE*Q16_SHIFT) {
                            SetPlaySpeed(Q16_SHIFT);
                            bVideoEos = OMX_FALSE;
                            if(pAppCallback != NULL)
                                (*pAppCallback)(pAppData, GM_EVENT_BW_BOS, 0);
                        }
                    }

                    if(((bHasAudio == OMX_TRUE && bHasVideo == OMX_TRUE) && (bAudioEos == OMX_TRUE && bVideoEos == OMX_TRUE))
                            || ((bHasAudio == OMX_TRUE && bHasVideo != OMX_TRUE) && bAudioEos == OMX_TRUE)
                            || ((bHasAudio != OMX_TRUE && bHasVideo == OMX_TRUE) && bVideoEos == OMX_TRUE)) {
                        bMediaEos = OMX_TRUE;
                        if(pAppCallback != NULL)
                            (*pAppCallback)(pAppData, GM_EVENT_EOS, 0);
                    }
                }
            }
            break;
        case OMX_EventMark:
            {
                if(pComponent == AudioRender) {
                    bAudioMarkDone = OMX_TRUE;
                    LOG_DEBUG("Player get Audio mark done event.\n");
                }
                if(pComponent == VideoRender) {
                    bVideoMarkDone = OMX_TRUE;
                    LOG_DEBUG("Player get Video mark done event.\n");
                }
                fsl_osal_cond_signal(Cond);
            }
            break;
        case OMX_EventError:
            {
                LOG_ERROR("%s report Error %x.\n", pComponent->name, nData1);
                bError = OMX_TRUE;
                fsl_osal_cond_broadcast(Cond);
                if((OMX_S32)nData1 == OMX_ErrorStreamCorrupt) {
                    if(pAppCallback != NULL)
                        (*pAppCallback)(pAppData, GM_EVENT_CORRUPTED_STREM, 0);
                }
                if((OMX_S32)nData1 == OMX_ErrorHardware) {
                    if(pAppCallback != NULL)
                        (*pAppCallback)(pAppData, GM_EVENT_CORRUPTED_STREM, 0);
                }
            }
            break;
        case OMX_EventRenderingStart:
            {
                if(pAppCallback != NULL)
                    (*pAppCallback)(pAppData, GM_EVENT_RENDERING_START, 0);
            }
            break;
        case OMX_EventBufferingData:
            LOG_DEBUG("Buffering data ...\n");
            bBuffering = OMX_TRUE;
            if(State == GM_STATE_PLAYING) {
                LOG_DEBUG("Pause player when buffering data.\n");
                if(AudioRender != NULL)
                    AudioRender->StateTransDownWard(OMX_StatePause);
                if(VideoRender != NULL)
                    VideoRender->StateTransDownWard(OMX_StatePause);
                if(Clock != NULL)
                    Clock->StateTransDownWard(OMX_StatePause);
            }
            break;
        case OMX_EventBufferingDone:
            LOG_DEBUG("Buffering data done.\n");
            bBuffering = OMX_FALSE;
            if(bSuspend != OMX_TRUE) {
                if(State == GM_STATE_PLAYING) {
                    if(Clock != NULL)
                        Clock->StateTransUpWard(OMX_StateExecuting);
                    if(AudioRender != NULL)
                        AudioRender->StateTransUpWard(OMX_StateExecuting);
                    if(VideoRender != NULL)
                        VideoRender->StateTransUpWard(OMX_StateExecuting);
                    LOG_DEBUG("Resume player after buffering data done.\n");
                }
            }

            break;
        case OMX_EventBufferingUpdate:
            LOG_DEBUG("Buffering update: %d\%\n", nData1);
            if(pAppCallback != NULL) {
                OMX_S32 percentage = nData1;
                (*pAppCallback)(pAppData, GM_EVENT_BUFFERING_UPDATE, (void *)&percentage);
            }
            break;
        case OMX_EventStreamSkipped:
            {
                LOG_DEBUG("flush pipeline as stream skipped.\n");
                if(State != GM_STATE_PLAYING && State != GM_STATE_PAUSE)
                    break;

                OMX_TIME_CONFIG_CLOCKSTATETYPE ClockState;
                OMX_INIT_STRUCT(&ClockState, OMX_TIME_CONFIG_CLOCKSTATETYPE);

                //stop clock
                OMX_INIT_STRUCT(&ClockState, OMX_TIME_CONFIG_CLOCKSTATETYPE);
                OMX_GetConfig(Clock->hComponent, OMX_IndexConfigTimeClockState, &ClockState);
                ClockState.eState = OMX_TIME_ClockStateStopped;
                OMX_SetConfig(Clock->hComponent, OMX_IndexConfigTimeClockState, &ClockState);

                Parser->StateTransDownWard(OMX_StatePause);

                //flush all data for input ports of all components
                OMX_S32 ComponentNum = Pipeline->GetNodeCnt();
                OMX_S32 i;
                for(i=1; i<ComponentNum; i++) {
                    pComponent = Pipeline->GetNode(i);
                    pComponent->PortFlush(0);
                }

                //start clock with WaitingForStartTime
                ClockState.eState = OMX_TIME_ClockStateWaitingForStartTime;
                ClockState.nWaitMask = 0;
                if(bHasAudio == OMX_TRUE)
                    ClockState.nWaitMask |= 1 << AUDIO_CLOCK_PORT;
                if(bHasVideo == OMX_TRUE)
                    ClockState.nWaitMask |= 1 << VIDEO_CLOCK_PORT;
                OMX_SetConfig(Clock->hComponent, OMX_IndexConfigTimeClockState, &ClockState);

                Parser->StateTransUpWard(OMX_StateExecuting);
            }
            break;
        case OMX_EventPortSettingsChanged:
            if(pComponent == VideoDecoder){
                pComponent->DynamicPortSettingChange(nData1);
            }
            else{
                LOG_ERROR("%s: port setting change event isn't supported by %s: port: %d \n",__FUNCTION__,pComponent->name,nData1);
            }
            break;			
        default:
            break;
    }

    return OMX_ErrorNone;
}

OMX_ERRORTYPE GMPlayer::RegistAppCallback(
        OMX_PTR pAppPrivate,
        GM_EVENTHANDLER pCallback)
{
    pAppCallback = pCallback;
    pAppData = pAppPrivate;
    return OMX_ErrorNone;
}

OMX_STRING GMPlayer::MediaTypeInspect()
{
	parserRole = MediaTypeInspectBySubfix();
	if(parserRole == NULL) {
		LOG_ERROR("Can't inspect media content type by subfix.\n");
	}
	if (parserRole && bStreamingSource == OMX_FALSE) {
		parserRole = MediaTypeConformByContent((OMX_STRING)(&Content->contentURI), parserRole);
	}
	if(parserRole == NULL) {
        parserRole = MediaTypeInspectByContent();
        if(parserRole == NULL)
            LOG_ERROR("Can't inspect media content type.\n");
    }

	// Disable OGG local playback.
    if (parserRole && fsl_osal_strcmp(parserRole, "parser.ogg")==0 && bStreamingSource == OMX_FALSE) {
		parserRole = NULL;
	}

    return parserRole;
}

OMX_STRING GMPlayer::MediaTypeInspectBySubfix()
{
    OMX_STRING role = NULL;
    OMX_STRING p = NULL;
    fsl_osal_char subfix[16];

    if(fsl_osal_strncmp((fsl_osal_char*)(&Content->contentURI), "rtsp://", 7) == 0)
        return (OMX_STRING)"parser.rtsp";

    if(fsl_osal_strncmp((fsl_osal_char*)(&Content->contentURI), "mms://", 6) == 0)
        return (OMX_STRING)"parser.asf";

    if(fsl_osal_strncmp((fsl_osal_char*)(&Content->contentURI), "udp://", 6) == 0)
        return (OMX_STRING)"parser.mpg2";

    if(fsl_osal_strncmp((fsl_osal_char*)(&Content->contentURI), "rtp://", 6) == 0)
        return (OMX_STRING)"parser.mpg2";

    p = fsl_osal_strrchr((fsl_osal_char*)(&Content->contentURI), '.');
    if (p == NULL || fsl_osal_strlen(p)>15)
        return NULL;


    fsl_osal_strcpy(subfix, p);
    char_tolower(subfix);
    if (fsl_osal_strcmp(subfix, ".avi")==0|| fsl_osal_strcmp(subfix, ".divx")==0)
        role = (OMX_STRING)"parser.avi";
    else if (fsl_osal_strcmp(subfix, ".mp3")==0)
        role = (OMX_STRING)"parser.mp3";
    else if (fsl_osal_strcmp(subfix, ".aac")==0)
        role = (OMX_STRING)"parser.aac";
    else if (fsl_osal_strcmp(subfix, ".bsac")==0)
        role = (OMX_STRING)"parser.bsac";
    else if (fsl_osal_strcmp(subfix, ".ac3")==0)
        role = (OMX_STRING)"parser.ac3";
    else if  (fsl_osal_strcmp(subfix, ".wmv")==0 || fsl_osal_strcmp(subfix, ".wma")==0 || fsl_osal_strcmp(subfix, ".asf")==0)
        role = (OMX_STRING)"parser.asf";
    else if  (fsl_osal_strcmp(subfix, ".rm")==0 || fsl_osal_strcmp(subfix, ".rmvb")==0 || fsl_osal_strcmp(subfix, ".ra")==0)
        role = (OMX_STRING)"parser.rmvb";
    else if (fsl_osal_strcmp(subfix, ".3gp")==0 || fsl_osal_strcmp(subfix, ".3g2")==0 \
			|| fsl_osal_strcmp(subfix, ".mp4")==0 || fsl_osal_strcmp(subfix, ".mov")==0 \
            || fsl_osal_strcmp(subfix, ".m4v")==0 || fsl_osal_strcmp(subfix, ".m4a")==0)
        role = (OMX_STRING)"parser.mp4";
    else if (fsl_osal_strcmp(subfix, ".flac")==0)
        role = (OMX_STRING)"parser.flac";
    else if (fsl_osal_strcmp(subfix, ".wav")==0)
        role = (OMX_STRING)"parser.wav";
    else if (fsl_osal_strcmp(subfix, ".mpg")==0 || fsl_osal_strcmp(subfix, ".vob")==0
            || fsl_osal_strcmp(subfix, ".ts")==0)
        role = (OMX_STRING)"parser.mpg2";
    else if (fsl_osal_strcmp(subfix, ".ogg")==0)
        role = (OMX_STRING)"parser.ogg";
    else if (fsl_osal_strcmp(subfix, ".mkv")==0)
        role = (OMX_STRING)"parser.mkv";
    else if (fsl_osal_strcmp(subfix, ".m3u8")==0)
        role = (OMX_STRING)"parser.httplive";
    //else if (fsl_osal_strcmp(subfix, ".flv")==0)
    //    role = "parser.flv";
    else if (fsl_osal_strcmp(subfix, ".webm")==0)
        role = (OMX_STRING)"parser.mkv";
    else if (fsl_osal_strcmp(subfix, ".amr")==0 || fsl_osal_strcmp(subfix, ".awb")==0)
        role = (OMX_STRING)"parser.amr";

    LOG_DEBUG("%s, role: %s\n", __FUNCTION__, role);

    return role;
}

OMX_STRING GMPlayer::MediaTypeInspectByContent()
{
    OMX_STRING role = NULL;
    CP_PIPETYPE *Pipe = NULL;
    MediaType type = TYPE_NONE;

    type = GetContentType((OMX_STRING)(&Content->contentURI));
    if(type == TYPE_AVI)
        role = (OMX_STRING)"parser.avi";
    else if(type == TYPE_MP4)
        role = (OMX_STRING)"parser.mp4";
    else if(type == TYPE_RMVB)
        role = (OMX_STRING)"parser.rmvb";
    else if(type == TYPE_MKV)
        role = (OMX_STRING)"parser.mkv";
    else if(type == TYPE_ASF)
        role = (OMX_STRING)"parser.asf";
    else if(type == TYPE_FLV)
        role = (OMX_STRING)"parser.flv";
    else if(type == TYPE_MPG2)
        role = (OMX_STRING)"parser.mpg2";
    else if(type == TYPE_MP3)
        role = (OMX_STRING)"parser.mp3";
    else if(type == TYPE_FLAC)
        role = (OMX_STRING)"parser.flac";
    else if(type == TYPE_AAC)
        role = (OMX_STRING)"parser.aac";
    else if(type == TYPE_AC3)
        role = (OMX_STRING)"parser.ac3";
    else if(type == TYPE_WAV)
        role = (OMX_STRING)"parser.wav";
    else if(type == TYPE_OGG)
        role = (OMX_STRING)"parser.ogg";
    else if(type == TYPE_HTTPLIVE)
        role = (OMX_STRING)"parser.httplive";
    else if(type == TYPE_AMR)
        role = (OMX_STRING)"parser.amr";
    else
        role = NULL;

    LOG_DEBUG("%s, role: %s\n", __FUNCTION__, role);

    return role;
}


OMX_ERRORTYPE GMPlayer::LoadComponent(
        OMX_STRING role,
        GMComponent **ppComponent)
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;
    GMComponent *pComponent = NULL;

    LOG_DEBUG("Loading component %s ...\n", role);

    *ppComponent = NULL;
    pComponent = FSL_NEW(GMComponent, ());
    if(pComponent == NULL)
        return OMX_ErrorInsufficientResources;

    ret = pComponent->Load(role);
    if(ret != OMX_ErrorNone) {
        FSL_DELETE(pComponent);
        return ret;
    }

    pComponent->RegistSysCallback(SysEventCallback, (OMX_PTR)this);
    pComponent->RegistBufferCallback(&gBufferCb, this);

    *ppComponent = pComponent;

    LOG_DEBUG("Load component %s done.\n", pComponent->name);

    return OMX_ErrorNone;
}

OMX_ERRORTYPE GMPlayer::LoadParser(
        OMX_STRING role)
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;
    OMX_AUDIO_PARAM_PORTFORMATTYPE sAudioPortFmt;
    OMX_VIDEO_PARAM_PORTFORMATTYPE sVideoPortFmt;
    OMX_PARAM_U32TYPE u32Type;

    Parser = NULL;
    ret = LoadComponent(role, &Parser);
    if(ret != OMX_ErrorNone) {
        LOG_ERROR("Can't load parser: %s\n", role);
        return ret;
    }

    if(bAbort == OMX_TRUE) {
        ret = OMX_ErrorUndefined;
        goto err;
    }

    ret = OMX_SetParameter(Parser->hComponent, OMX_IndexParamContentURI, Content);
    if(ret != OMX_ErrorNone)
        goto err;

    ret = GetContentPipe(&hPipe, (OMX_STRING)&Content->contentURI);
    if(ret != OMX_ErrorNone)
        goto err;

    if(hPipe != NULL) {
        OMX_PARAM_CONTENTPIPETYPE sPipe;
        OMX_INIT_STRUCT(&sPipe, OMX_PARAM_CONTENTPIPETYPE);
        sPipe.hPipe = hPipe;
        ret = OMX_SetParameter(Parser->hComponent, OMX_IndexParamCustomContentPipe, &sPipe);
        if(ret != OMX_ErrorNone)
            return ret;
    }

    OMX_INIT_STRUCT(&sAudioPortFmt, OMX_AUDIO_PARAM_PORTFORMATTYPE);
    sAudioPortFmt.nPortIndex = Parser->PortPara[AUDIO_PORT_PARA].nStartPortNumber;
    //TODO: Disable other ports
    ret = OMX_GetParameter(Parser->hComponent, OMX_IndexParamAudioPortFormat, &sAudioPortFmt);
    if(ret != OMX_ErrorNone)
        goto err;
    sAudioPortFmt.eEncoding = OMX_AUDIO_CodingAutoDetect;
    ret = OMX_SetParameter(Parser->hComponent, OMX_IndexParamAudioPortFormat, &sAudioPortFmt);
    if(ret != OMX_ErrorNone)
        goto err;
    AudioFmt = OMX_AUDIO_CodingAutoDetect;

    OMX_INIT_STRUCT(&sVideoPortFmt, OMX_VIDEO_PARAM_PORTFORMATTYPE);
    sVideoPortFmt.nPortIndex = Parser->PortPara[VIDEO_PORT_PARA].nStartPortNumber;
    //TODO: Disable other ports
    ret = OMX_GetParameter(Parser->hComponent, OMX_IndexParamVideoPortFormat, &sVideoPortFmt);
    if(ret != OMX_ErrorNone)
        goto err;
    sVideoPortFmt.eCompressionFormat = OMX_VIDEO_CodingAutoDetect;
    ret = OMX_SetParameter(Parser->hComponent, OMX_IndexParamVideoPortFormat, &sVideoPortFmt);
    if(ret != OMX_ErrorNone)
        goto err;
    VideoFmt = OMX_VIDEO_CodingAutoDetect;
	if (bGetMetadata == OMX_TRUE) {
		OMX_PARAM_IS_GET_METADATA sGetMetadata;
		OMX_INIT_STRUCT(&sGetMetadata, OMX_PARAM_IS_GET_METADATA);
		sGetMetadata.bGetMetadata = OMX_TRUE;
		ret = OMX_SetParameter(Parser->hComponent, OMX_IndexParamIsGetMetadata, &sGetMetadata);
		if(ret != OMX_ErrorNone)
			goto err;
	}

    ret = Parser->StateTransUpWard(OMX_StateIdle);
    if(ret != OMX_ErrorNone)
        goto err;

    ret = Parser->StateTransUpWard(OMX_StateExecuting);
    if(ret != OMX_ErrorNone)
        goto err;

    while(1) {
        if(AudioFmt != OMX_AUDIO_CodingAutoDetect && VideoFmt != OMX_VIDEO_CodingAutoDetect)
            break;
        if(bError == OMX_TRUE || bAbort == OMX_TRUE) {
            ret = OMX_ErrorFormatNotDetected;
            goto err;
        }

        fsl_osal_cond_timedwait(Cond, 10000);
    }

    OMX_INIT_STRUCT(&u32Type, OMX_PARAM_U32TYPE);

    if(bEnableAudio != OMX_TRUE)
        Parser->PortDisable(Parser->PortPara[AUDIO_PORT_PARA].nStartPortNumber);
    else {
        if(AudioFmt != OMX_AUDIO_CodingMax) {
            u32Type.nPortIndex = Parser->PortPara[AUDIO_PORT_PARA].nStartPortNumber;
            OMX_GetParameter(Parser->hComponent, OMX_IndexParamNumAvailableStreams, &u32Type);
            nAudioTrackCnt = u32Type.nU32;
            LOG_DEBUG("Total audio track number: %d\n", nAudioTrackCnt);
            if(nAudioTrackCnt > 0) {
                // Get audio track info.
                OMX_U32 i;
                OMX_AUDIO_PARAM_PORTFORMATTYPE sAudioPortFmt;

                OMX_INIT_STRUCT(&sAudioPortFmt, OMX_AUDIO_PARAM_PORTFORMATTYPE);
                sAudioPortFmt.nPortIndex = Parser->PortPara[AUDIO_PORT_PARA].nStartPortNumber;

                for (i = 0; i < nAudioTrackCnt; i ++)
                {
                    sAUDIOTRACKINFO *pTrackInfo = NULL;
                    pTrackInfo = (sAUDIOTRACKINFO *)FSL_MALLOC(sizeof(sAUDIOTRACKINFO));
                    if(pTrackInfo == NULL) {
                        return OMX_ErrorInsufficientResources;
                    }

                    pTrackInfo->nTrackNum = i;
                    u32Type.nU32 = i;
                    OMX_SetParameter(Parser->hComponent, OMX_IndexParamActiveStream, &u32Type);

                    OMX_GetParameter(Parser->hComponent, OMX_IndexParamAudioPortFormat, &sAudioPortFmt);
                    pTrackInfo->eCodingType = sAudioPortFmt.eEncoding;
                    LOG_DEBUG("Audio track conding format: %d\n", sAudioPortFmt.eEncoding);

                    AudioTrackInfo->Add(pTrackInfo);
                }

                bHasAudio = OMX_TRUE;
                u32Type.nU32 = 0;
                OMX_SetParameter(Parser->hComponent, OMX_IndexParamActiveStream, &u32Type);

                OMX_GetParameter(Parser->hComponent, OMX_IndexParamAudioPortFormat, &sAudioPortFmt);
                AudioFmt = sAudioPortFmt.eEncoding;
                LOG_DEBUG("Audio current track conding format: %d\n", sAudioPortFmt.eEncoding);

            }
        }
        else
            Parser->PortDisable(Parser->PortPara[AUDIO_PORT_PARA].nStartPortNumber);
    }

    if(bEnableVideo != OMX_TRUE)
        Parser->PortDisable(Parser->PortPara[VIDEO_PORT_PARA].nStartPortNumber);
    else {
        if(VideoFmt != OMX_VIDEO_CodingMax) {
            u32Type.nPortIndex = Parser->PortPara[VIDEO_PORT_PARA].nStartPortNumber;
            OMX_GetParameter(Parser->hComponent, OMX_IndexParamNumAvailableStreams, &u32Type);
            nVideoTrackCnt = u32Type.nU32;
            LOG_DEBUG("Total video track number: %d\n", nVideoTrackCnt);
            if(nVideoTrackCnt > 0) {
                bHasVideo = OMX_TRUE;
                u32Type.nU32 = 0;
                OMX_SetParameter(Parser->hComponent, OMX_IndexParamActiveStream, &u32Type);
            }
            OMX_CONFIG_ROTATIONTYPE sRotation;
            OMX_INIT_STRUCT(&sRotation, OMX_CONFIG_ROTATIONTYPE);
            
            OMX_GetConfig(Parser->hComponent,OMX_IndexConfigCommonRotate,&sRotation);
            mRotate = sRotation.nRotation;
        }
        else
            Parser->PortDisable(Parser->PortPara[VIDEO_PORT_PARA].nStartPortNumber);
    }

    if(nAudioTrackCnt > 0) {
        OMX_U32 nMetaNum = getMetadataNum();
        for (OMX_U32 i = 0; i < nMetaNum; i ++) {
            OMX_U32 nMetadataSize = getMetadataSize(i);
            OMX_METADATA *pMetadata = (OMX_METADATA *)FSL_MALLOC(sizeof(OMX_METADATA) + nMetadataSize);;
            if (pMetadata == NULL)
                continue;
            fsl_osal_memset(pMetadata, 0, sizeof(OMX_METADATA) + nMetadataSize);
            getMetadata(i, pMetadata);
            OMX_STRING AudioTrackKey = (OMX_STRING)"audio_track_";
            //printf("metadata key: %s, value: %s\n", pMetadata->nKey, pMetadata->nValue);
            if(fsl_osal_strncmp((const fsl_osal_char*)pMetadata->nKey, AudioTrackKey, fsl_osal_strlen(AudioTrackKey)) == 0) {
                int nTrackIndex = 0;
                sscanf((char*)pMetadata->nKey, "audio_track_%d", &nTrackIndex);
                sAUDIOTRACKINFO *pTrackInfo = AudioTrackInfo->GetNode(nTrackIndex);
                fsl_osal_strcpy((fsl_osal_char*)pTrackInfo->lanuage, (fsl_osal_char*)pMetadata->nValue);
                //printf("%s lanuage %s\n", AudioTrackKey, pTrackInfo->lanuage);
            }
            FSL_FREE(pMetadata);
        }
    }

    if(bEnableSubtitle)
    {
        OMX_S32 subCnt;
        OMX_GetParameter(Parser->hComponent, OMX_IndexParamSubtitleNumAvailableStreams, &subCnt); 
        nInbandSubtitleTrackCnt=(OMX_U32)subCnt;
        LOG_DEBUG("nInbandSubtitleTrackCnt: %d \n",subCnt);
      	 if(nInbandSubtitleTrackCnt>0)
         {
             OMX_U32 i,nMetaNum,nMetadataSize;
             OMX_METADATA * pMetadata;
             sSUBTITLETRACKINFO *pSubTrackInfo=NULL;
             OMX_STRING SubtitleTrackNameKey= (OMX_STRING)"subtitle_track_name_";
             OMX_STRING SubtitleTrackTypeKey= (OMX_STRING)"subtitle_track_type_";

             OMX_S32 nInbandSubtitleTrackStartNum = SubtitleTrackInfo->GetNodeCnt();

             for (i = 0; i < nInbandSubtitleTrackCnt; i ++)
             {
                 pSubTrackInfo = (sSUBTITLETRACKINFO *)FSL_MALLOC(sizeof(sSUBTITLETRACKINFO));
                 if(pSubTrackInfo == NULL) {
                     return OMX_ErrorInsufficientResources;
                 }
                 pSubTrackInfo->nTrackNum = i;
                 pSubTrackInfo->bInBand = OMX_TRUE;
                 SubtitleTrackInfo->Add(pSubTrackInfo);
             }

             nMetaNum = getMetadataNum();
             for (i = 0; i < nMetaNum; i ++) {
                 nMetadataSize = getMetadataSize(i);
                 pMetadata = (OMX_METADATA *)FSL_MALLOC(sizeof(OMX_METADATA) + nMetadataSize);
                 if (pMetadata == NULL)
                     continue;
                 fsl_osal_memset(pMetadata, 0, sizeof(OMX_METADATA) + nMetadataSize);
                 getMetadata(i, pMetadata);
                 if(fsl_osal_strncmp((const fsl_osal_char*)pMetadata->nKey, SubtitleTrackNameKey, 
                             fsl_osal_strlen(SubtitleTrackNameKey)) == 0) {
                     int nTrackIndex = 0;
                     sscanf((char*)pMetadata->nKey, "subtitle_track_name_%d", &nTrackIndex);
                     pSubTrackInfo = SubtitleTrackInfo->GetNode(nTrackIndex + nInbandSubtitleTrackStartNum);
                     fsl_osal_strcpy((fsl_osal_char*)pSubTrackInfo->lanuage, (fsl_osal_char*)pMetadata->nValue);
                 }
                 else if(fsl_osal_strncmp((const fsl_osal_char*)pMetadata->nKey, SubtitleTrackTypeKey, 
                             fsl_osal_strlen(SubtitleTrackTypeKey)) == 0) {
                     int nTrackIndex = 0;
                     sscanf((char*)pMetadata->nKey, "subtitle_track_type_%d", &nTrackIndex);
                     pSubTrackInfo = SubtitleTrackInfo->GetNode(nTrackIndex + nInbandSubtitleTrackStartNum);
                     fsl_osal_strcpy((fsl_osal_char*)pSubTrackInfo->type, (fsl_osal_char*)pMetadata->nValue);
                 }

                 FSL_FREE(pMetadata);
             }            

             InbandSubtitleSource = FSL_NEW(GMInbandSubtitleSource, (this));
             if(InbandSubtitleSource == NULL) {
                 LOG_ERROR("Can't create GMInbandSubtitleSource, disable subtitle.\n");
             }

             if(pSubtitlePlayer == NULL) {
                 if(OMX_ErrorNone != SetupSubtitlePlayer()) {
                     LOG_ERROR("Setup subtitle player failed, disable subtitle.\n");
                 }
             }
         }
    }

    Pipeline->Add(Parser);
    ParserRole = role;

    return OMX_ErrorNone;

err:
    Parser->StateTransDownWard(OMX_StateIdle);
    Parser->StateTransDownWard(OMX_StateLoaded);
    Parser->UnLoad();
    FSL_DELETE(Parser);
    Parser = NULL;
    return ret;
}

OMX_ERRORTYPE GMPlayer::GetContentPipe(CP_PIPETYPE **Pipe, OMX_STRING url)
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;

    *Pipe = NULL;
    if(fsl_osal_strstr(url, "sharedfd://"))
        ret = OMX_GetContentPipe((void**)Pipe, (OMX_STRING)"SHAREDFD_PIPE");
    else if(fsl_osal_strstr(url, "http://"))
        ret = OMX_GetContentPipe((void**)Pipe, (OMX_STRING)"HTTPS_PIPE");
    else if(fsl_osal_strstr(url, "mms://"))
        ret = OMX_GetContentPipe((void**)Pipe, (OMX_STRING)"MMS_PIPE");
    else if(fsl_osal_strstr(url, "udp://"))
        ret = OMX_GetContentPipe((void**)Pipe, (OMX_STRING)"UDPS_PIPE");
    else if(fsl_osal_strstr(url, "rtp://"))
        ret = OMX_GetContentPipe((void**)Pipe, (OMX_STRING)"RTPS_PIPE");
    else
        ret = OMX_GetContentPipe((void**)Pipe, (OMX_STRING)"LOCAL_FILE_PIPE_NEW");

    if(*Pipe == NULL)
        return OMX_ErrorContentPipeCreationFailed;

    return ret;
}

OMX_ERRORTYPE GMPlayer::LoadClock()
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;
    OMX_TIME_CONFIG_CLOCKSTATETYPE ClockState;
    OMX_TIME_CONFIG_ACTIVEREFCLOCKTYPE RefClock;

    ret = LoadComponent((OMX_STRING)"clocksrc", &Clock);
    if(ret != OMX_ErrorNone)
        return ret;

    OMX_S32 i;
    for(i=0; i<(OMX_S32)Clock->nPorts; i++)
        Clock->PortDisable(i);

    Clock->StateTransUpWard(OMX_StateIdle);
    Clock->StateTransUpWard(OMX_StateExecuting);

    OMX_INIT_STRUCT(&ClockState, OMX_TIME_CONFIG_CLOCKSTATETYPE);
    ClockState.eState = OMX_TIME_ClockStateWaitingForStartTime;
    if(bHasAudio == OMX_TRUE)
        ClockState.nWaitMask |= 1 << AUDIO_CLOCK_PORT;
    if(bHasVideo == OMX_TRUE)
        ClockState.nWaitMask |= 1 << VIDEO_CLOCK_PORT;
    OMX_SetConfig(Clock->hComponent, OMX_IndexConfigTimeClockState, &ClockState);

    OMX_INIT_STRUCT(&RefClock, OMX_TIME_CONFIG_ACTIVEREFCLOCKTYPE);
    if(bHasAudio == OMX_TRUE)
        RefClock.eClock = OMX_TIME_RefClockAudio;
    else
        RefClock.eClock = OMX_TIME_RefClockVideo;
    OMX_SetConfig(Clock->hComponent, OMX_IndexConfigTimeActiveRefClock, &RefClock);

    return OMX_ErrorNone;
}

OMX_ERRORTYPE GMPlayer::RemoveClock()
{
    OMX_TIME_CONFIG_CLOCKSTATETYPE ClockState;

    if(Clock == NULL)
        return OMX_ErrorNone;

    OMX_INIT_STRUCT(&ClockState, OMX_TIME_CONFIG_CLOCKSTATETYPE);
    ClockState.eState = OMX_TIME_ClockStateStopped;
    OMX_SetConfig(Clock->hComponent, OMX_IndexConfigTimeClockState, &ClockState);

    if(Parser != NULL)
        DeAttachClock(PARSER_CLOCK_PORT, Parser, 2);
    if(AudioRender != NULL)
        DeAttachClock(AUDIO_CLOCK_PORT, AudioRender, 1);
    if(VideoRender != NULL)
        DeAttachClock(VIDEO_CLOCK_PORT, VideoRender, 1);
    if(VideoDecoder != NULL) {
        OMX_CONFIG_CLOCK sClock;
        OMX_INIT_STRUCT(&sClock, OMX_CONFIG_CLOCK);
        sClock.hClock = NULL;
        OMX_SetConfig(VideoDecoder->hComponent, OMX_IndexConfigClock, &sClock);
    }

    Clock->StateTransDownWard(OMX_StateIdle);
    Clock->StateTransDownWard(OMX_StateLoaded);

    Clock->UnLoad();
    FSL_DELETE(Clock);
    Clock = NULL;

    return OMX_ErrorNone;
}

OMX_ERRORTYPE GMPlayer::AttachClock(
        OMX_U32 nClockPortIndex,
        GMComponent *pComponent,
        OMX_U32 nPortIndex)
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;

    ret = OMX_SetupTunnel(Clock->hComponent, nClockPortIndex, pComponent->hComponent, nPortIndex);
    if(ret != OMX_ErrorNone) {
        LOG_ERROR("Can't attach clock to component %s in port %d\n", pComponent->name, nPortIndex);
        return ret;
    }

    pComponent->TunneledPortEnable(nPortIndex);
    Clock->TunneledPortEnable(nClockPortIndex);
    Clock->WaitTunneledPortEnable(nClockPortIndex);
    pComponent->WaitTunneledPortEnable(nPortIndex);

    return OMX_ErrorNone;
}

OMX_ERRORTYPE GMPlayer::DeAttachClock(
        OMX_U32 nClockPortIndex,
        GMComponent *pComponent,
        OMX_U32 nPortIndex)
{
    OMX_ERRORTYPE ret1 = OMX_ErrorNone;
    OMX_ERRORTYPE ret2 = OMX_ErrorNone;

    Clock->TunneledPortDisable(nClockPortIndex);
    pComponent->TunneledPortDisable(nPortIndex);
    pComponent->WaitTunneledPortDisable(nPortIndex);
    Clock->WaitTunneledPortDisable(nClockPortIndex);

    OMX_SetupTunnel(Clock->hComponent, nClockPortIndex, NULL, 0);
    OMX_SetupTunnel(pComponent->hComponent, nPortIndex, NULL, 0);

    return OMX_ErrorNone;
}

OMX_ERRORTYPE GMPlayer::SetupPipeline()
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;
    OMX_PARAM_U32TYPE u32Type;
    OMX_INIT_STRUCT(&u32Type, OMX_PARAM_U32TYPE);

    if(bHasAudio == OMX_TRUE) {
        ret = SetupAudioPipeline();
        if(ret != OMX_ErrorNone) {
            LOG_ERROR("SetupAudioPipeline() faint\n");
            DisableParserAudioPort();
        }
    }

    if(bHasVideo == OMX_TRUE) {
        ret =SetupVideoPipeline();
        if(ret != OMX_ErrorNone) {
            LOG_ERROR("SetupVideoPipeline() faint\n");
            DisableParserVideoPort();
        }
    }

    if(bHasAudio != OMX_TRUE && bHasVideo != OMX_TRUE){
        LOG_ERROR("SetupPipeline() no audio and no video faint\n");
        ret = OMX_ErrorUndefined;
        }

    return ret;
}

OMX_ERRORTYPE GMPlayer::SetupAudioPipeline()
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;
    OMX_STRING role = NULL;

    role = GetAudioRole(AudioFmt);
    if(role == NULL)
        return OMX_ErrorComponentNotFound;

    if(AudioFmt == OMX_AUDIO_CodingAC3 && bAudioPassthrough == OMX_TRUE)
        role = (OMX_STRING)"audio_processor.ac3toiec937";

    return SetupAudioPipeline(role);
}

OMX_ERRORTYPE GMPlayer::SetupAudioPipeline(OMX_STRING role)
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;

    if(role == NULL)
        return OMX_ErrorComponentNotFound;

    ret = LoadComponent(role, &AudioDecoder);
    if(ret != OMX_ErrorNone) {
        LOG_ERROR("Can't load component with role: %s\n", role);
        return OMX_ErrorInvalidComponentName;
    }

    Parser->PortDisable(Parser->PortPara[AUDIO_PORT_PARA].nStartPortNumber);
    ret = ConnectComponent(Parser, Parser->PortPara[AUDIO_PORT_PARA].nStartPortNumber, AudioDecoder, 0);
    if(ret != OMX_ErrorNone)
        return ret;
    Parser->PortEnable(Parser->PortPara[AUDIO_PORT_PARA].nStartPortNumber);
    Pipeline->Add(AudioDecoder);

    OMX_BOOL bIsPassThrough = (bAudioPassthrough == OMX_TRUE && AudioFmt == OMX_AUDIO_CodingAC3) ? OMX_TRUE : OMX_FALSE;

    if(bUseFakeAudioSink == OMX_FALSE) {
        ret = LoadComponent((OMX_STRING)AUDIO_RENDER, &AudioRender);
        if(ret != OMX_ErrorNone) {
            LOG_ERROR("Can't load component with role: %s\n", role);
            return OMX_ErrorInvalidComponentName;
        }

        if(bIsPassThrough == OMX_TRUE) {
            OMX_AUDIO_PARAM_PORTFORMATTYPE sAudioPortFmt;
            OMX_INIT_STRUCT(&sAudioPortFmt, OMX_AUDIO_PARAM_PORTFORMATTYPE);
            sAudioPortFmt.nPortIndex = 0;
            sAudioPortFmt.eEncoding = OMX_AUDIO_CodingIEC937;
            ret = OMX_SetParameter(AudioRender->hComponent, OMX_IndexParamAudioPortFormat, &sAudioPortFmt);
            if(ret != OMX_ErrorNone)
                return ret;
        }
    }
    else {
        ret = LoadComponent((OMX_STRING)AUDIO_RENDER_FAKE, &AudioRender);
        if(ret != OMX_ErrorNone) {
            LOG_ERROR("Can't load component with role: %s\n", role);
            return OMX_ErrorInvalidComponentName;
        }
    }

    if(bEnableAudioPP == OMX_TRUE && bIsPassThrough != OMX_TRUE) {
        ret = LoadComponent((OMX_STRING)AUDIO_PROCESSOR, &AudioPostProcess);
        if(ret != OMX_ErrorNone) {
            LOG_ERROR("Can't load component with role: %s\n", AUDIO_PROCESSOR);
            bEnableAudioPP = OMX_FALSE;
        }
    }

    if(bEnableAudioPP == OMX_TRUE && bIsPassThrough != OMX_TRUE) {
        ret = ConnectComponent(AudioDecoder, 1, AudioPostProcess, 0);
        if(ret != OMX_ErrorNone)
            return ret;
        Pipeline->Add(AudioPostProcess);
        ret = ConnectComponent(AudioPostProcess, 1, AudioRender, 0);
        if(ret != OMX_ErrorNone)
            return ret;
        Pipeline->Add(AudioRender);
    }
    else {
        ret = ConnectComponent(AudioDecoder, 1, AudioRender, 0);
        if(ret != OMX_ErrorNone)
            return ret;
        Pipeline->Add(AudioRender);
    }

    if(mAudioSink != NULL)
        OMX_SetParameter(AudioRender->hComponent, OMX_IndexParamAudioSink, mAudioSink);

    if (bLiveMode == OMX_TRUE) {
        OMX_CONFIG_BOOLEANTYPE sLiveMode;

        OMX_INIT_STRUCT(&sLiveMode, OMX_CONFIG_BOOLEANTYPE);
        sLiveMode.bEnabled = bLiveMode;

        OMX_SetParameter(AudioRender->hComponent, OMX_IndexParamAudioRenderMode, &sLiveMode);
    }

    bHasAudio = OMX_TRUE;

    return OMX_ErrorNone;
}

OMX_ERRORTYPE GMPlayer::SetupVideoPipeline()
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;
    OMX_STRING role = NULL;
    OMX_PARAM_PORTDEFINITIONTYPE sPortDef1, sPortDef2;
    int i;
    OMX_INIT_STRUCT(&sPortDef1, OMX_PARAM_PORTDEFINITIONTYPE);
    OMX_INIT_STRUCT(&sPortDef2, OMX_PARAM_PORTDEFINITIONTYPE);

    role = GetVideoRole(VideoFmt);
    if(role == NULL)
        return OMX_ErrorComponentNotFound;

    ret = LoadComponent(role, &VideoDecoder);
    if(ret != OMX_ErrorNone) {
        LOG_ERROR("Can't load component with role: %s\n", role);
        return OMX_ErrorInvalidComponentName;
    }

    OMX_BOOL IsDecoderSupportNV12Fmt = OMX_FALSE;
    OMX_U32 buffer_usage = 0; 
    for(i=0; i<(int)(sizeof(VideoProperty)/sizeof(VIDEO_PROPERTY)); i++) {
        if(VideoProperty[i].type == (OMX_U32)VideoFmt) {
            IsDecoderSupportNV12Fmt = VideoProperty[i].IsDecoderSupportNV12Fmt;
            buffer_usage = VideoProperty[i].buffer_usage;
            break;
        }
    }

    if(bStreamingSource == OMX_TRUE) {
        OMX_DECODER_CACHED_THR sThreshold;
        OMX_INIT_STRUCT(&sThreshold, OMX_DECODER_CACHED_THR);
        sThreshold.nPortIndex = 0;
        sThreshold.nMaxDurationMsThreshold = 2000; //2s
        sThreshold.nMaxBufCntThreshold = 45;
        OMX_SetParameter(VideoDecoder->hComponent, OMX_IndexParamDecoderCachedThreshold, &sThreshold);
    }

    if(!fsl_osal_strcmp((fsl_osal_char*)ParserRole, "parser.httplive")) {
        OMX_DECODE_MODE mode = DEC_STREAM_MODE;
        OMX_SetParameter(VideoDecoder->hComponent, OMX_IndexParamDecoderPlayMode, &mode);
    }

    if(OMX_TRUE == IsDecoderSupportNV12Fmt) {
        sPortDef1.nPortIndex = 1;
        OMX_GetParameter(VideoDecoder->hComponent, OMX_IndexParamPortDefinition, &sPortDef1);
        sPortDef1.format.video.eColorFormat = OMX_COLOR_FormatYUV420SemiPlanar;
        OMX_SetParameter(VideoDecoder->hComponent, OMX_IndexParamPortDefinition, &sPortDef1);
    }
#ifdef MX5X
    if(mSurfaceType == OMX_VIDEO_SURFACE_TEXTURE) {
        OMX_U32 nAlignment = 4096;
        OMX_SetParameter(VideoDecoder->hComponent, OMX_IndexParamVideoDecChromaAlign, &nAlignment);
    }
#endif

    Parser->PortDisable(Parser->PortPara[VIDEO_PORT_PARA].nStartPortNumber);
    ret = ConnectComponent(Parser, Parser->PortPara[VIDEO_PORT_PARA].nStartPortNumber, VideoDecoder, 0);
    if(ret != OMX_ErrorNone)
        return ret;
    Parser->PortEnable(Parser->PortPara[VIDEO_PORT_PARA].nStartPortNumber);
    Pipeline->Add(VideoDecoder);

    if(mVideoRenderType >= OMX_VIDEO_RENDER_NUM){
        LOG_ERROR("Video render type not set!\n");
        return OMX_ErrorBadParameter;
    }

    for( i = 0; i< OMX_VIDEO_RENDER_NUM; i++)
        if(VideoRenderRoleTable[i].type == mVideoRenderType)
            break;

    if(i >= OMX_VIDEO_RENDER_NUM){
        LOG_ERROR("Video render type invalid: %d!\n", mVideoRenderType);
        return OMX_ErrorBadParameter;
    }

    printf("VideoRender role: %s\n", VideoRenderRoleTable[i].name);
    
    ret = LoadComponent((char *)VideoRenderRoleTable[i].name, &VideoRender);
    if(ret != OMX_ErrorNone) {
        LOG_ERROR("Can't load component with name: %s\n", VideoRenderRoleTable[i].name);
        return OMX_ErrorInvalidComponentName;
    }

            
    if(mVideoRenderType == OMX_VIDEO_RENDER_FB|| mVideoRenderType == OMX_VIDEO_RENDER_EGL)
        bEnableVideoPP = OMX_TRUE;

    if(bEnableVideoPP == OMX_TRUE) {
        ret = LoadComponent((OMX_STRING)VIDEO_PROCESSOR, &VideoPostProcess);
        if(ret != OMX_ErrorNone) {
            LOG_ERROR("Can't load component with role: %s\n", VIDEO_PROCESSOR);
            bEnableVideoPP = OMX_FALSE;
        }

        sPortDef1.nPortIndex = 0;
        OMX_GetParameter(VideoRender->hComponent, OMX_IndexParamPortDefinition, &sPortDef1);
        sPortDef2.nPortIndex = 1;
        OMX_GetParameter(VideoPostProcess->hComponent, OMX_IndexParamPortDefinition, &sPortDef2);
        sPortDef2.format.video.nFrameWidth = sPortDef1.format.video.nFrameWidth;
        sPortDef2.format.video.nFrameHeight = sPortDef1.format.video.nFrameHeight;
        sPortDef2.format.video.eColorFormat = sPortDef1.format.video.eColorFormat;

        if(mVideoRenderType == OMX_VIDEO_RENDER_EGL && sPortDef2.nBufferCountActual < 2)
            sPortDef2.nBufferCountActual = 2;

        OMX_SetParameter(VideoPostProcess->hComponent, OMX_IndexParamPortDefinition, &sPortDef2);
    }

    if(bEnableVideoPP == OMX_TRUE) {
        ret = ConnectComponent(VideoDecoder, 1, VideoPostProcess, 0);
        if(ret != OMX_ErrorNone)
            return ret;
        Pipeline->Add(VideoPostProcess);
        ret = ConnectComponent(VideoPostProcess, 1, VideoRender, 0);
        if(ret != OMX_ErrorNone)
            return ret;
        Pipeline->Add(VideoRender);
    }
    else {
        sPortDef1.nPortIndex = 1;
        OMX_GetParameter(VideoDecoder->hComponent, OMX_IndexParamPortDefinition, &sPortDef1);
        sPortDef2.nPortIndex = 0;
        OMX_GetParameter(VideoRender->hComponent, OMX_IndexParamPortDefinition, &sPortDef1);
        sPortDef2.format.video.eColorFormat = sPortDef1.format.video.eColorFormat;
        OMX_SetParameter(VideoRender->hComponent, OMX_IndexParamPortDefinition, &sPortDef1);

        ret = ConnectComponent(VideoDecoder, 1, VideoRender, 0);
        if(ret != OMX_ErrorNone)
            return ret;
        Pipeline->Add(VideoRender);
    }

#ifdef EGL_RENDER
    if(mVideoRenderType == OMX_VIDEO_RENDER_EGL){
        ret = GMCreateEglContext(&EglContext, sPortDef1.format.video.nFrameWidth,
                sPortDef1.format.video.nFrameHeight, GMEGLDisplayX11);
        if(ret != OMX_ErrorNone)
            return ret;

        VideoRender->RegistSysBufferCallback(AppShowFrame, this);
    }
#endif

    if(mVideoRenderType == OMX_VIDEO_RENDER_OVERLAY ||mVideoRenderType == OMX_VIDEO_RENDER_SURFACE){
        LOG_DEBUG("pass surface to render in SetupVideoPipeline(), %p\n", mSurface);
        if(mSurface != NULL ) {
            ret = OMX_SetConfig(VideoRender->hComponent, OMX_IndexParamSurface, mSurface);
            if(ret != OMX_ErrorNone)
                return ret;

            OMX_SetConfig(VideoRender->hComponent, OMX_IndexParamBufferUsage, &buffer_usage);

            OMX_CONFIG_ROTATIONTYPE sRotation;
            OMX_INIT_STRUCT(&sRotation, OMX_CONFIG_ROTATIONTYPE);
            sRotation.nPortIndex = 0;
            sRotation.nRotation = mRotate;
            OMX_SetConfig(VideoRender->hComponent,OMX_IndexConfigCommonRotate,&sRotation);
        }
    }

    if (mScalingMode != -1) {
        ret = OMX_SetConfig(VideoRender->hComponent, OMX_IndexConfigScalingMode, &mScalingMode);
        if(ret != OMX_ErrorNone)
            return ret;
    }

    return OMX_ErrorNone;
}

OMX_ERRORTYPE GMPlayer::StartPipeline()
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;

#ifdef ANDROID_BUILD
    PmContext = CreatePMeoryContext();
#endif

#if 0
    if(bHasVideo == OMX_TRUE && VideoFmt != OMX_VIDEO_CodingWMV9) {
        VideoDecoder->SetPortBufferAllocateType(0, GM_SELF_ALLOC);
        Parser->SetPortBufferAllocateType(Parser->PortPara[VIDEO_PORT_PARA].nStartPortNumber, GM_PEER_ALLOC);
        Parser->PortDisable(Parser->PortPara[VIDEO_PORT_PARA].nStartPortNumber);
        Parser->PortEnable(Parser->PortPara[VIDEO_PORT_PARA].nStartPortNumber);
    }
#endif

    ret = Parser->StartProcess();
    if(ret != OMX_ErrorNone)
        return ret;

    if(bHasAudio == OMX_TRUE) {
        ret = StartAudioPipeline();
        if(ret != OMX_ErrorNone)
            return ret;
    }

    if(bHasVideo == OMX_TRUE) {
        ret = StartVideoPipeline();
        if(ret != OMX_ErrorNone)
            return ret;
    }

    return OMX_ErrorNone;
}

OMX_ERRORTYPE GMPlayer::StartAudioPipeline()
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;

    ret = AudioDecoder->StateTransUpWard(OMX_StateIdle);
    if(ret != OMX_ErrorNone)
        return ret;
    ret = AudioDecoder->StateTransUpWard(OMX_StateExecuting);
    if(ret != OMX_ErrorNone)
        return ret;
    ret = Parser->PassOutBufferToPeer(Parser->PortPara[AUDIO_PORT_PARA].nStartPortNumber);
    if(ret != OMX_ErrorNone)
        return ret;
    ret = AudioDecoder->StartProcess();
    if(ret != OMX_ErrorNone)
        return ret;

    OMX_BOOL bIsPassThrough = (bAudioPassthrough == OMX_TRUE && AudioFmt == OMX_AUDIO_CodingAC3) ? OMX_TRUE : OMX_FALSE;

    if(bEnableAudioPP == OMX_TRUE && bIsPassThrough != OMX_TRUE) {
        ret = AudioPostProcess->StateTransUpWard(OMX_StateIdle);
        if(ret != OMX_ErrorNone)
            return ret;
        ret = AudioPostProcess->StateTransUpWard(OMX_StateExecuting);
        if(ret != OMX_ErrorNone)
            return ret;
        ret = AudioDecoder->PassOutBufferToPeer(1);
        if(ret != OMX_ErrorNone)
            return ret;
        ret = AudioPostProcess->StartProcess();
        if(ret != OMX_ErrorNone)
            return ret;
    }

    ret = AudioRender->StateTransUpWard(OMX_StateIdle);
    if(ret != OMX_ErrorNone)
        return ret;
    ret = AudioRender->StateTransUpWard(OMX_StatePause);
    if(ret != OMX_ErrorNone)
        return ret;
    AttachClock(AUDIO_CLOCK_PORT, AudioRender, 1);
    if(bEnableAudioPP == OMX_TRUE && bIsPassThrough != OMX_TRUE)
        ret = AudioPostProcess->PassOutBufferToPeer(1);
    else
        ret = AudioDecoder->PassOutBufferToPeer(1);
    if(ret != OMX_ErrorNone)
        return ret;

    return OMX_ErrorNone;
}


OMX_ERRORTYPE GMPlayer::StartVideoPipeline()
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;

    if(Clock != NULL) {
        OMX_CONFIG_CLOCK sClock;
        OMX_INIT_STRUCT(&sClock, OMX_CONFIG_CLOCK);
        sClock.hClock = Clock->hComponent;
        OMX_SetConfig(VideoDecoder->hComponent, OMX_IndexConfigClock, &sClock);
    }

    ret = VideoDecoder->StateTransUpWard(OMX_StateIdle);
    if(ret != OMX_ErrorNone)
        return ret;

    ret = VideoDecoder->StateTransUpWard(OMX_StateExecuting);
    if(ret != OMX_ErrorNone)
        return ret;

    if(bVideoDirectRendering){
        VideoDecoder->SetPortBufferAllocateType(1, GM_PEER_ALLOC);
        VideoRender->SetPortBufferAllocateType(0, GM_SELF_ALLOC);
    }
    
    if(mVideoRenderType == OMX_VIDEO_RENDER_EGL){
        VideoPostProcess->SetPortBufferAllocateType(1, GM_USE_EGLIMAGE);
        VideoRender->SetPortBufferAllocateType(0, GM_PEER_BUFFER);
    }
    
#ifdef ANDROID_BUILD
    if(!bVideoDirectRendering)
        VideoDecoder->SetPortBufferAllocateType(1, GM_CLIENT_ALLOC);
#endif

    ret = Parser->PassOutBufferToPeer(Parser->PortPara[VIDEO_PORT_PARA].nStartPortNumber);
    if(ret != OMX_ErrorNone)
        return ret;

    ret = VideoDecoder->StartProcess();
    if(ret != OMX_ErrorNone)
        return ret;

    if(bEnableVideoPP == OMX_TRUE) {
        ret = VideoPostProcess->StateTransUpWard(OMX_StateIdle);
        if(ret != OMX_ErrorNone)
            return ret;
        ret = VideoPostProcess->StateTransUpWard(OMX_StateExecuting);
        if(ret != OMX_ErrorNone)
            return ret;
        ret = VideoDecoder->PassOutBufferToPeer(1);
        if(ret != OMX_ErrorNone)
            return ret;
        ret = VideoPostProcess->StartProcess();
        if(ret != OMX_ErrorNone)
            return ret;
    }

    ret = VideoRender->StateTransUpWard(OMX_StateIdle);
    if(ret != OMX_ErrorNone)
        return ret;

    AttachClock(VIDEO_CLOCK_PORT, VideoRender, 1);

    ret = VideoRender->StateTransUpWard(OMX_StatePause);
    if(ret != OMX_ErrorNone)
        return ret;

    SetDefaultScreenMode();

    if(bEnableVideoPP == OMX_TRUE)
        ret = VideoPostProcess->PassOutBufferToPeer(1);
    else
        ret = VideoDecoder->PassOutBufferToPeer(1);
    if(ret != OMX_ErrorNone)
        return ret;

    return OMX_ErrorNone;
}

OMX_BOOL GMPlayer::AdjustAudioVolume(OMX_BOOL bVolumeUp)
{
    OMX_BOOL ret = OMX_TRUE;
	OMX_AUDIO_CONFIG_VOLUMETYPE Volume;

    fsl_osal_mutex_lock(Lock);
	if (AudioPostProcess)
	{
		if (AudioPostProcess->hComponent)
		{
			OMX_INIT_STRUCT(&Volume, OMX_AUDIO_CONFIG_VOLUMETYPE);
			OMX_GetConfig(AudioPostProcess->hComponent, OMX_IndexConfigAudioVolume, &Volume);
			if (bVolumeUp == OMX_TRUE)
			{
				Volume.sVolume.nValue += 10;
			}
			else
			{
				Volume.sVolume.nValue -= 10;
			}
			OMX_SetConfig(AudioPostProcess->hComponent, OMX_IndexConfigAudioVolume, &Volume);

			fsl_osal_mutex_unlock(Lock);
			return OMX_TRUE;
		}
	}

    fsl_osal_mutex_unlock(Lock);
    return OMX_FALSE;
}

OMX_BOOL GMPlayer::EnableDisablePEQ(OMX_BOOL bAudioEffect)
{
	OMX_ERRORTYPE ret = OMX_ErrorNone;
	OMX_AUDIO_CONFIG_EQUALIZERTYPE Equalizer;

    fsl_osal_mutex_lock(Lock);
	if (AudioPostProcess)
	{
		if (AudioPostProcess->hComponent)
		{
			OMX_INIT_STRUCT(&Equalizer, OMX_AUDIO_CONFIG_EQUALIZERTYPE);
			ret = OMX_GetConfig(AudioPostProcess->hComponent, OMX_IndexConfigAudioEqualizer, &Equalizer);
			Equalizer.bEnable = bAudioEffect;
			ret = OMX_SetConfig(AudioPostProcess->hComponent, OMX_IndexConfigAudioEqualizer, &Equalizer);
			fsl_osal_mutex_unlock(Lock);
			return OMX_TRUE;
		}
	}

    fsl_osal_mutex_unlock(Lock);

    return OMX_FALSE;
}

OMX_U32 GMPlayer::GetBandNumPEQ()
{
    OMX_U32 nPEQBandNum = 0;
	OMX_AUDIO_CONFIG_EQUALIZERTYPE Equalizer;

    fsl_osal_mutex_lock(Lock);
	if (AudioPostProcess)
	{
		if (AudioPostProcess->hComponent)
		{
			OMX_INIT_STRUCT(&Equalizer, OMX_AUDIO_CONFIG_EQUALIZERTYPE);
			OMX_GetConfig(AudioPostProcess->hComponent, OMX_IndexConfigAudioEqualizer, &Equalizer);
			nPEQBandNum = Equalizer.sBandIndex.nValue;
		}
	}

    fsl_osal_mutex_unlock(Lock);

    return nPEQBandNum;
}

OMX_BOOL GMPlayer::SetAudioEffectPEQ(OMX_U32 nBindIndex, OMX_U32 nFC, OMX_S32 nGain)
{
	OMX_ERRORTYPE ret = OMX_ErrorNone;
	OMX_AUDIO_CONFIG_EQUALIZERTYPE Equalizer;

    fsl_osal_mutex_lock(Lock);
	if (AudioPostProcess)
	{
		if (AudioPostProcess->hComponent)
		{
			OMX_INIT_STRUCT(&Equalizer, OMX_AUDIO_CONFIG_EQUALIZERTYPE);
			ret = OMX_GetConfig(AudioPostProcess->hComponent, OMX_IndexConfigAudioEqualizer, &Equalizer);
			Equalizer.sBandIndex.nValue = nBindIndex;
			Equalizer.sCenterFreq.nValue = nFC;
			Equalizer.sBandLevel.nValue = nGain;
			ret = OMX_SetConfig(AudioPostProcess->hComponent, OMX_IndexConfigAudioEqualizer, &Equalizer);
			fsl_osal_mutex_unlock(Lock);
			return OMX_TRUE;
		}
	}

    fsl_osal_mutex_unlock(Lock);

    return OMX_FALSE;
}

OMX_U32 GMPlayer::GetAudioTrackNum()
{
    OMX_U32 nAudioTrackNum;
    fsl_osal_mutex_lock(Lock);
    nAudioTrackNum = AudioTrackInfo->GetNodeCnt();
    fsl_osal_mutex_unlock(Lock);
    LOG_DEBUG("Total audio track number: %d\n", nAudioTrackNum);
    return nAudioTrackNum;
}

OMX_STRING GMPlayer::GetAudioTrackName(
        OMX_U32 nTrackIndex)
{
    fsl_osal_mutex_lock(Lock);
    if(nTrackIndex >= AudioTrackInfo->GetNodeCnt()) {
        fsl_osal_mutex_unlock(Lock);
        return 0;
    }
    sAUDIOTRACKINFO *pTrackInfo = AudioTrackInfo->GetNode(nTrackIndex);
    fsl_osal_mutex_unlock(Lock);
    return (OMX_STRING) pTrackInfo->lanuage;
}

OMX_U32 GMPlayer::GetCurAudioTrack()
{
    OMX_U32 nCurAudioTrack = 0;
    OMX_PARAM_U32TYPE u32Type;

    if(Parser) {
        OMX_INIT_STRUCT(&u32Type, OMX_PARAM_U32TYPE);
        u32Type.nPortIndex = Parser->PortPara[AUDIO_PORT_PARA].nStartPortNumber;
        OMX_GetParameter(Parser->hComponent, OMX_IndexParamActiveStream, &u32Type);
        nCurAudioTrack = u32Type.nU32;
        LOG_DEBUG("Current audio track: %d\n", nCurAudioTrack);
    }

    return nCurAudioTrack;
}

OMX_BOOL GMPlayer::SelectAudioTrack(OMX_U32 nSelectedTrack)
{
    OMX_U32 nAudioTrackNum;
    OMX_U32 nCurAudioTrack;

    fsl_osal_mutex_lock(Lock);

    printf("Select audio track %d.\n", nSelectedTrack);

    if (!(State == GM_STATE_PLAYING || State == GM_STATE_PAUSE || State == GM_STATE_LOADED)) {
        LOG_WARNING("Select audio track in invalid state.\n");
        fsl_osal_mutex_unlock(Lock);
        return OMX_FALSE;
    }

    nAudioTrackNum = AudioTrackInfo->GetNodeCnt();;
    if (nSelectedTrack >= nAudioTrackNum) {
        LOG_WARNING("Selected audio track is out of range.\n");
        fsl_osal_mutex_unlock(Lock);
        return OMX_FALSE;
    }

    nCurAudioTrack = GetCurAudioTrack();
    if (nSelectedTrack == nCurAudioTrack) {
        LOG_DEBUG("Selected audio track is the same as current track.\n");
        fsl_osal_mutex_unlock(Lock);
        return OMX_TRUE;
    }

    OMX_PARAM_CAPABILITY sMediaSeekable;

    OMX_INIT_STRUCT(&sMediaSeekable, OMX_PARAM_CAPABILITY);
    sMediaSeekable.nPortIndex = OMX_ALL;
    OMX_GetParameter(Parser->hComponent, OMX_IndexParamMediaSeekable, &sMediaSeekable);
    if (sMediaSeekable.bCapability != OMX_TRUE) {
        LOG_ERROR("Meida file is not seekable. Can't select audio track.\n");
        fsl_osal_mutex_unlock(Lock);
        return OMX_FALSE;
    }


    OMX_STRING role = NULL;
    AudioFmt = AudioTrackInfo->GetNode(nSelectedTrack)->eCodingType;
    role = GetAudioRole(AudioFmt);
    if(role == NULL) {
        LOG_ERROR("Can't find role for audio type: %d\n", AudioFmt);
        fsl_osal_mutex_unlock(Lock);
        return OMX_FALSE;
    }

    if(bAudioPassthrough == OMX_TRUE) {
        if(AudioFmt == OMX_AUDIO_CodingAC3)
            role = (OMX_STRING)"audio_processor.ac3toiec937";
    }

    ReSetupAudioPipeline(role, nSelectedTrack);

    fsl_osal_mutex_unlock(Lock);

    LOG_DEBUG("Select audio track out.\n");
    return OMX_TRUE;
}

OMX_U32 GMPlayer::GetSubtitleTrackNum()
{
    OMX_U32 nSubtitleTrackNum;
    fsl_osal_mutex_lock(Lock);
    nSubtitleTrackNum = SubtitleTrackInfo->GetNodeCnt();
    fsl_osal_mutex_unlock(Lock);
    LOG_DEBUG("Total subtitle track number: %d\n", nSubtitleTrackNum);
    return nSubtitleTrackNum;
}

OMX_S32 GMPlayer::GetSubtitleCurTrack()
{
    return nCurSubtitleTrack;
}

OMX_STRING GMPlayer::GetSubtitleTrackName(OMX_U32 nTrackIndex)
{
    fsl_osal_mutex_lock(Lock);
    if(nTrackIndex >= SubtitleTrackInfo->GetNodeCnt()) {
        fsl_osal_mutex_unlock(Lock);
        return NULL;
    }
    sSUBTITLETRACKINFO *pTrackInfo = SubtitleTrackInfo->GetNode(nTrackIndex);
    fsl_osal_mutex_unlock(Lock);
    return (OMX_STRING) pTrackInfo->lanuage;
}

OMX_BOOL GMPlayer::SelectSubtitleTrack(OMX_S32 nSelectedTrack)
{
    OMX_S32 nSubtitleTrackNum;

    printf("Select subtitle Track: %d\n", nSelectedTrack);

    if (!(State == GM_STATE_PLAYING || State == GM_STATE_PAUSE || State == GM_STATE_LOADED))
    {
        LOG_WARNING("Select subtitle track in invalid state.\n");
        return OMX_FALSE;
    }
    nSubtitleTrackNum = (OMX_S32)SubtitleTrackInfo->GetNodeCnt();
    if (nSelectedTrack > nSubtitleTrackNum)
    {
        LOG_WARNING("Selected subtitle track is out of range.\n");
        return OMX_FALSE;
    }
    if (nSelectedTrack == nCurSubtitleTrack)
    {
        LOG_DEBUG("Selected subtitle track is the same as current track.\n");
        return OMX_TRUE;
    }


    fsl_osal_mutex_lock(Lock);

    OMX_ERRORTYPE ret = OMX_ErrorNone;
    bHasSubtitle = OMX_FALSE;

    pSubtitlePlayer->Reset();

    if(nSelectedTrack < 0) {
        sSUBTITLETRACKINFO *pCurTrackInfo = SubtitleTrackInfo->GetNode(nCurSubtitleTrack);
        if(pCurTrackInfo->bInBand == OMX_TRUE)
            SelectInbandSubtitle(-1);

        nCurSubtitleTrack = -1;
        fsl_osal_mutex_unlock(Lock);
        return OMX_TRUE;
    }

    sSUBTITLETRACKINFO *pSelectTrackInfo = SubtitleTrackInfo->GetNode(nSelectedTrack);;
    if(nCurSubtitleTrack >= 0) {
        sSUBTITLETRACKINFO *pCurTrackInfo = SubtitleTrackInfo->GetNode(nCurSubtitleTrack);
        if(pCurTrackInfo->bInBand == OMX_TRUE && pSelectTrackInfo->bInBand != OMX_TRUE)
            SelectInbandSubtitle(-1);
    }

    if(pSelectTrackInfo->bInBand == OMX_TRUE) {
        ret = SelectInbandSubtitle(nSelectedTrack);
    }
    else {
        ret = SelectOutbandSubtitle(nSelectedTrack);
    }
    if(ret != OMX_ErrorNone) {
        nCurSubtitleTrack = -1;
        LOG_WARNING("Select subtitle %d failed, ret = %d, disable subtilte.\n", nSelectedTrack, ret);
        fsl_osal_mutex_unlock(Lock);
        return OMX_TRUE;
    }

    nCurSubtitleTrack = nSelectedTrack;
    bHasSubtitle = OMX_TRUE;

    fsl_osal_mutex_unlock(Lock);

    return OMX_TRUE;
}

OMX_ERRORTYPE GMPlayer::GetSubtitleNextSample(OMX_U8**ppData, OMX_S32* pFilledLen, OMX_TICKS* pTime)
{
    OMX_SUBTITLE_SAMPLE sSample;
    OMX_ERRORTYPE ret;
    if (!(State == GM_STATE_PLAYING || State == GM_STATE_PAUSE || State == GM_STATE_LOADED)){
        LOG_WARNING("Require subtitle data in invalid state.\n");
        *ppData=NULL;
        return OMX_ErrorIncorrectStateOperation;
    }
    fsl_osal_mutex_lock(Lock);
    ret=OMX_GetConfig(Parser->hComponent, OMX_IndexParamSubtitleNextSample,&sSample);
    fsl_osal_mutex_unlock(Lock);	
    *ppData=sSample.pBuffer;
    *pFilledLen=sSample.nFilledLen;
    *pTime=sSample.nTimeStamp;
    return ret;
}

OMX_ERRORTYPE GMPlayer::SeekTrackToPlaytime(OMX_U32 nPortIndex)
{
	OMX_ERRORTYPE ret = OMX_ErrorNone;
    OMX_TIME_CONFIG_SEEKMODETYPE seek_mode;
    OMX_TIME_CONFIG_TIMESTAMPTYPE ts;
	OMX_TIME_SEEKMODETYPE mode = OMX_TIME_SeekModeAccurate;
	OMX_TICKS position;
    OMX_TICKS Dur = 0;

	fsl_osal_mutex_unlock(Lock);
	ret = GetMediaPosition(&position);
	fsl_osal_mutex_lock(Lock);
    if (ret != OMX_ErrorNone)
        return ret;

	OMX_INIT_STRUCT(&seek_mode, OMX_TIME_CONFIG_SEEKMODETYPE);
	seek_mode.eType = mode;
    ret = OMX_SetConfig(Parser->hComponent, OMX_IndexConfigTimeSeekMode, &seek_mode);
    if (ret != OMX_ErrorNone)
        return ret;

	GetStreamDuration(&Dur, nPortIndex);

    if(position > (Dur - OMX_TICKS_PER_SECOND))
        position = Dur - OMX_TICKS_PER_SECOND;

    LOG_DEBUG("Seek to %lld, port: %d\n", position, nPortIndex);

    OMX_INIT_STRUCT(&ts, OMX_TIME_CONFIG_TIMESTAMPTYPE);
	ts.nPortIndex = nPortIndex;
    ts.nTimestamp = position;
    ret = OMX_SetConfig(Parser->hComponent, OMX_IndexConfigTimePosition, &ts);
    if (ret != OMX_ErrorNone)
        return ret;

	return ret;
}

OMX_BOOL GMPlayer::AddRemoveAudioPP(OMX_BOOL bAddComponent)
{
	OMX_BOOL ret = OMX_TRUE;

        if(bStreamingSource == OMX_TRUE)
            return OMX_FALSE;

        if(bAudioPassthrough == OMX_TRUE) {
            bEnableAudioPP = bAddComponent;
            return OMX_FALSE;
        }

	fsl_osal_mutex_lock(Lock);
	if (State == GM_STATE_STOP)
	{
		bEnableAudioPP = bAddComponent;
	}
	else if (State == GM_STATE_PLAYING)
	{
		ret = AddRemoveComponent((OMX_STRING)AUDIO_PROCESSOR, &AudioPostProcess, AudioDecoder, \
				bAddComponent);
		if (ret != OMX_TRUE)
		{
			LOG_WARNING("Add remove component failed.\n");
		}
		bEnableAudioPP = bAddComponent;
	}
	else if (State == GM_STATE_PAUSE)
	{
		/* Postponed to playing state to avoid lose audio data. */
		bAddRemoveComponentPostponed = bAddComponent;
		bAddRemoveComponentReceived = OMX_TRUE;
	}
	else
	{
		LOG_WARNING("Add remove audio post processor in wrong state.\n");
	}

	fsl_osal_mutex_unlock(Lock);
	return ret;
}

OMX_BOOL GMPlayer::AddRemoveComponent(OMX_STRING role, GMComponent **ppComponent, GMComponent *pUpComponent, OMX_BOOL bAddComponent)
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;
	GMComponent *pComponentPtr;
	OMX_BOOL bComponentInPipeline = OMX_FALSE;
    OMX_U32 ComponentNum, i;

	/** Check if the component in the pipeline */
    ComponentNum = Pipeline->GetNodeCnt();
    for(i=0; i<ComponentNum; i++) {
        pComponentPtr = Pipeline->GetNode(i);
        if(pComponentPtr == *ppComponent)
		{
			bComponentInPipeline = OMX_TRUE;
			break;
		}
    }

	if (bAddComponent == OMX_FALSE && bComponentInPipeline == OMX_TRUE)
	{
		ret = RemoveComponent(*ppComponent);
		if (ret != OMX_ErrorNone)
		{
			LOG_WARNING("Remove component failed.\n");
			return OMX_FALSE;
		}
		*ppComponent = NULL;
	}
	else if (bAddComponent == OMX_TRUE && bComponentInPipeline == OMX_FALSE)
	{
		ret = AddComponent(role, ppComponent, pUpComponent);
		if (ret != OMX_ErrorNone)
		{
			LOG_WARNING("Remove component failed.\n");
			return OMX_FALSE;
		}
	}

    return OMX_TRUE;
}

OMX_ERRORTYPE GMPlayer::RemoveComponent(GMComponent *pComponent)
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;
	GMComponent *pUpComponent = NULL;
	GMComponent *pDownComponent = NULL;
	GMComponent *pComponentPtr = NULL;
    OMX_U32 ComponentNum, i;

	/** Get up and down component in the pipeline, Just valid for codec or post processor component */
    ComponentNum = Pipeline->GetNodeCnt();
    for(i=0; i<ComponentNum; i++) {
        pComponentPtr = Pipeline->GetNode(i);
        if(pComponentPtr == pComponent)
		{
			if (i<ComponentNum-1)
			{
				pDownComponent = Pipeline->GetNode(i+1);
				break;
			}
		}
		pUpComponent = pComponentPtr;
    }

	ret = DoRemoveComponent(pUpComponent, pComponent, pDownComponent);
	if (ret != OMX_ErrorNone)
	{
		LOG_DEBUG("Remove component operate.\n");
		return ret;
	}

    return ret;
}

OMX_ERRORTYPE GMPlayer::ConnectAndStartComponent(GMComponent *pOutComp, OMX_U32 nOutPortIndex, GMComponent *pInComp, OMX_U32 nInPortIndex)
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;

	ret = ConnectComponent(pOutComp, nOutPortIndex, pInComp, nInPortIndex);
	if (ret != OMX_ErrorNone)
	{
		LOG_DEBUG("Connect component failed.\n");
		return ret;
	}

	ret = pOutComp->PortEnable(nOutPortIndex);
	if (ret != OMX_ErrorNone)
	{
		LOG_DEBUG("Up component port enable failed.\n");
		return ret;
	}

	ret = pInComp->PortEnable(nInPortIndex);
	if (ret != OMX_ErrorNone)
	{
		LOG_DEBUG("Down component port enable failed.\n");
		return ret;
	}

	ret = pOutComp->StartProcess();
	if (ret != OMX_ErrorNone)
	{
		LOG_DEBUG("Up component start process failed.\n");
		return ret;
	}

	ret = pOutComp->PassOutBufferToPeer(nOutPortIndex);
	if (ret != OMX_ErrorNone)
	{
		LOG_DEBUG("Up component pass out buffer to peer failed.\n");
		return ret;
	}

    return OMX_ErrorNone;
}

OMX_ERRORTYPE GMPlayer::DoRemoveComponent(GMComponent *pUpComponent, GMComponent *pComponent, GMComponent *pDownComponent)
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;

	if (pComponent == NULL || pUpComponent == NULL || pDownComponent == NULL)
	{
		LOG_WARNING("Invalid parameter.\n");
		return OMX_ErrorBadParameter;
	}

	ret = pUpComponent->PortDisable(FILTER_OUT_PORT);
	if (ret != OMX_ErrorNone)
	{
		LOG_DEBUG("Up component port disable failed.\n");
		return ret;
	}

	ret = pComponent->PortDisable(FILTER_INPUT_PORT);
	if (ret != OMX_ErrorNone)
	{
		LOG_DEBUG("Up component port disable failed.\n");
		return ret;
	}

	ret = pComponent->PortDisable(FILTER_OUT_PORT);
	if (ret != OMX_ErrorNone)
	{
		LOG_DEBUG("Component port disable failed.\n");
		return ret;
	}

	ret = pDownComponent->PortDisable(FILTER_INPUT_PORT);
	if (ret != OMX_ErrorNone)
	{
		LOG_DEBUG("Component port disable failed.\n");
		return ret;
	}

	ret = ConnectAndStartComponent(pUpComponent, FILTER_OUT_PORT, pDownComponent, FILTER_INPUT_PORT);
	if (ret != OMX_ErrorNone)
	{
		LOG_DEBUG("Connect component failed.\n");
		return ret;
	}

	/** Remove component from pipe line */
    OMX_U32 ComponentNum, i;
	GMComponent *pComponentPtr;
    ComponentNum = Pipeline->GetNodeCnt();
    for(i=0; i<ComponentNum; i++) {
		pComponentPtr = Pipeline->GetNode(i);
		if (pComponentPtr == pComponent)
		{
			Pipeline->Remove(pComponentPtr);
			pComponentPtr->StateTransDownWard(OMX_StateIdle);
			pComponentPtr->StateTransDownWard(OMX_StateLoaded);
			pComponentPtr->UnLoad();
                        FSL_DELETE(pComponentPtr);
		}
	}

    return ret;
}

OMX_ERRORTYPE GMPlayer::AddComponent(OMX_STRING role, GMComponent **ppComponent, GMComponent *pUpComponent)
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;
	GMComponent *pDownComponent = NULL;
	GMComponent *pComponentPtr = NULL;
    OMX_U32 ComponentNum, i;

	/** Get down component in the pipeline, Just valid for codec or post processor component */
    ComponentNum = Pipeline->GetNodeCnt();
    for(i=0; i<ComponentNum; i++) {
        pComponentPtr = Pipeline->GetNode(i);
        if(pComponentPtr == pUpComponent)
		{
			if (i<ComponentNum-1)
			{
				pDownComponent = Pipeline->GetNode(i+1);
				break;
			}
		}
    }

	if (pUpComponent == NULL || pDownComponent == NULL)
	{
		LOG_WARNING("Invalid parameter.\n");
		return OMX_ErrorBadParameter;
	}

	ret = LoadComponent(role, ppComponent);
	if (ret != OMX_ErrorNone)
	{
		LOG_DEBUG("Load component: %s failed.\n", role);
		return ret;
	}

	GMComponent *pComponent = *ppComponent;

//	ret = pComponent->PortDisable(OMX_ALL);
//	if (ret != OMX_ErrorNone)
//	{
//		LOG_DEBUG("Component port disable failed.\n");
//        goto err;
//	}
	ret = pComponent->PortDisable(FILTER_INPUT_PORT);
	if (ret != OMX_ErrorNone)
	{
		LOG_DEBUG("Component port disable failed.\n");
        goto err;
	}
	ret = pComponent->PortDisable(FILTER_OUT_PORT);
	if (ret != OMX_ErrorNone)
	{
		LOG_DEBUG("Component port disable failed.\n");
        goto err;
	}

	ret = pComponent->StateTransUpWard(OMX_StateIdle);
	if (ret != OMX_ErrorNone)
	{
        goto err;
	}

	ret = pComponent->StateTransUpWard(OMX_StateExecuting);
	if (ret != OMX_ErrorNone)
	{
        goto err;
	}

	/* Disconnect and then connect */
	ret = pUpComponent->PortDisable(FILTER_OUT_PORT);
	if (ret != OMX_ErrorNone)
	{
		LOG_DEBUG("Up component port disable failed.\n");
        goto err;
	}

	ret = pDownComponent->PortDisable(FILTER_INPUT_PORT);
	if (ret != OMX_ErrorNone)
	{
		LOG_DEBUG("Up component port disable failed.\n");
        goto err;
	}

	ret = ConnectAndStartComponent(pUpComponent, FILTER_OUT_PORT, pComponent, FILTER_INPUT_PORT);
	if (ret != OMX_ErrorNone)
	{
		LOG_DEBUG("Connect and start component failed.\n");
        goto err;
	}

	ret = ConnectAndStartComponent(pComponent, FILTER_OUT_PORT, pDownComponent, FILTER_INPUT_PORT);
	if (ret != OMX_ErrorNone)
	{
		LOG_DEBUG("Connect and start component failed.\n");
        goto err;
	}

	/* Add component to pipe line */
    Pipeline->Add(pComponent, pUpComponent);

    return ret;

err:
	pComponentPtr->StateTransDownWard(OMX_StateIdle);
	pComponentPtr->StateTransDownWard(OMX_StateLoaded);
	pComponent->UnLoad();
        FSL_DELETE(pComponent);
	return ret;
}

static OMX_ERRORTYPE DumpRect(OMX_CONFIG_RECTTYPE *pRect)
{
    LOG_DEBUG("\n\
            rect width %d, rect crop left %d\n\
            rect height %d,rect crop top %d\n",
            pRect->nWidth,pRect->nLeft,
            pRect->nHeight,pRect->nTop);

    return OMX_ErrorNone;
}

static OMX_ERRORTYPE DumpOutputMode(OMX_CONFIG_OUTPUTMODE *pOutputMode)
{
    LOG_DEBUG("%s,%d.\n",__FUNCTION__,__LINE__);
    LOG_DEBUG("\n\
            is tv out %d\n\
            fb layer %d\n\
            tv mode %d\n\
            rotate %d\n",
            pOutputMode->bTv,
            pOutputMode->eFbLayer,
            pOutputMode->eTvMode,
            pOutputMode->eRotation);
    LOG_DEBUG("input rect \n");
    DumpRect(&pOutputMode->sRectIn);
    LOG_DEBUG("output rect \n");
    DumpRect(&pOutputMode->sRectOut);
    return OMX_ErrorNone;
}

static OMX_ERRORTYPE CalcCrop(SCREENTYPE eScreenParam,
        OMX_CONFIG_RECTTYPE *pInRect,
        OMX_CONFIG_RECTTYPE *pOutRect,
        OMX_CONFIG_RECTTYPE *pCropRect)
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;
    OMX_U32 nWidth, nHeight;

    if(eScreenParam == SCREEN_STD) {
        /* Need set display crop */
        if(pInRect->nWidth * pOutRect->nHeight >= pInRect->nHeight * pOutRect->nWidth) {
            /* Crop display height */
            nHeight = pInRect->nHeight * pOutRect->nWidth / pInRect->nWidth;
            /* only do cropping when difference of aspect ratio between input and output rect is big enough */
            if(pOutRect->nHeight - nHeight > 4){
                pCropRect->nTop = (pOutRect->nHeight - nHeight) / 2 + pOutRect->nTop;
                pCropRect->nLeft = pOutRect->nLeft;
                pCropRect->nWidth = pOutRect->nWidth;
                pCropRect->nHeight = nHeight;
            }
            else{
                fsl_osal_memcpy(pCropRect, pOutRect, sizeof(OMX_CONFIG_RECTTYPE));
            }
        }
        else {
            /* Crop display width */
            nWidth = pInRect->nWidth * pOutRect->nHeight / pInRect->nHeight;
            /* only do cropping when difference of aspect ratio between input and output rect is big enough */
            if(pOutRect->nWidth - nWidth > 4){
                pCropRect->nLeft = (pOutRect->nWidth - nWidth) / 2 + pOutRect->nLeft;
                pCropRect->nTop = pOutRect->nTop;
                pCropRect->nWidth = nWidth;
                pCropRect->nHeight = pOutRect->nHeight;
            }
            else{
                fsl_osal_memcpy(pCropRect, pOutRect, sizeof(OMX_CONFIG_RECTTYPE));
            }
        }
    }

    

    if(eScreenParam == SCREEN_ZOOM) {
        /* Need set input crop */
        if(pInRect->nWidth * pOutRect->nHeight >= pInRect->nHeight * pOutRect->nWidth) {
            /* Crop in width */
			OMX_S32 nScale = pOutRect->nHeight * 1000 / pInRect->nHeight;
			/*480/240 = 2*/
			OMX_S32 nAmplifiedWid = (pInRect->nWidth * nScale /1000 + 7)/8*8;
			//400* 2 * 1.333 = 1056
			/*1056*480 is desired when full screen without crop and keep ratio*/
			OMX_S32 nCropLeftOnAmplifiedRect = (nAmplifiedWid - pOutRect->nWidth)/2;//(1056-640)/2=208
			OMX_S32 nCropLeftOnInRect = (nCropLeftOnAmplifiedRect * 1000/ nScale + 7)/8*8;//208/2 = 104

            pCropRect->nLeft = nCropLeftOnInRect + pInRect->nLeft;
            pCropRect->nTop = pInRect->nTop;
            pCropRect->nWidth = pInRect->nWidth - 2*nCropLeftOnInRect;
            pCropRect->nHeight = pInRect->nHeight;

			LOG_DEBUG("%s,%d,original input rect width %d\n\
                    amplifying scale %d,\n\
                    width of amplified rect %d,\n\
                    crop_left_on amplified rect %d,\n\
                    crop_left on input rect width %d,\n\
                    width after crop the decoder output padded left %d\n",
                    __FUNCTION__,__LINE__,
                    pInRect->nWidth,
                    nScale,
                    nAmplifiedWid,
                    nCropLeftOnAmplifiedRect,
                    nCropLeftOnInRect,
                    pCropRect->nWidth);
        }
        else
        {
            OMX_S32 nScale = pOutRect->nWidth * 1000 / pInRect->nWidth;
			OMX_S32 nAmplifiedHeight = (pInRect->nHeight * nScale/1000 + 7)/8*8;
			//480*1.333 = 640,means desired rect is 640*640 when full screen and keep ratio without crop.
			OMX_S32 nCropTopOnAmplifiedRect = (nAmplifiedHeight - pOutRect->nHeight)/2;//(640-480)/2=80
			OMX_S32 nCropTopOnInRect = (nCropTopOnAmplifiedRect * 1000/ nScale  + 7)/8*8;//80 /1.333 = 60

            pCropRect->nLeft = pInRect->nLeft;
            pCropRect->nTop = pInRect->nTop + nCropTopOnInRect;
            pCropRect->nWidth = pInRect->nWidth;
            pCropRect->nHeight = pInRect->nHeight - 2*nCropTopOnInRect;

			LOG_DEBUG("%s,%d,original input rect height %d\n\
                    amplifying scale %d,\n\
                    height of amplified rect %d,\n\
                    crop_top_on amplified rect %d,\n\
                    crop_top on input rect height %d,\n\
                    height after including the crop of decoder padded top %d\n",
                    __FUNCTION__,__LINE__,
                    pInRect->nHeight,
                    nScale,
                    nAmplifiedHeight,
                    nCropTopOnAmplifiedRect,
                    nCropTopOnInRect,
                    pCropRect->nHeight);

        }
    }

    return ret;
}

OMX_ERRORTYPE GMPlayer::SetCrop(SCREENTYPE eScreenParam,OMX_CONFIG_RECTTYPE *pCropRect, OMX_CONFIG_OUTPUTMODE *pOutputMode)
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;
    OMX_U32 nWidth, nHeight;

    if(eScreenParam == SCREEN_STD) {
        fsl_osal_memcpy(&pOutputMode->sRectIn, &sRectIn, sizeof(OMX_CONFIG_RECTTYPE));
        if (!bRotate)
        {
            fsl_osal_memcpy(&pOutputMode->sRectOut, pCropRect, sizeof(OMX_CONFIG_RECTTYPE));
        }
        else
        {
            pOutputMode->sRectOut.nHeight   = pCropRect->nWidth;
            pOutputMode->sRectOut.nTop      = pCropRect->nLeft;
            pOutputMode->sRectOut.nWidth    = pCropRect->nHeight;
            pOutputMode->sRectOut.nLeft     = pCropRect->nTop;
        }
    }

    if(eScreenParam == SCREEN_FUL) {
        fsl_osal_memcpy(&pOutputMode->sRectIn, &sRectIn, sizeof(OMX_CONFIG_RECTTYPE));
        fsl_osal_memcpy(&pOutputMode->sRectOut, &sDispWindow, sizeof(OMX_CONFIG_RECTTYPE));

    }

    if(eScreenParam == SCREEN_ZOOM) {
        fsl_osal_memcpy(&pOutputMode->sRectIn, pCropRect, sizeof(OMX_CONFIG_RECTTYPE));
        fsl_osal_memcpy(&pOutputMode->sRectOut, &sDispWindow, sizeof(OMX_CONFIG_RECTTYPE));
    }
    DumpOutputMode(pOutputMode);
    return ret;
}

OMX_ERRORTYPE GMPlayer::ModifyOutputRectWhenRotate(OMX_CONFIG_OUTPUTMODE *pOutputMode)
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;

    switch (pOutputMode->eRotation)
    {
        case ROTATE_NONE:
        case ROTATE_VERT_FLIP:
        case ROTATE_HORIZ_FLIP:
        case ROTATE_180:
            bRotate = OMX_FALSE;
            break;
        case ROTATE_90_RIGHT:
        case ROTATE_90_RIGHT_VFLIP:
        case ROTATE_90_RIGHT_HFLIP:
        case ROTATE_90_LEFT:
            bRotate = OMX_TRUE;
            break;
        default :
            break;
    }

    if (pOutputMode->eTvMode == MODE_PAL)
    {
        sDispWindow.nTop = 0;
        sDispWindow.nWidth = 720;
        sDispWindow.nLeft = 0;
        sDispWindow.nHeight = 576;
    }
    else if (pOutputMode->eTvMode == MODE_NTSC)
    {
        sDispWindow.nTop = 0;
        sDispWindow.nWidth = 720;
        sDispWindow.nLeft = 0;
        sDispWindow.nHeight = 480;
    }
    else if (pOutputMode->eTvMode == MODE_720P)
    {
        sDispWindow.nTop = 0;
        sDispWindow.nWidth = 1280;
        sDispWindow.nLeft = 0;
        sDispWindow.nHeight = 720;
    }

    if (!pOutputMode->bTv)
    {
        if (bRotate)
        {
            sRectOut.nLeft = sDispWindow.nTop;
            sRectOut.nTop = sDispWindow.nLeft;
            sRectOut.nHeight = sDispWindow.nWidth;
            sRectOut.nWidth = sDispWindow.nHeight;
        }
        else
        {
            sRectOut.nTop = sDispWindow.nTop;
            sRectOut.nLeft = sDispWindow.nLeft;
            sRectOut.nWidth = sDispWindow.nWidth;
            sRectOut.nHeight = sDispWindow.nHeight;
        }
    }
    else if (pOutputMode->eTvMode == MODE_PAL)
    {
        sRectOut.nTop = 0;
        sRectOut.nWidth = 720;
        sRectOut.nLeft = 0;
        sRectOut.nHeight = 576;
    }
    else if (pOutputMode->eTvMode == MODE_NTSC)
    {
        sRectOut.nTop = 0;
        sRectOut.nWidth = 720;
        sRectOut.nLeft = 0;
        sRectOut.nHeight = 480;
    }
    else if (pOutputMode->eTvMode == MODE_720P)
    {
        sRectOut.nTop = 0;
        sRectOut.nWidth = 1280;
        sRectOut.nLeft = 0;
        sRectOut.nHeight = 720;
    }

    return ret;
}

OMX_BOOL GMPlayer::IsSameScreenMode(SCREENTYPE eScreenParam, OMX_BOOL bTvOut, OMX_U32 nTvMode, OMX_U32 nRotate)
{
    if (eScreenParam == eScreen &&
        sOutputMode.bTv  == bTvOut &&
        sOutputMode.eTvMode == (TV_MODE)nTvMode &&
        sOutputMode.eRotation == (ROTATION)nRotate)
        return OMX_TRUE;

    return OMX_FALSE;
}

static OMX_ERRORTYPE ResetRotateWhenTvOut(OMX_CONFIG_OUTPUTMODE *pOutputMode)
{
    if (pOutputMode->bTv)
    {
        pOutputMode->eRotation = ROTATE_NONE;
    }

    return OMX_ErrorNone;
}

OMX_BOOL GMPlayer::CheckIsScreenModeChangeFinished()
{
    OMX_CONFIG_OUTPUTMODE sOutputModeTmp;
    OMX_INIT_STRUCT(&sOutputModeTmp, OMX_CONFIG_OUTPUTMODE);
    //check if done
    OMX_U32 cnt = 0;
    for(;;) {
        if (OMX_GetConfig(VideoRender->hComponent, OMX_IndexOutputMode, &sOutputModeTmp)!=OMX_ErrorNone)
            return OMX_FALSE;
        if(!sOutputModeTmp.bSetupDone) {
            fsl_osal_sleep(10*1000);
            if((cnt++) > 500) {
                LOG_ERROR("timeout for waiting screen mode changed.\n");
                return OMX_FALSE;
            }
            continue;
        }
        break;
    }
    return OMX_TRUE;
}

        
OMX_ERRORTYPE GMPlayer::ApplyDispScaleFactor()
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;

    OMX_U32 nScaledDispWidth = sRectIn.nWidth * sDispRatio.xWidth / Q16_SHIFT;
    OMX_U32 nScaledDispHeight = sRectIn.nHeight * sDispRatio.xHeight / Q16_SHIFT;

    sScaledRectIn.nLeft = sRectIn.nLeft * sDispRatio.xWidth / Q16_SHIFT + (sRectIn.nWidth - nScaledDispWidth)/2;
    sScaledRectIn.nWidth = nScaledDispWidth;
    sScaledRectIn.nTop = sRectIn.nTop * sDispRatio.xHeight / Q16_SHIFT + (sRectIn.nHeight - nScaledDispHeight)/2;
    sScaledRectIn.nHeight = nScaledDispHeight;

    LOG_DEBUG("%s,%d width scale %d,height scale %d.\n",__FUNCTION__,__LINE__,sDispRatio.xWidth,sDispRatio.xHeight);
    LOG_DEBUG("sRectIn: %d,%d,%d,%d\n", sRectIn.nWidth, sRectIn.nHeight, sRectIn.nLeft, sRectIn.nTop);
    LOG_DEBUG("sScaledRectIn: %d,%d,%d,%d\n", sScaledRectIn.nWidth, sScaledRectIn.nHeight, sScaledRectIn.nLeft, sScaledRectIn.nTop);
    DumpRect(&sScaledRectIn);
    return ret;
}


OMX_ERRORTYPE GMPlayer::ScreenMode(SCREENTYPE eScreenParam, OMX_BOOL bTvOut, OMX_U32 nTvMode, OMX_U32 nRotate)
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;

    if (IsSameScreenMode(eScreenParam, bTvOut, nTvMode, nRotate))
        return ret;

    OMX_CONFIG_OUTPUTMODE sOutputModeTmp;
    OMX_INIT_STRUCT(&sOutputModeTmp, OMX_CONFIG_OUTPUTMODE);
    sOutputModeTmp.nPortIndex = 0;
    sOutputModeTmp.bTv = bTvOut;
    sOutputModeTmp.eFbLayer = LAYER2;
    sOutputModeTmp.eTvMode = (TV_MODE)nTvMode;
    sOutputModeTmp.eRotation = (ROTATION)nRotate;
    OMX_CONFIG_RECTTYPE sCropRect;
    OMX_INIT_STRUCT(&sCropRect, OMX_CONFIG_RECTTYPE);

    ResetRotateWhenTvOut(&sOutputModeTmp);

    ModifyOutputRectWhenRotate(&sOutputModeTmp);

    ApplyDispScaleFactor();

    CalcCrop(eScreenParam,&sScaledRectIn,&sRectOut,&sCropRect);

    SetCrop(eScreenParam,&sCropRect,&sOutputModeTmp);

    LOG_DEBUG("%s,%d,set rotate to %d\n",__FUNCTION__,__LINE__,sOutputModeTmp.eRotation);
    LOG_DEBUG("%s,%d,set screen mode to %d\n",__FUNCTION__,__LINE__,eScreenParam);
    ret = OMX_SetConfig(VideoRender->hComponent, OMX_IndexOutputMode, &sOutputModeTmp);
    if (ret != OMX_ErrorNone)
        LOG_ERROR("%s,%d,set output mode failed,ret %d\n",__FUNCTION__,__LINE__,ret);

    fsl_osal_memcpy(&sOutputMode, &sOutputModeTmp, sizeof(OMX_CONFIG_OUTPUTMODE));
    eScreen = eScreenParam;
    return ret;
}

OMX_BOOL GMPlayer::Rotate(OMX_S32 nRotate)
{
    OMX_BOOL ret = OMX_TRUE;

    printf("Rotate to %d\n", nRotate);

    if (VideoRender)
    {
        if (nRotate == (OMX_S32)sOutputMode.eRotation)
            return OMX_TRUE;
        if (nRotate > 7 || nRotate < 0)
            return OMX_FALSE;
		if (!CheckIsScreenModeChangeFinished())
			return OMX_FALSE;
        fsl_osal_mutex_lock(Lock);
        ScreenMode(eScreen, sOutputMode.bTv, (OMX_U32)sOutputMode.eTvMode, (OMX_U32)nRotate);
        ret = CheckIsScreenModeChangeFinished();
        fsl_osal_mutex_unlock(Lock);

    }
    return ret;
}

OMX_BOOL GMPlayer::FullScreen(OMX_BOOL bzoomin, OMX_BOOL bkeepratio)
{
    OMX_BOOL ret = OMX_TRUE;

	LOG_DEBUG("%s,%d,\n",__FUNCTION__,__LINE__);
	if (VideoRender)
	{
		if (!CheckIsScreenModeChangeFinished())
			return OMX_FALSE;
        fsl_osal_mutex_lock(Lock);
		ScreenMode(SCREEN_FUL, sOutputMode.bTv, (OMX_U32)sOutputMode.eTvMode, (OMX_U32)sOutputMode.eRotation);
        ret = CheckIsScreenModeChangeFinished();
        fsl_osal_mutex_unlock(Lock);
    }
	return ret;
}

OMX_BOOL GMPlayer::SetVideoMode(GM_VIDEO_MODE vmode)
{
    OMX_BOOL ret = OMX_TRUE;
    SCREENTYPE eScreenTemp;

    printf("Set Screen mode to %d.\n", vmode);

    switch (vmode){
        case GM_VIDEO_MODE_NORMAL:
            eScreenTemp = SCREEN_STD;
            break;
        case GM_VIDEO_MODE_FULLSCREEN:
            eScreenTemp = SCREEN_FUL;
            break;
        case GM_VIDEO_MODE_ZOOM:
            eScreenTemp = SCREEN_ZOOM;
            break;
        default:
            return OMX_FALSE;
            break;
    };
	if (VideoRender)
	{
		if (!CheckIsScreenModeChangeFinished())
			return OMX_FALSE;
        fsl_osal_mutex_lock(Lock);
		ScreenMode(eScreenTemp, sOutputMode.bTv, (OMX_U32)sOutputMode.eTvMode, (OMX_U32)sOutputMode.eRotation);
        ret = CheckIsScreenModeChangeFinished();
        fsl_osal_mutex_unlock(Lock);
    }

    return ret;
}

OMX_BOOL GMPlayer::SetVideoDevice(GM_VIDEO_DEVICE vdevice, OMX_BOOL bBlockMode)
{
    OMX_BOOL ret = OMX_TRUE;
    TV_MODE eTvMode;
    OMX_BOOL bTv = OMX_FALSE;
    OMX_CONFIG_RECTTYPE sRectDisp;

    switch (vdevice){
        case GM_VIDEO_DEVICE_LCD:
            eTvMode = MODE_NONE;
            break;
        case GM_VIDEO_DEVICE_TV_NTSC:
            eTvMode = MODE_NTSC;
            bTv = OMX_TRUE;
            break;
        case GM_VIDEO_DEVICE_TV_PAL:
            eTvMode = MODE_PAL;
            bTv = OMX_TRUE;
            break;
        case GM_VIDEO_DEVICE_TV_720P:
            eTvMode = MODE_720P;
            bTv = OMX_TRUE;
            break;
        default:
            return OMX_FALSE;
            break;
    };

    if (VideoRender)
    {
        if (!CheckIsScreenModeChangeFinished())
            return OMX_FALSE;
        fsl_osal_mutex_lock(Lock);
        fsl_osal_memcpy(&sRectDisp, &sDispWindow, sizeof(OMX_CONFIG_RECTTYPE));
        ScreenMode(eScreen, bTv, (OMX_U32)eTvMode, (OMX_U32)sOutputMode.eRotation);
        ret = CheckIsScreenModeChangeFinished();
        fsl_osal_memcpy(&sDispWindow, &sRectDisp, sizeof(OMX_CONFIG_RECTTYPE));
        fsl_osal_mutex_unlock(Lock);
    }

    return ret;
}



OMX_ERRORTYPE GMPlayer::SetDefaultScreenMode()
{
    OMX_ERRORTYPE eRetVal = OMX_ErrorNone;

    if (VideoRender)
    {
        OMX_INIT_STRUCT(&sRectIn, OMX_CONFIG_RECTTYPE);
        eRetVal = OMX_GetConfig(VideoRender->hComponent,OMX_IndexConfigCommonInputCrop,&sRectIn);
        if(eRetVal != OMX_ErrorNone)
        {
            LOG_ERROR("%s,%d,Get input crop failed\n",__FUNCTION__,__LINE__);
            return eRetVal;
        }

        if(sVideoRect.nWidth > 0 && sVideoRect.nHeight > 0) {
            LOG_DEBUG("User set video crop from: %d,%d,%d,%d",
                    sRectIn.nLeft, sRectIn.nTop, sRectIn.nWidth, sRectIn.nHeight);

            OMX_CONFIG_RECTTYPE sTargetRect;
            sTargetRect.nLeft = MAX(sRectIn.nLeft, sVideoRect.nLeft);
            sTargetRect.nTop = MAX(sRectIn.nTop, sVideoRect.nTop);
            sTargetRect.nWidth = MIN(sRectIn.nWidth, sVideoRect.nWidth);
            sTargetRect.nHeight = MIN(sRectIn.nHeight, sVideoRect.nHeight);

            OMX_PARAM_PORTDEFINITIONTYPE sPortDef;
            OMX_U32 nWidth, nHeight;
            OMX_INIT_STRUCT(&sPortDef, OMX_PARAM_PORTDEFINITIONTYPE);
            sPortDef.nPortIndex = 1;
            OMX_GetParameter(VideoDecoder->hComponent, OMX_IndexParamPortDefinition, &sPortDef);
            nWidth = sPortDef.format.video.nFrameWidth;
            nHeight = sPortDef.format.video.nFrameHeight;

            if(sTargetRect.nLeft <= ((OMX_S32)nWidth - 128) || sTargetRect.nHeight <= (nWidth - 128)) {
                sRectIn.nLeft = sTargetRect.nLeft;
                sRectIn.nTop = sTargetRect.nTop;
                sRectIn.nWidth = sTargetRect.nWidth;
                sRectIn.nHeight = sTargetRect.nHeight;
            }

            LOG_DEBUG("to  %d,%d,%d,%d\n",
                    sRectIn.nLeft, sRectIn.nTop, sRectIn.nWidth, sRectIn.nHeight);
        }
        
        OMX_INIT_STRUCT(&sDispRatio, OMX_CONFIG_SCALEFACTORTYPE );
        sDispRatio.nPortIndex = 1;
        eRetVal = OMX_GetConfig(VideoDecoder->hComponent,OMX_IndexConfigCommonScale,&sDispRatio);
        if(eRetVal != OMX_ErrorNone)
        {
            LOG_WARNING("%s,%d,Get output scale failed\n",__FUNCTION__,__LINE__);
            sDispRatio.xWidth = Q16_SHIFT;
            sDispRatio.xHeight = Q16_SHIFT;
        }

        LOG_DEBUG("%s,%d width scale %d,height scale %d.\n",__FUNCTION__,__LINE__,sDispRatio.xWidth,sDispRatio.xHeight);

        if(mVideoRenderType == OMX_VIDEO_RENDER_V4L || mVideoRenderType == OMX_VIDEO_RENDER_IPULIB)
            ScreenMode(SCREEN_STD, OMX_FALSE, MODE_NONE, ROTATE_NONE);

        /*get setting result*/
        eRetVal = OMX_GetConfig(VideoRender->hComponent,OMX_IndexConfigCommonInputCrop,&sRectIn);
        if(eRetVal != OMX_ErrorNone)
        {
            LOG_ERROR("%s,%d,Get input crop failed\n",__FUNCTION__,__LINE__);
            return eRetVal;
        }
    }
    return eRetVal;
}


OMX_ERRORTYPE GMPlayer::ConnectComponent(
        GMComponent *pOutComp,
        OMX_U32 nOutPortIndex,
        GMComponent *pInComp,
        OMX_U32 nInPortIndex)
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;

    ret = pOutComp->Link(nOutPortIndex,pInComp,nInPortIndex);
    if(ret != OMX_ErrorNone) {
        LOG_ERROR("Connect component[%s:%d] with component[%s:%d] failed.\n",
                pOutComp->name, nOutPortIndex, pInComp->name, nInPortIndex);
        return ret;
    }

    ret = pInComp->Link(nInPortIndex, pOutComp, nOutPortIndex);
    if(ret != OMX_ErrorNone) {
        LOG_ERROR("Connect component[%s:%d] with component[%s:%d] failed.\n",
                pInComp->name, nInPortIndex, pOutComp->name, nOutPortIndex);
        pOutComp->Link(nOutPortIndex, NULL, 0);
        return ret;
    }

    LOG_DEBUG("Connect component[%s:%d] with component[%s:%d] success.\n",
            pOutComp->name, nOutPortIndex, pInComp->name, nInPortIndex);

    return OMX_ErrorNone;
}

OMX_ERRORTYPE GMPlayer::DisConnectComponent(
        GMComponent *pOutComp,
        OMX_U32 nOutPortIndex,
        GMComponent *pInComp,
        OMX_U32 nInPortIndex)
{
    pOutComp->Link(nOutPortIndex, NULL, 0);
    pInComp->Link(nInPortIndex, NULL, 0);

    return OMX_ErrorNone;
}

OMX_ERRORTYPE GMPlayer::GetStreamDuration(
        OMX_TICKS *pDur,
        OMX_U32 nPortIndex)
{
    OMX_TIME_CONFIG_TIMESTAMPTYPE sDur;

    OMX_INIT_STRUCT(&sDur, OMX_TIME_CONFIG_TIMESTAMPTYPE);
    sDur.nPortIndex = nPortIndex;
    OMX_GetParameter(Parser->hComponent, OMX_IndexParamMediaDuration, &sDur);
    *pDur = sDur.nTimestamp;

    return OMX_ErrorNone;
}

OMX_ERRORTYPE GMPlayer::DoSeek(
        OMX_TIME_SEEKMODETYPE mode,
        OMX_TICKS position)
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;
    OMX_TIME_CONFIG_SEEKMODETYPE seek_mode;
    OMX_TIME_CONFIG_TIMESTAMPTYPE ts;
    OMX_TIME_CONFIG_CLOCKSTATETYPE ClockState;
    OMX_TIME_CONFIG_ACTIVEREFCLOCKTYPE RefClock;
    GMComponent *pComponent = NULL;
    OMX_TICKS Dur = 0;
    OMX_U32 ComponentNum, i;
    OMX_MARKTYPE sMarkData;

    if(Parser == NULL)
        return OMX_ErrorInvalidComponent;

    //Get ref clock, not support seek in trick mode.
    OMX_INIT_STRUCT(&RefClock, OMX_TIME_CONFIG_ACTIVEREFCLOCKTYPE);
    OMX_GetConfig(Clock->hComponent, OMX_IndexConfigTimeActiveRefClock, &RefClock);
    if(bHasAudio == OMX_TRUE && RefClock.eClock == OMX_TIME_RefClockVideo)
        return OMX_ErrorNone;

    LOG_DEBUG("\nSeek player want to seek to %lld, mode: %d,\n ", position, mode);

    //pause parser
    Parser->StateTransDownWard(OMX_StatePause);

    if(bHasSubtitle == OMX_TRUE)
        pSubtitlePlayer->Reset();

    //seek parser to position
    OMX_INIT_STRUCT(&seek_mode, OMX_TIME_CONFIG_SEEKMODETYPE);
    seek_mode.eType = mode;
    ret = OMX_SetConfig(Parser->hComponent, OMX_IndexConfigTimeSeekMode, &seek_mode);
    if (ret != OMX_ErrorNone)
        return ret;

    if(bHasVideo == OMX_TRUE)
        GetStreamDuration(&Dur, Parser->PortPara[VIDEO_PORT_PARA].nStartPortNumber);
    else
        GetStreamDuration(&Dur, Parser->PortPara[AUDIO_PORT_PARA].nStartPortNumber);

    if(position > (Dur - OMX_TICKS_PER_SECOND))
        position = Dur - OMX_TICKS_PER_SECOND;
    if(position < 0)
        position = 0;

    LOG_DEBUG("Seek to %lld\n", position);

    OMX_INIT_STRUCT(&ts, OMX_TIME_CONFIG_TIMESTAMPTYPE);
    ts.nTimestamp = position;
    ts.nPortIndex = OMX_ALL;
    ret = OMX_SetConfig(Parser->hComponent, OMX_IndexConfigTimePosition, &ts);
    if (ret != OMX_ErrorNone) {
        Parser->StateTransUpWard(OMX_StateExecuting);
        return ret;
    }

    //stop clock
    OMX_INIT_STRUCT(&ClockState, OMX_TIME_CONFIG_CLOCKSTATETYPE);
    OMX_GetConfig(Clock->hComponent, OMX_IndexConfigTimeClockState, &ClockState);
    ClockState.eState = OMX_TIME_ClockStateStopped;
    OMX_SetConfig(Clock->hComponent, OMX_IndexConfigTimeClockState, &ClockState);

    //flush all data for input ports of all components
    ComponentNum = Pipeline->GetNodeCnt();
    for(i=1; i<ComponentNum; i++) {
        pComponent = Pipeline->GetNode(i);
        pComponent->PortFlush(0);
    }

    //start clock with WaitingForStartTime
    ClockState.eState = OMX_TIME_ClockStateWaitingForStartTime;
    ClockState.nWaitMask = 0;
    if(bHasAudio == OMX_TRUE)
        ClockState.nWaitMask |= 1 << AUDIO_CLOCK_PORT;
    if(bHasVideo == OMX_TRUE)
        ClockState.nWaitMask |= 1 << VIDEO_CLOCK_PORT;
    OMX_SetConfig(Clock->hComponent, OMX_IndexConfigTimeClockState, &ClockState);

    //mark the first buffer of parser
    bAudioMarkDone = bVideoMarkDone = OMX_TRUE;
    if(bHasAudio == OMX_TRUE) {
        sMarkData.hMarkTargetComponent = AudioRender->hComponent;
        bAudioMarkDone = OMX_FALSE;
        Parser->PortMarkBuffer(Parser->PortPara[AUDIO_PORT_PARA].nStartPortNumber, &sMarkData);
    }

    if(bHasVideo == OMX_TRUE) {
        sMarkData.hMarkTargetComponent = VideoRender->hComponent;
        bVideoMarkDone = OMX_FALSE;
        Parser->PortMarkBuffer(Parser->PortPara[VIDEO_PORT_PARA].nStartPortNumber, &sMarkData);
    }

    bAudioEos = bVideoEos = OMX_FALSE;
    bVideoFFEos = OMX_FALSE;
    bMediaEos = OMX_FALSE;

    //resume the parser
    Parser->StateTransUpWard(OMX_StateExecuting);

    //wait the marked buffer event
    WAIT_MARKBUFFER(5*OMX_TICKS_PER_SECOND);
    if(ret == OMX_ErrorTimeout) {
        if(mode == OMX_TIME_SeekModeFast)
            LOG_ERROR("time out when waiting mark buffer in fast seek.\n");
        if(mode == OMX_TIME_SeekModeAccurate)
            LOG_WARNING("time out when waiting mark buffer in accurate seek.\n");
    }

    if(bHasSubtitle == OMX_TRUE) {
        sSUBTITLETRACKINFO *pSubTrackInfo = SubtitleTrackInfo->GetNode(nCurSubtitleTrack);
        if(pSubTrackInfo->bInBand != OMX_TRUE) {
            GMOutbandSubtitleSource *pOutbandSource = 
                (GMOutbandSubtitleSource*)OutbandSubtitleSource->GetNode(pSubTrackInfo->nTrackNum);

            OMX_TIME_CONFIG_TIMESTAMPTYPE sCur;
            OMX_INIT_STRUCT(&sCur, OMX_TIME_CONFIG_TIMESTAMPTYPE);
            sCur.nPortIndex = OMX_ALL;
            OMX_GetConfig(Clock->hComponent, OMX_IndexConfigTimeCurrentMediaTime, &sCur);

            pOutbandSource->SeekTo(sCur.nTimestamp);
        }
        pSubtitlePlayer->Start();
    }

    return ret;
}


OMX_ERRORTYPE GMPlayer::Normal2Trick(
        OMX_S32 Scale)
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;
    OMX_TIME_CONFIG_SCALETYPE sScale;
    OMX_TIME_CONFIG_ACTIVEREFCLOCKTYPE ActiveClock;
    OMX_MARKTYPE sMarkData;
    OMX_PARAM_CAPABILITY sMediaSeekable;
    GMComponent *pComponent = NULL;
    OMX_U32 ComponentNum, i;
    OMX_TICKS position;
    OMX_TIME_CONFIG_SEEKMODETYPE seek_mode;
    OMX_TIME_CONFIG_TIMESTAMPTYPE ts;
    OMX_TICKS Dur = 0;

    OMX_INIT_STRUCT(&sMediaSeekable, OMX_PARAM_CAPABILITY);
    sMediaSeekable.nPortIndex = OMX_ALL;
    OMX_GetParameter(Parser->hComponent, OMX_IndexParamMediaSeekable, &sMediaSeekable);
    if(sMediaSeekable.bCapability != OMX_TRUE) {
        LOG_WARNING("Meida file is not seekable.\n");
        return OMX_ErrorNone;
    }

    Parser->StateTransDownWard(OMX_StatePause);

    OMX_TIME_CONFIG_TIMESTAMPTYPE sCur;
    OMX_INIT_STRUCT(&sCur, OMX_TIME_CONFIG_TIMESTAMPTYPE);
    sCur.nPortIndex = OMX_ALL;
    OMX_GetConfig(Clock->hComponent, OMX_IndexConfigTimeCurrentMediaTime, &sCur);
    position = sCur.nTimestamp;

    GetStreamDuration(&Dur, Parser->PortPara[VIDEO_PORT_PARA].nStartPortNumber);
    if(position > (Dur - OMX_TICKS_PER_SECOND))
        position = Dur - OMX_TICKS_PER_SECOND;
    if(position < 0)
        position = 0;

    printf("Dur is %lld, current position is %lld\n", Dur, position);

    //seek parser to position
    OMX_INIT_STRUCT(&seek_mode, OMX_TIME_CONFIG_SEEKMODETYPE);
    seek_mode.eType = OMX_TIME_SeekModeFast;
    ret = OMX_SetConfig(Parser->hComponent, OMX_IndexConfigTimeSeekMode, &seek_mode);
    if (ret != OMX_ErrorNone)
        return ret;

    OMX_INIT_STRUCT(&ts, OMX_TIME_CONFIG_TIMESTAMPTYPE);
    ts.nTimestamp = position;
    ts.nPortIndex = OMX_ALL;
    ret = OMX_SetConfig(Parser->hComponent, OMX_IndexConfigTimePosition, &ts);
    if (ret != OMX_ErrorNone) {
        return ret;
    }
    
    //flush all data for input ports of all components
    ComponentNum = Pipeline->GetNodeCnt();
    for(i=1; i<ComponentNum; i++) {
        pComponent = Pipeline->GetNode(i);
        pComponent->PortFlush(0);
    }

    //Set scale
    OMX_INIT_STRUCT(&sScale, OMX_TIME_CONFIG_SCALETYPE);
    sScale.xScale = Scale;
    OMX_SetConfig(Clock->hComponent, OMX_IndexConfigTimeScale, &sScale);

    //flush v4l render for issue of can not change between trick modes
    //after previous flush, there may be a buffer storing in clock arrives render and not flushed,
    //so call flush again
    ComponentNum = Pipeline->GetNodeCnt();
    for(i=1; i<ComponentNum; i++) {
        pComponent = Pipeline->GetNode(i);
        if(fsl_osal_strncmp((char *)pComponent->name, "OMX.Freescale.std.video_render", fsl_osal_strlen("OMX.Freescale.std.video_render")) == 0){
            pComponent->PortFlush(0);
            break;
        }
    }


    //Set ref clock
    OMX_INIT_STRUCT(&ActiveClock, OMX_TIME_CONFIG_ACTIVEREFCLOCKTYPE);
    ActiveClock.eClock = OMX_TIME_RefClockVideo;
    OMX_SetConfig(Clock->hComponent, OMX_IndexConfigTimeActiveRefClock, &ActiveClock);


    //mark the first buffer of parser
    sMarkData.hMarkTargetComponent = VideoRender->hComponent;
    bAudioMarkDone = OMX_TRUE;
    bVideoMarkDone = OMX_FALSE;
    Parser->PortMarkBuffer(Parser->PortPara[VIDEO_PORT_PARA].nStartPortNumber, &sMarkData);

    //resume the parser
    Parser->StateTransUpWard(OMX_StateExecuting);

    //wait the marked buffer event
    WAIT_MARKBUFFER(5*OMX_TICKS_PER_SECOND);
    if(ret == OMX_ErrorTimeout)
        LOG_ERROR("time out when waiting mark buffer in trick mode.\n");

    if(State == GM_STATE_PAUSE) {
        VideoRender->StateTransUpWard(OMX_StateExecuting);
        Clock->StateTransUpWard(OMX_StateExecuting);
    }

    return ret;
}

OMX_ERRORTYPE GMPlayer::Trick2Normal(
        OMX_S32 Scale)
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;
    OMX_TIME_CONFIG_SCALETYPE sScale;
    OMX_TIME_CONFIG_CLOCKSTATETYPE ClockState;
    OMX_TIME_CONFIG_ACTIVEREFCLOCKTYPE ActiveClock;
    OMX_MARKTYPE sMarkData;
    GMComponent *pComponent = NULL;
    OMX_U32 ComponentNum, i;
    OMX_TICKS position;
    OMX_TIME_CONFIG_SEEKMODETYPE seek_mode;
    OMX_TIME_CONFIG_TIMESTAMPTYPE ts;
    OMX_TICKS Dur = 0;

    Parser->StateTransDownWard(OMX_StatePause);

    OMX_TIME_CONFIG_TIMESTAMPTYPE sCur;
    OMX_INIT_STRUCT(&sCur, OMX_TIME_CONFIG_TIMESTAMPTYPE);
    sCur.nPortIndex = OMX_ALL;
    OMX_GetConfig(Clock->hComponent, OMX_IndexConfigTimeCurrentMediaTime, &sCur);
    position = sCur.nTimestamp;

    GetStreamDuration(&Dur, Parser->PortPara[VIDEO_PORT_PARA].nStartPortNumber);
    if(position > Dur)
        position = Dur;
    if(position < 0)
        position = 0;

    printf("Dur is %lld, current position is %lld\n", Dur, position);

    //seek parser to position
    OMX_INIT_STRUCT(&seek_mode, OMX_TIME_CONFIG_SEEKMODETYPE);
    seek_mode.eType = OMX_TIME_SeekModeFast;
    ret = OMX_SetConfig(Parser->hComponent, OMX_IndexConfigTimeSeekMode, &seek_mode);
    if (ret != OMX_ErrorNone)
        return ret;

    OMX_INIT_STRUCT(&ts, OMX_TIME_CONFIG_TIMESTAMPTYPE);
    ts.nTimestamp = position;
    ts.nPortIndex = OMX_ALL;
    ret = OMX_SetConfig(Parser->hComponent, OMX_IndexConfigTimePosition, &ts);
    if (ret != OMX_ErrorNone) {
        return ret;
    }
    
    //stop clock
    OMX_INIT_STRUCT(&ClockState, OMX_TIME_CONFIG_CLOCKSTATETYPE);
    OMX_GetConfig(Clock->hComponent, OMX_IndexConfigTimeClockState, &ClockState);
    ClockState.eState = OMX_TIME_ClockStateStopped;
    OMX_SetConfig(Clock->hComponent, OMX_IndexConfigTimeClockState, &ClockState);

    //flush all data for input ports of all components
    ComponentNum = Pipeline->GetNodeCnt();
    for(i=1; i<ComponentNum; i++) {
        pComponent = Pipeline->GetNode(i);
        pComponent->PortFlush(0);
    }

    //start clock with WaitingForStartTime
    ClockState.eState = OMX_TIME_ClockStateWaitingForStartTime;
    ClockState.nWaitMask = 0;
    if(bHasAudio == OMX_TRUE)
        ClockState.nWaitMask |= 1 << AUDIO_CLOCK_PORT;
    if(bHasVideo == OMX_TRUE)
        ClockState.nWaitMask |= 1 << VIDEO_CLOCK_PORT;
    OMX_SetConfig(Clock->hComponent, OMX_IndexConfigTimeClockState, &ClockState);

    //Set scale
    OMX_INIT_STRUCT(&sScale, OMX_TIME_CONFIG_SCALETYPE);
    sScale.xScale = Scale;
    OMX_SetConfig(Clock->hComponent, OMX_IndexConfigTimeScale, &sScale);

    //Set ref clock
    OMX_INIT_STRUCT(&ActiveClock, OMX_TIME_CONFIG_ACTIVEREFCLOCKTYPE);
    if(bHasAudio == OMX_TRUE)
        ActiveClock.eClock = OMX_TIME_RefClockAudio;
    else
        ActiveClock.eClock = OMX_TIME_RefClockVideo;
    OMX_SetConfig(Clock->hComponent, OMX_IndexConfigTimeActiveRefClock, &ActiveClock);

    //mark the first buffer of parser
    bAudioMarkDone = bVideoMarkDone = OMX_TRUE;
    if(bHasAudio == OMX_TRUE) {
        sMarkData.hMarkTargetComponent = AudioRender->hComponent;
        bAudioMarkDone = OMX_FALSE;
        Parser->PortMarkBuffer(Parser->PortPara[AUDIO_PORT_PARA].nStartPortNumber, &sMarkData);
    }
    if(bHasVideo == OMX_TRUE) {
        sMarkData.hMarkTargetComponent = VideoRender->hComponent;
        bVideoMarkDone = OMX_FALSE;
        Parser->PortMarkBuffer(Parser->PortPara[VIDEO_PORT_PARA].nStartPortNumber, &sMarkData);
    }

    //resume the parser
    Parser->StateTransUpWard(OMX_StateExecuting);

    //wait the marked buffer event
    WAIT_MARKBUFFER(5*OMX_TICKS_PER_SECOND);
    if(ret == OMX_ErrorTimeout)
        LOG_ERROR("time out when waiting mark buffer in trick mode.\n");

    if(State == GM_STATE_PAUSE) {
        Clock->StateTransDownWard(OMX_StatePause);
        VideoRender->StateTransDownWard(OMX_StatePause);
    }

    return ret;
}

OMX_ERRORTYPE GMPlayer::SpeedControl(
        OMX_S32 Scale)
{
    OMX_TIME_CONFIG_SCALETYPE sScale;

    OMX_INIT_STRUCT(&sScale, OMX_TIME_CONFIG_SCALETYPE);
    sScale.xScale = Scale;
    OMX_SetConfig(Clock->hComponent, OMX_IndexConfigTimeScale, &sScale);

    return OMX_ErrorNone;
}


OMX_BOOL GMPlayer::GetScreenShotInfo(OMX_IMAGE_PORTDEFINITIONTYPE *pfmt, OMX_CONFIG_RECTTYPE *pCropRect)
{
    OMX_PARAM_PORTDEFINITIONTYPE sPortDef;

    if(pfmt == NULL || pCropRect == NULL)
        return OMX_FALSE;

	if (!VideoRender)
        return OMX_FALSE;

    OMX_INIT_STRUCT(&sPortDef, OMX_PARAM_PORTDEFINITIONTYPE);
    sPortDef.nPortIndex = 0;
    if(OMX_GetParameter(VideoRender->hComponent, OMX_IndexParamPortDefinition, &sPortDef)!=OMX_ErrorNone)
        return OMX_FALSE;

    pfmt->nFrameWidth = sPortDef.format.video.nFrameWidth;
    pfmt->nFrameHeight = sPortDef.format.video.nFrameHeight; 
    pfmt->eColorFormat = sPortDef.format.video.eColorFormat;

    OMX_U32 nPortIndex = pCropRect->nPortIndex;
    OMX_INIT_STRUCT(pCropRect, OMX_CONFIG_RECTTYPE);
    if(OMX_GetConfig(VideoRender->hComponent, OMX_IndexConfigCommonInputCrop, pCropRect)!=OMX_ErrorNone)
        return OMX_FALSE;

    return OMX_TRUE;
}

OMX_BOOL GMPlayer::GetThumbNail(OMX_TICKS pos, OMX_U8 *pBuf)
{
    OMX_CONFIG_CAPTUREFRAME sCapture;
    OMX_TICKS dur;
    OMX_BOOL ret = OMX_TRUE;

    if(pBuf == NULL)
        return OMX_FALSE;

    if (State != GM_STATE_LOADED)
        return OMX_FALSE;
	if (!VideoRender)
        return OMX_FALSE;

    OMX_INIT_STRUCT(&sCapture, OMX_CONFIG_CAPTUREFRAME);
    sCapture.nPortIndex = 0;
    sCapture.eType = CAP_THUMBNAL;
    sCapture.pBuffer = pBuf;
    sCapture.bDone = OMX_FALSE;
    if (OMX_SetConfig(VideoRender->hComponent, OMX_IndexConfigCaptureFrame, &sCapture)!=OMX_ErrorNone)
        return OMX_FALSE;

	GetStreamDuration(&dur, Parser->PortPara[VIDEO_PORT_PARA].nStartPortNumber);
    if(pos > dur)
        pos = dur;

    //seek to desired position
	// Seek to 0 for flv to avoid thumbnail masoic as most flv haven't seek table.
	if (parserRole && !fsl_osal_strcmp(parserRole, "parser.flv"))
		pos = 0;

	if(OMX_ErrorNone != Seek(OMX_TIME_SeekModeFast, pos))
		return OMX_FALSE;

    LOG_DEBUG("seek done.\n");

    //check if done
    OMX_U32 cnt = 0;
    for(;;) {
        if (OMX_GetConfig(VideoRender->hComponent, OMX_IndexConfigCaptureFrame, &sCapture)!=OMX_ErrorNone)
            return OMX_FALSE;
        if(!sCapture.bDone) {
            fsl_osal_sleep(10*1000);
            if((cnt++) > 500) {
                LOG_ERROR("timeout for waiting thumbnail.\n");
                ret = OMX_FALSE;
                break;
            }
            continue;
        }
        break;
    }

    return ret;
}

OMX_BOOL GMPlayer::GetSnapShot(OMX_U8 *buf)
{
    OMX_CONFIG_CAPTUREFRAME sCapture;
    OMX_U32 cnt=0;

    fsl_osal_mutex_lock(Lock);

    if (!VideoRender)
        goto errCatch;

    if ((State == GM_STATE_NULL
                || State == GM_STATE_LOADED
                || State == GM_STATE_STOP))
        goto errCatch;

    OMX_INIT_STRUCT(&sCapture, OMX_CONFIG_CAPTUREFRAME);
    sCapture.nPortIndex = 0;
    sCapture.eType = CAP_SNAPSHOT;
    sCapture.pBuffer = buf;
    sCapture.bDone = OMX_FALSE;
    if (OMX_SetConfig(VideoRender->hComponent, OMX_IndexConfigCaptureFrame, &sCapture)!=OMX_ErrorNone)
        goto errCatch;

    //check if done
    for(;;) {
        if (OMX_GetConfig(VideoRender->hComponent, OMX_IndexConfigCaptureFrame, &sCapture)!=OMX_ErrorNone)
            goto errCatch;

        if(!sCapture.bDone) {
            fsl_osal_sleep(50*1000);
            if((cnt++) > 500) {
                LOG_ERROR("timeout for waiting snapshot.\n");
                goto errCatch;
            }
            continue;
        }
        break;
    }

    fsl_osal_mutex_unlock(Lock);
    return OMX_TRUE;

errCatch:
    fsl_osal_mutex_unlock(Lock);
    return OMX_FALSE;
}

OMX_BOOL GMPlayer::SysSleep(OMX_BOOL bSleep)
{
    OMX_CONFIG_SYSSLEEP sConfSysSleep;
    OMX_STATETYPE eState = OMX_StateInvalid;

    if (!VideoRender)
        return OMX_FALSE;

    OMX_INIT_STRUCT(&sConfSysSleep,OMX_CONFIG_SYSSLEEP);
    sConfSysSleep.nPortIndex = 0;

    if (bSleep != OMX_TRUE)
    {
        /*wake up*/
        sConfSysSleep.bSleep = OMX_FALSE;
        if(OMX_SetConfig(VideoRender->hComponent,OMX_IndexSysSleep,&sConfSysSleep)!=OMX_ErrorNone)
        {
            LOG_DEBUG("SetConfig system sleep Failed \n");
            return OMX_FALSE;
        }
        if(State == GM_STATE_PLAYING && bBuffering != OMX_TRUE) {
            if(Clock != NULL)
                Clock->StateTransUpWard(OMX_StateExecuting);
            if(AudioRender != NULL)
                AudioRender->StateTransUpWard(OMX_StateExecuting);
            if(VideoRender != NULL)
                VideoRender->StateTransUpWard(OMX_StateExecuting);
        }
        bSuspend = OMX_FALSE;
    }
    else
    {
        /*sleep*/
        bSuspend = OMX_TRUE;
        if(State == GM_STATE_PLAYING) {
            if(AudioRender != NULL)
                AudioRender->StateTransDownWard(OMX_StatePause);
            if(VideoRender != NULL)
                VideoRender->StateTransDownWard(OMX_StatePause);
            if(Clock != NULL)
                Clock->StateTransDownWard(OMX_StatePause);
        }
        sConfSysSleep.bSleep = OMX_TRUE;
        if(OMX_SetConfig(VideoRender->hComponent,OMX_IndexSysSleep,&sConfSysSleep)!=OMX_ErrorNone)
        {
            LOG_DEBUG("SetConfig system sleep Failed \n");
            return OMX_FALSE;
        }
    }

    return OMX_TRUE;
}

OMX_ERRORTYPE GMPlayer::SetAudioSink(OMX_PTR sink)
{
    if(sink == NULL)
	{
		bUseFakeAudioSink = OMX_TRUE;
        LOG_ERROR("SetAudioSink with NULL. Use fake audio sink.\n");
	}

    mAudioSink = sink;
    return OMX_ErrorNone;
}

OMX_ERRORTYPE GMPlayer::SetDisplayRect(
        OMX_CONFIG_RECTTYPE *pRect)
{
    OMX_CONFIG_OUTPUTMODE sOutputModeTmp;
    OMX_CONFIG_RECTTYPE sCropRect;
    OMX_ERRORTYPE ret;

    fsl_osal_mutex_lock(Lock);

    OMX_INIT_STRUCT(&sCropRect, OMX_CONFIG_RECTTYPE);

    OMX_S32 left_diff = ABS(sOutputMode.sRectOut.nLeft - pRect->nLeft);
    OMX_S32 top_diff = ABS(sOutputMode.sRectOut.nTop - pRect->nTop);
    OMX_S32 width_diff = ABS((OMX_S32)sOutputMode.sRectOut.nWidth - (OMX_S32)pRect->nWidth);
    OMX_S32 height_diff = ABS((OMX_S32)sOutputMode.sRectOut.nHeight - (OMX_S32)pRect->nHeight);

    if(left_diff < 8 && top_diff < 8 && width_diff < 8 && height_diff < 8) {
        LOG_DEBUG("GMPlayer set same display rectagle.\n");
        fsl_osal_mutex_unlock(Lock);
        return OMX_ErrorNone;
    }

    sDispWindow.nLeft = pRect->nLeft;
    sDispWindow.nTop = pRect->nTop;
    sDispWindow.nWidth = pRect->nWidth;
    sDispWindow.nHeight = pRect->nHeight;
    fsl_osal_memcpy(&sRectOut, &sDispWindow, sizeof(OMX_CONFIG_RECTTYPE));

    if (bRotate)
    {
        sRectOut.nLeft = sDispWindow.nTop;
        sRectOut.nTop = sDispWindow.nLeft;
        sRectOut.nHeight = sDispWindow.nWidth;
        sRectOut.nWidth = sDispWindow.nHeight;
    }

    if(State == GM_STATE_LOADING || State == GM_STATE_STOP) {
        fsl_osal_mutex_unlock(Lock);
        return OMX_ErrorNone;
    }

    if(VideoRender == NULL) {
        fsl_osal_mutex_unlock(Lock);
        return OMX_ErrorInvalidComponent;
    }

    fsl_osal_memcpy(&sOutputModeTmp, &sOutputMode, sizeof(OMX_CONFIG_OUTPUTMODE));
    ApplyDispScaleFactor();
    CalcCrop(eScreen,&sScaledRectIn,&sRectOut,&sCropRect);
    SetCrop(eScreen,&sCropRect,&sOutputModeTmp);
    ret = OMX_SetConfig(VideoRender->hComponent, OMX_IndexOutputMode, &sOutputModeTmp);
    if(ret != OMX_ErrorNone)
        LOG_ERROR("%s,%d,set output mode failed, ret %x\n",__FUNCTION__,__LINE__, ret);

    fsl_osal_memcpy(&sOutputMode, &sOutputModeTmp, sizeof(OMX_CONFIG_OUTPUTMODE));

    fsl_osal_mutex_unlock(Lock);

    return OMX_ErrorNone;
}

OMX_ERRORTYPE GMPlayer::SetVideoRect(
        OMX_CONFIG_RECTTYPE *pRect)
{
    OMX_CONFIG_OUTPUTMODE sOutputModeTmp;
    OMX_CONFIG_RECTTYPE sCropRect;

    fsl_osal_mutex_lock(Lock);

    OMX_INIT_STRUCT(&sCropRect, OMX_CONFIG_RECTTYPE);

    if((OMX_S32)pRect->nLeft < 0 || (OMX_S32)pRect->nTop < 0
            || (OMX_S32)pRect->nWidth < 0 || (OMX_S32)pRect->nHeight < 0)
        return OMX_ErrorBadParameter;

    if(sVideoRect.nLeft == pRect->nLeft && sVideoRect.nTop == pRect->nTop
            && sVideoRect.nWidth == pRect->nWidth && sVideoRect.nHeight == pRect->nHeight)
        return OMX_ErrorNone;

    sVideoRect.nLeft = pRect->nLeft;
    sVideoRect.nTop = pRect->nTop;
    sVideoRect.nWidth = pRect->nWidth;
    sVideoRect.nHeight = pRect->nHeight;

    if(State == GM_STATE_LOADING || State == GM_STATE_STOP) {
        fsl_osal_mutex_unlock(Lock);
        return OMX_ErrorNone;
    }

    if(VideoDecoder == NULL || VideoRender == NULL) {
        fsl_osal_mutex_unlock(Lock);
        return OMX_ErrorInvalidComponent;
    }

    OMX_PARAM_PORTDEFINITIONTYPE sPortDef;
    OMX_U32 nWidth, nHeight;
    OMX_INIT_STRUCT(&sPortDef, OMX_PARAM_PORTDEFINITIONTYPE);
    sPortDef.nPortIndex = 1;
    OMX_GetParameter(VideoDecoder->hComponent, OMX_IndexParamPortDefinition, &sPortDef);
    nWidth = sPortDef.format.video.nFrameWidth;
    nHeight = sPortDef.format.video.nFrameHeight;

    OMX_CONFIG_RECTTYPE sTargetRect;
    OMX_CONFIG_RECTTYPE sRect;
    OMX_INIT_STRUCT(&sRect, OMX_CONFIG_RECTTYPE);
    sRect.nPortIndex = 1;
    if(OMX_ErrorNone == OMX_GetConfig(VideoDecoder->hComponent, OMX_IndexConfigCommonOutputCrop, &sRect)) {
        sTargetRect.nLeft = MAX(sRect.nLeft, sVideoRect.nLeft);
        sTargetRect.nTop = MAX(sRect.nTop, sVideoRect.nTop);
        sTargetRect.nWidth = MIN(sRect.nWidth, sVideoRect.nWidth);
        sTargetRect.nHeight = MIN(sRect.nHeight, sVideoRect.nHeight);

        LOG_DEBUG("User set video crop from: %d,%d,%d,%d",
                sRect.nLeft, sRect.nTop, sRect.nWidth, sRect.nHeight);
    }
    else {
        sTargetRect.nLeft = sVideoRect.nLeft;
        sTargetRect.nTop = sVideoRect.nTop;
        sTargetRect.nWidth = MIN(sVideoRect.nWidth, nWidth);
        sTargetRect.nHeight = MIN(sVideoRect.nHeight, nHeight);

        LOG_DEBUG("User set video crop from: %d,%d,%d,%d",
                0, 0, nWidth, nHeight);
    }

    if(sTargetRect.nLeft > ((OMX_S32)nWidth - 128) || sTargetRect.nHeight > (nWidth - 128))
        return OMX_ErrorBadParameter;

    sRectIn.nLeft = sTargetRect.nLeft;
    sRectIn.nTop = sTargetRect.nTop;
    sRectIn.nWidth = sTargetRect.nWidth;
    sRectIn.nHeight = sTargetRect.nHeight;

    LOG_DEBUG("to  %d,%d,%d,%d\n",
            sRectIn.nLeft, sRectIn.nTop, sRectIn.nWidth, sRectIn.nHeight);

    fsl_osal_memcpy(&sOutputModeTmp, &sOutputMode, sizeof(OMX_CONFIG_OUTPUTMODE));
    ApplyDispScaleFactor();
    CalcCrop(eScreen,&sScaledRectIn,&sRectOut,&sCropRect);
    SetCrop(eScreen,&sCropRect,&sOutputModeTmp);
    if(OMX_ErrorNone != OMX_SetConfig(VideoRender->hComponent, OMX_IndexOutputMode, &sOutputModeTmp))
        LOG_ERROR("%s,%d,set output mode failed\n",__FUNCTION__,__LINE__);

    fsl_osal_memcpy(&sOutputMode, &sOutputModeTmp, sizeof(OMX_CONFIG_OUTPUTMODE));

    fsl_osal_mutex_unlock(Lock);

    return OMX_ErrorNone;
}

OMX_ERRORTYPE GMPlayer::ParserMp3Metadata()
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;
	OMX_ERRORTYPE (*ParserMetadata)(OMX_PTR *, OMX_PARAM_CONTENTURITYPE *, OMX_PARAM_CONTENTPIPETYPE *);

    ret = GetContentPipe(&hPipe, (OMX_STRING)&Content->contentURI);
    if(ret != OMX_ErrorNone)
		return ret;

	OMX_PARAM_CONTENTPIPETYPE sPipe;
	OMX_INIT_STRUCT(&sPipe, OMX_PARAM_CONTENTPIPETYPE);
	sPipe.hPipe = hPipe;

    libMgr = FSL_NEW(ShareLibarayMgr, ());
    if(libMgr == NULL)
        return OMX_ErrorInsufficientResources;

    hLibMp3 = libMgr->load((fsl_osal_char*)"lib_omx_mp3_parser_v2_arm11_elinux.so");
    if(hLibMp3 == NULL) {
        FSL_DELETE(libMgr);
        return OMX_ErrorUndefined;
    }

    ParserMetadata = (OMX_ERRORTYPE (*)(OMX_PTR *, OMX_PARAM_CONTENTURITYPE *, OMX_PARAM_CONTENTPIPETYPE *))libMgr->getSymbol(hLibMp3, (fsl_osal_char*)"ParserMetadata");
    if(ParserMetadata == NULL) {
        libMgr->unload(hLibMp3);
		hLibMp3 = NULL;
        FSL_DELETE(libMgr);
        return OMX_ErrorUndefined;
    }

	ParserMetadata(&ParserMetadataHandle, Content, &sPipe);

    return ret;
}

OMX_ERRORTYPE GMPlayer::loadMetadataExtractor(
        OMX_STRING contentURI)
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;
    OMX_STRING role;
    OMX_U32 len;
    OMX_U8 *ptr = NULL;

    libMgr = NULL;
    hLibMp3 = NULL;
	bEnableAudio = OMX_FALSE;
	bGetMp3Metadata = OMX_FALSE;

	if(contentURI == NULL)
        return OMX_ErrorBadParameter;

    fsl_osal_mutex_lock(Lock);

    if(State != GM_STATE_STOP) {
        fsl_osal_mutex_unlock(Lock);
        return OMX_ErrorNone;
    }

    len = fsl_osal_strlen(contentURI) + 1;
    ptr = (OMX_U8*)FSL_MALLOC(sizeof(OMX_PARAM_CONTENTURITYPE) + len);
    if(ptr == NULL) {
        fsl_osal_mutex_unlock(Lock);
        return OMX_ErrorInsufficientResources;
    }

    Content = (OMX_PARAM_CONTENTURITYPE*)ptr;
    if(fsl_osal_strncmp(contentURI, "file://", 7) == 0)
        fsl_osal_strcpy((fsl_osal_char*)(&Content->contentURI), contentURI + 7);
    else
        fsl_osal_strcpy((fsl_osal_char*)(&Content->contentURI), contentURI);

    printf("Loading content: %s\n", (fsl_osal_char*)(&Content->contentURI));

    if(fsl_osal_strncmp(contentURI, "http://", 7) == 0
            || fsl_osal_strncmp(contentURI, "rtsp://", 7) == 0
            || fsl_osal_strncmp(contentURI, "mms://", 6) == 0
            || fsl_osal_strncmp(contentURI, "udp://", 6) == 0
            || fsl_osal_strncmp(contentURI, "rtp://", 6) == 0)
        bStreamingSource = OMX_TRUE;

    role = MediaTypeInspect();
    if(role == NULL) {
        LOG_ERROR("Can't inspec content format.\n");
        FSL_FREE(Content);
        fsl_osal_mutex_unlock(Lock);
        return OMX_ErrorUndefined;
    }

	struct timeval tv, tv1;
	gettimeofday (&tv, NULL);

    if (fsl_osal_strcmp(role, "parser.mp3")==0 && bStreamingSource == OMX_FALSE) {
		ParserMp3Metadata();
		bGetMp3Metadata = OMX_TRUE;
	} else {
		bGetMetadata = OMX_TRUE;
		ret = LoadParser(role);
		if(ret != OMX_ErrorNone) {
			LOG_ERROR("Can't load content %s\n", contentURI);
			fsl_osal_mutex_unlock(Lock);
			return ret;
		}

		bGetMetadata = OMX_FALSE;
		State = GM_STATE_LOADING;
	}
	gettimeofday (&tv1, NULL);
	LOG_DEBUG("Load Parser Time: %d\n", (tv1.tv_sec-tv.tv_sec)*1000+(tv1.tv_usec-tv.tv_usec)/1000);

	fsl_osal_mutex_unlock(Lock);

	return OMX_ErrorNone;
}

OMX_ERRORTYPE GMPlayer::unloadMetadataExtractor()
{
	if (bGetMp3Metadata == OMX_TRUE) {
		OMX_ERRORTYPE (*UnloadParserMetadata)(OMX_PTR);

		UnloadParserMetadata = (OMX_ERRORTYPE (*)(OMX_PTR))libMgr->getSymbol(hLibMp3, (fsl_osal_char*)"UnloadParserMetadata");
		if(UnloadParserMetadata == NULL) {
			return OMX_ErrorUndefined;
		}

		UnloadParserMetadata(ParserMetadataHandle);

		if(Content != NULL)
			FSL_FREE(Content);

		if (libMgr && hLibMp3)
			libMgr->unload(hLibMp3);
		FSL_DELETE(libMgr);
		return OMX_ErrorNone;
	}

	if (!(State == GM_STATE_LOADING || State == GM_STATE_LOADED)) {
        return OMX_ErrorIncorrectStateOperation;
    }

	if (State == GM_STATE_LOADING) {
		fsl_osal_mutex_lock(Lock);

		if(Content != NULL)
			FSL_FREE(Content);

		Parser->StateTransDownWard(OMX_StateIdle);
		Parser->StateTransDownWard(OMX_StateLoaded);
		Parser->UnLoad();
		FSL_DELETE(Parser);

		State = GM_STATE_STOP;
		fsl_osal_mutex_unlock(Lock);
	}
	else if (State == GM_STATE_LOADED) {
		Stop();
	}

    return OMX_ErrorNone;
}

OMX_U32 GMPlayer::getMetadataNum()
{
    OMX_CONFIG_METADATAITEMCOUNTTYPE sMatadataItemCount;

	if (bGetMp3Metadata == OMX_TRUE) {
		OMX_U32 (*GetMetadataNum)(OMX_PTR);

		GetMetadataNum = (OMX_U32 (*)(OMX_PTR))libMgr->getSymbol(hLibMp3, (fsl_osal_char*)"GetMetadataNum");
		if(GetMetadataNum == NULL) {
			return 0;
		}

		return GetMetadataNum(ParserMetadataHandle);
	}

	if (!(State == GM_STATE_LOADING || State == GM_STATE_LOADED)) {
		LOG_WARNING("Get metadata number in invalid state.\n");
		return 0;
	}

    OMX_INIT_STRUCT(&sMatadataItemCount, OMX_CONFIG_METADATAITEMCOUNTTYPE);
	sMatadataItemCount.eScopeMode = OMX_MetadataScopeAllLevels;
	OMX_GetConfig(Parser->hComponent, OMX_IndexConfigMetadataItemCount, &sMatadataItemCount);
	LOG_DEBUG("Metadata count: %d\n", sMatadataItemCount.nMetadataItemCount);

    return sMatadataItemCount.nMetadataItemCount;
}

OMX_U32 GMPlayer::getMetadataSize(OMX_U32 index)
{
    OMX_CONFIG_METADATAITEMTYPE sMatadataItem;

	if (bGetMp3Metadata == OMX_TRUE) {
		OMX_U32 (*GetMetadataSize)(OMX_PTR handle, OMX_U32);

		GetMetadataSize = (OMX_U32 (*)(OMX_PTR handle, OMX_U32))libMgr->getSymbol(hLibMp3, (fsl_osal_char*)"GetMetadataSize");
		if(GetMetadataSize == NULL) {
			return 0;
		}

		return GetMetadataSize(ParserMetadataHandle, index);
	}

	if (!(State == GM_STATE_LOADING || State == GM_STATE_LOADED)) {
		LOG_WARNING("Get metadata number in invalid state.\n");
		return 0;
	}

    OMX_INIT_STRUCT(&sMatadataItem, OMX_CONFIG_METADATAITEMTYPE);
	sMatadataItem.eScopeMode = OMX_MetadataScopeAllLevels;
	sMatadataItem.eSearchMode = OMX_MetadataSearchValueSizeByIndex;
	sMatadataItem.nMetadataItemIndex = index;
	OMX_GetConfig(Parser->hComponent, OMX_IndexConfigMetadataItem, &sMatadataItem);
	//LOG_DEBUG("Metadata value size: %d\n", sMatadataItem.nValueMaxSize);

    return sMatadataItem.nValueMaxSize;
}

OMX_ERRORTYPE GMPlayer::getMetadata(
		OMX_U32 index, 
		OMX_METADATA *pMetadata)
{
	OMX_ERRORTYPE (*GetMetadata)(OMX_PTR, OMX_CONFIG_METADATAITEMTYPE *) = NULL;

	if (bGetMp3Metadata == OMX_TRUE) {

		GetMetadata = (OMX_ERRORTYPE (*)(OMX_PTR, OMX_CONFIG_METADATAITEMTYPE *))libMgr->getSymbol(hLibMp3, (fsl_osal_char*)"GetMetadata");
		if(GetMetadata == NULL) {
			return OMX_ErrorUndefined;
		}
	}

	if (!(State == GM_STATE_LOADING || State == GM_STATE_LOADED || bGetMp3Metadata == OMX_TRUE)) {
		LOG_WARNING("Get metadata number in invalid state.\n");
        return OMX_ErrorIncorrectStateOperation;
	}

	OMX_CONFIG_METADATAITEMTYPE *pMetadataItem;

	OMX_U32 nMetadataSize = getMetadataSize(index);

	pMetadataItem = (OMX_CONFIG_METADATAITEMTYPE *)FSL_MALLOC(sizeof(OMX_CONFIG_METADATAITEMTYPE) + nMetadataSize);;
	if (pMetadataItem == NULL) {
		LOG_ERROR("Can't get memory.\n");
		return OMX_ErrorInsufficientResources;
	}
	fsl_osal_memset(pMetadataItem, 0, sizeof(OMX_CONFIG_METADATAITEMTYPE) + nMetadataSize);

	OMX_INIT_STRUCT(pMetadataItem, OMX_CONFIG_METADATAITEMTYPE);

	pMetadataItem->eScopeMode = OMX_MetadataScopeAllLevels;
	pMetadataItem->eSearchMode = OMX_MetadataSearchItemByIndex;
	pMetadataItem->nMetadataItemIndex = index;
	if (bGetMp3Metadata == OMX_TRUE) {
		GetMetadata(ParserMetadataHandle, pMetadataItem);
	} else {
		OMX_GetConfig(Parser->hComponent, OMX_IndexConfigMetadataItem, pMetadataItem);
	}

	fsl_osal_memcpy(pMetadata->nKey, pMetadataItem->nKey, pMetadataItem->nKeySizeUsed);
	pMetadata->nKeySizeUsed = pMetadataItem->nKeySizeUsed;
	fsl_osal_memcpy(pMetadata->nValue, pMetadataItem->nValue, pMetadataItem->nValueSizeUsed);
	pMetadata->nValueSizeUsed = pMetadataItem->nValueSizeUsed;

	FSL_FREE(pMetadataItem);

    return OMX_ErrorNone;
}

static OMX_ERRORTYPE ConvertImage(
        OMX_IMAGE_PORTDEFINITIONTYPE *pIn_fmt, 
        OMX_CONFIG_RECTTYPE *pCropRect, 
        OMX_U8 *in, 
        OMX_IMAGE_PORTDEFINITIONTYPE *pOut_fmt, 
        OMX_U8 *out)
{
    OMX_ImageConvert* ic;
    ic = OMX_ImageConvertCreate();
    if(ic == NULL)
        return OMX_ErrorInsufficientResources;

    //resize image
    ic->resize(ic, pIn_fmt, pCropRect, in, pOut_fmt, out);
    ic->delete_it(ic);

    return OMX_ErrorNone;
}

OMX_ERRORTYPE GMPlayer::getThumbnail(
		OMX_IMAGE_PORTDEFINITIONTYPE *pfmt, 
		OMX_TICKS position, 
		OMX_U8 **ppBuf)
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;
    OMX_U8 *thumb_buf = NULL;
	OMX_S32 size;

    if(bHasVideo != OMX_TRUE) 
		return OMX_ErrorFormatNotDetected;

    fsl_osal_mutex_lock(Lock);

    ret = SetupPipeline();
    if(ret != OMX_ErrorNone)
        goto err1;

    LoadClock();
    AttachClock(PARSER_CLOCK_PORT, Parser, 2);

    ret = StartPipeline();
    if(ret != OMX_ErrorNone)
        goto err1;

    State = GM_STATE_LOADED;
    fsl_osal_mutex_unlock(Lock);

    //get the video format
    OMX_IMAGE_PORTDEFINITIONTYPE in_format;
    OMX_CONFIG_RECTTYPE sCropRect;
    GetScreenShotInfo(&in_format, &sCropRect);

    //allocate buffer for save thumbnail picture
    size = in_format.nFrameWidth * in_format.nFrameHeight * 2;
    thumb_buf = (OMX_U8*)FSL_MALLOC(size);
    if(thumb_buf == NULL) {
		ret = OMX_ErrorInsufficientResources;
        goto err1;
    }
    fsl_osal_memset(thumb_buf, 0, size);

    //get thumbnail
    if(OMX_TRUE != GetThumbNail(position, thumb_buf)) {
        LOG_ERROR("Failed to get thumnail\n");
        FSL_FREE(thumb_buf);
		ret = OMX_ErrorBadParameter;
        goto err1;
    }

	LOG_DEBUG("Video resolution: %d,%d", in_format.nFrameWidth, in_format.nFrameHeight);

	LOG_DEBUG("Video crop: %d,%d,%d,%d",
			sCropRect.nLeft, sCropRect.nTop, sCropRect.nWidth, sCropRect.nHeight);
	sCropRect.nWidth &= ~1;
	sCropRect.nHeight &= ~1;
	pfmt->nFrameWidth = sCropRect.nWidth;
    pfmt->nFrameHeight = sCropRect.nHeight;
	LOG_DEBUG("thumbnail resolution: %d,%d", pfmt->nFrameWidth, pfmt->nFrameHeight);

    //allocate buffer for save thumbnail picture
    size = pfmt->nFrameWidth * pfmt->nFrameHeight * 2;
    *ppBuf = (OMX_U8*)FSL_MALLOC(size);
    if(*ppBuf == NULL) {
        FSL_FREE(thumb_buf);
		FSL_FREE(*ppBuf);
		ret = OMX_ErrorInsufficientResources;
        goto err1;
    }
    fsl_osal_memset(*ppBuf, 0, size);

    //convert thumbnail picture to required format and resolution
    ConvertImage(&in_format, &sCropRect, thumb_buf, pfmt, *ppBuf);

    FSL_FREE(thumb_buf);

    return OMX_ErrorNone;

err1:
    fsl_osal_mutex_unlock(Lock);
    Stop();

    return ret;
}

OMX_ERRORTYPE GMPlayer::SetDisplayMode(
        OMX_CONFIG_RECTTYPE *pInRect,
        OMX_CONFIG_RECTTYPE *pOutRect,
        GM_VIDEO_MODE eVideoMode, 
        GM_VIDEO_DEVICE tv_dev,
        OMX_U32 nRotate
        )
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;
    SCREENTYPE eScreenTemp = SCREEN_STD;
    TV_MODE eTvMode = MODE_NONE;
    OMX_BOOL bTv = OMX_FALSE;

    fsl_osal_mutex_lock(Lock);

    if(VideoRender == NULL) {
        fsl_osal_mutex_unlock(Lock);
        return OMX_ErrorInvalidComponent;
    }

    printf("SetDisplayMode eVideoMode %d, tv_dev %d, nRotate %d, OutRect %d,%d,%d,%d.\n", 
        eVideoMode, tv_dev, nRotate, pOutRect->nWidth, pOutRect->nHeight, pOutRect->nLeft, pOutRect->nTop);

    if(pInRect != NULL)
        printf("SetDisplayMode InRect %d,%d,%d,%d.\n", pInRect->nWidth, pInRect->nHeight, pInRect->nLeft, pInRect->nTop);
    if(pOutRect != NULL)
        printf("SetDisplayMode OutRect %d,%d,%d,%d.\n", pOutRect->nWidth, pOutRect->nHeight, pOutRect->nLeft, pOutRect->nTop);

    switch (eVideoMode){
        case GM_VIDEO_MODE_NORMAL:
            eScreenTemp = SCREEN_STD;
            break;
        case GM_VIDEO_MODE_FULLSCREEN:
            eScreenTemp = SCREEN_FUL;
            break;
        case GM_VIDEO_MODE_ZOOM:
            eScreenTemp = SCREEN_ZOOM;
            break;
        default:
            fsl_osal_mutex_unlock(Lock);
            return OMX_ErrorBadParameter;
    };

    switch (tv_dev){
        case GM_VIDEO_DEVICE_LCD:
            eTvMode = MODE_NONE;
            bTv = OMX_FALSE;
            break;
        case GM_VIDEO_DEVICE_TV_NTSC:
            eTvMode = MODE_NTSC;
            bTv = OMX_TRUE;
            break;
        case GM_VIDEO_DEVICE_TV_PAL:
            eTvMode = MODE_PAL;
            bTv = OMX_TRUE;
            break;
        case GM_VIDEO_DEVICE_TV_720P:
            eTvMode = MODE_720P;
            bTv = OMX_TRUE;
            break;
        default:
            fsl_osal_mutex_unlock(Lock);
            return OMX_ErrorBadParameter;
    };

    if(pOutRect != NULL) {
        sDispWindow.nLeft = pOutRect->nLeft;
        sDispWindow.nTop = pOutRect->nTop;
        sDispWindow.nWidth = pOutRect->nWidth;
        sDispWindow.nHeight = pOutRect->nHeight;
    }
    else if(eTvMode == MODE_PAL){
        sDispWindow.nTop = 0;
        sDispWindow.nWidth = 720;
        sDispWindow.nLeft = 0;
        sDispWindow.nHeight = 576;
    }
    else if (eTvMode == MODE_NTSC)
    {
        sDispWindow.nTop = 0;
        sDispWindow.nWidth = 720;
        sDispWindow.nLeft = 0;
        sDispWindow.nHeight = 480;
    }
    else if (eTvMode == MODE_720P)
    {
        sDispWindow.nTop = 0;
        sDispWindow.nWidth = 1280;
        sDispWindow.nLeft = 0;
        sDispWindow.nHeight = 720;
    }
    else
        printf("keep original sDispWindow\n");

    /* check whether parameters are changed or not */
    if (eScreenTemp == eScreen &&
        sOutputMode.bTv  == bTv &&
        sOutputMode.eTvMode == eTvMode &&
        sOutputMode.eRotation == (ROTATION)nRotate) 
    {
        OMX_S32 left_diff = ABS(sOutputMode.sRectOut.nLeft - sDispWindow.nLeft);
        OMX_S32 top_diff = ABS(sOutputMode.sRectOut.nTop - sDispWindow.nTop);
        OMX_S32 width_diff = ABS((OMX_S32)sOutputMode.sRectOut.nWidth - (OMX_S32)sDispWindow.nWidth);
        OMX_S32 height_diff = ABS((OMX_S32)sOutputMode.sRectOut.nHeight - (OMX_S32)sDispWindow.nHeight);

        if(left_diff < 8 && top_diff < 8 && width_diff < 8 && height_diff < 8) {
            printf("GMPlayer set same display rectagle.\n");
            fsl_osal_mutex_unlock(Lock);
            return OMX_ErrorNone;
        }
        
    }

    fsl_osal_memcpy(&sRectOut, &sDispWindow, sizeof(OMX_CONFIG_RECTTYPE));
    
    OMX_CONFIG_OUTPUTMODE sOutputModeTmp;
    OMX_INIT_STRUCT(&sOutputModeTmp, OMX_CONFIG_OUTPUTMODE);
    sOutputModeTmp.nPortIndex = 0;
    sOutputModeTmp.bTv = bTv;
    sOutputModeTmp.eFbLayer = LAYER2;
    sOutputModeTmp.eTvMode = eTvMode;
    sOutputModeTmp.eRotation = (ROTATION)nRotate;
    OMX_CONFIG_RECTTYPE sCropRect;
    OMX_INIT_STRUCT(&sCropRect, OMX_CONFIG_RECTTYPE);

    ResetRotateWhenTvOut(&sOutputModeTmp);

    switch (sOutputModeTmp.eRotation)
    {
        case ROTATE_NONE:
        case ROTATE_VERT_FLIP:
        case ROTATE_HORIZ_FLIP:
        case ROTATE_180:
            bRotate = OMX_FALSE;
            break;
        case ROTATE_90_RIGHT:
        case ROTATE_90_RIGHT_VFLIP:
        case ROTATE_90_RIGHT_HFLIP:
        case ROTATE_90_LEFT:
            bRotate = OMX_TRUE;
            break;
        default :
            break;
    }

    //ModifyOutputRectWhenRotate(&sOutputModeTmp);
    if (bRotate)
    {
        sRectOut.nLeft = sDispWindow.nTop;
        sRectOut.nTop = sDispWindow.nLeft;
        sRectOut.nHeight = sDispWindow.nWidth;
        sRectOut.nWidth = sDispWindow.nHeight;
    }

    ApplyDispScaleFactor();

    CalcCrop(eScreenTemp,&sScaledRectIn,&sRectOut,&sCropRect);
    printf("CalcCrop=> sCropRect: %d,%d,%d,%d\n", sCropRect.nWidth, sCropRect.nHeight, sCropRect.nLeft, sCropRect.nTop);

    SetCrop(eScreenTemp,&sCropRect,&sOutputModeTmp);
    printf("SetCrop=> sOutputModeTmp.sRectOut: %d,%d,%d,%d\n", 
        sOutputModeTmp.sRectOut.nWidth, sOutputModeTmp.sRectOut.nHeight, sOutputModeTmp.sRectOut.nLeft, sOutputModeTmp.sRectOut.nTop);

    LOG_DEBUG("%s,%d,set rotate to %d\n",__FUNCTION__,__LINE__,sOutputModeTmp.eRotation);
    LOG_DEBUG("%s,%d,set screen mode to %d\n",__FUNCTION__,__LINE__,eScreenTemp);
    ret = OMX_SetConfig(VideoRender->hComponent, OMX_IndexOutputMode, &sOutputModeTmp);
    if (ret != OMX_ErrorNone)
        LOG_ERROR("%s,%d,set output mode failed,ret %d\n",__FUNCTION__,__LINE__,ret);

    fsl_osal_memcpy(&sOutputMode, &sOutputModeTmp, sizeof(OMX_CONFIG_OUTPUTMODE));
    eScreen = eScreenTemp;

    fsl_osal_mutex_unlock(Lock);

    return ret;

}

OMX_ERRORTYPE GMPlayer::DistroyVideoPipeline()
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;

	ret = VideoDecoder->PortDisable(FILTER_INPUT_PORT);
	if (ret != OMX_ErrorNone) {
		LOG_ERROR("Video decoder input port disable failed.\n");
		return ret;
	}

	ret = Parser->PortDisable(Parser->PortPara[VIDEO_PORT_PARA].nStartPortNumber);
	if (ret != OMX_ErrorNone) {
		LOG_ERROR("Parser video output port disable failed.\n");
		return ret;
	}

        DisableParserVideoPort();

	DeAttachClock(VIDEO_CLOCK_PORT, VideoRender, 1);

	Pipeline->Remove(VideoDecoder);
	VideoDecoder->StateTransDownWard(OMX_StateIdle);
	Pipeline->Remove(VideoRender);
	VideoRender->StateTransDownWard(OMX_StateIdle);
	VideoDecoder->StateTransDownWard(OMX_StateLoaded);
	VideoRender->StateTransDownWard(OMX_StateLoaded);
	VideoDecoder->UnLoad();
	FSL_DELETE(VideoDecoder);
	VideoRender->UnLoad();
	FSL_DELETE(VideoRender);

    return ret;
}

OMX_ERRORTYPE GMPlayer::ReSetupVideoPipeline()
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;

        OMX_PARAM_U32TYPE u32Type;
        OMX_INIT_STRUCT(&u32Type, OMX_PARAM_U32TYPE);
        u32Type.nPortIndex = Parser->PortPara[VIDEO_PORT_PARA].nStartPortNumber;
        u32Type.nU32 = 0;
        OMX_SetParameter(Parser->hComponent, OMX_IndexParamActiveStream, &u32Type);

        OMX_BOOL bInsert = OMX_TRUE;
        OMX_SetConfig(Parser->hComponent, OMX_IndexConfigInsertVideoCodecData, &bInsert);

	// Seek video track to play time. Need send codec config data.
	ret = SeekTrackToPlaytime(Parser->PortPara[VIDEO_PORT_PARA].nStartPortNumber);
	if (ret != OMX_ErrorNone) {
		LOG_ERROR("Seek video track to play time failed.\n");
		return ret;
	}

	ret = SetupVideoPipeline();
	if(ret != OMX_ErrorNone) {
		LOG_ERROR("SetupVideoPipeline() faint\n");
		return ret;
	}

        bHasVideo = OMX_TRUE;

	ret = Parser->StartProcess(Parser->PortPara[VIDEO_PORT_PARA].nStartPortNumber);
	if (ret != OMX_ErrorNone) {
		LOG_ERROR("Parser start process failed.\n");
		return ret;
	}

	ret = StartVideoPipeline();
	if(ret != OMX_ErrorNone) {
		LOG_ERROR("StartVideoPipeline failed.\n");
		return ret;
	}

    return ret;
}

OMX_ERRORTYPE GMPlayer::SetVideoScalingMode(OMX_S32 mode)
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;

    LOG_DEBUG("SetVideoScalingMode: %d\n", mode);

    mScalingMode = mode;

    if (VideoRender)
        ret = OMX_SetConfig(VideoRender->hComponent, OMX_IndexConfigScalingMode, &mScalingMode);

    return ret;
}


OMX_ERRORTYPE GMPlayer::SetVideoSurface(OMX_PTR surface, OMX_VIDEO_SURFACE_TYPE type)
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;
    OMX_BOOL wasPlaying = OMX_FALSE;

    fsl_osal_mutex_lock(Lock);

    mSurface = surface;
    mSurfaceType = type;

    LOG_DEBUG("SetVideoSurface: %p\n", mSurface);

    if (bVideoDirectRendering == OMX_TRUE
            && (State == GM_STATE_PLAYING || State == GM_STATE_PAUSE || State == GM_STATE_LOADED)) {
        if (State == GM_STATE_PLAYING) {
            wasPlaying = OMX_TRUE;

            if(AudioRender != NULL)
                AudioRender->StateTransDownWard(OMX_StatePause);
            if(VideoRender != NULL)
                VideoRender->StateTransDownWard(OMX_StatePause);
            if(Clock != NULL)
                Clock->StateTransDownWard(OMX_StatePause);
        }

        if(bHasVideo) {
            ret = DistroyVideoPipeline();
            if (ret != OMX_ErrorNone) {
                LOG_ERROR("DistroyVideoPipeline failed.\n");
                fsl_osal_mutex_unlock(Lock);
                return ret;
            }
        }

        if (mSurface != NULL) {
            ret = ReSetupVideoPipeline();
            if (ret != OMX_ErrorNone) {
                LOG_ERROR("ReSetupVideoPipeline failed.\n");
                DisableParserVideoPort();
                fsl_osal_mutex_unlock(Lock);
                return ret;
            }
        }

        if (wasPlaying == OMX_TRUE) {
            if(Clock != NULL)
                Clock->StateTransUpWard(OMX_StateExecuting);
            if(AudioRender != NULL)
                AudioRender->StateTransUpWard(OMX_StateExecuting);
            if(VideoRender != NULL)
                VideoRender->StateTransUpWard(OMX_StateExecuting);
        }
    } else {
        if(VideoRender != NULL) {
            ret = OMX_SetConfig(VideoRender->hComponent, OMX_IndexParamSurface, mSurface);
        }
    }

    fsl_osal_mutex_unlock(Lock);


    return ret;
}

OMX_ERRORTYPE GMPlayer::SetVideoRenderType(OMX_VIDEO_RENDER_TYPE type)
{
    if(type >= OMX_VIDEO_RENDER_NUM)
        return OMX_ErrorBadParameter;
    
    mVideoRenderType = type;

    LOG_DEBUG("SetVideoRenderType %d\n", type);

    if(type == OMX_VIDEO_RENDER_V4L || type == OMX_VIDEO_RENDER_SURFACE)
        bVideoDirectRendering = OMX_TRUE;
    else
        bVideoDirectRendering = OMX_FALSE;
    
    return OMX_ErrorNone;
}

OMX_ERRORTYPE GMPlayer::SetAudioPassThrough(OMX_BOOL bEnable)
{
    LOG_DEBUG("SetAudioPassThrough %d\n", bEnable);

    if(bEnable == bAudioPassthrough)
        return OMX_ErrorNone;

    fsl_osal_mutex_lock(Lock);

    bAudioPassthrough = bEnable;
    if(State == GM_STATE_STOP || State == GM_STATE_LOADING) {
        fsl_osal_mutex_unlock(Lock);
        return OMX_ErrorNone;
    }

    if(AudioFmt == OMX_AUDIO_CodingAC3) {
        if(bAudioPassthrough == OMX_TRUE) {
            ReSetupAudioPipeline((OMX_STRING)"audio_processor.ac3toiec937", GetCurAudioTrack());
        }
        else
            ReSetupAudioPipeline((OMX_STRING)"audio_decoder.ac3", GetCurAudioTrack());
    }

    fsl_osal_mutex_unlock(Lock);

    return OMX_ErrorNone;
}

OMX_ERRORTYPE GMPlayer::DeStructAudioPipeline()
{
    if(AudioDecoder) {
        AudioDecoder->PortDisable(FILTER_INPUT_PORT);
        Parser->PortDisable(Parser->PortPara[AUDIO_PORT_PARA].nStartPortNumber);
        AudioDecoder->StateTransDownWard(OMX_StateIdle);
    }
    if(AudioPostProcess)
        AudioPostProcess->StateTransDownWard(OMX_StateIdle);
    if(AudioRender)
        AudioRender->StateTransDownWard(OMX_StateIdle);
    if(AudioDecoder) {
        AudioDecoder->StateTransDownWard(OMX_StateLoaded);
        Pipeline->Remove(AudioDecoder);
        AudioDecoder->UnLoad();
        FSL_DELETE(AudioDecoder);
    }
    if(AudioPostProcess) {
        AudioPostProcess->StateTransDownWard(OMX_StateLoaded);
        Pipeline->Remove(AudioPostProcess);
        AudioPostProcess->UnLoad();
        FSL_DELETE(AudioPostProcess);
    }
    if(AudioRender) {
        AudioRender->StateTransDownWard(OMX_StateLoaded);
        Pipeline->Remove(AudioRender);
        AudioRender->UnLoad();
        FSL_DELETE(AudioRender);
    }

    DisableParserAudioPort();

    return OMX_ErrorNone;
}

OMX_ERRORTYPE GMPlayer::ReSetupAudioPipeline(OMX_STRING NewAudioRole, OMX_S32 nAudioTrackIdx)
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;
    GMComponent *AudioDecoderTmp = NULL;

    if(Clock->IsPortEnabled(AUDIO_CLOCK_PORT)) {
        DeAttachClock(AUDIO_CLOCK_PORT, AudioRender, 1);
        DeStructAudioPipeline();
    }

    if(nAudioTrackIdx < 0)
        nAudioTrackIdx = 0;
    OMX_PARAM_U32TYPE u32Type;
    OMX_INIT_STRUCT(&u32Type, OMX_PARAM_U32TYPE);
    u32Type.nPortIndex = Parser->PortPara[AUDIO_PORT_PARA].nStartPortNumber;
    u32Type.nU32 = nAudioTrackIdx;
    OMX_SetParameter(Parser->hComponent, OMX_IndexParamActiveStream, &u32Type);

    // Seek audio track to play time.
    ret = SeekTrackToPlaytime(Parser->PortPara[AUDIO_PORT_PARA].nStartPortNumber);
    if (ret != OMX_ErrorNone) {
        LOG_ERROR("Seek audio track to play time failed.\n");
        DeStructAudioPipeline();
        return OMX_ErrorNone;
    }

    ret = SetupAudioPipeline(NewAudioRole);
    if(ret != OMX_ErrorNone) {
        LOG_ERROR("SetupAudioPipeline() with role %s faint\n", NewAudioRole);
        DeStructAudioPipeline();
        return OMX_ErrorNone;
    }

    ret = Parser->StartProcess(Parser->PortPara[AUDIO_PORT_PARA].nStartPortNumber);
    if(ret != OMX_ErrorNone) {
        LOG_ERROR("Start parser port error, ret: %x\n", ret);
        DeStructAudioPipeline();
        return OMX_ErrorNone;
    }

    ret = StartAudioPipeline();
    if(ret != OMX_ErrorNone) {
        LOG_ERROR("Start audio pipeline for role %s failed, ret: %x.\n", ret);
        DeStructAudioPipeline();
        return OMX_ErrorNone;
    }

    if(State == GM_STATE_PLAYING)
        AudioRender->StateTransUpWard(OMX_StateExecuting);

    return OMX_ErrorNone;
}

OMX_ERRORTYPE GMPlayer::GetAudioTrackType(
        OMX_U32 nTrackIndex, 
        OMX_AUDIO_CODINGTYPE *pType)
{
    if (!(State == GM_STATE_PLAYING || State == GM_STATE_PAUSE || State == GM_STATE_LOADED)) {
        LOG_ERROR("Require audio track number in invalid state.\n");
        return OMX_ErrorIncorrectStateOperation;
    }

    if(nTrackIndex > nAudioTrackCnt) {
        LOG_ERROR("Track index is invalid.\n");
        return OMX_ErrorBadParameter;
    }

    *pType = AudioTrackInfo->GetNode(nTrackIndex)->eCodingType;
    return OMX_ErrorNone;
}

OMX_ERRORTYPE GMPlayer::AddOutBandSubtitleSource(
        OMX_STRING uri, 
        OMX_STRING type)
{
    CP_PIPETYPE *hPipe = NULL;
    fsl_osal_mutex_lock(Lock);

    GetContentPipe(&hPipe, uri);
    GMOutbandSubtitleSource *pOutbandSource = (GMOutbandSubtitleSource*)FSL_NEW(GMOutbandSubtitleSource, (this, type, hPipe, uri));
    if(pOutbandSource == NULL) {
        fsl_osal_mutex_unlock(Lock);
        return OMX_ErrorInsufficientResources;
    }

    sSUBTITLETRACKINFO *pSubTrackInfo = (sSUBTITLETRACKINFO *)FSL_MALLOC(sizeof(sSUBTITLETRACKINFO));
    if(pSubTrackInfo == NULL) {
        FSL_DELETE(pOutbandSource);
        fsl_osal_mutex_unlock(Lock);
        return OMX_ErrorInsufficientResources;
    }

    OMX_ERRORTYPE ret = pOutbandSource->Init();
    if(ret != OMX_ErrorNone) {
        LOG_ERROR("Out band source init failed.\n");
        FSL_DELETE(pOutbandSource);
        FSL_DELETE(pSubTrackInfo);
        fsl_osal_mutex_unlock(Lock);
        return ret;
    }

    pSubTrackInfo->bInBand = OMX_FALSE;
    pSubTrackInfo->nTrackNum = nOutbandSubtitleTrackCnt;
    fsl_osal_strcpy((OMX_STRING)pSubTrackInfo->type, type);
    nOutbandSubtitleTrackCnt ++;
    sprintf((OMX_STRING)pSubTrackInfo->lanuage, "%s %d", type, (int)nOutbandSubtitleTrackCnt);
    SubtitleTrackInfo->Add(pSubTrackInfo);
    OutbandSubtitleSource->Add((GMSubtitleSource*)pOutbandSource);

    if(pSubtitlePlayer == NULL) {
        if(OMX_ErrorNone != SetupSubtitlePlayer()) {
            LOG_ERROR("Setup subtitle player failed, disable subtitle.\n");
        }
    }

    fsl_osal_mutex_unlock(Lock);

    return OMX_ErrorNone;
}

OMX_ERRORTYPE GMPlayer::RenderSubtitle(
        GM_SUBTITLE_SAMPLE &sample)
{
    if(pAppCallback != NULL) {
        (*pAppCallback)(pAppData, GM_EVENT_SUBTITLE, &sample);
    }

    return OMX_ErrorNone;
}


OMX_BOOL GMPlayer::IsSeekable()
{
    OMX_PARAM_CAPABILITY sMediaSeekable;
    OMX_INIT_STRUCT(&sMediaSeekable, OMX_PARAM_CAPABILITY);
    sMediaSeekable.nPortIndex = OMX_ALL;
    if(Parser && Parser->hComponent)
        OMX_GetParameter(Parser->hComponent, OMX_IndexParamMediaSeekable, &sMediaSeekable);

    return sMediaSeekable.bCapability;
}



OMX_ERRORTYPE GMPlayer::SetupSubtitlePlayer()
{
    if(pSubtitlePlayer)
        return OMX_ErrorNone;

    pSubtitlePlayer = FSL_NEW(GMSubtitlePlayer, (this));
    if(pSubtitlePlayer == NULL)
        return OMX_ErrorInsufficientResources;
    return pSubtitlePlayer->Init();
}

void GMPlayer::DestroySubtitlePlayer()
{
    if(pSubtitlePlayer == NULL)
        return;

    pSubtitlePlayer->Stop();
    pSubtitlePlayer->DeInit();
    FSL_DELETE(pSubtitlePlayer);
    pSubtitlePlayer = NULL;
    return;
}

OMX_ERRORTYPE GMPlayer::SelectInbandSubtitle(OMX_S32 nSelectedTrack)
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;
    OMX_S32 index;

    if(nSelectedTrack < 0) {
        index = -1;
        OMX_SetConfig(Parser->hComponent, OMX_IndexParamSubtitleSelect, &index);
        return OMX_ErrorNone;
    }

    OMX_PARAM_CAPABILITY sMediaSeekable;
    OMX_INIT_STRUCT(&sMediaSeekable, OMX_PARAM_CAPABILITY);
    sMediaSeekable.nPortIndex = OMX_ALL;
    OMX_GetParameter(Parser->hComponent, OMX_IndexParamMediaSeekable, &sMediaSeekable);
    if (sMediaSeekable.bCapability != OMX_TRUE)
    {
        LOG_ERROR("Meida file is not seekable. Can't select subtitle track.\n");
        return OMX_ErrorUndefined;
    }

    OMX_TIME_CONFIG_TIMESTAMPTYPE sCur;
    OMX_INIT_STRUCT(&sCur, OMX_TIME_CONFIG_TIMESTAMPTYPE);
    sCur.nPortIndex = OMX_ALL;
    OMX_GetConfig(Clock->hComponent, OMX_IndexConfigTimeCurrentMediaTime, &sCur);

    sSUBTITLETRACKINFO *pSubTrackInfo = SubtitleTrackInfo->GetNode(nSelectedTrack);
    index = pSubTrackInfo->nTrackNum;
    ret = OMX_SetConfig(Parser->hComponent, OMX_IndexParamSubtitleSelect, &index);
    if(ret != OMX_ErrorNone)
        return ret;

    ret = OMX_SetConfig(Parser->hComponent, OMX_IndexParamSubtitleConfigTimePosition, &sCur.nTimestamp);
    if(ret != OMX_ErrorNone)
        return ret;

    GMInbandSubtitleSource *pSource = (GMInbandSubtitleSource*)InbandSubtitleSource;
    ret = pSource->SetType((OMX_STRING)pSubTrackInfo->type);
    if(ret != OMX_ErrorNone) {
        index = -1;
        OMX_SetConfig(Parser->hComponent, OMX_IndexParamSubtitleSelect, &index);
        return ret;
    }
    pSubtitlePlayer->SetSource(pSource);
    pSubtitlePlayer->Start();

    return OMX_ErrorNone;
}
        
OMX_ERRORTYPE GMPlayer::SelectOutbandSubtitle(OMX_S32 nSelectedTrack)
{
    if(nSelectedTrack < 0)
        return OMX_ErrorNone;

    sSUBTITLETRACKINFO *pSubTrackInfo = SubtitleTrackInfo->GetNode(nSelectedTrack);
    GMOutbandSubtitleSource *pOutbandSource = 
        (GMOutbandSubtitleSource*)OutbandSubtitleSource->GetNode(pSubTrackInfo->nTrackNum);

    OMX_TIME_CONFIG_TIMESTAMPTYPE sCur;
    OMX_INIT_STRUCT(&sCur, OMX_TIME_CONFIG_TIMESTAMPTYPE);
    sCur.nPortIndex = OMX_ALL;
    OMX_GetConfig(Clock->hComponent, OMX_IndexConfigTimeCurrentMediaTime, &sCur);

    pOutbandSource->SeekTo(sCur.nTimestamp);
    pSubtitlePlayer->SetSource((GMSubtitleSource*)pOutbandSource);
    pSubtitlePlayer->Start();

    return OMX_ErrorNone;
}

OMX_ERRORTYPE GMPlayer::DisableParserAudioPort()
{
    OMX_PARAM_U32TYPE u32Type;
    OMX_INIT_STRUCT(&u32Type, OMX_PARAM_U32TYPE);
    u32Type.nPortIndex = Parser->PortPara[AUDIO_PORT_PARA].nStartPortNumber;
    u32Type.nU32 = -1;
    OMX_SetParameter(Parser->hComponent, OMX_IndexParamActiveStream, &u32Type);
    Parser->PortDisable(Parser->PortPara[AUDIO_PORT_PARA].nStartPortNumber);
    bHasAudio = OMX_FALSE;

    return OMX_ErrorNone;
}

OMX_ERRORTYPE GMPlayer::DisableParserVideoPort()
{
    OMX_PARAM_U32TYPE u32Type;
    OMX_INIT_STRUCT(&u32Type, OMX_PARAM_U32TYPE);
    u32Type.nPortIndex = Parser->PortPara[VIDEO_PORT_PARA].nStartPortNumber;
    u32Type.nU32 = -1;
    OMX_SetParameter(Parser->hComponent, OMX_IndexParamActiveStream, &u32Type);
    Parser->PortDisable(Parser->PortPara[VIDEO_PORT_PARA].nStartPortNumber);
    bHasVideo = OMX_FALSE;

    return OMX_ErrorNone;
}
