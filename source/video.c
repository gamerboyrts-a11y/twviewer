#include "video.h"
#include "log.h"
#include "audio.h"
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <3ds/services/mvd.h>
#include <curl/curl.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <citro3d.h>


/* sizing */
#define OUT_W    400
#define OUT_H    240
#define TEX_W    1024
#define TEX_H    512
#define NAL_MAX  (256*1024)
#define PES_MAX  (512*1024)
#define APES_MAX (64*1024)
#define SEG_MAX  (4*1024*1024)


/* state */
static struct {
    bool   active, offline, has_frame, mvd_ok, tex_valid;
    Thread thread;               /* downloader */
    LightLock lock;
    u8    *outbuf;               /* MVD RGB565 output (linear, page aligned) */
    u8    *nalbuf;               /* NAL input (linear) */
    u8    *stgbuf;               /* restride staging (linear, TEX_W pitch) */
    C3D_Tex           tex;
    Tex3DS_SubTexture subtex;
    C2D_Image         img;
    MVDSTD_Config     cfg;
    int  vid_out_w, vid_out_h;   /* 16-aligned MVD buffer dims (stride) */
    char channel[48];
    char oauth[128];
    char client_id[48];
    char hls_url[4096];
    char last_seg[2048];
    char usher_resolve[64];
    /* stream metadata (guarded by lock) */
    char meta_title[128];
    char meta_game[64];
    int  meta_viewers;
    bool meta_dirty;
} V;


/* diagnostic counters — reset in video_start */
static int  s_frame_count = 0;
static int  s_upload_count = 0;
static int  s_draw_count = 0;
static bool s_got_real_frame = false;
static u64  s_pace_tick = 0;     /* 30fps pacing target (system ticks) */


/* bearer token helper — strips "PASS " / "oauth:" prefix */
static const char *bearer_token(void) {
    const char *t = V.oauth;
    if (strncmp(t, "PASS ", 5) == 0) t += 5;
    if (strncmp(t, "oauth:", 6) == 0) t += 6;
    return (t[0] && strcmp(t, "schmoopiie") != 0) ? t : NULL;
}


/* curl write callback */
typedef struct { char *d; size_t len, cap; } Buf;
static size_t cb(void *p, size_t s, size_t n, void *u) {
    Buf *b = u; size_t in = s*n;
    if (b->len+in+1 > b->cap) {
        char *t = realloc(b->d, b->cap+in+4096);
        if (!t) return 0;
        b->d = t; b->cap += in+4096;
    }
    memcpy(b->d+b->len, p, in); b->len += in; b->d[b->len] = 0;
    return in;
}


/* make relative URL absolute using a base URL */
static char *resolve_url(const char *url, const char *base_url) {
    if (!url || !url[0]) return NULL;
    if (strncmp(url, "http", 4) == 0) return strdup(url);
    char base[2048] = {0};
    if (base_url) {
        const char *last_slash = strrchr(base_url, '/');
        if (last_slash) {
            size_t l = (size_t)(last_slash - base_url) + 1;
            if (l >= sizeof(base)) l = sizeof(base)-1;
            strncpy(base, base_url, l);
        }
    }
    char *full = malloc(2048);
    if (!full) return NULL;
    snprintf(full, 2048, "%s%s", base, url);
    return full;
}


/* Generic HTTPS GET.
 * Persistent handle: the TLS session + TCP connection stay alive between
 * playlist polls (a fresh handshake costs 0.5-1s on 3DS and caused the
 * per-segment stutter).  Downloader thread only. */
static CURL *s_curl_pl = NULL;
static char *http_get(const char *url, const char *auth_hdr) {
    if (!s_curl_pl) s_curl_pl = curl_easy_init();
    CURL *c = s_curl_pl;
    if (!c) return NULL;
    Buf b = {malloc(4096), 0, 4096};
    if (!b.d) return NULL;
    b.d[0] = 0;

    curl_easy_reset(c);
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &b);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(c, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_PATH_AS_IS, 1L);
    curl_easy_setopt(c, CURLOPT_TCP_KEEPALIVE, 1L);

    struct curl_slist *resolve_list = NULL;
    if (V.usher_resolve[0] && strstr(url, "usher.ttvnw.net")) {
        resolve_list = curl_slist_append(NULL, V.usher_resolve);
        curl_easy_setopt(c, CURLOPT_RESOLVE, resolve_list);
    }

    struct curl_slist *headers = NULL;
    if (auth_hdr)
        headers = curl_slist_append(headers, auth_hdr);
    if (strstr(url, "usher.ttvnw.net")) {
        char cid_hdr[80];
        snprintf(cid_hdr, sizeof(cid_hdr), "Client-ID: %s", V.client_id);
        headers = curl_slist_append(headers, cid_hdr);
    }
    if (headers) curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers);

    CURLcode res = curl_easy_perform(c);
    if (headers) curl_slist_free_all(headers);

    long code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    if (resolve_list) curl_slist_free_all(resolve_list);

    if (code != 200) {
        LOG("http_get code=%ld curl=%d url=%.80s", code, (int)res, url);
        LOG("http_get FAILED body(%.120s)", b.d ? b.d : "(null)");
        free(b.d); return NULL;
    }
    return b.d;
}


