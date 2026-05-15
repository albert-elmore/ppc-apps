#include <stdio.h>
#include <Sound.h>
#include <OSUtils.h>
#include <Memory.h>

#define SAMPLE_RATE         11025
#define FRAMES_PER_BUFFER   2048
#define MASTER_GAIN         12000.0

#define NOISE_BUFFER_SIZE   4096
#define MAX_SEQUENCE_STEPS  16

typedef struct {
    int division;   /* 8, 16, 32, etc */
    int repeats;    /* number of triggers before advancing */
} SequenceStep;

static SndChannelPtr gChan = NULL;
static SndDoubleBufferHeader gHeader;
static SndDoubleBufferPtr gBufA = NULL;
static SndDoubleBufferPtr gBufB = NULL;
static volatile int gRunning = 1;

static unsigned long gRandState = 1;

/* noise source */
static signed char gNoiseBuffer[NOISE_BUFFER_SIZE];

/* sequence state */
static SequenceStep gSteps[MAX_SEQUENCE_STEPS];
static int gStepCount = 0;
static int gCurrentStepIndex = 0;
static int gCurrentStepTriggerCount = 0;

/* timing */
static double gBPM = 134.0;
static int gPulseMs = 18;
static long gPulseLengthSamples = 0;
static double gSamplesUntilNextTrigger = 0.0;

/* active pulse playback */
static int gPulseActive = 0;
static long gPulseSamplePos = 0;

/* prototypes */
unsigned long NextRand(void);
void SeedRand(unsigned long seed);
long RandRange(long min, long max);
void GenerateNoiseBuffer(void);

double SamplesPerTrigger(double bpm, int division);
void AdvanceToNextStep(void);
void TriggerPulse(void);
void SetBPM(double bpm);
void SetPulseMs(int pulseMs);
void SetSequence(const int *pairs, int pairCount);
void InitSequencer(void);

double PulseEnvelope(long pos, long total);
void FillBuffer(SndDoubleBufferPtr db);
pascal void MyDoubleBackProc(SndChannelPtr channel, SndDoubleBufferPtr doubleBufferPtr);

