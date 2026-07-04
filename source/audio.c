#include "audio.h"
#include "log.h"

#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <string.h>
#include <stdlib.h>

/* ── configuration ─────────────────────────────────────────── */
#define NUM_WBUFS   64              /* ~1.4 s of queue at 48 kHz */
#define WBUF_BYTES  (2048 * 2 * 2)  /* up to 2048 samples, stereo, s16 */

/* ── state ─────────────────────────────────────────────────── */
static struct {
    bool ok;                 /* ndsp + decoder ready */
    AVCodecContext *ctx;
    AVPacket *pkt;
    AVFrame *frame;
    ndspWaveBuf wbuf[NUM_WBUFS];
    u8 *wmem[NUM_WBUFS];     /* linearAlloc'd PCM storage */
    int widx;
    int cur_rate;
    u32 fed, played, dropped;
} A;

bool audio_init(void)
{
    memset(&A, 0, sizeof(A));

    Result r = ndspInit();
    if (R_FAILED(r)) {
        LOG("audio: ndspInit FAILED 0x%lx (dspfirm.cdc missing?)", r);
        return false;
    }

    const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_AAC);
    if (!codec) { LOG("audio: no AAC decoder"); ndspExit(); return false; }

    A.ctx = avcodec_alloc_context3(codec);
    if (!A.ctx) { ndspExit(); return false; }
    A.ctx->thread_count = 1;

    if (avcodec_open2(A.ctx, codec, NULL) < 0) {
        LOG("audio: avcodec_open2 FAILED");
        avcodec_free_context(&A.ctx);
        ndspExit();
        return false;
    }

    A.pkt = av_packet_alloc();
    A.frame = av_frame_alloc();

    for (int i = 0; i < NUM_WBUFS; i++) {
        A.wmem[i] = (u8*)linearAlloc(WBUF_BYTES);
        if (!A.wmem[i]) { LOG("audio: linearAlloc FAILED"); audio_exit(); return false; }
    }

    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    ndspChnReset(0);
    ndspChnSetInterp(0, NDSP_INTERP_LINEAR);
    ndspChnSetRate(0, 48000.0f);
    ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);
    A.cur_rate = 48000;

    A.ok = true;
    LOG("audio: init OK (AAC via avcodec %d)", LIBAVCODEC_VERSION_MAJOR);
    return true;
}

void audio_exit(void)
{
    if (A.ctx) avcodec_free_context(&A.ctx);
    if (A.pkt) av_packet_free(&A.pkt);
    if (A.frame) av_frame_free(&A.frame);
    for (int i = 0; i < NUM_WBUFS; i++) {
        if (A.wmem[i]) { linearFree(A.wmem[i]); A.wmem[i] = NULL; }
    }
    if (A.ok) { ndspChnReset(0); ndspExit(); }
    A.ok = false;
}

void audio_reset(void)
{
    if (!A.ok) return;
    ndspChnReset(0);
    ndspChnSetInterp(0, NDSP_INTERP_LINEAR);
    ndspChnSetRate(0, (float)A.cur_rate);
    ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);
    avcodec_flush_buffers(A.ctx);
    memset(A.wbuf, 0, sizeof(A.wbuf));
    A.widx = 0;
}

bool audio_available(void) { return A.ok; }

static inline s16 f2s16(float v)
{
    if (v > 1.0f) v = 1.0f;
    if (v < -1.0f) v = -1.0f;
    return (s16)(v * 32767.0f);
}

static void queue_frame(AVFrame *f)
{
    int n = f->nb_samples;
    if (n <= 0 || n * 4 > WBUF_BYTES) return;

    ndspWaveBuf *b = &A.wbuf[A.widx];
    if (b->status == NDSP_WBUF_QUEUED || b->status == NDSP_WBUF_PLAYING) {
        A.dropped++;
        if ((A.dropped % 256) == 1)
            LOG("audio: ring full, dropped=%lu", (unsigned long)A.dropped);
        return;
    }

    if (f->sample_rate > 0 && f->sample_rate != A.cur_rate) {
        A.cur_rate = f->sample_rate;
        ndspChnSetRate(0, (float)A.cur_rate);
        LOG("audio: rate=%d ch=%d", A.cur_rate, f->ch_layout.nb_channels);
    }

    s16 *out = (s16*)A.wmem[A.widx];
    int ch = f->ch_layout.nb_channels;

    if (f->format == AV_SAMPLE_FMT_FLTP) {
        const float *L = (const float*)f->data[0];
        const float *R = (ch > 1 && f->data[1]) ? (const float*)f->data[1] : L;
        for (int i = 0; i < n; i++) {
            out[i*2]   = f2s16(L[i]);
            out[i*2+1] = f2s16(R[i]);
        }
    } else if (f->format == AV_SAMPLE_FMT_S16P) {
        const s16 *L = (const s16*)f->data[0];
        const s16 *R = (ch > 1 && f->data[1]) ? (const s16*)f->data[1] : L;
        for (int i = 0; i < n; i++) { out[i*2] = L[i]; out[i*2+1] = R[i]; }
    } else if (f->format == AV_SAMPLE_FMT_S16) {
        const s16 *S = (const s16*)f->data[0];
        if (ch >= 2) {
            memcpy(out, S, n * 4);
        } else {
            for (int i = 0; i < n; i++) { out[i*2] = S[i]; out[i*2+1] = S[i]; }
        }
    } else {
        return; /* unsupported sample format */
    }

    memset(b, 0, sizeof(*b));
    b->data_vaddr = A.wmem[A.widx];
    b->nsamples = n;
    DSP_FlushDataCache(A.wmem[A.widx], n * 4);
    ndspChnWaveBufAdd(0, b);

    A.widx = (A.widx + 1) % NUM_WBUFS;
    A.played++;
    if (A.played == 1)
        LOG("audio: first PCM frame queued (%d samples @%d Hz)", n, A.cur_rate);
}

static void decode_adts_frame(const u8 *data, int len)
{
    if (av_new_packet(A.pkt, len) < 0) return;
    memcpy(A.pkt->data, data, len);

    int rc = avcodec_send_packet(A.ctx, A.pkt);
    av_packet_unref(A.pkt);
    if (rc < 0) {
        A.fed++;
        if ((A.fed % 512) == 1) LOG("audio: send_packet rc=%d", rc);
        return;
    }

    while (avcodec_receive_frame(A.ctx, A.frame) == 0)
        queue_frame(A.frame);
}

void audio_feed(const u8 *data, int len)
{
    if (!A.ok) return;

    /* Walk ADTS frames: 0xFFF sync, frame length in header bits 30-42 */
    while (len >= 7) {
        if (!(data[0] == 0xFF && (data[1] & 0xF0) == 0xF0)) {
            data++; len--;
            continue;
        }
        int flen = ((data[3] & 0x03) << 11) | (data[4] << 3) | (data[5] >> 5);
        if (flen < 7 || flen > len)
            break;
        decode_adts_frame(data, flen);
        data += flen;
        len -= flen;
    }
}
