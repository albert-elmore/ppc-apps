#include <MacTypes.h>
#include <QuickDraw.h>
#include <Windows.h>
#include <Controls.h>
#include <Events.h>
#include <Dialogs.h>
#include <TextEdit.h>
#include <Menus.h>
#include <Fonts.h>
#include <Sound.h>
#include <StandardFile.h>
#include <Files.h>
#include <Resources.h>
#include <Memory.h>
#include <OSUtils.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

#define kWindowWidth          820
#define kWindowHeight         718

#define kLoadLeft             20
#define kLoadTop              50
#define kLoadWidth            96
#define kLoadHeight           24

#define kPlayLeft             132
#define kPlayTop              50
#define kPlayWidth            96
#define kPlayHeight           24

#define kStopLeft             244
#define kStopTop              50
#define kStopWidth            96

#define kResetPitchLeft       356
#define kResetPitchTop        50
#define kResetPitchWidth      120
#define kResetPitchHeight     24
#define kStopHeight           24

#define kQuitLeft             720
#define kQuitTop              684
#define kQuitWidth            80
#define kQuitHeight           24

#define kWaveLeft             20
#define kWaveTop              88
#define kWaveWidth            780
#define kWaveHeight           108
#define kWavePoints           400
#define kWaveSubBuckets       16
#define kDecodePumpInterval   8192L

#define kSliderLeft           20
#define kSliderWidth          746
#define kSliderHeight         16

#define kStartSliderTop       218
#define kEndSliderTop         262
#define kShiftSliderTop       306
#define kGrainSizeSliderTop   350
#define kGrainRateSliderTop   394
#define kGainSliderTop        438
#define kPitchSliderTop       482
#define kFineSliderTop        526

#ifndef inUpButton
#define inUpButton 20
#define inDownButton 21
#define inPageUp 22
#define inPageDown 23
#define inThumb 129
#endif

#define kSliderMax            1000L
#define kBufferFrames         512L
#define kNumDBuffers          2
#define kMaxGrains            64
#define kSafeMaxActiveVoices  20

typedef struct StereoSampleBuffer {
    short          *samples;      /* interleaved LRLR... */
    long           frameCount;
    unsigned long  sampleRate;
    Boolean        loaded;
} StereoSampleBuffer;

typedef struct GrainVoice {
    Boolean active;
    long    startFrame;
    long    durationFrames;
    long    offsetFrames;
    unsigned long phase16;
    unsigned long phaseStep16;
    short   outputMode;      /* 1 = left, 2 = right */
} GrainVoice;

static WindowPtr      gWindow = NULL;
static Boolean        gDone = false;

static Rect           gLoadRect;
static Rect           gPlayRect;
static Rect           gStopRect;
static Rect           gResetPitchRect;
static Rect           gQuitRect;
static Rect           gWaveRect;

static ControlHandle  gLoadButton = NULL;
static ControlHandle  gPlayButton = NULL;
static ControlHandle  gStopButton = NULL;
static ControlHandle  gResetPitchButton = NULL;
static ControlHandle  gQuitButton = NULL;
static ControlActionUPP gSliderActionUPP = NULL;
static ControlHandle  gStartControl = NULL;
static ControlHandle  gEndControl = NULL;
static ControlHandle  gShiftControl = NULL;
static ControlHandle  gGrainSizeControl = NULL;
static ControlHandle  gGrainRateControl = NULL;
static ControlHandle  gGainControl = NULL;
static ControlHandle  gPitchControl = NULL;
static ControlHandle  gFineTuneControl = NULL;

static Str255         gActivity = "\p";
static Str255         gLoadedName = "\pNo file loaded";

static short          gWaveMin[kWavePoints];
static short          gWaveMax[kWavePoints];
static Boolean        gWaveformReady = false;
static Boolean        gWaveBuildActive = false;
static long           gWaveBuildNext = 0;

static FSSpec         gLoadedSpec;
static Boolean        gHasLoadedFile = false;
static Boolean        gLoadPending = false;
static FSSpec         gPendingLoadSpec;
static Boolean        gInSliderTrack = false;
static Boolean        gNeedsFullRedraw = false;

static StereoSampleBuffer gSample = { NULL, 0, 0, false };

static long           gLoopStartNorm = 0;
static long           gLoopEndNorm = kSliderMax;
static long           gGrainSizeNorm = 558;  /* 112 ms */
static long           gGrainRateNorm = 126;  /* 26 ms */
static long           gGainNorm = 500;       /* 100% */
static long           gPitchNorm = 500;      /* 0 semitones */
static long           gFineTuneNorm = 500;   /* 0 cents */

static volatile Boolean gEnginePlaying = false;
static volatile Boolean gEngineRunning = false;
static volatile long    gLoopStartFrame = 0;
static volatile long    gLoopEndFrame = 1;
static volatile long    gGrainSizeFrames = 441;
static volatile long    gGrainRateFrames = 441;
static volatile long    gFramesUntilNextGrain = 0;
static volatile long    gOutputGain256 = 256;
static volatile unsigned long gPitchStep16 = 65536UL;
static volatile long    gEffectiveGrainRateFrames = 441;
static Boolean          gSafetyClampActive = false;

static GrainVoice      gGrains[kMaxGrains];

static SndChannelPtr   gChannel = NULL;
static SndDoubleBackUPP gDoubleBackUPP = NULL;
static SndDoubleBufferHeader2 gDBHeader;
static SndDoubleBufferPtr gDBufs[kNumDBuffers] = { NULL, NULL };

/* Prototypes */
static void InitToolboxStuff(void);
static void CreateMainWindow(void);
static void CreateSliderControls(void);
static void DrawMainWindow(void);
static void DrawWaveform(void);
static void DrawSliderLabels(void);
static void ClearWaveform(void);
static void StartWaveformBuild(void);
static void RebuildWaveformBucket(long pointIndex);
static void AdvanceWaveformBuild(short maxPoints);
static void PerformPendingLoad(void);
static void ProcessBackgroundTasks(void);
static void ServiceUI(void);
static void DrawActivityOnly(void);
static void ResetPitchControls(void);
static void EventLoop(void);
static void HandleUpdate(EventRecord *event);
static void HandleMouseDown(EventRecord *event);
static void HandleKeyDown(EventRecord *event);
static void HandleControlHit(ControlHandle control);

static void CopyPString(ConstStr255Param src, Str255 dst);
static void AppendPString(Str255 dst, ConstStr255Param src);
static void AppendLong(Str255 dst, long value);
static void SetActivity(ConstStr255Param text);
static void SetLoadedName(ConstStr255Param text);
static void SetActivityError(ConstStr255Param prefix, OSErr err);
static short StringWidthP(ConstStr255Param s);
static void DrawLabelValue(short y, ConstStr255Param label, ConstStr255Param value);
static void FormatLoopStartValue(Str255 out);
static void FormatLoopEndValue(Str255 out);
static void FormatShiftValue(Str255 out);
static void FormatGrainSizeValue(Str255 out);
static void FormatGrainRateValue(Str255 out);
static void FormatGainValue(Str255 out);
static void FormatPitchValue(Str255 out);
static void FormatFineTuneValue(Str255 out);
static Boolean IsSliderControl(ControlHandle control);
static void MaybeNudgeScrollbar(ControlHandle control, ControlPartCode part);
static pascal void SliderActionProc(ControlHandle control, ControlPartCode part);
static void ApplyControlValue(ControlHandle control, long value);
static void RequestFullRedraw(void);
static void CommitSliderChange(ControlHandle control);
static void SliderActionLive(ControlHandle control);