/* Fetch GQL playback token and build HLS URL */
static bool fetch_hls_url(void) {
    char body[512];
    snprintf(body, sizeof(body),
        "[{\"operationName\":\"PlaybackAccessToken\","
        "\"query\":\"query PlaybackAccessToken($login:String!,$playerType:String!)"
        "{streamPlaybackAccessToken(channelName:$login,"
        "params:{platform:\\\"web\\\",playerBackend:\\\"mediaplayer\\\","
        "playerType:$playerType}){value signature}}\","
        "\"variables\":{\"login\":\"%s\",\"playerType\":\"site\"}}]",
        V.channel);

    CURL *c = curl_easy_init(); if (!c) return false;
    Buf buf = {malloc(8192), 0, 8192};
    if (!buf.d) { curl_easy_cleanup(c); return false; }
    buf.d[0] = 0;

    struct curl_slist *h = NULL;
    h = curl_slist_append(h, "Content-Type: application/json");
    char gql_cid_hdr[80];
    snprintf(gql_cid_hdr, sizeof(gql_cid_hdr), "Client-ID: %s", V.client_id);
    h = curl_slist_append(h, gql_cid_hdr);

    const char *tok = bearer_token();
    if (tok) {
        char ahdr[256];
        snprintf(ahdr, sizeof(ahdr), "Authorization: Bearer %s", tok);
        h = curl_slist_append(h, ahdr);
    }

    curl_easy_setopt(c, CURLOPT_URL, "https://gql.twitch.tv/gql");
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, h);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(c, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 15L);
    CURLcode gql_res = curl_easy_perform(c);
    long gql_code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &gql_code);
    curl_slist_free_all(h);
    curl_easy_cleanup(c);
    LOG("vid: GQL curl=%d http=%ld resp(%.200s)", (int)gql_res, gql_code,
        buf.d ? buf.d : "(null)");

    if (!buf.d || !buf.d[0]) { free(buf.d); return false; }

    char sig[128] = {0}, token_raw[2048] = {0};

    const char *ps = strstr(buf.d, "\"signature\"");
    if (ps) {
        ps += 11;
        while (*ps==':'||*ps==' ') ps++;
        if (*ps=='"') {
            ps++;
            const char *e = strchr(ps, '"');
            if (e) {
                size_t l = (size_t)(e-ps);
                if (l >= sizeof(sig)) l = sizeof(sig)-1;
                strncpy(sig, ps, l); sig[l] = 0;
            }
        }
    }

    const char *pv = strstr(buf.d, "\"value\"");
    if (pv) {
        pv += 7;
        while (*pv==':'||*pv==' ') pv++;
        if (*pv=='"') {
            pv++;
            int i = 0;
            while (*pv && i < (int)sizeof(token_raw)-1) {
                if (*pv=='"' && (i==0 || *(pv-1)!='\\')) break;
                token_raw[i++] = *pv++;
            }
            token_raw[i] = 0;
        }
    }

    free(buf.d);

    if (!sig[0] || !token_raw[0]) {
        LOG("vid: GQL missing sig or token");
        return false;
    }

    /* unescape \" and \\ inside the token JSON */
    char token_unesc[2048] = {0};
    {
        const char *src = token_raw;
        int di = 0;
        while (*src && di < (int)sizeof(token_unesc)-1) {
            if (*src == '\\' && (*(src+1) == '"' || *(src+1) == '\\')) {
                token_unesc[di++] = *(src+1); src += 2;
            } else {
                token_unesc[di++] = *src++;
            }
        }
        token_unesc[di] = 0;
    }
    LOG("vid: token_unesc(%.60s...)", token_unesc);

    /* URL-encode the token */
    CURL *ce = curl_easy_init();
    if (!ce) return false;
    char *etok = curl_easy_escape(ce, token_unesc, 0);
    if (!etok) { curl_easy_cleanup(ce); return false; }

    /* lowercase channel name */
    char lc[48];
    int o = 0;
    for (int i = 0; V.channel[i] && o < (int)sizeof(lc)-1; i++) {
        char ch = V.channel[i];
        if (ch == '#') continue;
        lc[o++] = (ch >= 'A' && ch <= 'Z') ? (char)(ch + 32) : ch;
    }
    lc[o] = 0;

    snprintf(V.hls_url, sizeof(V.hls_url),
        "https://usher.ttvnw.net/api/channel/hls/%s.m3u8"
        "?sig=%s&token=%s&allow_source=true&allow_spectre=true&fast_breadcrumbs=true",
        lc, sig, etok);
    curl_free(etok);
    curl_easy_cleanup(ce);

    /* pre-resolve usher (3DS DNS is flaky with curl) */
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo("usher.ttvnw.net", NULL, &hints, &res) == 0 && res) {
        char ip[32] = {0};
        inet_ntop(AF_INET, &((struct sockaddr_in*)res->ai_addr)->sin_addr,
                  ip, sizeof(ip));
        freeaddrinfo(res);
        snprintf(V.usher_resolve, sizeof(V.usher_resolve),
                 "usher.ttvnw.net:443:%s", ip);
        LOG("vid: usher resolved to %s", ip);
    } else {
        V.usher_resolve[0] = 0;
    }

    LOG("vid: hls_url=%.80s", V.hls_url);
    return true;
}


/* Copy a JSON string value (src points just after the opening quote) into
 * dst, un-escaping and replacing non-printable/non-ASCII bytes with '?'. */
static void json_copy_string(const char *src, char *dst, int dstsz) {
    int o = 0;
    while (*src && o < dstsz - 1) {
        if (*src == '"') break;
        char ch;
        if (*src == '\\' && src[1]) {
            src++;
            ch = *src++;
            if (ch == 'n' || ch == 't') ch = ' ';
            else if (ch == 'u') {
                for (int k = 0; k < 4 && *src; k++) src++;
                ch = '?';
            }
        } else {
            ch = *src++;
        }
        dst[o++] = ((unsigned char)ch >= 32 && (unsigned char)ch < 127) ? ch : '?';
    }
    dst[o] = 0;
}


