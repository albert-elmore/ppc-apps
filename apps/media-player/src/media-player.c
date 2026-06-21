/*
 * G3 Stage Player - single file for CodeWarrior (Mac OS 9).
 * Project source: apps/media-player/src/media-player.c
 */

#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <math.h>
#include <stdlib.h>
#include <Sound.h>
#include <OSUtils.h>
#include <Memory.h>
#include <Windows.h>
#include <Controls.h>
#include <Events.h>
#include <Dialogs.h>
#include <Quickdraw.h>
#include <StandardFile.h>
#include <Files.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Scroll bar part codes (IM: Controls) — define if SDK omits them */
#ifndef inButton
#define inButton 10
#endif
#ifndef inUpButton
#define inUpButton 20
#define inDownButton 21
#define inPageUp 22
#define inPageDown 23
#define inThumb 129
#endif

/* ---------- audio engine (was g3_player.h + g3_player.c) ---------- */

#define G3_DECK_COUNT 3
#define G3_SAMPLE_RATE 44100
#define G3_FRAMES_PER_BUFFER 1024
#define G3_STREAM_RING_SECONDS 3
#define G3_STREAM_RING_FRAMES ((unsigned long)G3_SAMPLE_RATE * (unsigned long)G3_STREAM_RING_SECONDS)
#define G3_STREAM_PREFETCH_FRAMES ((unsigned long)G3_SAMPLE_RATE * 2UL)

/* ---------- freeze / granular (tweak and rebuild) ---------- */
#define G3_FREEZE_WINDOW_MS           1000
#define G3_GRAIN_DURATION_MS          200
#define G3_GRAIN_SPAWN_INTERVAL_MS    10  /* higher = less CPU in audio callback */
#define G3_GRAIN_FADE_MS              40  /* Hann fade in/out; capped to half grain length */
#define G3_FREEZE_MAX_GRAINS          25
#define G3_FREEZE_OUTPUT_GAIN         0.60f /* per-grain level before fixed headroom scale */
#define G3_FREEZE_MIX_HEADROOM        0.32f /* ~1/sqrt(max grains); avoids pumping pops */
#define G3_FREEZE_CROSSFADE_MS        40   /* fade between play and freeze on toggle */
#define G3_FREEZE_MIN_PLAYBACK_SPEED  0.08 /* grains need min speed; extreme pitch down must not stall CPU */
#define G3_GRAIN_MAX_WALL_SAMPLES     (G3_SAMPLE_RATE * 3) /* max output samples per grain regardless of pitch */

typedef enum G3FreezeXfadeDir {
    G3_FREEZE_XFADE_NONE = 0,
    G3_FREEZE_XFADE_IN = 1,
    G3_FREEZE_XFADE_OUT = 2
} G3FreezeXfadeDir;

typedef enum G3TransportState {
    G3_TRANSPORT_STOPPED = 0,
    G3_TRANSPORT_PLAYING = 1,
    G3_TRANSPORT_PAUSED = 2,
    G3_TRANSPORT_FROZEN = 3
} G3TransportState;

typedef struct G3Grain {
    int active;
    unsigned long start_frame; /* same window for L/R so stereo stays coherent */
    double age;
    unsigned long length;
    unsigned long wall_samples; /* output samples this grain has lived (caps CPU at low pitch) */
} G3Grain;

typedef struct G3FreezeCapture {
    short* buffer;
    unsigned long frame_count;
    int channels;
    unsigned long grain_length_samples;
    unsigned long fade_length_samples;
    unsigned long spawn_interval_samples;
    double spawn_accum;
    double playback_speed;
    int xfade_dir;
    unsigned long xfade_remain;
    unsigned long xfade_total;
    G3Grain grains[G3_FREEZE_MAX_GRAINS];
} G3FreezeCapture;

typedef struct G3DeckStatus {
    int loaded;
    G3TransportState state;
    float volume;
    float pan;
    float pitch_percent;
    float ramp_seconds;
    unsigned long duration_ms;
    unsigned long position_ms;
    float peak_dbfs;
} G3DeckStatus;

typedef struct G3MasterStatus {
    float master_volume;
    float peak_dbfs;
} G3MasterStatus;

typedef enum G3WavBackend {
    G3_WAV_NONE = 0,
    G3_WAV_MEMORY = 1,
    G3_WAV_FILE = 2
} G3WavBackend;

typedef struct G3WavData {
    G3WavBackend backend;
    short refNum;
    short scanRefNum;
    FILE* stdio_file;
    unsigned long data_offset;
    unsigned long pcm_bytes;
    unsigned long frame_count;
    int channels;
    short* samples;
    short* ring_a;
    short* ring_b;
    int ring_play_a;
    int ring_fill_pending;
    unsigned long ring_fill_start;
    unsigned long ring_fill_count;
    unsigned long ring_cap;
    unsigned long ring_start;
    unsigned long ring_count;
} G3WavData;

typedef struct G3Deck {
    G3WavData wav;
    int loaded;
    G3TransportState state;
    double frame_pos;
    double speed_current;
    double speed_target;
    float pitch_percent;
    float volume;
    float pan;
    float volume_now;
    float pan_now;
    float ramp_seconds;
    float peak_linear;
    G3FreezeCapture freeze;
} G3Deck;

typedef struct G3Player {
    G3Deck decks[G3_DECK_COUNT];
    float master_volume;
    float master_peak_linear;
} G3Player;

void g3_player_init(G3Player* player);
void g3_player_shutdown(G3Player* player);
int g3_player_load_wav_bytes(G3Player* player, int deck_index, const void* bytes, unsigned long size);
int g3_player_load_wav(G3Player* player, int deck_index, const char* path);
void g3_player_unload(G3Player* player, int deck_index);
void g3_player_play(G3Player* player, int deck_index);
void g3_player_pause(G3Player* player, int deck_index);
void g3_player_stop(G3Player* player, int deck_index);
int g3_player_toggle_freeze(G3Player* player, int deck_index);
void g3_player_seek_ms(G3Player* player, int deck_index, unsigned long position_ms);
void g3_player_set_volume(G3Player* player, int deck_index, float value);
void g3_player_set_pan(G3Player* player, int deck_index, float value);
void g3_player_set_pitch_percent(G3Player* player, int deck_index, float value);
void g3_player_set_ramp_seconds(G3Player* player, int deck_index, float value);
void g3_player_set_master_volume(G3Player* player, float value);
void g3_player_get_deck_status(const G3Player* player, int deck_index, G3DeckStatus* out_status);
void g3_player_get_master_status(const G3Player* player, G3MasterStatus* out_status);
int g3_player_render(G3Player* player, short* interleaved_stereo_out, int frame_count);

/* Forward declarations — CodeWarrior requires prototypes before use. */
static void g3_wav_close(G3WavData* wav);
static int g3_wav_open_ring(G3WavData* wav);
static int g3_wav_read_pcm_at(G3WavData* wav, unsigned long start_frame, unsigned long num_frames, short* dst);
static int g3_wav_read_pcm_ring(G3WavData* wav, unsigned long start_frame, unsigned long num_frames, short* dst);
static int g3_wav_stage_fill(G3WavData* wav, unsigned long start_frame);
static void g3_wav_invalidate_ring(G3WavData* wav);
static void g3_wav_ring_try_swap(G3Deck* deck);
static void g3_wav_get_frame(const G3WavData* wav, unsigned long frame, short* out_l, short* out_r);
static int g3_wav_ring_has_frames(const G3WavData* wav, unsigned long frame_lo, unsigned long frame_hi);
static void g3_wav_pump_deck(G3Deck* deck);
static short g3_fclip_to_i16(double v);

static int g3_clamp_deck_index(int deck_index) {
    return deck_index >= 0 && deck_index < G3_DECK_COUNT;
}