static Boolean ChooseWAVFile(FSSpec *spec);
static OSErr LoadEntireFile(const FSSpec *spec, Ptr *outData, long *outSize);
static unsigned long ReadLE32(const unsigned char *p);
static unsigned short ReadLE16(const unsigned char *p);
static OSErr FindWAVChunks(Ptr fileData, long fileSize, Ptr *outFmtPtr, long *outFmtSize, Ptr *outDataPtr, long *outDataSize);
static OSErr LoadStereoSampleFromWAV(Ptr fileData, long fileSize);
static void DisposeSampleBuffer(void);

static unsigned long RandomBelow(unsigned long limit);

static long ClampLong(long value, long minValue, long maxValue);
static void ResetLoopControls(void);
static long NormToFrame(long normValue);
static long MinLoopFrames(void);
static void GetEffectiveLoopNorms(long *outStartNorm, long *outEndNorm);
static void MoveLoopWindowTo(long newStartNorm);
static void ApplyLoopFramesFromControls(void);
static long GrainSizeMSFromNorm(long normValue);
static long GrainRateMSFromNorm(long normValue);
static long GainPercentFromNorm(long normValue);
static long PitchSemitonesFromNorm(long normValue);
static long FineTuneCentsFromNorm(long normValue);
static void ApplyGrainControls(void);
static void ResetPitchControls(void);

static OSErr InitAudioEngine(void);
static void DisposeAudioEngine(void);
static OSErr StartAudioEngine(void);
static void StopAudioEngine(void);
static void PrimeAndStartDoubleBuffer(void);
static void BuildDoubleBufferHeader(void);
static OSErr AllocateDoubleBuffers(void);
static void DisposeDoubleBuffers(void);
static void FillDoubleBuffer(SndDoubleBufferPtr dbuf);
static void FillOutputFrames(short *dst, long frameCount);
static void ResetGrainVoices(void);
static short ActiveGrainCount(void);
static long NextGrainIntervalFrames(void);
static Boolean SpawnVoice(short outputMode);
static void SpawnGrainPair(void);
static short ApplyEnvelopeToSample(short sample, long offsetFrames, long durationFrames);
static pascal void GranularDoubleBackProc(SndChannelPtr chan, SndDoubleBufferPtr doubleBuffer);

static void InitToolboxStuff(void)
{
    MaxApplZone();
    MoreMasters();
    MoreMasters();
    MoreMasters();

    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);
    InitCursor();
}

static void CopyPString(ConstStr255Param src, Str255 dst)
{
    BlockMoveData(src, dst, src[0] + 1);
}

static void AppendPString(Str255 dst, ConstStr255Param src)
{
    short dstLen = dst[0];
    short srcLen = src[0];
    short room = 255 - dstLen;

    if (srcLen > room) srcLen = room;
    if (srcLen > 0) {
        BlockMoveData(src + 1, dst + dstLen + 1, srcLen);
        dst[0] = (unsigned char)(dstLen + srcLen);
    }
}

static void AppendLong(Str255 dst, long value)
{
    Str255 num;
    NumToString(value, num);
    AppendPString(dst, num);
}

static void SetActivity(ConstStr255Param text)
{
    CopyPString(text, gActivity);
    if (gWindow != NULL) {
        SetPort(gWindow);
        InvalRect(&gWaveRect);
    }
}

static void DrawActivityOnly(void)
{
    if (gWindow == NULL) return;
    SetPort(gWindow);
    DrawWaveform();
}

static void ServiceUI(void)
{
    EventRecord event;

    DrawActivityOnly();
    while (WaitNextEvent(everyEvent, &event, 0, NULL)) {
        if (event.what == updateEvt && (WindowPtr)event.message == gWindow) {
            HandleUpdate(&event);
        }
    }
}

static short StringWidthP(ConstStr255Param s)
{
    return TextWidth(s + 1, 0, s[0]);
}

static void DrawLabelValue(short y, ConstStr255Param label, ConstStr255Param value)
{
    short x;

    MoveTo(20, y);
    DrawString(label);
    x = (short)(20 + StringWidthP(label) + 8);
    MoveTo(x, y);
    DrawString(value);
}

static void SetLoadedName(ConstStr255Param text)
{
    CopyPString(text, gLoadedName);
    if (gWindow != NULL) {
        SetPort(gWindow);
        InvalRect(&gWindow->portRect);
    }
}

static void SetActivityError(ConstStr255Param prefix, OSErr err)
{
    Str255 msg;
    CopyPString(prefix, msg);
    AppendLong(msg, (long)err);
    SetActivity(msg);
}

static long ClampLong(long value, long minValue, long maxValue)
{
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}

static Boolean ChooseWAVFile(FSSpec *spec)
{
    StandardFileReply reply;
    StandardGetFile(NULL, 0, NULL, &reply);
    if (!reply.sfGood) return false;
    *spec = reply.sfFile;
    return true;
}

static OSErr LoadEntireFile(const FSSpec *spec, Ptr *outData, long *outSize)
{
    OSErr err;
    short refNum;
    long fileSize;
    long bytesToRead;
    Ptr buffer;

    *outData = NULL;
    *outSize = 0;

    err = FSpOpenDF(spec, fsRdPerm, &refNum);
    if (err != noErr) return err;

    err = GetEOF(refNum, &fileSize);
    if (err != noErr) {
        FSClose(refNum);
        return err;
    }

    if (fileSize <= 0) {
        FSClose(refNum);
        return paramErr;
    }

    buffer = NewPtr(fileSize);
    if (buffer == NULL) {
        FSClose(refNum);
        return memFullErr;
    }

    bytesToRead = fileSize;
    err = FSRead(refNum, &bytesToRead, buffer);
    FSClose(refNum);
    if (err != noErr && err != eofErr) {
        DisposePtr(buffer);
        return err;
    }

    *outData = buffer;
    *outSize = bytesToRead;
    return noErr;
}

static unsigned long ReadLE32(const unsigned char *p)
{
    return ((unsigned long)p[0]) |
           (((unsigned long)p[1]) << 8) |
           (((unsigned long)p[2]) << 16) |
           (((unsigned long)p[3]) << 24);
}

static unsigned short ReadLE16(const unsigned char *p)
{
    return (unsigned short)(((unsigned short)p[0]) | (((unsigned short)p[1]) << 8));
}

static OSErr FindWAVChunks(Ptr fileData, long fileSize, Ptr *outFmtPtr, long *outFmtSize, Ptr *outDataPtr, long *outDataSize)
{
    unsigned char *p;
    long pos;

    *outFmtPtr = NULL;
    *outFmtSize = 0;
    *outDataPtr = NULL;
    *outDataSize = 0;

    if (fileSize < 12) return paramErr;

    p = (unsigned char *)fileData;
    if (!(p[0] == 'R' && p[1] == 'I' && p[2] == 'F' && p[3] == 'F')) return paramErr;
    if (!(p[8] == 'W' && p[9] == 'A' && p[10] == 'V' && p[11] == 'E')) return paramErr;

    pos = 12;
    while (pos + 8 <= fileSize) {
        unsigned char *chunk = p + pos;
        unsigned long chunkSize = ReadLE32(chunk + 4);
        long nextPos = pos + 8 + (long)chunkSize;
        if (chunkSize & 1) nextPos++;
        if (nextPos > fileSize) break;

        if (chunk[0] == 'f' && chunk[1] == 'm' && chunk[2] == 't' && chunk[3] == ' ') {
            long avail = fileSize - (pos + 8);
            long useSize = (long)chunkSize;
            if (useSize > avail) useSize = avail;
            *outFmtPtr = (Ptr)(chunk + 8);
            *outFmtSize = useSize;
        } else if (chunk[0] == 'd' && chunk[1] == 'a' && chunk[2] == 't' && chunk[3] == 'a') {
            long avail = fileSize - (pos + 8);
            long useSize = (long)chunkSize;
            if (useSize > avail) useSize = avail;
            *outDataPtr = (Ptr)(chunk + 8);
            *outDataSize = useSize;
        }

        pos = nextPos;
    }

    if (*outFmtPtr == NULL || *outDataPtr == NULL) return paramErr;
    return noErr;
}


