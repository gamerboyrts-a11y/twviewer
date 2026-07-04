#pragma once
#include <3ds.h>
#include <stdbool.h>

/* AAC audio playback via ffmpeg (avcodec) + ndsp.
 * audio_init() may fail gracefully (e.g. missing DSP firmware dump) —
 * the app keeps working without sound. */
bool audio_init(void);
void audio_exit(void);

/* Reset decoder state when a new stream starts. */
void audio_reset(void);

/* Feed a chunk of ADTS AAC data (an audio PES payload from the TS demuxer).
 * Splits it into ADTS frames, decodes and queues them on the DSP. */
void audio_feed(const u8 *data, int len);

bool audio_available(void);
