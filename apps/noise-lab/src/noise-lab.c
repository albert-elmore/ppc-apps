
#include <stdio.h>
#include <string.h>
#include <Sound.h>
#include <OSUtils.h>
#include <Memory.h>
#include <Windows.h>
#include <Controls.h>
#include <Events.h>
#include <TextEdit.h>
#include <Dialogs.h>
#include <Quickdraw.h>
#include <StandardFile.h>
#include <Files.h>
#include <Folders.h>
#include <Math.h>

#ifndef kDesktopFolderType
#define kDesktopFolderType 'desk'
#endif

#ifndef inUpButton
#define inUpButton   20
#endif
#ifndef inDownButton
#define inDownButton 21
#endif
#ifndef inPageUp
#define inPageUp     22
#endif
#ifndef inPageDown
#define inPageDown   23
#endif

#ifndef shiftKey
#define shiftKey    512
#endif
#ifndef optionKey
#define optionKey   2048
#endif

#define SAMPLE_RATE         44100
#define FRAMES_PER_BUFFER   1024
#define MIN_SINE_FREQ       10
#define MAX_SINE_FREQ       5000
#define MAX_RATE_HOLD       1024
#define MAX_LOOP_SLIDER     1000
#define MAX_GAIN_PCT        100
#define TWO_PI              6.28318530717958647692
#define PAUSE_FADE_MS_MAX    20000
#define MASTER_FADE_SCALE    1000000L

enum {
    kParamMasterGain = 0,
    kParamNoiseGain  = 1,
    kParamSineGain   = 2,
    kParamTrigLoopMs = 3,
    kParamSampleHold = 4,
    kParamNoiseLoop  = 5,
    kParamNoiseLP    = 6,
    kParamNoiseHP    = 7,
    kParamNoiseAttack = 8,
    kParamNoiseHold   = 9,
    kParamNoiseDecay  = 10,
    kParamLoopEnvAttack = 11,
    kParamLoopEnvHold   = 12,
    kParamLoopEnvDecay  = 13,
    kParamLoopEnvDepth  = 14,
    kParamHoldEnvAttack = 15,
    kParamHoldEnvHold   = 16,
    kParamHoldEnvDecay  = 17,
    kParamHoldEnvDepth  = 18,
    kParamSineFreq   = 19,
    kParamSinePhase  = 20,
    kParamPitchEnv   = 21,
    kParamPitchDecay = 22,
    kParamSineAttack = 23,
    kParamSineHold   = 24,
    kParamSineDecay  = 25,
    kParamSineTuneWide = 26,
    kParamSineTuneFine = 27,
    kParamSineLP       = 28,
    kParamSineHP       = 29,
    kParamExportSecs   = 30,
    kParamCount        = 31
};

enum {
    kPageMain   = 0,
    kPageNoise1 = 1,
    kPageNoise2 = 2,
    kPageLoopEnv = 3,
    kPagePreset = 4,
    kPageSine1  = 5,
    kPageSine2  = 6,
    kPageExport = 7
};

static SndChannelPtr gChan = NULL;
static SndDoubleBufferHeader gHeader;
static SndDoubleBufferPtr gBufA = NULL;
static SndDoubleBufferPtr gBufB = NULL;
static SndDoubleBackUPP gDoubleBackUPP = NULL;

/* UI globals */
static WindowPtr gWindow = NULL;

static ControlHandle gMainPageButton = NULL;
static ControlHandle gNoisePageButton = NULL;
static ControlHandle gNoise2PageButton = NULL;
static ControlHandle gLoopEnvPageButton = NULL;
static ControlHandle gPresetPageButton = NULL;
static ControlHandle gSinePageButton = NULL;
static ControlHandle gSine2PageButton = NULL;
static ControlHandle gExportPageButton = NULL;

static ControlHandle gMasterGainSlider = NULL;
static ControlHandle gNoiseGainSlider = NULL;
static ControlHandle gSineGainSlider = NULL;
static ControlHandle gTrigLoopSlider = NULL;
static ControlHandle gSampleHoldSlider = NULL;
static ControlHandle gNoiseLoopSlider = NULL;
static ControlHandle gNoiseLPSlider = NULL;
static ControlHandle gNoiseHPSlider = NULL;
static ControlHandle gNoiseAttackSlider = NULL;
static ControlHandle gNoiseHoldSlider = NULL;
static ControlHandle gNoiseDecaySlider = NULL;
static ControlHandle gLoopEnvAttackSlider = NULL;
static ControlHandle gLoopEnvHoldSlider = NULL;
static ControlHandle gLoopEnvDecaySlider = NULL;
static ControlHandle gLoopEnvDepthSlider = NULL;
static ControlHandle gHoldEnvAttackSlider = NULL;
static ControlHandle gHoldEnvHoldSlider = NULL;
static ControlHandle gHoldEnvDecaySlider = NULL;
static ControlHandle gHoldEnvDepthSlider = NULL;
static ControlHandle gSineFreqSlider = NULL;
static ControlHandle gSinePhaseSlider = NULL;
static ControlHandle gPitchEnvSlider = NULL;
static ControlHandle gPitchDecaySlider = NULL;
static ControlHandle gSineAttackSlider = NULL;
static ControlHandle gSineHoldSlider = NULL;
static ControlHandle gAmpDecaySlider = NULL;
static ControlHandle gSineTuneWideSlider = NULL;
static ControlHandle gSineTuneFineSlider = NULL;
static ControlHandle gSineLPSlider = NULL;
static ControlHandle gSineHPSlider = NULL;
static ControlHandle gPauseFadeSlider = NULL;

static ControlHandle gNoiseModeButton = NULL;
static ControlHandle gBitDepthButton = NULL;
static ControlHandle gTrigButton = NULL;
static ControlHandle gNoiseLPButton = NULL;
static ControlHandle gNoiseHPButton = NULL;
static ControlHandle gSineLPButton = NULL;
static ControlHandle gSineHPButton = NULL;
static ControlHandle gPauseButton = NULL;
static ControlHandle gExportButton = NULL;
static ControlHandle gExportModeButton = NULL;
static ControlHandle gSavePresetButton = NULL;
static ControlHandle gLoadPresetButton = NULL;
static ControlHandle gQuickSaveButton = NULL;
static ControlHandle gRandomLoadButton = NULL;
static ControlHandle gRandomizeButton = NULL;
static ControlHandle gQuitButton = NULL;

static ControlActionUPP gSliderActionUPP = NULL;
static short gSliderTrackModifiers = 0;

/* display rects */
static Rect gValueRects[kParamCount];

static Rect gRandomToggleRects[kParamCount];
static Rect gRandomMinRects[kParamCount];
static Rect gRandomMaxRects[kParamCount];

enum {
    kEditValue = 0,
    kEditRandMin = 1,
    kEditRandMax = 2
};

static int gSelectedEditField = kEditValue;
static int gRandomEnabled[kParamCount];
static long gRandomMin[kParamCount];
static long gRandomMax[kParamCount];

static Rect gNoiseStatusRect;
static Rect gBitStatusRect;
static Rect gTrigStatusRect;
static Rect gNoiseLPStatusRect;
static Rect gNoiseHPStatusRect;
static Rect gSineLPStatusRect;
static Rect gSineHPStatusRect;
static Rect gPauseStatusRect;
static Rect gPauseFadeValueRect;

static Rect gHelpRect;

/* synth state */
static int gRunning = 1;
static unsigned long gRandState = 1;

static int gNoiseMode = 0;       /* 0=white, 1=bright, 2=held */
static int gBitDepthMode = 2;    /* 0=16, 1=8-ish, 2=4-ish */
static int gTrigMode = 1;        /* 0=free sustain, 1=loop env */
static int gNoiseLPOn = 0;
static int gNoiseHPOn = 0;
static int gSineLPOn = 0;
static int gSineHPOn = 0;
static int gPaused = 1;
static int gPauseFadeMs = 250;

static int gMasterGainPct = 100;
static int gNoiseGainPct = 30;
static int gSineGainPct = 83;
static int gTrigLoopMs = 407;

static int gRateHold = 1;
static long gNoiseLoopSize = 1;
static int gNoiseLPAmt = 0;
static int gNoiseHPAmt = 0;
static int gNoiseAttackMs = 0;
static int gNoiseHoldMs = 0;
static int gNoiseDecayMs = 2000;
static int gLoopEnvAttackMs = 889;
static int gLoopEnvHoldMs = 0;
static int gLoopEnvDecayMs = 765;
static int gLoopEnvDepthPct = 1;
static int gHoldEnvAttackMs = 0;
static int gHoldEnvHoldMs = 0;
static int gHoldEnvDecayMs = 200;
static int gHoldEnvDepthPct = 0;

static int gSineFreq = 50;
static int gSineStartPhaseDeg = 0;
static int gPitchEnvAmt = 115;
static int gPitchDecayMs = 11;
static int gSineAttackMs = 0;
static int gSineHoldMs = 0;
static int gAmpDecayMs = 2000;
static int gSineTuneWide = 36;   /* 0=-36 semis, 36=unity */
static int gSineTuneFine = 100;  /* 0=-100 cents, 100=unity */
static int gSineLPAmt = 0;
static int gSineHPAmt = 0;
static int gExportMilliseconds = 5000;
static int gExportMode = 0;    /* 0=time ms, 1=full trigger cycle */


static int gPlayNoiseMode = 0;
static int gPlayBitDepthMode = 2;
static int gPlayTrigMode = 1;
static int gPlayNoiseLPOn = 0;
static int gPlayNoiseHPOn = 0;
static int gPlaySineLPOn = 0;
static int gPlaySineHPOn = 0;
static int gPlayMasterGainPct = 100;
static int gPlayNoiseGainPct = 30;
static int gPlaySineGainPct = 83;
static int gPlayTrigLoopMs = 407;
static int gPlayRateHold = 11;
static long gPlayNoiseLoopSize = 1;
static int gPlayNoiseLPAmt = 0;
static int gPlayNoiseHPAmt = 0;
static int gPlayNoiseAttackMs = 0;
static int gPlayNoiseHoldMs = 0;
static int gPlayNoiseDecayMs = 2000;
static int gPlayLoopEnvAttackMs = 889;
static int gPlayLoopEnvHoldMs = 0;
static int gPlayLoopEnvDecayMs = 765;
static int gPlayLoopEnvDepthPct = 1;
static int gPlayHoldEnvAttackMs = 0;
static int gPlayHoldEnvHoldMs = 0;
static int gPlayHoldEnvDecayMs = 200;
static int gPlayHoldEnvDepthPct = 0;
static int gPlaySineFreq = 50;
static int gPlaySineStartPhaseDeg = 0;
static int gPlayPitchEnvAmt = 115;
static int gPlayPitchDecayMs = 11;
static int gPlaySineAttackMs = 0;
static int gPlaySineHoldMs = 0;
static int gPlayAmpDecayMs = 2000;
static int gPlaySineTuneWide = 36;
static int gPlaySineTuneFine = 100;
static int gPlaySineLPAmt = 0;
static int gPlaySineHPAmt = 0;

static int gPendingNoiseBufferRegen = 0;

static int gCurrentPage = kPageMain;
static int gSelectedParam = kParamMasterGain;

/* audio runtime */
static int gHoldCounter = 0;
static double gHeldNoise = 0.0;
static double gSinePhase = 0.0;
static double gPitchEnvLevel = 0.0;
static double gNoiseAmpEnvLevel = 1.0;
static double gAmpEnvLevel = 1.0;
static double gLoopEnvLevel = 0.0;
static double gHoldEnvLevel = 0.0;
static int gNoiseEnvStage = 0;
static int gSineEnvStage = 0;
static int gLoopEnvStage = 0;
static int gHoldEnvStage = 0;
static long gNoiseEnvCounter = 0;
static long gSineEnvCounter = 0;
static long gLoopEnvCounter = 0;
static long gHoldEnvCounter = 0;
static long gSineLoopCounter = 0;

static double gNoiseLPState = 0.0;
static double gNoiseHPState = 0.0;
static double gNoiseHPPrevInput = 0.0;

static double gSineLPState = 0.0;
static double gSineHPState = 0.0;
static double gSineHPPrevInput = 0.0;

static long gNoiseReadPos = 0;
static SInt16 gNoiseBuffer[SAMPLE_RATE];
static long gFadeCurrent = 0;
static long gFadeTarget = 0;
static long gFadeStep = MASTER_FADE_SCALE;

/* typed input / cursor */
static char gTypedDigits[16];
static short gTypedLen = 0;
static int gCursorVisible = 1;
static unsigned long gLastBlinkTick = 0;

static RGBColor gSelectedBlue = { 36000, 47000, 65535 };
static char gLastExportMessage[128] = "No export yet.";
static char gLastLoadedPresetName[64] = "None";

typedef struct ExportState {
    int holdCounter;
    double heldNoise;
    double sinePhase;
    double pitchEnvLevel;
    double noiseAmpEnvLevel;
    double ampEnvLevel;
    double loopEnvLevel;
    double holdEnvLevel;
    int noiseEnvStage;
    int sineEnvStage;
    int loopEnvStage;
    int holdEnvStage;
    long noiseEnvCounter;
    long sineEnvCounter;
    long loopEnvCounter;
    long holdEnvCounter;
    long sineLoopCounter;
    double noiseLPState;
    double noiseHPState;
    double noiseHPPrevInput;
    double sineLPState;
    double sineHPState;
    double sineHPPrevInput;
    long noiseReadPos;
} ExportState;

/* prototypes */
unsigned long NextRand(void);
SInt16 NextNoiseSample16(void);
void GenerateNoiseBuffer(void);
void ResetAudioState(void);
void TriggerSineEnvelope(double *phasePtr, double *pitchEnvPtr);
void TriggerAmpEnvelope(int attackMs, int holdMs, int decayMs, int *stagePtr, long *counterPtr, double *levelPtr);
void StepAmpEnvelope(int attackMs, int holdMs, int decayMs, int *stagePtr, long *counterPtr, double *levelPtr);
long EffectiveNoiseLoopSize(double envLevel);
int EffectiveSampleHold(double envLevel);

long LoopSliderToSamples(short sliderValue);
short LoopSamplesToSlider(long samples);
double LPAlphaFromAmount(int amountPct);
double HPAlphaFromAmount(int amountPct);
double QuantizeSampleForMode(double x, int bitDepthMode);
double PitchDecayMultiplier(void);
long MsToSamples(int ms);
long TrigLoopSamples(void);
double CurrentSineBaseFreq(void);
void UpdateFadeStep(void);
void StartPauseFade(int pauseNow);

void FillBuffer(SndDoubleBufferPtr db);
pascal void MyDoubleBackProc(SndChannelPtr chan, SndDoubleBufferPtr db);

void InitAudio(void);
void ShutdownAudio(void);

void MakePString(const char *src, Str255 dst);
void DrawPascalFromCString(const char *src);
void PStringToCString(ConstStr255Param src, char *dst);

const char *SelectedParamName(void);
const char *SelectedFieldName(void);
void ClearTypedValue(void);
long GetParamDisplayValue(int param);
void SetParamFromDisplayValue(int param, long value);
long ParamDisplayMin(int param);
long ParamDisplayMax(int param);
void InitRandomizationRanges(void);
void ClampRandomRangeForParam(int param);
void AppendTypedDigit(char c);
void RemoveTypedDigit(void);
void ApplyTypedValue(void);

void InitUI(void);
void LayoutUI(void);
void DrawStaticUI(WindowPtr w);
void UpdateValueAreas(WindowPtr w);
int ParamPage(int param);
int ParamVisibleOnPage(int param, int page);
void EnsureSelectedParamVisible(void);
void SetCurrentPage(int page);
void UpdateStatusAreas(WindowPtr w);
void SyncControlsFromParams(void);
void UpdateExportModeUI(void);
void SelectParam(int param);
void UpdateParamFromSlider(ControlHandle control);
void PaintSelectedValueRect(Rect *r);
void DrawValueTextInRect(Rect *r, const char *text, int selected);
void BlinkCursorMaybe(void);

short PromptForSaveFile(FSSpec *spec);
void GenerateRandomPresetName(Str255 dst);
short PromptForPresetFile(FSSpec *spec);
short PromptForLoadPreset(FSSpec *spec);
void ShowExportResult(const char *msg);
void ResetExportState(ExportState *st);
double NextRenderedSample(ExportState *st);
OSErr WriteBytes(short refNum, const void *data, long count);
OSErr WriteLE16(short refNum, unsigned short value);
OSErr WriteLE32(short refNum, unsigned long value);
long GetExportSampleCount(void);
OSErr ExportWavFile(FSSpec *spec, long totalSamples);
OSErr SavePresetFile(FSSpec *spec);
OSErr LoadPresetFile(FSSpec *spec);
int ExtractLongValue(const char *text, const char *key, long *value);
void DoExport(void);
void DoSavePreset(void);
void DoLoadPreset(void);
OSErr GetSessionFolderSpec(FSSpec *folderSpec, long *folderDirID);
OSErr SavePresetToSession(FSSpec *savedSpec);
OSErr LoadRandomPresetFromSession(FSSpec *loadedSpec);
void DoQuickSavePreset(void);
void DoRandomLoadPreset(void);
const char *ParamPersistKey(int param);

long RandomRange(long minValue, long maxValue);
void RandomizeSliderParams(void);
void SelectParamField(int param, int field);
void CommitAudioParamsFromUI(void);
void RefreshAudioAfterParamChange(void);
void CommitPendingLoopParamsIfNeeded(void);

short GetSliderArrowStep(short modifiers);
short GetSliderPageStep(short modifiers);
pascal void SliderAction(ControlHandle control, short partCode);

void DoMouseDown(EventRecord *event);
void DoUpdate(EventRecord *event);
void DoActivate(EventRecord *event);
void DrawTickInRect(const Rect *r);



long GetParamDisplayValue(int param)
{
    switch (param) {
        case kParamMasterGain: return gMasterGainPct;
        case kParamNoiseGain: return gNoiseGainPct;
        case kParamSineGain: return gSineGainPct;
        case kParamTrigLoopMs: return gTrigLoopMs;
        case kParamSampleHold: return gRateHold;
        case kParamNoiseLoop: return gNoiseLoopSize;
        case kParamNoiseLP: return gNoiseLPAmt;
        case kParamNoiseHP: return gNoiseHPAmt;
        case kParamNoiseAttack: return gNoiseAttackMs;
        case kParamNoiseHold: return gNoiseHoldMs;
        case kParamNoiseDecay: return gNoiseDecayMs;
        case kParamLoopEnvAttack: return gLoopEnvAttackMs;
        case kParamLoopEnvHold: return gLoopEnvHoldMs;
        case kParamLoopEnvDecay: return gLoopEnvDecayMs;
        case kParamLoopEnvDepth: return gLoopEnvDepthPct;
        case kParamHoldEnvAttack: return gHoldEnvAttackMs;
        case kParamHoldEnvHold: return gHoldEnvHoldMs;
        case kParamHoldEnvDecay: return gHoldEnvDecayMs;
        case kParamHoldEnvDepth: return gHoldEnvDepthPct;
        case kParamSineFreq: return gSineFreq;
        case kParamSinePhase: return gSineStartPhaseDeg;
        case kParamPitchEnv: return gPitchEnvAmt;
        case kParamPitchDecay: return gPitchDecayMs;
        case kParamSineAttack: return gSineAttackMs;
        case kParamSineHold: return gSineHoldMs;
        case kParamSineDecay: return gAmpDecayMs;
        case kParamSineTuneWide: return gSineTuneWide - 36;
        case kParamSineTuneFine: return gSineTuneFine - 100;
        case kParamSineLP: return gSineLPAmt;
        case kParamSineHP: return gSineHPAmt;
        case kParamExportSecs: return gExportMilliseconds;
    }
    return 0L;
}

long ParamDisplayMin(int param)
{
    switch (param) {
        case kParamMasterGain:
        case kParamNoiseGain:
        case kParamSineGain:
        case kParamNoiseLP:
        case kParamNoiseHP:
        case kParamLoopEnvDepth:
        case kParamHoldEnvDepth:
        case kParamSinePhase:
        case kParamSineLP:
        case kParamSineHP:
            return 0L;
        case kParamTrigLoopMs:
            return 1L;
        case kParamSampleHold:
            return 1L;
        case kParamNoiseLoop:
            return 1L;
        case kParamNoiseAttack:
        case kParamNoiseHold:
        case kParamNoiseDecay:
        case kParamLoopEnvAttack:
        case kParamLoopEnvHold:
        case kParamLoopEnvDecay:
        case kParamHoldEnvAttack:
        case kParamHoldEnvHold:
        case kParamHoldEnvDecay:
        case kParamSineAttack:
        case kParamSineHold:
        case kParamSineDecay:
            return 0L;
        case kParamSineFreq:
            return MIN_SINE_FREQ;
        case kParamPitchEnv:
            return 0L;
        case kParamPitchDecay:
            return 5L;
        case kParamSineTuneWide:
            return -36L;
        case kParamSineTuneFine:
            return -100L;
        case kParamExportSecs:
            return 1L;
    }
    return 0L;
}

long ParamDisplayMax(int param)
{
    switch (param) {
        case kParamMasterGain:
        case kParamNoiseGain:
        case kParamSineGain:
        case kParamNoiseLP:
        case kParamNoiseHP:
        case kParamHoldEnvDepth:
        case kParamSineLP:
        case kParamSineHP:
            return 100L;
        case kParamLoopEnvDepth:
            return 3L;
        case kParamTrigLoopMs:
            return 1500L;
        case kParamSampleHold:
            return MAX_RATE_HOLD;
        case kParamNoiseLoop:
            return 1000L;
        case kParamSinePhase:
            return 359L;
        case kParamNoiseAttack:
        case kParamNoiseHold:
        case kParamNoiseDecay:
        case kParamLoopEnvAttack:
        case kParamLoopEnvHold:
        case kParamLoopEnvDecay:
        case kParamHoldEnvAttack:
        case kParamHoldEnvHold:
        case kParamHoldEnvDecay:
        case kParamSineAttack:
        case kParamSineHold:
        case kParamSineDecay:
            return 2000L;
        case kParamSineFreq:
            return MAX_SINE_FREQ;
        case kParamPitchEnv:
            return 4000L;
        case kParamPitchDecay:
            return 1500L;
        case kParamSineTuneWide:
            return 36L;
        case kParamSineTuneFine:
            return 100L;
        case kParamExportSecs:
            return 600000L;
    }
    return 0L;
}