static unsigned long RandomBelow(unsigned long limit)
{
    static unsigned long sRandState = 0x13579BDFUL;
    unsigned long r;

    if (limit == 0UL) return 0UL;

    sRandState = sRandState * 1664525UL + 1013904223UL;
    r = sRandState;

    return r % limit;
}

static void DisposeSampleBuffer(void)
{
    if (gSample.samples != NULL) {
        DisposePtr((Ptr)gSample.samples);
        gSample.samples = NULL;
    }
    gSample.frameCount = 0;
    gSample.sampleRate = 0;
    gSample.loaded = false;
}

static OSErr LoadStereoSampleFromWAV(Ptr fileData, long fileSize)
{
    OSErr err;
    Ptr fmtPtr;
    long fmtSize;
    Ptr dataPtr;
    long dataSize;
    unsigned char *fmt;
    unsigned char *src;
    unsigned short audioFormat;
    unsigned short numChannels;
    unsigned long sampleRate;
    unsigned short bitsPerSample;
    long frameCount;
    short *dst;
    long i;

    err = FindWAVChunks(fileData, fileSize, &fmtPtr, &fmtSize, &dataPtr, &dataSize);
    if (err != noErr) return err;
    if (fmtSize < 16) return paramErr;

    fmt = (unsigned char *)fmtPtr;
    src = (unsigned char *)dataPtr;
    audioFormat = ReadLE16(fmt + 0);
    numChannels = ReadLE16(fmt + 2);
    sampleRate = ReadLE32(fmt + 4);
    bitsPerSample = ReadLE16(fmt + 14);

    if (audioFormat != 1) return paramErr;
    if (!(numChannels == 1 || numChannels == 2)) return paramErr;
    if (bitsPerSample != 16) return paramErr;
    if (sampleRate < 8000UL || sampleRate > 48000UL) return paramErr;

    {
        long bytesPerFrame = (long)numChannels * 2L;
        long maxFramesFromData = dataSize / bytesPerFrame;
        long srcBytes = fileSize - (long)(src - (unsigned char *)fileData);
        long maxFramesFromFile = srcBytes / bytesPerFrame;

        frameCount = maxFramesFromData;
        if (maxFramesFromFile < frameCount) frameCount = maxFramesFromFile;
    }
    if (frameCount <= 0) return paramErr;

    DisposeSampleBuffer();
    dst = (short *)NewPtr(frameCount * 2L * sizeof(short));
    if (dst == NULL) return memFullErr;

    if (numChannels == 2) {
        for (i = 0; i < frameCount; i++) {
            unsigned short llo = src[i * 4 + 0];
            unsigned short lhi = src[i * 4 + 1];
            unsigned short rlo = src[i * 4 + 2];
            unsigned short rhi = src[i * 4 + 3];
            dst[i * 2 + 0] = (short)((lhi << 8) | llo);
            dst[i * 2 + 1] = (short)((rhi << 8) | rlo);
            if ((i & (kDecodePumpInterval - 1L)) == 0L) ServiceUI();
        }
    } else {
        for (i = 0; i < frameCount; i++) {
            unsigned short lo = src[i * 2 + 0];
            unsigned short hi = src[i * 2 + 1];
            short s = (short)((hi << 8) | lo);
            dst[i * 2 + 0] = s;
            dst[i * 2 + 1] = s;
            if ((i & (kDecodePumpInterval - 1L)) == 0L) ServiceUI();
        }
    }

    gSample.samples = dst;
    gSample.frameCount = frameCount;
    gSample.sampleRate = sampleRate;
    gSample.loaded = true;
    return noErr;
}

static long MinLoopFrames(void)
{
    if (!gSample.loaded || gSample.sampleRate == 0) return 44L;
    return ClampLong((long)(gSample.sampleRate / 1000UL), 1L, gSample.frameCount > 1 ? gSample.frameCount - 1 : 1L);
}

static long NormToFrame(long normValue)
{
    long maxFrame;
    if (!gSample.loaded || gSample.frameCount <= 1) return 0;
    maxFrame = gSample.frameCount - 1;
    return (normValue * maxFrame) / kSliderMax;
}

static void ResetLoopControls(void)
{
    gLoopStartNorm = 0;
    gLoopEndNorm = kSliderMax;
    if (gStartControl != NULL) SetControlValue(gStartControl, (short)gLoopStartNorm);
    if (gEndControl != NULL) SetControlValue(gEndControl, (short)gLoopEndNorm);
    if (gShiftControl != NULL) SetControlValue(gShiftControl, (short)gLoopStartNorm);
}

static void GetEffectiveLoopNorms(long *outStartNorm, long *outEndNorm)
{
    long startNorm = gLoopStartNorm;
    long endNorm = gLoopEndNorm;
    long minNorm = 1L;

    if (gSample.loaded && gSample.frameCount > 1) {
        minNorm = (MinLoopFrames() * kSliderMax) / (gSample.frameCount - 1);
        if (minNorm < 1L) minNorm = 1L;
    }

    if (endNorm < startNorm + minNorm) endNorm = startNorm + minNorm;
    if (endNorm > kSliderMax) {
        endNorm = kSliderMax;
        startNorm = endNorm - minNorm;
        if (startNorm < 0) startNorm = 0;
    }

    *outStartNorm = startNorm;
    *outEndNorm = endNorm;
}

static void MoveLoopWindowTo(long newStartNorm)
{
    long startNorm, endNorm, widthNorm;
    GetEffectiveLoopNorms(&startNorm, &endNorm);
    widthNorm = endNorm - startNorm;
    if (widthNorm < 1) widthNorm = 1;

    if (newStartNorm < 0) newStartNorm = 0;
    if (newStartNorm > kSliderMax - widthNorm) newStartNorm = kSliderMax - widthNorm;

    gLoopStartNorm = newStartNorm;
    gLoopEndNorm = newStartNorm + widthNorm;
    if (gLoopEndNorm > kSliderMax) gLoopEndNorm = kSliderMax;

    if (gStartControl != NULL) SetControlValue(gStartControl, (short)gLoopStartNorm);
    if (gEndControl != NULL) SetControlValue(gEndControl, (short)gLoopEndNorm);
    if (gShiftControl != NULL) SetControlValue(gShiftControl, (short)gLoopStartNorm);
}

static void RequestFullRedraw(void)
{
    if (gInSliderTrack) {
        gNeedsFullRedraw = true;
    } else if (gWindow != NULL) {
        SetPort(gWindow);
        InvalRect(&gWindow->portRect);
    }
}

static void UpdateLoopFramesOnly(void)
{
    long startNorm, endNorm;
    long startFrame, endFrame;
    long minFrames;

    if (!gSample.loaded || gSample.frameCount <= 0) return;

    GetEffectiveLoopNorms(&startNorm, &endNorm);
    startFrame = NormToFrame(startNorm);
    endFrame = NormToFrame(endNorm);
    minFrames = MinLoopFrames();
    if (endFrame < startFrame + minFrames) endFrame = startFrame + minFrames;
    if (endFrame > gSample.frameCount) endFrame = gSample.frameCount;
    if (startFrame < 0) startFrame = 0;
    if (startFrame >= endFrame) startFrame = endFrame - 1;
    if (endFrame <= startFrame) endFrame = startFrame + 1;

    gLoopStartFrame = startFrame;
    gLoopEndFrame = endFrame;
}

