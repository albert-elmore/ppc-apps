
#include <stdio.h>
#include <Sound.h>
#include <OSUtils.h>
#include <Memory.h>
#include <Windows.h>
#include <Controls.h>
#include <Events.h>
#include <Dialogs.h>
#include <Quickdraw.h>
#include <Math.h>
#include <string.h>

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

#define SAMPLE_RATE          11025
#define FRAMES_PER_BUFFER    1024
#define NUM_OSC              40
#define OSCS_PER_CHANNEL     20
#define OSCS_PER_PAGE        10
#define NUM_OSC_PAGES        4
#define TWO_PI               6.28318530717958647692

#define CHANGE_SLIDER_MAX    1000
#define NOTE_MIN_HZ          40
#define NOTE_MAX_HZ          2000
#define MANUAL_NOTE_MAX_HZ   9999
#define GAIN_MAX_PCT         100
#define NOTES_PER_EVENT_MAX  NUM_OSC
#define FADE_MS_MAX          20000
#define MASTER_FADE_SCALE    1000000L

/* =========================
   GLOBAL AUDIO STATE
   ========================= */

static SndChannelPtr gChan = NULL;
static SndDoubleBufferHeader gHeader;
static SndDoubleBufferPtr gBufA = NULL;
static SndDoubleBufferPtr gBufB = NULL;
static SndDoubleBackUPP gDoubleBackUPP = NULL;

/* UI globals */
static WindowPtr gWindow = NULL;
static ControlHandle gChangeSlider = NULL;
static ControlHandle gLowSlider = NULL;
static ControlHandle gHighSlider = NULL;
static ControlHandle gGainSlider = NULL;
static ControlHandle gMinNotesSlider = NULL;
static ControlHandle gMaxNotesSlider = NULL;
static ControlHandle gFadeSlider = NULL;
static ControlHandle gFreezeCheckbox = NULL;
static ControlHandle gStepButton = NULL;
static ControlHandle gPauseButton = NULL;
static ControlHandle gQuitButton = NULL;
static ControlHandle gPageMainButton = NULL;
static ControlHandle gPageOscButtons[NUM_OSC_PAGES] = { NULL, NULL, NULL, NULL };
static ControlHandle gOscMuteCheckbox[NUM_OSC];
static ControlActionUPP gSliderActionUPP = NULL;
static short gSliderTrackModifiers = 0;

/* display rects */
static Rect gChangeValueRect;
static Rect gLowValueRect;
static Rect gHighValueRect;
static Rect gGainValueRect;
static Rect gMinNotesValueRect;
static Rect gMaxNotesValueRect;
static Rect gFadeValueRect;
static Rect gStatusRect;
static Rect gHelpRect;
static Rect gOscLabelRects[NUM_OSC];
static Rect gOscValueRects[NUM_OSC];

/* synth state */
static int gRunning = 1;
static unsigned long gRandState = 1;
static double gPhase[NUM_OSC];
static double gFreqs[NUM_OSC];
static long gOscAge[NUM_OSC];
static int gOscMuted[NUM_OSC];
static volatile int gOscDirty = 1;

static const double gInitialFreqs[NUM_OSC] = {
    110, 123, 130, 146, 164,
    174, 196, 220, 246, 261,
    293, 329, 349, 392, 440,
    493, 523, 587, 659, 698,
    110, 123, 130, 146, 164,
    174, 196, 220, 246, 261,
    293, 329, 349, 392, 440,
    493, 523, 587, 659, 698
};

static int gChangeSliderValue = 300;
static int gLowFreq = 80;
static int gHighFreq = 1200;
static int gGainPct = 35;
static int gMinNotesPerEvent = 1;
static int gMaxNotesPerEvent = 4;
static int gFadeMs = 250;
static volatile int gFreezeNotes = 0;
static volatile int gStepRequested = 0;
static volatile int gAudioPaused = 0;
static int gSelectedOsc = -1;
static int gCurrentPage = 0;
static char gOscEditBuffer[32] = "";
static int gOscEditActive = 0;
static int gOscEditLen = 0;

static volatile long gSamplesUntilChange = 0;
static volatile long gFadeCurrent = MASTER_FADE_SCALE;
static volatile long gFadeTarget = MASTER_FADE_SCALE;
static volatile long gFadeStep = MASTER_FADE_SCALE;

/* =========================
   PROTOTYPES
   ========================= */

unsigned long NextRand(void);
void SeedRand(unsigned long seed);
long RandRange(long min, long max);
double RandUnit(void);
int EffectiveLowFreq(void);
int EffectiveHighFreq(void);
int EffectiveMinNotesPerEvent(void);
int EffectiveMaxNotesPerEvent(void);
double RandFreqInRange(void);
long ChangeSliderToSamples(short sliderValue);
void ScheduleNextChange(void);
void ApplyFrequencyChange(int oscIndex, double newFreq);
void ForceRandomChange(void);
void InitFrequencies(void);
void FillBuffer(SndDoubleBufferPtr db);
pascal void MyDoubleBackProc(SndChannelPtr chan, SndDoubleBufferPtr db);

void InitAudio(void);
void ShutdownAudio(void);
void StartPauseFade(int pauseNow);
void UpdateFadeStep(void);

void MakePString(const char *src, Str255 dst);
void DrawPascalFromCString(const char *src);
void LayoutUI(void);
void SyncControlsFromParams(void);
void UpdatePageVisibility(void);
void InitUI(void);
void DrawStaticUI(WindowPtr w);
void UpdateValueAreas(WindowPtr w);
void UpdateStatusArea(WindowPtr w);
void UpdateOscillatorAreas(WindowPtr w);
void UpdateParamFromSlider(ControlHandle control);
short GetSliderArrowStep(short modifiers);
short GetSliderPageStep(short modifiers);
pascal void SliderAction(ControlHandle control, short partCode);
void DoMouseDown(EventRecord *event);
void DoUpdate(EventRecord *event);
void DoActivate(EventRecord *event);
void HandleKey(EventRecord *event);
int OscIndexAtPoint(Point localPt);
void SelectOscillator(int oscIndex);
void RefreshSelectedOscEditBuffer(void);
void CommitSelectedOscillatorEdit(void);
int PageOscStartIndex(int page);
int PageOscEndIndex(int page);

