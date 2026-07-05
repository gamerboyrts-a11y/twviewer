## Context

When user switch channel from Channels tab, `join_channel()` immediately call `video_stop()`, clear chat, reconnect IRC, then call `video_start()` and switch to TAB_CHAT. Video pipeline runs on separate thread (core 2). First decoded frame take time (network fetch HLS playlist, download segments, decode keyframe). During this gap, old channel's last frame stay visible on top screen with no feedback that new channel loading.

`video.h` already provide `video_has_picture()` — return true once first frame decoded and uploaded to texture. Comment explicitly say "use to show buffering spinner until then."

## Goals / Non-Goals

**Goals:**
- Show loading indicator on bottom screen when channel switch initiated
- Hide indicator when first frame of new stream ready (`video_has_picture()` true) or error occur
- Simple text indicator (no complex spinner animation) — 3DS limited resources
- Zero changes to video pipeline threading or decoding logic

**Non-Goals:**
- Loading indicator for initial app launch (user already see stream list immediately)
- Progress percentage or time estimate (no reliable data source)
- Animated spinner graphics (keep simple, text-based)

## Decisions

**Decision 1: Loading state flag in app struct**
Add `bool channel_loading` to main app state struct. Set true in `join_channel()` before `video_start()`, check false in main loop once `video_has_picture()` or `app.state == STATE_ERROR`.

*Why:* Simple boolean sufficient. No complex state machine needed.

*Alternative considered:* Timestamp-based timeout — rejected because `video_has_picture()` already reliable signal.

**Decision 2: Text indicator, not animated sprite**
Display "Loading stream..." text centered on bottom screen when `channel_loading` true.

*Why:* 3DS homebrew best practice = minimize GPU load. Text render via citro2d already used everywhere. Animated spinner = extra texture + frame updates.

*Alternative considered:* Rotating sprite — rejected, resource cost not justified for rare operation (channel switches).

**Decision 3: Check loading state in main loop after frame upload**
In main loop, after `video_upload_frame()` call, check `if (app.channel_loading && video_has_picture())` then set `app.channel_loading = false`.

*Why:* `video_upload_frame()` process any pending decoded frame. Checking immediately after ensure indicator hide as soon as first frame ready.

*Alternative considered:* Check inside draw function — rejected, draw function should be pure render, not mutate state.

**Decision 4: Also clear loading on error state**
Set `app.channel_loading = false` when `app.state` transition to `STATE_ERROR`.

*Why:* If stream fail to load (offline channel, network error), user need see error message, not stuck loading indicator.

## Risks / Trade-offs

**Risk:** Loading indicator flash too briefly on fast network (sub-second load)
→ **Mitigation:** Acceptable. Brief flash better than no feedback. Could add minimum display time (e.g., 500ms) in future if user report flashing annoying, but start simple.

**Risk:** `video_has_picture()` not thread-safe if called outside expected context
→ **Mitigation:** Check video.c implementation — `video_has_picture()` just read `V.tex_valid` bool. No lock needed for single bool read on ARM (atomic). Already used in draw path with no reported issues.

**Trade-off:** Text indicator less polished than animated spinner
→ **Accept:** Fits 3DS homebrew aesthetic. Every other homebrew app use simple text feedback. Consistency > polish.