static void ApplyLoopFramesFromControls(void)
{
    long startNorm, endNorm;
    long startFrame, endFrame;
    long minFrames;

    GetEffectiveLoopNorms(&startNorm, &endNorm);
    gLoopStartNorm = startNorm;
    gLoopEndNorm = endNorm;

    if (!gInSliderTrack) {
        if (gStartControl != NULL) SetControlValue(gStartControl, (short)gLoopStartNorm);
        if (gEndControl != NULL) SetControlValue(gEndControl, (short)gLoopEndNorm);
        if (gShiftControl != NULL) SetControlValue(gShiftControl, (short)gLoopStartNorm);
    }

    if (!gSample.loaded || gSample.frameCount <= 0) return;

    startFrame = NormToFrame(gLoopStartNorm);
    endFrame = NormToFrame(gLoopEndNorm);
    minFrames = MinLoopFrames();
    if (endFrame < startFrame + minFrames) endFrame = startFrame + minFrames;
    if (endFrame > gSample.frameCount) endFrame = gSample.frameCount;
    if (startFrame < 0) startFrame = 0;
    if (startFrame >= endFrame) startFrame = endFrame - 1;
    if (endFrame <= startFrame) endFrame = startFrame + 1;

    gLoopStartFrame = startFrame;
    gLoopEndFrame = endFrame;

    RequestFullRedraw();
}

static long GrainSizeMSFromNorm(long normValue)
{
    return 1L + (normValue * 199L) / kSliderMax;
}

static long GrainRateMSFromNorm(long normValue)
{
    return 1L + (normValue * 199L) / kSliderMax;
}

static long GainPercentFromNorm(long normValue)
{
    return (normValue * 200L) / kSliderMax;
}

static long PitchSemitonesFromNorm(long normValue)
{
    return -24L + (normValue * 48L) / kSliderMax;
}

static long FineTuneCentsFromNorm(long normValue)
{
    return -100L + (normValue * 200L) / kSliderMax;
}

static void ApplyGrainControls(void)
{
    long grainSizeMS = GrainSizeMSFromNorm(gGrainSizeNorm);
    long grainRateMS = GrainRateMSFromNorm(gGrainRateNorm);
    long sizeFrames;
    long requestedRateFrames;
    long minSafeRateFrames;
    double pitchRatio;
    double semis;

    gSafetyClampActive = false;

    if (!gSample.loaded || gSample.sampleRate == 0) {
        gGrainSizeFrames = 44L;
        gGrainRateFrames = 44L;
        gEffectiveGrainRateFrames = 44L;
        return;
    }

    sizeFrames = (grainSizeMS * (long)gSample.sampleRate) / 1000L;
    requestedRateFrames = (grainRateMS * (long)gSample.sampleRate) / 1000L;
    if (sizeFrames < 1L) sizeFrames = 1L;
    if (requestedRateFrames < 1L) requestedRateFrames = 1L;

    minSafeRateFrames = (sizeFrames * 2L + (kSafeMaxActiveVoices - 1L)) / kSafeMaxActiveVoices;
    if (minSafeRateFrames < 1L) minSafeRateFrames = 1L;

    gGrainSizeFrames = sizeFrames;
    gGrainRateFrames = requestedRateFrames;
    gEffectiveGrainRateFrames = requestedRateFrames;
    if (gEffectiveGrainRateFrames < minSafeRateFrames) {
        gEffectiveGrainRateFrames = minSafeRateFrames;
        gSafetyClampActive = true;
    }

    gOutputGain256 = (GainPercentFromNorm(gGainNorm) * 256L) / 100L;
    if (gOutputGain256 < 0L) gOutputGain256 = 0L;

    semis = (double)PitchSemitonesFromNorm(gPitchNorm) + ((double)FineTuneCentsFromNorm(gFineTuneNorm) / 100.0);
    pitchRatio = pow(2.0, semis / 12.0);
    if (pitchRatio < 0.25) pitchRatio = 0.25;
    if (pitchRatio > 4.0) pitchRatio = 4.0;
    gPitchStep16 = (unsigned long)(pitchRatio * 65536.0 + 0.5);
    if (gPitchStep16 < 1UL) gPitchStep16 = 1UL;
}

static void ResetPitchControls(void)
{
    gPitchNorm = 500;
    gFineTuneNorm = 500;
    if (gPitchControl != NULL) SetControlValue(gPitchControl, (short)gPitchNorm);
    if (gFineTuneControl != NULL) SetControlValue(gFineTuneControl, (short)gFineTuneNorm);
    ApplyGrainControls();
    RequestFullRedraw();
}

static void ResetGrainVoices(void)
{
    short i;
    for (i = 0; i < kMaxGrains; i++) {
        gGrains[i].active = false;
        gGrains[i].startFrame = 0;
        gGrains[i].durationFrames = 0;
        gGrains[i].offsetFrames = 0;
        gGrains[i].phase16 = 0UL;
        gGrains[i].phaseStep16 = 65536UL;
        gGrains[i].outputMode = 1;
    }
    gFramesUntilNextGrain = 0;
}

static short ActiveGrainCount(void)
{
    short i;
    short count = 0;

    for (i = 0; i < kMaxGrains; i++) {
        if (gGrains[i].active) count++;
    }
    return count;
}

static long NextGrainIntervalFrames(void)
{
    long interval = gEffectiveGrainRateFrames;

    if (interval < 1L) interval = 1L;
    return interval;
}

static Boolean SpawnVoice(short outputMode)
{
    short i;
    long loopStart = gLoopStartFrame;
    long loopEnd = gLoopEndFrame;
    long duration = gGrainSizeFrames;
    long maxStart;
    long startFrame;

    if (!gSample.loaded) return false;
    if (loopEnd <= loopStart) return false;

    if (duration < 1L) duration = 1L;
    if (duration > (loopEnd - loopStart)) duration = (loopEnd - loopStart);
    if (duration < 1L) duration = 1L;

    maxStart = loopEnd - duration;
    if (maxStart < loopStart) maxStart = loopStart;

    if (maxStart > loopStart) {
        unsigned long span = (unsigned long)((maxStart - loopStart) + 1L);
        startFrame = loopStart + (long)RandomBelow(span);
    } else {
        startFrame = loopStart;
    }

    for (i = 0; i < kMaxGrains; i++) {
        if (!gGrains[i].active) {
            gGrains[i].active = true;
            gGrains[i].startFrame = startFrame;
            gGrains[i].durationFrames = duration;
            gGrains[i].offsetFrames = 0;
            gGrains[i].phase16 = 0UL;
            gGrains[i].phaseStep16 = gPitchStep16;
            gGrains[i].outputMode = outputMode;
            return true;
        }
    }

    return false;
}

static void SpawnGrainPair(void)
{
    short active = ActiveGrainCount();

    if (active <= (kSafeMaxActiveVoices - 2)) {
        SpawnVoice(1);
        SpawnVoice(2);
    }
}

static short ApplyEnvelopeToSample(short sample, long offsetFrames, long durationFrames)
{
    long x;
    long invX;
    long gain;
    long s;

    if (durationFrames <= 2L) return sample;
    if (offsetFrames < 0L) offsetFrames = 0L;
    if (offsetFrames >= durationFrames) offsetFrames = durationFrames - 1L;

    x = (offsetFrames * 32767L) / (durationFrames - 1L);
    invX = 32767L - x;

    gain = (4L * x * invX) / 32767L;
    if (gain > 32767L) gain = 32767L;
    if (gain < 0L) gain = 0L;

    s = ((long)sample * gain) / 32767L;
    if (s > 32767L) s = 32767L;
    if (s < -32768L) s = -32768L;
    return (short)s;
}

