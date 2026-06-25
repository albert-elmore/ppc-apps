/*
 * CPU Monitor — Mac OS 9 / PowerPC G3 (CodeWarrior).
 *
 * Measures system CPU utilization by differencing Process Manager
 * processActiveTime across all running processes. This reflects actual
 * CPU time charged to applications, not a synthetic busy-loop benchmark.
 */

#include <stdio.h>
#include <OSUtils.h>
#include <Processes.h>
#include <Windows.h>
#include <Controls.h>
#include <Events.h>
#include <Dialogs.h>
#include <Quickdraw.h>
#include <Fonts.h>
#include <TextEdit.h>
#include <Menus.h>

/* Explicit prototype for this compiler setup */
pascal unsigned long TickCount(void);

#define WINDOW_LEFT            60
#define WINDOW_TOP             60
#define WINDOW_RIGHT           500
#define WINDOW_BOTTOM          340

#define TARGET_SAMPLE_TICKS    30   /* ~500 ms fixed measurement windows */
#define MAX_SAMPLE_TICKS       60   /* discard stale intervals after pauses */
#define EVENT_SLEEP_TICKS      15   /* cooperative yield while waiting */
#define BAR_WIDTH_PIXELS       300
#define BAR_HEIGHT_PIXELS      18
#define HISTORY_SIZE           10   /* moving average over ~5 seconds */

static WindowPtr gWindow = NULL;
static ControlHandle gResetButton = NULL;
static ControlHandle gQuitButton = NULL;

static Rect gBarFrame;
static Rect gStatusRect;
static Rect gDetailRect;
static Rect gWarningRect;

static unsigned long gLastSampleTick = 0;
static unsigned long gLastTotalActive = 0;
static unsigned long gLastSelfActive = 0;
static int gHasPriorSample = 0;

static int gCurrentPercent = 0;
static int gSmoothedPercent = 0;
static int gPeakPercent = 0;
static unsigned long gLastActiveDelta = 0;
static unsigned long gLastWallDelta = 0;
static int gHistory[HISTORY_SIZE];
static int gHistoryIndex = 0;
static int gHistoryCount = 0;
static int gNeedsRedraw = 1;

static int PSNEqual(const ProcessSerialNumber *a, const ProcessSerialNumber *b);
static void SampleActiveTimes(unsigned long *totalOut, unsigned long *selfOut);
static void InitUI(void);
static void LayoutUI(void);
static void DrawStaticUI(WindowPtr w);
static void DrawDynamicUI(WindowPtr w);
static void DoUpdate(EventRecord *event);
static void DoActivate(EventRecord *event);
static void DoMouseDown(EventRecord *event, int *running);
static void ResetMeterStats(void);
static void UpdateMeter(void);
static void AddPercentSample(int percent);
static int ComputeSmoothedPercent(void);
static void DrawCString(const char *text);
static void MakePString(const char *src, Str255 dst);

static int PSNEqual(const ProcessSerialNumber *a, const ProcessSerialNumber *b)
{
    return a->highLongOfPSN == b->highLongOfPSN &&
           a->lowLongOfPSN == b->lowLongOfPSN;
}

static void SampleActiveTimes(unsigned long *totalOut, unsigned long *selfOut)
{
    ProcessSerialNumber psn;
    ProcessSerialNumber curPsn;
    ProcessInfoRec info;
    unsigned long total;
    unsigned long self;
    OSErr err;

    total = 0;
    self = 0;
    psn.highLongOfPSN = 0;
    psn.lowLongOfPSN = kNoProcess;

    info.processInfoLength = (long)sizeof(ProcessInfoRec);
    info.processName = NULL;
    info.processAppSpec = NULL;

    if (GetCurrentProcess(&curPsn) != noErr) {
        curPsn.highLongOfPSN = 0;
        curPsn.lowLongOfPSN = 0;
    }

    for (;;) {
        err = GetNextProcess(&psn);
        if (err != noErr) {
            break;
        }

        err = GetProcessInformation(&psn, &info);
        if (err == noErr) {
            total += (unsigned long)info.processActiveTime;
            if (PSNEqual(&psn, &curPsn)) {
                self = (unsigned long)info.processActiveTime;
            }
        }
    }

    *totalOut = total;
    *selfOut = self;
}

static void MakePString(const char *src, Str255 dst)
{
    short len;

    len = 0;
    while (src[len] != '\0' && len < 255) {
        len++;
    }

    dst[0] = (unsigned char)len;
    while (len > 0) {
        dst[len] = (unsigned char)src[len - 1];
        len--;
    }
}

static void DrawCString(const char *text)
{
    Str255 p;
    MakePString(text, p);
    DrawString(p);
}

static void LayoutUI(void)
{
    if (gWindow == NULL) {
        return;
    }

    SetRect(&gBarFrame, 70, 88, 70 + BAR_WIDTH_PIXELS, 88 + BAR_HEIGHT_PIXELS);
    SetRect(&gStatusRect, 70, 132, 440, 152);
    SetRect(&gDetailRect, 70, 158, 440, 220);
    SetRect(&gWarningRect, 70, 234, 440, 294);

    MoveControl(gResetButton, 70, 32);
    MoveControl(gQuitButton, 360, 32);
}

