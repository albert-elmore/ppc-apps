#include <MacTypes.h>
#include <QuickDraw.h>
#include <QuickDrawText.h>
#include <Windows.h>
#include <Events.h>
#include <Menus.h>
#include <ToolUtils.h>
#include <OSUtils.h>
#include <Fonts.h>
#include <TextUtils.h>
#include <Devices.h>
#include <Math.h>
#include <Strings.h>

/*
    Primitive 3D cube visualizer for classic Mac OS / CodeWarrior

    Features:
    - 15x15x15 point cube
    - optional wireframe edges
    - double buffering
    - trails mode
    - hidden cursor
    - status overlay
    - keyboard controls
    - command-Q quit

    Controls:
    Command-Q  quit
    Space      pause/resume
    [ / ]      slower/faster overall rotation
    A / Z      slower/faster X rotation
    S / X      slower/faster Y rotation
    D / C      slower/faster Z rotation
    - / =      zoom out / zoom in
    , / .      smaller / larger dots
    O / P      weaker / stronger perspective
    Arrow keys move cube
    W          toggle wireframe
    T          toggle trails
    J          toggle jitter
    H          toggle overlay
    R          reset defaults
*/

#define GRID_SIZE               15
#define POINT_COUNT             (GRID_SIZE * GRID_SIZE * GRID_SIZE)
#define FRAME_DELAY_TICKS       3
#define MAX_DOT_SIZE            8
#define TRAIL_FADE_PAT          8
#define EDGE_COUNT              12

typedef struct Point3D {
    float x;
    float y;
    float z;
} Point3D;

typedef struct ScreenPoint {
    int x;
    int y;
    Boolean visible;
} ScreenPoint;

typedef struct Edge {
    int a;
    int b;
} Edge;

static WindowPtr gWindow = NULL;
static Rect gScreenRect;
static Point3D gPoints[POINT_COUNT];

static GWorldPtr gOffscreenWorld = NULL;

/* cube corner points for wireframe */
static Point3D gCorners[8] = {
    {-1.0f, -1.0f, -1.0f},
    { 1.0f, -1.0f, -1.0f},
    { 1.0f,  1.0f, -1.0f},
    {-1.0f,  1.0f, -1.0f},
    {-1.0f, -1.0f,  1.0f},
    { 1.0f, -1.0f,  1.0f},
    { 1.0f,  1.0f,  1.0f},
    {-1.0f,  1.0f,  1.0f}
};

static Edge gEdges[EDGE_COUNT] = {
    {0,1},{1,2},{2,3},{3,0},
    {4,5},{5,6},{6,7},{7,4},
    {0,4},{1,5},{2,6},{3,7}
};

static float gAngleX = 0.0f;
static float gAngleY = 0.0f;
static float gAngleZ = 0.0f;

static float gRotSpeedX = 0.012f;
static float gRotSpeedY = 0.017f;
static float gRotSpeedZ = 0.008f;

static float gZoom = 1.0f;
static int   gDotSize = 1;
static float gPerspective = 220.0f;

static int gCenterX = 0;
static int gCenterY = 0;
static int gOffsetX = 0;
static int gOffsetY = 0;
static float gBaseScale = 0.0f;

static Boolean gPaused = false;
static Boolean gShowOverlay = true;
static Boolean gWireframe = false;
static Boolean gTrails = false;
static Boolean gJitter = false;

/* ------------------------------------------------------------------------- */

static int MyRandRange(int range)
{
    if (range <= 0) return 0;
    return (Random() % (range * 2 + 1)) - range;
}

static void NumToPStr(short value, Str255 out)
{
    NumToString(value, out);
}