/* Fetch stream title / game / viewer count via GQL.  Downloader thread. */
static void fetch_stream_meta(void) {
    char lc[48];
    int o = 0;
    for (int i = 0; V.channel[i] && o < (int)sizeof(lc)-1; i++) {
        char ch = V.channel[i];
        if (ch == '#') continue;
        lc[o++] = (ch >= 'A' && ch <= 'Z') ? (char)(ch + 32) : ch;
    }
    lc[o] = 0;

    char body[320];
    snprintf(body, sizeof(body),
        "{\"query\":\"query($login:String!){user(login:$login)"
        "{stream{title viewersCount game{displayName}}}}\","
        "\"variables\":{\"login\":\"%s\"}}", lc);

    CURL *c = curl_easy_init();
    if (!c) return;
    Buf buf = {malloc(4096), 0, 4096};
    if (!buf.d) { curl_easy_cleanup(c); return; }
    buf.d[0] = 0;

    struct curl_slist *h = NULL;
    h = curl_slist_append(h, "Content-Type: application/json");
    char cid[80];
    snprintf(cid, sizeof(cid), "Client-ID: %s", V.client_id);
    h = curl_slist_append(h, cid);

    curl_easy_setopt(c, CURLOPT_URL, "https://gql.twitch.tv/gql");
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, h);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(c, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 10L);
    CURLcode res = curl_easy_perform(c);
    long code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    curl_slist_free_all(h);
    curl_easy_cleanup(c);

    if (res != CURLE_OK || code != 200) {
        LOG("vid: meta GQL curl=%d http=%ld", (int)res, code);
        free(buf.d);
        return;
    }

    char title[128] = {0}, game[64] = {0};
    int viewers = -1;
    const char *tp = strstr(buf.d, "\"title\":\"");
    if (tp) json_copy_string(tp + 9, title, sizeof(title));
    const char *vp = strstr(buf.d, "\"viewersCount\":");
    if (vp) viewers = atoi(vp + 15);
    const char *gp = strstr(buf.d, "\"displayName\":\"");
    if (gp) json_copy_string(gp + 15, game, sizeof(game));
    free(buf.d);

    if (!title[0] && viewers < 0) return;

    LightLock_Lock(&V.lock);
    if (title[0]) strncpy(V.meta_title, title, sizeof(V.meta_title)-1);
    if (game[0])  strncpy(V.meta_game, game, sizeof(V.meta_game)-1);
    if (viewers >= 0) V.meta_viewers = viewers;
    V.meta_dirty = true;
    LightLock_Unlock(&V.lock);
    LOG("vid: meta \"%.40s\" game=%.24s viewers=%d", title, game, viewers);
}


/* Target 360p variant URL from master m3u8; fills out_w/out_h.
 * Picks variant closest to 640x360 resolution. */
static char *m3u8_lowest_variant(const char *body, const char *base_url,
                                 int *out_w, int *out_h) {
    const int target_w = 640, target_h = 360;
    long best_dist = 0x7fffffff;
    char best_url[1024] = {0};
    int best_w = 0, best_h = 0;
    const char *p = body;

    while ((p = strstr(p, "#EXT-X-STREAM-INF:")) != NULL) {
        int rw = 0, rh = 0;
        const char *nl  = strchr(p, '\n');
        if (!nl) break;
        const char *rp = strstr(p, "RESOLUTION=");
        if (rp && rp < nl) sscanf(rp + 11, "%dx%d", &rw, &rh);

        nl++;
        while (*nl == '\r' || *nl == '\n') nl++;
        if (*nl && *nl != '#') {
            const char *eol = nl;
            while (*eol && *eol != '\r' && *eol != '\n') eol++;
            if (rw > 0 && rh > 0 && eol > nl) {
                /* distance from target resolution */
                long dw = rw - target_w, dh = rh - target_h;
                long dist = dw*dw + dh*dh;
                if (dist < best_dist) {
                    size_t l = (size_t)(eol - nl);
                    if (l >= sizeof(best_url)) l = sizeof(best_url)-1;
                    best_dist = dist;
                    memcpy(best_url, nl, l);
                    best_url[l] = 0;
                    best_w = rw; best_h = rh;
                }
            }
        }
        p = nl;
    }

    if (!best_url[0]) return NULL;
    if (out_w) *out_w = best_w;
    if (out_h) *out_h = best_h;
    return resolve_url(best_url, base_url);
}


/* Pick the NEXT segment to play from the media m3u8 (parses #EXTINF: tags;
 * modern Twitch CDN URLs have no file extension).  Playing the segment
 * AFTER the last played one gives gap-free playback; if we fell out of the
 * playlist window, jump to one before the live edge. */