void SetParamFromDisplayValue(int param, long value)
{
    long minValue = ParamDisplayMin(param);
    long maxValue = ParamDisplayMax(param);

    if (value < minValue) value = minValue;
    if (value > maxValue) value = maxValue;

    switch (param) {
        case kParamMasterGain: gMasterGainPct = (int)value; break;
        case kParamNoiseGain: gNoiseGainPct = (int)value; break;
        case kParamSineGain: gSineGainPct = (int)value; break;
        case kParamTrigLoopMs: gTrigLoopMs = (int)value; break;
        case kParamSampleHold: gRateHold = (int)value; break;
        case kParamNoiseLoop: gNoiseLoopSize = value; break;
        case kParamNoiseLP: gNoiseLPAmt = (int)value; break;
        case kParamNoiseHP: gNoiseHPAmt = (int)value; break;
        case kParamNoiseAttack: gNoiseAttackMs = (int)value; break;
        case kParamNoiseHold: gNoiseHoldMs = (int)value; break;
        case kParamNoiseDecay: gNoiseDecayMs = (int)value; break;
        case kParamLoopEnvAttack: gLoopEnvAttackMs = (int)value; break;
        case kParamLoopEnvHold: gLoopEnvHoldMs = (int)value; break;
        case kParamLoopEnvDecay: gLoopEnvDecayMs = (int)value; break;
        case kParamLoopEnvDepth: gLoopEnvDepthPct = (int)value; break;
        case kParamHoldEnvAttack: gHoldEnvAttackMs = (int)value; break;
        case kParamHoldEnvHold: gHoldEnvHoldMs = (int)value; break;
        case kParamHoldEnvDecay: gHoldEnvDecayMs = (int)value; break;
        case kParamHoldEnvDepth: gHoldEnvDepthPct = (int)value; break;
        case kParamSineFreq: gSineFreq = (int)value; break;
        case kParamSinePhase: gSineStartPhaseDeg = (int)value; break;
        case kParamPitchEnv: gPitchEnvAmt = (int)value; break;
        case kParamPitchDecay: gPitchDecayMs = (int)value; break;
        case kParamSineAttack: gSineAttackMs = (int)value; break;
        case kParamSineHold: gSineHoldMs = (int)value; break;
        case kParamSineDecay: gAmpDecayMs = (int)value; break;
        case kParamSineTuneWide: gSineTuneWide = (int)value + 36; break;
        case kParamSineTuneFine: gSineTuneFine = (int)value + 100; break;
        case kParamSineLP: gSineLPAmt = (int)value; break;
        case kParamSineHP: gSineHPAmt = (int)value; break;
        case kParamExportSecs: if (gExportMode == 0) gExportMilliseconds = (int)value; break;
    }
}

void ClampRandomRangeForParam(int param)
{
    long hardMin = ParamDisplayMin(param);
    long hardMax = ParamDisplayMax(param);

    if (gRandomMin[param] < hardMin) gRandomMin[param] = hardMin;
    if (gRandomMax[param] > hardMax) gRandomMax[param] = hardMax;
    if (gRandomMin[param] > hardMax) gRandomMin[param] = hardMax;
    if (gRandomMax[param] < hardMin) gRandomMax[param] = hardMin;
    if (gRandomMin[param] > gRandomMax[param]) {
        long temp = gRandomMin[param];
        gRandomMin[param] = gRandomMax[param];
        gRandomMax[param] = temp;
    }
}

void InitRandomizationRanges(void)
{
    int i;
    for (i = 0; i < kParamCount; i++) {
        gRandomEnabled[i] = (i != kParamExportSecs);
        gRandomMin[i] = ParamDisplayMin(i);
        gRandomMax[i] = ParamDisplayMax(i);
        SetRect(&gRandomToggleRects[i], 1200, 0, 1201, 1);
        SetRect(&gRandomMinRects[i], 1200, 0, 1201, 1);
        SetRect(&gRandomMaxRects[i], 1200, 0, 1201, 1);
    }

    gRandomEnabled[kParamExportSecs] = 0;

    /* Main */
    gRandomEnabled[kParamMasterGain] = 0;
    gRandomEnabled[kParamNoiseGain] = 0;
    gRandomEnabled[kParamSineGain] = 0;
    gRandomEnabled[kParamTrigLoopMs] = 1;
    gRandomMin[kParamTrigLoopMs] = 100;
    gRandomMax[kParamTrigLoopMs] = 600;

    /* Noise 1 */
    gRandomEnabled[kParamNoiseAttack] = 1;
    gRandomMin[kParamNoiseAttack] = 0;
    gRandomMax[kParamNoiseAttack] = 500;
    gRandomEnabled[kParamNoiseHold] = 1;
    gRandomEnabled[kParamNoiseDecay] = 1;
    gRandomEnabled[kParamNoiseLP] = 1;
    gRandomEnabled[kParamNoiseHP] = 1;

    /* Noise 2 */
    gRandomEnabled[kParamSampleHold] = 1;
    gRandomMin[kParamSampleHold] = 1;
    gRandomMax[kParamSampleHold] = 3;
    gRandomEnabled[kParamHoldEnvAttack] = 1;
    gRandomEnabled[kParamHoldEnvHold] = 1;
    gRandomEnabled[kParamHoldEnvDepth] = 0;
    gRandomMin[kParamHoldEnvDepth] = 10;
    gRandomMax[kParamHoldEnvDepth] = 60;

    /* Noise 3 */
    gRandomEnabled[kParamNoiseLoop] = 1;
    gRandomMin[kParamNoiseLoop] = 1;
    gRandomMax[kParamNoiseLoop] = 1000;
    gRandomEnabled[kParamLoopEnvAttack] = 1;
    gRandomEnabled[kParamLoopEnvHold] = 1;
    gRandomEnabled[kParamLoopEnvDecay] = 1;
    gRandomEnabled[kParamLoopEnvDepth] = 1;
    gRandomMin[kParamLoopEnvDepth] = 0;
    gRandomMax[kParamLoopEnvDepth] = 3;

    /* Sine 1 */
    gRandomEnabled[kParamSineFreq] = 1;
    gRandomMin[kParamSineFreq] = 20;
    gRandomMax[kParamSineFreq] = 150;
    gRandomEnabled[kParamSinePhase] = 0;
    gRandomEnabled[kParamPitchEnv] = 1;
    gRandomMin[kParamPitchEnv] = 0;
    gRandomMax[kParamPitchEnv] = 200;
    gRandomEnabled[kParamPitchDecay] = 1;
    gRandomMin[kParamPitchDecay] = 0;
    gRandomMax[kParamPitchDecay] = 200;
    gRandomEnabled[kParamSineAttack] = 1;
    gRandomMin[kParamSineAttack] = 0;
    gRandomMax[kParamSineAttack] = 200;
    gRandomEnabled[kParamSineHold] = 1;
    gRandomMin[kParamSineHold] = 0;
    gRandomMax[kParamSineHold] = 50;

    /* Sine 2 */
    gRandomEnabled[kParamSineDecay] = 1;
    gRandomEnabled[kParamSineTuneWide] = 0;
    gRandomEnabled[kParamSineTuneFine] = 0;
    gRandomEnabled[kParamSineLP] = 1;
    gRandomEnabled[kParamSineHP] = 1;

    for (i = 0; i < kParamCount; i++) ClampRandomRangeForParam(i);
}

const char *SelectedFieldName(void)
{
    switch (gSelectedEditField) {
        case kEditRandMin: return "Rnd Min";
        case kEditRandMax: return "Rnd Max";
    }
    return "Value";
}

long RandomRange(long minValue, long maxValue)
{
    unsigned long span;

    if (maxValue <= minValue) return minValue;
    span = (unsigned long)(maxValue - minValue + 1L);
    return minValue + (long)(NextRand() % span);
}

void RandomizeSliderParams(void)
{
    int i;

    for (i = 0; i < kParamCount; i++) {
        long value;

        if (!gRandomEnabled[i]) continue;
        if (i == kParamExportSecs) continue;

        ClampRandomRangeForParam(i);
        value = RandomRange(gRandomMin[i], gRandomMax[i]);
        SetParamFromDisplayValue(i, value);
    }

    ClearTypedValue();
    SyncControlsFromParams();

    CommitAudioParamsFromUI();
    if (gTrigMode == 1) ResetAudioState();

    if (gWindow != NULL) {
        UpdateStatusAreas(gWindow);
        UpdateValueAreas(gWindow);
    }
}

void SelectParamField(int param, int field)
{
    gSelectedParam = param;
    gSelectedEditField = field;
    ClearTypedValue();
    gCursorVisible = 1;
    if (gWindow != NULL) UpdateValueAreas(gWindow);
}

void CommitAudioParamsFromUI(void)
{
    int oldNoiseMode = gPlayNoiseMode;

    gPlayNoiseMode = gNoiseMode;
    gPlayBitDepthMode = gBitDepthMode;
    gPlayTrigMode = gTrigMode;
    gPlayNoiseLPOn = gNoiseLPOn;
    gPlayNoiseHPOn = gNoiseHPOn;
    gPlaySineLPOn = gSineLPOn;
    gPlaySineHPOn = gSineHPOn;
    gPlayMasterGainPct = gMasterGainPct;
    gPlayNoiseGainPct = gNoiseGainPct;
    gPlaySineGainPct = gSineGainPct;
    gPlayTrigLoopMs = gTrigLoopMs;
    gPlayRateHold = gRateHold;
    gPlayNoiseLoopSize = gNoiseLoopSize;
    gPlayNoiseLPAmt = gNoiseLPAmt;
    gPlayNoiseHPAmt = gNoiseHPAmt;
    gPlayNoiseAttackMs = gNoiseAttackMs;
    gPlayNoiseHoldMs = gNoiseHoldMs;
    gPlayNoiseDecayMs = gNoiseDecayMs;
    gPlayLoopEnvAttackMs = gLoopEnvAttackMs;
    gPlayLoopEnvHoldMs = gLoopEnvHoldMs;
    gPlayLoopEnvDecayMs = gLoopEnvDecayMs;
    gPlayLoopEnvDepthPct = gLoopEnvDepthPct;
    gPlayHoldEnvAttackMs = gHoldEnvAttackMs;
    gPlayHoldEnvHoldMs = gHoldEnvHoldMs;
    gPlayHoldEnvDecayMs = gHoldEnvDecayMs;
    gPlayHoldEnvDepthPct = gHoldEnvDepthPct;
    gPlaySineFreq = gSineFreq;
    gPlaySineStartPhaseDeg = gSineStartPhaseDeg;
    gPlayPitchEnvAmt = gPitchEnvAmt;
    gPlayPitchDecayMs = gPitchDecayMs;
    gPlaySineAttackMs = gSineAttackMs;
    gPlaySineHoldMs = gSineHoldMs;
    gPlayAmpDecayMs = gAmpDecayMs;
    gPlaySineTuneWide = gSineTuneWide;
    gPlaySineTuneFine = gSineTuneFine;
    gPlaySineLPAmt = gSineLPAmt;
    gPlaySineHPAmt = gSineHPAmt;

    if (gPendingNoiseBufferRegen || (oldNoiseMode != gPlayNoiseMode)) {
        GenerateNoiseBuffer();
        gPendingNoiseBufferRegen = 0;
    }
}

void RefreshAudioAfterParamChange(void)
{
    if (gPaused || (gTrigMode == 0)) {
        CommitAudioParamsFromUI();
        CommitAudioParamsFromUI();
    ResetAudioState();
    } else {
        if (gPlayNoiseMode != gNoiseMode) gPendingNoiseBufferRegen = 1;
    }
}

void CommitPendingLoopParamsIfNeeded(void)
{
    if (!gPaused && (gTrigMode == 1)) {
        CommitAudioParamsFromUI();
    }
}

unsigned long NextRand(void)
{
    gRandState = (gRandState * 1664525UL) + 1013904223UL;
    return gRandState;
}

long EffectiveNoiseLoopSize(double envLevel)
{
    double amount = ((double)gPlayLoopEnvDepthPct / 100.0) * envLevel;
    long value = gPlayNoiseLoopSize + (long)(((double)(SAMPLE_RATE - gPlayNoiseLoopSize)) * amount);
    if (value < 1L) value = 1L;
    if (value > SAMPLE_RATE) value = SAMPLE_RATE;
    return value;
}

int EffectiveSampleHold(double envLevel)
{
    double amount = ((double)gPlayHoldEnvDepthPct / 100.0) * envLevel;
    int value = gPlayRateHold + (int)(((double)(MAX_RATE_HOLD - gPlayRateHold)) * amount);
    if (value < 1) value = 1;
    if (value > MAX_RATE_HOLD) value = MAX_RATE_HOLD;
    return value;
}

SInt16 NextNoiseSample16(void)
{
    return (SInt16)((NextRand() >> 16) & 0xFFFF);
}

void GenerateNoiseBuffer(void)
{
    long i;

    if (gPlayNoiseMode == 0) {
        for (i = 0; i < SAMPLE_RATE; i++) {
            gNoiseBuffer[i] = NextNoiseSample16();
        }
    } else if (gPlayNoiseMode == 1) {
        SInt16 prev = NextNoiseSample16();
        for (i = 0; i < SAMPLE_RATE; i++) {
            long cur = (long)NextNoiseSample16();
            long diff = cur - (long)prev;
            if (diff < -32768L) diff = -32768L;
            if (diff > 32767L) diff = 32767L;
            gNoiseBuffer[i] = (SInt16)diff;
            prev = (SInt16)cur;
        }
    } else {
        int holdLen = 16;
        int remain = 0;
        SInt16 held = 0;
        for (i = 0; i < SAMPLE_RATE; i++) {
            if (remain <= 0) {
                held = NextNoiseSample16();
                remain = holdLen;
            }
            gNoiseBuffer[i] = held;
            remain--;
        }
    }
    gNoiseReadPos = 0;
}

void TriggerSineEnvelope(double *phasePtr, double *pitchEnvPtr)
{
    *phasePtr = ((double)gPlaySineStartPhaseDeg / 360.0) * TWO_PI;
    while (*phasePtr >= TWO_PI) *phasePtr -= TWO_PI;
    while (*phasePtr < 0.0) *phasePtr += TWO_PI;
    *pitchEnvPtr = 1.0;
}

long MsToSamples(int ms)
{
    long value;
    if (ms <= 0) return 0L;
    value = ((long)ms * SAMPLE_RATE) / 1000L;
    if (value < 1L) value = 1L;
    return value;
}

void TriggerAmpEnvelope(int attackMs, int holdMs, int decayMs, int *stagePtr, long *counterPtr, double *levelPtr)
{
    long attackSamples = MsToSamples(attackMs);
    long holdSamples = MsToSamples(holdMs);
    long decaySamples = MsToSamples(decayMs);

    *levelPtr = 0.0;
    *counterPtr = 0L;
    *stagePtr = 0;

    if (attackSamples > 0L) {
        *stagePtr = 1;
        *counterPtr = attackSamples;
    } else if (holdSamples > 0L) {
        *stagePtr = 2;
        *counterPtr = holdSamples;
        *levelPtr = 1.0;
    } else if (decaySamples > 0L) {
        *stagePtr = 3;
        *counterPtr = decaySamples;
        *levelPtr = 1.0;
    }
}

void StepAmpEnvelope(int attackMs, int holdMs, int decayMs, int *stagePtr, long *counterPtr, double *levelPtr)
{
    long attackSamples = MsToSamples(attackMs);
    long holdSamples = MsToSamples(holdMs);
    long decaySamples = MsToSamples(decayMs);

    switch (*stagePtr) {
        case 1:
            if (attackSamples <= 1L) {
                *levelPtr = 1.0;
                if (holdSamples > 0L) {
                    *stagePtr = 2;
                    *counterPtr = holdSamples;
                } else if (decaySamples > 0L) {
                    *stagePtr = 3;
                    *counterPtr = decaySamples;
                } else {
                    *stagePtr = 0;
                    *counterPtr = 0L;
                }
            } else {
                *levelPtr += 1.0 / (double)attackSamples;
                if (*levelPtr > 1.0) *levelPtr = 1.0;
                (*counterPtr)--;
                if (*counterPtr <= 0L) {
                    *levelPtr = 1.0;
                    if (holdSamples > 0L) {
                        *stagePtr = 2;
                        *counterPtr = holdSamples;
                    } else if (decaySamples > 0L) {
                        *stagePtr = 3;
                        *counterPtr = decaySamples;
                    } else {
                        *stagePtr = 0;
                        *counterPtr = 0L;
                    }
                }
            }
            break;
        case 2:
            *levelPtr = 1.0;
            (*counterPtr)--;
            if (*counterPtr <= 0L) {
                if (decaySamples > 0L) {
                    *stagePtr = 3;
                    *counterPtr = decaySamples;
                } else {
                    *stagePtr = 0;
                    *counterPtr = 0L;
                }
            }
            break;
        case 3:
            if (decaySamples <= 1L) {
                *levelPtr = 0.0;
                *stagePtr = 0;
                *counterPtr = 0L;
            } else {
                *levelPtr -= 1.0 / (double)decaySamples;
                if (*levelPtr < 0.0) *levelPtr = 0.0;
                (*counterPtr)--;
                if (*counterPtr <= 0L) {
                    *levelPtr = 0.0;
                    *stagePtr = 0;
                    *counterPtr = 0L;
                }
            }
            break;
        default:
            break;
    }
}

void ResetAudioState(void)
{
    gHoldCounter = 0;
    gHeldNoise = 0.0;
    gSinePhase = ((double)gPlaySineStartPhaseDeg / 360.0) * TWO_PI;
    gPitchEnvLevel = 0.0;
    gNoiseAmpEnvLevel = 1.0;
    gAmpEnvLevel = 1.0;
    gNoiseEnvStage = 0;
    gSineEnvStage = 0;
    gNoiseEnvCounter = 0L;
    gSineEnvCounter = 0L;
    gSineLoopCounter = 0;
    gNoiseLPState = 0.0;
    gNoiseHPState = 0.0;
    gNoiseHPPrevInput = 0.0;
    gSineLPState = 0.0;
    gSineHPState = 0.0;
    gSineHPPrevInput = 0.0;
    gNoiseReadPos = 0;
    if (gPlayTrigMode == 1) {
        TriggerSineEnvelope(&gSinePhase, &gPitchEnvLevel);
        TriggerAmpEnvelope(gPlayNoiseAttackMs, gPlayNoiseHoldMs, gPlayNoiseDecayMs, &gNoiseEnvStage, &gNoiseEnvCounter, &gNoiseAmpEnvLevel);
        TriggerAmpEnvelope(gPlaySineAttackMs, gPlaySineHoldMs, gPlayAmpDecayMs, &gSineEnvStage, &gSineEnvCounter, &gAmpEnvLevel);
    }
}

long LoopSliderToSamples(short sliderValue)
{
    long value;
    if (sliderValue < 1) sliderValue = 1;
    if (sliderValue > MAX_LOOP_SLIDER) sliderValue = MAX_LOOP_SLIDER;
    value = 1L + ((long)(sliderValue - 1) * (SAMPLE_RATE - 1L)) / (MAX_LOOP_SLIDER - 1L);
    if (value < 1L) value = 1L;
    if (value > SAMPLE_RATE) value = SAMPLE_RATE;
    return value;
}

short LoopSamplesToSlider(long samples)
{
    long value;
    if (samples < 1L) samples = 1L;
    if (samples > SAMPLE_RATE) samples = SAMPLE_RATE;
    value = 1L + ((samples - 1L) * (MAX_LOOP_SLIDER - 1L)) / (SAMPLE_RATE - 1L);
    if (value < 1L) value = 1L;
    if (value > MAX_LOOP_SLIDER) value = MAX_LOOP_SLIDER;
    return (short)value;
}

double LPAlphaFromAmount(int amountPct)
{
    if (amountPct <= 0) return 0.0;
    if (amountPct >= 100) return 1.0;
    return 0.005 + (((double)amountPct / 100.0) * 0.995);
}

double HPAlphaFromAmount(int amountPct)
{
    if (amountPct <= 0) return 0.0;
    if (amountPct >= 100) return 0.999;
    return 0.5 + (((double)amountPct / 100.0) * 0.499);
}

double PitchDecayMultiplier(void)
{
    double seconds;
    if (gPlayPitchDecayMs <= 1) return 0.0;
    seconds = (double)gPlayPitchDecayMs / 1000.0;
    return exp(-1.0 / (seconds * (double)SAMPLE_RATE));
}

long TrigLoopSamples(void)
{
    long value = ((long)gPlayTrigLoopMs * SAMPLE_RATE) / 1000L;
    if (value < 1L) value = 1L;
    if (value > SAMPLE_RATE * 10L) value = SAMPLE_RATE * 10L;
    return value;
}

double CurrentSineBaseFreq(void)
{
    double semis = (double)(gPlaySineTuneWide - 36) + ((double)(gPlaySineTuneFine - 100) / 100.0);
    double freq = (double)gPlaySineFreq * pow(2.0, semis / 12.0);
    if (freq < 1.0) freq = 1.0;
    if (freq > 20000.0) freq = 20000.0;
    return freq;
}

double QuantizeSampleForMode(double x, int bitDepthMode)
{
    long levels;
    long q;
    double scaled;

    if (x < -1.0) x = -1.0;
    if (x > 1.0) x = 1.0;
    if (bitDepthMode == 0) return x;

    levels = (bitDepthMode == 1) ? 32L : 8L;
    scaled = (x + 1.0) * 0.5;
    q = (long)(scaled * (double)(levels - 1L) + 0.5);
    scaled = (double)q / (double)(levels - 1L);
    return (scaled * 2.0) - 1.0;
}