/* =========================
   RANDOM / PARAMS
   ========================= */

unsigned long NextRand(void)
{
    gRandState = (gRandState * 1664525UL) + 1013904223UL;
    return gRandState;
}

void SeedRand(unsigned long seed)
{
    if (seed == 0) {
        seed = 1;
    }
    gRandState = seed;
}

long RandRange(long min, long max)
{
    unsigned long r;
    long span;

    if (max <= min) {
        return min;
    }

    span = max - min + 1;
    r = NextRand();
    return min + (long)(r % (unsigned long)span);
}

double RandUnit(void)
{
    unsigned long r;

    r = NextRand() & 0x7FFFFFFFUL;
    return (double)r / 2147483647.0;
}

int EffectiveLowFreq(void)
{
    if (gLowFreq < gHighFreq) {
        return gLowFreq;
    }
    return gHighFreq;
}

int EffectiveHighFreq(void)
{
    if (gHighFreq > gLowFreq) {
        return gHighFreq;
    }
    return gLowFreq;
}

int EffectiveMinNotesPerEvent(void)
{
    if (gMinNotesPerEvent < gMaxNotesPerEvent) {
        return gMinNotesPerEvent;
    }
    return gMaxNotesPerEvent;
}

int EffectiveMaxNotesPerEvent(void)
{
    if (gMaxNotesPerEvent > gMinNotesPerEvent) {
        return gMaxNotesPerEvent;
    }
    return gMinNotesPerEvent;
}

double RandFreqInRange(void)
{
    int low;
    int high;
    double unit;

    low = EffectiveLowFreq();
    high = EffectiveHighFreq();
    unit = RandUnit();

    return (double)low + (unit * (double)(high - low));
}

long ChangeSliderToSamples(short sliderValue)
{
    long ms;

    if (sliderValue <= 0) {
        return 0;
    }

    if (sliderValue > CHANGE_SLIDER_MAX) {
        sliderValue = CHANGE_SLIDER_MAX;
    }

    /* left = slow, right = fast */
    ms = 5000L - (((long)(sliderValue - 1) * 4950L) / (CHANGE_SLIDER_MAX - 1));
    if (ms < 50L) {
        ms = 50L;
    }

    return (ms * SAMPLE_RATE) / 1000L;
}

void ScheduleNextChange(void)
{
    gSamplesUntilChange = ChangeSliderToSamples((short)gChangeSliderValue);
}

void ApplyFrequencyChange(int oscIndex, double newFreq)
{
    int i;

    if (oscIndex < 0 || oscIndex >= NUM_OSC) {
        return;
    }

    if (newFreq < 1.0) {
        newFreq = 1.0;
    }
    if (newFreq > (double)MANUAL_NOTE_MAX_HZ) {
        newFreq = (double)MANUAL_NOTE_MAX_HZ;
    }

    gFreqs[oscIndex] = newFreq;
    gOscAge[oscIndex] = 0;
    gOscDirty = 1;

    for (i = 0; i < NUM_OSC; i++) {
        if (i != oscIndex && gOscAge[i] < 2000000000L) {
            gOscAge[i]++;
        }
    }
}

void ForceRandomChange(void)
{
    int count;
    int i;
    int osc;
    int minNotes;
    int maxNotes;
    int used[NUM_OSC];
    int oldestIndex;
    long oldestAge;

    if (gFreezeNotes) {
        ScheduleNextChange();
        return;
    }

    minNotes = EffectiveMinNotesPerEvent();
    maxNotes = EffectiveMaxNotesPerEvent();
    count = (int)RandRange((long)minNotes, (long)maxNotes);

    if (count < 1) {
        count = 1;
    }
    if (count > NUM_OSC) {
        count = NUM_OSC;
    }

    for (i = 0; i < NUM_OSC; i++) {
        used[i] = 0;
    }

    oldestIndex = 0;
    oldestAge = gOscAge[0];
    for (i = 1; i < NUM_OSC; i++) {
        if (gOscAge[i] > oldestAge) {
            oldestAge = gOscAge[i];
            oldestIndex = i;
        }
    }

    ApplyFrequencyChange(oldestIndex, RandFreqInRange());
    used[oldestIndex] = 1;

    for (i = 1; i < count; i++) {
        int tries;
        tries = 0;
        do {
            osc = (int)RandRange(0, NUM_OSC - 1);
            tries++;
        } while (used[osc] && tries < 128);

        if (!used[osc]) {
            used[osc] = 1;
            ApplyFrequencyChange(osc, RandFreqInRange());
        }
    }

    ScheduleNextChange();
}

void InitFrequencies(void)
{
    int i;

    for (i = 0; i < NUM_OSC; i++) {
        gFreqs[i] = gInitialFreqs[i];
        gPhase[i] = 0.0;
        gOscAge[i] = i;
        gOscMuted[i] = 0;
    }

    gOscDirty = 1;
    ScheduleNextChange();
}

void UpdateFadeStep(void)
{
    long fadeSamples;

    fadeSamples = ((long)gFadeMs * SAMPLE_RATE) / 1000L;
    if (fadeSamples <= 0) {
        gFadeStep = MASTER_FADE_SCALE;
    } else {
        gFadeStep = MASTER_FADE_SCALE / fadeSamples;
        if (gFadeStep < 1) {
            gFadeStep = 1;
        }
    }
}

void StartPauseFade(int pauseNow)
{
    UpdateFadeStep();

    if (pauseNow) {
        gFadeTarget = 0;
    } else {
        gAudioPaused = 0;
        gFadeTarget = MASTER_FADE_SCALE;
    }

    if (gFadeMs <= 0) {
        gFadeCurrent = gFadeTarget;
        gAudioPaused = (gFadeCurrent == 0);
    }
}

/* =========================
   AUDIO
   ========================= */