static void DrawStaticUI(WindowPtr w)
{
    SetPort(w);
    EraseRect(&w->portRect);

    MoveTo(70, 72);
    DrawString("\pCPU Utilization");

    FrameRect(&gBarFrame);

    MoveTo(70, 306);
    DrawString("\pProcess Manager sampling — low overhead, system-wide CPU time.");
    MoveTo(70, 322);
    DrawString("\pLeave this app running while profiling other ppc-apps.");
}

static void DrawDynamicUI(WindowPtr w)
{
    Rect fillRect;
    Rect clearRect;
    char temp[200];
    int fillWidth;

    SetPort(w);

    clearRect = gBarFrame;
    InsetRect(&clearRect, 1, 1);
    EraseRect(&clearRect);

    fillWidth = ((gSmoothedPercent * (BAR_WIDTH_PIXELS - 2)) / 100);
    if (fillWidth > 0) {
        fillRect = clearRect;
        fillRect.right = fillRect.left + fillWidth;
        PaintRect(&fillRect);
    }

    EraseRect(&gStatusRect);
    sprintf(temp, "CPU Used: %d%%   Peak: %d%%", gSmoothedPercent, gPeakPercent);
    MoveTo(gStatusRect.left, gStatusRect.bottom - 4);
    DrawCString(temp);

    EraseRect(&gDetailRect);
    sprintf(temp, "Instant: %d%%   Active: %lu ticks / %lu ticks wall",
            gCurrentPercent, gLastActiveDelta, gLastWallDelta);
    MoveTo(gDetailRect.left, gDetailRect.top + 12);
    DrawCString(temp);
    sprintf(temp, "Source: all processes except this monitor");
    MoveTo(gDetailRect.left, gDetailRect.top + 30);
    DrawCString(temp);
    sprintf(temp, "Window: %d ticks (~%d ms), %d-sample average",
            TARGET_SAMPLE_TICKS, (TARGET_SAMPLE_TICKS * 1000) / 60, HISTORY_SIZE);
    MoveTo(gDetailRect.left, gDetailRect.top + 48);
    DrawCString(temp);

    EraseRect(&gWarningRect);
    if (!gHasPriorSample) {
        MoveTo(gWarningRect.left, gWarningRect.top + 12);
        DrawString("\pCollecting first sample...");
    } else if (gSmoothedPercent >= 85) {
        MoveTo(gWarningRect.left, gWarningRect.top + 12);
        DrawString("\pHeavy load detected. Audio app may be close to the edge.");
    } else if (gSmoothedPercent >= 60) {
        MoveTo(gWarningRect.left, gWarningRect.top + 12);
        DrawString("\pModerate load. Watch for spikes when more voices or effects start.");
    } else {
        MoveTo(gWarningRect.left, gWarningRect.top + 12);
        DrawString("\pLoad looks manageable.");
    }

    DrawControls(w);
    gNeedsRedraw = 0;
}

static void ResetMeterStats(void)
{
    int i;

    gCurrentPercent = 0;
    gSmoothedPercent = 0;
    gPeakPercent = 0;
    gLastActiveDelta = 0;
    gLastWallDelta = 0;
    gLastSampleTick = 0;
    gLastTotalActive = 0;
    gLastSelfActive = 0;
    gHasPriorSample = 0;
    gHistoryIndex = 0;
    gHistoryCount = 0;
    for (i = 0; i < HISTORY_SIZE; i++) {
        gHistory[i] = 0;
    }
    gNeedsRedraw = 1;
}

static void AddPercentSample(int percent)
{
    gHistory[gHistoryIndex] = percent;
    gHistoryIndex++;
    if (gHistoryIndex >= HISTORY_SIZE) {
        gHistoryIndex = 0;
    }
    if (gHistoryCount < HISTORY_SIZE) {
        gHistoryCount++;
    }
}

static int ComputeSmoothedPercent(void)
{
    int i;
    long sum;

    if (gHistoryCount <= 0) {
        return gCurrentPercent;
    }

    sum = 0;
    for (i = 0; i < gHistoryCount; i++) {
        sum += gHistory[i];
    }

    return (int)(sum / gHistoryCount);
}

