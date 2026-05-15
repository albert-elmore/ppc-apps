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

typedef enum G3TransportState {
    G3_TRANSPORT_STOPPED = 0,
    G3_TRANSPORT_PLAYING = 1,
    G3_TRANSPORT_PAUSED = 2
} G3TransportState;

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

typedef struct G3WavData {
    short* samples;
    unsigned long frame_count;
    int channels;
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
void g3_player_seek_ms(G3Player* player, int deck_index, unsigned long position_ms);
void g3_player_set_volume(G3Player* player, int deck_index, float value);
void g3_player_set_pan(G3Player* player, int deck_index, float value);
void g3_player_set_pitch_percent(G3Player* player, int deck_index, float value);
void g3_player_set_ramp_seconds(G3Player* player, int deck_index, float value);
void g3_player_set_master_volume(G3Player* player, float value);
void g3_player_get_deck_status(const G3Player* player, int deck_index, G3DeckStatus* out_status);
void g3_player_get_master_status(const G3Player* player, G3MasterStatus* out_status);
int g3_player_render(G3Player* player, short* interleaved_stereo_out, int frame_count);

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

static void g3_deck_reset(G3Deck* deck) {
    memset(deck, 0, sizeof(*deck));
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
    player->master_volume = g3_db_to_linear(-4.0f);
    for (i = 0; i < G3_DECK_COUNT; ++i) g3_deck_reset(&player->decks[i]);
}

void g3_player_shutdown(G3Player* player) {
    int i;
    for (i = 0; i < G3_DECK_COUNT; ++i) {
        free(player->decks[i].wav.samples);
        player->decks[i].wav.samples = NULL;
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
    deck->wav.samples = pcm;
    deck->wav.channels = (int)fmt_channels;
    deck->wav.frame_count = pcm_bytes / (unsigned long)(fmt_channels * 2);
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
    short* pcm = NULL;
    unsigned long pcm_bytes = 0;
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
            pcm = (short*)malloc((size_t)pcm_bytes);
            if (!pcm) { fclose(f); return 0; }
            if (fread(pcm, 1, (size_t)pcm_bytes, f) != (size_t)pcm_bytes) {
                free(pcm);
                fclose(f);
                return 0;
            }
            got_data = 1;
        } else {
            fseek(f, (long)chunk_size, SEEK_CUR);
        }

        if (chunk_size & 1u) fseek(f, 1L, SEEK_CUR);
    }
    fclose(f);

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
    deck->wav.samples = pcm;
    deck->wav.channels = (int)fmt_channels;
    deck->wav.frame_count = pcm_bytes / (unsigned long)(fmt_channels * 2);
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
    free(deck->wav.samples);
    g3_deck_reset(deck);
}

void g3_player_play(G3Player* player, int deck_index) {
    G3Deck* deck;
    if (!g3_clamp_deck_index(deck_index)) return;
    deck = &player->decks[deck_index];
    if (!deck->loaded) return;
    deck->state = G3_TRANSPORT_PLAYING;
    deck->speed_target = 1.0 + ((double)deck->pitch_percent / 100.0);
}

void g3_player_pause(G3Player* player, int deck_index) {
    G3Deck* deck;
    if (!g3_clamp_deck_index(deck_index)) return;
    deck = &player->decks[deck_index];
    if (!deck->loaded) return;
    deck->speed_target = 0.0;
    deck->state = G3_TRANSPORT_PAUSED;
}

void g3_player_stop(G3Player* player, int deck_index) {
    G3Deck* deck;
    if (!g3_clamp_deck_index(deck_index)) return;
    deck = &player->decks[deck_index];
    if (!deck->loaded) return;
    deck->speed_target = 0.0;
    deck->state = G3_TRANSPORT_STOPPED;
}