#define M3U8_MAX_SEGS 16
static char *m3u8_pick_segment(const char *body, const char *base_url,
                               const char *last_url) {
    const char *starts[M3U8_MAX_SEGS];
    int lens[M3U8_MAX_SEGS];
    int n = 0;
    const char *p = body;

    while ((p = strstr(p, "#EXTINF:"))) {
        const char *nl = strchr(p, '\n');
        if (!nl) break;
        nl++;
        while (*nl == '\r' || *nl == '\n') nl++;
        if (*nl && *nl != '#') {
            const char *eol = nl;
            while (*eol && *eol != '\r' && *eol != '\n') eol++;
            if (eol > nl) {
                if (n == M3U8_MAX_SEGS) {
                    memmove((void*)starts, (void*)(starts+1),
                            sizeof(starts[0])*(M3U8_MAX_SEGS-1));
                    memmove(lens, lens+1, sizeof(lens[0])*(M3U8_MAX_SEGS-1));
                    n--;
                }
                starts[n] = nl;
                lens[n] = (int)(eol - nl);
                n++;
            }
        }
        p = nl;
    }

    if (n == 0) {
        LOG("vid: no segments in m3u8 (%.200s)", body ? body : "(null)");
        return NULL;
    }

    int pick = -1;
    if (last_url && last_url[0]) {
        for (int i = 0; i < n; i++) {
            char tmp[2048];
            int l = lens[i] < (int)sizeof(tmp)-1 ? lens[i] : (int)sizeof(tmp)-1;
            memcpy(tmp, starts[i], l); tmp[l] = 0;
            if (strstr(last_url, tmp)) { pick = i + 1; break; }
        }
    }
    if (pick < 0)  pick = (n >= 2) ? n - 2 : 0;   /* (re)join near live edge */
    if (pick >= n) return NULL;                    /* nothing new yet */

    char urlbuf[2048];
    int l = lens[pick] < (int)sizeof(urlbuf)-1 ? lens[pick] : (int)sizeof(urlbuf)-1;
    memcpy(urlbuf, starts[pick], l); urlbuf[l] = 0;
    return resolve_url(urlbuf, base_url);
}


/* ═════════════════ MPEG-TS demuxer ═════════════════ */
static struct {
    int  pmt_pid, vid_pid;
    u8   pes[PES_MAX];
    int  pes_len;
    bool pes_valid;
    /* audio (AAC ADTS, stream_type 0x0F) */
    int  aud_pid;
    u8   apes[APES_MAX];
    int  apes_len;
    bool apes_valid;
} TS;

static void ts_reset(void) {
    TS.pmt_pid = -1; TS.vid_pid = -1; TS.pes_len = 0; TS.pes_valid = false;
    TS.aud_pid = -1; TS.apes_len = 0; TS.apes_valid = false;
}


/* Feed one NAL unit to MVD.  Port of the working Video_player_for_3DS
 * approach: MVDSTD_SetConfig before EVERY ProcessVideoFrame; sentinel
 * corner bytes detect real frame writes; INCOMPLETEPROCESSING keyframe
 * tails are resubmitted so the reference picture stays intact. */
static void nal_feed(const u8 *data, int len) {
    if (len <= 0 || len + 3 > NAL_MAX) return;
    int type = data[0] & 0x1f;

    /* mvdstdProcessVideoFrame wants the 3-byte 00 00 01 prefix */
    V.nalbuf[0] = 0x00;
    V.nalbuf[1] = 0x00;
    V.nalbuf[2] = 0x01;
    memcpy(V.nalbuf + 3, data, len);
    int total = len + 3;
    GSPGPU_FlushDataCache(V.nalbuf, total);

    Result cr = MVDSTD_SetConfig(&V.cfg);

    MVDSTD_ProcessNALUnitOut po;
    memset(&po, 0, sizeof(po));
    Result r = mvdstdProcessVideoFrame(V.nalbuf, total, 0, &po);

    /* Large keyframes (>~9KB) return INCOMPLETEPROCESSING — resubmit the
     * unprocessed tail or the whole next second of P-frames smears. */
    {
        u32 pos = 0, left = (u32)total;
        int guard = 0;
        while (r == MVD_STATUS_INCOMPLETEPROCESSING &&
               po.remaining_size > 0 && po.remaining_size < left &&
               guard++ < 4) {
            pos  += left - po.remaining_size;
            left  = po.remaining_size;
            r = mvdstdProcessVideoFrame(V.nalbuf + pos, left, 0, &po);
            if (s_frame_count < 3 || type == 5)
                LOG("nal: cont +%lu mvd=0x%lx", (unsigned long)left, r);
        }
    }

    if (s_frame_count < 3 || (type == 5 && r != MVD_STATUS_FRAMEREADY))
        LOG("nal: type=%d len=%d mvd=0x%lx cfg=0x%lx", type, len, r, cr);

    if (MVD_CHECKNALUPROC_SUCCESS(r) && r == MVD_STATUS_FRAMEREADY) {
        int w = V.vid_out_w, h = V.vid_out_h;
        GSPGPU_InvalidateDataCache(V.outbuf, w * h * 2);

        /* sentinel corners (0x11): changed => MVD wrote THIS frame */
        bool wrote = (V.outbuf[0] != 0x11 ||
                      V.outbuf[w * 2 - 1] != 0x11 ||
                      V.outbuf[(w * h * 2) - (w * 2)] != 0x11 ||
                      V.outbuf[w * h * 2 - 1] != 0x11);

        s_frame_count++;

        if (wrote && (!s_got_real_frame || (s_frame_count % 300) == 0)) {
            const u16 *px = (const u16*)V.outbuf;
            const u16 *mid = (const u16*)(V.outbuf + (h/2) * w * 2);
            if (!s_got_real_frame)
                LOG("FIRST REAL FRAME #%d", s_frame_count);
            s_got_real_frame = true;
            LOG("PIXELS row0: %04x %04x %04x %04x", px[0],px[1],px[2],px[3]);
            LOG("PIXELS mid: %04x %04x %04x %04x", mid[0],mid[1],mid[2],mid[3]);
        }

        if (wrote) {
            LightLock_Lock(&V.lock);
            V.has_frame = true;
            LightLock_Unlock(&V.lock);

            /* re-arm sentinels for the next frame check */
            V.outbuf[0] = 0x11;
            V.outbuf[w * 2 - 1] = 0x11;
            V.outbuf[(w * h * 2) - (w * 2)] = 0x11;
            V.outbuf[w * h * 2 - 1] = 0x11;
            GSPGPU_FlushDataCache(V.outbuf, w * h * 2);
        }

        if (s_frame_count <= 3 || (s_frame_count % 300) == 0)
            LOG("nal: FRAMEREADY #%d wrote=%d", s_frame_count, (int)wrote);

        /* Pace decode to exactly 30fps with the system tick (fixed sleeps
         * drift: too fast drains the buffer, too slow falls behind). */
        if (wrote) {
            const u64 tpf = SYSCLOCK_ARM11 / 30;
            u64 now = svcGetSystemTick();
            if (s_pace_tick == 0 || now > s_pace_tick + SYSCLOCK_ARM11)
                s_pace_tick = now;
            s_pace_tick += tpf;
            if (s_pace_tick > now) {
                u64 dt = s_pace_tick - now;
                svcSleepThread((s64)(dt * 1000000000ULL / SYSCLOCK_ARM11));
            }
        }
    }
}