static OSErr AllocateDoubleBuffers(void)
{
    long bytesPerBuffer = sizeof(SndDoubleBuffer) + (kBufferFrames * 2L * 2L);
    short i;

    DisposeDoubleBuffers();
    for (i = 0; i < kNumDBuffers; i++) {
        gDBufs[i] = (SndDoubleBufferPtr)NewPtrClear(bytesPerBuffer);
        if (gDBufs[i] == NULL) {
            DisposeDoubleBuffers();
            return memFullErr;
        }
        gDBufs[i]->dbNumFrames = kBufferFrames;
        gDBufs[i]->dbFlags = 0;
    }
    return noErr;
}

static void DisposeDoubleBuffers(void)
{
    short i;
    for (i = 0; i < kNumDBuffers; i++) {
        if (gDBufs[i] != NULL) {
            DisposePtr((Ptr)gDBufs[i]);
            gDBufs[i] = NULL;
        }
    }
}

static void BuildDoubleBufferHeader(void)
{
    short i;
    memset(&gDBHeader, 0, sizeof(gDBHeader));
    gDBHeader.dbhNumChannels = 2;
    gDBHeader.dbhSampleSize = 16;
    gDBHeader.dbhCompressionID = 0;
    gDBHeader.dbhPacketSize = 0;
    gDBHeader.dbhSampleRate = (UnsignedFixed)(gSample.sampleRate << 16);
    gDBHeader.dbhDoubleBack = gDoubleBackUPP;
    for (i = 0; i < kNumDBuffers; i++) {
        gDBHeader.dbhBufferPtr[i] = gDBufs[i];
    }
}

static void FillOutputFrames(short *dst, long frameCount)
{
    long i;

    if (!gSample.loaded || gSample.samples == NULL || gSample.frameCount < 2) {
        for (i = 0; i < frameCount * 2L; i++) dst[i] = 0;
        return;
    }

    for (i = 0; i < frameCount; i++) {
        long mixL = 0;
        long mixR = 0;
        short g;

        if (gFramesUntilNextGrain <= 0) {
            SpawnGrainPair();
            gFramesUntilNextGrain = NextGrainIntervalFrames();
        }

        for (g = 0; g < kMaxGrains; g++) {
            if (gGrains[g].active) {
                long srcFrame = gGrains[g].startFrame + (long)(gGrains[g].phase16 >> 16);
                if (srcFrame < 0L || srcFrame >= gSample.frameCount ||
                    gGrains[g].offsetFrames >= gGrains[g].durationFrames) {
                    gGrains[g].active = false;
                } else {
                    short srcL = gSample.samples[srcFrame * 2L + 0];
                    short srcR = gSample.samples[srcFrame * 2L + 1];
                    long mono = ((long)srcL + (long)srcR) / 2L;
                    short envSample = ApplyEnvelopeToSample((short)mono, gGrains[g].offsetFrames, gGrains[g].durationFrames);

                    if (gGrains[g].outputMode == 1) {
                        mixL += envSample;
                    } else if (gGrains[g].outputMode == 2) {
                        mixR += envSample;
                    }

                    gGrains[g].offsetFrames++;
                    gGrains[g].phase16 += gGrains[g].phaseStep16;
                    if (gGrains[g].offsetFrames >= gGrains[g].durationFrames) {
                        gGrains[g].active = false;
                    }
                }
            }
        }

        mixL = (mixL * gOutputGain256) / 256L;
        mixR = (mixR * gOutputGain256) / 256L;

        if (mixL > 32767L) mixL = 32767L;
        if (mixL < -32768L) mixL = -32768L;
        if (mixR > 32767L) mixR = 32767L;
        if (mixR < -32768L) mixR = -32768L;

        dst[i * 2L + 0] = (short)mixL;
        dst[i * 2L + 1] = (short)mixR;

        gFramesUntilNextGrain--;
    }
}

static void FillDoubleBuffer(SndDoubleBufferPtr dbuf)
{
    short *dst;
    if (dbuf == NULL) return;
    dst = (short *)dbuf->dbSoundData;
    FillOutputFrames(dst, dbuf->dbNumFrames);
    dbuf->dbFlags = dbBufferReady;
}

static pascal void GranularDoubleBackProc(SndChannelPtr chan, SndDoubleBufferPtr doubleBuffer)
{
    (void)chan;
    if (!gEngineRunning || !gEnginePlaying) {
        if (doubleBuffer != NULL) {
            long i;
            short *dst = (short *)doubleBuffer->dbSoundData;
            for (i = 0; i < doubleBuffer->dbNumFrames * 2L; i++) dst[i] = 0;
            doubleBuffer->dbFlags = dbLastBuffer | dbBufferReady;
        }
        return;
    }

    FillDoubleBuffer(doubleBuffer);
}

static OSErr InitAudioEngine(void)
{
    OSErr err;

    if (gDoubleBackUPP == NULL) {
        gDoubleBackUPP = NewSndDoubleBackProc(GranularDoubleBackProc);
    }
    if (gDoubleBackUPP == NULL) return memFullErr;

    err = SndNewChannel(&gChannel, sampledSynth, initStereo, NULL);
    if (err != noErr) return err;

    err = AllocateDoubleBuffers();
    if (err != noErr) {
        SndDisposeChannel(gChannel, true);
        gChannel = NULL;
        return err;
    }

    gEngineRunning = true;
    gEnginePlaying = false;
    return noErr;
}

static void DisposeAudioEngine(void)
{
    StopAudioEngine();
    DisposeDoubleBuffers();

    if (gChannel != NULL) {
        SndDisposeChannel(gChannel, true);
        gChannel = NULL;
    }

    if (gDoubleBackUPP != NULL) {
        DisposeSndDoubleBackUPP(gDoubleBackUPP);
        gDoubleBackUPP = NULL;
    }

    gEngineRunning = false;
}

static void PrimeAndStartDoubleBuffer(void)
{
    short i;
    BuildDoubleBufferHeader();
    for (i = 0; i < kNumDBuffers; i++) FillDoubleBuffer(gDBufs[i]);
    SndPlayDoubleBuffer(gChannel, (SndDoubleBufferHeaderPtr)&gDBHeader);
}

static OSErr StartAudioEngine(void)
{
    if (!gSample.loaded) return paramErr;
    if (!gEngineRunning) return paramErr;

    ApplyLoopFramesFromControls();
    ApplyGrainControls();
    ResetGrainVoices();
    gEnginePlaying = true;

    PrimeAndStartDoubleBuffer();
    return noErr;
}

static void StopAudioEngine(void)
{
    SndCommand cmd;

    gEnginePlaying = false;
    if (gChannel != NULL) {
        cmd.cmd = quietCmd;
        cmd.param1 = 0;
        cmd.param2 = 0;
        SndDoImmediate(gChannel, &cmd);
        cmd.cmd = flushCmd;
        SndDoImmediate(gChannel, &cmd);
    }
}