void FillBuffer(SndDoubleBufferPtr db)
{
    long i;
    int j;
    double sumLeft;
    double sumRight;
    double outLeft;
    double outRight;
    double step;
    double gain;
    double fadeGain;
    int activeLeft;
    int activeRight;
    SInt16 *p;

    if (gStepRequested) {
        if (!gFreezeNotes) {
            ForceRandomChange();
        }
        gStepRequested = 0;
    } else if (gChangeSliderValue > 0 && !gFreezeNotes) {
        if (gSamplesUntilChange <= 0) {
            ForceRandomChange();
        }
    }

    p = (SInt16 *)db->dbSoundData;
    db->dbNumFrames = FRAMES_PER_BUFFER;
    db->dbFlags = dbBufferReady;

    gain = (double)gGainPct / 100.0;
    fadeGain = (double)gFadeCurrent / (double)MASTER_FADE_SCALE;

    for (i = 0; i < FRAMES_PER_BUFFER; i++) {
        sumLeft = 0.0;
        sumRight = 0.0;
        activeLeft = 0;
        activeRight = 0;

        for (j = 0; j < NUM_OSC; j++) {
            if (!gOscMuted[j]) {
                if (j < OSCS_PER_CHANNEL) {
                    sumLeft += sin(gPhase[j]);
                    activeLeft++;
                } else {
                    sumRight += sin(gPhase[j]);
                    activeRight++;
                }
            }

            step = (TWO_PI * gFreqs[j]) / (double)SAMPLE_RATE;
            gPhase[j] += step;
            if (gPhase[j] >= TWO_PI) {
                gPhase[j] -= TWO_PI;
            }
        }

        if (activeLeft > 0) {
            sumLeft /= (double)activeLeft;
        } else {
            sumLeft = 0.0;
        }

        if (activeRight > 0) {
            sumRight /= (double)activeRight;
        } else {
            sumRight = 0.0;
        }

        outLeft = sumLeft * 28000.0 * gain * fadeGain;
        outRight = sumRight * 28000.0 * gain * fadeGain;

        if (outLeft < -32768.0) outLeft = -32768.0;
        if (outLeft >  32767.0) outLeft =  32767.0;
        if (outRight < -32768.0) outRight = -32768.0;
        if (outRight >  32767.0) outRight =  32767.0;

        p[(i * 2)]     = (SInt16)outLeft;
        p[(i * 2) + 1] = (SInt16)outRight;

        if (gFadeCurrent < gFadeTarget) {
            gFadeCurrent += gFadeStep;
            if (gFadeCurrent >= gFadeTarget) {
                gFadeCurrent = gFadeTarget;
            }
        } else if (gFadeCurrent > gFadeTarget) {
            gFadeCurrent -= gFadeStep;
            if (gFadeCurrent <= gFadeTarget) {
                gFadeCurrent = gFadeTarget;
            }
        }
    }

    if (gFadeCurrent == 0 && gFadeTarget == 0) {
        gAudioPaused = 1;
    }

    if (gChangeSliderValue > 0 && !gFreezeNotes) {
        gSamplesUntilChange -= FRAMES_PER_BUFFER;
    }

    if (!gRunning) {
        db->dbFlags = dbBufferReady | dbLastBuffer;
    }
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

    dbSize = sizeof(SndDoubleBuffer) + (((FRAMES_PER_BUFFER * 2L * (long)sizeof(SInt16))) - 1);

    gBufA = (SndDoubleBufferPtr)NewPtrClear(dbSize);
    gBufB = (SndDoubleBufferPtr)NewPtrClear(dbSize);
    if ((gBufA == NULL) || (gBufB == NULL)) {
        return;
    }

    gDoubleBackUPP = NewSndDoubleBackProc(MyDoubleBackProc);

    gHeader.dbhNumChannels = 2;
    gHeader.dbhSampleSize = 16;
    gHeader.dbhCompressionID = 0;
    gHeader.dbhPacketSize = 0;
    gHeader.dbhSampleRate = rate11khz;
    gHeader.dbhBufferPtr[0] = gBufA;
    gHeader.dbhBufferPtr[1] = gBufB;
    gHeader.dbhDoubleBack = gDoubleBackUPP;

    FillBuffer(gBufA);
    FillBuffer(gBufB);

    err = SndNewChannel(&gChan, sampledSynth, initStereo, NULL);
    if (err != noErr) {
        return;
    }

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

/* =========================
   STRING / UI HELPERS
   ========================= */

void MakePString(const char *src, Str255 dst)
{
    short len;

    len = 0;
    while ((src[len] != '\0') && (len < 255)) {
        len++;
    }

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

int PageOscStartIndex(int page)
{
    if (page < 1 || page > NUM_OSC_PAGES) {
        return -1;
    }
    return (page - 1) * OSCS_PER_PAGE;
}

int PageOscEndIndex(int page)
{
    int endIndex;

    endIndex = PageOscStartIndex(page);
    if (endIndex < 0) {
        return -1;
    }

    endIndex += OSCS_PER_PAGE;
    if (endIndex > NUM_OSC) {
        endIndex = NUM_OSC;
    }
    return endIndex;
}

void LayoutUI(void)
{
    Rect r;
    int i;
    int row;
    int col;
    int x;
    int y;

    if (gWindow == NULL) {
        return;
    }

    MoveControl(gPageMainButton, 18, 18);
    for (i = 0; i < NUM_OSC_PAGES; i++) {
        MoveControl(gPageOscButtons[i], 102 + (i * 84), 18);
    }

    SetRect(&r, 164, 54, 468, 70);
    MoveControl(gChangeSlider, r.left, r.top);
    SizeControl(gChangeSlider, r.right - r.left, r.bottom - r.top);

    SetRect(&r, 164, 90, 468, 106);
    MoveControl(gLowSlider, r.left, r.top);
    SizeControl(gLowSlider, r.right - r.left, r.bottom - r.top);

    SetRect(&r, 164, 126, 468, 142);
    MoveControl(gHighSlider, r.left, r.top);
    SizeControl(gHighSlider, r.right - r.left, r.bottom - r.top);

    SetRect(&r, 164, 162, 468, 178);
    MoveControl(gGainSlider, r.left, r.top);
    SizeControl(gGainSlider, r.right - r.left, r.bottom - r.top);

    SetRect(&r, 164, 198, 468, 214);
    MoveControl(gMinNotesSlider, r.left, r.top);
    SizeControl(gMinNotesSlider, r.right - r.left, r.bottom - r.top);

    SetRect(&r, 164, 234, 468, 250);
    MoveControl(gMaxNotesSlider, r.left, r.top);
    SizeControl(gMaxNotesSlider, r.right - r.left, r.bottom - r.top);

    SetRect(&r, 164, 270, 468, 286);
    MoveControl(gFadeSlider, r.left, r.top);
    SizeControl(gFadeSlider, r.right - r.left, r.bottom - r.top);

    MoveControl(gFreezeCheckbox, 252, 438);
    MoveControl(gStepButton, 24, 438);
    MoveControl(gPauseButton, 108, 438);
    MoveControl(gQuitButton, 586, 438);

    SetRect(&gChangeValueRect, 486, 52, 706, 70);
    SetRect(&gLowValueRect, 486, 88, 706, 106);
    SetRect(&gHighValueRect, 486, 124, 706, 142);
    SetRect(&gGainValueRect, 486, 160, 706, 178);
    SetRect(&gMinNotesValueRect, 486, 196, 706, 214);
    SetRect(&gMaxNotesValueRect, 486, 232, 706, 250);
    SetRect(&gFadeValueRect, 486, 268, 706, 286);
    SetRect(&gStatusRect, 24, 318, 706, 340);
    SetRect(&gHelpRect, 24, 348, 706, 428);

    for (i = 0; i < NUM_OSC; i++) {
        row = (i % OSCS_PER_PAGE) / 2;
        col = (i % OSCS_PER_PAGE) % 2;
        x = 34 + (col * 340);
        y = 124 + (row * 32);

        SetRect(&gOscLabelRects[i], x, y - 10, x + 58, y + 4);
        SetRect(&gOscValueRects[i], x + 50, y - 14, x + 180, y + 6);
        MoveControl(gOscMuteCheckbox[i], x + 196, y - 22);
    }

    UpdatePageVisibility();
}

void SyncControlsFromParams(void)
{
    int i;

    if (gChangeSlider != NULL) {
        SetControlValue(gChangeSlider, (short)gChangeSliderValue);
    }
    if (gLowSlider != NULL) {
        SetControlValue(gLowSlider, (short)gLowFreq);
    }
    if (gHighSlider != NULL) {
        SetControlValue(gHighSlider, (short)gHighFreq);
    }
    if (gGainSlider != NULL) {
        SetControlValue(gGainSlider, (short)gGainPct);
    }
    if (gMinNotesSlider != NULL) {
        SetControlValue(gMinNotesSlider, (short)gMinNotesPerEvent);
    }
    if (gMaxNotesSlider != NULL) {
        SetControlValue(gMaxNotesSlider, (short)gMaxNotesPerEvent);
    }
    if (gFadeSlider != NULL) {
        SetControlValue(gFadeSlider, (short)gFadeMs);
    }
    if (gFreezeCheckbox != NULL) {
        SetControlValue(gFreezeCheckbox, (short)gFreezeNotes);
    }
    for (i = 0; i < NUM_OSC; i++) {
        if (gOscMuteCheckbox[i] != NULL) {
            SetControlValue(gOscMuteCheckbox[i], (short)gOscMuted[i]);
        }
    }
}

void UpdatePageVisibility(void)
{
    int i;
    int startIndex;
    int endIndex;

    if (gWindow == NULL) {
        return;
    }

    if (gCurrentPage == 0) {
        ShowControl(gChangeSlider);
        ShowControl(gLowSlider);
        ShowControl(gHighSlider);
        ShowControl(gGainSlider);
        ShowControl(gMinNotesSlider);
        ShowControl(gMaxNotesSlider);
        ShowControl(gFadeSlider);
        ShowControl(gFreezeCheckbox);
    } else {
        HideControl(gChangeSlider);
        HideControl(gLowSlider);
        HideControl(gHighSlider);
        HideControl(gGainSlider);
        HideControl(gMinNotesSlider);
        HideControl(gMaxNotesSlider);
        HideControl(gFadeSlider);
        HideControl(gFreezeCheckbox);
    }

    startIndex = PageOscStartIndex(gCurrentPage);
    endIndex = PageOscEndIndex(gCurrentPage);

    for (i = 0; i < NUM_OSC; i++) {
        if (gCurrentPage > 0 && startIndex >= 0 && endIndex >= 0 && i >= startIndex && i < endIndex) {
            ShowControl(gOscMuteCheckbox[i]);
        } else {
            HideControl(gOscMuteCheckbox[i]);
        }
    }
}

void InitUI(void)
{
    Rect wr;
    Rect r;
    int i;

    SetRect(&wr, 40, 40, 780, 536);
    gWindow = NewWindow(NULL, &wr, "\p40 OSC Stereo Lab", true, zoomDocProc, (WindowPtr)-1, true, 0);

    SetRect(&r, 18, 18, 92, 38);
    gPageMainButton = NewControl(gWindow, &r, "\pMain", true, 0, 0, 1, pushButProc, 0);

    SetRect(&r, 102, 18, 180, 38);
    gPageOscButtons[0] = NewControl(gWindow, &r, "\p1-10", true, 0, 0, 1, pushButProc, 0);

    SetRect(&r, 186, 18, 264, 38);
    gPageOscButtons[1] = NewControl(gWindow, &r, "\p11-20", true, 0, 0, 1, pushButProc, 0);

    SetRect(&r, 270, 18, 348, 38);
    gPageOscButtons[2] = NewControl(gWindow, &r, "\p21-30", true, 0, 0, 1, pushButProc, 0);

    SetRect(&r, 354, 18, 432, 38);
    gPageOscButtons[3] = NewControl(gWindow, &r, "\p31-40", true, 0, 0, 1, pushButProc, 0);

    SetRect(&r, 164, 54, 468, 70);
    gChangeSlider = NewControl(gWindow, &r, "\p", true, gChangeSliderValue, 0, CHANGE_SLIDER_MAX, scrollBarProc, 0);

    SetRect(&r, 164, 90, 468, 106);
    gLowSlider = NewControl(gWindow, &r, "\p", true, gLowFreq, NOTE_MIN_HZ, NOTE_MAX_HZ, scrollBarProc, 0);

    SetRect(&r, 164, 126, 468, 142);
    gHighSlider = NewControl(gWindow, &r, "\p", true, gHighFreq, NOTE_MIN_HZ, NOTE_MAX_HZ, scrollBarProc, 0);

    SetRect(&r, 164, 162, 468, 178);
    gGainSlider = NewControl(gWindow, &r, "\p", true, gGainPct, 0, GAIN_MAX_PCT, scrollBarProc, 0);

    SetRect(&r, 164, 198, 468, 214);
    gMinNotesSlider = NewControl(gWindow, &r, "\p", true, gMinNotesPerEvent, 1, NOTES_PER_EVENT_MAX, scrollBarProc, 0);

    SetRect(&r, 164, 234, 468, 250);
    gMaxNotesSlider = NewControl(gWindow, &r, "\p", true, gMaxNotesPerEvent, 1, NOTES_PER_EVENT_MAX, scrollBarProc, 0);

    SetRect(&r, 164, 270, 468, 286);
    gFadeSlider = NewControl(gWindow, &r, "\p", true, gFadeMs, 0, FADE_MS_MAX, scrollBarProc, 0);

    SetRect(&r, 252, 438, 380, 458);
    gFreezeCheckbox = NewControl(gWindow, &r, "\pFreeze Notes", true, gFreezeNotes, 0, 1, checkBoxProc, 0);

    SetRect(&r, 24, 438, 96, 458);
    gStepButton = NewControl(gWindow, &r, "\pStep", true, 0, 0, 1, pushButProc, 0);

    SetRect(&r, 108, 438, 240, 458);
    gPauseButton = NewControl(gWindow, &r, "\pPause/Resume", true, 0, 0, 1, pushButProc, 0);

    SetRect(&r, 586, 438, 666, 458);
    gQuitButton = NewControl(gWindow, &r, "\pQuit", true, 0, 0, 1, pushButProc, 0);

    for (i = 0; i < NUM_OSC; i++) {
        SetRect(&r, 0, 0, 64, 18);
        gOscMuteCheckbox[i] = NewControl(gWindow, &r, "\pMute", true, 0, 0, 1, checkBoxProc, 0);
    }

    gSliderActionUPP = NewControlActionProc(SliderAction);

    SetPort(gWindow);
    LayoutUI();
    SyncControlsFromParams();
    UpdatePageVisibility();
    DrawStaticUI(gWindow);
    DrawControls(gWindow);
    UpdateStatusArea(gWindow);
    UpdateValueAreas(gWindow);
    UpdateOscillatorAreas(gWindow);
}

void DrawStaticUI(WindowPtr w)
{
    char title[64];

    SetPort(w);
    EraseRect(&w->portRect);

    MoveTo(470, 32);
    DrawString("\pView");

    if (gCurrentPage == 0) {
        MoveTo(24, 66);
        DrawString("\pChange Rate");

        MoveTo(24, 102);
        DrawString("\pRange Low Hz");

        MoveTo(24, 138);
        DrawString("\pRange High Hz");

        MoveTo(24, 174);
        DrawString("\pGain %");

        MoveTo(24, 210);
        DrawString("\pMin Notes/Event");

        MoveTo(24, 246);
        DrawString("\pMax Notes/Event");

        MoveTo(24, 282);
        DrawString("\pPause Fade ms");
    } else {
        int startIndex;
        int endIndex;

        startIndex = PageOscStartIndex(gCurrentPage);
        endIndex = PageOscEndIndex(gCurrentPage);
        sprintf(title, "Oscillators %d-%d", startIndex + 1, endIndex);
        MoveTo(24, 66);
        DrawPascalFromCString(title);

        if (startIndex < OSCS_PER_CHANNEL) {
            if (endIndex <= OSCS_PER_CHANNEL) {
                DrawString("\p  (Left channel)");
            } else {
                DrawString("\p  (Crosses L/R split)");
            }
        } else {
            DrawString("\p  (Right channel)");
        }

        MoveTo(24, 86);
        DrawString("\pClick a value box, type a frequency, then press Return.");
    }
}

void UpdateValueAreas(WindowPtr w)
{
    char temp[96];

    SetPort(w);

    EraseRect(&gChangeValueRect);
    EraseRect(&gLowValueRect);
    EraseRect(&gHighValueRect);
    EraseRect(&gGainValueRect);
    EraseRect(&gMinNotesValueRect);
    EraseRect(&gMaxNotesValueRect);
    EraseRect(&gFadeValueRect);

    if (gCurrentPage != 0) {
        return;
    }

    FrameRect(&gChangeValueRect);
    if (gChangeSliderValue <= 0) {
        sprintf(temp, "%s", "stopped");
    } else {
        long ms;
        ms = (ChangeSliderToSamples((short)gChangeSliderValue) * 1000L) / SAMPLE_RATE;
        sprintf(temp, "%ld ms", ms);
    }
    MoveTo(gChangeValueRect.left + 4, gChangeValueRect.bottom - 4);
    DrawPascalFromCString(temp);

    FrameRect(&gLowValueRect);
    sprintf(temp, "%d", gLowFreq);
    MoveTo(gLowValueRect.left + 4, gLowValueRect.bottom - 4);
    DrawPascalFromCString(temp);

    FrameRect(&gHighValueRect);
    sprintf(temp, "%d", gHighFreq);
    MoveTo(gHighValueRect.left + 4, gHighValueRect.bottom - 4);
    DrawPascalFromCString(temp);

    FrameRect(&gGainValueRect);
    sprintf(temp, "%d", gGainPct);
    MoveTo(gGainValueRect.left + 4, gGainValueRect.bottom - 4);
    DrawPascalFromCString(temp);

    FrameRect(&gMinNotesValueRect);
    sprintf(temp, "%d", gMinNotesPerEvent);
    MoveTo(gMinNotesValueRect.left + 4, gMinNotesValueRect.bottom - 4);
    DrawPascalFromCString(temp);

    FrameRect(&gMaxNotesValueRect);
    sprintf(temp, "%d", gMaxNotesPerEvent);
    MoveTo(gMaxNotesValueRect.left + 4, gMaxNotesValueRect.bottom - 4);
    DrawPascalFromCString(temp);

    FrameRect(&gFadeValueRect);
    sprintf(temp, "%d ms", gFadeMs);
    MoveTo(gFadeValueRect.left + 4, gFadeValueRect.bottom - 4);
    DrawPascalFromCString(temp);
}

void UpdateStatusArea(WindowPtr w)
{
    char temp[256];
    int low;
    int high;
    int minNotes;
    int maxNotes;

    low = EffectiveLowFreq();
    high = EffectiveHighFreq();
    minNotes = EffectiveMinNotesPerEvent();
    maxNotes = EffectiveMaxNotesPerEvent();

    SetPort(w);
    EraseRect(&gStatusRect);

    if (gAudioPaused && gFadeCurrent == 0) {
        sprintf(temp, "Status: Stereo audio paused. L=Osc 1-20, R=Osc 21-40. Range %d to %d Hz, %d to %d notes/event.", low, high, minNotes, maxNotes);
    } else if (gFadeTarget == 0) {
        sprintf(temp, "Status: Fading out to pause. L=Osc 1-20, R=Osc 21-40. Range %d to %d Hz, %d to %d notes/event.", low, high, minNotes, maxNotes);
    } else if (gFadeCurrent < gFadeTarget) {
        sprintf(temp, "Status: Fading in. L=Osc 1-20, R=Osc 21-40. Range %d to %d Hz, %d to %d notes/event.", low, high, minNotes, maxNotes);
    } else if (gFreezeNotes) {
        sprintf(temp, "Status: Freeze notes enabled. L=Osc 1-20, R=Osc 21-40. Range %d to %d Hz, %d to %d notes/event.", low, high, minNotes, maxNotes);
    } else if (gChangeSliderValue <= 0) {
        sprintf(temp, "Status: Change rate stopped. L=Osc 1-20, R=Osc 21-40. Range %d to %d Hz, %d to %d notes/event.", low, high, minNotes, maxNotes);
    } else {
        sprintf(temp, "Status: Stereo running. L=Osc 1-20, R=Osc 21-40. Range %d to %d Hz, %d to %d notes/event.", low, high, minNotes, maxNotes);
    }

    MoveTo(gStatusRect.left, gStatusRect.bottom - 2);
    DrawPascalFromCString(temp);

    EraseRect(&gHelpRect);
    if (gCurrentPage == 0) {
        MoveTo(gHelpRect.left, gHelpRect.top + 12);
        DrawString("\pMain page: timing, range, gain, note count, and pause fade.");
        MoveTo(gHelpRect.left, gHelpRect.top + 30);
        DrawString("\pStereo layout: oscillators 1-20 feed left, oscillators 21-40 feed right.");
        MoveTo(gHelpRect.left, gHelpRect.top + 48);
        DrawString("\pUse the oscillator tabs to edit frequencies and mute individual oscillators.");
    } else {
        MoveTo(gHelpRect.left, gHelpRect.top + 12);
        DrawString("\pTyped values override the selected oscillator when you press Return.");
        MoveTo(gHelpRect.left, gHelpRect.top + 28);
        DrawString("\pEach oscillator has its own Mute checkbox beside the value box.");
        MoveTo(gHelpRect.left, gHelpRect.top + 44);
        DrawString("\pKeyboard shortcuts: S = Step, P = Pause, F = Freeze, Q = Quit.");
    }
}

void UpdateOscillatorAreas(WindowPtr w)
{
    int i;
    int startIndex;
    int endIndex;
    char temp[64];
    Rect r;

    SetPort(w);

    if (gCurrentPage == 0) {
        gOscDirty = 0;
        return;
    }

    startIndex = PageOscStartIndex(gCurrentPage);
    endIndex = PageOscEndIndex(gCurrentPage);
    if (startIndex < 0 || endIndex < 0) {
        gOscDirty = 0;
        return;
    }

    for (i = startIndex; i < endIndex; i++) {
        EraseRect(&gOscLabelRects[i]);
        EraseRect(&gOscValueRects[i]);
    }

    for (i = startIndex; i < endIndex; i++) {
        sprintf(temp, "O%d", i + 1);
        MoveTo(gOscLabelRects[i].left, gOscLabelRects[i].bottom);
        DrawPascalFromCString(temp);

        r = gOscValueRects[i];
        FrameRect(&r);

        if (i == gSelectedOsc) {
            Rect inner;
            inner = r;
            InsetRect(&inner, 1, 1);
            FrameRect(&inner);
        }

        if (i == gSelectedOsc && gOscEditActive) {
            strcpy(temp, gOscEditBuffer);
        } else {
            sprintf(temp, "%.1f", gFreqs[i]);
        }

        MoveTo(r.left + 4, r.bottom - 5);
        DrawPascalFromCString(temp);

        if (gOscMuteCheckbox[i] != NULL) {
            ShowControl(gOscMuteCheckbox[i]);
            Draw1Control(gOscMuteCheckbox[i]);
        }
    }

    gOscDirty = 0;
}

void UpdateParamFromSlider(ControlHandle control)
{
    if (control == gChangeSlider) {
        gChangeSliderValue = GetControlValue(gChangeSlider);
        ScheduleNextChange();
    } else if (control == gLowSlider) {
        gLowFreq = GetControlValue(gLowSlider);
    } else if (control == gHighSlider) {
        gHighFreq = GetControlValue(gHighSlider);
    } else if (control == gGainSlider) {
        gGainPct = GetControlValue(gGainSlider);
        if (gGainPct < 0) gGainPct = 0;
        if (gGainPct > GAIN_MAX_PCT) gGainPct = GAIN_MAX_PCT;
    } else if (control == gMinNotesSlider) {
        gMinNotesPerEvent = GetControlValue(gMinNotesSlider);
        if (gMinNotesPerEvent < 1) gMinNotesPerEvent = 1;
        if (gMinNotesPerEvent > NOTES_PER_EVENT_MAX) gMinNotesPerEvent = NOTES_PER_EVENT_MAX;
    } else if (control == gMaxNotesSlider) {
        gMaxNotesPerEvent = GetControlValue(gMaxNotesSlider);
        if (gMaxNotesPerEvent < 1) gMaxNotesPerEvent = 1;
        if (gMaxNotesPerEvent > NOTES_PER_EVENT_MAX) gMaxNotesPerEvent = NOTES_PER_EVENT_MAX;
    } else if (control == gFadeSlider) {
        gFadeMs = GetControlValue(gFadeSlider);
        if (gFadeMs < 0) gFadeMs = 0;
        if (gFadeMs > FADE_MS_MAX) gFadeMs = FADE_MS_MAX;
        UpdateFadeStep();
    }

    UpdateValueAreas(gWindow);
    UpdateStatusArea(gWindow);
}

short GetSliderArrowStep(short modifiers)
{
    if (modifiers & shiftKey) {
        return 10;
    }
    return 1;
}

short GetSliderPageStep(short modifiers)
{
    if (modifiers & shiftKey) {
        return 32;
    }
    if (modifiers & optionKey) {
        return 2;
    }
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
        if (value < minValue) {
            value = minValue;
        }
        SetControlValue(control, value);
    } else if (partCode == inDownButton) {
        value = (short)(value + arrowStep);
        if (value > maxValue) {
            value = maxValue;
        }
        SetControlValue(control, value);
    } else if (partCode == inPageUp) {
        value = (short)(value - pageStep);
        if (value < minValue) {
            value = minValue;
        }
        SetControlValue(control, value);
    } else if (partCode == inPageDown) {
        value = (short)(value + pageStep);
        if (value > maxValue) {
            value = maxValue;
        }
        SetControlValue(control, value);
    }

    UpdateParamFromSlider(control);
}

int OscIndexAtPoint(Point localPt)
{
    int i;
    int startIndex;
    int endIndex;

    startIndex = PageOscStartIndex(gCurrentPage);
    endIndex = PageOscEndIndex(gCurrentPage);
    if (startIndex < 0 || endIndex < 0) {
        return -1;
    }

    for (i = startIndex; i < endIndex; i++) {
        if (PtInRect(localPt, &gOscValueRects[i])) {
            return i;
        }
    }

    return -1;
}

void RefreshSelectedOscEditBuffer(void)
{
    if (gSelectedOsc >= 0 && gSelectedOsc < NUM_OSC) {
        sprintf(gOscEditBuffer, "%.1f", gFreqs[gSelectedOsc]);
        gOscEditLen = (int)strlen(gOscEditBuffer);
        gOscEditActive = 0;
    } else {
        gOscEditBuffer[0] = '\0';
        gOscEditLen = 0;
        gOscEditActive = 0;
    }
}

void SelectOscillator(int oscIndex)
{
    gSelectedOsc = oscIndex;
    RefreshSelectedOscEditBuffer();
    UpdateOscillatorAreas(gWindow);
}

void CommitSelectedOscillatorEdit(void)
{
    double value;

    value = 0.0;

    if (gSelectedOsc < 0 || gSelectedOsc >= NUM_OSC) {
        return;
    }

    if (gOscEditLen <= 0) {
        RefreshSelectedOscEditBuffer();
        UpdateOscillatorAreas(gWindow);
        return;
    }

    if (sscanf(gOscEditBuffer, "%lf", &value) == 1) {
        ApplyFrequencyChange(gSelectedOsc, value);
    }

    RefreshSelectedOscEditBuffer();
    UpdateOscillatorAreas(gWindow);
}

void DoMouseDown(EventRecord *event)
{
    WindowPtr whichWindow;
    short part;

    part = FindWindow(event->where, &whichWindow);

    switch (part) {
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
            if (TrackGoAway(whichWindow, event->where)) {
                gRunning = 0;
            }
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
                ControlHandle ctrl;
                short ctlPart;
                Point localPt;
                int oscIndex;
                int i;
                int pageHandled;

                ctrl = NULL;
                SetPort(whichWindow);
                localPt = event->where;
                GlobalToLocal(&localPt);
                oscIndex = OscIndexAtPoint(localPt);

                if (oscIndex >= 0) {
                    SelectOscillator(oscIndex);
                    break;
                }

                ctlPart = FindControl(localPt, whichWindow, &ctrl);

                if ((ctrl != NULL) && (ctrl == gPageMainButton)) {
                    if (TrackControl(ctrl, localPt, NULL) != 0) {
                        gCurrentPage = 0;
                        gSelectedOsc = -1;
                        gOscEditActive = 0;
                        UpdatePageVisibility();
                        InvalRect(&whichWindow->portRect);
                    }
                } else {
                    pageHandled = 0;
                    for (i = 0; i < NUM_OSC_PAGES; i++) {
                        if ((ctrl != NULL) && (ctrl == gPageOscButtons[i])) {
                            if (TrackControl(ctrl, localPt, NULL) != 0) {
                                gCurrentPage = i + 1;
                                if (gSelectedOsc < PageOscStartIndex(gCurrentPage) || gSelectedOsc >= PageOscEndIndex(gCurrentPage)) {
                                    gSelectedOsc = -1;
                                    gOscEditActive = 0;
                                }
                                UpdatePageVisibility();
                                InvalRect(&whichWindow->portRect);
                            }
                            pageHandled = 1;
                            break;
                        }
                    }

                    if (!pageHandled) {
                        if ((ctrl != NULL) &&
                            ((ctrl == gChangeSlider) ||
                             (ctrl == gLowSlider) ||
                             (ctrl == gHighSlider) ||
                             (ctrl == gGainSlider) ||
                             (ctrl == gMinNotesSlider) ||
                             (ctrl == gMaxNotesSlider) ||
                             (ctrl == gFadeSlider)) &&
                            (ctlPart != 0)) {
                            gSliderTrackModifiers = event->modifiers;
                            TrackControl(ctrl, localPt, gSliderActionUPP);
                            gSliderTrackModifiers = 0;
                            UpdateParamFromSlider(ctrl);
                        } else if ((ctrl != NULL) && (ctrl == gFreezeCheckbox)) {
                            if (TrackControl(ctrl, localPt, NULL) != 0) {
                                gFreezeNotes = !gFreezeNotes;
                                SetControlValue(gFreezeCheckbox, (short)gFreezeNotes);
                                UpdateStatusArea(gWindow);
                                UpdateValueAreas(gWindow);
                            }
                        } else if ((ctrl != NULL) && (ctrl == gStepButton)) {
                            if (TrackControl(ctrl, localPt, NULL) != 0) {
                                gStepRequested = 1;
                                UpdateStatusArea(gWindow);
                            }
                        } else if ((ctrl != NULL) && (ctrl == gPauseButton)) {
                            if (TrackControl(ctrl, localPt, NULL) != 0) {
                                if (gFadeTarget == 0 || gFadeCurrent == 0) {
                                    StartPauseFade(0);
                                } else {
                                    StartPauseFade(1);
                                }
                                UpdateStatusArea(gWindow);
                            }
                        } else if ((ctrl != NULL) && (ctrl == gQuitButton)) {
                            if (TrackControl(ctrl, localPt, NULL) != 0) {
                                gRunning = 0;
                            }
                        } else if (ctrl != NULL) {
                            for (i = 0; i < NUM_OSC; i++) {
                                if (ctrl == gOscMuteCheckbox[i]) {
                                    if (TrackControl(ctrl, localPt, NULL) != 0) {
                                        gOscMuted[i] = !gOscMuted[i];
                                        SetControlValue(gOscMuteCheckbox[i], (short)gOscMuted[i]);
                                        gOscDirty = 1;
                                    }
                                    UpdateStatusArea(gWindow);
                                    return;
                                }
                            }

                            gSelectedOsc = -1;
                            gOscEditActive = 0;
                            UpdateOscillatorAreas(gWindow);
                        } else {
                            gSelectedOsc = -1;
                            gOscEditActive = 0;
                            UpdateOscillatorAreas(gWindow);
                        }
                    }
                }
            }
            break;
    }
}