static void UpdateMeter(void)
{
    unsigned long nowTick;
    unsigned long totalActive;
    unsigned long selfActive;
    unsigned long wallDelta;
    unsigned long totalDelta;
    unsigned long selfDelta;
    unsigned long activeDelta;
    unsigned long percent;

    nowTick = TickCount();
    SampleActiveTimes(&totalActive, &selfActive);

    if (!gHasPriorSample) {
        gLastSampleTick = nowTick;
        gLastTotalActive = totalActive;
        gLastSelfActive = selfActive;
        gHasPriorSample = 1;
        gNeedsRedraw = 1;
        return;
    }

    if (nowTick < gLastSampleTick) {
        gLastSampleTick = nowTick;
        gLastTotalActive = totalActive;
        gLastSelfActive = selfActive;
        return;
    }

    wallDelta = nowTick - gLastSampleTick;
    if (wallDelta < (unsigned long)TARGET_SAMPLE_TICKS) {
        return;
    }

    if (wallDelta > (unsigned long)MAX_SAMPLE_TICKS) {
        /* Paused, dragged, or blocked — restart without a bogus spike. */
        gLastSampleTick = nowTick;
        gLastTotalActive = totalActive;
        gLastSelfActive = selfActive;
        return;
    }

    if (totalActive < gLastTotalActive || selfActive < gLastSelfActive) {
        gLastSampleTick = nowTick;
        gLastTotalActive = totalActive;
        gLastSelfActive = selfActive;
        return;
    }

    totalDelta = totalActive - gLastTotalActive;
    selfDelta = selfActive - gLastSelfActive;
    if (totalDelta < selfDelta) {
        gLastSampleTick = nowTick;
        gLastTotalActive = totalActive;
        gLastSelfActive = selfActive;
        return;
    }

    activeDelta = totalDelta - selfDelta;
    gLastActiveDelta = activeDelta;
    gLastWallDelta = wallDelta;

    percent = (activeDelta * 100UL) / wallDelta;
    if (percent > 100UL) {
        percent = 100UL;
    }

    gCurrentPercent = (int)percent;
    AddPercentSample(gCurrentPercent);
    gSmoothedPercent = ComputeSmoothedPercent();

    if (gCurrentPercent > gPeakPercent) {
        gPeakPercent = gCurrentPercent;
    } else if (gPeakPercent > gSmoothedPercent + 3) {
        gPeakPercent--;
    }

    gLastSampleTick = nowTick;
    gLastTotalActive = totalActive;
    gLastSelfActive = selfActive;
    gNeedsRedraw = 1;
}

static void InitUI(void)
{
    Rect wr;
    Rect r;

    SetRect(&wr, WINDOW_LEFT, WINDOW_TOP, WINDOW_RIGHT, WINDOW_BOTTOM);
    gWindow = NewWindow(NULL, &wr, "\pCPU Monitor", true, zoomDocProc, (WindowPtr)-1, true, 0);

    SetRect(&r, 70, 32, 150, 52);
    gResetButton = NewControl(gWindow, &r, "\pReset", true, 0, 0, 1, pushButProc, 0);

    SetRect(&r, 360, 32, 420, 52);
    gQuitButton = NewControl(gWindow, &r, "\pQuit", true, 0, 0, 1, pushButProc, 0);

    SetPort(gWindow);
    LayoutUI();
    DrawStaticUI(gWindow);
    DrawDynamicUI(gWindow);
}

static void DoUpdate(EventRecord *event)
{
    WindowPtr w;

    w = (WindowPtr)event->message;
    BeginUpdate(w);
    DrawStaticUI(w);
    DrawDynamicUI(w);
    EndUpdate(w);
}

static void DoActivate(EventRecord *event)
{
    WindowPtr w;

    w = (WindowPtr)event->message;
    if (w == gWindow) {
        SetPort(w);
        DrawStaticUI(w);
        DrawDynamicUI(w);
    }
}

static void DoMouseDown(EventRecord *event, int *running)
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
                *running = 0;
            }
            break;

        case inContent:
            if (whichWindow != FrontWindow()) {
                SelectWindow(whichWindow);
            } else if (whichWindow == gWindow) {
                Point localPt;
                ControlHandle ctrl;
                short ctlPart;

                SetPort(whichWindow);
                localPt = event->where;
                GlobalToLocal(&localPt);
                ctrl = NULL;
                ctlPart = FindControl(localPt, whichWindow, &ctrl);

                if (ctlPart != 0 && ctrl != NULL) {
                    if (ctrl == gResetButton) {
                        if (TrackControl(ctrl, localPt, NULL) != 0) {
                            ResetMeterStats();
                            DrawDynamicUI(gWindow);
                        }
                    } else if (ctrl == gQuitButton) {
                        if (TrackControl(ctrl, localPt, NULL) != 0) {
                            *running = 0;
                        }
                    }
                }
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
    }
}

int main(void)
{
    EventRecord event;
    int running;

    running = 1;

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

    InitUI();

    while (running) {
        if (WaitNextEvent(everyEvent, &event, EVENT_SLEEP_TICKS, NULL)) {
            switch (event.what) {
                case mouseDown:
                    DoMouseDown(&event, &running);
                    break;

                case updateEvt:
                    DoUpdate(&event);
                    break;

                case activateEvt:
                    DoActivate(&event);
                    break;

                case keyDown:
                case autoKey:
                {
                    char c;
                    c = (char)(event.message & charCodeMask);
                    if (c == 'q' || c == 'Q' || c == 27) {
                        running = 0;
                    } else if (c == 'r' || c == 'R') {
                        ResetMeterStats();
                        DrawDynamicUI(gWindow);
                    }
                }
                    break;
            }
        }

        UpdateMeter();
        if (gNeedsRedraw && gWindow != NULL) {
            DrawDynamicUI(gWindow);
        }
    }

    return 0;
}