static void CreateSliderControls(void)
{
    Rect r;

    SetRect(&r, kSliderLeft, kStartSliderTop, kSliderLeft + kSliderWidth, kStartSliderTop + kSliderHeight);
    gStartControl = NewControl(gWindow, &r, "\p", true, 0, 0, (short)kSliderMax, scrollBarProc, 0L);

    SetRect(&r, kSliderLeft, kEndSliderTop, kSliderLeft + kSliderWidth, kEndSliderTop + kSliderHeight);
    gEndControl = NewControl(gWindow, &r, "\p", true, (short)kSliderMax, 0, (short)kSliderMax, scrollBarProc, 0L);

    SetRect(&r, kSliderLeft, kShiftSliderTop, kSliderLeft + kSliderWidth, kShiftSliderTop + kSliderHeight);
    gShiftControl = NewControl(gWindow, &r, "\p", true, 0, 0, (short)kSliderMax, scrollBarProc, 0L);

    SetRect(&r, kSliderLeft, kGrainSizeSliderTop, kSliderLeft + kSliderWidth, kGrainSizeSliderTop + kSliderHeight);
    gGrainSizeControl = NewControl(gWindow, &r, "\p", true, (short)gGrainSizeNorm, 0, (short)kSliderMax, scrollBarProc, 0L);

    SetRect(&r, kSliderLeft, kGrainRateSliderTop, kSliderLeft + kSliderWidth, kGrainRateSliderTop + kSliderHeight);
    gGrainRateControl = NewControl(gWindow, &r, "\p", true, (short)gGrainRateNorm, 0, (short)kSliderMax, scrollBarProc, 0L);

    SetRect(&r, kSliderLeft, kGainSliderTop, kSliderLeft + kSliderWidth, kGainSliderTop + kSliderHeight);
    gGainControl = NewControl(gWindow, &r, "\p", true, (short)gGainNorm, 0, (short)kSliderMax, scrollBarProc, 0L);

    SetRect(&r, kSliderLeft, kPitchSliderTop, kSliderLeft + kSliderWidth, kPitchSliderTop + kSliderHeight);
    gPitchControl = NewControl(gWindow, &r, "\p", true, (short)gPitchNorm, 0, (short)kSliderMax, scrollBarProc, 0L);

    SetRect(&r, kSliderLeft, kFineSliderTop, kSliderLeft + kSliderWidth, kFineSliderTop + kSliderHeight);
    gFineTuneControl = NewControl(gWindow, &r, "\p", true, (short)gFineTuneNorm, 0, (short)kSliderMax, scrollBarProc, 0L);
}

static void CreateMainWindow(void)
{
    Rect r;

    SetRect(&r, 60, 40, 60 + kWindowWidth, 40 + kWindowHeight);
    gWindow = NewWindow(NULL, &r, "\pGranular Engine V2h", true, documentProc, (WindowPtr)-1L, true, 0);

    SetRect(&gLoadRect, kLoadLeft, kLoadTop, kLoadLeft + kLoadWidth, kLoadTop + kLoadHeight);
    SetRect(&gPlayRect, kPlayLeft, kPlayTop, kPlayLeft + kPlayWidth, kPlayTop + kPlayHeight);
    SetRect(&gStopRect, kStopLeft, kStopTop, kStopLeft + kStopWidth, kStopTop + kStopHeight);
    SetRect(&gResetPitchRect, kResetPitchLeft, kResetPitchTop, kResetPitchLeft + kResetPitchWidth, kResetPitchTop + kResetPitchHeight);
    SetRect(&gQuitRect, kQuitLeft, kQuitTop, kQuitLeft + kQuitWidth, kQuitTop + kQuitHeight);
    SetRect(&gWaveRect, kWaveLeft, kWaveTop, kWaveLeft + kWaveWidth, kWaveTop + kWaveHeight);

    gLoadButton = NewControl(gWindow, &gLoadRect, "\pLoad WAV", true, 0, 0, 1, pushButProc, 0L);
    gPlayButton = NewControl(gWindow, &gPlayRect, "\pPlay", true, 0, 0, 1, pushButProc, 0L);
    gStopButton = NewControl(gWindow, &gStopRect, "\pStop", true, 0, 0, 1, pushButProc, 0L);
    gResetPitchButton = NewControl(gWindow, &gResetPitchRect, "\pReset Pitch", true, 0, 0, 1, pushButProc, 0L);
    gQuitButton = NewControl(gWindow, &gQuitRect, "\pQuit", true, 0, 0, 1, pushButProc, 0L);

    if (gSliderActionUPP == NULL) {
        gSliderActionUPP = NewControlActionProc(SliderActionProc);
    }

    CreateSliderControls();
    ClearWaveform();

    if (gWindow != NULL) {
        SetPort(gWindow);
        DrawMainWindow();
    }
}

static void ClearWaveform(void)
{
    short i;
    for (i = 0; i < kWavePoints; i++) {
        gWaveMin[i] = 0;
        gWaveMax[i] = 0;
    }
    gWaveformReady = false;
    gWaveBuildActive = false;
    gWaveBuildNext = 0;
}

static void RebuildWaveformBucket(long pointIndex)
{
    long frameCount;
    long start;
    long end;
    long span;
    long mn;
    long mx;

    if (pointIndex < 0 || pointIndex >= kWavePoints) return;
    if (!gSample.loaded || gSample.samples == NULL || gSample.frameCount <= 0) {
        gWaveMin[pointIndex] = 0;
        gWaveMax[pointIndex] = 0;
        return;
    }

    frameCount = gSample.frameCount;
    start = (pointIndex * frameCount) / kWavePoints;
    end = ((pointIndex + 1L) * frameCount) / kWavePoints;
    if (end <= start) end = start + 1L;
    if (end > frameCount) end = frameCount;

    span = end - start;
    mn = 32767L;
    mx = -32768L;

    if (span > 0L) {
        long sub;
        for (sub = 0; sub < kWaveSubBuckets; sub++) {
            long subStart = start + (sub * span) / kWaveSubBuckets;
            long subEnd = start + ((sub + 1L) * span) / kWaveSubBuckets;
            long f;
            long step;

            if (subEnd <= subStart) subEnd = subStart + 1L;
            if (subEnd > end) subEnd = end;

            step = (subEnd - subStart) / 64L;
            if (step < 1L) step = 1L;

            for (f = subStart; f < subEnd; f += step) {
                long s = ((long)gSample.samples[f * 2L + 0] + (long)gSample.samples[f * 2L + 1]) / 2L;
                if (s < mn) mn = s;
                if (s > mx) mx = s;
            }
            if (subEnd > subStart) {
                long last = subEnd - 1L;
                long s = ((long)gSample.samples[last * 2L + 0] + (long)gSample.samples[last * 2L + 1]) / 2L;
                if (s < mn) mn = s;
                if (s > mx) mx = s;
            }
        }
    }

    if (mn > mx) {
        mn = 0L;
        mx = 0L;
    }

    gWaveMin[pointIndex] = (short)mn;
    gWaveMax[pointIndex] = (short)mx;
}

static void StartWaveformBuild(void)
{
    ClearWaveform();
    if (!gSample.loaded || gSample.samples == NULL || gSample.frameCount <= 0) return;
    gWaveBuildActive = true;
    gWaveBuildNext = 0;
    SetActivity("\pBuilding waveform...");
}

static void AdvanceWaveformBuild(short maxPoints)
{
    short built = 0;

    if (!gWaveBuildActive) return;

    while (gWaveBuildNext < kWavePoints && built < maxPoints) {
        RebuildWaveformBucket(gWaveBuildNext);
        gWaveBuildNext++;
        built++;
    }

    if (gWaveBuildNext >= kWavePoints) {
        gWaveBuildActive = false;
        gWaveformReady = true;
        SetActivity("\p");
        if (gWindow != NULL) {
            SetPort(gWindow);
            InvalRect(&gWaveRect);
        }
    } else if (gWindow != NULL) {
        SetPort(gWindow);
        InvalRect(&gWaveRect);
    }
}