void UpdateFadeStep(void)
{
    long fadeSamples;

    fadeSamples = ((long)gPauseFadeMs * SAMPLE_RATE) / 1000L;
    if (fadeSamples <= 0L) {
        gFadeStep = MASTER_FADE_SCALE;
    } else {
        gFadeStep = MASTER_FADE_SCALE / fadeSamples;
        if (gFadeStep < 1L) gFadeStep = 1L;
    }
}

void StartPauseFade(int pauseNow)
{
    UpdateFadeStep();

    if (pauseNow) {
        gFadeTarget = 0L;
    } else {
        if (gFadeCurrent <= 0L) ResetAudioState();
        gPaused = 0;
        gFadeTarget = MASTER_FADE_SCALE;
    }

    if (gPauseFadeMs <= 0) {
        gFadeCurrent = gFadeTarget;
        gPaused = (gFadeCurrent == 0L);
    }
}



void FillBuffer(SndDoubleBufferPtr db)
{
    long i;
    SInt16 *p;
    double pitchDecayMul = PitchDecayMultiplier();
    long trigLoopSamples = TrigLoopSamples();

    p = (SInt16 *)db->dbSoundData;
    db->dbNumFrames = FRAMES_PER_BUFFER;
    db->dbFlags = dbBufferReady;

    for (i = 0; i < FRAMES_PER_BUFFER; i++) {
        double out = 0.0;
        double fadeGain = (double)gFadeCurrent / (double)MASTER_FADE_SCALE;

        if (!gPaused || (gFadeCurrent > 0L)) {
            double noiseX;
            double sineX;
            double sineFreqNow;
            long effectiveLoopSize;
            int effectiveHold;

            if (gPlayTrigMode == 1) {
                if (gSineLoopCounter <= 0) {
                    CommitPendingLoopParamsIfNeeded();
                    TriggerSineEnvelope(&gSinePhase, &gPitchEnvLevel);
                    TriggerAmpEnvelope(gPlayNoiseAttackMs, gPlayNoiseHoldMs, gPlayNoiseDecayMs, &gNoiseEnvStage, &gNoiseEnvCounter, &gNoiseAmpEnvLevel);
                    TriggerAmpEnvelope(gPlaySineAttackMs, gPlaySineHoldMs, gPlayAmpDecayMs, &gSineEnvStage, &gSineEnvCounter, &gAmpEnvLevel);
                    TriggerAmpEnvelope(gPlayLoopEnvAttackMs, gPlayLoopEnvHoldMs, gPlayLoopEnvDecayMs, &gLoopEnvStage, &gLoopEnvCounter, &gLoopEnvLevel);
                    TriggerAmpEnvelope(gPlayHoldEnvAttackMs, gPlayHoldEnvHoldMs, gPlayHoldEnvDecayMs, &gHoldEnvStage, &gHoldEnvCounter, &gHoldEnvLevel);
                    gSineLoopCounter = trigLoopSamples;
                }
                gSineLoopCounter--;
            } else {
                CommitAudioParamsFromUI();
                gPitchEnvLevel = 0.0;
                gNoiseAmpEnvLevel = 1.0;
                gAmpEnvLevel = 1.0;
                gLoopEnvLevel = 0.0;
                gHoldEnvLevel = 0.0;
                gNoiseEnvStage = 0;
                gSineEnvStage = 0;
                gLoopEnvStage = 0;
                gHoldEnvStage = 0;
            }

            effectiveLoopSize = EffectiveNoiseLoopSize(gLoopEnvLevel);
            effectiveHold = EffectiveSampleHold(gHoldEnvLevel);

            noiseX = (double)gNoiseBuffer[gNoiseReadPos] / 32768.0;
            gNoiseReadPos++;
            if (gNoiseReadPos >= effectiveLoopSize) gNoiseReadPos = 0;

            if (effectiveHold <= 1) {
                gHeldNoise = noiseX;
            } else {
                if (gHoldCounter <= 0) {
                    gHeldNoise = noiseX;
                    gHoldCounter = effectiveHold;
                }
                noiseX = gHeldNoise;
                gHoldCounter--;
            }

            if (gPlayTrigMode == 1) {
                noiseX = noiseX * gNoiseAmpEnvLevel;
                StepAmpEnvelope(gPlayNoiseAttackMs, gPlayNoiseHoldMs, gPlayNoiseDecayMs, &gNoiseEnvStage, &gNoiseEnvCounter, &gNoiseAmpEnvLevel);
                StepAmpEnvelope(gPlayLoopEnvAttackMs, gPlayLoopEnvHoldMs, gPlayLoopEnvDecayMs, &gLoopEnvStage, &gLoopEnvCounter, &gLoopEnvLevel);
                StepAmpEnvelope(gPlayHoldEnvAttackMs, gPlayHoldEnvHoldMs, gPlayHoldEnvDecayMs, &gHoldEnvStage, &gHoldEnvCounter, &gHoldEnvLevel);
            }

            if (gPlayNoiseLPOn && (gPlayNoiseLPAmt > 0)) {
                double aLP = LPAlphaFromAmount(gPlayNoiseLPAmt);
                gNoiseLPState = gNoiseLPState + (aLP * (noiseX - gNoiseLPState));
                noiseX = gNoiseLPState;
            }

            if (gPlayNoiseHPOn && (gPlayNoiseHPAmt > 0)) {
                double aHP = HPAlphaFromAmount(gPlayNoiseHPAmt);
                gNoiseHPState = aHP * (gNoiseHPState + noiseX - gNoiseHPPrevInput);
                gNoiseHPPrevInput = noiseX;
                noiseX = gNoiseHPState;
            } else {
                gNoiseHPPrevInput = noiseX;
            }

            sineFreqNow = CurrentSineBaseFreq() + ((double)gPlayPitchEnvAmt * gPitchEnvLevel);
            sineX = sin(gSinePhase);
            gSinePhase += (TWO_PI * sineFreqNow) / (double)SAMPLE_RATE;
            while (gSinePhase >= TWO_PI) gSinePhase -= TWO_PI;

            if (gPlayTrigMode == 1) {
                sineX = sineX * gAmpEnvLevel;
                gPitchEnvLevel = gPitchEnvLevel * pitchDecayMul;
                if (gPitchEnvLevel < 0.0001) gPitchEnvLevel = 0.0;
                StepAmpEnvelope(gPlaySineAttackMs, gPlaySineHoldMs, gPlayAmpDecayMs, &gSineEnvStage, &gSineEnvCounter, &gAmpEnvLevel);
            }

            if (gPlaySineLPOn && (gPlaySineLPAmt > 0)) {
                double aLP = LPAlphaFromAmount(gPlaySineLPAmt);
                gSineLPState = gSineLPState + (aLP * (sineX - gSineLPState));
                sineX = gSineLPState;
            }

            if (gPlaySineHPOn && (gPlaySineHPAmt > 0)) {
                double aHP = HPAlphaFromAmount(gPlaySineHPAmt);
                gSineHPState = aHP * (gSineHPState + sineX - gSineHPPrevInput);
                gSineHPPrevInput = sineX;
                sineX = gSineHPState;
            } else {
                gSineHPPrevInput = sineX;
            }

            noiseX = QuantizeSampleForMode(noiseX, gPlayBitDepthMode);
            sineX = QuantizeSampleForMode(sineX, gPlayBitDepthMode);

            out = (noiseX * ((double)gPlayNoiseGainPct / 100.0)) +
                  (sineX * ((double)gPlaySineGainPct / 100.0));
            out = out * ((double)gPlayMasterGainPct / 100.0);
            out = out * fadeGain;
        }

        if (out < -1.0) out = -1.0;
        if (out > 1.0) out = 1.0;
        p[i] = (SInt16)(out * 32767.0);

        if (gFadeCurrent < gFadeTarget) {
            gFadeCurrent += gFadeStep;
            if (gFadeCurrent >= gFadeTarget) gFadeCurrent = gFadeTarget;
        } else if (gFadeCurrent > gFadeTarget) {
            gFadeCurrent -= gFadeStep;
            if (gFadeCurrent <= gFadeTarget) gFadeCurrent = gFadeTarget;
        }
    }

    if ((gFadeCurrent == 0L) && (gFadeTarget == 0L)) gPaused = 1;

    if (!gRunning) db->dbFlags = dbBufferReady | dbLastBuffer;
}


pascal void MyDoubleBackProc(SndChannelPtr chan, SndDoubleBufferPtr db)
{
    (void)chan;
    FillBuffer(db);
}

void InitAudio(void)
{
    OSErr err;
    long dbSize;

    dbSize = sizeof(SndDoubleBuffer) + ((FRAMES_PER_BUFFER * sizeof(SInt16)) - 1);
    gBufA = (SndDoubleBufferPtr)NewPtrClear(dbSize);
    gBufB = (SndDoubleBufferPtr)NewPtrClear(dbSize);
    if ((gBufA == NULL) || (gBufB == NULL)) return;

    gDoubleBackUPP = NewSndDoubleBackProc(MyDoubleBackProc);

    gFadeCurrent = gPaused ? 0L : MASTER_FADE_SCALE;
    gFadeTarget = gFadeCurrent;
    UpdateFadeStep();

    gHeader.dbhNumChannels = 1;
    gHeader.dbhSampleSize = 16;
    gHeader.dbhCompressionID = 0;
    gHeader.dbhPacketSize = 0;
    gHeader.dbhSampleRate = rate44khz;
    gHeader.dbhBufferPtr[0] = gBufA;
    gHeader.dbhBufferPtr[1] = gBufB;
    gHeader.dbhDoubleBack = gDoubleBackUPP;

    FillBuffer(gBufA);
    FillBuffer(gBufB);

    err = SndNewChannel(&gChan, sampledSynth, initMono, NULL);
    if (err != noErr) return;
    SndPlayDoubleBuffer(gChan, &gHeader);
}

void ShutdownAudio(void)
{
    if (gChan != NULL) {
        SndDisposeChannel(gChan, true);
        gChan = NULL;
    }
    if (gBufA != NULL) {
        DisposePtr((Ptr)gBufA);
        gBufA = NULL;
    }
    if (gBufB != NULL) {
        DisposePtr((Ptr)gBufB);
        gBufB = NULL;
    }
    if (gDoubleBackUPP != NULL) {
        DisposeSndDoubleBackUPP(gDoubleBackUPP);
        gDoubleBackUPP = NULL;
    }
    if (gSliderActionUPP != NULL) {
        DisposeControlActionUPP(gSliderActionUPP);
        gSliderActionUPP = NULL;
    }
}

void MakePString(const char *src, Str255 dst)
{
    short len = 0;
    while ((src[len] != '\0') && (len < 255)) len++;
    dst[0] = (unsigned char)len;
    while (len > 0) {
        dst[len] = (unsigned char)src[len - 1];
        len--;
    }
}

void DrawPascalFromCString(const char *src)
{
    Str255 pStr;
    MakePString(src, pStr);
    DrawString(pStr);
}

void PStringToCString(ConstStr255Param src, char *dst)
{
    short i;
    short len = src[0];
    for (i = 0; i < len; i++) dst[i] = (char)src[i + 1];
    dst[len] = '\0';
}

int ParamPage(int param)
{
    switch (param) {
        case kParamMasterGain:
        case kParamNoiseGain:
        case kParamSineGain:
        case kParamTrigLoopMs:
            return kPageMain;
        case kParamSampleHold:
        case kParamNoiseLoop:
        case kParamNoiseLP:
        case kParamNoiseHP:
            return kPageNoise1;
        case kParamNoiseAttack:
        case kParamNoiseHold:
        case kParamNoiseDecay:
            return kPageNoise2;
        case kParamLoopEnvAttack:
        case kParamLoopEnvHold:
        case kParamLoopEnvDecay:
        case kParamLoopEnvDepth:
            return kPageLoopEnv;
        case kParamHoldEnvAttack:
        case kParamHoldEnvHold:
        case kParamHoldEnvDecay:
        case kParamHoldEnvDepth:
            return kPageNoise2;
        case kParamSineFreq:
        case kParamSinePhase:
        case kParamPitchEnv:
        case kParamPitchDecay:
        case kParamSineAttack:
        case kParamSineHold:
            return kPageSine1;
        case kParamSineDecay:
        case kParamSineTuneWide:
        case kParamSineTuneFine:
        case kParamSineLP:
        case kParamSineHP:
            return kPageSine2;
        case kParamExportSecs:
            return kPageExport;
    }
    return kPageMain;
}

int ParamVisibleOnPage(int param, int page)
{
    switch (page) {
        case kPageMain:
            return (param == kParamMasterGain) || (param == kParamNoiseGain) ||
                   (param == kParamSineGain) || (param == kParamTrigLoopMs);
        case kPageNoise1:
            return (param == kParamNoiseAttack) || (param == kParamNoiseHold) ||
                   (param == kParamNoiseDecay) || (param == kParamNoiseLP) ||
                   (param == kParamNoiseHP);
        case kPageNoise2:
            return (param == kParamSampleHold) || (param == kParamHoldEnvAttack) ||
                   (param == kParamHoldEnvHold) || (param == kParamHoldEnvDecay) ||
                   (param == kParamHoldEnvDepth);
        case kPageLoopEnv:
            return (param == kParamNoiseLoop) || (param == kParamLoopEnvAttack) ||
                   (param == kParamLoopEnvHold) || (param == kParamLoopEnvDecay) ||
                   (param == kParamLoopEnvDepth);
        case kPagePreset:
            return 0;
        case kPageSine1:
            return (param == kParamSineFreq) || (param == kParamSinePhase) ||
                   (param == kParamPitchEnv) || (param == kParamPitchDecay) ||
                   (param == kParamSineAttack) || (param == kParamSineHold);
        case kPageSine2:
            return (param == kParamSineDecay) || (param == kParamSineTuneWide) ||
                   (param == kParamSineTuneFine) || (param == kParamSineLP) ||
                   (param == kParamSineHP);
        case kPageExport:
            return (param == kParamExportSecs);
    }
    return 0;
}

void EnsureSelectedParamVisible(void)
{
    if (!ParamVisibleOnPage(gSelectedParam, gCurrentPage)) {
        if (gCurrentPage == kPageMain) gSelectedParam = kParamMasterGain;
        else if (gCurrentPage == kPageNoise1) gSelectedParam = kParamNoiseAttack;
        else if (gCurrentPage == kPageNoise2) gSelectedParam = kParamSampleHold;
        else if (gCurrentPage == kPageLoopEnv) gSelectedParam = kParamNoiseLoop;
        else if (gCurrentPage == kPagePreset) gSelectedParam = kParamMasterGain;
        else if (gCurrentPage == kPageSine1) gSelectedParam = kParamSineFreq;
        else if (gCurrentPage == kPageSine2) gSelectedParam = kParamSineDecay;
        else gSelectedParam = kParamExportSecs;
        ClearTypedValue();
    }
}

void SetCurrentPage(int page)
{
    if (page < kPageMain) page = kPageMain;
    if (page > kPageExport) page = kPageExport;
    gCurrentPage = page;
    EnsureSelectedParamVisible();
    if (gWindow != NULL) {
        LayoutUI();
        InvalRect(&gWindow->portRect);
    }
}


const char *SelectedParamName(void)
{
    switch (gSelectedParam) {
        case kParamMasterGain: return "Master Gain %";
        case kParamNoiseGain: return "Noise Gain %";
        case kParamSineGain: return "Sine Gain %";
        case kParamTrigLoopMs: return "Trig Loop ms";
        case kParamSampleHold: return "Sample & Hold";
        case kParamNoiseLoop: return "Buffer Size";
        case kParamNoiseLP: return "Filter LP %";
        case kParamNoiseHP: return "Filter HP %";
        case kParamNoiseAttack: return "Amp Attack";
        case kParamNoiseHold: return "Amp Hold";
        case kParamNoiseDecay: return "Amp Decay";
        case kParamLoopEnvAttack: return "Attack";
        case kParamLoopEnvHold: return "Hold";
        case kParamLoopEnvDecay: return "Decay";
        case kParamLoopEnvDepth: return "Depth %";
        case kParamHoldEnvAttack: return "Attack";
        case kParamHoldEnvHold: return "Hold";
        case kParamHoldEnvDecay: return "Decay";
        case kParamHoldEnvDepth: return "Depth %";
        case kParamSineFreq: return "Sine Freq";
        case kParamSinePhase: return "Start Phase";
        case kParamPitchEnv: return "Pitch Env Hz";
        case kParamPitchDecay: return "Pitch Decay";
        case kParamSineAttack: return "Sine Attack";
        case kParamSineHold: return "Sine Hold";
        case kParamSineDecay: return "Sine Decay";
        case kParamSineTuneWide: return "Tune Semi";
        case kParamSineTuneFine: return "Fine Tune";
        case kParamSineLP: return "Filter LP %";
        case kParamSineHP: return "Filter HP %";
        case kParamExportSecs: return "Export ms";
    }
    return "Master Gain %";
}


void ClearTypedValue(void)
{
    gTypedLen = 0;
    gTypedDigits[0] = '\0';
}

void AppendTypedDigit(char c)
{
    if (gTypedLen < 10) {
        gTypedDigits[gTypedLen] = c;
        gTypedLen++;
        gTypedDigits[gTypedLen] = '\0';
    }
}

void RemoveTypedDigit(void)
{
    if (gTypedLen > 0) {
        gTypedLen--;
        gTypedDigits[gTypedLen] = '\0';
    }
}

void SelectParam(int param)
{
    SelectParamField(param, kEditValue);
}


void ApplyTypedValue(void)
{
    long value = 0;
    short i;
    short startIndex = 0;
    int negative = 0;

    if (gTypedLen <= 0) return;
    if (gTypedDigits[0] == '-') {
        negative = 1;
        startIndex = 1;
        if (gTypedLen == 1) return;
    }
    for (i = startIndex; i < gTypedLen; i++) value = (value * 10L) + (long)(gTypedDigits[i] - '0');
    if (negative) value = -value;

    if (gSelectedEditField == kEditRandMin) {
        gRandomMin[gSelectedParam] = value;
        ClampRandomRangeForParam(gSelectedParam);
    } else if (gSelectedEditField == kEditRandMax) {
        gRandomMax[gSelectedParam] = value;
        ClampRandomRangeForParam(gSelectedParam);
    } else {
        SetParamFromDisplayValue(gSelectedParam, value);
        SyncControlsFromParams();
        RefreshAudioAfterParamChange();
    }

    ClearTypedValue();
    UpdateValueAreas(gWindow);
}



