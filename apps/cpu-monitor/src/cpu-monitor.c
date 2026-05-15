#include <stdio.h>
#include <OSUtils.h>
#include <Windows.h>
#include <Controls.h>
#include <Events.h>
#include <Dialogs.h>
#include <Quickdraw.h>
#include <Fonts.h>
#include <TextEdit.h>
#include <Menus.h>

/* Explicit prototypes for this compiler setup */
pascal unsigned long TickCount(void);
pascal void Delay(unsigned long numTicks, unsigned long *finalTicks);

#define WINDOW_LEFT            60
#define WINDOW_TOP             60
#define WINDOW_RIGHT           500
#define WINDOW_BOTTOM          320

#define SAMPLE_TICKS_BASELINE  20
#define SAMPLE_TICKS_RUNTIME   6
#define UPDATE_INTERVAL_TICKS  20
#define BAR_WIDTH_PIXELS       300
#define BAR_HEIGHT_PIXELS      18
#define HISTORY_SIZE           8

static WindowPtr gWindow = NULL;
static ControlHandle gRecalButton = NULL;
static ControlHandle gQuitButton = NULL;

static Rect gBarFrame;
static Rect gStatusRect;
static Rect gDetailRect;
static Rect gWarningRect;

static unsigned long gBaselineLoops = 0;
static unsigned long gLastCurrentLoops = 0;
static int gCurrentPercent = 0;
static int gSmoothedPercent = 0;
static unsigned long gLastUpdateTick = 0;
static int gHistory[HISTORY_SIZE];
static int gHistoryIndex = 0;
static int gHistoryCount = 0;
static int gNeedsRedraw = 1;

static unsigned long MeasureLoops(short ticks);
static void InitUI(void);
static void LayoutUI(void);
static void DrawStaticUI(WindowPtr w);
static void DrawDynamicUI(WindowPtr w);
static void DoUpdate(EventRecord *event);
static void DoActivate(EventRecord *event);
static void DoMouseDown(EventRecord *event, int *running);
static void RecalibrateBaseline(void);
static void ResetSmoothing(void);
static void UpdateMeterIfDue(void);
static void AddPercentSample(int percent);
static int ComputeSmoothedPercent(void);
static void DrawCString(const char *text);
static void MakePString(const char *src, Str255 dst);
static unsigned long NormalizeLoopsTo20Ticks(unsigned long loops, short ticks);

static unsigned long MeasureLoops(short ticks)
{
    unsigned long count;
    unsigned long start;
    unsigned long now;
    volatile unsigned long sink;

    count = 0;
    sink = 0;
    start = TickCount();

    do {
        sink += (count ^ 0x13579BDFUL);
        sink = (sink << 1) ^ (sink >> 1);
        count++;
        now = TickCount();
    } while ((unsigned long)(now - start) < (unsigned long)ticks);

    return count;
}

static unsigned long NormalizeLoopsTo20Ticks(unsigned long loops, short ticks)
{
    if (ticks <= 0) {
        return 0;
    }
    return (unsigned long)(((unsigned long)loops * (unsigned long)SAMPLE_TICKS_BASELINE) / (unsigned long)ticks);
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
    SetRect(&gDetailRect, 70, 158, 440, 204);
    SetRect(&gWarningRect, 70, 214, 440, 274);

    MoveControl(gRecalButton, 70, 32);
    MoveControl(gQuitButton, 360, 32);
}

static void DrawStaticUI(WindowPtr w)
{
    SetPort(w);
    EraseRect(&w->portRect);

    MoveTo(70, 72);
    DrawString("\pCPU Utilization");

    FrameRect(&gBarFrame);

    MoveTo(70, 286);
    DrawString("\pLighter sampling: short probes, delayed updates, smoothed readout.");
    MoveTo(70, 302);
    DrawString("\pUse Recalibrate when the machine is mostly idle.");
}