static float g3_clampf(float value, float lo, float hi) {
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

static float g3_linear_to_dbfs(float linear_peak) {
    float v = linear_peak;
    if (v < 0.000001f) return -120.0f;
    return (float)(20.0 * log10((double)v));
}

static float g3_db_to_linear(float db) {
    return (float)pow(10.0, (double)db / 20.0);
}

static unsigned long g3_ms_to_frames(unsigned long ms) {
    return (unsigned long)((ms * (unsigned long)G3_SAMPLE_RATE) / 1000UL);
}

static unsigned long gFreezeRng = 1;
static short* gFreezePendingFree[G3_DECK_COUNT];

static unsigned long g3_freeze_rand(void) {
    gFreezeRng = (gFreezeRng * 1103515245UL) + 12345UL;
    return (gFreezeRng >> 16) & 32767UL;
}

static void g3_freeze_drain_pending(void) {
    int d;
    for (d = 0; d < G3_DECK_COUNT; ++d) {
        if (gFreezePendingFree[d] != NULL) {
            free(gFreezePendingFree[d]);
            gFreezePendingFree[d] = NULL;
        }
    }
}

/* Detach capture buffer; actual free happens at next audio buffer boundary. */
static void g3_freeze_release_deck(int deck_index, G3FreezeCapture* fz) {
    int g;
    if (fz == NULL) return;
    if (fz->buffer != NULL) {
        if (deck_index >= 0 && deck_index < G3_DECK_COUNT) {
            if (gFreezePendingFree[deck_index] != NULL) free(gFreezePendingFree[deck_index]);
            gFreezePendingFree[deck_index] = fz->buffer;
        } else {
            free(fz->buffer);
        }
        fz->buffer = NULL;
    }
    for (g = 0; g < G3_FREEZE_MAX_GRAINS; ++g) fz->grains[g].active = 0;
    fz->spawn_accum = 0.0;
    fz->frame_count = 0;
    fz->channels = 0;
    fz->grain_length_samples = 0;
    fz->fade_length_samples = 0;
    fz->spawn_interval_samples = 0;
    fz->playback_speed = 1.0;
    fz->xfade_dir = G3_FREEZE_XFADE_NONE;
    fz->xfade_remain = 0;
    fz->xfade_total = 0;
}

static void g3_freeze_start_xfade(G3FreezeCapture* fz, int dir) {
    unsigned long samples;
    if (fz == NULL) return;
    samples = g3_ms_to_frames((unsigned long)G3_FREEZE_CROSSFADE_MS);
    if (samples < 1) samples = 1;
    fz->xfade_dir = dir;
    fz->xfade_total = samples;
    fz->xfade_remain = samples;
}

static void g3_freeze_begin_unfreeze(G3Deck* deck) {
    G3FreezeCapture* fz;
    if (deck == NULL) return;
    fz = &deck->freeze;
    fz->spawn_accum = 0.0;
    g3_freeze_start_xfade(fz, G3_FREEZE_XFADE_OUT);
}

static void g3_freeze_end_capture(G3Deck* deck, int deck_index) {
    if (deck == NULL) return;
    deck->state = G3_TRANSPORT_PAUSED;
    g3_freeze_release_deck(deck_index, &deck->freeze);
}

static int g3_spawn_grain(G3FreezeCapture* fz);

static double g3_deck_pitch_speed(const G3Deck* deck) {
    double s;
    if (deck == NULL) return 1.0;
    s = 1.0 + ((double)deck->pitch_percent / 100.0);
    if (s < 0.000001) s = 0.000001;
    return s;
}

static double g3_freeze_pitch_speed(const G3Deck* deck) {
    double s;
    if (deck == NULL) return 1.0;
    s = g3_deck_pitch_speed(deck);
    if (s < G3_FREEZE_MIN_PLAYBACK_SPEED) s = G3_FREEZE_MIN_PLAYBACK_SPEED;
    return s;
}

static void g3_freeze_seed_grains(G3FreezeCapture* fz) {
    int g;
    if (fz == NULL) return;
    fz->spawn_accum = 0.0;
    for (g = 0; g < G3_FREEZE_MAX_GRAINS; ++g) {
        fz->grains[g].active = 0;
        fz->grains[g].age = 0.0;
        fz->grains[g].length = 0;
        fz->grains[g].wall_samples = 0;
    }
    for (g = 0; g < 3; ++g) {
        if (g3_spawn_grain(fz)) {
            unsigned long stagger = (fz->grain_length_samples * (unsigned long)g) / 3UL;
            if (stagger >= fz->grains[g].length) stagger = fz->grains[g].length - 1;
            fz->grains[g].age = (double)stagger;
        }
    }
}

static void g3_deck_resume_from_freeze(G3Deck* deck) {
    if (deck == NULL) return;
    deck->speed_target = g3_deck_pitch_speed(deck);
    deck->speed_current = deck->speed_target;
}

static void g3_deck_reset(G3Deck* deck, int deck_index) {
    if (deck == NULL) return;
    g3_freeze_release_deck(deck_index, &deck->freeze);
    g3_wav_close(&deck->wav);
    memset(deck, 0, sizeof(*deck));
    deck->wav.refNum = -1;
    deck->wav.scanRefNum = -1;
    deck->volume = 0.8f;
    deck->pan = 0.0f;
    deck->volume_now = 0.8f;
    deck->pan_now = 0.0f;
    deck->pitch_percent = 0.0f;
    deck->ramp_seconds = 0.0f;
    deck->state = G3_TRANSPORT_STOPPED;
}

void g3_player_init(G3Player* player) {
    int i;
    memset(player, 0, sizeof(*player));
    player->master_volume = g3_db_to_linear(0.0f);
    for (i = 0; i < G3_DECK_COUNT; ++i) g3_deck_reset(&player->decks[i], i);
}

void g3_player_shutdown(G3Player* player) {
    int i;
    g3_freeze_drain_pending();
    for (i = 0; i < G3_DECK_COUNT; ++i) {
        g3_freeze_release_deck(i, &player->decks[i].freeze);
        g3_wav_close(&player->decks[i].wav);
    }
}

static int g3_read_u32_le(FILE* f, unsigned long* out_value) {
    unsigned char b[4];
    if (fread(b, 1, 4, f) != 4) return 0;
    *out_value = ((unsigned long)b[0]) |
                 ((unsigned long)b[1] << 8) |
                 ((unsigned long)b[2] << 16) |
                 ((unsigned long)b[3] << 24);
    return 1;
}

static int g3_read_u16_le(FILE* f, unsigned short* out_value) {
    unsigned char b[2];
    if (fread(b, 1, 2, f) != 2) return 0;
    *out_value = (unsigned short)(b[0] | (b[1] << 8));
    return 1;
}

static unsigned long g3_mem_u32_le(const unsigned char* p) {
    return ((unsigned long)p[0]) |
           ((unsigned long)p[1] << 8) |
           ((unsigned long)p[2] << 16) |
           ((unsigned long)p[3] << 24);
}

static unsigned short g3_mem_u16_le(const unsigned char* p) {
    return (unsigned short)(p[0] | (p[1] << 8));
}

/* 16-bit PCM in .wav files is little-endian; big-endian Macs must swap after read. */
#if defined(__POWERPC__) || defined(__ppc__) || defined(powerc) || defined(__CFM68K__) || \
    (defined(__BIG_ENDIAN__) && __BIG_ENDIAN__)
static void g3_pcm16_le_to_host(short* samples, unsigned long pcm_bytes) {
    unsigned char* b = (unsigned char*)samples;
    unsigned long i;
    for (i = 0; i + 1 < pcm_bytes; i += 2) {
        unsigned char t = b[i];
        b[i] = b[i + 1];
        b[i + 1] = t;
    }
}
#else
#define g3_pcm16_le_to_host(s, n) ((void)0)
#endif

static void g3_wav_close(G3WavData* wav) {
    if (wav == NULL) return;
    if (wav->refNum >= 0) {
        FSClose(wav->refNum);
        wav->refNum = -1;
    }
    if (wav->scanRefNum >= 0) {
        FSClose(wav->scanRefNum);
        wav->scanRefNum = -1;
    }
    if (wav->stdio_file != NULL) {
        fclose(wav->stdio_file);
        wav->stdio_file = NULL;
    }
    free(wav->samples);
    free(wav->ring_a);
    free(wav->ring_b);
    memset(wav, 0, sizeof(*wav));
    wav->refNum = -1;
    wav->scanRefNum = -1;
}

static int g3_wav_open_ring(G3WavData* wav) {
    unsigned long cap;
    unsigned long sample_count;
    size_t bytes;
    if (wav == NULL) return 0;
    cap = G3_STREAM_RING_FRAMES;
    sample_count = cap * (unsigned long)wav->channels;
    bytes = (size_t)(sample_count * sizeof(short));
    wav->ring_a = (short*)malloc(bytes);
    wav->ring_b = (short*)malloc(bytes);
    if (wav->ring_a == NULL || wav->ring_b == NULL) {
        free(wav->ring_a);
        free(wav->ring_b);
        wav->ring_a = NULL;
        wav->ring_b = NULL;
        return 0;
    }
    wav->ring_cap = cap;
    wav->ring_play_a = 1;
    wav->ring_fill_pending = 0;
    wav->ring_start = 0;
    wav->ring_count = 0;
    return 1;
}

static short* g3_wav_play_buf(const G3WavData* wav) {
    return wav->ring_play_a ? wav->ring_a : wav->ring_b;
}

static short* g3_wav_fill_buf(const G3WavData* wav) {
    return wav->ring_play_a ? wav->ring_b : wav->ring_a;
}

static int g3_wav_read_pcm_at(G3WavData* wav, unsigned long start_frame, unsigned long num_frames, short* dst) {
    unsigned long byte_count;
    unsigned long byte_off;
    long readCount;
    OSErr err;
    short ioRef;

    if (wav == NULL || dst == NULL || num_frames == 0 || wav->frame_count == 0) return 0;
    if (start_frame >= wav->frame_count) return 0;
    if (start_frame + num_frames > wav->frame_count)
        num_frames = wav->frame_count - start_frame;

    byte_count = num_frames * (unsigned long)wav->channels * sizeof(short);
    if (wav->backend == G3_WAV_MEMORY) {
        if (wav->samples == NULL) return 0;
        memcpy(dst, wav->samples + (start_frame * (unsigned long)wav->channels), (size_t)byte_count);
        return 1;
    }
    if (wav->backend != G3_WAV_FILE) return 0;

    byte_off = wav->data_offset + start_frame * (unsigned long)wav->channels * sizeof(short);
    ioRef = wav->scanRefNum >= 0 ? wav->scanRefNum : wav->refNum;
    if (ioRef >= 0) {
        err = SetFPos(ioRef, fsFromStart, (long)byte_off);
        if (err != noErr) return 0;
        readCount = (long)byte_count;
        err = FSRead(ioRef, &readCount, dst);
        if (err != noErr || (unsigned long)readCount != byte_count) return 0;
    } else if (wav->stdio_file != NULL) {
        if (fseek(wav->stdio_file, (long)byte_off, SEEK_SET) != 0) return 0;
        if (fread(dst, 1, (size_t)byte_count, wav->stdio_file) != (size_t)byte_count) return 0;
    } else {
        return 0;
    }
    g3_pcm16_le_to_host(dst, byte_count);
    return 1;
}

static int g3_wav_read_pcm_ring(G3WavData* wav, unsigned long start_frame, unsigned long num_frames, short* dst) {
    unsigned long byte_count;
    unsigned long byte_off;
    long readCount;
    OSErr err;

    if (wav == NULL || dst == NULL || num_frames == 0 || wav->frame_count == 0) return 0;
    if (start_frame >= wav->frame_count) return 0;
    if (start_frame + num_frames > wav->frame_count)
        num_frames = wav->frame_count - start_frame;

    byte_count = num_frames * (unsigned long)wav->channels * sizeof(short);
    if (wav->backend != G3_WAV_FILE || wav->refNum < 0) return g3_wav_read_pcm_at(wav, start_frame, num_frames, dst);

    byte_off = wav->data_offset + start_frame * (unsigned long)wav->channels * sizeof(short);
    err = SetFPos(wav->refNum, fsFromStart, (long)byte_off);
    if (err != noErr) return 0;
    readCount = (long)byte_count;
    err = FSRead(wav->refNum, &readCount, dst);
    if (err != noErr || (unsigned long)readCount != byte_count) return 0;
    g3_pcm16_le_to_host(dst, byte_count);
    return 1;
}

static int g3_wav_stage_fill(G3WavData* wav, unsigned long start_frame) {
    unsigned long num_frames;
    short* dst;
    if (wav == NULL || wav->backend == G3_WAV_MEMORY) return 1;
    if (wav->ring_a == NULL || wav->ring_b == NULL) return 0;
    if (wav->ring_fill_pending) return 1;
    if (start_frame >= wav->frame_count) return 0;
    num_frames = wav->ring_cap;
    if (start_frame + num_frames > wav->frame_count)
        num_frames = wav->frame_count - start_frame;
    dst = g3_wav_fill_buf(wav);
    if (!g3_wav_read_pcm_ring(wav, start_frame, num_frames, dst)) return 0;
    wav->ring_fill_start = start_frame;
    wav->ring_fill_count = num_frames;
    wav->ring_fill_pending = 1;
    return 1;
}

static void g3_wav_invalidate_ring(G3WavData* wav) {
    if (wav != NULL) {
        wav->ring_count = 0;
        wav->ring_fill_pending = 0;
    }
}

static void g3_wav_ring_do_swap(G3WavData* wav) {
    wav->ring_play_a = wav->ring_play_a ? 0 : 1;
    wav->ring_start = wav->ring_fill_start;
    wav->ring_count = wav->ring_fill_count;
    wav->ring_fill_pending = 0;
}

static void g3_wav_ring_try_swap(G3Deck* deck) {
    G3WavData* wav;
    unsigned long fp;
    if (deck == NULL || !deck->loaded) return;
    wav = &deck->wav;
    if (wav->backend == G3_WAV_MEMORY) return;
    if (!wav->ring_fill_pending) return;
    fp = (unsigned long)deck->frame_pos;
    if (wav->ring_count == 0) {
        g3_wav_ring_do_swap(wav);
        return;
    }
    if (fp >= wav->ring_fill_start && fp < wav->ring_fill_start + wav->ring_fill_count) {
        g3_wav_ring_do_swap(wav);
    }
}

static void g3_wav_get_frame(const G3WavData* wav, unsigned long frame, short* out_l, short* out_r) {
    if (wav->backend == G3_WAV_MEMORY) {
        if (wav->channels == 1) {
            *out_l = wav->samples[frame];
            *out_r = *out_l;
        } else {
            *out_l = wav->samples[(frame * 2) + 0];
            *out_r = wav->samples[(frame * 2) + 1];
        }
    } else {
        const short* rb = g3_wav_play_buf(wav);
        unsigned long idx = frame - wav->ring_start;
        if (idx >= wav->ring_count) {
            *out_l = 0;
            *out_r = 0;
            return;
        }
        if (wav->channels == 1) {
            *out_l = rb[idx];
            *out_r = *out_l;
        } else {
            *out_l = rb[(idx * 2) + 0];
            *out_r = rb[(idx * 2) + 1];
        }
    }
}

static int g3_wav_ring_has_frames(const G3WavData* wav, unsigned long frame_lo, unsigned long frame_hi) {
    (void)frame_hi;
    if (wav == NULL || wav->frame_count == 0) return 0;
    if (wav->backend == G3_WAV_MEMORY) return wav->samples != NULL;
    if (wav->ring_count == 0) return 0;
    if (frame_lo < wav->ring_start) return 0;
    if (frame_lo >= wav->ring_start + wav->ring_count) return 0;
    return 1;
}

static void g3_wav_pump_deck(G3Deck* deck) {
    G3WavData* wav;
    unsigned long cur;
    unsigned long play_end;
    if (deck == NULL || !deck->loaded) return;
    wav = &deck->wav;
    if (wav->backend == G3_WAV_MEMORY) return;
    if (deck->state != G3_TRANSPORT_PLAYING) return;
    cur = (unsigned long)deck->frame_pos;
    if (!g3_wav_ring_has_frames(wav, cur, cur)) {
        unsigned long refill = cur;
        if (wav->ring_cap > 0 && wav->frame_count > wav->ring_cap &&
            refill + wav->ring_cap > wav->frame_count) {
            refill = wav->frame_count - wav->ring_cap;
        }
        if (!wav->ring_fill_pending) g3_wav_stage_fill(wav, refill);
        return;
    }
    if (wav->ring_fill_pending || wav->ring_count == 0) return;
    play_end = wav->ring_start + wav->ring_count;
    if (cur + G3_STREAM_PREFETCH_FRAMES >= play_end) {
        unsigned long next = play_end;
        if (next < wav->frame_count) g3_wav_stage_fill(wav, next);
    }
}

static short g3_fclip_to_i16(double v) {
    long s;
    if (v > 32767.0) v = 32767.0;
    if (v < -32768.0) v = -32768.0;
    s = (long)(v + (v >= 0.0 ? 0.5 : -0.5));
    return (short)s;
}

int g3_player_load_wav_bytes(G3Player* player, int deck_index, const void* bytes, unsigned long size) {
    const unsigned char* p = (const unsigned char*)bytes;
    unsigned long pos = 0;
    int got_fmt = 0;
    int got_data = 0;
    unsigned short fmt_audio = 0;
    unsigned short fmt_channels = 0;
    unsigned short fmt_bits = 0;
    unsigned long fmt_rate = 0;
    short* pcm = NULL;
    unsigned long pcm_bytes = 0;
    G3Deck* deck;

    if (!g3_clamp_deck_index(deck_index) || p == NULL || size < 12) return 0;
    deck = &player->decks[deck_index];

    if (memcmp(p + 0, "RIFF", 4) != 0 || memcmp(p + 8, "WAVE", 4) != 0) return 0;
    pos = 12;

    while (pos + 8 <= size) {
        unsigned long chunk_size;
        const unsigned char* chunk_data;
        if (pos + 8 > size) break;
        chunk_size = g3_mem_u32_le(p + pos + 4);
        chunk_data = p + pos + 8;
        if (pos + 8 + chunk_size > size) break;

        if (memcmp(p + pos, "fmt ", 4) == 0) {
            if (chunk_size < 16) return 0;
            fmt_audio = g3_mem_u16_le(chunk_data + 0);
            fmt_channels = g3_mem_u16_le(chunk_data + 2);
            fmt_rate = g3_mem_u32_le(chunk_data + 4);
            fmt_bits = g3_mem_u16_le(chunk_data + 14);
            got_fmt = 1;
        } else if (memcmp(p + pos, "data", 4) == 0) {
            pcm_bytes = chunk_size;
            pcm = (short*)malloc((size_t)pcm_bytes);
            if (!pcm) return 0;
            memcpy(pcm, chunk_data, (size_t)pcm_bytes);
            got_data = 1;
            break;
        }

        pos += 8 + chunk_size;
        if (chunk_size & 1u) pos += 1;
    }

    if (!got_fmt || !got_data) {
        free(pcm);
        return 0;
    }
    if (fmt_audio != 1 || (fmt_channels != 1 && fmt_channels != 2) || fmt_bits != 16 || fmt_rate != G3_SAMPLE_RATE) {
        free(pcm);
        return 0;
    }

    g3_pcm16_le_to_host(pcm, pcm_bytes);

    g3_player_unload(player, deck_index);
    deck->wav.backend = G3_WAV_MEMORY;
    deck->wav.refNum = -1;
    deck->wav.samples = pcm;
    deck->wav.channels = (int)fmt_channels;
    deck->wav.frame_count = pcm_bytes / (unsigned long)(fmt_channels * 2);
    deck->wav.pcm_bytes = pcm_bytes;
    deck->loaded = 1;
    deck->state = G3_TRANSPORT_STOPPED;
    deck->frame_pos = 0.0;
    deck->speed_current = 0.0;
    deck->speed_target = 0.0;
    deck->peak_linear = 0.0f;
    return 1;
}

int g3_player_load_wav(G3Player* player, int deck_index, const char* path) {
    FILE* f;
    char riff[4];
    unsigned long riff_size;
    char wave[4];
    int got_fmt = 0;
    int got_data = 0;
    unsigned short fmt_audio = 0;
    unsigned short fmt_channels = 0;
    unsigned short fmt_bits = 0;
    unsigned long fmt_rate = 0;
    unsigned long pcm_bytes = 0;
    unsigned long data_offset = 0;
    G3Deck* deck;

    if (!g3_clamp_deck_index(deck_index)) return 0;
    deck = &player->decks[deck_index];

    f = fopen(path, "rb");
    if (!f) return 0;

    if (fread(riff, 1, 4, f) != 4 || memcmp(riff, "RIFF", 4) != 0) { fclose(f); return 0; }
    if (!g3_read_u32_le(f, &riff_size)) { fclose(f); return 0; }
    if (fread(wave, 1, 4, f) != 4 || memcmp(wave, "WAVE", 4) != 0) { fclose(f); return 0; }
    (void)riff_size;

    while (!got_data) {
        char chunk_id[4];
        unsigned long chunk_size;
        if (fread(chunk_id, 1, 4, f) != 4) break;
        if (!g3_read_u32_le(f, &chunk_size)) break;

        if (memcmp(chunk_id, "fmt ", 4) == 0) {
            unsigned short block_align;
            unsigned long byte_rate;
            if (!g3_read_u16_le(f, &fmt_audio) ||
                !g3_read_u16_le(f, &fmt_channels) ||
                !g3_read_u32_le(f, &fmt_rate) ||
                !g3_read_u32_le(f, &byte_rate) ||
                !g3_read_u16_le(f, &block_align) ||
                !g3_read_u16_le(f, &fmt_bits)) {
                fclose(f);
                return 0;
            }
            if (chunk_size > 16) fseek(f, (long)(chunk_size - 16), SEEK_CUR);
            got_fmt = 1;
        } else if (memcmp(chunk_id, "data", 4) == 0) {
            pcm_bytes = chunk_size;
            data_offset = (unsigned long)ftell(f);
            if (fseek(f, (long)chunk_size, SEEK_CUR) != 0) {
                fclose(f);
                return 0;
            }
            got_data = 1;
        } else {
            fseek(f, (long)chunk_size, SEEK_CUR);
        }

        if (chunk_size & 1u) fseek(f, 1L, SEEK_CUR);
    }

    if (!got_fmt || !got_data) {
        fclose(f);
        return 0;
    }

    if (fmt_audio != 1 || (fmt_channels != 1 && fmt_channels != 2) || fmt_bits != 16 || fmt_rate != G3_SAMPLE_RATE) {
        fclose(f);
        return 0;
    }

    g3_player_unload(player, deck_index);
    deck->wav.backend = G3_WAV_FILE;
    deck->wav.refNum = -1;
    deck->wav.stdio_file = f;
    deck->wav.data_offset = data_offset;
    deck->wav.pcm_bytes = pcm_bytes;
    deck->wav.channels = (int)fmt_channels;
    deck->wav.frame_count = pcm_bytes / (unsigned long)(fmt_channels * 2);
    if (!g3_wav_open_ring(&deck->wav)) {
        g3_wav_close(&deck->wav);
        return 0;
    }
    deck->loaded = 1;
    deck->state = G3_TRANSPORT_STOPPED;
    deck->frame_pos = 0.0;
    deck->speed_current = 0.0;
    deck->speed_target = 0.0;
    deck->peak_linear = 0.0f;
    return 1;
}

void g3_player_unload(G3Player* player, int deck_index) {
    G3Deck* deck;
    if (!g3_clamp_deck_index(deck_index)) return;
    deck = &player->decks[deck_index];
    g3_wav_close(&deck->wav);
    g3_deck_reset(deck, deck_index);
}

void g3_player_play(G3Player* player, int deck_index) {
    G3Deck* deck;
    if (!g3_clamp_deck_index(deck_index)) return;
    deck = &player->decks[deck_index];
    if (!deck->loaded) return;
    if (deck->state == G3_TRANSPORT_FROZEN) {
        g3_deck_resume_from_freeze(deck);
        g3_freeze_begin_unfreeze(deck);
        deck->state = G3_TRANSPORT_PLAYING;
        g3_wav_pump_deck(deck);
        return;
    }
    deck->state = G3_TRANSPORT_PLAYING;
    deck->speed_target = 1.0 + ((double)deck->pitch_percent / 100.0);
    if (deck->speed_target < 0.000001) deck->speed_target = 0.000001;
    if (deck->ramp_seconds <= 0.0001f) deck->speed_current = deck->speed_target;
    g3_wav_pump_deck(deck);
}

void g3_player_pause(G3Player* player, int deck_index) {
    G3Deck* deck;
    if (!g3_clamp_deck_index(deck_index)) return;
    deck = &player->decks[deck_index];
    if (!deck->loaded) return;
    if (deck->state == G3_TRANSPORT_FROZEN) {
        if (deck->freeze.buffer != NULL) g3_freeze_release_deck(deck_index, &deck->freeze);
        deck->speed_target = 0.0;
        deck->speed_current = 0.0;
        deck->state = G3_TRANSPORT_PAUSED;
        return;
    }
    if (deck->freeze.buffer != NULL) g3_freeze_release_deck(deck_index, &deck->freeze);
    deck->speed_target = 0.0;
    if (deck->ramp_seconds <= 0.0001f) deck->speed_current = 0.0;
    deck->state = G3_TRANSPORT_PAUSED;
}

void g3_player_stop(G3Player* player, int deck_index) {
    G3Deck* deck;
    if (!g3_clamp_deck_index(deck_index)) return;
    deck = &player->decks[deck_index];
    if (!deck->loaded) return;
    if (deck->freeze.buffer != NULL) g3_freeze_release_deck(deck_index, &deck->freeze);
    deck->speed_target = 0.0;
    deck->speed_current = 0.0;
    deck->state = G3_TRANSPORT_STOPPED;
    deck->frame_pos = 0.0;
}

static int g3_freeze_begin(G3Deck* deck, int deck_index) {
    unsigned long window_frames;
    unsigned long half_frames;
    unsigned long start_frame;
    unsigned long copy_samples;
    unsigned long bytes;
    short* dst;
    G3FreezeCapture* fz;

    if (deck == NULL || !deck->loaded || deck->wav.frame_count == 0) return 0;

    g3_freeze_drain_pending();
    g3_freeze_release_deck(deck_index, &deck->freeze);
    g3_freeze_drain_pending();
    fz = &deck->freeze;

    window_frames = g3_ms_to_frames((unsigned long)G3_FREEZE_WINDOW_MS);
    if (window_frames < 2) window_frames = 2;
    if (window_frames > deck->wav.frame_count) window_frames = deck->wav.frame_count;

    half_frames = window_frames / 2;
    start_frame = (unsigned long)deck->frame_pos;
    if (start_frame > half_frames) start_frame -= half_frames;
    else start_frame = 0;
    if (start_frame + window_frames > deck->wav.frame_count)
        start_frame = deck->wav.frame_count - window_frames;

    copy_samples = window_frames * (unsigned long)deck->wav.channels;
    bytes = copy_samples * sizeof(short);
    dst = (short*)malloc((size_t)bytes);
    if (dst == NULL) return 0;

    if (!g3_wav_read_pcm_at(&deck->wav, start_frame, window_frames, dst)) {
        free(dst);
        return 0;
    }

    fz->buffer = dst;
    fz->frame_count = window_frames;
    fz->channels = deck->wav.channels;
    fz->spawn_interval_samples = g3_ms_to_frames((unsigned long)G3_GRAIN_SPAWN_INTERVAL_MS);
    if (fz->spawn_interval_samples < 1) fz->spawn_interval_samples = 1;
    fz->grain_length_samples = g3_ms_to_frames((unsigned long)G3_GRAIN_DURATION_MS);
    if (fz->grain_length_samples < 2) fz->grain_length_samples = 2;
    if (fz->grain_length_samples > fz->frame_count) fz->grain_length_samples = fz->frame_count;
    fz->fade_length_samples = g3_ms_to_frames((unsigned long)G3_GRAIN_FADE_MS);
    if (fz->fade_length_samples < 1) fz->fade_length_samples = 1;
    if (fz->fade_length_samples > fz->grain_length_samples / 2)
        fz->fade_length_samples = fz->grain_length_samples / 2;
    gFreezeRng = (unsigned long)TickCount() | 1UL;
    g3_freeze_seed_grains(fz);
    g3_freeze_start_xfade(fz, G3_FREEZE_XFADE_IN);

    fz->playback_speed = g3_freeze_pitch_speed(deck);
    deck->speed_target = 0.0;
    deck->speed_current = 0.0;
    deck->state = G3_TRANSPORT_FROZEN;
    return 1;
}

int g3_player_toggle_freeze(G3Player* player, int deck_index) {
    G3Deck* deck;
    if (!g3_clamp_deck_index(deck_index)) return 0;
    deck = &player->decks[deck_index];
    if (!deck->loaded) return 0;
    if (deck->state == G3_TRANSPORT_FROZEN) {
        g3_deck_resume_from_freeze(deck);
        g3_freeze_begin_unfreeze(deck);
        deck->state = G3_TRANSPORT_PLAYING;
        g3_wav_pump_deck(deck);
        return 0;
    }
    /* Ignore re-freeze while still crossfading out of a previous unfreeze. */
    if (deck->state == G3_TRANSPORT_PLAYING && deck->freeze.buffer != NULL &&
        deck->freeze.xfade_dir == G3_FREEZE_XFADE_OUT && deck->freeze.xfade_remain > 0)
        return 2;
    if (!g3_freeze_begin(deck, deck_index)) return -1;
    return 1;
}

void g3_player_seek_ms(G3Player* player, int deck_index, unsigned long position_ms) {
    G3Deck* deck;
    double frame_pos;
    if (!g3_clamp_deck_index(deck_index)) return;
    deck = &player->decks[deck_index];
    if (!deck->loaded) return;
    if (deck->state == G3_TRANSPORT_FROZEN) g3_freeze_end_capture(deck, deck_index);
    frame_pos = ((double)position_ms / 1000.0) * (double)G3_SAMPLE_RATE;
    if (frame_pos < 0.0) frame_pos = 0.0;
    if (frame_pos >= (double)deck->wav.frame_count) frame_pos = (double)(deck->wav.frame_count ? deck->wav.frame_count - 1 : 0);
    deck->frame_pos = frame_pos;
    g3_wav_invalidate_ring(&deck->wav);
    if (deck->wav.backend == G3_WAV_FILE) {
        unsigned long refill = (unsigned long)frame_pos;
        if (deck->wav.ring_cap > 0 && deck->wav.frame_count > deck->wav.ring_cap &&
            refill + deck->wav.ring_cap > deck->wav.frame_count) {
            refill = deck->wav.frame_count - deck->wav.ring_cap;
        }
        g3_wav_stage_fill(&deck->wav, refill);
    }
}

void g3_player_set_volume(G3Player* player, int deck_index, float value) {
    G3Deck* deck;
    if (!g3_clamp_deck_index(deck_index)) return;
    deck = &player->decks[deck_index];
    deck->volume = g3_clampf(value, 0.0f, 3.0f);
    if (deck->ramp_seconds <= 0.0001f) deck->volume_now = deck->volume;
}

void g3_player_set_pan(G3Player* player, int deck_index, float value) {
    G3Deck* deck;
    if (!g3_clamp_deck_index(deck_index)) return;
    deck = &player->decks[deck_index];
    deck->pan = g3_clampf(value, -1.0f, 1.0f);
    if (deck->ramp_seconds <= 0.0001f) deck->pan_now = deck->pan;
}

void g3_player_set_pitch_percent(G3Player* player, int deck_index, float value) {
    G3Deck* deck;
    if (!g3_clamp_deck_index(deck_index)) return;
    deck = &player->decks[deck_index];
    deck->pitch_percent = g3_clampf(value, -100.0f, 100.0f);
    if (deck->state == G3_TRANSPORT_FROZEN) {
        deck->freeze.playback_speed = g3_freeze_pitch_speed(deck);
        g3_freeze_seed_grains(&deck->freeze);
    } else if (deck->state == G3_TRANSPORT_PLAYING) {
        deck->speed_target = g3_deck_pitch_speed(deck);
    }
}

void g3_player_set_ramp_seconds(G3Player* player, int deck_index, float value) {
    G3Deck* deck;
    if (!g3_clamp_deck_index(deck_index)) return;
    deck = &player->decks[deck_index];
    deck->ramp_seconds = g3_clampf(value, 0.0f, 8.0f);
    if (deck->ramp_seconds <= 0.0001f) {
        deck->volume_now = deck->volume;
        deck->pan_now = deck->pan;
        deck->speed_current = deck->speed_target;
    }
}

void g3_player_set_master_volume(G3Player* player, float value) {
    /* Linear gain; +24 dB ~= 15.85, leave headroom to +24 dB */
    player->master_volume = g3_clampf(value, 0.0f, g3_db_to_linear(24.0f) * 1.05f);
}

void g3_player_get_deck_status(const G3Player* player, int deck_index, G3DeckStatus* out_status) {
    const G3Deck* deck;
    if (!out_status) return;
    memset(out_status, 0, sizeof(*out_status));
    if (!g3_clamp_deck_index(deck_index)) return;
    deck = &player->decks[deck_index];
    out_status->loaded = deck->loaded;
    out_status->state = deck->state;
    out_status->volume = deck->volume;
    out_status->pan = deck->pan;
    out_status->pitch_percent = deck->pitch_percent;
    out_status->ramp_seconds = deck->ramp_seconds;
    out_status->peak_dbfs = g3_linear_to_dbfs(deck->peak_linear);
    if (deck->loaded) {
        out_status->duration_ms = (unsigned long)((1000.0 * (double)deck->wav.frame_count) / (double)G3_SAMPLE_RATE);
        out_status->position_ms = (unsigned long)((1000.0 * deck->frame_pos) / (double)G3_SAMPLE_RATE);
    }
}

void g3_player_get_master_status(const G3Player* player, G3MasterStatus* out_status) {
    if (!out_status) return;
    out_status->master_volume = player->master_volume;
    out_status->peak_dbfs = g3_linear_to_dbfs(player->master_peak_linear);
}

static void g3_update_speed(G3Deck* deck, int frame_count) {
    double ramp = (double)deck->ramp_seconds;
    double step;
    if (ramp <= 0.0001) {
        deck->speed_current = deck->speed_target;
        return;
    }
    step = ((double)frame_count / (double)G3_SAMPLE_RATE) / ramp;
    if (deck->speed_current < deck->speed_target) {
        deck->speed_current += step;
        if (deck->speed_current > deck->speed_target) deck->speed_current = deck->speed_target;
    } else if (deck->speed_current > deck->speed_target) {
        deck->speed_current -= step;
        if (deck->speed_current < deck->speed_target) deck->speed_current = deck->speed_target;
    }
}

/* Same ramp law as speed: ~1 unit per ramp_seconds toward target (matches pitch motor). */
static void g3_ramp_float_toward(float* cur, float target, double ramp_sec, int frames);

static float g3_grain_envelope(double age, unsigned long length, unsigned long fade_len) {
    double t;
    if (length == 0) return 0.0f;
    if (fade_len == 0) return 1.0f;
    if (fade_len > length / 2) fade_len = length / 2;
    if (age < (double)fade_len) {
        t = age / (double)fade_len;
        return (float)(0.5 * (1.0 - cos(M_PI * t)));
    }
    if (age >= (double)length - (double)fade_len) {
        t = ((double)length - age) / (double)fade_len;
        if (t < 0.0) t = 0.0;
        return (float)(0.5 * (1.0 - cos(M_PI * t)));
    }
    return 1.0f;
}

static double g3_soft_saturate(double x) {
    /* Gentle knee above unity — hard clip on summed grains causes audible clicks. */
    if (x > 1.0) {
        double over = x - 1.0;
        return 1.0 + over / (1.0 + over * 4.0);
    }
    if (x < -1.0) {
        double over = -x - 1.0;
        return -1.0 - over / (1.0 + over * 4.0);
    }
    return x;
}

static float g3_freeze_xfade_ease(float t) {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    return (float)(0.5 * (1.0 - cos(M_PI * (double)t)));
}

static float g3_freeze_xfade_gain_at(const G3FreezeCapture* fz, unsigned long offset) {
    unsigned long remain;
    unsigned long total;
    float t;
    if (fz == NULL || fz->xfade_dir == G3_FREEZE_XFADE_NONE || fz->xfade_total == 0) return 1.0f;
    remain = fz->xfade_remain;
    if (offset >= remain) {
        if (fz->xfade_dir == G3_FREEZE_XFADE_OUT) return 0.0f;
        return 1.0f;
    }
    remain -= offset;
    total = fz->xfade_total;
    if (fz->xfade_dir == G3_FREEZE_XFADE_IN) {
        t = 1.0f - (float)remain / (float)total;
        return g3_freeze_xfade_ease(t);
    }
    t = (float)remain / (float)total;
    return g3_freeze_xfade_ease(t);
}

static unsigned long g3_freeze_grain_length_samples(const G3FreezeCapture* fz) {
    unsigned long len;
    if (fz == NULL) return g3_ms_to_frames((unsigned long)G3_GRAIN_DURATION_MS);
    len = fz->grain_length_samples;
    if (len < 2) len = 2;
    if (fz->frame_count > 0 && len > fz->frame_count) len = fz->frame_count;
    return len;
}

static int g3_spawn_grain(G3FreezeCapture* fz) {
    int g;
    unsigned long pick;
    unsigned long max_start;
    unsigned long grain_len;
    if (fz == NULL || fz->buffer == NULL || fz->frame_count < 2) return 0;
    grain_len = g3_freeze_grain_length_samples(fz);
    if (grain_len < 2) return 0;
    for (g = 0; g < G3_FREEZE_MAX_GRAINS; ++g) {
        if (!fz->grains[g].active) break;
    }
    if (g >= G3_FREEZE_MAX_GRAINS) return 0;
    if (fz->frame_count > grain_len)
        max_start = fz->frame_count - grain_len;
    else
        max_start = 0;
    pick = g3_freeze_rand();
    if (max_start > 0)
        pick %= (max_start + 1);
    else
        pick = 0;
    fz->grains[g].active = 1;
    fz->grains[g].start_frame = pick;
    fz->grains[g].age = 0;
    fz->grains[g].length = grain_len;
    fz->grains[g].wall_samples = 0;
    return 1;
}

static void g3_grain_mix_sample(G3FreezeCapture* fz, G3Grain* gr, double* out_l, double* out_r) {
    unsigned long frame_i;
    unsigned long frame_n;
    unsigned long pos_i;
    unsigned long pos_n;
    double frac;
    float env;
    float wall_env;
    double sample_l;
    double sample_r;
    double speed;
    unsigned long wall_remain;

    if (fz == NULL || gr == NULL || !gr->active || fz->buffer == NULL) return;
    if (gr->age >= (double)gr->length) {
        gr->active = 0;
        return;
    }
    speed = fz->playback_speed;
    if (speed < G3_FREEZE_MIN_PLAYBACK_SPEED) speed = G3_FREEZE_MIN_PLAYBACK_SPEED;

    frame_i = (unsigned long)gr->age;
    frame_n = frame_i + 1;
    if (frame_n >= gr->length) frame_n = frame_i;
    frac = gr->age - (double)frame_i;

    pos_i = gr->start_frame + frame_i;
    pos_n = gr->start_frame + frame_n;
    if (pos_i >= fz->frame_count) pos_i = fz->frame_count - 1;
    if (pos_n >= fz->frame_count) pos_n = fz->frame_count - 1;

    env = g3_grain_envelope(gr->age, gr->length, fz->fade_length_samples);
    wall_remain = G3_GRAIN_MAX_WALL_SAMPLES - gr->wall_samples;
    if (wall_remain < fz->fade_length_samples) {
        wall_env = (float)wall_remain / (float)fz->fade_length_samples;
        if (wall_env < 0.0f) wall_env = 0.0f;
        if (wall_env < env) env = wall_env;
    }
    if (env <= 0.0f) {
        gr->active = 0;
        return;
    }

    if (fz->channels == 1) {
        short s0 = fz->buffer[pos_i];
        short s1 = fz->buffer[pos_n];
        sample_l = ((double)s0 + ((double)(s1 - s0) * frac)) / 32768.0;
        sample_r = sample_l;
    } else {
        short l0 = fz->buffer[(pos_i * 2) + 0];
        short l1 = fz->buffer[(pos_n * 2) + 0];
        short r0 = fz->buffer[(pos_i * 2) + 1];
        short r1 = fz->buffer[(pos_n * 2) + 1];
        sample_l = ((double)l0 + ((double)(l1 - l0) * frac)) / 32768.0;
        sample_r = ((double)r0 + ((double)(r1 - r0) * frac)) / 32768.0;
    }
    *out_l += sample_l * (double)env;
    *out_r += sample_r * (double)env;
    gr->age += speed;
    gr->wall_samples++;
    if (gr->age >= (double)gr->length || gr->wall_samples >= G3_GRAIN_MAX_WALL_SAMPLES) gr->active = 0;
}

static double sFreezeMixL[G3_FRAMES_PER_BUFFER];
static double sFreezeMixR[G3_FRAMES_PER_BUFFER];

static void g3_render_frozen_buffer(int deck_index, G3Deck* deck, double* out_l, double* out_r, int frame_count,
                                  float* peak_out) {
    G3FreezeCapture* fz;
    int si;
    int g;
    double interval;
    double vol_scale;
    float peak;
    int render_frozen;

    if (deck == NULL || out_l == NULL || out_r == NULL || frame_count <= 0) return;
    fz = &deck->freeze;
    if (fz->buffer == NULL) return;
    render_frozen = (deck->state == G3_TRANSPORT_FROZEN);
    if (!render_frozen && !(deck->state == G3_TRANSPORT_PLAYING && fz->xfade_dir == G3_FREEZE_XFADE_OUT &&
                            fz->xfade_remain > 0))
        return;
    if (frame_count > G3_FRAMES_PER_BUFFER) frame_count = G3_FRAMES_PER_BUFFER;

    g3_ramp_float_toward(&deck->volume_now, deck->volume, (double)deck->ramp_seconds, frame_count);
    vol_scale = (double)deck->volume_now * (double)G3_FREEZE_OUTPUT_GAIN * (double)G3_FREEZE_MIX_HEADROOM;
    interval = (double)fz->spawn_interval_samples;
    if (fz->playback_speed > 0.0001) interval /= fz->playback_speed;
    if (interval > (double)G3_SAMPLE_RATE) interval = (double)G3_SAMPLE_RATE;
    if (interval < 1.0) interval = 1.0;
    peak = 0.0f;

    for (si = 0; si < frame_count; ++si) {
        double grain_l;
        double grain_r;
        double sample_l;
        double sample_r;
        float abs_local;

        if (fz->xfade_dir != G3_FREEZE_XFADE_OUT) {
            fz->spawn_accum += 1.0;
            if (fz->spawn_accum >= interval) {
                fz->spawn_accum -= interval;
                g3_spawn_grain(fz);
            }
        }

        grain_l = 0.0;
        grain_r = 0.0;
        for (g = 0; g < G3_FREEZE_MAX_GRAINS; ++g) {
            if (fz->grains[g].active) g3_grain_mix_sample(fz, &fz->grains[g], &grain_l, &grain_r);
        }

        {
            float xfade = g3_freeze_xfade_gain_at(fz, (unsigned long)si);
            vol_scale = (double)deck->volume_now * (double)G3_FREEZE_OUTPUT_GAIN * (double)G3_FREEZE_MIX_HEADROOM *
                        (double)xfade;
            sample_l = g3_soft_saturate(grain_l * vol_scale);
            sample_r = g3_soft_saturate(grain_r * vol_scale);
        }
        out_l[si] += sample_l;
        out_r[si] += sample_r;

        abs_local = (float)(fabs(sample_l) > fabs(sample_r) ? fabs(sample_l) : fabs(sample_r));
        if (abs_local > peak) peak = abs_local;
    }

    if (fz->xfade_dir != G3_FREEZE_XFADE_NONE && fz->xfade_remain > 0) {
        if ((unsigned long)frame_count >= fz->xfade_remain)
            fz->xfade_remain = 0;
        else
            fz->xfade_remain -= (unsigned long)frame_count;
    }
    if (fz->xfade_dir == G3_FREEZE_XFADE_IN && fz->xfade_remain == 0)
        fz->xfade_dir = G3_FREEZE_XFADE_NONE;
    if (fz->xfade_dir == G3_FREEZE_XFADE_OUT && fz->xfade_remain == 0)
        g3_freeze_release_deck(deck_index, fz);

    if (peak_out != NULL && peak > *peak_out) *peak_out = peak;
}

static void g3_ramp_float_toward(float* cur, float target, double ramp_sec, int frames) {
    double step;
    double c;
    double t;
    if (cur == NULL) return;
    c = (double)*cur;
    t = (double)target;
    if (ramp_sec <= 0.0001) {
        *cur = target;
        return;
    }
    step = ((double)frames / (double)G3_SAMPLE_RATE) / ramp_sec;
    if (c < t) {
        c += step;
        if (c > t) c = t;
    } else if (c > t) {
        c -= step;
        if (c < t) c = t;
    }
    *cur = (float)c;
}

static int g3_deck_interp_sample(G3Deck* deck, long frame_i, long frame_n, double frac, double* out_l, double* out_r) {
    short s0l, s0r, s1l, s1r;
    G3WavData* wav;
    unsigned long play_end;
    if (deck == NULL || out_l == NULL || out_r == NULL) return 0;
    wav = &deck->wav;
    if (!g3_wav_ring_has_frames(wav, (unsigned long)frame_i, (unsigned long)frame_n)) return 0;
    if (wav->backend != G3_WAV_MEMORY && wav->ring_count > 0) {
        play_end = wav->ring_start + wav->ring_count;
        if ((unsigned long)frame_n >= play_end) frame_n = (long)(play_end - 1UL);
    }
    g3_wav_get_frame(wav, (unsigned long)frame_i, &s0l, &s0r);
    g3_wav_get_frame(wav, (unsigned long)frame_n, &s1l, &s1r);
    *out_l = ((double)s0l + ((double)(s1l - s0l) * frac)) / 32768.0;
    *out_r = ((double)s0r + ((double)(s1r - s0r) * frac)) / 32768.0;
    return 1;
}

int g3_player_render(G3Player* player, short* out_stereo, int frame_count) {
    int i;
    int d;
    float deck_peak[G3_DECK_COUNT];

    if (!out_stereo || frame_count <= 0) return 0;
    if (frame_count > G3_FRAMES_PER_BUFFER) frame_count = G3_FRAMES_PER_BUFFER;
    g3_freeze_drain_pending();
    for (d = 0; d < G3_DECK_COUNT; ++d) deck_peak[d] = 0.0f;
    player->master_peak_linear = 0.0f;

    memset(sFreezeMixL, 0, (size_t)frame_count * sizeof(double));
    memset(sFreezeMixR, 0, (size_t)frame_count * sizeof(double));
    for (d = 0; d < G3_DECK_COUNT; ++d) {
        G3Deck* deck = &player->decks[d];
        if (!deck->loaded) continue;
        if (deck->freeze.buffer != NULL) {
            if (deck->state == G3_TRANSPORT_FROZEN ||
                (deck->state == G3_TRANSPORT_PLAYING && deck->freeze.xfade_dir == G3_FREEZE_XFADE_OUT &&
                 deck->freeze.xfade_remain > 0))
                g3_render_frozen_buffer(d, deck, sFreezeMixL, sFreezeMixR, frame_count, &deck_peak[d]);
        }
    }

    for (i = 0; i < frame_count; ++i) {
        double mix_l = sFreezeMixL[i];
        double mix_r = sFreezeMixR[i];

        for (d = 0; d < G3_DECK_COUNT; ++d) {
            G3Deck* deck = &player->decks[d];
            long frame_i;
            long frame_n;
            double frac;
            double mono_or_l, right;
            double left_gain, right_gain;
            double sample_l, sample_r;
            float abs_local;

            if (!deck->loaded) continue;
            g3_wav_ring_try_swap(deck);
            if (deck->state == G3_TRANSPORT_FROZEN) {
                G3FreezeCapture* fz_in = &deck->freeze;
                if (fz_in->buffer != NULL && fz_in->xfade_dir == G3_FREEZE_XFADE_IN && fz_in->xfade_remain > 0 &&
                    deck->wav.frame_count > 1) {
                    float xfade = g3_freeze_xfade_gain_at(fz_in, (unsigned long)i);
                    double play_l;
                    double play_r;

                    frame_i = (long)deck->frame_pos;
                    frame_n = frame_i + 1;
                    if (frame_n >= (long)deck->wav.frame_count) frame_n = frame_i;
                    frac = deck->frame_pos - (double)frame_i;
                    if (!g3_deck_interp_sample(deck, frame_i, frame_n, frac, &play_l, &play_r)) continue;
                    {
                        double p = (double)((deck->pan_now + 1.0f) * 0.5f);
                        left_gain = cos((M_PI * 0.5) * p);
                        right_gain = sin((M_PI * 0.5) * p);
                    }
                    g3_ramp_float_toward(&deck->volume_now, deck->volume, (double)deck->ramp_seconds, 1);
                    play_l *= left_gain * (double)deck->volume_now * (1.0 - (double)xfade);
                    play_r *= right_gain * (double)deck->volume_now * (1.0 - (double)xfade);
                    mix_l += play_l;
                    mix_r += play_r;
                    abs_local = (float)(fabs(play_l) > fabs(play_r) ? fabs(play_l) : fabs(play_r));
                    if (abs_local > deck_peak[d]) deck_peak[d] = abs_local;
                }
                continue;
            }
            g3_update_speed(deck, 1);
            g3_ramp_float_toward(&deck->volume_now, deck->volume, (double)deck->ramp_seconds, 1);
            g3_ramp_float_toward(&deck->pan_now, deck->pan, (double)deck->ramp_seconds, 1);
            if (deck->speed_current <= 0.000001) {
                if (deck->state == G3_TRANSPORT_STOPPED) deck->frame_pos = 0.0;
                continue;
            }

            if (deck->frame_pos >= (double)(deck->wav.frame_count - 1)) {
                deck->speed_current = 0.0;
                deck->speed_target = 0.0;
                deck->state = G3_TRANSPORT_STOPPED;
                deck->frame_pos = 0.0;
                continue;
            }

            frame_i = (long)deck->frame_pos;
            frame_n = frame_i + 1;
            if (frame_n >= (long)deck->wav.frame_count) frame_n = frame_i;
            frac = deck->frame_pos - (double)frame_i;

            if (!g3_deck_interp_sample(deck, frame_i, frame_n, frac, &mono_or_l, &right)) continue;

            {
                double p = (double)((deck->pan_now + 1.0f) * 0.5f);
                left_gain = cos((M_PI * 0.5) * p);
                right_gain = sin((M_PI * 0.5) * p);
            }

            sample_l = mono_or_l * left_gain * (double)deck->volume_now;
            sample_r = right * right_gain * (double)deck->volume_now;
            if (deck->freeze.buffer != NULL && deck->freeze.xfade_dir == G3_FREEZE_XFADE_OUT &&
                deck->freeze.xfade_remain > 0) {
                float xfade = g3_freeze_xfade_gain_at(&deck->freeze, (unsigned long)i);
                sample_l *= (1.0 - (double)xfade);
                sample_r *= (1.0 - (double)xfade);
            }
            mix_l += sample_l;
            mix_r += sample_r;

            abs_local = (float)(fabs(sample_l) > fabs(sample_r) ? fabs(sample_l) : fabs(sample_r));
            if (abs_local > deck_peak[d]) deck_peak[d] = abs_local;

            deck->frame_pos += deck->speed_current;
        }

        mix_l *= (double)player->master_volume;
        mix_r *= (double)player->master_volume;

        out_stereo[(i * 2) + 0] = g3_fclip_to_i16(mix_l * 32767.0);
        out_stereo[(i * 2) + 1] = g3_fclip_to_i16(mix_r * 32767.0);

        {
            float abs_master = (float)(fabs(mix_l) > fabs(mix_r) ? fabs(mix_l) : fabs(mix_r));
            if (abs_master > player->master_peak_linear) player->master_peak_linear = abs_master;
        }
    }

    for (d = 0; d < G3_DECK_COUNT; ++d) player->decks[d].peak_linear = deck_peak[d];
    return frame_count;
}

/* ---------- Mac UI / audio I/O ---------- */

#define FRAMES_PER_BUFFER G3_FRAMES_PER_BUFFER
#define WINDOW_W 800
#define WAVE_RIGHT (WINDOW_W - 20)
#define WAVE_POINTS 320
#define G3_WAVE_CACHE_MAGIC 0x47335756UL /* "G3WV" */
#define G3_WAVE_CACHE_VERSION 1
#define G3_WAVE_REBUILD_CHUNK 32
#define PEAK_DISPLAY_DECAY_DB 2.0f
#define DEFAULT_DECK_VOL 0.8f
#define MASTER_DB_MIN (-60.0f)
#define MASTER_DB_MAX (24.0f)
#define MASTER_DB_DEFAULT (0.0f)
#define MASTER_SLIDER_STEPS 840 /* 0.1 dB steps: -60.0 .. +24.0 */
/* Top of content = D1-D3 only (no banner text). */
#define DECK_TAB_TOP 18
#define DECK_TAB_H 22
#define GAP_TAB_TO_PANEL 14
#define DECK_PANEL_TOP (DECK_TAB_TOP + DECK_TAB_H + GAP_TAB_TO_PANEL)
#define TR_H 20
#define GAP_FIRST_SL 8
#define ROW_STEP 30
#define SL_H 16
#define SL_TRACK_TEXT_BASE 11 /* baseline Y = slider top + this (aligns w/ 16px scroll bar) */
#define SL0_TOP (DECK_PANEL_TOP + GAP_FIRST_SL)
#define LAST_SL_BOT ((SL0_TOP) + 3 * (ROW_STEP) + (SL_H))
#define GAP_SL_TO_TRANSPORT 12
#define TRANSPORT_TOP ((LAST_SL_BOT) + (GAP_SL_TO_TRANSPORT))
#define TRANSPORT_BOT ((TRANSPORT_TOP) + (TR_H))
#define GAP_TRANSPORT_TO_WAVE 10
#define WAVE_TOP ((TRANSPORT_BOT) + (GAP_TRANSPORT_TO_WAVE))
#define WAVE_H 54
#define WAVE_BOTTOM (WAVE_TOP + WAVE_H)
#define GAP_BEFORE_STAT 12
#define STAT_H 30
#define STAT_TOP (WAVE_BOTTOM + GAP_BEFORE_STAT)
#define STAT_BOTTOM (STAT_TOP + STAT_H)
#define SINGLE_DECK_CONTENT_H ((STAT_BOTTOM) - (DECK_PANEL_TOP))
#define MASTER_BAND_GAP 20
#define MASTER_BAND_TOP (short)((DECK_PANEL_TOP) + (SINGLE_DECK_CONTENT_H) + (MASTER_BAND_GAP))
/* Master row: label vertically centered with slider track */
#define MASTER_SL_T (short)(MASTER_BAND_TOP + 8)
#define MASTER_SL_B (short)(MASTER_BAND_TOP + 24)
#define MASTER_LBL_BASELINE (short)(MASTER_SL_T + SL_TRACK_TEXT_BASE)
#define MASTER_STAT_H 22
#define MASTER_STAT_T (short)(MASTER_BAND_TOP + 28)
#define MASTER_STAT_B (short)(MASTER_STAT_T + MASTER_STAT_H)
#define MSG_ROW_T (short)(MASTER_STAT_B + 8)
#define MSG_ROW_H 22
#define MSG_ROW_B (short)(MSG_ROW_T + MSG_ROW_H)
#define MSG_BASELINE (short)(MSG_ROW_T + SL_TRACK_TEXT_BASE)
#define MSG_RECT_T MSG_ROW_T
#define MSG_RECT_B MSG_ROW_B
#define MSG_TEXT_LEFT 86
#define BTN_CORNER_TOP (short)(MSG_ROW_T + 2)
#define BTN_CORNER_BOT (short)(MSG_ROW_B - 2)
#define STAT_TEXT_BASELINE_OFF 20
#define BTN_CORNER_QUIT_R (short)(WAVE_RIGHT - 8)
#define BTN_CORNER_QUIT_L (short)(WAVE_RIGHT - 78)
#define BTN_CORNER_DEF_R (short)(BTN_CORNER_QUIT_L - 10)
#define BTN_CORNER_DEF_L (short)(BTN_CORNER_DEF_R - 92)
#define MSG_TEXT_RIGHT (short)(BTN_CORNER_DEF_L - 12)
#define LBL_X 14
#define SL_LEFT 132
#define SL_RIGHT 512
#define VAL_LEFT 520
#define VAL_RIGHT 668
#define BTN_LEFT 676
#define BTN_RIGHT 734
#define SLIDER_VAL_COUNT 5
#define WINDOW_CONTENT_BOTTOM (MSG_ROW_B + 12)
#define WINDOW_CHROME_EXTRA 38
#define WINDOW_H (WINDOW_CONTENT_BOTTOM + WINDOW_CHROME_EXTRA)
#define TARGET_SCREEN_W 1024
#define TARGET_SCREEN_H 768

static G3Player gPlayer;
static int gRunning = 1;

static SndChannelPtr gChan = NULL;
static SndDoubleBufferHeader gHeader;
static SndDoubleBufferPtr gBufA = NULL;
static SndDoubleBufferPtr gBufB = NULL;
static SndDoubleBackUPP gDoubleBackUPP = NULL;

static WindowPtr gWindow = NULL;
static int gUiDeck = 0;
static ControlHandle gTabBtn[G3_DECK_COUNT];
static ControlHandle gLoadBtn = NULL;
static ControlHandle gPlayBtn = NULL;
static ControlHandle gFreezeBtn = NULL;
static ControlHandle gStopBtn = NULL;
static ControlHandle gVolSlider = NULL;
static ControlHandle gPanSlider = NULL;
static ControlHandle gPitchSlider = NULL;
static ControlHandle gRampSlider = NULL;
static ControlHandle gVolDefBtn = NULL;
static ControlHandle gPanDefBtn = NULL;
static ControlHandle gPitchDefBtn = NULL;
static ControlHandle gRampDefBtn = NULL;
static ControlHandle gMasterSlider = NULL;
static ControlHandle gMasterDefBtn = NULL;
static ControlHandle gResetBtn = NULL;
static ControlHandle gQuitBtn = NULL;
static ControlActionUPP gSliderActionUPP = NULL;

static Rect gDeckStatusRect;
static Rect gWaveRect;
static Rect gMasterStatusRect;
static Rect gSliderValueRect[SLIDER_VAL_COUNT];
static Rect gMessageRect;

static short gWaveMin[G3_DECK_COUNT][WAVE_POINTS];
static short gWaveMax[G3_DECK_COUNT][WAVE_POINTS];
static char gDeckFileName[G3_DECK_COUNT][64];
static FSSpec gDeckFileSpec[G3_DECK_COUNT];
static int gDeckHasFileSpec[G3_DECK_COUNT];
static unsigned long gDeckSrcModTime[G3_DECK_COUNT];
static char gLastMessage[192] = "Ready.";
static long gUiTickCounter = 0;
static int gWaveRebuildDeck = -1;
static int gWaveRebuildNext = 0;
static char gPrevDeckStat[384];
static char gPrevMasterStat[384];
static char gPrevMsgDrawn[192];
static int gPrevMsgInit = 0;
static char gPrevSliderVal[SLIDER_VAL_COUNT][64];
static float gShownDeckPeakDb = -120.0f;
static float gShownMasterPeakDb = -120.0f;
static short gWavePlayheadX = -1;
static int gWaveformBodyDirty = 1;
static int IsSliderControl(ControlHandle ctrl);
static int AnyDeckPlaying(void);
static int WaveHit(Point localPt);
static void DrawSliderCaptions(void);
static void InvalidateSliderValueCache(void);
static void DrawSliderValues(void);
static void DrawWaveformBody(void);
static void UpdateWaveformPlayhead(void);
static void MarkWaveformDirty(void);
static void RedrawVisibleWaveform(void);
static void UpdateStatusAreas(void);
static void DoUpdate(EventRecord* event);
static void UiPumpEvents(void);
static void ResetAllSlidersToDefaults(void);
static void SyncVisibleDeckAndMaster(void);
static void ApplyVisibleDeckSlidersToEngine(void);
static void LoadDeckSlidersFromEngine(int deckIndex);
static void SwitchToDeck(int deckIndex);
static short SliderMaxForControl(ControlHandle c);
static void MaybeNudgeScrollbar(ControlHandle c, ControlPartCode initialPart);
static void RefreshPlayPauseButton(void);
static void RefreshFreezeButton(void);
static short MasterDbToSlider(float db);
static float SliderToMasterDb(short s);
static float DeckVolLinearToDb(float linear);

static void MakePString(const char* src, Str255 dst) {
    short len = 0;
    while (src[len] != '\0' && len < 255) len++;
    dst[0] = (unsigned char)len;
    while (len > 0) {
        dst[len] = (unsigned char)src[len - 1];
        len--;
    }
}

static void DrawCString(const char* src) {
    Str255 p;
    MakePString(src, p);
    DrawString(p);
}

/* QuickDraw MoveTo Y is the text baseline; center in short status bands. */
static short TextBaselineInRect(const Rect* r) {
    short h;
    if (r == NULL) return 0;
    h = (short)(r->bottom - r->top);
    if (h <= 18) return (short)(r->top + SL_TRACK_TEXT_BASE);
    return (short)(r->top + STAT_TEXT_BASELINE_OFF);
}

static void CopyPascalToCString(const unsigned char* pstr, char* dst, size_t dstSize) {
    unsigned char len;
    size_t i;
    if (dst == NULL || dstSize == 0) return;
    if (pstr == NULL) {
        dst[0] = '\0';
        return;
    }
    len = pstr[0];
    if ((size_t)len >= dstSize) len = (unsigned char)(dstSize - 1);
    for (i = 0; i < (size_t)len; ++i) dst[i] = (char)pstr[i + 1];
    dst[len] = '\0';
}

static unsigned long G3GetFSSpecModDate(const FSSpec* spec) {
    CInfoPBRec pb;
    Str255 pName;
    OSErr err;
    if (spec == NULL) return 0;
    BlockMoveData(spec->name, pName, (Size)(1 + spec->name[0]));
    pb.hFileInfo.ioNamePtr = pName;
    pb.hFileInfo.ioVRefNum = spec->vRefNum;
    pb.hFileInfo.ioFDirIndex = 0;
    pb.hFileInfo.ioDirID = spec->parID;
    err = PBGetCatInfo(&pb, false);
    if (err != noErr) return 0;
    return (unsigned long)pb.hFileInfo.ioFlMdDat;
}

static void SetDeckFileNameFromFSSpec(int deck_index, const FSSpec* spec) {
    if (deck_index < 0 || deck_index >= G3_DECK_COUNT || spec == NULL) return;
    CopyPascalToCString((const unsigned char*)spec->name, gDeckFileName[deck_index],
                        sizeof(gDeckFileName[deck_index]));
    gDeckFileSpec[deck_index] = *spec;
    gDeckHasFileSpec[deck_index] = 1;
    gDeckSrcModTime[deck_index] = G3GetFSSpecModDate(spec);
}

static void g3_put_u32_le(unsigned char* p, unsigned long v) {
    p[0] = (unsigned char)(v & 0xffUL);
    p[1] = (unsigned char)((v >> 8) & 0xffUL);
    p[2] = (unsigned char)((v >> 16) & 0xffUL);
    p[3] = (unsigned char)((v >> 24) & 0xffUL);
}

static unsigned long g3_get_u32_le(const unsigned char* p) {
    return ((unsigned long)p[0]) | ((unsigned long)p[1] << 8) | ((unsigned long)p[2] << 16) |
           ((unsigned long)p[3] << 24);
}

static unsigned short g3_get_u16_le(const unsigned char* p) {
    return (unsigned short)(p[0] | (p[1] << 8));
}

static void WaveCacheSpecFromAudio(const FSSpec* audio, FSSpec* cacheOut) {
    char cname[64];
    char* dot;
    Str255 pname;
    if (audio == NULL || cacheOut == NULL) return;
    *cacheOut = *audio;
    CopyPascalToCString((const unsigned char*)audio->name, cname, sizeof(cname));
    dot = strrchr(cname, '.');
    if (dot != NULL) *dot = '\0';
    strncat(cname, ".g3w", sizeof(cname) - strlen(cname) - 1);
    MakePString(cname, pname);
    BlockMoveData(pname, cacheOut->name, (Size)(1 + pname[0]));
}

static int TryLoadWaveformCache(int deck_index) {
    FSSpec cacheSpec;
    short refNum;
    OSErr err;
    unsigned char hdr[24];
    long readCount;
    unsigned long magic;
    unsigned short version;
    unsigned short channels;
    unsigned long frame_count;
    unsigned long pcm_bytes;
    unsigned long src_mod;
    unsigned short points;
    const G3Deck* deck;
    unsigned char* body;
    unsigned long bodyBytes;
    int i;
    if (deck_index < 0 || deck_index >= G3_DECK_COUNT) return 0;
    if (!gDeckHasFileSpec[deck_index]) return 0;
    deck = &gPlayer.decks[deck_index];
    if (!deck->loaded) return 0;
    WaveCacheSpecFromAudio(&gDeckFileSpec[deck_index], &cacheSpec);
    err = FSpOpenDF(&cacheSpec, fsRdPerm, &refNum);
    if (err != noErr) return 0;
    readCount = 24;
    err = FSRead(refNum, &readCount, hdr);
    if (err != noErr || readCount != 24) {
        FSClose(refNum);
        return 0;
    }
    magic = g3_get_u32_le(hdr + 0);
    version = g3_get_u16_le(hdr + 4);
    channels = g3_get_u16_le(hdr + 6);
    frame_count = g3_get_u32_le(hdr + 8);
    pcm_bytes = g3_get_u32_le(hdr + 12);
    src_mod = g3_get_u32_le(hdr + 16);
    points = g3_get_u16_le(hdr + 20);
    if (magic != G3_WAVE_CACHE_MAGIC || version != G3_WAVE_CACHE_VERSION || points != WAVE_POINTS ||
        channels != (unsigned short)deck->wav.channels || frame_count != deck->wav.frame_count ||
        pcm_bytes != deck->wav.pcm_bytes || src_mod != gDeckSrcModTime[deck_index]) {
        FSClose(refNum);
        return 0;
    }
    bodyBytes = (unsigned long)WAVE_POINTS * 2UL * (unsigned long)sizeof(short);
    body = (unsigned char*)malloc((size_t)bodyBytes);
    if (body == NULL) {
        FSClose(refNum);
        return 0;
    }
    readCount = (long)bodyBytes;
    err = FSRead(refNum, &readCount, body);
    FSClose(refNum);
    if (err != noErr || (unsigned long)readCount != bodyBytes) {
        free(body);
        return 0;
    }
    for (i = 0; i < WAVE_POINTS; ++i) {
        gWaveMin[deck_index][i] = (short)g3_get_u16_le(body + (i * 4) + 0);
        gWaveMax[deck_index][i] = (short)g3_get_u16_le(body + (i * 4) + 2);
    }
    free(body);
    return 1;
}

static void SaveWaveformCache(int deck_index) {
    FSSpec cacheSpec;
    short refNum;
    OSErr err;
    unsigned char hdr[24];
    unsigned char* body;
    unsigned long bodyBytes;
    long writeCount;
    const G3Deck* deck;
    int i;
    if (deck_index < 0 || deck_index >= G3_DECK_COUNT) return;
    if (!gDeckHasFileSpec[deck_index]) return;
    deck = &gPlayer.decks[deck_index];
    if (!deck->loaded) return;
    WaveCacheSpecFromAudio(&gDeckFileSpec[deck_index], &cacheSpec);
    err = FSpCreate(&cacheSpec, 0, 0, 0);
    if (err != noErr && err != dupFNErr) return;
    err = FSpOpenDF(&cacheSpec, fsWrPerm, &refNum);
    if (err != noErr) return;
    SetFPos(refNum, fsFromStart, 0L);
    g3_put_u32_le(hdr + 0, G3_WAVE_CACHE_MAGIC);
    hdr[4] = (unsigned char)(G3_WAVE_CACHE_VERSION & 0xff);
    hdr[5] = (unsigned char)((G3_WAVE_CACHE_VERSION >> 8) & 0xff);
    hdr[6] = (unsigned char)(deck->wav.channels & 0xff);
    hdr[7] = (unsigned char)((deck->wav.channels >> 8) & 0xff);
    g3_put_u32_le(hdr + 8, deck->wav.frame_count);
    g3_put_u32_le(hdr + 12, deck->wav.pcm_bytes);
    g3_put_u32_le(hdr + 16, gDeckSrcModTime[deck_index]);
    hdr[20] = (unsigned char)(WAVE_POINTS & 0xff);
    hdr[21] = (unsigned char)((WAVE_POINTS >> 8) & 0xff);
    hdr[22] = 0;
    hdr[23] = 0;
    writeCount = 24;
    err = FSWrite(refNum, &writeCount, hdr);
    if (err != noErr || writeCount != 24) {
        FSClose(refNum);
        return;
    }
    bodyBytes = (unsigned long)WAVE_POINTS * 2UL * (unsigned long)sizeof(short);
    body = (unsigned char*)malloc((size_t)bodyBytes);
    if (body == NULL) {
        FSClose(refNum);
        return;
    }
    for (i = 0; i < WAVE_POINTS; ++i) {
        body[(i * 4) + 0] = (unsigned char)((unsigned short)gWaveMin[deck_index][i] & 0xff);
        body[(i * 4) + 1] = (unsigned char)(((unsigned short)gWaveMin[deck_index][i] >> 8) & 0xff);
        body[(i * 4) + 2] = (unsigned char)((unsigned short)gWaveMax[deck_index][i] & 0xff);
        body[(i * 4) + 3] = (unsigned char)(((unsigned short)gWaveMax[deck_index][i] >> 8) & 0xff);
    }
    writeCount = (long)bodyBytes;
    err = FSWrite(refNum, &writeCount, body);
    free(body);
    if (err == noErr) SetEOF(refNum, 24L + (long)bodyBytes);
    FSClose(refNum);
}

static void UiPumpEvents(void) {
    EventRecord ev;
    while (WaitNextEvent(everyEvent, &ev, 0, NULL)) {
        if (ev.what == updateEvt && (WindowPtr)ev.message == gWindow)
            DoUpdate(&ev);
    }
    SyncVisibleDeckAndMaster();
    UpdateStatusAreas();
}

static void FreeDeckSamplesSafe(int deck_index, G3Deck* deck) {
    if (deck == NULL) return;
    if (deck->wav.backend == G3_WAV_NONE && deck->wav.samples == NULL && deck->wav.refNum < 0 &&
        deck->wav.scanRefNum < 0 && deck->wav.stdio_file == NULL) {
        return;
    }
    g3_freeze_release_deck(deck_index, &deck->freeze);
    deck->loaded = 0;
    deck->state = G3_TRANSPORT_STOPPED;
    deck->frame_pos = 0.0;
    deck->speed_current = 0.0;
    deck->speed_target = 0.0;
    UiPumpEvents();
    UiPumpEvents();
    g3_wav_close(&deck->wav);
    gDeckHasFileSpec[deck_index] = 0;
    gDeckSrcModTime[deck_index] = 0;
}

#if defined(__POWERPC__) || defined(__ppc__) || defined(powerc) || defined(__CFM68K__) || \
    (defined(__BIG_ENDIAN__) && __BIG_ENDIAN__)
static void g3_pcm16_le_to_host_yield(short* samples, unsigned long pcm_bytes) {
    unsigned char* b = (unsigned char*)samples;
    unsigned long pos = 0;
    const unsigned long chunk_bytes = 65536UL;
    while (pos + 1 < pcm_bytes) {
        unsigned long end = pos + chunk_bytes;
        unsigned long i;
        if (end > pcm_bytes) end = pcm_bytes;
        if (end & 1u) end--;
        for (i = pos; i + 1 < end; i += 2) {
            unsigned char t = b[i];
            b[i] = b[i + 1];
            b[i + 1] = t;
        }
        pos = end;
        UiPumpEvents();
    }
}
#else
static void g3_pcm16_le_to_host_yield(short* samples, unsigned long pcm_bytes) {
    (void)samples;
    (void)pcm_bytes;
}
#endif

static OSErr ReadFileBytesWithPump(short refNum, void* dst, unsigned long totalBytes) {
    unsigned char* p = (unsigned char*)dst;
    unsigned long remaining = totalBytes;
    const unsigned long blockSize = 65536UL;
    while (remaining > 0) {
        long readCount = (long)((remaining > blockSize) ? blockSize : remaining);
        OSErr err = FSRead(refNum, &readCount, p);
        if (err != noErr || readCount <= 0) return (err != noErr) ? err : -1;
        p += readCount;
        remaining -= (unsigned long)readCount;
        UiPumpEvents();
    }
    return noErr;
}

static short VolToSlider(float v) {
    long x = (long)(v * 100.0f + 0.5f);
    if (x < 0) x = 0;
    if (x > 300) x = 300;
    return (short)x;
}

static float SliderToVol(short s) {
    if (s < 0) s = 0;
    if (s > 300) s = 300;
    return (float)s / 100.0f;
}

static short PanToSlider(float p) {
    long x = (long)((p + 1.0f) * 100.0f);
    if (x < 0) x = 0;
    if (x > 200) x = 200;
    return (short)x;
}

static float SliderToPan(short s) {
    if (s < 0) s = 0;
    if (s > 200) s = 200;
    return ((float)s / 100.0f) - 1.0f;
}

static short PitchToSlider(float p) {
    long x = (long)(p + 100.0f + 0.5f);
    if (x < 0) x = 0;
    if (x > 200) x = 200;
    return (short)x;
}

static float SliderToPitch(short s) {
    if (s < 0) s = 0;
    if (s > 200) s = 200;
    return (float)s - 100.0f;
}

static short RampToSlider(float sec) {
    long x = (long)(sec * 1000.0f + 0.5f);
    if (x < 0) x = 0;
    if (x > 8000) x = 8000;
    return (short)x;
}

static float SliderToRamp(short s) {
    if (s < 0) s = 0;
    if (s > 8000) s = 8000;
    return (float)s / 1000.0f;
}

static short MasterDbToSlider(float db) {
    long x = (long)((db - MASTER_DB_MIN) * 10.0f + 0.5f);
    if (x < 0) x = 0;
    if (x > MASTER_SLIDER_STEPS) x = MASTER_SLIDER_STEPS;
    return (short)x;
}

static float SliderToMasterDb(short s) {
    if (s < 0) s = 0;
    if (s > MASTER_SLIDER_STEPS) s = MASTER_SLIDER_STEPS;
    return MASTER_DB_MIN + (float)s / 10.0f;
}

/* Deck fader dB: 1.0 linear = 0 dB, 0 = quiet (clamp display). */
static float DeckVolLinearToDb(float linear) {
    if (linear < 0.000001f) return -96.0f;
    return (float)(20.0 * log10((double)linear));
}

static void FormatClock(unsigned long ms, char* out, int out_len) {
    unsigned long sec = ms / 1000UL;
    unsigned long m = sec / 60UL;
    unsigned long s = sec % 60UL;
    snprintf(out, (size_t)out_len, "%lu:%02lu", m, s);
}

static float SmoothPeakForDisplay(float current, float shown) {
    if (current > shown) return current;
    if (current < shown - PEAK_DISPLAY_DECAY_DB) return current;
    return shown;
}

static void MarkWaveformDirty(void) {
    gWaveformBodyDirty = 1;
    gWavePlayheadX = -1;
}

static void RedrawVisibleWaveform(void) {
    MarkWaveformDirty();
    DrawWaveformBody();
}

static int AnyDeckPlaying(void) {
    int i;
    for (i = 0; i < G3_DECK_COUNT; ++i) {
        if (gPlayer.decks[i].state == G3_TRANSPORT_PLAYING ||
            gPlayer.decks[i].state == G3_TRANSPORT_FROZEN) {
            return 1;
        }
    }
    return 0;
}

static int WaveHit(Point localPt) {
    return PtInRect(localPt, &gWaveRect) ? 1 : 0;
}

static void ApplyVisibleDeckSlidersToEngine(void) {
    int d = gUiDeck;
    if (gVolSlider == NULL) return;
    g3_player_set_volume(&gPlayer, d, SliderToVol(GetControlValue(gVolSlider)));
    g3_player_set_pan(&gPlayer, d, SliderToPan(GetControlValue(gPanSlider)));
    g3_player_set_pitch_percent(&gPlayer, d, SliderToPitch(GetControlValue(gPitchSlider)));
    g3_player_set_ramp_seconds(&gPlayer, d, SliderToRamp(GetControlValue(gRampSlider)));
}

static void LoadDeckSlidersFromEngine(int deckIndex) {
    const G3Deck* deck;
    if (deckIndex < 0 || deckIndex >= G3_DECK_COUNT || gVolSlider == NULL) return;
    deck = &gPlayer.decks[deckIndex];
    SetControlValue(gVolSlider, VolToSlider(deck->volume));
    SetControlValue(gPanSlider, PanToSlider(deck->pan));
    SetControlValue(gPitchSlider, PitchToSlider(deck->pitch_percent));
    SetControlValue(gRampSlider, RampToSlider(deck->ramp_seconds));
    Draw1Control(gVolSlider);
    Draw1Control(gPanSlider);
    Draw1Control(gPitchSlider);
    Draw1Control(gRampSlider);
}

static void SwitchToDeck(int deckIndex) {
    if (deckIndex < 0 || deckIndex >= G3_DECK_COUNT) return;
    if (deckIndex == gUiDeck) return;
    ApplyVisibleDeckSlidersToEngine();
    gUiDeck = deckIndex;
    LoadDeckSlidersFromEngine(gUiDeck);
    MarkWaveformDirty();
    InvalidateSliderValueCache();
    DrawSliderValues();
    DrawWaveformBody();
    snprintf(gLastMessage, sizeof(gLastMessage), "Editing deck %d (all decks still mix).", gUiDeck + 1);
    UpdateStatusAreas();
}

/* Push visible deck + master into the mixer every event-loop tick. */
static void SyncVisibleDeckAndMaster(void) {
    if (gMasterSlider == NULL) return;
    ApplyVisibleDeckSlidersToEngine();
    g3_player_set_master_volume(&gPlayer, g3_db_to_linear(SliderToMasterDb(GetControlValue(gMasterSlider))));
}

static short SliderMaxForControl(ControlHandle c) {
    if (c == gVolSlider) return 300;
    if (c == gMasterSlider) return MASTER_SLIDER_STEPS;
    if (c == gPanSlider) return 200;
    if (c == gPitchSlider) return 200;
    if (c == gRampSlider) return 8000;
    return 32767;
}

static void MaybeNudgeScrollbar(ControlHandle c, ControlPartCode initialPart) {
    short v0;
    short lo;
    short hi;
    short v1;
    short delta;
    if (c == NULL) return;
    if (initialPart != inUpButton && initialPart != inDownButton && initialPart != inPageUp && initialPart != inPageDown) return;
    v0 = GetControlValue(c);
    lo = 0;
    hi = SliderMaxForControl(c);
    v1 = v0;
    if (initialPart == inUpButton && v0 > lo) v1 = (short)(v0 - 1);
    else if (initialPart == inDownButton && v0 < hi) v1 = (short)(v0 + 1);
    else if (initialPart == inPageUp) {
        delta = (short)((hi - lo) / 10);
        if (delta < 1) delta = 1;
        v1 = (short)(v0 - delta);
        if (v1 < lo) v1 = lo;
    } else if (initialPart == inPageDown) {
        delta = (short)((hi - lo) / 10);
        if (delta < 1) delta = 1;
        v1 = (short)(v0 + delta);
        if (v1 > hi) v1 = hi;
    }
    if (v1 != v0) {
        SetControlValue(c, v1);
        Draw1Control(c);
    }
}

static void ResetAllSlidersToDefaults(void) {
    int i;
    for (i = 0; i < G3_DECK_COUNT; ++i) {
        g3_player_set_volume(&gPlayer, i, DEFAULT_DECK_VOL);
        g3_player_set_pan(&gPlayer, i, 0.0f);
        g3_player_set_pitch_percent(&gPlayer, i, 0.0f);
        g3_player_set_ramp_seconds(&gPlayer, i, 0.0f);
    }
    SetControlValue(gMasterSlider, MasterDbToSlider(MASTER_DB_DEFAULT));
    g3_player_set_master_volume(&gPlayer, g3_db_to_linear(MASTER_DB_DEFAULT));
    LoadDeckSlidersFromEngine(gUiDeck);
    Draw1Control(gMasterSlider);
    snprintf(gLastMessage, sizeof(gLastMessage), "All decks + master reset to defaults.");
}

static void ResetDeckVolDefault(void) {
    int d = gUiDeck;
    SetControlValue(gVolSlider, VolToSlider(DEFAULT_DECK_VOL));
    g3_player_set_volume(&gPlayer, d, DEFAULT_DECK_VOL);
    Draw1Control(gVolSlider);
    snprintf(gLastMessage, sizeof(gLastMessage), "Deck %d volume -> default.", d + 1);
}

static void ResetDeckPanDefault(void) {
    int d = gUiDeck;
    SetControlValue(gPanSlider, PanToSlider(0.0f));
    g3_player_set_pan(&gPlayer, d, 0.0f);
    Draw1Control(gPanSlider);
    snprintf(gLastMessage, sizeof(gLastMessage), "Deck %d pan -> center.", d + 1);
}

static void ResetDeckPitchDefault(void) {
    int d = gUiDeck;
    SetControlValue(gPitchSlider, PitchToSlider(0.0f));
    g3_player_set_pitch_percent(&gPlayer, d, 0.0f);
    Draw1Control(gPitchSlider);
    snprintf(gLastMessage, sizeof(gLastMessage), "Deck %d pitch -> 0%%.", d + 1);
}

static void ResetDeckRampDefault(void) {
    int d = gUiDeck;
    SetControlValue(gRampSlider, RampToSlider(0.0f));
    g3_player_set_ramp_seconds(&gPlayer, d, 0.0f);
    Draw1Control(gRampSlider);
    snprintf(gLastMessage, sizeof(gLastMessage), "Deck %d ramp -> 0.", d + 1);
}

static void ResetMasterDefault(void) {
    SetControlValue(gMasterSlider, MasterDbToSlider(MASTER_DB_DEFAULT));
    g3_player_set_master_volume(&gPlayer, g3_db_to_linear(MASTER_DB_DEFAULT));
    Draw1Control(gMasterSlider);
    snprintf(gLastMessage, sizeof(gLastMessage), "Master -> -4 dB.");
}

static void DrawSliderCaptions(void) {
    int row;
    SetPort(gWindow);
    for (row = 0; row < 4; ++row) {
        short y0 = (short)(SL0_TOP + row * ROW_STEP);
        MoveTo(LBL_X, (short)(y0 + SL_TRACK_TEXT_BASE));
        switch (row) {
            case 0: DrawCString("Vol"); break;
            case 1: DrawCString("Pan"); break;
            case 2: DrawCString("Pitch"); break;
            default: DrawCString("Ramp"); break;
        }
    }
    MoveTo(LBL_X, MASTER_LBL_BASELINE);
    DrawCString("Master");
}

static void RefreshPlayPauseButton(void) {
    static int s_ppDeck = -1;
    static G3TransportState s_ppState = (G3TransportState)(-999);
    G3TransportState st;
    if (gPlayBtn == NULL || gWindow == NULL) return;
    st = gPlayer.decks[gUiDeck].state;
    if (gUiDeck == s_ppDeck && st == s_ppState) return;
    s_ppDeck = gUiDeck;
    s_ppState = st;
    if (st == G3_TRANSPORT_PLAYING)
        SetControlTitle(gPlayBtn, "\pPause");
    else
        SetControlTitle(gPlayBtn, "\pPlay");
    Draw1Control(gPlayBtn);
}

static void RefreshFreezeButton(void) {
    static int s_frDeck = -1;
    static G3TransportState s_frState = (G3TransportState)(-999);
    G3TransportState st;
    if (gFreezeBtn == NULL || gWindow == NULL) return;
    st = gPlayer.decks[gUiDeck].state;
    if (gUiDeck == s_frDeck && st == s_frState) return;
    s_frDeck = gUiDeck;
    s_frState = st;
    if (st == G3_TRANSPORT_FROZEN)
        SetControlTitle(gFreezeBtn, "\pUnfreeze");
    else
        SetControlTitle(gFreezeBtn, "\pFreeze");
    Draw1Control(gFreezeBtn);
}

#define G3_WAVE_SCAN_BLOCK_FRAMES 4096

static void RebuildWaveformBucket(int deck_index, int point_index) {
    G3Deck* deck;
    G3WavData* wav;
    unsigned long start;
    unsigned long end;
    long mn;
    long mx;
    unsigned long pos;
    short block[G3_WAVE_SCAN_BLOCK_FRAMES * 2];
    if (deck_index < 0 || deck_index >= G3_DECK_COUNT) return;
    if (point_index < 0 || point_index >= WAVE_POINTS) return;
    deck = &gPlayer.decks[deck_index];
    wav = &deck->wav;
    if (!deck->loaded || wav->frame_count == 0) {
        gWaveMin[deck_index][point_index] = 0;
        gWaveMax[deck_index][point_index] = 0;
        return;
    }

    start = (unsigned long)(((double)point_index * (double)wav->frame_count) / (double)WAVE_POINTS);
    end = (unsigned long)(((double)(point_index + 1) * (double)wav->frame_count) / (double)WAVE_POINTS);
    mn = 32767;
    mx = -32768;
    if (end <= start) end = start + 1;
    if (end > wav->frame_count) end = wav->frame_count;

    pos = start;
    while (pos < end) {
        unsigned long n = end - pos;
        unsigned long f;
        if (n > G3_WAVE_SCAN_BLOCK_FRAMES) n = G3_WAVE_SCAN_BLOCK_FRAMES;
        if (!g3_wav_read_pcm_at(wav, pos, n, block)) break;
        for (f = 0; f < n; ++f) {
            long s;
            if (wav->channels == 1) {
                s = block[f];
            } else {
                long l = block[(f * 2) + 0];
                long r = block[(f * 2) + 1];
                s = (l + r) / 2;
            }
            if (s < mn) mn = s;
            if (s > mx) mx = s;
        }
        pos += n;
    }
    if (mn > mx) {
        mn = 0;
        mx = 0;
    }
    gWaveMin[deck_index][point_index] = (short)mn;
    gWaveMax[deck_index][point_index] = (short)mx;
}

static void StartWaveformRebuild(int deck_index) {
    int i;
    if (deck_index < 0 || deck_index >= G3_DECK_COUNT) return;
    if (TryLoadWaveformCache(deck_index)) {
        gWaveRebuildDeck = -1;
        gWaveRebuildNext = 0;
        if (deck_index == gUiDeck) {
            MarkWaveformDirty();
            DrawWaveformBody();
        }
        return;
    }
    for (i = 0; i < WAVE_POINTS; ++i) {
        gWaveMin[deck_index][i] = 0;
        gWaveMax[deck_index][i] = 0;
    }
    gWaveRebuildDeck = deck_index;
    gWaveRebuildNext = 0;
}

static int AdvanceWaveformRebuild(int max_points) {
    int built = 0;
    if (gWaveRebuildDeck < 0 || gWaveRebuildDeck >= G3_DECK_COUNT) return 0;
    while (gWaveRebuildNext < WAVE_POINTS && built < max_points) {
        RebuildWaveformBucket(gWaveRebuildDeck, gWaveRebuildNext);
        gWaveRebuildNext++;
        built++;
    }
    if (gWaveRebuildNext >= WAVE_POINTS) {
        int done_deck = gWaveRebuildDeck;
        gWaveRebuildDeck = -1;
        gWaveRebuildNext = 0;
        SaveWaveformCache(done_deck);
        if (done_deck == gUiDeck) {
            MarkWaveformDirty();
            DrawWaveformBody();
        }
    }
    return built;
}

static void DrawWaveformBody(void) {
    int d = gUiDeck;
    G3DeckStatus st;
    Rect r = gWaveRect;
    int i;
    int w;
    int mid;
    long play_x;
    SetPort(gWindow);
    PenMode(patCopy);
    EraseRect(&r);
    FrameRect(&r);
    g3_player_get_deck_status(&gPlayer, d, &st);

    w = r.right - r.left - 2;
    mid = (r.top + r.bottom) / 2;
    for (i = 0; i < WAVE_POINTS; ++i) {
        int x = r.left + 1 + (i * w) / WAVE_POINTS;
        int y1 = mid - ((int)gWaveMax[d][i] * ((r.bottom - r.top - 6) / 2)) / 32768;
        int y2 = mid - ((int)gWaveMin[d][i] * ((r.bottom - r.top - 6) / 2)) / 32768;
        MoveTo(x, y1);
        LineTo(x, y2);
    }

    gWavePlayheadX = -1;
    if (st.loaded && st.duration_ms > 0) {
        play_x = (long)((((double)st.position_ms / (double)st.duration_ms) * (double)w));
        if (play_x < 0) play_x = 0;
        if (play_x > w) play_x = w;
        MoveTo(r.left + 1 + (short)play_x, r.top + 1);
        LineTo(r.left + 1 + (short)play_x, r.bottom - 1);
        gWavePlayheadX = (short)play_x;
    }
    gWaveformBodyDirty = 0;
}

static void UpdateWaveformPlayhead(void) {
    int d = gUiDeck;
    G3DeckStatus st;
    Rect r = gWaveRect;
    int w;
    long play_x;
    short x0;
    if (gWaveformBodyDirty || gWindow == NULL) return;
    g3_player_get_deck_status(&gPlayer, d, &st);
    if (!st.loaded || st.duration_ms == 0) {
        if (gWavePlayheadX >= 0) {
            SetPort(gWindow);
            PenMode(patXor);
            PenSize(1, 1);
            x0 = (short)(r.left + 1 + gWavePlayheadX);
            MoveTo(x0, (short)(r.top + 1));
            LineTo(x0, (short)(r.bottom - 1));
            PenMode(patCopy);
            gWavePlayheadX = -1;
        }
        return;
    }

    w = r.right - r.left - 2;
    play_x = (long)((((double)st.position_ms / (double)st.duration_ms) * (double)w));
    if (play_x < 0) play_x = 0;
    if (play_x > w) play_x = w;
    if (play_x == (long)gWavePlayheadX) return;

    SetPort(gWindow);
    PenMode(patXor);
    PenSize(1, 1);
    if (gWavePlayheadX >= 0) {
        x0 = (short)(r.left + 1 + gWavePlayheadX);
        MoveTo(x0, (short)(r.top + 1));
        LineTo(x0, (short)(r.bottom - 1));
    }
    x0 = (short)(r.left + 1 + play_x);
    MoveTo(x0, (short)(r.top + 1));
    LineTo(x0, (short)(r.bottom - 1));
    PenMode(patCopy);
    gWavePlayheadX = (short)play_x;
}

static int LoadDeckFromFSSpec(int deck_index, const FSSpec* spec) {
    short refNum;
    long readCount;
    long dataPos;
    OSErr err;
    char id[4];
    unsigned char le4[4];
    unsigned char fmtbuf[16];
    unsigned long chunkSize;
    unsigned short fmtAudio = 0;
    unsigned short fmtChannels = 0;
    unsigned long fmtRate = 0;
    unsigned short fmtBits = 0;
    int gotFmt = 0;
    int gotData = 0;
    unsigned long pcmBytes = 0;
    unsigned long dataOffset = 0;
    G3Deck* deck;
    if (deck_index < 0 || deck_index >= G3_DECK_COUNT || spec == NULL) return 0;
    deck = &gPlayer.decks[deck_index];

    err = FSpOpenDF(spec, fsRdPerm, &refNum);
    if (err != noErr) {
        snprintf(gLastMessage, sizeof(gLastMessage), "Deck %d load failed: open error %d.", deck_index + 1, (int)err);
        return 0;
    }

    readCount = 4;
    err = FSRead(refNum, &readCount, id);
    if (err != noErr || readCount != 4 || memcmp(id, "RIFF", 4) != 0) {
        FSClose(refNum);
        snprintf(gLastMessage, sizeof(gLastMessage), "Deck %d load failed: missing RIFF.", deck_index + 1);
        return 0;
    }
    readCount = 4;
    err = FSRead(refNum, &readCount, le4);
    if (err != noErr || readCount != 4) {
        FSClose(refNum);
        snprintf(gLastMessage, sizeof(gLastMessage), "Deck %d load failed: bad RIFF size.", deck_index + 1);
        return 0;
    }
    readCount = 4;
    err = FSRead(refNum, &readCount, id);
    if (err != noErr || readCount != 4 || memcmp(id, "WAVE", 4) != 0) {
        FSClose(refNum);
        snprintf(gLastMessage, sizeof(gLastMessage), "Deck %d load failed: missing WAVE.", deck_index + 1);
        return 0;
    }

    while (!gotData) {
        readCount = 4;
        err = FSRead(refNum, &readCount, id);
        if (err != noErr || readCount != 4) break;
        readCount = 4;
        err = FSRead(refNum, &readCount, le4);
        if (err != noErr || readCount != 4) break;
        chunkSize = g3_mem_u32_le(le4);

        if (memcmp(id, "fmt ", 4) == 0) {
            if (chunkSize < 16) break;
            readCount = 16;
            err = FSRead(refNum, &readCount, fmtbuf);
            if (err != noErr || readCount != 16) break;
            fmtAudio = g3_mem_u16_le(fmtbuf + 0);
            fmtChannels = g3_mem_u16_le(fmtbuf + 2);
            fmtRate = g3_mem_u32_le(fmtbuf + 4);
            fmtBits = g3_mem_u16_le(fmtbuf + 14);
            if (chunkSize > 16) {
                SetFPos(refNum, fsFromMark, (long)(chunkSize - 16));
            }
            gotFmt = 1;
        } else if (memcmp(id, "data", 4) == 0) {
            err = GetFPos(refNum, &dataPos);
            if (err != noErr) break;
            pcmBytes = chunkSize;
            dataOffset = (unsigned long)dataPos;
            SetFPos(refNum, fsFromMark, (long)chunkSize);
            gotData = 1;
        } else {
            SetFPos(refNum, fsFromMark, (long)chunkSize);
        }
        if (chunkSize & 1u) SetFPos(refNum, fsFromMark, 1L);
    }

    if (!gotFmt || !gotData || fmtAudio != 1 || (fmtChannels != 1 && fmtChannels != 2) || fmtBits != 16 || fmtRate != 44100UL) {
        FSClose(refNum);
        snprintf(gLastMessage, sizeof(gLastMessage), "Deck %d load failed: needs PCM16 44.1k mono/stereo.", deck_index + 1);
        return 0;
    }

    FreeDeckSamplesSafe(deck_index, deck);
    deck->wav.backend = G3_WAV_FILE;
    deck->wav.refNum = refNum;
    deck->wav.scanRefNum = -1;
    err = FSpOpenDF(spec, fsRdPerm, &deck->wav.scanRefNum);
    if (err != noErr) {
        deck->wav.scanRefNum = -1;
    }
    deck->wav.stdio_file = NULL;
    deck->wav.data_offset = dataOffset;
    deck->wav.pcm_bytes = pcmBytes;
    deck->wav.channels = (int)fmtChannels;
    deck->wav.frame_count = pcmBytes / (unsigned long)(fmtChannels * 2);
    if (!g3_wav_open_ring(&deck->wav)) {
        g3_wav_close(&deck->wav);
        snprintf(gLastMessage, sizeof(gLastMessage), "Deck %d load failed: out of memory.", deck_index + 1);
        return 0;
    }
    deck->loaded = 1;
    deck->state = G3_TRANSPORT_STOPPED;
    deck->frame_pos = 0.0;
    deck->speed_current = 0.0;
    deck->speed_target = 0.0;
    deck->peak_linear = 0.0f;

    SetDeckFileNameFromFSSpec(deck_index, spec);
    gPrevDeckStat[0] = '\0';
    gPrevMasterStat[0] = '\0';
    snprintf(gLastMessage, sizeof(gLastMessage), "Deck %d loaded: %s", deck_index + 1, gDeckFileName[deck_index]);
    StartWaveformRebuild(deck_index);
    if (deck_index == gUiDeck) {
        MarkWaveformDirty();
        DrawWaveformBody();
    }
    return 1;
}

static int PickAndLoadDeckFile(int deck_index) {
    StandardFileReply reply;
    int ok;
    if (deck_index < 0 || deck_index >= G3_DECK_COUNT) return 0;

    StandardGetFile(NULL, -1, NULL, &reply);
    if (!reply.sfGood) return 0;

    snprintf(gLastMessage, sizeof(gLastMessage), "Deck %d: loading...", deck_index + 1);
    gPrevMsgInit = 0;
    gPrevDeckStat[0] = '\0';
    UpdateStatusAreas();
    ok = LoadDeckFromFSSpec(deck_index, &reply.sfFile);
    return ok;
}

static void DrawSliderValueCached(int index, const char* text, short baselineY) {
    if (strcmp(text, gPrevSliderVal[index]) == 0) return;
    SetPort(gWindow);
    EraseRect(&gSliderValueRect[index]);
    MoveTo(gSliderValueRect[index].left, baselineY);
    DrawCString(text);
    strncpy(gPrevSliderVal[index], text, sizeof(gPrevSliderVal[index]) - 1);
    gPrevSliderVal[index][sizeof(gPrevSliderVal[index]) - 1] = '\0';
}

static void InvalidateSliderValueCache(void) {
    int i;
    for (i = 0; i < SLIDER_VAL_COUNT; ++i) gPrevSliderVal[i][0] = '\0';
}

static void DrawSliderValues(void) {
    char text[64];

    snprintf(text, sizeof(text), "%+.1f dB", (double)DeckVolLinearToDb(SliderToVol(GetControlValue(gVolSlider))));
    DrawSliderValueCached(0, text, (short)(SL0_TOP + 0 * ROW_STEP + SL_TRACK_TEXT_BASE));

    snprintf(text, sizeof(text), "%.2f", SliderToPan(GetControlValue(gPanSlider)));
    DrawSliderValueCached(1, text, (short)(SL0_TOP + 1 * ROW_STEP + SL_TRACK_TEXT_BASE));

    snprintf(text, sizeof(text), "%.1f%%", SliderToPitch(GetControlValue(gPitchSlider)));
    DrawSliderValueCached(2, text, (short)(SL0_TOP + 2 * ROW_STEP + SL_TRACK_TEXT_BASE));

    snprintf(text, sizeof(text), "%d ms", (int)GetControlValue(gRampSlider));
    DrawSliderValueCached(3, text, (short)(SL0_TOP + 3 * ROW_STEP + SL_TRACK_TEXT_BASE));

    snprintf(text, sizeof(text), "%+.1f dB", (double)SliderToMasterDb(GetControlValue(gMasterSlider)));
    DrawSliderValueCached(4, text, MASTER_LBL_BASELINE);
}

static void UpdateStatusAreas(void) {
    char line[384];
    G3DeckStatus d;
    char pos[16];
    char dur[16];
    const char* fileLabel;
    short deckY;
    short masterY;
    short msgY;
    static int s_peakDeck = -1;
    SetPort(gWindow);
    deckY = TextBaselineInRect(&gDeckStatusRect);
    masterY = TextBaselineInRect(&gMasterStatusRect);
    msgY = MSG_BASELINE;

    if (s_peakDeck != gUiDeck) {
        s_peakDeck = gUiDeck;
        gShownDeckPeakDb = -120.0f;
    }

    g3_player_get_deck_status(&gPlayer, gUiDeck, &d);
    gShownDeckPeakDb = SmoothPeakForDisplay(d.peak_dbfs, gShownDeckPeakDb);
    FormatClock(d.position_ms, pos, sizeof(pos));
    FormatClock(d.duration_ms, dur, sizeof(dur));
    if (gWaveRebuildDeck == gUiDeck)
        fileLabel = "(building waveform)";
    else if (gDeckFileName[gUiDeck][0] != '\0')
        fileLabel = gDeckFileName[gUiDeck];
    else
        fileLabel = "(no file)";
    snprintf(line, sizeof(line),
             "Deck %d  %s  %s  %s / %s  Peak %.0f dBFS",
             gUiDeck + 1,
             d.state == G3_TRANSPORT_PLAYING ? "PLAY" :
             (d.state == G3_TRANSPORT_FROZEN ? "FREEZE" :
              (d.state == G3_TRANSPORT_PAUSED ? "PAUSE" : "STOP")),
             fileLabel, pos, dur, (double)gShownDeckPeakDb);
    if (strcmp(line, gPrevDeckStat) != 0) {
        EraseRect(&gDeckStatusRect);
        MoveTo(gDeckStatusRect.left + 6, deckY);
        DrawCString(line);
        strncpy(gPrevDeckStat, line, sizeof(gPrevDeckStat) - 1);
        gPrevDeckStat[sizeof(gPrevDeckStat) - 1] = '\0';
    }

    {
        G3MasterStatus m;
        g3_player_get_master_status(&gPlayer, &m);
        gShownMasterPeakDb = SmoothPeakForDisplay(m.peak_dbfs, gShownMasterPeakDb);
        snprintf(line, sizeof(line), "MASTER Peak %.0f dBFS", (double)gShownMasterPeakDb);
        if (strcmp(line, gPrevMasterStat) != 0) {
            EraseRect(&gMasterStatusRect);
            MoveTo(gMasterStatusRect.left + 6, masterY);
            DrawCString(line);
            strncpy(gPrevMasterStat, line, sizeof(gPrevMasterStat) - 1);
            gPrevMasterStat[sizeof(gPrevMasterStat) - 1] = '\0';
        }
    }

    if (!gPrevMsgInit || strcmp(gLastMessage, gPrevMsgDrawn) != 0) {
        EraseRect(&gMessageRect);
        MoveTo(gMessageRect.left, msgY);
        DrawCString(gLastMessage);
        strncpy(gPrevMsgDrawn, gLastMessage, sizeof(gPrevMsgDrawn) - 1);
        gPrevMsgDrawn[sizeof(gPrevMsgDrawn) - 1] = '\0';
        gPrevMsgInit = 1;
    }

    RefreshPlayPauseButton();
    RefreshFreezeButton();
}

static void LayoutUI(void) {
    Rect r;
    short left;
    short top;
    short mb;
    int row;

    SetRect(&r, 20, DECK_TAB_TOP, 86, DECK_TAB_TOP + DECK_TAB_H);
    gTabBtn[0] = NewControl(gWindow, &r, "\pD1", true, 0, 0, 1, pushButProc, 0);
    SetRect(&r, 94, DECK_TAB_TOP, 160, DECK_TAB_TOP + DECK_TAB_H);
    gTabBtn[1] = NewControl(gWindow, &r, "\pD2", true, 0, 0, 1, pushButProc, 0);
    SetRect(&r, 168, DECK_TAB_TOP, 234, DECK_TAB_TOP + DECK_TAB_H);
    gTabBtn[2] = NewControl(gWindow, &r, "\pD3", true, 0, 0, 1, pushButProc, 0);

    left = 20;
    for (row = 0; row < 4; ++row) {
        short y0 = (short)(SL0_TOP + row * ROW_STEP);
        short y1 = (short)(y0 + SL_H);
        SetRect(&r, SL_LEFT, y0, SL_RIGHT, y1);
        switch (row) {
            case 0:
                gVolSlider = NewControl(gWindow, &r, "\p", true, VolToSlider(DEFAULT_DECK_VOL), 0, 300, scrollBarProc, 0);
                SetRect(&gSliderValueRect[0], VAL_LEFT, (short)(y0 - 2), VAL_RIGHT, (short)(y1 + 4));
                SetRect(&r, BTN_LEFT, y0, BTN_RIGHT, y1);
                gVolDefBtn = NewControl(gWindow, &r, "\pDft", true, 0, 0, 1, pushButProc, 0);
                break;
            case 1:
                gPanSlider = NewControl(gWindow, &r, "\p", true, PanToSlider(0.0f), 0, 200, scrollBarProc, 0);
                SetRect(&gSliderValueRect[1], VAL_LEFT, (short)(y0 - 2), VAL_RIGHT, (short)(y1 + 4));
                SetRect(&r, BTN_LEFT, y0, BTN_RIGHT, y1);
                gPanDefBtn = NewControl(gWindow, &r, "\pDft", true, 0, 0, 1, pushButProc, 0);
                break;
            case 2:
                gPitchSlider = NewControl(gWindow, &r, "\p", true, PitchToSlider(0.0f), 0, 200, scrollBarProc, 0);
                SetRect(&gSliderValueRect[2], VAL_LEFT, (short)(y0 - 2), VAL_RIGHT, (short)(y1 + 4));
                SetRect(&r, BTN_LEFT, y0, BTN_RIGHT, y1);
                gPitchDefBtn = NewControl(gWindow, &r, "\pDft", true, 0, 0, 1, pushButProc, 0);
                break;
            default:
                gRampSlider = NewControl(gWindow, &r, "\p", true, RampToSlider(0.0f), 0, 8000, scrollBarProc, 0);
                SetRect(&gSliderValueRect[3], VAL_LEFT, (short)(y0 - 2), VAL_RIGHT, (short)(y1 + 4));
                SetRect(&r, BTN_LEFT, y0, BTN_RIGHT, y1);
                gRampDefBtn = NewControl(gWindow, &r, "\pDft", true, 0, 0, 1, pushButProc, 0);
                break;
        }
    }

    top = TRANSPORT_TOP;
    SetRect(&r, left, top, left + 56, top + TR_H);
    gLoadBtn = NewControl(gWindow, &r, "\pLoad", true, 0, 0, 1, pushButProc, 0);
    SetRect(&r, left + 62, top, left + 128, top + TR_H);
    gPlayBtn = NewControl(gWindow, &r, "\pPlay", true, 0, 0, 1, pushButProc, 0);
    SetRect(&r, left + 134, top, left + 200, top + TR_H);
    gFreezeBtn = NewControl(gWindow, &r, "\pFreeze", true, 0, 0, 1, pushButProc, 0);
    SetRect(&r, left + 206, top, left + 270, top + TR_H);
    gStopBtn = NewControl(gWindow, &r, "\pStop", true, 0, 0, 1, pushButProc, 0);

    SetRect(&gWaveRect, left, WAVE_TOP, WAVE_RIGHT, WAVE_BOTTOM);
    SetRect(&gDeckStatusRect, left, STAT_TOP, WAVE_RIGHT, STAT_BOTTOM);

    mb = MASTER_BAND_TOP;
    SetRect(&r, SL_LEFT, MASTER_SL_T, SL_RIGHT, MASTER_SL_B);
    gMasterSlider = NewControl(gWindow, &r, "\p", true, MasterDbToSlider(MASTER_DB_DEFAULT), 0, MASTER_SLIDER_STEPS, scrollBarProc, 0);
    SetRect(&gSliderValueRect[4], VAL_LEFT, (short)(MASTER_SL_T - 2), VAL_RIGHT, (short)(MASTER_SL_B + 4));
    SetRect(&r, BTN_LEFT, MASTER_SL_T, BTN_RIGHT, MASTER_SL_B);
    gMasterDefBtn = NewControl(gWindow, &r, "\pDft", true, 0, 0, 1, pushButProc, 0);

    SetRect(&gMasterStatusRect, 20, MASTER_STAT_T, WAVE_RIGHT, MASTER_STAT_B);
    SetRect(&gMessageRect, MSG_TEXT_LEFT, MSG_RECT_T, MSG_TEXT_RIGHT, MSG_RECT_B);
    SetRect(&r, BTN_CORNER_DEF_L, BTN_CORNER_TOP, BTN_CORNER_DEF_R, BTN_CORNER_BOT);
    gResetBtn = NewControl(gWindow, &r, "\pDefaults", true, 0, 0, 1, pushButProc, 0);
    SetRect(&r, BTN_CORNER_QUIT_L, BTN_CORNER_TOP, BTN_CORNER_QUIT_R, BTN_CORNER_BOT);
    gQuitBtn = NewControl(gWindow, &r, "\pQuit", true, 0, 0, 1, pushButProc, 0);
    MoveTo(LBL_X, MSG_BASELINE);
    DrawString("\pMessage:");
    FrameRect(&gDeckStatusRect);
    FrameRect(&gMasterStatusRect);
}

static pascal void SliderAction(ControlHandle control, short partCode) {
    (void)control;
    (void)partCode;
    /* TrackControl runs modally: main loop Sync* does not run during drags. */
    SyncVisibleDeckAndMaster();
    DrawSliderValues();
}

static void FillBuffer(SndDoubleBufferPtr db) {
    db->dbNumFrames = FRAMES_PER_BUFFER;
    db->dbFlags = dbBufferReady;
    g3_player_render(&gPlayer, (short*)db->dbSoundData, FRAMES_PER_BUFFER);
    if (!gRunning) db->dbFlags = dbBufferReady | dbLastBuffer;
}

static pascal void MyDoubleBackProc(SndChannelPtr chan, SndDoubleBufferPtr db) {
    (void)chan;
    FillBuffer(db);
}

static void InitAudio(void) {
    OSErr err;
    long dbSize = sizeof(SndDoubleBuffer) + (((FRAMES_PER_BUFFER * 2L * (long)sizeof(SInt16))) - 1);
    gBufA = (SndDoubleBufferPtr)NewPtrClear(dbSize);
    gBufB = (SndDoubleBufferPtr)NewPtrClear(dbSize);
    if (gBufA == NULL || gBufB == NULL) return;

    gDoubleBackUPP = NewSndDoubleBackProc(MyDoubleBackProc);
    gHeader.dbhNumChannels = 2;
    gHeader.dbhSampleSize = 16;
    gHeader.dbhCompressionID = 0;
    gHeader.dbhPacketSize = 0;
    gHeader.dbhSampleRate = rate44khz;
    gHeader.dbhBufferPtr[0] = gBufA;
    gHeader.dbhBufferPtr[1] = gBufB;
    gHeader.dbhDoubleBack = gDoubleBackUPP;

    FillBuffer(gBufA);
    FillBuffer(gBufB);

    err = SndNewChannel(&gChan, sampledSynth, initStereo, NULL);
    if (err != noErr) return;
    SndPlayDoubleBuffer(gChan, &gHeader);
}

static void ShutdownAudio(void) {
    if (gChan != NULL) { SndDisposeChannel(gChan, true); gChan = NULL; }
    if (gBufA != NULL) { DisposePtr((Ptr)gBufA); gBufA = NULL; }
    if (gBufB != NULL) { DisposePtr((Ptr)gBufB); gBufB = NULL; }
    if (gDoubleBackUPP != NULL) { DisposeSndDoubleBackUPP(gDoubleBackUPP); gDoubleBackUPP = NULL; }
}

static void HandleDeckButtons(ControlHandle ctrl) {
    int i;
    for (i = 0; i < G3_DECK_COUNT; ++i) {
        if (ctrl == gTabBtn[i]) {
            SwitchToDeck(i);
            return;
        }
    }
    if (ctrl == gMasterDefBtn) {
        ResetMasterDefault();
        SyncVisibleDeckAndMaster();
        DrawSliderValues();
        UpdateStatusAreas();
        return;
    }
    if (ctrl == gResetBtn) {
        ResetAllSlidersToDefaults();
        DrawSliderValues();
        UpdateStatusAreas();
        return;
    }
    if (ctrl == gVolDefBtn) {
        ResetDeckVolDefault();
        SyncVisibleDeckAndMaster();
        DrawSliderValues();
        UpdateStatusAreas();
        return;
    }
    if (ctrl == gPanDefBtn) {
        ResetDeckPanDefault();
        SyncVisibleDeckAndMaster();
        DrawSliderValues();
        UpdateStatusAreas();
        return;
    }
    if (ctrl == gPitchDefBtn) {
        ResetDeckPitchDefault();
        SyncVisibleDeckAndMaster();
        DrawSliderValues();
        UpdateStatusAreas();
        return;
    }
    if (ctrl == gRampDefBtn) {
        ResetDeckRampDefault();
        SyncVisibleDeckAndMaster();
        DrawSliderValues();
        UpdateStatusAreas();
        return;
    }
    if (ctrl == gLoadBtn) {
        PickAndLoadDeckFile(gUiDeck);
        DrawSliderValues();
        UpdateStatusAreas();
        MarkWaveformDirty();
        DrawWaveformBody();
        return;
    }
    if (ctrl == gPlayBtn) {
        if (gPlayer.decks[gUiDeck].state == G3_TRANSPORT_PLAYING) {
            g3_player_pause(&gPlayer, gUiDeck);
            snprintf(gLastMessage, sizeof(gLastMessage), "Deck %d pause.", gUiDeck + 1);
        } else {
            g3_player_play(&gPlayer, gUiDeck);
            snprintf(gLastMessage, sizeof(gLastMessage), "Deck %d play.", gUiDeck + 1);
        }
        gPrevDeckStat[0] = '\0';
        RedrawVisibleWaveform();
        UpdateStatusAreas();
        return;
    }
    if (ctrl == gFreezeBtn) {
        int fr = g3_player_toggle_freeze(&gPlayer, gUiDeck);
        if (fr < 0)
            snprintf(gLastMessage, sizeof(gLastMessage), "Deck %d freeze failed (memory?).", gUiDeck + 1);
        else if (fr > 0)
            snprintf(gLastMessage, sizeof(gLastMessage), "Deck %d freeze @ %lu ms.", gUiDeck + 1,
                     (unsigned long)((1000.0 * gPlayer.decks[gUiDeck].frame_pos) / (double)G3_SAMPLE_RATE));
        else if (fr == 2)
            snprintf(gLastMessage, sizeof(gLastMessage), "Deck %d still unfading — wait a moment.", gUiDeck + 1);
        else
            snprintf(gLastMessage, sizeof(gLastMessage), "Deck %d unfrozen, playing.", gUiDeck + 1);
        gPrevDeckStat[0] = '\0';
        RedrawVisibleWaveform();
        UpdateStatusAreas();
        return;
    }
    if (ctrl == gStopBtn) {
        g3_player_stop(&gPlayer, gUiDeck);
        snprintf(gLastMessage, sizeof(gLastMessage), "Deck %d stop.", gUiDeck + 1);
        RedrawVisibleWaveform();
        UpdateStatusAreas();
        return;
    }
}

static void DoMouseDown(EventRecord* event) {
    WindowPtr whichWindow;
    short part = FindWindow(event->where, &whichWindow);
    switch (part) {
        case inSysWindow:
            SystemClick(event, whichWindow);
            break;
        case inDrag: {
            Rect dragRect;
            SetRect(&dragRect, 4, 24, qd.screenBits.bounds.right - 4, qd.screenBits.bounds.bottom - 4);
            DragWindow(whichWindow, event->where, &dragRect);
            break;
        }
        case inGoAway:
            if (TrackGoAway(whichWindow, event->where)) gRunning = 0;
            break;
        case inContent:
            if (whichWindow != FrontWindow()) {
                SelectWindow(whichWindow);
            } else if (whichWindow == gWindow) {
                Point localPt = event->where;
                ControlHandle ctrl = NULL;
                short ctlPart;
                SetPort(whichWindow);
                GlobalToLocal(&localPt);
                if (WaveHit(localPt)) {
                    G3DeckStatus st;
                    Rect r;
                    int w;
                    int rel;
                    double frac;
                    unsigned long ms;
                    int deckHit = gUiDeck;
                    if (!gPlayer.decks[deckHit].loaded) {
                        snprintf(gLastMessage, sizeof(gLastMessage), "Deck %d: load a WAV first.", deckHit + 1);
                        UpdateStatusAreas();
                    } else {
                        g3_player_get_deck_status(&gPlayer, deckHit, &st);
                        r = gWaveRect;
                        w = r.right - r.left - 2;
                        rel = localPt.h - (r.left + 1);
                        if (rel < 0) rel = 0;
                        if (rel > w) rel = w;
                        frac = (w <= 0) ? 0.0 : ((double)rel / (double)w);
                        ms = (st.duration_ms > 0) ? (unsigned long)(frac * (double)st.duration_ms) : 0UL;
                        g3_player_seek_ms(&gPlayer, deckHit, ms);
                        snprintf(gLastMessage, sizeof(gLastMessage), "Deck %d seek to %lu ms.", deckHit + 1, ms);
                        if (deckHit == gUiDeck) RedrawVisibleWaveform();
                        UpdateStatusAreas();
                    }
                    break;
                }
                ctlPart = FindControl(localPt, whichWindow, &ctrl);
                if (ctrl != NULL && ctlPart != 0) {
                    if (IsSliderControl(ctrl)) {
                        short before = GetControlValue(ctrl);
                        TrackControl(ctrl, localPt, gSliderActionUPP);
                        if (GetControlValue(ctrl) == before &&
                            (ctlPart == inUpButton || ctlPart == inDownButton || ctlPart == inPageUp || ctlPart == inPageDown)) {
                            MaybeNudgeScrollbar(ctrl, ctlPart);
                        }
                        SyncVisibleDeckAndMaster();
                        DrawSliderValues();
                    } else {
                        ControlPartCode donePart;
                        donePart = TrackControl(ctrl, localPt, (ControlActionUPP)0L);
                        if (ctrl == gQuitBtn && donePart == inButton) gRunning = 0;
                        else if (donePart == inButton) HandleDeckButtons(ctrl);
                    }
                }
            }
            break;
    }
}

static void InvalidateStaticTextCache(void) {
    gPrevDeckStat[0] = '\0';
    gPrevMasterStat[0] = '\0';
    gPrevMsgInit = 0;
    InvalidateSliderValueCache();
    MarkWaveformDirty();
}

static void DoUpdate(EventRecord* event) {
    WindowPtr w = (WindowPtr)event->message;
    BeginUpdate(w);
    SetPort(w);
    EraseRect(&w->portRect);
    InvalidateStaticTextCache();
    DrawControls(w);
    DrawSliderCaptions();
    MoveTo(LBL_X, MSG_BASELINE);
    DrawString("\pMessage:");
    DrawSliderValues();
    DrawWaveformBody();
    UpdateStatusAreas();
    EndUpdate(w);
}

static void InitUI(void) {
    Rect wr;
    short winLeft;
    short winTop;
    winLeft = (short)((TARGET_SCREEN_W - WINDOW_W) / 2);
    winTop = (short)((TARGET_SCREEN_H - WINDOW_H) / 2);
    if (winTop < 20) winTop = 20;
    SetRect(&wr, winLeft, winTop, (short)(winLeft + WINDOW_W), (short)(winTop + WINDOW_H));
    gWindow = NewWindow(NULL, &wr, "\pG3 Stage Player v32", true, zoomDocProc, (WindowPtr)-1, true, 0);
    SetPort(gWindow);
    EraseRect(&gWindow->portRect);
    LayoutUI();
    LoadDeckSlidersFromEngine(gUiDeck);
    gSliderActionUPP = NewControlActionProc(SliderAction);
    DrawControls(gWindow);
    DrawSliderCaptions();
    MoveTo(LBL_X, MSG_BASELINE);
    DrawString("\pMessage:");
    DrawSliderValues();
    DrawWaveformBody();
    InvalidateStaticTextCache();
    UpdateStatusAreas();
}

static int IsSliderControl(ControlHandle ctrl) {
    if (ctrl == gMasterSlider) return 1;
    if (ctrl == gVolSlider || ctrl == gPanSlider || ctrl == gPitchSlider || ctrl == gRampSlider) return 1;
    return 0;
}

int main(void) {
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

    g3_player_init(&gPlayer);
    InitUI();
    InitAudio();

    while (gRunning) {
        int di;
        SyncVisibleDeckAndMaster();
        for (di = 0; di < G3_DECK_COUNT; ++di)
            g3_wav_pump_deck(&gPlayer.decks[di]);
        if (WaitNextEvent(everyEvent, &event, 2, NULL)) {
            switch (event.what) {
                case mouseDown: DoMouseDown(&event); break;
                case updateEvt: DoUpdate(&event); break;
                default: break;
            }
        }
        gUiTickCounter++;
        if (gWaveRebuildDeck >= 0) AdvanceWaveformRebuild(G3_WAVE_REBUILD_CHUNK);
        for (di = 0; di < G3_DECK_COUNT; ++di)
            g3_wav_pump_deck(&gPlayer.decks[di]);
        if (gWaveformBodyDirty)
            DrawWaveformBody();
        else if (AnyDeckPlaying() && (gUiTickCounter % 2) == 0)
            UpdateWaveformPlayhead();
        if ((gUiTickCounter % 8) == 0) UpdateStatusAreas();
    }

    ShutdownAudio();
    g3_player_shutdown(&gPlayer);
    return 0;
}