void DoUpdate(EventRecord *event)
{
    WindowPtr w;

    w = (WindowPtr)event->message;
    BeginUpdate(w);
    DrawStaticUI(w);
    DrawControls(w);
    UpdateStatusArea(w);
    UpdateValueAreas(w);
    UpdateOscillatorAreas(w);
    EndUpdate(w);
}

void DoActivate(EventRecord *event)
{
    WindowPtr w;

    w = (WindowPtr)event->message;
    if (w == gWindow) {
        SetPort(w);
        DrawControls(w);
        UpdateStatusArea(w);
        UpdateValueAreas(w);
        UpdateOscillatorAreas(w);
    }
}

void HandleKey(EventRecord *event)
{
    char c;

    c = (char)(event->message & charCodeMask);

    if ((c == 27) || (c == 'q') || (c == 'Q')) {
        gRunning = 0;
        return;
    }

    if (c == 's' || c == 'S') {
        gStepRequested = 1;
        UpdateStatusArea(gWindow);
        return;
    }

    if (c == 'p' || c == 'P') {
        if (gFadeTarget == 0 || gFadeCurrent == 0) {
            StartPauseFade(0);
        } else {
            StartPauseFade(1);
        }
        UpdateStatusArea(gWindow);
        return;
    }

    if (c == 'f' || c == 'F') {
        gFreezeNotes = !gFreezeNotes;
        SetControlValue(gFreezeCheckbox, (short)gFreezeNotes);
        UpdateStatusArea(gWindow);
        UpdateValueAreas(gWindow);
        return;
    }

    if (gSelectedOsc < 0 || gSelectedOsc >= NUM_OSC) {
        return;
    }

    if (c == 3 || c == 13) {
        CommitSelectedOscillatorEdit();
        return;
    }

    if (c == 8) {
        if (gOscEditLen > 0) {
            gOscEditLen--;
            gOscEditBuffer[gOscEditLen] = '\0';
            gOscEditActive = 1;
            UpdateOscillatorAreas(gWindow);
        }
        return;
    }

    if (((c >= '0') && (c <= '9')) || (c == '.')) {
        if (!gOscEditActive) {
            gOscEditLen = 0;
            gOscEditBuffer[0] = '\0';
            gOscEditActive = 1;
        }

        if (gOscEditLen < (int)sizeof(gOscEditBuffer) - 1) {
            if (c == '.') {
                if (strchr(gOscEditBuffer, '.') != NULL) {
                    return;
                }
            }
            gOscEditBuffer[gOscEditLen++] = c;
            gOscEditBuffer[gOscEditLen] = '\0';
            UpdateOscillatorAreas(gWindow);
        }
        return;
    }
}

/* =========================
   MAIN
   ========================= */

int main(void)
{
    EventRecord event;

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

    SeedRand((unsigned long)TickCount());
    InitFrequencies();
    UpdateFadeStep();
    InitUI();
    InitAudio();

    while (gRunning) {
        if (WaitNextEvent(everyEvent, &event, 3, NULL)) {
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
                    HandleKey(&event);
                    break;
            }
        }

        if (gWindow != NULL && gOscDirty) {
            UpdateOscillatorAreas(gWindow);
        }
    }

    ShutdownAudio();
    return 0;
}