/* Flush a completed video PES: split into NALs, feed each to MVD. */
static void pes_flush(void) {
    if (!TS.pes_valid || TS.pes_len < 9) return;
    int hdr = 9 + TS.pes[8];
    if (hdr >= TS.pes_len) return;
    const u8 *h = TS.pes + hdr;
    int hlen = TS.pes_len - hdr;
    int i = 0;
    while (i < hlen-3) {
        bool sc4 = (i+3 < hlen) &&
                   h[i]==0 && h[i+1]==0 && h[i+2]==0 && h[i+3]==1;
        bool sc3 = h[i]==0 && h[i+1]==0 && h[i+2]==1;
        if (!sc3 && !sc4) { i++; continue; }
        int start = sc4 ? i+4 : i+3;
        int j = start+1;
        while (j < hlen-2) {
            if (h[j]==0 && h[j+1]==0 && h[j+2]==1) break;
            if (j+3 < hlen &&
                h[j]==0 && h[j+1]==0 && h[j+2]==0 && h[j+3]==1) break;
            j++;
        }
        /* No further start code: LAST NAL — take ALL remaining bytes.
         * (Stopping at hlen-2 truncated 2 bytes off every keyframe and
         * corrupted the decoder's reference picture: gray video.) */
        if (j >= hlen-2) j = hlen;
        nal_feed(h+start, j-start);
        i = j;
    }
    TS.pes_len = 0; TS.pes_valid = false;
}


/* Flush a completed audio PES: hand the ADTS payload to the decoder. */
static void apes_flush(void) {
    if (!TS.apes_valid || TS.apes_len < 9) { TS.apes_len = 0; TS.apes_valid = false; return; }
    int hdr = 9 + TS.apes[8];
    if (hdr < TS.apes_len)
        audio_feed(TS.apes + hdr, TS.apes_len - hdr);
    TS.apes_len = 0; TS.apes_valid = false;
}


static void ts_packet(const u8 *p) {
    if (p[0] != 0x47) return;
    int pid   = ((p[1]&0x1F)<<8) | p[2];
    bool pusi = (p[1]>>6) & 1;
    int afc   = (p[3]>>4) & 3;
    int off   = 4;
    if (afc & 2) off += 1+p[4];
    if (!(afc & 1) || off >= 188) return;
    const u8 *pay = p+off; int plen = 188-off;

    if (pid==0 && TS.pmt_pid<0) {
        const u8 *s = pay+1+pay[0];
        if (s[0]==0x00) {
            const u8 *e = s+3+(((s[1]&0x0f)<<8)|s[2])-4;
            const u8 *q = s+8;
            while (q+3 < e) {
                int pn = (q[0]<<8)|q[1];
                int pp = ((q[2]&0x1f)<<8)|q[3];
                if (pn) { TS.pmt_pid = pp; break; }
                q += 4;
            }
        }
    } else if (pid==TS.pmt_pid && TS.vid_pid<0) {
        const u8 *s = pay+1+pay[0];
        if (s[0]==0x02) {
            int sl  = ((s[1]&0x0f)<<8)|s[2];
            int pil = ((s[10]&0x0f)<<8)|s[11];
            const u8 *es = s+12+pil;
            const u8 *e  = s+3+sl-4;
            while (es+4 < e) {
                int st = es[0];
                int epid = ((es[1]&0x1f)<<8)|es[2];
                if (st==0x1B && TS.vid_pid<0) TS.vid_pid = epid;      /* H.264 */
                else if (st==0x0F && TS.aud_pid<0) TS.aud_pid = epid; /* AAC   */
                es += 5 + (((es[3]&0x0f)<<8) | es[4]);
            }
        }
    } else if (pid==TS.vid_pid) {
        if (pusi) pes_flush();
        if (pusi) TS.pes_valid = true;
        if (TS.pes_valid) {
            int copy = plen;
            if (TS.pes_len+copy > PES_MAX) copy = PES_MAX-TS.pes_len;
            if (copy > 0) {
                memcpy(TS.pes+TS.pes_len, pay, copy);
                TS.pes_len += copy;
            }
        }
    } else if (pid==TS.aud_pid) {
        if (pusi) apes_flush();
        if (pusi) TS.apes_valid = true;
        if (TS.apes_valid) {
            int copy = plen;
            if (TS.apes_len+copy > APES_MAX) copy = APES_MAX-TS.apes_len;
            if (copy > 0) {
                memcpy(TS.apes+TS.apes_len, pay, copy);
                TS.apes_len += copy;
            }
        }
    }
}


/* ═════════════════ segment pipeline ═════════════════ */