void LayoutUI(void)
{
    Rect r;
    short offX = 1200;
    short rowY[6];
    short i;
    short statusY;
    short quitY = 518;
    short quitX = 580;

    if (gWindow == NULL) return;

    rowY[0] = 94;
    rowY[1] = 138;
    rowY[2] = 182;
    rowY[3] = 226;
    rowY[4] = 270;
    rowY[5] = 314;

    MoveControl(gMainPageButton, 10, 20);
    MoveControl(gNoisePageButton, 86, 20);
    MoveControl(gNoise2PageButton, 162, 20);
    MoveControl(gLoopEnvPageButton, 238, 20);
    MoveControl(gSinePageButton, 314, 20);
    MoveControl(gSine2PageButton, 390, 20);
    MoveControl(gExportPageButton, 466, 20);
    MoveControl(gPresetPageButton, 542, 20);

    MoveControl(gMasterGainSlider, offX, 40);
    MoveControl(gNoiseGainSlider, offX, 40);
    MoveControl(gSineGainSlider, offX, 40);
    MoveControl(gTrigLoopSlider, offX, 40);
    MoveControl(gSampleHoldSlider, offX, 40);
    MoveControl(gNoiseLoopSlider, offX, 40);
    MoveControl(gNoiseLPSlider, offX, 40);
    MoveControl(gNoiseHPSlider, offX, 40);
    MoveControl(gNoiseAttackSlider, offX, 40);
    MoveControl(gNoiseHoldSlider, offX, 40);
    MoveControl(gNoiseDecaySlider, offX, 40);
    MoveControl(gLoopEnvAttackSlider, offX, 40);
    MoveControl(gLoopEnvHoldSlider, offX, 40);
    MoveControl(gLoopEnvDecaySlider, offX, 40);
    MoveControl(gLoopEnvDepthSlider, offX, 40);
    MoveControl(gHoldEnvAttackSlider, offX, 40);
    MoveControl(gHoldEnvHoldSlider, offX, 40);
    MoveControl(gHoldEnvDecaySlider, offX, 40);
    MoveControl(gHoldEnvDepthSlider, offX, 40);
    MoveControl(gSineFreqSlider, offX, 40);
    MoveControl(gSinePhaseSlider, offX, 40);
    MoveControl(gPitchEnvSlider, offX, 40);
    MoveControl(gPitchDecaySlider, offX, 40);
    MoveControl(gSineAttackSlider, offX, 40);
    MoveControl(gSineHoldSlider, offX, 40);
    MoveControl(gAmpDecaySlider, offX, 40);
    MoveControl(gSineTuneWideSlider, offX, 40);
    MoveControl(gSineTuneFineSlider, offX, 40);
    MoveControl(gSineLPSlider, offX, 40);
    MoveControl(gSineHPSlider, offX, 40);
    MoveControl(gPauseFadeSlider, offX, 40);

    MoveControl(gNoiseModeButton, offX, 40);
    MoveControl(gBitDepthButton, offX, 40);
    MoveControl(gTrigButton, offX, 40);
    MoveControl(gNoiseLPButton, offX, 40);
    MoveControl(gNoiseHPButton, offX, 40);
    MoveControl(gSineLPButton, offX, 40);
    MoveControl(gSineHPButton, offX, 40);
    MoveControl(gPauseButton, offX, 40);
    MoveControl(gExportButton, offX, 40);
    MoveControl(gExportModeButton, offX, 40);
    MoveControl(gSavePresetButton, offX, 40);
    MoveControl(gLoadPresetButton, offX, 40);
    MoveControl(gQuickSaveButton, offX, 40);
    MoveControl(gRandomLoadButton, offX, 40);
    MoveControl(gRandomizeButton, 580, quitY);
    MoveControl(gQuitButton, 700, quitY);

    for (i = 0; i < kParamCount; i++) SetRect(&gValueRects[i], offX, 0, offX + 1, 1);

    SetRect(&gNoiseStatusRect, offX, 0, offX + 1, 1);
    SetRect(&gBitStatusRect, offX, 0, offX + 1, 1);
    SetRect(&gTrigStatusRect, offX, 0, offX + 1, 1);
    SetRect(&gNoiseLPStatusRect, offX, 0, offX + 1, 1);
    SetRect(&gNoiseHPStatusRect, offX, 0, offX + 1, 1);
    SetRect(&gSineLPStatusRect, offX, 0, offX + 1, 1);
    SetRect(&gSineHPStatusRect, offX, 0, offX + 1, 1);
    SetRect(&gPauseStatusRect, offX, 0, offX + 1, 1);
    SetRect(&gPauseFadeValueRect, offX, 0, offX + 1, 1);

    if (gCurrentPage == kPageMain) {
        SetRect(&r, 180, rowY[0], 510, rowY[0] + 16); MoveControl(gMasterGainSlider, r.left, r.top); SizeControl(gMasterGainSlider, r.right-r.left, r.bottom-r.top);
        SetRect(&gValueRects[kParamMasterGain], 542, rowY[0]-2, 617, rowY[0]+16);
        SetRect(&r, 180, rowY[1], 510, rowY[1] + 16); MoveControl(gNoiseGainSlider, r.left, r.top); SizeControl(gNoiseGainSlider, r.right-r.left, r.bottom-r.top);
        SetRect(&gValueRects[kParamNoiseGain], 542, rowY[1]-2, 617, rowY[1]+16);
        SetRect(&r, 180, rowY[2], 510, rowY[2] + 16); MoveControl(gSineGainSlider, r.left, r.top); SizeControl(gSineGainSlider, r.right-r.left, r.bottom-r.top);
        SetRect(&gValueRects[kParamSineGain], 542, rowY[2]-2, 617, rowY[2]+16);
        SetRect(&r, 180, rowY[3], 510, rowY[3] + 16); MoveControl(gTrigLoopSlider, r.left, r.top); SizeControl(gTrigLoopSlider, r.right-r.left, r.bottom-r.top);
        SetRect(&gValueRects[kParamTrigLoopMs], 542, rowY[3]-2, 617, rowY[3]+16);

        SetRect(&r, 180, rowY[4], 510, rowY[4] + 16); MoveControl(gPauseFadeSlider, r.left, r.top); SizeControl(gPauseFadeSlider, r.right-r.left, r.bottom-r.top);
        SetRect(&gPauseFadeValueRect, 542, rowY[4]-2, 617, rowY[4]+16);

        MoveControl(gBitDepthButton, 20, 362);
        MoveControl(gTrigButton, 85, 362);
        MoveControl(gPauseButton, 160, 362);
        statusY = 388;
        SetRect(&gBitStatusRect, 20, statusY, 70, statusY + 16);
        SetRect(&gTrigStatusRect, 85, statusY, 150, statusY + 16);
        SetRect(&gPauseStatusRect, 160, statusY, 240, statusY + 16);
        MoveControl(gQuickSaveButton, 340, quitY);
        MoveControl(gRandomLoadButton, 460, quitY);
    } else if (gCurrentPage == kPageNoise1) {
        SetRect(&r, 180, rowY[0], 510, rowY[0] + 16); MoveControl(gNoiseAttackSlider, r.left, r.top); SizeControl(gNoiseAttackSlider, r.right-r.left, r.bottom-r.top);
        SetRect(&gValueRects[kParamNoiseAttack], 542, rowY[0]-2, 617, rowY[0]+16);
        SetRect(&r, 180, rowY[1], 510, rowY[1] + 16); MoveControl(gNoiseHoldSlider, r.left, r.top); SizeControl(gNoiseHoldSlider, r.right-r.left, r.bottom-r.top);
        SetRect(&gValueRects[kParamNoiseHold], 542, rowY[1]-2, 617, rowY[1]+16);
        SetRect(&r, 180, rowY[2], 510, rowY[2] + 16); MoveControl(gNoiseDecaySlider, r.left, r.top); SizeControl(gNoiseDecaySlider, r.right-r.left, r.bottom-r.top);
        SetRect(&gValueRects[kParamNoiseDecay], 542, rowY[2]-2, 617, rowY[2]+16);
        SetRect(&r, 180, rowY[3], 510, rowY[3] + 16); MoveControl(gNoiseLPSlider, r.left, r.top); SizeControl(gNoiseLPSlider, r.right-r.left, r.bottom-r.top);
        SetRect(&gValueRects[kParamNoiseLP], 542, rowY[3]-2, 617, rowY[3]+16);
        SetRect(&r, 180, rowY[4], 510, rowY[4] + 16); MoveControl(gNoiseHPSlider, r.left, r.top); SizeControl(gNoiseHPSlider, r.right-r.left, r.bottom-r.top);
        SetRect(&gValueRects[kParamNoiseHP], 542, rowY[4]-2, 617, rowY[4]+16);

        MoveControl(gNoiseModeButton, 20, 362);
        MoveControl(gNoiseLPButton, 105, 362);
        MoveControl(gNoiseHPButton, 165, 362);
        MoveControl(gBitDepthButton, 225, 362);
        MoveControl(gTrigButton, 290, 362);
        MoveControl(gPauseButton, 365, 362);
        statusY = 388;
        SetRect(&gNoiseStatusRect, 20, statusY, 90, statusY + 16);
        SetRect(&gNoiseLPStatusRect, 105, statusY, 155, statusY + 16);
        SetRect(&gNoiseHPStatusRect, 165, statusY, 215, statusY + 16);
        SetRect(&gBitStatusRect, 225, statusY, 275, statusY + 16);
        SetRect(&gTrigStatusRect, 290, statusY, 355, statusY + 16);
        SetRect(&gPauseStatusRect, 365, statusY, 445, statusY + 16);
        MoveControl(gQuickSaveButton, 340, quitY);
        MoveControl(gRandomLoadButton, 460, quitY);
        MoveControl(gQuickSaveButton, 340, quitY);
        MoveControl(gRandomLoadButton, 460, quitY);
        MoveControl(gQuickSaveButton, 340, quitY);
        MoveControl(gRandomLoadButton, 460, quitY);
    } else if (gCurrentPage == kPageNoise2) {
        SetRect(&r, 180, rowY[0], 510, rowY[0] + 16); MoveControl(gSampleHoldSlider, r.left, r.top); SizeControl(gSampleHoldSlider, r.right-r.left, r.bottom-r.top);
        SetRect(&gValueRects[kParamSampleHold], 542, rowY[0]-2, 617, rowY[0]+16);
        SetRect(&r, 180, rowY[1], 510, rowY[1] + 16); MoveControl(gHoldEnvAttackSlider, r.left, r.top); SizeControl(gHoldEnvAttackSlider, r.right-r.left, r.bottom-r.top);
        SetRect(&gValueRects[kParamHoldEnvAttack], 542, rowY[1]-2, 617, rowY[1]+16);
        SetRect(&r, 180, rowY[2], 510, rowY[2] + 16); MoveControl(gHoldEnvHoldSlider, r.left, r.top); SizeControl(gHoldEnvHoldSlider, r.right-r.left, r.bottom-r.top);
        SetRect(&gValueRects[kParamHoldEnvHold], 542, rowY[2]-2, 617, rowY[2]+16);
        SetRect(&r, 180, rowY[3], 510, rowY[3] + 16); MoveControl(gHoldEnvDecaySlider, r.left, r.top); SizeControl(gHoldEnvDecaySlider, r.right-r.left, r.bottom-r.top);
        SetRect(&gValueRects[kParamHoldEnvDecay], 542, rowY[3]-2, 617, rowY[3]+16);
        SetRect(&r, 180, rowY[4], 510, rowY[4] + 16); MoveControl(gHoldEnvDepthSlider, r.left, r.top); SizeControl(gHoldEnvDepthSlider, r.right-r.left, r.bottom-r.top);
        SetRect(&gValueRects[kParamHoldEnvDepth], 542, rowY[4]-2, 617, rowY[4]+16);

        MoveControl(gNoiseModeButton, 20, 362);
        MoveControl(gNoiseLPButton, 105, 362);
        MoveControl(gNoiseHPButton, 165, 362);
        MoveControl(gBitDepthButton, 225, 362);
        MoveControl(gTrigButton, 290, 362);
        MoveControl(gPauseButton, 365, 362);
        statusY = 388;
        SetRect(&gNoiseStatusRect, 20, statusY, 90, statusY + 16);
        SetRect(&gNoiseLPStatusRect, 105, statusY, 155, statusY + 16);
        SetRect(&gNoiseHPStatusRect, 165, statusY, 215, statusY + 16);
        SetRect(&gBitStatusRect, 225, statusY, 275, statusY + 16);
        SetRect(&gTrigStatusRect, 290, statusY, 355, statusY + 16);
        SetRect(&gPauseStatusRect, 365, statusY, 445, statusY + 16);
    } else if (gCurrentPage == kPageLoopEnv) {
        SetRect(&r, 180, rowY[0], 510, rowY[0] + 16); MoveControl(gNoiseLoopSlider, r.left, r.top); SizeControl(gNoiseLoopSlider, r.right-r.left, r.bottom-r.top);
        SetRect(&gValueRects[kParamNoiseLoop], 542, rowY[0]-2, 617, rowY[0]+16);
        SetRect(&r, 180, rowY[1], 510, rowY[1] + 16); MoveControl(gLoopEnvAttackSlider, r.left, r.top); SizeControl(gLoopEnvAttackSlider, r.right-r.left, r.bottom-r.top);
        SetRect(&gValueRects[kParamLoopEnvAttack], 542, rowY[1]-2, 617, rowY[1]+16);
        SetRect(&r, 180, rowY[2], 510, rowY[2] + 16); MoveControl(gLoopEnvHoldSlider, r.left, r.top); SizeControl(gLoopEnvHoldSlider, r.right-r.left, r.bottom-r.top);
        SetRect(&gValueRects[kParamLoopEnvHold], 542, rowY[2]-2, 617, rowY[2]+16);
        SetRect(&r, 180, rowY[3], 510, rowY[3] + 16); MoveControl(gLoopEnvDecaySlider, r.left, r.top); SizeControl(gLoopEnvDecaySlider, r.right-r.left, r.bottom-r.top);
        SetRect(&gValueRects[kParamLoopEnvDecay], 542, rowY[3]-2, 617, rowY[3]+16);
        SetRect(&r, 180, rowY[4], 510, rowY[4] + 16); MoveControl(gLoopEnvDepthSlider, r.left, r.top); SizeControl(gLoopEnvDepthSlider, r.right-r.left, r.bottom-r.top);
        SetRect(&gValueRects[kParamLoopEnvDepth], 542, rowY[4]-2, 617, rowY[4]+16);

        MoveControl(gNoiseModeButton, 20, 362);
        MoveControl(gNoiseLPButton, 105, 362);
        MoveControl(gNoiseHPButton, 165, 362);
        MoveControl(gBitDepthButton, 225, 362);
        MoveControl(gTrigButton, 290, 362);
        MoveControl(gPauseButton, 365, 362);
        statusY = 388;
        SetRect(&gNoiseStatusRect, 20, statusY, 90, statusY + 16);
        SetRect(&gNoiseLPStatusRect, 105, statusY, 155, statusY + 16);
        SetRect(&gNoiseHPStatusRect, 165, statusY, 215, statusY + 16);
        SetRect(&gBitStatusRect, 225, statusY, 275, statusY + 16);
        SetRect(&gTrigStatusRect, 290, statusY, 355, statusY + 16);
        SetRect(&gPauseStatusRect, 365, statusY, 445, statusY + 16);
    } else if (gCurrentPage == kPagePreset) {
        MoveControl(gSavePresetButton, 20, 118);
        MoveControl(gLoadPresetButton, 20, 148);
        MoveControl(gPauseButton, 20, 362);
        statusY = 388;
        SetRect(&gPauseStatusRect, 20, statusY, 100, statusY + 16);
    } else if (gCurrentPage == kPageSine1) {
        SetRect(&r, 180, rowY[0], 510, rowY[0] + 16); MoveControl(gSineFreqSlider, r.left, r.top); SizeControl(gSineFreqSlider, r.right-r.left, r.bottom-r.top);
        SetRect(&gValueRects[kParamSineFreq], 542, rowY[0]-2, 617, rowY[0]+16);
        SetRect(&r, 180, rowY[1], 510, rowY[1] + 16); MoveControl(gSinePhaseSlider, r.left, r.top); SizeControl(gSinePhaseSlider, r.right-r.left, r.bottom-r.top);
        SetRect(&gValueRects[kParamSinePhase], 542, rowY[1]-2, 617, rowY[1]+16);
        SetRect(&r, 180, rowY[2], 510, rowY[2] + 16); MoveControl(gPitchEnvSlider, r.left, r.top); SizeControl(gPitchEnvSlider, r.right-r.left, r.bottom-r.top);
        SetRect(&gValueRects[kParamPitchEnv], 542, rowY[2]-2, 617, rowY[2]+16);
        SetRect(&r, 180, rowY[3], 510, rowY[3] + 16); MoveControl(gPitchDecaySlider, r.left, r.top); SizeControl(gPitchDecaySlider, r.right-r.left, r.bottom-r.top);
        SetRect(&gValueRects[kParamPitchDecay], 542, rowY[3]-2, 617, rowY[3]+16);
        SetRect(&r, 180, rowY[4], 510, rowY[4] + 16); MoveControl(gSineAttackSlider, r.left, r.top); SizeControl(gSineAttackSlider, r.right-r.left, r.bottom-r.top);
        SetRect(&gValueRects[kParamSineAttack], 542, rowY[4]-2, 617, rowY[4]+16);
        SetRect(&r, 180, rowY[5], 510, rowY[5] + 16); MoveControl(gSineHoldSlider, r.left, r.top); SizeControl(gSineHoldSlider, r.right-r.left, r.bottom-r.top);
        SetRect(&gValueRects[kParamSineHold], 542, rowY[5]-2, 617, rowY[5]+16);

        MoveControl(gSineLPButton, 20, 362);
        MoveControl(gSineHPButton, 80, 362);
        MoveControl(gBitDepthButton, 140, 362);
        MoveControl(gTrigButton, 205, 362);
        MoveControl(gPauseButton, 280, 362);
        statusY = 388;
        SetRect(&gSineLPStatusRect, 20, statusY, 70, statusY + 16);
        SetRect(&gSineHPStatusRect, 80, statusY, 130, statusY + 16);
        SetRect(&gBitStatusRect, 140, statusY, 190, statusY + 16);
        SetRect(&gTrigStatusRect, 205, statusY, 270, statusY + 16);
        SetRect(&gPauseStatusRect, 280, statusY, 360, statusY + 16);
        MoveControl(gQuickSaveButton, 340, quitY);
        MoveControl(gRandomLoadButton, 460, quitY);
        MoveControl(gQuickSaveButton, 340, quitY);
        MoveControl(gRandomLoadButton, 460, quitY);
    } else if (gCurrentPage == kPageSine2) {
        SetRect(&r, 180, rowY[0], 510, rowY[0] + 16); MoveControl(gAmpDecaySlider, r.left, r.top); SizeControl(gAmpDecaySlider, r.right-r.left, r.bottom-r.top);
        SetRect(&gValueRects[kParamSineDecay], 542, rowY[0]-2, 617, rowY[0]+16);
        SetRect(&r, 180, rowY[1], 510, rowY[1] + 16); MoveControl(gSineTuneWideSlider, r.left, r.top); SizeControl(gSineTuneWideSlider, r.right-r.left, r.bottom-r.top);
        SetRect(&gValueRects[kParamSineTuneWide], 542, rowY[1]-2, 617, rowY[1]+16);
        SetRect(&r, 180, rowY[2], 510, rowY[2] + 16); MoveControl(gSineTuneFineSlider, r.left, r.top); SizeControl(gSineTuneFineSlider, r.right-r.left, r.bottom-r.top);
        SetRect(&gValueRects[kParamSineTuneFine], 542, rowY[2]-2, 617, rowY[2]+16);
        SetRect(&r, 180, rowY[3], 510, rowY[3] + 16); MoveControl(gSineLPSlider, r.left, r.top); SizeControl(gSineLPSlider, r.right-r.left, r.bottom-r.top);
        SetRect(&gValueRects[kParamSineLP], 542, rowY[3]-2, 617, rowY[3]+16);
        SetRect(&r, 180, rowY[4], 510, rowY[4] + 16); MoveControl(gSineHPSlider, r.left, r.top); SizeControl(gSineHPSlider, r.right-r.left, r.bottom-r.top);
        SetRect(&gValueRects[kParamSineHP], 542, rowY[4]-2, 617, rowY[4]+16);

        MoveControl(gSineLPButton, 20, 362);
        MoveControl(gSineHPButton, 80, 362);
        MoveControl(gBitDepthButton, 140, 362);
        MoveControl(gTrigButton, 205, 362);
        MoveControl(gPauseButton, 280, 362);
        statusY = 388;
        SetRect(&gSineLPStatusRect, 20, statusY, 70, statusY + 16);
        SetRect(&gSineHPStatusRect, 80, statusY, 130, statusY + 16);
        SetRect(&gBitStatusRect, 140, statusY, 190, statusY + 16);
        SetRect(&gTrigStatusRect, 205, statusY, 270, statusY + 16);
        SetRect(&gPauseStatusRect, 280, statusY, 360, statusY + 16);
    } else {
        SetRect(&gValueRects[kParamExportSecs], 520, rowY[0]-2, 595, rowY[0]+16);
        MoveControl(gExportModeButton, 20, 146);
        MoveControl(gExportButton, 20, 176);
    }

    for (i = 0; i < kParamCount; i++) {
        if (!ParamVisibleOnPage(i, gCurrentPage)) {
            SetRect(&gRandomToggleRects[i], offX, 0, offX + 1, 1);
            SetRect(&gRandomMinRects[i], offX, 0, offX + 1, 1);
            SetRect(&gRandomMaxRects[i], offX, 0, offX + 1, 1);
            continue;
        }

        if ((gValueRects[i].left < 1000) && (i != kParamExportSecs)) {
            gRandomToggleRects[i].top = gValueRects[i].top + 1;
            gRandomToggleRects[i].bottom = gValueRects[i].bottom - 1;
            gValueRects[i].left = 520;
            gValueRects[i].right = 574;

            gRandomToggleRects[i].top = gValueRects[i].top + 1;
            gRandomToggleRects[i].bottom = gValueRects[i].bottom - 1;
            gRandomToggleRects[i].left = 584;
            gRandomToggleRects[i].right = 600;

            gRandomMinRects[i].top = gValueRects[i].top;
            gRandomMinRects[i].bottom = gValueRects[i].bottom;
            gRandomMinRects[i].left = 626;
            gRandomMinRects[i].right = 701;

            gRandomMaxRects[i].top = gValueRects[i].top;
            gRandomMaxRects[i].bottom = gValueRects[i].bottom;
            gRandomMaxRects[i].left = 706;
            gRandomMaxRects[i].right = 781;
        } else if (i == kParamExportSecs) {
            SetRect(&gRandomToggleRects[i], offX, 0, offX + 1, 1);
            SetRect(&gRandomMinRects[i], offX, 0, offX + 1, 1);
            SetRect(&gRandomMaxRects[i], offX, 0, offX + 1, 1);
        }
    }

    SetRect(&gHelpRect, 20, 464, 700, 512);

    HiliteControl(gMainPageButton, (gCurrentPage == kPageMain) ? 255 : 0);
    HiliteControl(gNoisePageButton, (gCurrentPage == kPageNoise1) ? 255 : 0);
    HiliteControl(gNoise2PageButton, (gCurrentPage == kPageNoise2) ? 255 : 0);
    HiliteControl(gLoopEnvPageButton, (gCurrentPage == kPageLoopEnv) ? 255 : 0);
    HiliteControl(gPresetPageButton, (gCurrentPage == kPagePreset) ? 255 : 0);
    HiliteControl(gSinePageButton, (gCurrentPage == kPageSine1) ? 255 : 0);
    HiliteControl(gSine2PageButton, (gCurrentPage == kPageSine2) ? 255 : 0);
    HiliteControl(gExportPageButton, (gCurrentPage == kPageExport) ? 255 : 0);
}