static void FloatTimes100ToPStr(float value, Str255 out)
{
    long n = (long)(value * 100.0f);
    long whole = n / 100;
    long frac = n % 100;
    Str255 a, b, temp;

    if (frac < 0) frac = -frac;

    NumToString((long)whole, a);
    NumToString((long)frac, b);

    temp[0] = 0;
    BlockMove(a + 1, temp + 1, a[0]);
    temp[0] = a[0];

    temp[++temp[0]] = '.';

    if (frac < 10) {
        temp[++temp[0]] = '0';
        if (b[0] >= 1) {
            temp[++temp[0]] = b[1];
        } else {
            temp[++temp[0]] = '0';
        }
    } else {
        if (b[0] >= 1) temp[++temp[0]] = b[1];
        if (b[0] >= 2) temp[++temp[0]] = b[2];
    }

    BlockMove(temp, out, temp[0] + 1);
}

/* ------------------------------------------------------------------------- */

static void ResetControls(void)
{
    gRotSpeedX = 0.012f;
    gRotSpeedY = 0.017f;
    gRotSpeedZ = 0.008f;
    gZoom = 1.0f;
    gDotSize = 1;
    gPerspective = 220.0f;
    gOffsetX = 0;
    gOffsetY = 0;
    gPaused = false;
    gShowOverlay = true;
    gWireframe = false;
    gTrails = false;
    gJitter = false;
}

static void InitToolboxStuff(void)
{
    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);
    InitCursor();
}

static void BuildPointCloud(void)
{
    int i = 0;
    int x, y, z;

    for (z = 0; z < GRID_SIZE; z++) {
        for (y = 0; y < GRID_SIZE; y++) {
            for (x = 0; x < GRID_SIZE; x++) {
                gPoints[i].x = ((float)x / (float)(GRID_SIZE - 1)) * 2.0f - 1.0f;
                gPoints[i].y = ((float)y / (float)(GRID_SIZE - 1)) * 2.0f - 1.0f;
                gPoints[i].z = ((float)z / (float)(GRID_SIZE - 1)) * 2.0f - 1.0f;
                i++;
            }
        }
    }
}

static void SetupWindow(void)
{
    gScreenRect = qd.screenBits.bounds;

    gWindow = NewCWindow(
        NULL,
        &gScreenRect,
        "\p",
        true,
        plainDBox,
        (WindowPtr)-1L,
        false,
        0L
    );

    SetPort(gWindow);

    gCenterX = (gScreenRect.left + gScreenRect.right) / 2;
    gCenterY = (gScreenRect.top + gScreenRect.bottom) / 2;

    {
        int w = gScreenRect.right - gScreenRect.left;
        int h = gScreenRect.bottom - gScreenRect.top;
        int minDim = (w < h) ? w : h;
        gBaseScale = (float)minDim * 0.28f;
    }
}

static OSErr SetupOffscreenWorld(void)
{
    OSErr err;
    GDHandle oldDevice;
    GWorldPtr oldWorld;
    PixMapHandle pm;

    GetGWorld(&oldWorld, &oldDevice);

    err = NewGWorld(&gOffscreenWorld, 0, &gScreenRect, NULL, NULL, 0);
    if (err != noErr || gOffscreenWorld == NULL) {
        return err;
    }

    pm = GetGWorldPixMap(gOffscreenWorld);
    if (pm == NULL) {
        DisposeGWorld(gOffscreenWorld);
        gOffscreenWorld = NULL;
        return memFullErr;
    }

    if (!LockPixels(pm)) {
        DisposeGWorld(gOffscreenWorld);
        gOffscreenWorld = NULL;
        return memFullErr;
    }

    SetGWorld(oldWorld, oldDevice);
    return noErr;
}

static void ShutdownOffscreenWorld(void)
{
    if (gOffscreenWorld != NULL) {
        PixMapHandle pm = GetGWorldPixMap(gOffscreenWorld);
        if (pm != NULL) {
            UnlockPixels(pm);
        }
        DisposeGWorld(gOffscreenWorld);
        gOffscreenWorld = NULL;
    }
}

/* ------------------------------------------------------------------------- */

static void ClearCurrentPortBlack(void)
{
    Rect r = gScreenRect;
    RGBColor black = {0, 0, 0};

    RGBBackColor(&black);
    RGBForeColor(&black);
    PaintRect(&r);
}

