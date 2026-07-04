# Twitch3DS — project notes

Twitch client homebrew for New 3DS (C, libctru, citro2d/citro3d, MVD hardware
H.264 decode, ffmpeg AAC audio). Build with devkitARM via
`/c/devkitPro/msys2/usr/bin/bash -lc "cd '<repo>' && make"`.

## Git backup policy

After every working change set (successful build the user will test),
commit and push:

1. `git add -A`
2. Commit with a short imperative message describing the change.
3. `git push origin main`

Never leave tested changes uncommitted at the end of a session.

## Architecture quick map

- `source/main.c` — UI, IRC chat (mbedtls), device-code login, tabs.
- `source/video.c` — HLS pipeline: GQL token, usher m3u8, downloader thread +
  decoder thread (core 2), MPEG-TS demux, MVD hardware decode, GX upload.
- `source/audio.c` — AAC (ADTS) decode via bundled ffmpeg, ndsp output.
- `library/` — prebuilt 3DS ffmpeg static libs (from Core-2-Extreme's
  Video_player_for_3DS builds) + headers. Only ffmpeg libs live here;
  curl/mbedtls/ctru come from devkitPro portlibs.

## Hard-won MVD facts (do not regress)

- MVD config dims must be 16-aligned (284x160 stream -> 288x160 config).
- `MVDSTD_SetConfig` before every `mvdstdProcessVideoFrame`; it fails until
  SPS/PPS have been fed.
- `mvdstdRenderVideoFrame`/`mvdstdSetupOutputBuffers` are NOT used.
- MVD output is linear RGB565; restride to TEX_W then GX transfer
  (`GX_TRANSFER_OUT_TILED`), never treat it as pre-tiled with stock libctru.
- Keyframes >~9KB return `MVD_STATUS_INCOMPLETEPROCESSING` (0x17004):
  resubmit the unprocessed tail.
- The last NAL in a PES has no following start code — take ALL remaining
  bytes (a 2-byte truncation here corrupts every keyframe).

## Planning & Review Policy

- For small fixes, implement directly without a full OpenSpec cycle.
- Only use OpenSpec (/opsx:propose) for large or risky features 
  (e.g., changes touching video.c, MVD decode, or thread handling).
- For any adversarial review, always dispatch to the four subagents 
  in .claude/agents/ (reviewer-correctness, reviewer-regression, 
  reviewer-scope, reviewer-security). Each subagent has "model: opus" 
  pinned in its frontmatter — do not substitute Fable or any other 
  model for these reviews, regardless of what the main session model 
  is set to.