void g3_player_seek_ms(G3Player* player, int deck_index, unsigned long position_ms) {
    G3Deck* deck;
    double frame_pos;
    if (!g3_clamp_deck_index(deck_index)) return;
    deck = &player->decks[deck_index];
    if (!deck->loaded) return;
    frame_pos = ((double)position_ms / 1000.0) * (double)G3_SAMPLE_RATE;
    if (frame_pos < 0.0) frame_pos = 0.0;
    if (frame_pos >= (double)deck->wav.frame_count) frame_pos = (double)(deck->wav.frame_count ? deck->wav.frame_count - 1 : 0);
    deck->frame_pos = frame_pos;
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
    if (deck->state == G3_TRANSPORT_PLAYING) {
        deck->speed_target = 1.0 + ((double)deck->pitch_percent / 100.0);
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

int g3_player_render(G3Player* player, short* out_stereo, int frame_count) {
    int i;
    int d;
    float deck_peak[G3_DECK_COUNT];

    if (!out_stereo || frame_count <= 0) return 0;
    for (d = 0; d < G3_DECK_COUNT; ++d) deck_peak[d] = 0.0f;
    player->master_peak_linear = 0.0f;

    for (i = 0; i < frame_count; ++i) {
        double mix_l = 0.0;
        double mix_r = 0.0;

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

            if (deck->wav.channels == 1) {
                short s0 = deck->wav.samples[frame_i];
                short s1 = deck->wav.samples[frame_n];
                mono_or_l = ((double)s0 + ((double)(s1 - s0) * frac)) / 32768.0;
                right = mono_or_l;
            } else {
                short l0 = deck->wav.samples[(frame_i * 2) + 0];
                short r0 = deck->wav.samples[(frame_i * 2) + 1];
                short l1 = deck->wav.samples[(frame_n * 2) + 0];
                short r1 = deck->wav.samples[(frame_n * 2) + 1];
                mono_or_l = ((double)l0 + ((double)(l1 - l0) * frac)) / 32768.0;
                right = ((double)r0 + ((double)(r1 - r0) * frac)) / 32768.0;
            }

            {
                double p = (double)((deck->pan_now + 1.0f) * 0.5f);
                left_gain = cos((M_PI * 0.5) * p);
                right_gain = sin((M_PI * 0.5) * p);
            }

            sample_l = mono_or_l * left_gain * (double)deck->volume_now;
            sample_r = right * right_gain * (double)deck->volume_now;
            mix_l += sample_l;
            mix_r += sample_r;

            abs_local = (float)(fabs(sample_l) > fabs(sample_r) ? fabs(sample_l) : fabs(sample_r));
            if (abs_local > deck_peak[d]) deck_peak[d] = abs_local;

            deck->frame_pos += deck->speed_current;
        }

        mix_l *= player->master_volume;
        mix_r *= player->master_volume;

        if (mix_l > 1.0) mix_l = 1.0;
        if (mix_l < -1.0) mix_l = -1.0;
        if (mix_r > 1.0) mix_r = 1.0;
        if (mix_r < -1.0) mix_r = -1.0;

        out_stereo[(i * 2) + 0] = (short)(mix_l * 32767.0);
        out_stereo[(i * 2) + 1] = (short)(mix_r * 32767.0);

        {
            float abs_master = (float)(fabs(mix_l) > fabs(mix_r) ? fabs(mix_l) : fabs(mix_r));
            if (abs_master > player->master_peak_linear) player->master_peak_linear = abs_master;
        }
    }

    for (d = 0; d < G3_DECK_COUNT; ++d) player->decks[d].peak_linear = deck_peak[d];
    return frame_count;
}

/* ---------- Mac UI / audio I/O ---------- */

#define FRAMES_PER_BUFFER 1024
#define WINDOW_W 800
#define WINDOW_H 656
#define WAVE_RIGHT (WINDOW_W - 20)
#define WAVE_POINTS 320
#define DEFAULT_DECK_VOL 0.8f
#define MASTER_DB_MIN (-60.0f)
#define MASTER_DB_MAX (24.0f)
#define MASTER_DB_DEFAULT (-4.0f)
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
#define MASTER_STAT_T (short)(MASTER_BAND_TOP + 34)
#define MASTER_STAT_B (short)(MASTER_BAND_TOP + 60)
#define MSG_RECT_T (short)(MASTER_BAND_TOP + 72)
#define MSG_RECT_B (short)(MASTER_BAND_TOP + 108)
#define MSG_LABEL_Y (short)(MASTER_BAND_TOP + 76)
#define MSG_TEXT_LEFT 86
#define BTN_CORNER_TOP (short)(MSG_RECT_T + 6)
#define BTN_CORNER_BOT (short)(MSG_RECT_T + 26)
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
static char gLastMessage[192] = "Ready.";
static long gUiTickCounter = 0;
static char gPrevDeckStat[256];
static char gPrevMasterStat[256];
static char gPrevMsgDrawn[192];
static int gPrevMsgInit = 0;
static int IsSliderControl(ControlHandle ctrl);
static int AnyDeckPlaying(void);
static int WaveHit(Point localPt);
static void DrawSliderCaptions(void);
static void DrawSliderValues(void);
static void DrawWaveforms(void);
static void UpdateStatusAreas(void);
static void ResetAllSlidersToDefaults(void);
static void SyncVisibleDeckAndMaster(void);
static void ApplyVisibleDeckSlidersToEngine(void);
static void LoadDeckSlidersFromEngine(int deckIndex);
static void SwitchToDeck(int deckIndex);
static short SliderMaxForControl(ControlHandle c);
static void MaybeNudgeScrollbar(ControlHandle c, ControlPartCode initialPart);
static void RefreshPlayPauseButton(void);
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

static int AnyDeckPlaying(void) {
    int i;
    for (i = 0; i < G3_DECK_COUNT; ++i) {
        if (gPlayer.decks[i].state == G3_TRANSPORT_PLAYING) return 1;
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
    DrawSliderValues();
    DrawWaveforms();
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

static void RebuildWaveform(int deck_index) {
    G3Deck* deck;
    long i;
    if (deck_index < 0 || deck_index >= G3_DECK_COUNT) return;
    deck = &gPlayer.decks[deck_index];

    for (i = 0; i < WAVE_POINTS; ++i) {
        gWaveMin[deck_index][i] = 0;
        gWaveMax[deck_index][i] = 0;
    }
    if (!deck->loaded || deck->wav.frame_count == 0 || deck->wav.samples == NULL) return;

    for (i = 0; i < WAVE_POINTS; ++i) {
        unsigned long start = (unsigned long)(((double)i * (double)deck->wav.frame_count) / (double)WAVE_POINTS);
        unsigned long end = (unsigned long)(((double)(i + 1) * (double)deck->wav.frame_count) / (double)WAVE_POINTS);
        long mn = 32767;
        long mx = -32768;
        unsigned long f;
        if (end <= start) end = start + 1;
        if (end > deck->wav.frame_count) end = deck->wav.frame_count;
        for (f = start; f < end; ++f) {
            long s;
            if (deck->wav.channels == 1) {
                s = deck->wav.samples[f];
            } else {
                long l = deck->wav.samples[(f * 2) + 0];
                long r = deck->wav.samples[(f * 2) + 1];
                s = (l + r) / 2;
            }
            if (s < mn) mn = s;
            if (s > mx) mx = s;
        }
        if (mn > mx) {
            mn = 0;
            mx = 0;
        }
        gWaveMin[deck_index][i] = (short)mn;
        gWaveMax[deck_index][i] = (short)mx;
    }
}

static void DrawWaveforms(void) {
    int d = gUiDeck;
    G3DeckStatus st;
    Rect r = gWaveRect;
    int i;
    int w;
    int mid;
    long play_x;
    SetPort(gWindow);
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

    if (st.loaded && st.duration_ms > 0) {
        play_x = (long)((((double)st.position_ms / (double)st.duration_ms) * (double)w));
        if (play_x < 0) play_x = 0;
        if (play_x > w) play_x = w;
        MoveTo(r.left + 1 + (short)play_x, r.top + 1);
        LineTo(r.left + 1 + (short)play_x, r.bottom - 1);
    }
}

static int LoadDeckFromFSSpec(int deck_index, const FSSpec* spec) {
    short refNum;
    long readCount;
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
    short* pcm = NULL;
    unsigned long pcmBytes = 0;
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
            pcmBytes = chunkSize;
            pcm = (short*)malloc((size_t)pcmBytes);
            if (!pcm) {
                FSClose(refNum);
                snprintf(gLastMessage, sizeof(gLastMessage), "Deck %d load failed: out of memory.", deck_index + 1);
                return 0;
            }
            readCount = (long)pcmBytes;
            err = FSRead(refNum, &readCount, pcm);
            if (err != noErr || (unsigned long)readCount != pcmBytes) {
                free(pcm);
                FSClose(refNum);
                snprintf(gLastMessage, sizeof(gLastMessage), "Deck %d load failed: data read error %d.", deck_index + 1, (int)err);
                return 0;
            }
            gotData = 1;
        } else {
            SetFPos(refNum, fsFromMark, (long)chunkSize);
        }
        if (chunkSize & 1u) SetFPos(refNum, fsFromMark, 1L);
    }
    FSClose(refNum);

    if (!gotFmt || !gotData || fmtAudio != 1 || (fmtChannels != 1 && fmtChannels != 2) || fmtBits != 16 || fmtRate != 44100UL) {
        free(pcm);
        snprintf(gLastMessage, sizeof(gLastMessage), "Deck %d load failed: needs PCM16 44.1k mono/stereo.", deck_index + 1);
        return 0;
    }

    g3_pcm16_le_to_host(pcm, pcmBytes);

    g3_player_unload(&gPlayer, deck_index);
    deck->wav.samples = pcm;
    deck->wav.channels = (int)fmtChannels;
    deck->wav.frame_count = pcmBytes / (unsigned long)(fmtChannels * 2);
    deck->loaded = 1;
    deck->state = G3_TRANSPORT_STOPPED;
    deck->frame_pos = 0.0;
    deck->speed_current = 0.0;
    deck->speed_target = 0.0;
    deck->peak_linear = 0.0f;

    snprintf(gLastMessage, sizeof(gLastMessage), "Deck %d loaded OK.", deck_index + 1);
    RebuildWaveform(deck_index);
    return 1;
}

static int PickAndLoadDeckFile(int deck_index) {
    StandardFileReply reply;
    if (deck_index < 0 || deck_index >= G3_DECK_COUNT) return 0;

    StandardGetFile(NULL, -1, NULL, &reply);
    if (!reply.sfGood) return 0;
    return LoadDeckFromFSSpec(deck_index, &reply.sfFile);
}

static void DrawSliderValues(void) {
    char text[64];
    SetPort(gWindow);

    EraseRect(&gSliderValueRect[0]);
    snprintf(text, sizeof(text), "%+.1f dB", (double)DeckVolLinearToDb(SliderToVol(GetControlValue(gVolSlider))));
    MoveTo(gSliderValueRect[0].left, (short)(SL0_TOP + 0 * ROW_STEP + SL_TRACK_TEXT_BASE));
    DrawCString(text);

    EraseRect(&gSliderValueRect[1]);
    snprintf(text, sizeof(text), "%.2f", SliderToPan(GetControlValue(gPanSlider)));
    MoveTo(gSliderValueRect[1].left, (short)(SL0_TOP + 1 * ROW_STEP + SL_TRACK_TEXT_BASE));
    DrawCString(text);

    EraseRect(&gSliderValueRect[2]);
    snprintf(text, sizeof(text), "%.1f%%", SliderToPitch(GetControlValue(gPitchSlider)));
    MoveTo(gSliderValueRect[2].left, (short)(SL0_TOP + 2 * ROW_STEP + SL_TRACK_TEXT_BASE));
    DrawCString(text);

    EraseRect(&gSliderValueRect[3]);
    snprintf(text, sizeof(text), "%d ms", (int)GetControlValue(gRampSlider));
    MoveTo(gSliderValueRect[3].left, (short)(SL0_TOP + 3 * ROW_STEP + SL_TRACK_TEXT_BASE));
    DrawCString(text);

    EraseRect(&gSliderValueRect[4]);
    snprintf(text, sizeof(text), "%+.1f dB", (double)SliderToMasterDb(GetControlValue(gMasterSlider)));
    MoveTo(gSliderValueRect[4].left, MASTER_LBL_BASELINE);
    DrawCString(text);
}

static void UpdateStatusAreas(void) {
    int i;
    char line[256];
    char mini[128];
    G3DeckStatus d;
    char pos[16];
    char dur[16];
    SetPort(gWindow);

    g3_player_get_deck_status(&gPlayer, gUiDeck, &d);
    FormatClock(d.position_ms, pos, sizeof(pos));
    FormatClock(d.duration_ms, dur, sizeof(dur));
    snprintf(line, sizeof(line),
             "Deck %d  %s  %s / %s  Peak %.0f dBFS",
             gUiDeck + 1,
             d.state == G3_TRANSPORT_PLAYING ? "PLAY" : (d.state == G3_TRANSPORT_PAUSED ? "PAUSE" : "STOP"),
             pos, dur, (double)d.peak_dbfs);
    if (strcmp(line, gPrevDeckStat) != 0) {
        EraseRect(&gDeckStatusRect);
        FrameRect(&gDeckStatusRect);
        MoveTo(gDeckStatusRect.left + 6, gDeckStatusRect.bottom - 6);
        DrawCString(line);
        strncpy(gPrevDeckStat, line, sizeof(gPrevDeckStat) - 1);
        gPrevDeckStat[sizeof(gPrevDeckStat) - 1] = '\0';
    }

    {
        size_t lm = 0;
        mini[0] = '\0';
        for (i = 0; i < G3_DECK_COUNT; ++i) {
            g3_player_get_deck_status(&gPlayer, i, &d);
            lm += (size_t)snprintf(mini + lm, sizeof(mini) - lm, "%sD%d:%s",
                                   i > 0 ? " " : "",
                                   i + 1,
                                   d.state == G3_TRANSPORT_PLAYING ? "P" : (d.state == G3_TRANSPORT_PAUSED ? "U" : "S"));
        }
    }

    {
        G3MasterStatus m;
        g3_player_get_master_status(&gPlayer, &m);
        snprintf(line, sizeof(line), "MASTER Peak %.0f dBFS  |  %s", (double)m.peak_dbfs, mini);
        if (strcmp(line, gPrevMasterStat) != 0) {
            EraseRect(&gMasterStatusRect);
            FrameRect(&gMasterStatusRect);
            MoveTo(gMasterStatusRect.left + 6, gMasterStatusRect.bottom - 6);
            DrawCString(line);
            strncpy(gPrevMasterStat, line, sizeof(gPrevMasterStat) - 1);
            gPrevMasterStat[sizeof(gPrevMasterStat) - 1] = '\0';
        }
    }

    if (!gPrevMsgInit || strcmp(gLastMessage, gPrevMsgDrawn) != 0) {
        EraseRect(&gMessageRect);
        MoveTo(gMessageRect.left, gMessageRect.bottom - 2);
        DrawCString(gLastMessage);
        strncpy(gPrevMsgDrawn, gLastMessage, sizeof(gPrevMsgDrawn) - 1);
        gPrevMsgDrawn[sizeof(gPrevMsgDrawn) - 1] = '\0';
        gPrevMsgInit = 1;
    }

    RefreshPlayPauseButton();
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
    SetRect(&r, left + 62, top, left + 146, top + TR_H);
    gPlayBtn = NewControl(gWindow, &r, "\pPlay", true, 0, 0, 1, pushButProc, 0);
    SetRect(&r, left + 152, top, left + 216, top + TR_H);
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
    MoveTo(20, MSG_LABEL_Y);
    DrawString("\pMessage:");
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
        DrawWaveforms();
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
        UpdateStatusAreas();
        return;
    }
    if (ctrl == gStopBtn) {
        g3_player_stop(&gPlayer, gUiDeck);
        snprintf(gLastMessage, sizeof(gLastMessage), "Deck %d stop.", gUiDeck + 1);
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
                        DrawWaveforms();
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
}

static void DoUpdate(EventRecord* event) {
    WindowPtr w = (WindowPtr)event->message;
    BeginUpdate(w);
    SetPort(w);
    EraseRect(&w->portRect);
    InvalidateStaticTextCache();
    DrawControls(w);
    DrawSliderCaptions();
    MoveTo(20, MSG_LABEL_Y);
    DrawString("\pMessage:");
    DrawSliderValues();
    DrawWaveforms();
    UpdateStatusAreas();
    EndUpdate(w);
}

static void InitUI(void) {
    Rect wr;
    SetRect(&wr, 40, 40, 40 + WINDOW_W, 40 + WINDOW_H);
    gWindow = NewWindow(NULL, &wr, "\pG3 Stage Player v14", true, zoomDocProc, (WindowPtr)-1, true, 0);
    SetPort(gWindow);
    EraseRect(&gWindow->portRect);
    LayoutUI();
    LoadDeckSlidersFromEngine(gUiDeck);
    gSliderActionUPP = NewControlActionProc(SliderAction);
    DrawControls(gWindow);
    DrawSliderCaptions();
    MoveTo(20, MSG_LABEL_Y);
    DrawString("\pMessage:");
    DrawSliderValues();
    DrawWaveforms();
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
        SyncVisibleDeckAndMaster();
        if (WaitNextEvent(everyEvent, &event, 2, NULL)) {
            switch (event.what) {
                case mouseDown: DoMouseDown(&event); break;
                case updateEvt: DoUpdate(&event); break;
                default: break;
            }
        }
        gUiTickCounter++;
        if ((gUiTickCounter % 20) == 0 && AnyDeckPlaying()) DrawWaveforms();
        if ((gUiTickCounter % 8) == 0) UpdateStatusAreas();
    }

    ShutdownAudio();
    g3_player_shutdown(&gPlayer);
    return 0;
}