/* Download a segment (persistent handle; the CDN connection stays open). */
static CURL *s_curl_seg = NULL;
static u8 *download_segment(const char *url, size_t *out_len) {
    *out_len = 0;
    if (!s_curl_seg) s_curl_seg = curl_easy_init();
    CURL *c = s_curl_seg;
    if (!c) return NULL;
    Buf b = {malloc(SEG_MAX/4), 0, SEG_MAX/4};
    if (!b.d) return NULL;
    b.d[0] = 0;
    curl_easy_reset(c);
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &b);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_TCP_KEEPALIVE, 1L);
    CURLcode res = curl_easy_perform(c);
    long code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    if (code != 200 || b.len < 188) {
        LOG("vid: seg dl code=%ld curl=%d len=%d", code, (int)res, (int)b.len);
        free(b.d);
        return NULL;
    }
    *out_len = b.len;
    return (u8*)b.d;
}

/* Demux + decode one downloaded segment (decoder thread). */
static void decode_segment(const u8 *data, size_t len) {
    /* Skip any ID3 header: find 3 consecutive TS sync bytes */
    size_t sync_off = 0;
    bool found = false;
    for (size_t i = 0; i + 188*2 <= len; i++) {
        if (data[i] == 0x47 && data[i+188] == 0x47 && data[i+376] == 0x47) {
            sync_off = i; found = true; break;
        }
    }
    if (!found) {
        LOG("vid: no TS sync in segment (first bytes %02x %02x %02x %02x)",
            data[0], data[1], data[2], data[3]);
        return;
    }

    ts_reset();
    for (size_t i = sync_off; i + 188 <= len; i += 188)
        ts_packet(data + i);
    pes_flush();
    apes_flush();
    if (TS.vid_pid < 0)
        LOG("vid: demux found no video PID (pmt=%d)", TS.pmt_pid);
}

/* 2-slot queue: downloader produces, decoder consumes. */
#define SEG_QUEUE_DEPTH 2
static struct { u8 *data; size_t len; } s_segq[SEG_QUEUE_DEPTH];
static int s_segq_count = 0;
static LightLock s_segq_lock;
static Thread s_dec_thread = NULL;

static bool segq_push(u8 *data, size_t len) {
    bool ok = false;
    LightLock_Lock(&s_segq_lock);
    if (s_segq_count < SEG_QUEUE_DEPTH) {
        s_segq[s_segq_count].data = data;
        s_segq[s_segq_count].len = len;
        s_segq_count++;
        ok = true;
    }
    LightLock_Unlock(&s_segq_lock);
    return ok;
}

static u8 *segq_pop(size_t *out_len) {
    u8 *d = NULL;
    LightLock_Lock(&s_segq_lock);
    if (s_segq_count > 0) {
        d = s_segq[0].data;
        *out_len = s_segq[0].len;
        for (int i = 1; i < s_segq_count; i++) s_segq[i-1] = s_segq[i];
        s_segq_count--;
    }
    LightLock_Unlock(&s_segq_lock);
    return d;
}

static bool segq_full(void) {
    LightLock_Lock(&s_segq_lock);
    bool f = (s_segq_count >= SEG_QUEUE_DEPTH);
    LightLock_Unlock(&s_segq_lock);
    return f;
}

static void segq_drain(void) {
    LightLock_Lock(&s_segq_lock);
    for (int i = 0; i < s_segq_count; i++) free(s_segq[i].data);
    s_segq_count = 0;
    LightLock_Unlock(&s_segq_lock);
}

/* Decoder thread: drains the queue and feeds MVD (paced) — runs in
 * parallel with the downloader so the picture never freezes. */
static void dec_thread_fn(void *arg) {
    (void)arg;
    while (V.active) {
        size_t len = 0;
        u8 *d = segq_pop(&len);
        if (!d) {
            svcSleepThread(15000000LL);
            continue;
        }
        decode_segment(d, len);
        free(d);
    }
    LOG("vid: decode thread exit");
}