unsigned long NextRand(void)
{
    gRandState = (gRandState * 1103515245UL) + 12345UL;
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

void GenerateNoiseBuffer(void)
{
    long i;

    for (i = 0; i < NOISE_BUFFER_SIZE; i++) {
        gNoiseBuffer[i] = (signed char)((RandRange(0, 255)) - 128);
    }
}

double SamplesPerTrigger(double bpm, int division)
{
    double secondsPerQuarter;
    double secondsPerTrigger;

    if (bpm <= 0.0) {
        bpm = 120.0;
    }

    if (division <= 0) {
        division = 16;
    }

    secondsPerQuarter = 60.0 / bpm;
    secondsPerTrigger = secondsPerQuarter * (4.0 / (double)division);

    return secondsPerTrigger * (double)SAMPLE_RATE;
}

void AdvanceToNextStep(void)
{
    gCurrentStepIndex++;

    if (gCurrentStepIndex >= gStepCount) {
        gCurrentStepIndex = 0;
    }

    gCurrentStepTriggerCount = 0;
}

void TriggerPulse(void)
{
    gPulseActive = 1;
    gPulseSamplePos = 0;

    gCurrentStepTriggerCount++;

    if (gCurrentStepTriggerCount >= gSteps[gCurrentStepIndex].repeats) {
        AdvanceToNextStep();
    }
}

void SetBPM(double bpm)
{
    if (bpm < 20.0) {
        bpm = 20.0;
    }

    if (bpm > 400.0) {
        bpm = 400.0;
    }

    gBPM = bpm;
    gSamplesUntilNextTrigger = SamplesPerTrigger(gBPM, gSteps[gCurrentStepIndex].division);
}

void SetPulseMs(int pulseMs)
{
    if (pulseMs < 1) {
        pulseMs = 1;
    }

    if (pulseMs > 1000) {
        pulseMs = 1000;
    }

    gPulseMs = pulseMs;
    gPulseLengthSamples = (long)(((double)gPulseMs / 1000.0) * (double)SAMPLE_RATE);

    if (gPulseLengthSamples < 1) {
        gPulseLengthSamples = 1;
    }
}

void SetSequence(const int *pairs, int pairCount)
{
    int i;

    if (pairCount > MAX_SEQUENCE_STEPS) {
        pairCount = MAX_SEQUENCE_STEPS;
    }

    if (pairCount < 1) {
        pairCount = 1;
    }

    gStepCount = pairCount;

    for (i = 0; i < pairCount; i++) {
        int div = pairs[i * 2];
        int reps = pairs[i * 2 + 1];

        if (div <= 0) {
            div = 16;
        }

        if (reps <= 0) {
            reps = 1;
        }

        gSteps[i].division = div;
        gSteps[i].repeats = reps;
    }

    gCurrentStepIndex = 0;
    gCurrentStepTriggerCount = 0;
    gSamplesUntilNextTrigger = SamplesPerTrigger(gBPM, gSteps[0].division);
}

void InitSequencer(void)
{
    static const int seqPairs[] = {
        16, 8,
        32, 16,
        8,  4,
        16, 8
    };

    GenerateNoiseBuffer();
    SetPulseMs(18);
    SetSequence(seqPairs, 4);
    SetBPM(134.0);
}

double PulseEnvelope(long pos, long total)
{
    double env;

    if (total <= 1) {
        return 0.0;
    }

    env = 1.0 - ((double)pos / (double)total);

    if (env < 0.0) {
        env = 0.0;
    }

    return env;
}

void FillBuffer(SndDoubleBufferPtr db)
{
    long i;
    SInt16 *p;

    p = (SInt16 *)db->dbSoundData;
    db->dbNumFrames = FRAMES_PER_BUFFER;
    db->dbFlags = dbBufferReady;

    for (i = 0; i < FRAMES_PER_BUFFER; i++) {
        double sample = 0.0;

        gSamplesUntilNextTrigger -= 1.0;

        if (gSamplesUntilNextTrigger <= 0.0) {
            int currentDivision;

            TriggerPulse();

            currentDivision = gSteps[gCurrentStepIndex].division;
            gSamplesUntilNextTrigger += SamplesPerTrigger(gBPM, currentDivision);
        }

        if (gPulseActive) {
            if (gPulseSamplePos < gPulseLengthSamples) {
                int noiseIndex;
                double raw;
                double env;
                double out;

                noiseIndex = (int)(gPulseSamplePos % NOISE_BUFFER_SIZE);
                raw = (double)gNoiseBuffer[noiseIndex] / 128.0;
                env = PulseEnvelope(gPulseSamplePos, gPulseLengthSamples);

                out = raw * env * MASTER_GAIN;
                sample = out;

                gPulseSamplePos++;
            } else {
                gPulseActive = 0;
                gPulseSamplePos = 0;
            }
        }

        if (sample < -32768.0) sample = -32768.0;
        if (sample >  32767.0) sample =  32767.0;

        p[i] = (SInt16)sample;
    }

    if (!gRunning) {
        db->dbFlags = dbBufferReady | dbLastBuffer;
    }
}

pascal void MyDoubleBackProc(SndChannelPtr channel, SndDoubleBufferPtr doubleBufferPtr)
{
    (void)channel;
    FillBuffer(doubleBufferPtr);
}

int main(void)
{
    OSErr err;
    long dbSize;
    SndDoubleBackUPP myUPP;

    SeedRand((unsigned long)TickCount());

    dbSize = sizeof(SndDoubleBuffer) + ((FRAMES_PER_BUFFER * sizeof(SInt16)) - 1);

    gBufA = (SndDoubleBufferPtr)NewPtrClear(dbSize);
    gBufB = (SndDoubleBufferPtr)NewPtrClear(dbSize);

    if ((gBufA == NULL) || (gBufB == NULL)) {
        printf("Could not allocate double buffers.\n");
        getchar();
        return 1;
    }

    InitSequencer();

    myUPP = NewSndDoubleBackProc(MyDoubleBackProc);

    gHeader.dbhNumChannels = 1;
    gHeader.dbhSampleSize = 16;
    gHeader.dbhCompressionID = 0;
    gHeader.dbhPacketSize = 0;
    gHeader.dbhSampleRate = rate11025hz;
    gHeader.dbhBufferPtr[0] = gBufA;
    gHeader.dbhBufferPtr[1] = gBufB;
    gHeader.dbhDoubleBack = myUPP;

    FillBuffer(gBufA);
    FillBuffer(gBufB);

    err = SndNewChannel(&gChan, sampledSynth, initMono, NULL);
    printf("SndNewChannel: %d\n", err);
    if (err != noErr) {
        DisposePtr((Ptr)gBufA);
        DisposePtr((Ptr)gBufB);
        DisposeSndDoubleBackUPP(myUPP);
        getchar();
        return 1;
    }

    err = SndPlayDoubleBuffer(gChan, &gHeader);
    printf("SndPlayDoubleBuffer: %d\n", err);
    printf("16-bit noise pulse sequencer running.\n");
    printf("BPM: %.1f\n", gBPM);
    printf("Pulse length: %d ms\n", gPulseMs);
    printf("Sequence: 16x8, 32x16, 8x4, 16x8\n");
    printf("Press Return in SIOUX to stop.\n");

    getchar();

    gRunning = 0;
    Delay(30, NULL);

    SndDisposeChannel(gChan, true);
    DisposePtr((Ptr)gBufA);
    DisposePtr((Ptr)gBufB);
    DisposeSndDoubleBackUPP(myUPP);

    printf("Stopped.\n");
    return 0;
}