static void DrawDynamicUI(WindowPtr w)
{
    Rect fillRect;
    Rect clearRect;
    char temp[160];
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
    sprintf(temp, "Current CPU Used: %d%%", gSmoothedPercent);
    MoveTo(gStatusRect.left, gStatusRect.bottom - 4);
    DrawCString(temp);

    EraseRect(&gDetailRect);
    sprintf(temp, "Baseline loops/20 ticks: %lu   Sample loops/%d ticks: %lu",
            gBaselineLoops, SAMPLE_TICKS_RUNTIME, gLastCurrentLoops);
    MoveTo(gDetailRect.left, gDetailRect.top + 12);
    DrawCString(temp);
    sprintf(temp, "Instant: %d%%   Update cadence: every %d ticks",
            gCurrentPercent, UPDATE_INTERVAL_TICKS);
    MoveTo(gDetailRect.left, gDetailRect.top + 30);
    DrawCString(temp);

    EraseRect(&gWarningRect);
    if (gBaselineLoops == 0) {
        MoveTo(gWarningRect.left, gWarningRect.top + 12);
        DrawString("\pBaseline unavailable.");
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

static void ResetSmoothing(void)
{
    int i;

    gCurrentPercent = 0;
    gSmoothedPercent = 0;
    gLastCurrentLoops = 0;
    gHistoryIndex = 0;
    gHistoryCount = 0;
    for (i = 0; i < HISTORY_SIZE; i++) {
        gHistory[i] = 0;
    }
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

static void RecalibrateBaseline(void)
{
    unsigned long first;
    unsigned long second;
    unsigned long third;

    ResetSmoothing();

    /* A few short passes reduce startup flukes without hammering the machine. */
    first = MeasureLoops(SAMPLE_TICKS_BASELINE);
    Delay(6, NULL);
    second = MeasureLoops(SAMPLE_TICKS_BASELINE);
    Delay(6, NULL);
    third = MeasureLoops(SAMPLE_TICKS_BASELINE);

    gBaselineLoops = first;
    if (second > gBaselineLoops) {
        gBaselineLoops = second;
    }
    if (third > gBaselineLoops) {
        gBaselineLoops = third;
    }

    gLastUpdateTick = TickCount();
    gNeedsRedraw = 1;
}

static void UpdateMeterIfDue(void)
{
    unsigned long now;
    unsigned long current;
    unsigned long normalizedCurrent;
    int percent;
    double ratio;

    now = TickCount();
    if ((unsigned long)(now - gLastUpdateTick) < (unsigned long)UPDATE_INTERVAL_TICKS) {
        return;
    }

    gLastUpdateTick = now;
    current = MeasureLoops(SAMPLE_TICKS_RUNTIME);
    gLastCurrentLoops = current;
    normalizedCurrent = NormalizeLoopsTo20Ticks(current, SAMPLE_TICKS_RUNTIME);

    if (gBaselineLoops == 0 || normalizedCurrent >= gBaselineLoops) {
        percent = 0;
    } else {
        ratio = (double)normalizedCurrent / (double)gBaselineLoops;
        percent = (int)(100.0 * (1.0 - ratio));
    }

    if (percent < 0) {
        percent = 0;
    }
    if (percent > 100) {
        percent = 100;
    }

    gCurrentPercent = percent;
    AddPercentSample(percent);
    gSmoothedPercent = ComputeSmoothedPercent();
    gNeedsRedraw = 1;
}

static void InitUI(void)
{
    Rect wr;
    Rect r;

    SetRect(&wr, WINDOW_LEFT, WINDOW_TOP, WINDOW_RIGHT, WINDOW_BOTTOM);
    gWindow = NewWindow(NULL, &wr, "\pCPU Monitor", true, zoomDocProc, (WindowPtr)-1, true, 0);

    SetRect(&r, 70, 32, 170, 52);
    gRecalButton = NewControl(gWindow, &r, "\pRecalibrate", true, 0, 0, 1, pushButProc, 0);

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
                    if (ctrl == gRecalButton) {
                        if (TrackControl(ctrl, localPt, NULL) != 0) {
                            RecalibrateBaseline();
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
    RecalibrateBaseline();
    DrawDynamicUI(gWindow);

    while (running) {
        if (WaitNextEvent(everyEvent, &event, 6, NULL)) {
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
                        RecalibrateBaseline();
                        DrawDynamicUI(gWindow);
                    }
                }
                    break;
            }
        }

        UpdateMeterIfDue();
        if (gNeedsRedraw && gWindow != NULL) {
            DrawDynamicUI(gWindow);
        }
    }

    return 0;
}