/* ═════════════════ downloader thread ═════════════════ */
static void vid_thread(void *arg) {
    (void)arg;
    char variant_url[2048] = {0};

    LOG("vid: fetching token for %s (cid=%.12s...)", V.channel, V.client_id);
    if (!fetch_hls_url()) {
        LOG("vid: fetch_hls_url FAILED");
        LightLock_Lock(&V.lock); V.offline = true; LightLock_Unlock(&V.lock);
        return;
    }

    char *master = http_get(V.hls_url, NULL);
    LOG("vid: master m3u8 %s len=%d",
        master ? "OK" : "FAILED", master ? (int)strlen(master) : 0);
    if (!master) {
        LightLock_Lock(&V.lock); V.offline = true; LightLock_Unlock(&V.lock);
        return;
    }

    int in_w = 0, in_h = 0;
    char *var = m3u8_lowest_variant(master, V.hls_url, &in_w, &in_h);
    free(master);
    if (!var) {
        LOG("vid: no variant in master m3u8");
        LightLock_Lock(&V.lock); V.offline = true; LightLock_Unlock(&V.lock);
        return;
    }
    strncpy(variant_url, var, sizeof(variant_url)-1);
    free(var);
    if (in_w <= 0 || in_h <= 0) { in_w = OUT_W; in_h = OUT_H; }
    LOG("vid: variant=%.100s res=%dx%d", variant_url, in_w, in_h);

    /* MVD config — Video_player_for_3DS recipe:
     * dims MUST be 16-aligned (H.264 macroblocks) or SetConfig fails;
     * GenerateDefaultConfig with NULL buffers then set physaddr manually;
     * SetConfig is re-sent before every ProcessVideoFrame (in nal_feed). */
    int out_w = in_w, out_h = in_h;
    if (out_w % 16) out_w += 16 - out_w % 16;
    if (out_h % 16) out_h += 16 - out_h % 16;
    if (out_w > TEX_W) out_w = TEX_W;
    if (out_h > TEX_H) out_h = TEX_H;
    mvdstdGenerateDefaultConfig(&V.cfg, out_w, out_h, out_w, out_h,
        NULL, NULL, NULL);
    V.cfg.physaddr_outdata0 = osConvertVirtToPhys(V.outbuf);
    LOG("vid: MVD cfg %dx%d (aligned from %dx%d) physaddr=0x%lx",
        out_w, out_h, in_w, in_h, V.cfg.physaddr_outdata0);

    /* arm sentinel corners */
    V.outbuf[0] = 0x11;
    V.outbuf[out_w * 2 - 1] = 0x11;
    V.outbuf[(out_w * out_h * 2) - (out_w * 2)] = 0x11;
    V.outbuf[out_w * out_h * 2 - 1] = 0x11;
    GSPGPU_FlushDataCache(V.outbuf, out_w * out_h * 2);

    /* buffer stride dims = aligned; DISPLAY dims = real video size
     * (crops the padding columns).  UV: v=1.0 at first tile row. */
    V.vid_out_w = out_w;
    V.vid_out_h = out_h;
    V.subtex = (Tex3DS_SubTexture){ in_w, in_h,
        0.0f, 1.0f, (float)in_w/TEX_W, 1.0f - (float)in_h/TEX_H };

    int seg_count = 0;
    u64 last_meta_ms = 0;
    while (V.active) {
        u64 now_ms = osGetTime();
        if (last_meta_ms == 0 || now_ms - last_meta_ms > 60000) {
            last_meta_ms = now_ms;
            fetch_stream_meta();
        }

        char *media = http_get(variant_url, NULL);
        if (!media) {
            LOG("vid: media m3u8 fetch failed");
            svcSleepThread(1000000000LL);
            continue;
        }

        char *seg = m3u8_pick_segment(media, variant_url, V.last_seg);
        free(media);

        if (!seg) {                         /* nothing new at live edge */
            svcSleepThread(300000000LL);
            continue;
        }

        while (V.active && segq_full())     /* wait for a free slot */
            svcSleepThread(30000000LL);
        if (!V.active) { free(seg); break; }

        size_t slen = 0;
        u8 *sdata = download_segment(seg, &slen);
        strncpy(V.last_seg, seg, sizeof(V.last_seg)-1);
        V.last_seg[sizeof(V.last_seg)-1] = 0;
        free(seg);

        if (sdata) {
            seg_count++;
            if (seg_count <= 3 || (seg_count % 30) == 0)
                LOG("vid: seg #%d (%d KB) q=%d", seg_count,
                    (int)(slen / 1024), s_segq_count);
            if (!segq_push(sdata, slen))
                free(sdata);
        }
    }
    LOG("vid: download thread exit");
}


/* ═════════════════ public API ═════════════════ */

/* Call BEFORE C3D_Init — MVD must claim GPU resources before citro3d.
 * Returns false on old 3DS (no MVD hardware). */
bool video_mvd_preinit(void) {
    bool isNew = false;
    APT_CheckNew3DS(&isNew);
    if (!isNew) { LOG("vid: old 3DS, no MVD"); return false; }
    Result r = mvdstdInit(MVDMODE_VIDEOPROCESSING, MVD_INPUT_H264,
        MVD_OUTPUT_BGR565, MVD_DEFAULT_WORKBUF_SIZE, NULL);
    if (R_FAILED(r)) { LOG("vid: mvdstdInit FAILED 0x%lx", r); return false; }
    V.mvd_ok = true;
    LOG("vid: mvdstdInit OK (pre-citro3d)");
    return true;
}

bool video_init(void) {
    if (!V.mvd_ok) { LOG("vid: skipping init (preinit failed)"); return false; }

    LightLock_Init(&V.lock);
    LightLock_Init(&s_segq_lock);

    V.outbuf = linearMemAlign(TEX_W * TEX_H * 2, 0x1000);
    V.nalbuf = linearAlloc(NAL_MAX);
    V.stgbuf = linearAlloc(TEX_W * TEX_H * 2);
    if (!V.outbuf || !V.nalbuf || !V.stgbuf) {
        LOG("vid: linearAlloc FAILED");
        return false;
    }
    memset(V.outbuf, 0, TEX_W * TEX_H * 2);
    memset(V.stgbuf, 0, TEX_W * TEX_H * 2);
    GSPGPU_FlushDataCache(V.outbuf, TEX_W * TEX_H * 2);
    GSPGPU_FlushDataCache(V.stgbuf, TEX_W * TEX_H * 2);

    C3D_TexInit(&V.tex, TEX_W, TEX_H, GPU_RGB565);
    C3D_TexSetFilter(&V.tex, GPU_LINEAR, GPU_LINEAR);
    /* clear texture memory — fresh linear heap shows as white noise */
    memset(V.tex.data, 0, TEX_W * TEX_H * 2);
    GSPGPU_FlushDataCache(V.tex.data, TEX_W * TEX_H * 2);

    V.subtex = (Tex3DS_SubTexture){ OUT_W, OUT_H,
        0.0f, 1.0f, (float)OUT_W/TEX_W, 1.0f - (float)OUT_H/TEX_H };
    V.img = (C2D_Image){ &V.tex, &V.subtex };
    V.tex_valid = false;
    V.vid_out_w = OUT_W;
    V.vid_out_h = OUT_H;

    /* audio is optional — video keeps working without it */
    if (!audio_init())
        LOG("vid: continuing without audio");

    LOG("vid: init OK");
    return true;
}