static void PerformPendingLoad(void)
{
    Ptr fileData = NULL;
    long fileSize = 0;
    OSErr err;

    ClearWaveform();
    SetActivity("\pReading file...");
    ServiceUI();

    err = LoadEntireFile(&gPendingLoadSpec, &fileData, &fileSize);
    if (err == noErr) {
        SetActivity("\pDecoding WAV...");
        ServiceUI();
        err = LoadStereoSampleFromWAV(fileData, fileSize);
        DisposePtr(fileData);
        fileData = NULL;
    }

    if (err != noErr) {
        if (fileData != NULL) DisposePtr(fileData);
        if (err == memFullErr) SetActivity("\pNot enough memory");
        else SetActivityError("\pLoad error ", err);
        SysBeep(10);
        ServiceUI();
        return;
    }

    gLoadedSpec = gPendingLoadSpec;
    gHasLoadedFile = true;
    SetLoadedName(gPendingLoadSpec.name);
    ResetLoopControls();
    ApplyLoopFramesFromControls();
    ApplyGrainControls();
    StartWaveformBuild();
    ServiceUI();

    if (gWindow != NULL) {
        SetPort(gWindow);
        InvalRect(&gWindow->portRect);
    }
}

static void ProcessBackgroundTasks(void)
{
    if (gLoadPending) {
        gLoadPending = false;
        PerformPendingLoad();
    }
    if (gWaveBuildActive) {
        AdvanceWaveformBuild(6);
    }
}

static void DrawWaveform(void)
{
    Rect r = gWaveRect;
    int w;
    int mid;
    int i;
    long startNorm;
    long endNorm;
    int xStart;
    int xEnd;

    EraseRect(&r);
    FrameRect(&r);

    if (!gWaveformReady) {
        MoveTo(r.left + 8, (r.top + r.bottom) / 2);
        if (gActivity[0] != 0) DrawString(gActivity);
        else DrawString("\pLoad a WAV to see waveform");
        return;
    }

    w = r.right - r.left - 2;
    mid = (r.top + r.bottom) / 2;
    for (i = 0; i < kWavePoints; i++) {
        int x = r.left + 1 + (i * w) / kWavePoints;
        int y1 = mid - ((int)gWaveMax[i] * ((r.bottom - r.top - 6) / 2)) / 32768;
        int y2 = mid - ((int)gWaveMin[i] * ((r.bottom - r.top - 6) / 2)) / 32768;
        MoveTo(x, y1);
        LineTo(x, y2);
    }

    GetEffectiveLoopNorms(&startNorm, &endNorm);
    xStart = r.left + 1 + (int)((startNorm * w) / kSliderMax);
    xEnd = r.left + 1 + (int)((endNorm * w) / kSliderMax);
    if (xEnd < xStart) {
        int t = xStart;
        xStart = xEnd;
        xEnd = t;
    }
    PenSize(1, 1);
    {
        RGBColor loopGray = { 20000, 20000, 20000 };
        RGBColor savedFore;

        GetForeColor(&savedFore);
        RGBForeColor(&loopGray);
        MoveTo(xStart, r.top + 1);
        LineTo(xStart, r.bottom - 1);
        MoveTo(xEnd, r.top + 1);
        LineTo(xEnd, r.bottom - 1);
        RGBForeColor(&savedFore);
    }
}

static void FormatLoopStartValue(Str255 out)
{
    long startNorm, endNorm;
    long startMS = 0;

    GetEffectiveLoopNorms(&startNorm, &endNorm);
    if (gSample.loaded && gSample.sampleRate > 0) {
        startMS = (NormToFrame(startNorm) * 1000L) / (long)gSample.sampleRate;
    }
    CopyPString("\p", out);
    AppendLong(out, startMS);
    AppendPString(out, "\p ms");
}

static void FormatLoopEndValue(Str255 out)
{
    long startNorm, endNorm;
    long endMS = 0;

    GetEffectiveLoopNorms(&startNorm, &endNorm);
    if (gSample.loaded && gSample.sampleRate > 0) {
        endMS = (NormToFrame(endNorm) * 1000L) / (long)gSample.sampleRate;
    }
    CopyPString("\p", out);
    AppendLong(out, endMS);
    AppendPString(out, "\p ms");
}

static void FormatShiftValue(Str255 out)
{
    FormatLoopStartValue(out);
}

static void FormatGrainSizeValue(Str255 out)
{
    CopyPString("\p", out);
    AppendLong(out, GrainSizeMSFromNorm(gGrainSizeNorm));
    AppendPString(out, "\p ms");
}

static void FormatGrainRateValue(Str255 out)
{
    long rateMS = GrainRateMSFromNorm(gGrainRateNorm);

    CopyPString("\p", out);
    AppendLong(out, rateMS);
    AppendPString(out, "\p ms");
    if (gSafetyClampActive) {
        long effectiveMS = rateMS;
        if (gSample.loaded && gSample.sampleRate > 0) {
            effectiveMS = (gEffectiveGrainRateFrames * 1000L) / (long)gSample.sampleRate;
            if (effectiveMS < 1L) effectiveMS = 1L;
        }
        AppendPString(out, "\p (safe ");
        AppendLong(out, effectiveMS);
        AppendPString(out, "\p)");
    }
}

static void FormatGainValue(Str255 out)
{
    CopyPString("\p", out);
    AppendLong(out, GainPercentFromNorm(gGainNorm));
    AppendPString(out, "\p%");
}

static void FormatPitchValue(Str255 out)
{
    CopyPString("\p", out);
    AppendLong(out, PitchSemitonesFromNorm(gPitchNorm));
    AppendPString(out, "\p st");
}

static void FormatFineTuneValue(Str255 out)
{
    CopyPString("\p", out);
    AppendLong(out, FineTuneCentsFromNorm(gFineTuneNorm));
    AppendPString(out, "\p cents");
}

static void DrawSliderLabels(void)
{
    Str255 value;

    FormatLoopStartValue(value);
    DrawLabelValue(kStartSliderTop - 6, "\pWindow Start", value);

    FormatLoopEndValue(value);
    DrawLabelValue(kEndSliderTop - 6, "\pWindow End", value);

    FormatShiftValue(value);
    DrawLabelValue(kShiftSliderTop - 6, "\pShift Window", value);

    FormatGrainSizeValue(value);
    DrawLabelValue(kGrainSizeSliderTop - 6, "\pGrain Size", value);

    FormatGrainRateValue(value);
    DrawLabelValue(kGrainRateSliderTop - 6, "\pGrain Rate", value);

    FormatGainValue(value);
    DrawLabelValue(kGainSliderTop - 6, "\pOutput Gain", value);

    FormatPitchValue(value);
    DrawLabelValue(kPitchSliderTop - 6, "\pPitch", value);

    FormatFineTuneValue(value);
    DrawLabelValue(kFineSliderTop - 6, "\pFine Tune", value);
}

static Boolean IsSliderControl(ControlHandle control)
{
    return (control == gStartControl || control == gEndControl || control == gShiftControl ||
            control == gGrainSizeControl || control == gGrainRateControl || control == gGainControl ||
            control == gPitchControl || control == gFineTuneControl);
}

static void MaybeNudgeScrollbar(ControlHandle control, ControlPartCode initialPart)
{
    short v0;
    short v1;
    short delta;

    if (control == NULL) return;
    if (initialPart != inUpButton && initialPart != inDownButton &&
        initialPart != inPageUp && initialPart != inPageDown) return;

    v0 = GetControlValue(control);
    v1 = v0;

    if (initialPart == inUpButton && v0 > 0) v1 = (short)(v0 - 1);
    else if (initialPart == inDownButton && v0 < (short)kSliderMax) v1 = (short)(v0 + 1);
    else if (initialPart == inPageUp) {
        delta = (short)(kSliderMax / 20L);
        if (delta < 1) delta = 1;
        v1 = (short)(v0 - delta);
        if (v1 < 0) v1 = 0;
    } else if (initialPart == inPageDown) {
        delta = (short)(kSliderMax / 20L);
        if (delta < 1) delta = 1;
        v1 = (short)(v0 + delta);
        if (v1 > (short)kSliderMax) v1 = (short)kSliderMax;
    }

    if (v1 != v0) {
        SetControlValue(control, v1);
        Draw1Control(control);
        ApplyControlValue(control, (long)v1);
    }
}

