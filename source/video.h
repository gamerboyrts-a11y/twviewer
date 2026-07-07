#pragma once
#include <3ds.h>
#include <stdbool.h>
#include <stddef.h>
#include <citro2d.h>

/* Call BEFORE C3D_Init — MVD must claim GPU resources first.
 * Returns false on old 3DS (no MVD hardware). */
bool video_mvd_preinit(void);

/* Call AFTER C3D_Init/C2D_Init to set up textures and buffers. */
bool video_init(void);
void video_exit(void);

/* channel = bare name (no '#').  oauth_pass = full "PASS oauth:xxx" string.
 * quality: 0=160p, 1=360p */
void video_start(const char *channel, const char *oauth_pass, const char *client_id, int quality);
void video_stop(void);

/* Call BEFORE C3D_FrameBegin to upload any pending decoded frame. */
void video_upload_frame(void);

/* Call from draw_top() inside a C3D frame. */
void video_draw_top(float x, float y);

/* True once at least one decoded frame is on the texture (use to show a
 * buffering spinner until then). */
bool video_has_picture(void);

bool video_is_offline(void);
bool video_is_active(void);

/* Stream metadata (title/game/viewers), fetched via GQL on the video
 * thread.  Returns true when fresh data was copied out. */
bool video_poll_meta(char *title, size_t tsz, char *game, size_t gsz,
                     int *viewers);