void video_exit(void) {
    video_stop();
    audio_exit();
    if (s_curl_pl)  { curl_easy_cleanup(s_curl_pl);  s_curl_pl  = NULL; }
    if (s_curl_seg) { curl_easy_cleanup(s_curl_seg); s_curl_seg = NULL; }
    if (V.mvd_ok)  { mvdstdExit(); V.mvd_ok = false; }
    if (V.outbuf)  { linearFree(V.outbuf); V.outbuf = NULL; }
    if (V.nalbuf)  { linearFree(V.nalbuf); V.nalbuf = NULL; }
    if (V.stgbuf)  { linearFree(V.stgbuf); V.stgbuf = NULL; }
    C3D_TexDelete(&V.tex);
}

void video_start(const char *channel, const char *oauth_pass,
                 const char *client_id) {
    video_stop();
    audio_reset();
    strncpy(V.channel,   channel,    sizeof(V.channel)-1);
    strncpy(V.oauth,     oauth_pass, sizeof(V.oauth)-1);
    strncpy(V.client_id, client_id,  sizeof(V.client_id)-1);
    V.hls_url[0] = 0; V.last_seg[0] = 0;
    V.has_frame = false; V.offline = false; V.active = true;
    V.tex_valid = false;  /* Clear old frame so loading indicator works */
    V.vid_out_w = OUT_W; V.vid_out_h = OUT_H;
    V.meta_title[0] = 0; V.meta_game[0] = 0;
    V.meta_viewers = 0; V.meta_dirty = false;
    s_frame_count = 0; s_upload_count = 0; s_draw_count = 0;
    s_got_real_frame = false;
    s_pace_tick = 0;
    segq_drain();
    /* both pipeline threads run on core 2 (New3DS extra core) */
    s_dec_thread = threadCreate(dec_thread_fn, NULL, 64*1024, 0x19, 2, false);
    V.thread     = threadCreate(vid_thread,    NULL, 128*1024, 0x1A, 2, false);
    LOG("video_start: ch=%s dl=%p dec=%p",
        V.channel, (void*)V.thread, (void*)s_dec_thread);
}

void video_stop(void) {
    if (!V.active) return;
    V.active = false;
    if (V.thread) {
        threadJoin(V.thread, U64_MAX);
        threadFree(V.thread);
        V.thread = NULL;
    }
    if (s_dec_thread) {
        threadJoin(s_dec_thread, U64_MAX);
        threadFree(s_dec_thread);
        s_dec_thread = NULL;
    }
    segq_drain();
    /* tex_valid intentionally NOT cleared — hold last frame on screen */
}

/* Call BEFORE C3D_FrameBegin (main thread): upload pending frame. */
void video_upload_frame(void) {
    LightLock_Lock(&V.lock);
    bool ready = V.has_frame;
    int w = V.vid_out_w, h = V.vid_out_h;
    if (ready) {
        V.has_frame = false;
        /* MVD output is LINEAR raster.  Restride rows into stgbuf at
         * TEX_W pitch, then let GX do the linear->tiled conversion. */
        for (int row = 0; row < h && row < TEX_H; row++) {
            memcpy(V.stgbuf + row * TEX_W * 2,
                   V.outbuf + row * w * 2,
                   (w <= TEX_W ? w : TEX_W) * 2);
        }
    }
    LightLock_Unlock(&V.lock);
    if (!ready) return;

    int th = (h > 0 && h <= TEX_H) ? h : TEX_H;
    GSPGPU_FlushDataCache(V.stgbuf, TEX_W * th * 2);
    C3D_SyncDisplayTransfer(
        (u32*)V.stgbuf,   GX_BUFFER_DIM(TEX_W, th),
        (u32*)V.tex.data, GX_BUFFER_DIM(TEX_W, th),
        GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGB565)  |
        GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB565) |
        GX_TRANSFER_OUT_TILED(1)                       |
        GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));

    V.tex_valid = true;
    s_upload_count++;
    if (s_upload_count == 1 || (s_upload_count % 300) == 0)
        LOG("upload_frame: #%d %dx%d", s_upload_count, w, h);
}

/* Draw inside a C3D frame, scaled (aspect-fit) to the 400x240 top screen. */
void video_draw_top(float x, float y) {
    if (!V.tex_valid) return;
    float w = (float)V.subtex.width;
    float h = (float)V.subtex.height;
    if (w <= 0.0f || h <= 0.0f) return;
    float sx = 400.0f / w;
    float sy = 240.0f / h;
    float s  = (sx < sy) ? sx : sy;
    float dx = x + (400.0f - w * s) * 0.5f;
    float dy = y + (240.0f - h * s) * 0.5f;
    C2D_DrawImageAt(V.img, dx, dy, 0.5f, NULL, s, s);
    s_draw_count++;
    if (s_draw_count == 1)
        LOG("draw_top: %dx%d scaled", (int)w, (int)h);
}

bool video_has_picture(void) {
    return V.tex_valid;
}

bool video_is_offline(void) {
    LightLock_Lock(&V.lock); bool r = V.offline; LightLock_Unlock(&V.lock);
    return r;
}
bool video_is_active(void) { return V.active; }

/* Copy fresh stream metadata; true only when new data arrived. */
bool video_poll_meta(char *title, size_t tsz, char *game, size_t gsz,
                     int *viewers) {
    bool got = false;
    LightLock_Lock(&V.lock);
    if (V.meta_dirty) {
        if (title && tsz) { strncpy(title, V.meta_title, tsz-1); title[tsz-1] = 0; }
        if (game && gsz)  { strncpy(game, V.meta_game, gsz-1);  game[gsz-1] = 0; }
        if (viewers) *viewers = V.meta_viewers;
        V.meta_dirty = false;
        got = true;
    }
    LightLock_Unlock(&V.lock);
    return got;
}