static void SliderActionLive(ControlHandle control)
{
    long value;

    if (control == NULL) return;
    value = (long)GetControlValue(control);

    if (control == gStartControl) {
        gLoopStartNorm = value;
        UpdateLoopFramesOnly();
    } else if (control == gEndControl) {
        gLoopEndNorm = value;
        UpdateLoopFramesOnly();
    } else if (control == gShiftControl) {
        gLoopStartNorm = value;
        UpdateLoopFramesOnly();
    } else if (control == gGrainSizeControl) {
        gGrainSizeNorm = value;
        ApplyGrainControls();
    } else if (control == gGrainRateControl) {
        gGrainRateNorm = value;
        ApplyGrainControls();
    } else if (control == gGainControl) {
        gGainNorm = value;
        ApplyGrainControls();
    } else if (control == gPitchControl) {
        gPitchNorm = value;
        ApplyGrainControls();
    } else if (control == gFineTuneControl) {
        gFineTuneNorm = value;
        ApplyGrainControls();
    }
}

static void ApplyControlValue(ControlHandle control, long value)
{
    if (control == gStartControl) {
        gLoopStartNorm = value;
        ApplyLoopFramesFromControls();
    } else if (control == gEndControl) {
        gLoopEndNorm = value;
        ApplyLoopFramesFromControls();
    } else if (control == gShiftControl) {
        MoveLoopWindowTo(value);
        ApplyLoopFramesFromControls();
    } else if (control == gGrainSizeControl) {
        gGrainSizeNorm = value;
        ApplyGrainControls();
        RequestFullRedraw();
    } else if (control == gGrainRateControl) {
        gGrainRateNorm = value;
        ApplyGrainControls();
        RequestFullRedraw();
    } else if (control == gGainControl) {
        gGainNorm = value;
        ApplyGrainControls();
        RequestFullRedraw();
    } else if (control == gPitchControl) {
        gPitchNorm = value;
        ApplyGrainControls();
        RequestFullRedraw();
    } else if (control == gFineTuneControl) {
        gFineTuneNorm = value;
        ApplyGrainControls();
        RequestFullRedraw();
    }
}

static void CommitSliderChange(ControlHandle control)
{
    if (control == NULL) return;
    ApplyControlValue(control, (long)GetControlValue(control));
    if (gNeedsFullRedraw && gWindow != NULL) {
        gNeedsFullRedraw = false;
        SetPort(gWindow);
        InvalRect(&gWindow->portRect);
    }
}

static pascal void SliderActionProc(ControlHandle control, ControlPartCode part)
{
    (void)part;
    SliderActionLive(control);
}

static void DrawMainWindow(void)
{
    if (gWindow == NULL) return;

    SetPort(gWindow);
    EraseRect(&gWindow->portRect);

    MoveTo(20, 20);
    DrawString("\pGranular Engine V2h");

    MoveTo(20, 38);
    DrawString("\pLoaded:");
    MoveTo(80, 38);
    DrawString(gLoadedName);

    Draw1Control(gLoadButton);
    Draw1Control(gPlayButton);
    Draw1Control(gStopButton);
    Draw1Control(gResetPitchButton);
    Draw1Control(gQuitButton);

    DrawWaveform();

    DrawSliderLabels();

    Draw1Control(gStartControl);
    Draw1Control(gEndControl);
    Draw1Control(gShiftControl);
    Draw1Control(gGrainSizeControl);
    Draw1Control(gGrainRateControl);
    Draw1Control(gGainControl);
    Draw1Control(gPitchControl);
    Draw1Control(gFineTuneControl);
}

static void HandleControlHit(ControlHandle control)
{
    long value;

    if (control == NULL) return;

    if (control == gLoadButton) {
        FSSpec spec;

        if (ChooseWAVFile(&spec)) {
            gPendingLoadSpec = spec;
            gLoadPending = true;
            ClearWaveform();
            SetActivity("\pLoading...");
            if (gWindow != NULL) {
                SetPort(gWindow);
                InvalRect(&gWaveRect);
                DrawActivityOnly();
            }
        }
        return;
    } else if (control == gPlayButton) {
        OSErr err = StartAudioEngine();
        if (err != noErr) {
            SetActivityError("\pPlay error ", err);
            SysBeep(10);
        }
        return;
    } else if (control == gStopButton) {
        StopAudioEngine();
        return;
    } else if (control == gResetPitchButton) {
        ResetPitchControls();
        return;
    } else if (control == gQuitButton) {
        gDone = true;
        return;
    }

    value = GetControlValue(control);
    ApplyControlValue(control, value);
}

static void HandleMouseDown(EventRecord *event)
{
    WindowPtr whichWindow;
    short part;
    Point localPt;
    ControlHandle control;
    short controlPart;

    part = FindWindow(event->where, &whichWindow);
    switch (part) {
        case inDrag:
            DragWindow(whichWindow, event->where, &qd.screenBits.bounds);
            break;
        case inGoAway:
            if (TrackGoAway(whichWindow, event->where)) gDone = true;
            break;
        case inContent:
            if (whichWindow != FrontWindow()) {
                SelectWindow(whichWindow);
                break;
            }

            localPt = event->where;
            GlobalToLocal(&localPt);

            controlPart = FindControl(localPt, whichWindow, &control);
            if (controlPart != 0 && control != NULL) {
                if (IsSliderControl(control)) {
                    short before = GetControlValue(control);

                    gInSliderTrack = true;
                    gNeedsFullRedraw = false;
                    TrackControl(control, localPt, gSliderActionUPP);
                    gInSliderTrack = false;

                    if (GetControlValue(control) == before) {
                        MaybeNudgeScrollbar(control, (ControlPartCode)controlPart);
                    }
                    CommitSliderChange(control);
                } else {
                    ControlPartCode donePart = TrackControl(control, localPt, NULL);
                    if (donePart != 0) HandleControlHit(control);
                }
            }
            break;
        default:
            break;
    }
}

static void HandleKeyDown(EventRecord *event)
{
    char c = (char)(event->message & charCodeMask);
    if (c == 'q' || c == 'Q') {
        gDone = true;
    } else if (c == ' ') {
        StartAudioEngine();
    } else if (c == 's' || c == 'S') {
        StopAudioEngine();
    }
}

static void HandleUpdate(EventRecord *event)
{
    BeginUpdate((WindowPtr)event->message);
    DrawMainWindow();
    EndUpdate((WindowPtr)event->message);
}

static void EventLoop(void)
{
    EventRecord event;
    while (!gDone) {
        ProcessBackgroundTasks();
        if (WaitNextEvent(everyEvent, &event, 6, NULL)) {
            switch (event.what) {
                case mouseDown:
                    HandleMouseDown(&event);
                    break;
                case updateEvt:
                    HandleUpdate(&event);
                    break;
                case keyDown:
                case autoKey:
                    HandleKeyDown(&event);
                    break;
                default:
                    break;
            }
        }
    }
}

int main(void)
{
    OSErr err;

    InitToolboxStuff();
    srand((unsigned int)TickCount());
    CreateMainWindow();
    err = InitAudioEngine();
    if (err != noErr) {
        SetActivityError("\pAudio init error ", err);
    }
    EventLoop();
    DisposeAudioEngine();
    if (gSliderActionUPP != NULL) {
        DisposeControlActionUPP(gSliderActionUPP);
        gSliderActionUPP = NULL;
    }
    DisposeSampleBuffer();
    return 0;
}