void SyncControlsFromParams(void)
{
    if (gMasterGainSlider) SetControlValue(gMasterGainSlider, (short)gMasterGainPct);
    if (gNoiseGainSlider) SetControlValue(gNoiseGainSlider, (short)gNoiseGainPct);
    if (gSineGainSlider) SetControlValue(gSineGainSlider, (short)gSineGainPct);
    if (gTrigLoopSlider) SetControlValue(gTrigLoopSlider, (short)gTrigLoopMs);
    if (gSampleHoldSlider) SetControlValue(gSampleHoldSlider, (short)gRateHold);
    if (gNoiseLoopSlider) SetControlValue(gNoiseLoopSlider, LoopSamplesToSlider(gNoiseLoopSize));
    if (gNoiseLPSlider) SetControlValue(gNoiseLPSlider, (short)gNoiseLPAmt);
    if (gNoiseHPSlider) SetControlValue(gNoiseHPSlider, (short)gNoiseHPAmt);
    if (gNoiseAttackSlider) SetControlValue(gNoiseAttackSlider, (short)gNoiseAttackMs);
    if (gNoiseHoldSlider) SetControlValue(gNoiseHoldSlider, (short)gNoiseHoldMs);
    if (gNoiseDecaySlider) SetControlValue(gNoiseDecaySlider, (short)gNoiseDecayMs);
    if (gLoopEnvAttackSlider) SetControlValue(gLoopEnvAttackSlider, (short)gLoopEnvAttackMs);
    if (gLoopEnvHoldSlider) SetControlValue(gLoopEnvHoldSlider, (short)gLoopEnvHoldMs);
    if (gLoopEnvDecaySlider) SetControlValue(gLoopEnvDecaySlider, (short)gLoopEnvDecayMs);
    if (gLoopEnvDepthSlider) SetControlValue(gLoopEnvDepthSlider, (short)gLoopEnvDepthPct);
    if (gHoldEnvAttackSlider) SetControlValue(gHoldEnvAttackSlider, (short)gHoldEnvAttackMs);
    if (gHoldEnvHoldSlider) SetControlValue(gHoldEnvHoldSlider, (short)gHoldEnvHoldMs);
    if (gHoldEnvDecaySlider) SetControlValue(gHoldEnvDecaySlider, (short)gHoldEnvDecayMs);
    if (gHoldEnvDepthSlider) SetControlValue(gHoldEnvDepthSlider, (short)gHoldEnvDepthPct);
    if (gSineFreqSlider) SetControlValue(gSineFreqSlider, (short)gSineFreq);
    if (gSinePhaseSlider) SetControlValue(gSinePhaseSlider, (short)gSineStartPhaseDeg);
    if (gPitchEnvSlider) SetControlValue(gPitchEnvSlider, (short)gPitchEnvAmt);
    if (gPitchDecaySlider) SetControlValue(gPitchDecaySlider, (short)gPitchDecayMs);
    if (gSineAttackSlider) SetControlValue(gSineAttackSlider, (short)gSineAttackMs);
    if (gSineHoldSlider) SetControlValue(gSineHoldSlider, (short)gSineHoldMs);
    if (gAmpDecaySlider) SetControlValue(gAmpDecaySlider, (short)gAmpDecayMs);
    if (gSineTuneWideSlider) SetControlValue(gSineTuneWideSlider, (short)gSineTuneWide);
    if (gSineTuneFineSlider) SetControlValue(gSineTuneFineSlider, (short)gSineTuneFine);
    if (gSineLPSlider) SetControlValue(gSineLPSlider, (short)gSineLPAmt);
    if (gSineHPSlider) SetControlValue(gSineHPSlider, (short)gSineHPAmt);
    if (gPauseFadeSlider) SetControlValue(gPauseFadeSlider, (short)gPauseFadeMs);
    UpdateExportModeUI();
}


void UpdateExportModeUI(void)
{
    if (gExportModeButton) {
        SetControlValue(gExportModeButton, (short)gExportMode);
        HiliteControl(gExportModeButton, 0);
    }
}


void InitUI(void)
{
    Rect wr;
    Rect r;

    SetRect(&wr, 40, 40, 840, 620);
    gWindow = NewWindow(NULL, &wr, "\pNoise Lab", true, zoomDocProc, (WindowPtr)-1, true, 0);

    SetRect(&r, 10, 20, 72, 40); gMainPageButton = NewControl(gWindow, &r, "\pMain", true, 0, 0, 1, pushButProc, 0);
    SetRect(&r, 86, 20, 148, 40); gNoisePageButton = NewControl(gWindow, &r, "\pNoise1", true, 0, 0, 1, pushButProc, 0);
    SetRect(&r, 162, 20, 224, 40); gNoise2PageButton = NewControl(gWindow, &r, "\pNoise2", true, 0, 0, 1, pushButProc, 0);
    SetRect(&r, 238, 20, 300, 40); gLoopEnvPageButton = NewControl(gWindow, &r, "\pNoise3", true, 0, 0, 1, pushButProc, 0);
    SetRect(&r, 314, 20, 376, 40); gPresetPageButton = NewControl(gWindow, &r, "\pPreset", true, 0, 0, 1, pushButProc, 0);
    SetRect(&r, 390, 20, 452, 40); gSinePageButton = NewControl(gWindow, &r, "\pSine1", true, 0, 0, 1, pushButProc, 0);
    SetRect(&r, 466, 20, 528, 40); gSine2PageButton = NewControl(gWindow, &r, "\pSine2", true, 0, 0, 1, pushButProc, 0);
    SetRect(&r, 542, 20, 604, 40); gExportPageButton = NewControl(gWindow, &r, "\pExport", true, 0, 0, 1, pushButProc, 0);

    SetRect(&r, 180, 74, 470, 90); gMasterGainSlider = NewControl(gWindow, &r, "\p", true, gMasterGainPct, 0, 100, scrollBarProc, 0);
    SetRect(&r, 180, 118, 470, 134); gNoiseGainSlider = NewControl(gWindow, &r, "\p", true, gNoiseGainPct, 0, 100, scrollBarProc, 0);
    SetRect(&r, 180, 162, 470, 178); gSineGainSlider = NewControl(gWindow, &r, "\p", true, gSineGainPct, 0, 100, scrollBarProc, 0);
    SetRect(&r, 180, 206, 470, 222); gTrigLoopSlider = NewControl(gWindow, &r, "\p", true, gTrigLoopMs, 1, 1500, scrollBarProc, 0);
    SetRect(&r, 180, 250, 470, 266); gPauseFadeSlider = NewControl(gWindow, &r, "\p", true, gPauseFadeMs, 0, PAUSE_FADE_MS_MAX, scrollBarProc, 0);

    SetRect(&r, 180, 74, 470, 90); gSampleHoldSlider = NewControl(gWindow, &r, "\p", true, gRateHold, 1, MAX_RATE_HOLD, scrollBarProc, 0);
    SetRect(&r, 180, 118, 470, 134); gNoiseLoopSlider = NewControl(gWindow, &r, "\p", true, LoopSamplesToSlider(gNoiseLoopSize), 1, MAX_LOOP_SLIDER, scrollBarProc, 0);
    SetRect(&r, 180, 162, 470, 178); gNoiseLPSlider = NewControl(gWindow, &r, "\p", true, gNoiseLPAmt, 0, 100, scrollBarProc, 0);
    SetRect(&r, 180, 206, 470, 222); gNoiseHPSlider = NewControl(gWindow, &r, "\p", true, gNoiseHPAmt, 0, 100, scrollBarProc, 0);

    SetRect(&r, 180, 74, 470, 90); gNoiseAttackSlider = NewControl(gWindow, &r, "\p", true, gNoiseAttackMs, 0, 2000, scrollBarProc, 0);
    SetRect(&r, 180, 118, 470, 134); gNoiseHoldSlider = NewControl(gWindow, &r, "\p", true, gNoiseHoldMs, 0, 2000, scrollBarProc, 0);
    SetRect(&r, 180, 162, 470, 178); gNoiseDecaySlider = NewControl(gWindow, &r, "\p", true, gNoiseDecayMs, 0, 2000, scrollBarProc, 0);

    SetRect(&r, 180, 74, 470, 90); gLoopEnvAttackSlider = NewControl(gWindow, &r, "\p", true, gLoopEnvAttackMs, 0, 2000, scrollBarProc, 0);
    SetRect(&r, 180, 118, 470, 134); gLoopEnvHoldSlider = NewControl(gWindow, &r, "\p", true, gLoopEnvHoldMs, 0, 2000, scrollBarProc, 0);
    SetRect(&r, 180, 162, 470, 178); gLoopEnvDecaySlider = NewControl(gWindow, &r, "\p", true, gLoopEnvDecayMs, 0, 2000, scrollBarProc, 0);
    SetRect(&r, 180, 206, 470, 222); gLoopEnvDepthSlider = NewControl(gWindow, &r, "\p", true, gLoopEnvDepthPct, 0, 100, scrollBarProc, 0);

    SetRect(&r, 180, 74, 470, 90); gHoldEnvAttackSlider = NewControl(gWindow, &r, "\p", true, gHoldEnvAttackMs, 0, 2000, scrollBarProc, 0);
    SetRect(&r, 180, 118, 470, 134); gHoldEnvHoldSlider = NewControl(gWindow, &r, "\p", true, gHoldEnvHoldMs, 0, 2000, scrollBarProc, 0);
    SetRect(&r, 180, 162, 470, 178); gHoldEnvDecaySlider = NewControl(gWindow, &r, "\p", true, gHoldEnvDecayMs, 0, 2000, scrollBarProc, 0);
    SetRect(&r, 180, 206, 470, 222); gHoldEnvDepthSlider = NewControl(gWindow, &r, "\p", true, gHoldEnvDepthPct, 0, 100, scrollBarProc, 0);

    SetRect(&r, 180, 74, 470, 90); gSineFreqSlider = NewControl(gWindow, &r, "\p", true, gSineFreq, MIN_SINE_FREQ, MAX_SINE_FREQ, scrollBarProc, 0);
    SetRect(&r, 180, 118, 470, 134); gSinePhaseSlider = NewControl(gWindow, &r, "\p", true, gSineStartPhaseDeg, 0, 359, scrollBarProc, 0);
    SetRect(&r, 180, 162, 470, 178); gPitchEnvSlider = NewControl(gWindow, &r, "\p", true, gPitchEnvAmt, 0, 4000, scrollBarProc, 0);
    SetRect(&r, 180, 206, 470, 222); gPitchDecaySlider = NewControl(gWindow, &r, "\p", true, gPitchDecayMs, 5, 1500, scrollBarProc, 0);
    SetRect(&r, 180, 250, 470, 266); gSineAttackSlider = NewControl(gWindow, &r, "\p", true, gSineAttackMs, 0, 2000, scrollBarProc, 0);
    SetRect(&r, 180, 294, 470, 310); gSineHoldSlider = NewControl(gWindow, &r, "\p", true, gSineHoldMs, 0, 2000, scrollBarProc, 0);

    SetRect(&r, 180, 74, 470, 90); gAmpDecaySlider = NewControl(gWindow, &r, "\p", true, gAmpDecayMs, 0, 2000, scrollBarProc, 0);
    SetRect(&r, 180, 118, 470, 134); gSineTuneWideSlider = NewControl(gWindow, &r, "\p", true, gSineTuneWide, 0, 72, scrollBarProc, 0);
    SetRect(&r, 180, 162, 470, 178); gSineTuneFineSlider = NewControl(gWindow, &r, "\p", true, gSineTuneFine, 0, 200, scrollBarProc, 0);
    SetRect(&r, 180, 206, 470, 222); gSineLPSlider = NewControl(gWindow, &r, "\p", true, gSineLPAmt, 0, 100, scrollBarProc, 0);
    SetRect(&r, 180, 250, 470, 266); gSineHPSlider = NewControl(gWindow, &r, "\p", true, gSineHPAmt, 0, 100, scrollBarProc, 0);

    SetRect(&r, 20, 342, 90, 362); gNoiseModeButton = NewControl(gWindow, &r, "\pNoise", true, 0, 0, 1, pushButProc, 0);
    SetRect(&r, 95, 342, 145, 362); gBitDepthButton = NewControl(gWindow, &r, "\pBits", true, 0, 0, 1, pushButProc, 0);
    SetRect(&r, 150, 342, 215, 362); gTrigButton = NewControl(gWindow, &r, "\pTrig", true, 0, 0, 1, pushButProc, 0);
    SetRect(&r, 220, 342, 270, 362); gNoiseLPButton = NewControl(gWindow, &r, "\pN LP", true, 0, 0, 1, pushButProc, 0);
    SetRect(&r, 275, 342, 325, 362); gNoiseHPButton = NewControl(gWindow, &r, "\pN HP", true, 0, 0, 1, pushButProc, 0);
    SetRect(&r, 330, 342, 380, 362); gSineLPButton = NewControl(gWindow, &r, "\pS LP", true, 0, 0, 1, pushButProc, 0);
    SetRect(&r, 385, 342, 435, 362); gSineHPButton = NewControl(gWindow, &r, "\pS HP", true, 0, 0, 1, pushButProc, 0);
    SetRect(&r, 440, 342, 520, 362); gPauseButton = NewControl(gWindow, &r, "\pPause", true, 0, 0, 1, pushButProc, 0);

    SetRect(&r, 20, 126, 140, 146); gExportModeButton = NewControl(gWindow, &r, "\pFull Cycle", true, gExportMode, 0, 1, checkBoxProc, 0);
    SetRect(&r, 20, 156, 120, 176); gExportButton = NewControl(gWindow, &r, "\pExport WAV", true, 0, 0, 1, pushButProc, 0);
    SetRect(&r, 20, 186, 120, 206); gSavePresetButton = NewControl(gWindow, &r, "\pSave Preset", true, 0, 0, 1, pushButProc, 0);
    SetRect(&r, 20, 216, 120, 236); gLoadPresetButton = NewControl(gWindow, &r, "\pLoad Preset", true, 0, 0, 1, pushButProc, 0);
    SetRect(&r, 340, 518, 450, 538); gQuickSaveButton = NewControl(gWindow, &r, "\pQuick Save", true, 0, 0, 1, pushButProc, 0);
    SetRect(&r, 460, 518, 570, 538); gRandomLoadButton = NewControl(gWindow, &r, "\pRandom Load", true, 0, 0, 1, pushButProc, 0);
    SetRect(&r, 580, 518, 680, 538); gRandomizeButton = NewControl(gWindow, &r, "\pRandomize", true, 0, 0, 1, pushButProc, 0);
    SetRect(&r, 700, 518, 780, 538); gQuitButton = NewControl(gWindow, &r, "\pQuit", true, 0, 0, 1, pushButProc, 0);

    gSliderActionUPP = NewControlActionProc(SliderAction);

    SetPort(gWindow);
    LayoutUI();
    SyncControlsFromParams();
    DrawStaticUI(gWindow);
    DrawControls(gWindow);
    UpdateStatusAreas(gWindow);
    UpdateValueAreas(gWindow);
}



void DrawStaticUI(WindowPtr w)
{
    SetPort(w);
    EraseRect(&w->portRect);

    MoveTo(20, 58);
    if ((gCurrentPage != kPageExport) && (gCurrentPage != kPagePreset)) {
        MoveTo(586, 78); DrawString("\pR");
        MoveTo(522, 78); DrawString("\pVal");
        MoveTo(632, 78); DrawString("\pMin");
        MoveTo(714, 78); DrawString("\pMax");
    } else if (gCurrentPage == kPageExport) {
        MoveTo(522, 78); DrawString("\pVal");
    }
    MoveTo(20, 78);
    if (gCurrentPage == kPageMain) {
        DrawString("\pMain page");
        MoveTo(20, 106); DrawString("\pMaster Gain %");
        MoveTo(20, 150); DrawString("\pNoise Gain %");
        MoveTo(20, 194); DrawString("\pSine Gain %");
        MoveTo(20, 238); DrawString("\pTrig Loop ms");
        MoveTo(20, 282); DrawString("\pPause Fade ms");
    } else if (gCurrentPage == kPageNoise1) {
        DrawString("\pNoise page 1");
        MoveTo(20, 106); DrawString("\pAmp Attack ms");
        MoveTo(20, 150); DrawString("\pAmp Hold ms");
        MoveTo(20, 194); DrawString("\pAmp Decay ms");
        MoveTo(20, 238); DrawString("\pFilter LP %");
        MoveTo(20, 282); DrawString("\pFilter HP %");
    } else if (gCurrentPage == kPageNoise2) {
        DrawString("\pNoise page 2");
        MoveTo(20, 106); DrawString("\pSample & Hold");
        MoveTo(20, 150); DrawString("\pAttack ms");
        MoveTo(20, 194); DrawString("\pHold ms");
        MoveTo(20, 238); DrawString("\pDecay ms");
        MoveTo(20, 282); DrawString("\pDepth %");
    } else if (gCurrentPage == kPageLoopEnv) {
        DrawString("\pNoise page 3");
        MoveTo(20, 106); DrawString("\pBuffer Size");
        MoveTo(20, 150); DrawString("\pAttack ms");
        MoveTo(20, 194); DrawString("\pHold ms");
        MoveTo(20, 238); DrawString("\pDecay ms");
        MoveTo(20, 282); DrawString("\pDepth %");
    } else if (gCurrentPage == kPagePreset) {
        DrawString("\pPreset page");
        MoveTo(20, 106); DrawString("\pSave presets with a new random 6-character file name.");
        MoveTo(20, 196); DrawString("\pLoaded preset:");
        MoveTo(20, 212); DrawPascalFromCString(gLastLoadedPresetName);
    } else if (gCurrentPage == kPageSine1) {
        DrawString("\pSine page 1");
        MoveTo(20, 106); DrawString("\pSine Freq");
        MoveTo(20, 150); DrawString("\pStart Phase deg");
        MoveTo(20, 194); DrawString("\pPitch Env Hz");
        MoveTo(20, 238); DrawString("\pPitch Decay ms");
        MoveTo(20, 282); DrawString("\pSine Attack ms");
        MoveTo(20, 326); DrawString("\pSine Hold ms");
    } else if (gCurrentPage == kPageSine2) {
        DrawString("\pSine page 2");
        MoveTo(20, 106); DrawString("\pSine Decay ms");
        MoveTo(20, 150); DrawString("\pTune Semi");
        MoveTo(20, 194); DrawString("\pFine Tune ct");
        MoveTo(20, 238); DrawString("\pFilter LP %");
        MoveTo(20, 282); DrawString("\pFilter HP %");
    } else {
        DrawString("\pExport page");
        MoveTo(20, 106); DrawString("\pTime ms");
        MoveTo(20, 122); DrawString("\pCheck Full Cycle to export one trigger loop.");
        MoveTo(20, 138); DrawString("\pWhen Full Cycle is on, Time ms is ignored.");
    }
}


void PaintSelectedValueRect(Rect *r)
{
    RGBColor oldFore;
    GetForeColor(&oldFore);
    RGBForeColor(&gSelectedBlue);
    PaintRect(r);
    RGBForeColor(&oldFore);
}

void DrawValueTextInRect(Rect *r, const char *text, int selected)
{
    short x = r->left + 4;
    short y = r->bottom - 4;
    RGBColor oldFore;
    RGBColor black = {0,0,0};

    if (selected) {
        GetForeColor(&oldFore);
        RGBForeColor(&black);
        MoveTo(x, y);
        DrawPascalFromCString(text);
        RGBForeColor(&oldFore);
    } else {
        MoveTo(x, y);
        DrawPascalFromCString(text);
    }
}

void UpdateStatusAreas(WindowPtr w)
{
    char temp[64];

    SetPort(w);
    EraseRect(&gNoiseStatusRect);
    EraseRect(&gBitStatusRect);
    EraseRect(&gTrigStatusRect);
    EraseRect(&gNoiseLPStatusRect);
    EraseRect(&gNoiseHPStatusRect);
    EraseRect(&gSineLPStatusRect);
    EraseRect(&gSineHPStatusRect);
    EraseRect(&gPauseStatusRect);

    if (gNoiseMode == 0) sprintf(temp, "%s", "White");
    else if (gNoiseMode == 1) sprintf(temp, "%s", "Bright");
    else sprintf(temp, "%s", "Held");
    MoveTo(gNoiseStatusRect.left, gNoiseStatusRect.bottom - 2); DrawPascalFromCString(temp);

    if (gBitDepthMode == 0) sprintf(temp, "%s", "16");
    else if (gBitDepthMode == 1) sprintf(temp, "%s", "8");
    else sprintf(temp, "%s", "4");
    MoveTo(gBitStatusRect.left, gBitStatusRect.bottom - 2); DrawPascalFromCString(temp);

    sprintf(temp, "%s", (gTrigMode == 0) ? "Free" : "Loop");
    MoveTo(gTrigStatusRect.left, gTrigStatusRect.bottom - 2); DrawPascalFromCString(temp);

    sprintf(temp, "%s", gNoiseLPOn ? "On" : "Off");
    MoveTo(gNoiseLPStatusRect.left, gNoiseLPStatusRect.bottom - 2); DrawPascalFromCString(temp);

    sprintf(temp, "%s", gNoiseHPOn ? "On" : "Off");
    MoveTo(gNoiseHPStatusRect.left, gNoiseHPStatusRect.bottom - 2); DrawPascalFromCString(temp);

    sprintf(temp, "%s", gSineLPOn ? "On" : "Off");
    MoveTo(gSineLPStatusRect.left, gSineLPStatusRect.bottom - 2); DrawPascalFromCString(temp);

    sprintf(temp, "%s", gSineHPOn ? "On" : "Off");
    MoveTo(gSineHPStatusRect.left, gSineHPStatusRect.bottom - 2); DrawPascalFromCString(temp);

    sprintf(temp, "%s", ((gFadeTarget == 0L) && (gFadeCurrent <= 0L || gPaused)) ? "Paused" : ((gFadeTarget == 0L) ? "Pausing" : "Running"));
    MoveTo(gPauseStatusRect.left, gPauseStatusRect.bottom - 2); DrawPascalFromCString(temp);
}



void DrawTickInRect(const Rect *r)
{
    Rect inner;

    inner = *r;
    InsetRect(&inner, 1, 1);
    PaintRect(&inner);
    PenPat(&qd.white);
    MoveTo(r->left + 3, r->top + 8);
    LineTo(r->left + 6, r->bottom - 4);
    LineTo(r->right - 3, r->top + 4);
    PenNormal();
}