static void FadeCurrentPort(void)
{
    Rect r = gScreenRect;
    PenState oldPen;

    GetPenState(&oldPen);
    PenPat(&qd.gray);
    PenMode(patBic);
    PaintRect(&r);
    SetPenState(&oldPen);
}

static void DrawDot(int x, int y)
{
    Rect r;
    RGBColor white = {65535, 65535, 65535};

    r.left   = x;
    r.top    = y;
    r.right  = x + gDotSize;
    r.bottom = y + gDotSize;

    RGBForeColor(&white);
    PaintRect(&r);
}

static void DrawLineWhite(int x1, int y1, int x2, int y2)
{
    RGBColor white = {65535, 65535, 65535};
    RGBForeColor(&white);
    MoveTo(x1, y1);
    LineTo(x2, y2);
}

static void RotatePoint(const Point3D *in, Point3D *out, float ax, float ay, float az)
{
    float sx = sin(ax), cx = cos(ax);
    float sy = sin(ay), cy = cos(ay);
    float sz = sin(az), cz = cos(az);

    float x, y, z;
    float x1, y1, z1;
    float x2, y2, z2;

    x = in->x;
    y = in->y * cx - in->z * sx;
    z = in->y * sx + in->z * cx;

    x1 = x * cy + z * sy;
    y1 = y;
    z1 = -x * sy + z * cy;

    x2 = x1 * cz - y1 * sz;
    y2 = x1 * sz + y1 * cz;
    z2 = z1;

    out->x = x2;
    out->y = y2;
    out->z = z2;
}

static void ProjectPoint(const Point3D *p, ScreenPoint *sp)
{
    const float cameraDistance = 4.0f;
    float scale = gBaseScale * gZoom;
    float zf = p->z + cameraDistance;

    sp->visible = false;

    if (zf > 0.1f) {
        int jx = 0;
        int jy = 0;

        if (gJitter) {
            jx = MyRandRange(1);
            jy = MyRandRange(1);
        }

        sp->x = gCenterX + gOffsetX + (int)((p->x * scale * gPerspective) / (zf * 100.0f)) + jx;
        sp->y = gCenterY + gOffsetY + (int)((p->y * scale * gPerspective) / (zf * 100.0f)) + jy;

        if (sp->x >= gScreenRect.left && sp->x < gScreenRect.right &&
            sp->y >= gScreenRect.top  && sp->y < gScreenRect.bottom) {
            sp->visible = true;
        }
    }
}

static void DrawOverlayLine(short x, short y, ConstStr255Param label, ConstStr255Param value)
{
    MoveTo(x, y);
    DrawString(label);
    DrawString(value);
}

static void RenderOverlay(void)
{
    Str255 s1, s2, s3, s4, s5, s6, s7, s8, s9;
    Str255 temp;

    RGBColor white = {65535, 65535, 65535};
    TextFont(4);      /* Monaco usually */
    TextSize(9);
    RGBForeColor(&white);

    FloatTimes100ToPStr(gZoom, s1);
    NumToPStr((short)gDotSize, s2);
    FloatTimes100ToPStr(gRotSpeedX, s3);
    FloatTimes100ToPStr(gRotSpeedY, s4);
    FloatTimes100ToPStr(gRotSpeedZ, s5);
    NumToPStr((short)gPerspective, s6);
    NumToPStr((short)gOffsetX, s7);
    NumToPStr((short)gOffsetY, s8);

    temp[0] = 0;
    if (gPaused) {
        CopyPascalString("\pON", temp);
    } else {
        CopyPascalString("\pOFF", temp);
    }
    BlockMove(temp, s9, temp[0] + 1);

    DrawOverlayLine(12, 16, "\pzoom: ", s1);
    DrawOverlayLine(12, 28, "\pdot: ", s2);
    DrawOverlayLine(12, 40, "\prx: ", s3);
    DrawOverlayLine(12, 52, "\pry: ", s4);
    DrawOverlayLine(12, 64, "\prz: ", s5);
    DrawOverlayLine(12, 76, "\ppersp: ", s6);
    DrawOverlayLine(12, 88, "\px: ", s7);
    DrawOverlayLine(12,100, "\py: ", s8);
    DrawOverlayLine(12,112, "\ppaused: ", s9);

    DrawOverlayLine(12,136, "\pwireframe: ", gWireframe ? "\pON" : "\pOFF");
    DrawOverlayLine(12,148, "\ptrails: ", gTrails ? "\pON" : "\pOFF");
    DrawOverlayLine(12,160, "\pjitter: ", gJitter ? "\pON" : "\pOFF");

    DrawOverlayLine(12,184, "\p[ ] speed  - = zoom  , . dot", "\p");
    DrawOverlayLine(12,196, "\pA/Z S/X D/C axis speed", "\p");
    DrawOverlayLine(12,208, "\pO/P perspective  arrows move", "\p");
    DrawOverlayLine(12,220, "\pW wire  T trails  J jitter", "\p");
    DrawOverlayLine(12,232, "\pH overlay  Space pause  R reset", "\p");
}