void UpdateValueAreas(WindowPtr w)
{
    char temp[128];
    int i;

    EnsureSelectedParamVisible();
    SetPort(w);

    for (i = 0; i < kParamCount; i++) {
        long currentValue = GetParamDisplayValue(i);

        EraseRect(&gValueRects[i]);
        EraseRect(&gRandomToggleRects[i]);
        EraseRect(&gRandomMinRects[i]);
        EraseRect(&gRandomMaxRects[i]);

        if (!ParamVisibleOnPage(i, gCurrentPage)) continue;

        if ((gSelectedParam == i) && (gSelectedEditField == kEditValue)) PaintSelectedValueRect(&gValueRects[i]);
        else FrameRect(&gValueRects[i]);

        if ((i == kParamExportSecs) && (gCurrentPage == kPageExport) && (gExportMode != 0)) {
            sprintf(temp, "AUTO");
            DrawValueTextInRect(&gValueRects[i], temp, 0);
        } else {
            if ((gSelectedParam == i) && (gSelectedEditField == kEditValue) && (gTypedLen > 0)) {
                if (gCursorVisible) sprintf(temp, "%s|", gTypedDigits);
                else sprintf(temp, "%s", gTypedDigits);
            } else if ((gSelectedParam == i) && (gSelectedEditField == kEditValue)) {
                sprintf(temp, "%ld%s", currentValue, gCursorVisible ? "|" : "");
            } else {
                sprintf(temp, "%ld", currentValue);
            }
            DrawValueTextInRect(&gValueRects[i], temp, ((gSelectedParam == i) && (gSelectedEditField == kEditValue)));
        }

        if (i != kParamExportSecs) {
            FrameRect(&gRandomToggleRects[i]);
            if (gRandomEnabled[i]) DrawTickInRect(&gRandomToggleRects[i]);

            if ((gSelectedParam == i) && (gSelectedEditField == kEditRandMin)) PaintSelectedValueRect(&gRandomMinRects[i]);
            else FrameRect(&gRandomMinRects[i]);
            if ((gSelectedParam == i) && (gSelectedEditField == kEditRandMin) && (gTypedLen > 0)) {
                if (gCursorVisible) sprintf(temp, "%s|", gTypedDigits);
                else sprintf(temp, "%s", gTypedDigits);
            } else if ((gSelectedParam == i) && (gSelectedEditField == kEditRandMin)) {
                sprintf(temp, "%ld%s", gRandomMin[i], gCursorVisible ? "|" : "");
            } else {
                sprintf(temp, "%ld", gRandomMin[i]);
            }
            DrawValueTextInRect(&gRandomMinRects[i], temp, ((gSelectedParam == i) && (gSelectedEditField == kEditRandMin)));

            if ((gSelectedParam == i) && (gSelectedEditField == kEditRandMax)) PaintSelectedValueRect(&gRandomMaxRects[i]);
            else FrameRect(&gRandomMaxRects[i]);
            if ((gSelectedParam == i) && (gSelectedEditField == kEditRandMax) && (gTypedLen > 0)) {
                if (gCursorVisible) sprintf(temp, "%s|", gTypedDigits);
                else sprintf(temp, "%s", gTypedDigits);
            } else if ((gSelectedParam == i) && (gSelectedEditField == kEditRandMax)) {
                sprintf(temp, "%ld%s", gRandomMax[i], gCursorVisible ? "|" : "");
            } else {
                sprintf(temp, "%ld", gRandomMax[i]);
            }
            DrawValueTextInRect(&gRandomMaxRects[i], temp, ((gSelectedParam == i) && (gSelectedEditField == kEditRandMax)));
        }
    }

    EraseRect(&gHelpRect);
    EraseRect(&gPauseFadeValueRect);

    if (gCurrentPage == kPageMain) {
        FrameRect(&gPauseFadeValueRect);
        sprintf(temp, "%d", gPauseFadeMs);
        DrawValueTextInRect(&gPauseFadeValueRect, temp, 0);
        MoveTo(gHelpRect.left, gHelpRect.top + 12); DrawString("\pClick the checkbox to include or exclude a parameter from Randomize.");
        MoveTo(gHelpRect.left, gHelpRect.top + 28); DrawString("\pClick Min or Max to type the allowed random range for that parameter.");
        MoveTo(gHelpRect.left, gHelpRect.top + 44); DrawString("\pCurrent edit field follows the blinking cursor.");
    } else if (gCurrentPage == kPageNoise1) {
        MoveTo(gHelpRect.left, gHelpRect.top + 12); DrawString("\pNoise 1 groups the amp envelope with LP and HP filters.");
        MoveTo(gHelpRect.left, gHelpRect.top + 28); DrawString("\pUse the tick box to lock any control out of randomization.");
        MoveTo(gHelpRect.left, gHelpRect.top + 44); DrawString("\pMin and Max boxes use the same units as the main value.");
    } else if (gCurrentPage == kPageNoise2) {
        MoveTo(gHelpRect.left, gHelpRect.top + 12); DrawString("\pNoise 2 groups Sample & Hold with its envelope controls.");
        MoveTo(gHelpRect.left, gHelpRect.top + 28); DrawString("\pDepth sets how far the hold time moves from the base value.");
        MoveTo(gHelpRect.left, gHelpRect.top + 44); DrawString("\pRandom ranges let you keep it in a safer zone.");
    } else if (gCurrentPage == kPageLoopEnv) {
        MoveTo(gHelpRect.left, gHelpRect.top + 12); DrawString("\pNoise 3 groups Noise Loop with its envelope controls.");
        MoveTo(gHelpRect.left, gHelpRect.top + 28); DrawString("\pDepth sets how far the loop length moves from the base value.");
        MoveTo(gHelpRect.left, gHelpRect.top + 44); DrawString("\pTune the random range per parameter before hitting Randomize.");
    } else if (gCurrentPage == kPagePreset) {
        MoveTo(gHelpRect.left, gHelpRect.top + 12); DrawString("\pSave makes a fresh 6-character lowercase file name each time.");
        MoveTo(gHelpRect.left, gHelpRect.top + 28); DrawString("\pLoad shows the preset file name on this page after import.");
        MoveTo(gHelpRect.left, gHelpRect.top + 44); DrawString("\pYou can still see save and load status in the message area.");
    } else if (gCurrentPage == kPageSine1) {
        MoveTo(gHelpRect.left, gHelpRect.top + 12); DrawString("\pStart Phase resets the oscillator to the same cycle point.");
        MoveTo(gHelpRect.left, gHelpRect.top + 28); DrawString("\pPitch envelope and amp envelope are separate.");
        MoveTo(gHelpRect.left, gHelpRect.top + 44); DrawString("\pRandom Min and Max can be negative for tune pages.");
    } else if (gCurrentPage == kPageSine2) {
        MoveTo(gHelpRect.left, gHelpRect.top + 12); DrawString("\pSine Decay, tune, and sine filters live on this page.");
        MoveTo(gHelpRect.left, gHelpRect.top + 28); DrawString("\pS LP and S HP buttons only live on this page.");
        MoveTo(gHelpRect.left, gHelpRect.top + 44); DrawString("\pUse Sine 1 for phase and pitch settings.");
    }

    if (gQuickSaveButton != NULL) Draw1Control(gQuickSaveButton);
    if (gRandomLoadButton != NULL) Draw1Control(gRandomLoadButton);
    if (gRandomizeButton != NULL) Draw1Control(gRandomizeButton);
    if (gQuitButton != NULL) Draw1Control(gQuitButton);
}



void UpdateParamFromSlider(ControlHandle control)
{
    if (control == gMasterGainSlider) { gMasterGainPct = GetControlValue(gMasterGainSlider); SelectParam(kParamMasterGain); }
    else if (control == gNoiseGainSlider) { gNoiseGainPct = GetControlValue(gNoiseGainSlider); SelectParam(kParamNoiseGain); }
    else if (control == gSineGainSlider) { gSineGainPct = GetControlValue(gSineGainSlider); SelectParam(kParamSineGain); }
    else if (control == gTrigLoopSlider) { gTrigLoopMs = GetControlValue(gTrigLoopSlider); SelectParam(kParamTrigLoopMs); }
    else if (control == gSampleHoldSlider) { gRateHold = GetControlValue(gSampleHoldSlider); SelectParam(kParamSampleHold); }
    else if (control == gNoiseLoopSlider) { gNoiseLoopSize = LoopSliderToSamples(GetControlValue(gNoiseLoopSlider)); SelectParam(kParamNoiseLoop); }
    else if (control == gNoiseLPSlider) { gNoiseLPAmt = GetControlValue(gNoiseLPSlider); SelectParam(kParamNoiseLP); }
    else if (control == gNoiseHPSlider) { gNoiseHPAmt = GetControlValue(gNoiseHPSlider); SelectParam(kParamNoiseHP); }
    else if (control == gNoiseAttackSlider) { gNoiseAttackMs = GetControlValue(gNoiseAttackSlider); SelectParam(kParamNoiseAttack); }
    else if (control == gNoiseHoldSlider) { gNoiseHoldMs = GetControlValue(gNoiseHoldSlider); SelectParam(kParamNoiseHold); }
    else if (control == gNoiseDecaySlider) { gNoiseDecayMs = GetControlValue(gNoiseDecaySlider); SelectParam(kParamNoiseDecay); }
    else if (control == gLoopEnvAttackSlider) { gLoopEnvAttackMs = GetControlValue(gLoopEnvAttackSlider); SelectParam(kParamLoopEnvAttack); }
    else if (control == gLoopEnvHoldSlider) { gLoopEnvHoldMs = GetControlValue(gLoopEnvHoldSlider); SelectParam(kParamLoopEnvHold); }
    else if (control == gLoopEnvDecaySlider) { gLoopEnvDecayMs = GetControlValue(gLoopEnvDecaySlider); SelectParam(kParamLoopEnvDecay); }
    else if (control == gLoopEnvDepthSlider) { gLoopEnvDepthPct = GetControlValue(gLoopEnvDepthSlider); SelectParam(kParamLoopEnvDepth); }
    else if (control == gHoldEnvAttackSlider) { gHoldEnvAttackMs = GetControlValue(gHoldEnvAttackSlider); SelectParam(kParamHoldEnvAttack); }
    else if (control == gHoldEnvHoldSlider) { gHoldEnvHoldMs = GetControlValue(gHoldEnvHoldSlider); SelectParam(kParamHoldEnvHold); }
    else if (control == gHoldEnvDecaySlider) { gHoldEnvDecayMs = GetControlValue(gHoldEnvDecaySlider); SelectParam(kParamHoldEnvDecay); }
    else if (control == gHoldEnvDepthSlider) { gHoldEnvDepthPct = GetControlValue(gHoldEnvDepthSlider); SelectParam(kParamHoldEnvDepth); }
    else if (control == gSineFreqSlider) { gSineFreq = GetControlValue(gSineFreqSlider); SelectParam(kParamSineFreq); }
    else if (control == gSinePhaseSlider) { gSineStartPhaseDeg = GetControlValue(gSinePhaseSlider); SelectParam(kParamSinePhase); }
    else if (control == gPitchEnvSlider) { gPitchEnvAmt = GetControlValue(gPitchEnvSlider); SelectParam(kParamPitchEnv); }
    else if (control == gPitchDecaySlider) { gPitchDecayMs = GetControlValue(gPitchDecaySlider); SelectParam(kParamPitchDecay); }
    else if (control == gSineAttackSlider) { gSineAttackMs = GetControlValue(gSineAttackSlider); SelectParam(kParamSineAttack); }
    else if (control == gSineHoldSlider) { gSineHoldMs = GetControlValue(gSineHoldSlider); SelectParam(kParamSineHold); }
    else if (control == gAmpDecaySlider) { gAmpDecayMs = GetControlValue(gAmpDecaySlider); SelectParam(kParamSineDecay); }
    else if (control == gSineTuneWideSlider) { gSineTuneWide = GetControlValue(gSineTuneWideSlider); SelectParam(kParamSineTuneWide); }
    else if (control == gSineTuneFineSlider) { gSineTuneFine = GetControlValue(gSineTuneFineSlider); SelectParam(kParamSineTuneFine); }
    else if (control == gSineLPSlider) { gSineLPAmt = GetControlValue(gSineLPSlider); SelectParam(kParamSineLP); }
    else if (control == gSineHPSlider) { gSineHPAmt = GetControlValue(gSineHPSlider); SelectParam(kParamSineHP); }
    else if (control == gPauseFadeSlider) { gPauseFadeMs = GetControlValue(gPauseFadeSlider); UpdateFadeStep(); }

    RefreshAudioAfterParamChange();
    UpdateValueAreas(gWindow);
}



short GetSliderArrowStep(short modifiers)
{
    if (modifiers & shiftKey) return 10;
    return 1;
}

short GetSliderPageStep(short modifiers)
{
    if (modifiers & shiftKey) return 32;
    if (modifiers & optionKey) return 2;
    return 8;
}

pascal void SliderAction(ControlHandle control, short partCode)
{
    short value;
    short minValue;
    short maxValue;
    short arrowStep;
    short pageStep;

    value = GetControlValue(control);
    minValue = GetControlMinimum(control);
    maxValue = GetControlMaximum(control);
    arrowStep = GetSliderArrowStep(gSliderTrackModifiers);
    pageStep = GetSliderPageStep(gSliderTrackModifiers);

    if (partCode == inUpButton) {
        value = (short)(value - arrowStep);
        if (value < minValue) value = minValue;
        SetControlValue(control, value);
    } else if (partCode == inDownButton) {
        value = (short)(value + arrowStep);
        if (value > maxValue) value = maxValue;
        SetControlValue(control, value);
    } else if (partCode == inPageUp) {
        value = (short)(value - pageStep);
        if (value < minValue) value = minValue;
        SetControlValue(control, value);
    } else if (partCode == inPageDown) {
        value = (short)(value + pageStep);
        if (value > maxValue) value = maxValue;
        SetControlValue(control, value);
    }

    UpdateParamFromSlider(control);
}

void BlinkCursorMaybe(void)
{
    unsigned long now = TickCount();
    if ((now - gLastBlinkTick) >= 20) {
        gLastBlinkTick = now;
        gCursorVisible = !gCursorVisible;
        if (gWindow != NULL) UpdateValueAreas(gWindow);
    }
}

void ShowExportResult(const char *msg)
{
    strncpy(gLastExportMessage, msg, sizeof(gLastExportMessage) - 1);
    gLastExportMessage[sizeof(gLastExportMessage) - 1] = '\0';
    if (gWindow != NULL) UpdateValueAreas(gWindow);
}


void ResetExportState(ExportState *st)
{
    st->holdCounter = 0;
    st->heldNoise = 0.0;
    st->sinePhase = ((double)gSineStartPhaseDeg / 360.0) * TWO_PI;
    st->pitchEnvLevel = 0.0;
    st->noiseAmpEnvLevel = 1.0;
    st->ampEnvLevel = 1.0;
    st->loopEnvLevel = 0.0;
    st->holdEnvLevel = 0.0;
    st->noiseEnvStage = 0;
    st->sineEnvStage = 0;
    st->loopEnvStage = 0;
    st->holdEnvStage = 0;
    st->noiseEnvCounter = 0L;
    st->sineEnvCounter = 0L;
    st->loopEnvCounter = 0L;
    st->holdEnvCounter = 0L;
    st->sineLoopCounter = 0;
    st->noiseLPState = 0.0;
    st->noiseHPState = 0.0;
    st->noiseHPPrevInput = 0.0;
    st->sineLPState = 0.0;
    st->sineHPState = 0.0;
    st->sineHPPrevInput = 0.0;
    st->noiseReadPos = 0;

    if (gTrigMode == 1) {
        st->sinePhase = ((double)gSineStartPhaseDeg / 360.0) * TWO_PI;
        st->pitchEnvLevel = 1.0;
        TriggerAmpEnvelope(gNoiseAttackMs, gNoiseHoldMs, gNoiseDecayMs, &st->noiseEnvStage, &st->noiseEnvCounter, &st->noiseAmpEnvLevel);
        TriggerAmpEnvelope(gSineAttackMs, gSineHoldMs, gAmpDecayMs, &st->sineEnvStage, &st->sineEnvCounter, &st->ampEnvLevel);
        TriggerAmpEnvelope(gLoopEnvAttackMs, gLoopEnvHoldMs, gLoopEnvDecayMs, &st->loopEnvStage, &st->loopEnvCounter, &st->loopEnvLevel);
        TriggerAmpEnvelope(gHoldEnvAttackMs, gHoldEnvHoldMs, gHoldEnvDecayMs, &st->holdEnvStage, &st->holdEnvCounter, &st->holdEnvLevel);
    }
}



double NextRenderedSample(ExportState *st)
{
    double out = 0.0;
    double noiseX;
    double sineX;
    double sineFreqNow;
    double pitchDecayMul = PitchDecayMultiplier();
    long trigLoopSamples = TrigLoopSamples();
    long effectiveLoopSize;
    int effectiveHold;

    if (gTrigMode == 1) {
        if (st->sineLoopCounter <= 0) {
            st->sinePhase = ((double)gSineStartPhaseDeg / 360.0) * TWO_PI;
            while (st->sinePhase >= TWO_PI) st->sinePhase -= TWO_PI;
            while (st->sinePhase < 0.0) st->sinePhase += TWO_PI;
            st->pitchEnvLevel = 1.0;
            TriggerAmpEnvelope(gNoiseAttackMs, gNoiseHoldMs, gNoiseDecayMs, &st->noiseEnvStage, &st->noiseEnvCounter, &st->noiseAmpEnvLevel);
            TriggerAmpEnvelope(gSineAttackMs, gSineHoldMs, gAmpDecayMs, &st->sineEnvStage, &st->sineEnvCounter, &st->ampEnvLevel);
            TriggerAmpEnvelope(gLoopEnvAttackMs, gLoopEnvHoldMs, gLoopEnvDecayMs, &st->loopEnvStage, &st->loopEnvCounter, &st->loopEnvLevel);
            TriggerAmpEnvelope(gHoldEnvAttackMs, gHoldEnvHoldMs, gHoldEnvDecayMs, &st->holdEnvStage, &st->holdEnvCounter, &st->holdEnvLevel);
            st->sineLoopCounter = trigLoopSamples;
        }
        st->sineLoopCounter--;
    } else {
        st->pitchEnvLevel = 0.0;
        st->noiseAmpEnvLevel = 1.0;
        st->ampEnvLevel = 1.0;
        st->loopEnvLevel = 0.0;
        st->holdEnvLevel = 0.0;
        st->noiseEnvStage = 0;
        st->sineEnvStage = 0;
        st->loopEnvStage = 0;
        st->holdEnvStage = 0;
    }

    effectiveLoopSize = EffectiveNoiseLoopSize(st->loopEnvLevel);
    effectiveHold = EffectiveSampleHold(st->holdEnvLevel);

    noiseX = (double)gNoiseBuffer[st->noiseReadPos] / 32768.0;
    st->noiseReadPos++;
    if (st->noiseReadPos >= effectiveLoopSize) st->noiseReadPos = 0;

    if (effectiveHold <= 1) {
        st->heldNoise = noiseX;
    } else {
        if (st->holdCounter <= 0) {
            st->heldNoise = noiseX;
            st->holdCounter = effectiveHold;
        }
        noiseX = st->heldNoise;
        st->holdCounter--;
    }

    if (gTrigMode == 1) {
        noiseX = noiseX * st->noiseAmpEnvLevel;
        StepAmpEnvelope(gNoiseAttackMs, gNoiseHoldMs, gNoiseDecayMs, &st->noiseEnvStage, &st->noiseEnvCounter, &st->noiseAmpEnvLevel);
        StepAmpEnvelope(gLoopEnvAttackMs, gLoopEnvHoldMs, gLoopEnvDecayMs, &st->loopEnvStage, &st->loopEnvCounter, &st->loopEnvLevel);
        StepAmpEnvelope(gHoldEnvAttackMs, gHoldEnvHoldMs, gHoldEnvDecayMs, &st->holdEnvStage, &st->holdEnvCounter, &st->holdEnvLevel);
    }

    if (gNoiseLPOn && (gNoiseLPAmt > 0)) {
        double aLP = LPAlphaFromAmount(gNoiseLPAmt);
        st->noiseLPState = st->noiseLPState + (aLP * (noiseX - st->noiseLPState));
        noiseX = st->noiseLPState;
    }

    if (gNoiseHPOn && (gNoiseHPAmt > 0)) {
        double aHP = HPAlphaFromAmount(gNoiseHPAmt);
        st->noiseHPState = aHP * (st->noiseHPState + noiseX - st->noiseHPPrevInput);
        st->noiseHPPrevInput = noiseX;
        noiseX = st->noiseHPState;
    } else {
        st->noiseHPPrevInput = noiseX;
    }

    sineFreqNow = CurrentSineBaseFreq() + ((double)gPitchEnvAmt * st->pitchEnvLevel);
    sineX = sin(st->sinePhase);
    st->sinePhase += (TWO_PI * sineFreqNow) / (double)SAMPLE_RATE;
    while (st->sinePhase >= TWO_PI) st->sinePhase -= TWO_PI;

    if (gTrigMode == 1) {
        sineX = sineX * st->ampEnvLevel;
        st->pitchEnvLevel = st->pitchEnvLevel * pitchDecayMul;
        if (st->pitchEnvLevel < 0.0001) st->pitchEnvLevel = 0.0;
        StepAmpEnvelope(gSineAttackMs, gSineHoldMs, gAmpDecayMs, &st->sineEnvStage, &st->sineEnvCounter, &st->ampEnvLevel);
    }

    if (gSineLPOn && (gSineLPAmt > 0)) {
        double aLP = LPAlphaFromAmount(gSineLPAmt);
        st->sineLPState = st->sineLPState + (aLP * (sineX - st->sineLPState));
        sineX = st->sineLPState;
    }

    if (gSineHPOn && (gSineHPAmt > 0)) {
        double aHP = HPAlphaFromAmount(gSineHPAmt);
        st->sineHPState = aHP * (st->sineHPState + sineX - st->sineHPPrevInput);
        st->sineHPPrevInput = sineX;
        sineX = st->sineHPState;
    } else {
        st->sineHPPrevInput = sineX;
    }

    noiseX = QuantizeSampleForMode(noiseX, gBitDepthMode);
    sineX = QuantizeSampleForMode(sineX, gBitDepthMode);

    out = (noiseX * ((double)gNoiseGainPct / 100.0)) +
          (sineX * ((double)gSineGainPct / 100.0));
    out = out * ((double)gMasterGainPct / 100.0);

    if (out < -1.0) out = -1.0;
    if (out > 1.0) out = 1.0;
    return out;
}


OSErr WriteBytes(short refNum, const void *data, long count)
{
    long actual = count;
    return FSWrite(refNum, &actual, (Ptr)data);
}

OSErr WriteLE16(short refNum, unsigned short value)
{
    unsigned char b[2];
    b[0] = (unsigned char)(value & 0xFF);
    b[1] = (unsigned char)((value >> 8) & 0xFF);
    return WriteBytes(refNum, b, 2L);
}

OSErr WriteLE32(short refNum, unsigned long value)
{
    unsigned char b[4];
    b[0] = (unsigned char)(value & 0xFF);
    b[1] = (unsigned char)((value >> 8) & 0xFF);
    b[2] = (unsigned char)((value >> 16) & 0xFF);
    b[3] = (unsigned char)((value >> 24) & 0xFF);
    return WriteBytes(refNum, b, 4L);
}

short PromptForSaveFile(FSSpec *spec)
{
    StandardFileReply reply;
    Str255 prompt;
    Str255 defaultName;

    MakePString("Export Noise Lab WAV:", prompt);
    MakePString("NoiseLabExport.wav", defaultName);
    StandardPutFile(prompt, defaultName, &reply);
    if (!reply.sfGood) return 0;
    *spec = reply.sfFile;
    return 1;
}


void GenerateRandomPresetName(Str255 dst)
{
    static const char alphabet[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    char name[32];
    int i;

    for (i = 0; i < 6; i++) {
        name[i] = alphabet[(int)(NextRand() % 36UL)];
    }
    name[6] = '\0';
    strcat(name, ".json");
    MakePString(name, dst);
}

short PromptForPresetFile(FSSpec *spec)
{
    StandardFileReply reply;
    Str255 prompt;
    Str255 defaultName;

    MakePString("Save Noise Lab preset:", prompt);
    GenerateRandomPresetName(defaultName);
    StandardPutFile(prompt, defaultName, &reply);
    if (!reply.sfGood) return 0;
    *spec = reply.sfFile;
    return 1;
}

short PromptForLoadPreset(FSSpec *spec)
{
    StandardFileReply reply;
    StandardGetFile(NULL, 0, NULL, &reply);
    if (!reply.sfGood) return 0;
    *spec = reply.sfFile;
    return 1;
}

OSErr SavePresetFile(FSSpec *spec)
{
    OSErr err;
    short refNum;
    char line[256];

    FSpDelete(spec);
    err = FSpCreate(spec, 'R*ch', 'TEXT', smSystemScript);
    if ((err != noErr) && (err != dupFNErr)) return err;

    err = FSpOpenDF(spec, fsWrPerm, &refNum);
    if (err != noErr) return err;

    if (err == noErr) err = WriteBytes(refNum, "{\r\n", 3L);

#define WRITE_JSON_LINE(fmt, a, b) \
    do { \
        if (err == noErr) { \
            sprintf(line, fmt, a, b); \
            err = WriteBytes(refNum, line, (long)strlen(line)); \
        } \
    } while (0)

    WRITE_JSON_LINE("  \"master_gain_pct\": %d,\r\n", gMasterGainPct, 0);
    WRITE_JSON_LINE("  \"noise_gain_pct\": %d,\r\n", gNoiseGainPct, 0);
    WRITE_JSON_LINE("  \"sine_gain_pct\": %d,\r\n", gSineGainPct, 0);
    WRITE_JSON_LINE("  \"trig_loop_ms\": %d,\r\n", gTrigLoopMs, 0);
    WRITE_JSON_LINE("  \"sample_hold\": %d,\r\n", gRateHold, 0);
    WRITE_JSON_LINE("  \"noise_loop_samples\": %ld,\r\n", gNoiseLoopSize, 0);
    WRITE_JSON_LINE("  \"noise_lp_pct\": %d,\r\n", gNoiseLPAmt, 0);
    WRITE_JSON_LINE("  \"noise_hp_pct\": %d,\r\n", gNoiseHPAmt, 0);
    WRITE_JSON_LINE("  \"noise_attack_ms\": %d,\r\n", gNoiseAttackMs, 0);
    WRITE_JSON_LINE("  \"noise_hold_ms\": %d,\r\n", gNoiseHoldMs, 0);
    WRITE_JSON_LINE("  \"noise_decay_ms\": %d,\r\n", gNoiseDecayMs, 0);
    WRITE_JSON_LINE("  \"loop_env_attack_ms\": %d,\r\n", gLoopEnvAttackMs, 0);
    WRITE_JSON_LINE("  \"loop_env_hold_ms\": %d,\r\n", gLoopEnvHoldMs, 0);
    WRITE_JSON_LINE("  \"loop_env_decay_ms\": %d,\r\n", gLoopEnvDecayMs, 0);
    WRITE_JSON_LINE("  \"loop_env_depth_pct\": %d,\r\n", gLoopEnvDepthPct, 0);
    WRITE_JSON_LINE("  \"hold_env_attack_ms\": %d,\r\n", gHoldEnvAttackMs, 0);
    WRITE_JSON_LINE("  \"hold_env_hold_ms\": %d,\r\n", gHoldEnvHoldMs, 0);
    WRITE_JSON_LINE("  \"hold_env_decay_ms\": %d,\r\n", gHoldEnvDecayMs, 0);
    WRITE_JSON_LINE("  \"hold_env_depth_pct\": %d,\r\n", gHoldEnvDepthPct, 0);
    WRITE_JSON_LINE("  \"sine_freq\": %d,\r\n", gSineFreq, 0);
    WRITE_JSON_LINE("  \"sine_start_phase_deg\": %d,\r\n", gSineStartPhaseDeg, 0);
    WRITE_JSON_LINE("  \"pitch_env_hz\": %d,\r\n", gPitchEnvAmt, 0);
    WRITE_JSON_LINE("  \"pitch_decay_ms\": %d,\r\n", gPitchDecayMs, 0);
    WRITE_JSON_LINE("  \"sine_attack_ms\": %d,\r\n", gSineAttackMs, 0);
    WRITE_JSON_LINE("  \"sine_hold_ms\": %d,\r\n", gSineHoldMs, 0);
    WRITE_JSON_LINE("  \"sine_decay_ms\": %d,\r\n", gAmpDecayMs, 0);
    WRITE_JSON_LINE("  \"tune_semi\": %d,\r\n", gSineTuneWide - 36, 0);
    WRITE_JSON_LINE("  \"fine_tune_ct\": %d,\r\n", gSineTuneFine - 100, 0);
    WRITE_JSON_LINE("  \"sine_lp_pct\": %d,\r\n", gSineLPAmt, 0);
    WRITE_JSON_LINE("  \"sine_hp_pct\": %d,\r\n", gSineHPAmt, 0);
    WRITE_JSON_LINE("  \"export_milliseconds\": %d,\r\n", gExportMilliseconds, 0);
    WRITE_JSON_LINE("  \"export_mode\": %d,\r\n", gExportMode, 0);
    WRITE_JSON_LINE("  \"noise_mode\": %d,\r\n", gNoiseMode, 0);
    WRITE_JSON_LINE("  \"bit_depth_mode\": %d,\r\n", gBitDepthMode, 0);
    WRITE_JSON_LINE("  \"trig_mode\": %d,\r\n", gTrigMode, 0);
    WRITE_JSON_LINE("  \"noise_lp_on\": %d,\r\n", gNoiseLPOn, 0);
    WRITE_JSON_LINE("  \"noise_hp_on\": %d,\r\n", gNoiseHPOn, 0);
    WRITE_JSON_LINE("  \"sine_lp_on\": %d,\r\n", gSineLPOn, 0);
    WRITE_JSON_LINE("  \"sine_hp_on\": %d,\r\n", gSineHPOn, 0);

    if (err == noErr) {
        int i;
        char key[64];
        for (i = 0; (i < kParamCount) && (err == noErr); i++) {
            sprintf(key, "rand_enabled_%s", ParamPersistKey(i));
            sprintf(line, "  \"%s\": %d,\r\n", key, gRandomEnabled[i]);
            err = WriteBytes(refNum, line, (long)strlen(line));
            if (err != noErr) break;
            sprintf(key, "rand_min_%s", ParamPersistKey(i));
            sprintf(line, "  \"%s\": %ld,\r\n", key, gRandomMin[i]);
            err = WriteBytes(refNum, line, (long)strlen(line));
            if (err != noErr) break;
            sprintf(key, "rand_max_%s", ParamPersistKey(i));
            sprintf(line, "  \"%s\": %ld,\r\n", key, gRandomMax[i]);
            err = WriteBytes(refNum, line, (long)strlen(line));
        }
    }

    if (err == noErr) {
        sprintf(line, "  \"paused\": %d\r\n}\r\n", gPaused);
        err = WriteBytes(refNum, line, (long)strlen(line));
    }

#undef WRITE_JSON_LINE

    FSClose(refNum);
    return err;
}

const char *ParamPersistKey(int param)
{
    static const char *keys[kParamCount] = {
        "master_gain", "noise_gain", "sine_gain", "trig_loop",
        "sample_hold", "noise_loop", "noise_lp", "noise_hp",
        "noise_attack", "noise_hold", "noise_decay",
        "loop_attack", "loop_hold", "loop_decay", "loop_depth",
        "hold_attack", "hold_hold", "hold_decay", "hold_depth",
        "sine_freq", "sine_phase", "pitch_env", "pitch_decay",
        "sine_attack", "sine_hold", "sine_decay",
        "tune_semi", "tune_fine", "sine_lp", "sine_hp",
        "export_ms"
    };

    if ((param < 0) || (param >= kParamCount)) return "param";
    return keys[param];
}

int ExtractLongValue(const char *text, const char *key, long *value)
{
    char pattern[64];
    const char *p;

    sprintf(pattern, "\"%s\"", key);
    p = strstr(text, pattern);
    if (p == NULL) return 0;
    p = strchr(p, ':');
    if (p == NULL) return 0;
    p++;
    while ((*p == ' ') || (*p == '\t')) p++;
    return (sscanf(p, "%ld", value) == 1);
}

OSErr LoadPresetFile(FSSpec *spec)
{
    OSErr err;
    short refNum;
    long size;
    long actual;
    char *buffer;
    long value;

    err = FSpOpenDF(spec, fsRdPerm, &refNum);
    if (err != noErr) return err;

    err = GetEOF(refNum, &size);
    if (err != noErr) { FSClose(refNum); return err; }

    buffer = (char *)NewPtrClear(size + 1L);
    if (buffer == NULL) { FSClose(refNum); return memFullErr; }

    actual = size;
    err = FSRead(refNum, &actual, (Ptr)buffer);
    FSClose(refNum);
    if ((err != noErr) && (err != eofErr)) { DisposePtr((Ptr)buffer); return err; }
    buffer[actual] = '\0';

    if (ExtractLongValue(buffer, "master_gain_pct", &value)) gMasterGainPct = (int)value;
    if (ExtractLongValue(buffer, "noise_gain_pct", &value)) gNoiseGainPct = (int)value;
    if (ExtractLongValue(buffer, "sine_gain_pct", &value)) gSineGainPct = (int)value;
    if (ExtractLongValue(buffer, "trig_loop_ms", &value)) gTrigLoopMs = (int)value;
    if (ExtractLongValue(buffer, "sample_hold", &value)) gRateHold = (int)value;
    if (ExtractLongValue(buffer, "noise_loop_samples", &value)) gNoiseLoopSize = value;
    if (ExtractLongValue(buffer, "noise_lp_pct", &value)) gNoiseLPAmt = (int)value;
    if (ExtractLongValue(buffer, "noise_hp_pct", &value)) gNoiseHPAmt = (int)value;
    if (ExtractLongValue(buffer, "noise_attack_ms", &value)) gNoiseAttackMs = (int)value;
    if (ExtractLongValue(buffer, "noise_hold_ms", &value)) gNoiseHoldMs = (int)value;
    if (ExtractLongValue(buffer, "noise_decay_ms", &value)) gNoiseDecayMs = (int)value;
    if (ExtractLongValue(buffer, "loop_env_attack_ms", &value)) gLoopEnvAttackMs = (int)value;
    if (ExtractLongValue(buffer, "loop_env_hold_ms", &value)) gLoopEnvHoldMs = (int)value;
    if (ExtractLongValue(buffer, "loop_env_decay_ms", &value)) gLoopEnvDecayMs = (int)value;
    if (ExtractLongValue(buffer, "loop_env_depth_pct", &value)) gLoopEnvDepthPct = (int)value;
    if (ExtractLongValue(buffer, "hold_env_attack_ms", &value)) gHoldEnvAttackMs = (int)value;
    if (ExtractLongValue(buffer, "hold_env_hold_ms", &value)) gHoldEnvHoldMs = (int)value;
    if (ExtractLongValue(buffer, "hold_env_decay_ms", &value)) gHoldEnvDecayMs = (int)value;
    if (ExtractLongValue(buffer, "hold_env_depth_pct", &value)) gHoldEnvDepthPct = (int)value;
    if (ExtractLongValue(buffer, "sine_freq", &value)) gSineFreq = (int)value;
    if (ExtractLongValue(buffer, "sine_start_phase_deg", &value)) gSineStartPhaseDeg = (int)value;
    if (ExtractLongValue(buffer, "pitch_env_hz", &value)) gPitchEnvAmt = (int)value;
    if (ExtractLongValue(buffer, "pitch_decay_ms", &value)) gPitchDecayMs = (int)value;
    if (ExtractLongValue(buffer, "sine_attack_ms", &value)) gSineAttackMs = (int)value;
    if (ExtractLongValue(buffer, "sine_hold_ms", &value)) gSineHoldMs = (int)value;
    if (ExtractLongValue(buffer, "sine_decay_ms", &value)) gAmpDecayMs = (int)value;
    if (ExtractLongValue(buffer, "tune_semi", &value)) gSineTuneWide = (int)value + 36;
    if (ExtractLongValue(buffer, "fine_tune_ct", &value)) gSineTuneFine = (int)value + 100;
    if (ExtractLongValue(buffer, "sine_lp_pct", &value)) gSineLPAmt = (int)value;
    if (ExtractLongValue(buffer, "sine_hp_pct", &value)) gSineHPAmt = (int)value;
    if (ExtractLongValue(buffer, "export_milliseconds", &value)) gExportMilliseconds = (int)value;
    else if (ExtractLongValue(buffer, "export_seconds", &value)) gExportMilliseconds = (int)value * 1000;
    if (ExtractLongValue(buffer, "export_mode", &value)) gExportMode = (int)value;
    if (ExtractLongValue(buffer, "noise_mode", &value)) gNoiseMode = (int)value;
    if (ExtractLongValue(buffer, "bit_depth_mode", &value)) gBitDepthMode = (int)value;
    if (ExtractLongValue(buffer, "trig_mode", &value)) gTrigMode = (int)value;
    if (ExtractLongValue(buffer, "noise_lp_on", &value)) gNoiseLPOn = (int)value;
    if (ExtractLongValue(buffer, "noise_hp_on", &value)) gNoiseHPOn = (int)value;
    if (ExtractLongValue(buffer, "sine_lp_on", &value)) gSineLPOn = (int)value;
    if (ExtractLongValue(buffer, "sine_hp_on", &value)) gSineHPOn = (int)value;
    {
        int i;
        char key[64];
        for (i = 0; i < kParamCount; i++) {
            sprintf(key, "rand_enabled_%s", ParamPersistKey(i));
            if (ExtractLongValue(buffer, key, &value)) gRandomEnabled[i] = (int)value;
            sprintf(key, "rand_min_%s", ParamPersistKey(i));
            if (ExtractLongValue(buffer, key, &value)) gRandomMin[i] = value;
            sprintf(key, "rand_max_%s", ParamPersistKey(i));
            if (ExtractLongValue(buffer, key, &value)) gRandomMax[i] = value;
            ClampRandomRangeForParam(i);
        }
    }
    if (ExtractLongValue(buffer, "paused", &value)) gPaused = (int)value;

    if (gMasterGainPct < 0) gMasterGainPct = 0; if (gMasterGainPct > 100) gMasterGainPct = 100;
    if (gNoiseGainPct < 0) gNoiseGainPct = 0; if (gNoiseGainPct > 100) gNoiseGainPct = 100;
    if (gSineGainPct < 0) gSineGainPct = 0; if (gSineGainPct > 100) gSineGainPct = 100;
    if (gTrigLoopMs < 1) gTrigLoopMs = 1; if (gTrigLoopMs > 1500) gTrigLoopMs = 1500;
    if (gRateHold < 1) gRateHold = 1; if (gRateHold > MAX_RATE_HOLD) gRateHold = MAX_RATE_HOLD;
    if (gNoiseLoopSize < 1L) gNoiseLoopSize = 1L; if (gNoiseLoopSize > SAMPLE_RATE) gNoiseLoopSize = SAMPLE_RATE;
    if (gNoiseLPAmt < 0) gNoiseLPAmt = 0; if (gNoiseLPAmt > 100) gNoiseLPAmt = 100;
    if (gNoiseHPAmt < 0) gNoiseHPAmt = 0; if (gNoiseHPAmt > 100) gNoiseHPAmt = 100;
    if (gNoiseAttackMs < 0) gNoiseAttackMs = 0; if (gNoiseAttackMs > 2000) gNoiseAttackMs = 2000;
    if (gNoiseHoldMs < 0) gNoiseHoldMs = 0; if (gNoiseHoldMs > 2000) gNoiseHoldMs = 2000;
    if (gNoiseDecayMs < 0) gNoiseDecayMs = 0; if (gNoiseDecayMs > 2000) gNoiseDecayMs = 2000;
    if (gLoopEnvAttackMs < 0) gLoopEnvAttackMs = 0; if (gLoopEnvAttackMs > 2000) gLoopEnvAttackMs = 2000;
    if (gLoopEnvHoldMs < 0) gLoopEnvHoldMs = 0; if (gLoopEnvHoldMs > 2000) gLoopEnvHoldMs = 2000;
    if (gLoopEnvDecayMs < 0) gLoopEnvDecayMs = 0; if (gLoopEnvDecayMs > 2000) gLoopEnvDecayMs = 2000;
    if (gLoopEnvDepthPct < 0) gLoopEnvDepthPct = 0; if (gLoopEnvDepthPct > 100) gLoopEnvDepthPct = 100;
    if (gHoldEnvAttackMs < 0) gHoldEnvAttackMs = 0; if (gHoldEnvAttackMs > 2000) gHoldEnvAttackMs = 2000;
    if (gHoldEnvHoldMs < 0) gHoldEnvHoldMs = 0; if (gHoldEnvHoldMs > 2000) gHoldEnvHoldMs = 2000;
    if (gHoldEnvDecayMs < 0) gHoldEnvDecayMs = 0; if (gHoldEnvDecayMs > 2000) gHoldEnvDecayMs = 2000;
    if (gHoldEnvDepthPct < 0) gHoldEnvDepthPct = 0; if (gHoldEnvDepthPct > 100) gHoldEnvDepthPct = 100;
    if (gSineFreq < MIN_SINE_FREQ) gSineFreq = MIN_SINE_FREQ; if (gSineFreq > MAX_SINE_FREQ) gSineFreq = MAX_SINE_FREQ;
    if (gSineStartPhaseDeg < 0) gSineStartPhaseDeg = 0; if (gSineStartPhaseDeg > 359) gSineStartPhaseDeg = 359;
    if (gPitchEnvAmt < 0) gPitchEnvAmt = 0; if (gPitchEnvAmt > 4000) gPitchEnvAmt = 4000;
    if (gPitchDecayMs < 5) gPitchDecayMs = 5; if (gPitchDecayMs > 1500) gPitchDecayMs = 1500;
    if (gSineAttackMs < 0) gSineAttackMs = 0; if (gSineAttackMs > 2000) gSineAttackMs = 2000;
    if (gSineHoldMs < 0) gSineHoldMs = 0; if (gSineHoldMs > 2000) gSineHoldMs = 2000;
    if (gAmpDecayMs < 0) gAmpDecayMs = 0; if (gAmpDecayMs > 2000) gAmpDecayMs = 2000;
    if (gSineTuneWide < 0) gSineTuneWide = 0; if (gSineTuneWide > 72) gSineTuneWide = 72;
    if (gSineTuneFine < 0) gSineTuneFine = 0; if (gSineTuneFine > 200) gSineTuneFine = 200;
    if (gSineLPAmt < 0) gSineLPAmt = 0; if (gSineLPAmt > 100) gSineLPAmt = 100;
    if (gSineHPAmt < 0) gSineHPAmt = 0; if (gSineHPAmt > 100) gSineHPAmt = 100;
    if (gExportMilliseconds < 1) gExportMilliseconds = 1; if (gExportMilliseconds > 600000) gExportMilliseconds = 600000;
    if (gExportMode < 0) gExportMode = 0; if (gExportMode > 1) gExportMode = 1;
    if (gNoiseMode < 0) gNoiseMode = 0; if (gNoiseMode > 2) gNoiseMode = 2;
    if (gBitDepthMode < 0) gBitDepthMode = 0; if (gBitDepthMode > 2) gBitDepthMode = 2;
    if (gTrigMode < 0) gTrigMode = 0; if (gTrigMode > 1) gTrigMode = 1;
    gNoiseLPOn = gNoiseLPOn ? 1 : 0;
    gNoiseHPOn = gNoiseHPOn ? 1 : 0;
    gSineLPOn = gSineLPOn ? 1 : 0;
    gSineHPOn = gSineHPOn ? 1 : 0;
    gPaused = gPaused ? 1 : 0;

    DisposePtr((Ptr)buffer);
    gPendingNoiseBufferRegen = 1;
    CommitAudioParamsFromUI();
    SyncControlsFromParams();
    ResetAudioState();
    return noErr;
}

long GetExportSampleCount(void)
{
    long totalSamples;
    long exportMs;

    if (gExportMode == 0) exportMs = (long)gExportMilliseconds;
    else exportMs = (long)gTrigLoopMs;

    if (exportMs < 1L) exportMs = 1L;
    if (exportMs > 600000L) exportMs = 600000L;

    totalSamples = (exportMs * SAMPLE_RATE + 500L) / 1000L;
    if (totalSamples < 1L) totalSamples = 1L;
    return totalSamples;
}

OSErr ExportWavFile(FSSpec *spec, long totalSamples)
{
    OSErr err;
    short refNum;
    long dataBytes;
    long i;
    ExportState st;

    if (totalSamples < 1L) totalSamples = 1L;

    dataBytes = totalSamples * 2L;

    FSpDelete(spec);
    err = FSpCreate(spec, 'R*ch', 'WAVE', smSystemScript);
    if ((err != noErr) && (err != dupFNErr)) return err;

    err = FSpOpenDF(spec, fsWrPerm, &refNum);
    if (err != noErr) return err;

    err = WriteBytes(refNum, "RIFF", 4L);
    if (err == noErr) err = WriteLE32(refNum, (unsigned long)(36L + dataBytes));
    if (err == noErr) err = WriteBytes(refNum, "WAVE", 4L);
    if (err == noErr) err = WriteBytes(refNum, "fmt ", 4L);
    if (err == noErr) err = WriteLE32(refNum, 16UL);
    if (err == noErr) err = WriteLE16(refNum, 1);
    if (err == noErr) err = WriteLE16(refNum, 1);
    if (err == noErr) err = WriteLE32(refNum, SAMPLE_RATE);
    if (err == noErr) err = WriteLE32(refNum, SAMPLE_RATE * 2UL);
    if (err == noErr) err = WriteLE16(refNum, 2);
    if (err == noErr) err = WriteLE16(refNum, 16);
    if (err == noErr) err = WriteBytes(refNum, "data", 4L);
    if (err == noErr) err = WriteLE32(refNum, (unsigned long)dataBytes);

    ResetExportState(&st);
    for (i = 0; (i < totalSamples) && (err == noErr); i++) {
        double x = NextRenderedSample(&st);
        SInt16 sample = (SInt16)(x * 32767.0);
        err = WriteLE16(refNum, (unsigned short)sample);
    }

    FSClose(refNum);
    return err;
}

void DoExport(void)
{
    FSSpec spec;
    OSErr err;
    char msg[128];
    char name[64];
    long totalSamples;
    long exportMs;

    if (!PromptForSaveFile(&spec)) {
        ShowExportResult("Export cancelled.");
        return;
    }

    totalSamples = GetExportSampleCount();
    exportMs = (totalSamples * 1000L + (SAMPLE_RATE / 2)) / SAMPLE_RATE;

    err = ExportWavFile(&spec, totalSamples);
    if (err == noErr) {
        PStringToCString(spec.name, name);
        if (gExportMode == 0) sprintf(msg, "Exported %ld ms to %s", exportMs, name);
        else sprintf(msg, "Exported 1 cycle (%ld ms) to %s", exportMs, name);
        ShowExportResult(msg);
    } else {
        sprintf(msg, "Export failed. OSErr %d", (int)err);
        ShowExportResult(msg);
    }
}


OSErr GetSessionFolderSpec(FSSpec *folderSpec, long *folderDirID)
{
    OSErr err;
    short desktopVRefNum;
    long desktopDirID;
    long sessionDirID;
    unsigned long nowSecs;
    DateTimeRec nowRec;
    Str255 sessionName;
    char tempName[64];
    CInfoPBRec pb;

    err = FindFolder(kOnSystemDisk, kDesktopFolderType, kCreateFolder,
                     &desktopVRefNum, &desktopDirID);
    if (err != noErr) return err;

    GetDateTime(&nowSecs);
    SecondsToDate(nowSecs, &nowRec);
    sprintf(tempName, "session-%04d-%02d-%02d",
            (int)nowRec.year, (int)nowRec.month, (int)nowRec.day);
    MakePString(tempName, sessionName);

    err = FSMakeFSSpec(desktopVRefNum, desktopDirID, sessionName, folderSpec);
    if (err == fnfErr) {
        err = DirCreate(desktopVRefNum, desktopDirID, sessionName, &sessionDirID);
        if (err != noErr) return err;
        err = FSMakeFSSpec(desktopVRefNum, desktopDirID, sessionName, folderSpec);
        if (err != noErr) return err;
    } else if (err != noErr) {
        return err;
    }

    memset(&pb, 0, sizeof(pb));
    pb.dirInfo.ioNamePtr = folderSpec->name;
    pb.dirInfo.ioVRefNum = folderSpec->vRefNum;
    pb.dirInfo.ioDrDirID = folderSpec->parID;
    pb.dirInfo.ioFDirIndex = 0;
    err = PBGetCatInfoSync(&pb);
    if (err != noErr) return err;

    if (folderDirID != NULL) *folderDirID = pb.dirInfo.ioDrDirID;
    return noErr;
}

OSErr SavePresetToSession(FSSpec *savedSpec)
{
    OSErr err;
    FSSpec folderSpec;
    FSSpec fileSpec;
    long folderDirID;
    Str255 fileName;

    err = GetSessionFolderSpec(&folderSpec, &folderDirID);
    if (err != noErr) return err;

    GenerateRandomPresetName(fileName);
    err = FSMakeFSSpec(folderSpec.vRefNum, folderDirID, fileName, &fileSpec);
    if ((err != noErr) && (err != fnfErr)) return err;

    err = SavePresetFile(&fileSpec);
    if ((err == noErr) && (savedSpec != NULL)) *savedSpec = fileSpec;
    return err;
}

OSErr LoadRandomPresetFromSession(FSSpec *loadedSpec)
{
    OSErr err;
    FSSpec folderSpec;
    long folderDirID;
    CInfoPBRec pb;
    Str255 itemName;
    int matchCount = 0;
    FSSpec chosenSpec;

    err = GetSessionFolderSpec(&folderSpec, &folderDirID);
    if (err != noErr) return err;

    memset(&pb, 0, sizeof(pb));
    pb.hFileInfo.ioNamePtr = itemName;
    pb.hFileInfo.ioVRefNum = folderSpec.vRefNum;
    pb.hFileInfo.ioDirID = folderDirID;

    for (pb.hFileInfo.ioFDirIndex = 1; ; pb.hFileInfo.ioFDirIndex++) {
        err = PBGetCatInfoSync(&pb);
        if (err == fnfErr) break;
        if (err != noErr) return err;
        if ((pb.hFileInfo.ioFlAttrib & ioDirMask) == 0) {
            matchCount++;
            if ((matchCount == 1) || ((NextRand() % (unsigned long)matchCount) == 0)) {
                err = FSMakeFSSpec(folderSpec.vRefNum, folderDirID, itemName, &chosenSpec);
                if (err != noErr) return err;
            }
        }
    }

    if (matchCount <= 0) return fnfErr;

    err = LoadPresetFile(&chosenSpec);
    if ((err == noErr) && (loadedSpec != NULL)) *loadedSpec = chosenSpec;
    return err;
}

void DoQuickSavePreset(void)
{
    FSSpec spec;
    OSErr err;
    char msg[128];
    char name[64];

    err = SavePresetToSession(&spec);
    if (err == noErr) {
        PStringToCString(spec.name, name);
        strncpy(gLastLoadedPresetName, name, sizeof(gLastLoadedPresetName) - 1);
        gLastLoadedPresetName[sizeof(gLastLoadedPresetName) - 1] = '\0';
        sprintf(msg, "Quick-saved preset to %s", name);
        ShowExportResult(msg);
        if (gWindow != NULL) InvalRect(&gWindow->portRect);
    } else {
        sprintf(msg, "Quick save failed. OSErr %d", (int)err);
        ShowExportResult(msg);
    }
}

void DoRandomLoadPreset(void)
{
    FSSpec spec;
    OSErr err;
    char msg[128];
    char name[64];

    err = LoadRandomPresetFromSession(&spec);
    if (err == noErr) {
        PStringToCString(spec.name, name);
        strncpy(gLastLoadedPresetName, name, sizeof(gLastLoadedPresetName) - 1);
        gLastLoadedPresetName[sizeof(gLastLoadedPresetName) - 1] = '\0';
        sprintf(msg, "Random-loaded preset from %s", name);
        ShowExportResult(msg);
        if (gWindow != NULL) {
            UpdateStatusAreas(gWindow);
            UpdateValueAreas(gWindow);
        }
    } else if (err == fnfErr) {
        ShowExportResult("No session presets found to random-load.");
    } else {
        sprintf(msg, "Random load failed. OSErr %d", (int)err);
        ShowExportResult(msg);
    }
}

void DoSavePreset(void)
{
    FSSpec spec;
    OSErr err;
    char msg[128];
    char name[64];

    if (!PromptForPresetFile(&spec)) {
        ShowExportResult("Preset save cancelled.");
        return;
    }

    err = SavePresetFile(&spec);
    if (err == noErr) {
        PStringToCString(spec.name, name);
        strncpy(gLastLoadedPresetName, name, sizeof(gLastLoadedPresetName) - 1);
        gLastLoadedPresetName[sizeof(gLastLoadedPresetName) - 1] = '\0';
        sprintf(msg, "Saved preset to %s", name);
        ShowExportResult(msg);
        if (gWindow != NULL) InvalRect(&gWindow->portRect);
    } else {
        sprintf(msg, "Preset save failed. OSErr %d", (int)err);
        ShowExportResult(msg);
    }
}

void DoLoadPreset(void)
{
    FSSpec spec;
    OSErr err;
    char msg[128];
    char name[64];

    if (!PromptForLoadPreset(&spec)) {
        ShowExportResult("Preset import cancelled.");
        return;
    }

    err = LoadPresetFile(&spec);
    if (err == noErr) {
        PStringToCString(spec.name, name);
        strncpy(gLastLoadedPresetName, name, sizeof(gLastLoadedPresetName) - 1);
        gLastLoadedPresetName[sizeof(gLastLoadedPresetName) - 1] = '\0';
        sprintf(msg, "Imported preset from %s", name);
        ShowExportResult(msg);
        if (gWindow != NULL) {
            UpdateStatusAreas(gWindow);
            UpdateValueAreas(gWindow);
        }
    } else {
        sprintf(msg, "Preset import failed. OSErr %d", (int)err);
        ShowExportResult(msg);
    }
}


void DoMouseDown(EventRecord *event)
{
    WindowPtr whichWindow;
    short part;

    part = FindWindow(event->where, &whichWindow);
    switch (part) {
        case inMenuBar:
            break;
        case inSysWindow:
            SystemClick(event, whichWindow);
            break;
        case inDrag:
        {
            Rect dragRect;
            SetRect(&dragRect, 4, 24, qd.screenBits.bounds.right - 4, qd.screenBits.bounds.bottom - 4);
            DragWindow(whichWindow, event->where, &dragRect);
        }
        break;
        case inGoAway:
            if (TrackGoAway(whichWindow, event->where)) gRunning = 0;
            break;
        case inZoomIn:
        case inZoomOut:
            if (TrackBox(whichWindow, event->where, part)) {
                ZoomWindow(whichWindow, part, false);
                LayoutUI();
                InvalRect(&whichWindow->portRect);
            }
            break;
        case inContent:
            if (whichWindow != FrontWindow()) {
                SelectWindow(whichWindow);
            } else if (whichWindow == gWindow) {
                ControlHandle ctrl = NULL;
                short ctlPart;
                Point localPt;
                int i;

                SetPort(whichWindow);
                localPt = event->where;
                GlobalToLocal(&localPt);

                for (i = 0; i < kParamCount; i++) {
                    if (!ParamVisibleOnPage(i, gCurrentPage)) continue;
                    if (PtInRect(localPt, &gRandomToggleRects[i])) {
                        if (i != kParamExportSecs) {
                            gRandomEnabled[i] = !gRandomEnabled[i];
                            UpdateValueAreas(gWindow);
                        }
                        return;
                    }
                    if (PtInRect(localPt, &gValueRects[i])) {
                        SelectParamField(i, kEditValue);
                        return;
                    }
                    if (PtInRect(localPt, &gRandomMinRects[i])) {
                        SelectParamField(i, kEditRandMin);
                        return;
                    }
                    if (PtInRect(localPt, &gRandomMaxRects[i])) {
                        SelectParamField(i, kEditRandMax);
                        return;
                    }
                }

                ctlPart = FindControl(localPt, whichWindow, &ctrl);

                if ((ctrl != NULL) && (ctrl == gMainPageButton)) {
                    if (TrackControl(ctrl, localPt, NULL) != 0) SetCurrentPage(kPageMain);
                } else if ((ctrl != NULL) && (ctrl == gNoisePageButton)) {
                    if (TrackControl(ctrl, localPt, NULL) != 0) SetCurrentPage(kPageNoise1);
                } else if ((ctrl != NULL) && (ctrl == gNoise2PageButton)) {
                    if (TrackControl(ctrl, localPt, NULL) != 0) SetCurrentPage(kPageNoise2);
                } else if ((ctrl != NULL) && (ctrl == gLoopEnvPageButton)) {
                    if (TrackControl(ctrl, localPt, NULL) != 0) SetCurrentPage(kPageLoopEnv);
                } else if ((ctrl != NULL) && (ctrl == gPresetPageButton)) {
                    if (TrackControl(ctrl, localPt, NULL) != 0) SetCurrentPage(kPagePreset);
                } else if ((ctrl != NULL) && (ctrl == gSinePageButton)) {
                    if (TrackControl(ctrl, localPt, NULL) != 0) SetCurrentPage(kPageSine1);
                } else if ((ctrl != NULL) && (ctrl == gSine2PageButton)) {
                    if (TrackControl(ctrl, localPt, NULL) != 0) SetCurrentPage(kPageSine2);
                } else if ((ctrl != NULL) && (ctrl == gExportPageButton)) {
                    if (TrackControl(ctrl, localPt, NULL) != 0) SetCurrentPage(kPageExport);
                } else if ((ctrl != NULL) &&
                    ((ctrl == gMasterGainSlider) || (ctrl == gNoiseGainSlider) || (ctrl == gSineGainSlider) ||
                     (ctrl == gTrigLoopSlider) || (ctrl == gSampleHoldSlider) || (ctrl == gNoiseLoopSlider) ||
                     (ctrl == gNoiseLPSlider) || (ctrl == gNoiseHPSlider) || (ctrl == gNoiseAttackSlider) ||
                     (ctrl == gNoiseHoldSlider) || (ctrl == gNoiseDecaySlider) || (ctrl == gLoopEnvAttackSlider) ||
                     (ctrl == gLoopEnvHoldSlider) || (ctrl == gLoopEnvDecaySlider) || (ctrl == gLoopEnvDepthSlider) ||
                     (ctrl == gHoldEnvAttackSlider) || (ctrl == gHoldEnvHoldSlider) || (ctrl == gHoldEnvDecaySlider) ||
                     (ctrl == gHoldEnvDepthSlider) || (ctrl == gSineFreqSlider) || (ctrl == gSinePhaseSlider) ||
                     (ctrl == gPitchEnvSlider) || (ctrl == gPitchDecaySlider) || (ctrl == gSineAttackSlider) ||
                     (ctrl == gSineHoldSlider) || (ctrl == gAmpDecaySlider) || (ctrl == gSineTuneWideSlider) ||
                     (ctrl == gSineTuneFineSlider) || (ctrl == gSineLPSlider) || (ctrl == gSineHPSlider) ||
                     (ctrl == gPauseFadeSlider)) &&
                    (ctlPart != 0)) {
                    gSliderTrackModifiers = event->modifiers;
                    TrackControl(ctrl, localPt, gSliderActionUPP);
                    gSliderTrackModifiers = 0;
                    UpdateParamFromSlider(ctrl);
                } else if ((ctrl != NULL) && (ctrl == gNoiseModeButton)) {
                    if (TrackControl(ctrl, localPt, NULL) != 0) {
                        gNoiseMode++; if (gNoiseMode > 2) gNoiseMode = 0;
                        gPendingNoiseBufferRegen = 1; RefreshAudioAfterParamChange();
                        UpdateStatusAreas(gWindow); UpdateValueAreas(gWindow);
                    }
                } else if ((ctrl != NULL) && (ctrl == gBitDepthButton)) {
                    if (TrackControl(ctrl, localPt, NULL) != 0) {
                        gBitDepthMode++; if (gBitDepthMode > 2) gBitDepthMode = 0;
                        UpdateStatusAreas(gWindow); UpdateValueAreas(gWindow);
                    }
                } else if ((ctrl != NULL) && (ctrl == gTrigButton)) {
                    if (TrackControl(ctrl, localPt, NULL) != 0) {
                        gTrigMode = !gTrigMode; CommitAudioParamsFromUI(); ResetAudioState();
                        UpdateStatusAreas(gWindow); UpdateValueAreas(gWindow);
                    }
                } else if ((ctrl != NULL) && (ctrl == gNoiseLPButton)) {
                    if (TrackControl(ctrl, localPt, NULL) != 0) {
                        gNoiseLPOn = !gNoiseLPOn; RefreshAudioAfterParamChange();
                        UpdateStatusAreas(gWindow); UpdateValueAreas(gWindow);
                    }
                } else if ((ctrl != NULL) && (ctrl == gNoiseHPButton)) {
                    if (TrackControl(ctrl, localPt, NULL) != 0) {
                        gNoiseHPOn = !gNoiseHPOn; RefreshAudioAfterParamChange();
                        UpdateStatusAreas(gWindow); UpdateValueAreas(gWindow);
                    }
                } else if ((ctrl != NULL) && (ctrl == gSineLPButton)) {
                    if (TrackControl(ctrl, localPt, NULL) != 0) {
                        gSineLPOn = !gSineLPOn; RefreshAudioAfterParamChange();
                        UpdateStatusAreas(gWindow); UpdateValueAreas(gWindow);
                    }
                } else if ((ctrl != NULL) && (ctrl == gSineHPButton)) {
                    if (TrackControl(ctrl, localPt, NULL) != 0) {
                        gSineHPOn = !gSineHPOn; RefreshAudioAfterParamChange();
                        UpdateStatusAreas(gWindow); UpdateValueAreas(gWindow);
                    }
                } else if ((ctrl != NULL) && (ctrl == gPauseButton)) {
                    if (TrackControl(ctrl, localPt, NULL) != 0) {
                        if ((gFadeTarget == 0L) || (gFadeCurrent == 0L)) StartPauseFade(0);
                        else StartPauseFade(1);
                        UpdateStatusAreas(gWindow); UpdateValueAreas(gWindow);
                    }
                } else if ((ctrl != NULL) && (ctrl == gExportModeButton)) {
                    if (TrackControl(ctrl, localPt, NULL) != 0) {
                        gExportMode = !gExportMode;
                        if ((gExportMode != 0) && (gSelectedParam == kParamExportSecs)) ClearTypedValue();
                        UpdateExportModeUI();
                        DrawStaticUI(gWindow);
                        DrawControls(gWindow);
                        UpdateStatusAreas(gWindow);
                        UpdateValueAreas(gWindow);
                    }
                } else if ((ctrl != NULL) && (ctrl == gExportButton)) {
                    if (TrackControl(ctrl, localPt, NULL) != 0) DoExport();
                } else if ((ctrl != NULL) && (ctrl == gSavePresetButton)) {
                    if (TrackControl(ctrl, localPt, NULL) != 0) DoSavePreset();
                } else if ((ctrl != NULL) && (ctrl == gLoadPresetButton)) {
                    if (TrackControl(ctrl, localPt, NULL) != 0) DoLoadPreset();
                } else if ((ctrl != NULL) && (ctrl == gQuickSaveButton)) {
                    if (TrackControl(ctrl, localPt, NULL) != 0) DoQuickSavePreset();
                } else if ((ctrl != NULL) && (ctrl == gRandomLoadButton)) {
                    if (TrackControl(ctrl, localPt, NULL) != 0) DoRandomLoadPreset();
                } else if ((ctrl != NULL) && (ctrl == gRandomizeButton)) {
                    if (TrackControl(ctrl, localPt, NULL) != 0) RandomizeSliderParams();
                } else if ((ctrl != NULL) && (ctrl == gQuitButton)) {
                    if (TrackControl(ctrl, localPt, NULL) != 0) gRunning = 0;
                }
            }
            break;
    }
}


void DoUpdate(EventRecord *event)
{
    WindowPtr w = (WindowPtr)event->message;
    BeginUpdate(w);
    DrawStaticUI(w);
    DrawControls(w);
    UpdateStatusAreas(w);
    UpdateValueAreas(w);
    EndUpdate(w);
}

void DoActivate(EventRecord *event)
{
    WindowPtr w = (WindowPtr)event->message;
    if (w == gWindow) {
        SetPort(w);
        DrawControls(w);
        UpdateStatusAreas(w);
        UpdateValueAreas(w);
    }
}

int main(void)
{
    EventRecord event;
    char c;

    MaxApplZone();
    MoreMasters();
    MoreMasters();

    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);
    InitCursor();

    ClearTypedValue();
    InitRandomizationRanges();
    gPendingNoiseBufferRegen = 1;
    CommitAudioParamsFromUI();
    ResetAudioState();
    gFadeCurrent = 0L;
    gFadeTarget = 0L;
    gPaused = 1;
    UpdateFadeStep();

    InitUI();
    InitAudio();
    gLastBlinkTick = TickCount();

    while (gRunning) {
        if (WaitNextEvent(everyEvent, &event, 6, NULL)) {
            switch (event.what) {
                case mouseDown:
                    DoMouseDown(&event);
                    break;
                case updateEvt:
                    DoUpdate(&event);
                    break;
                case activateEvt:
                    DoActivate(&event);
                    break;
                case keyDown:
                case autoKey:
                    c = (char)(event.message & charCodeMask);
                    if ((c == 27) || (c == 'q') || (c == 'Q')) {
                        gRunning = 0;
                    } else if ((c == 'p') || (c == 'P')) {
                        if ((gFadeTarget == 0L) || (gFadeCurrent == 0L)) StartPauseFade(0);
                        else StartPauseFade(1);
                        UpdateStatusAreas(gWindow); UpdateValueAreas(gWindow);
                    } else if ((c >= '0') && (c <= '9')) {
                        AppendTypedDigit(c);
                        gCursorVisible = 1;
                        UpdateValueAreas(gWindow);
                    } else if ((c == '-') && (gTypedLen == 0) &&
                               (ParamDisplayMin(gSelectedParam) < 0L)) {
                        AppendTypedDigit(c);
                        gCursorVisible = 1;
                        UpdateValueAreas(gWindow);
                    } else if ((c == 8) || (c == 127)) {
                        RemoveTypedDigit();
                        gCursorVisible = 1;
                        UpdateValueAreas(gWindow);
                    } else if ((c == 3) || (c == 13)) {
                        ApplyTypedValue();
                    }
                    break;
            }
        } else {
            BlinkCursorMaybe();
        }
    }

    ShutdownAudio();
    return 0;
}