static void RenderWireframe(void)
{
    int i;
    for (i = 0; i < EDGE_COUNT; i++) {
        Point3D ra, rb;
        ScreenPoint सा, sb;

        RotatePoint(&gCorners[gEdges[i].a], &ra, gAngleX, gAngleY, gAngleZ);
        RotatePoint(&gCorners[gEdges[i].b], &rb, gAngleX, gAngleY, gAngleZ);

        ProjectPoint(&ra, &सा);
        ProjectPoint(&rb, &sb);

        if (सा.visible && sb.visible) {
            DrawLineWhite(सा.x, सा.y, sb.x, sb.y);
        }
    }
}

static void RenderPoints(void)
{
    int i;
    for (i = 0; i < POINT_COUNT; i++) {
        Point3D p;
        ScreenPoint sp;

        RotatePoint(&gPoints[i], &p, gAngleX, gAngleY, gAngleZ);
        ProjectPoint(&p, &sp);

        if (sp.visible) {
            DrawDot(sp.x, sp.y);
        }
    }
}

static void RenderFrame(void)
{
    GDHandle oldDevice;
    GWorldPtr oldWorld;

    if (gOffscreenWorld == NULL || gWindow == NULL) {
        return;
    }

    GetGWorld(&oldWorld, &oldDevice);
    SetGWorld(gOffscreenWorld, NULL);

    if (gTrails) {
        FadeCurrentPort();
    } else {
        ClearCurrentPortBlack();
    }

    RenderPoints();

    if (gWireframe) {
        RenderWireframe();
    }

    if (gShowOverlay) {
        RenderOverlay();
    }

    SetPort(gWindow);
    CopyBits(
        (BitMap *)(*GetGWorldPixMap(gOffscreenWorld)),
        &gWindow->portBits,
        &gScreenRect,
        &gScreenRect,
        srcCopy,
        NULL
    );

    SetGWorld(oldWorld, oldDevice);
}

static void ClampControls(void)
{
    if (gZoom < 0.1f) gZoom = 0.1f;
    if (gZoom > 5.0f) gZoom = 5.0f;

    if (gDotSize < 1) gDotSize = 1;
    if (gDotSize > MAX_DOT_SIZE) gDotSize = MAX_DOT_SIZE;

    if (gPerspective < 40.0f) gPerspective = 40.0f;
    if (gPerspective > 800.0f) gPerspective = 800.0f;
}

static Boolean HandleArrowKey(UInt16 keyCode)
{
    switch (keyCode) {
        case 123:  /* left */
            gOffsetX -= 10;
            return true;
        case 124:  /* right */
            gOffsetX += 10;
            return true;
        case 125:  /* down */
            gOffsetY += 10;
            return true;
        case 126:  /* up */
            gOffsetY -= 10;
            return true;
    }
    return false;
}

static Boolean HandleKey(char c, UInt16 modifiers, UInt16 keyCode)
{
    if ((modifiers & cmdKey) && (c == 'q' || c == 'Q')) {
        return true;
    }

    if (HandleArrowKey(keyCode)) {
        ClampControls();
        return false;
    }

    switch (c) {
        case ' ':
            gPaused = !gPaused;
            break;

        case '[':
            gRotSpeedX *= 0.90f;
            gRotSpeedY *= 0.90f;
            gRotSpeedZ *= 0.90f;
            break;

        case ']':
            gRotSpeedX *= 1.10f;
            gRotSpeedY *= 1.10f;
            gRotSpeedZ *= 1.10f;
            break;

        case 'a':
        case 'A':
            gRotSpeedX *= 0.90f;
            break;

        case 'z':
        case 'Z':
            gRotSpeedX *= 1.10f;
            break;

        case 's':
        case 'S':
            gRotSpeedY *= 0.90f;
            break;

        case 'x':
        case 'X':
            gRotSpeedY *= 1.10f;
            break;

        case 'd':
        case 'D':
            gRotSpeedZ *= 0.90f;
            break;

        case 'c':
        case 'C':
            gRotSpeedZ *= 1.10f;
            break;

        case '-':
            gZoom *= 0.90f;
            break;

        case '=':
        case '+':
            gZoom *= 1.10f;
            break;

        case ',':
        case '<':
            gDotSize--;
            break;

        case '.':
        case '>':
            gDotSize++;
            break;

        case 'o':
        case 'O':
            gPerspective *= 0.90f;
            break;

        case 'p':
        case 'P':
            gPerspective *= 1.10f;
            break;

        case 'w':
        case 'W':
            gWireframe = !gWireframe;
            break;

        case 't':
        case 'T':
            gTrails = !gTrails;
            if (!gTrails) {
                /* clear old smears immediately */
                SetGWorld(gOffscreenWorld, NULL);
                ClearCurrentPortBlack();
            }
            break;

        case 'j':
        case 'J':
            gJitter = !gJitter;
            break;

        case 'h':
        case 'H':
            gShowOverlay = !gShowOverlay;
            break;

        case 'r':
        case 'R':
            ResetControls();
            break;
    }

    ClampControls();
    return false;
}

static Boolean HandleEvent(EventRecord *event)
{
    char c;
    UInt16 keyCode;

    switch (event->what) {
        case keyDown:
        case autoKey:
            c = (char)(event->message & charCodeMask);
            keyCode = (UInt16)((event->message & keyCodeMask) >> 8);
            return HandleKey(c, event->modifiers, keyCode);

        case updateEvt:
            BeginUpdate((WindowPtr)event->message);
            RenderFrame();
            EndUpdate((WindowPtr)event->message);
            break;

        case mouseDown:
            break;
    }

    return false;
}

/* ------------------------------------------------------------------------- */

int main(void)
{
    EventRecord event;
    UInt32 lastFrameTick = TickCount();
    Boolean done = false;
    OSErr err;

    InitToolboxStuff();
    BuildPointCloud();
    SetupWindow();
    ResetControls();

    err = SetupOffscreenWorld();
    if (err != noErr) {
        return 1;
    }

    SetPort(gWindow);
    HideCursor();

    RenderFrame();

    while (!done) {
        while (WaitNextEvent(everyEvent, &event, 1L, NULL)) {
            if (HandleEvent(&event)) {
                done = true;
                break;
            }
        }

        if ((TickCount() - lastFrameTick) >= FRAME_DELAY_TICKS) {
            if (!gPaused) {
                gAngleX += gRotSpeedX;
                gAngleY += gRotSpeedY;
                gAngleZ += gRotSpeedZ;
            }

            RenderFrame();
            lastFrameTick = TickCount();
        }
    }

    ShowCursor();
    ShutdownOffscreenWorld();
    return 0;
}
