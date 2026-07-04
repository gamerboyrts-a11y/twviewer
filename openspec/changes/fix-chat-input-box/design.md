# Design — fix-chat-input-box

## Context

Chat tab bottom bar (`source/main.c`) today:

- `draw_chat_tab()` (main.c:898) renders the input bar: SEND button at `(BOT_W-46, INPUT_BAR_Y+2, 44, INPUT_BAR_H-4)`, and the left area shows `app.input` if non-empty, else `app.status_msg` ("Connected to #chan"), else a hint. Line 938 passes `app.status_msg` as a printf format — latent format-string bug.
- `handle_touch()` TAB_CHAT (main.c:1027): only the SEND rect is touchable; it opens swkbd and sends immediately on confirm. The left input area has no hit region.
- `handle_buttons()` TAB_CHAT (main.c:1100): A opens swkbd and sends immediately; B clears `app.input`.
- `swkbd_get()` (main.c:468) has no initial-text support.
- `app.input[128]` already exists in the App struct and persists across frames/tabs — it just never holds a reviewable draft because every keyboard path sends and clears instantly.

Constraints: single-threaded UI loop, citro2d immediate-mode drawing, system font at 0.38f scale, input bar is 26 px tall on a 320 px wide screen. swkbd is a blocking modal (fine — already used everywhere).

## Goals / Non-Goals

**Goals:**
- Two-step compose-then-send: draft visible in a touchable input box before SEND fires.
- Keyboard pre-filled with the draft when editing.
- Status text out of the input box; box always looks/acts like an input field.
- Kill the format-string bug on line 938.

**Non-Goals:**
- No inline/hardware keyboard, no character-by-character on-screen typing (swkbd stays the text entry mechanism — 3DS convention).
- No changes to Channels/Settings tab input bars, IRC protocol, video/audio, or persisted files.
- No draft persistence across app restarts.
- No multi-line drafts or emote rendering.

## Decisions

1. **Reuse `app.input` as the draft buffer.** It already exists, persists, and B already clears it. No struct changes. Alternative (new `draft[]` field) adds state for no benefit.

2. **Extend `swkbd_get()` with an `initial` parameter** (`const char *initial`, NULL/"" = none) that calls `swkbdSetInitialText()`. Update the 4 existing call sites to pass `NULL` (channel search x2) or `app.input` (chat compose x2). Alternative — a separate `swkbd_edit()` wrapper — duplicates 10 lines for nothing.

3. **Compose paths store, SEND path sends.**
   - Input-box tap / A press: `swkbd_get(tmp, ..., "Type a message", app.input)`; on confirm copy `tmp` into `app.input`. On cancel leave `app.input` untouched (note: confirm-with-empty cannot occur — `swkbd_get` returns true only for non-empty text, so "clear via keyboard" is not a path; B remains the clear gesture).
   - SEND tap: if `app.input[0]`, call `irc_send_msg(app.input)` then clear `app.input` **only if the send actually went out**; if empty, fall back to compose (same as input-box tap).
   - `irc_send_msg()` currently returns `void` and silently drops on not-connected / pushes "Login to send messages" on logged-out. Change it to return `bool` (true = message written to TLS socket successfully, false = any failure: not connected, not logged in, or TLS write error). Check `mbedtls_ssl_write` return (currently ignored at main.c:423); return false on negative return or zero bytes written. This satisfies the "draft preserved on failed send" scenarios including transient socket errors.
   - `join_channel()` (main.c:487-519) clears `app.input[0] = '\0'` after resetting chat state, before `app.tab = TAB_CHAT` (main.c:518), to prevent drafts intended for the old channel from being sent to the new channel.

4. **Hit regions.** Input box: `(2, INPUT_BAR_Y+2, BOT_W-52, INPUT_BAR_H-4)`; SEND unchanged at `(BOT_W-46, INPUT_BAR_Y+2, 44, INPUT_BAR_H-4)`. Both inside the input bar, mutually exclusive, no overlap with the chat-scroll region (which ends at CHAT_BOT). Check SEND first, then input box, then `return` — the existing fallthrough into scroll handling is unreachable for bar taps anyway (py >= INPUT_BAR_Y > CHAT_BOT) but an explicit return keeps it obvious.

5. **Rendering.**
   - Box: `draw_rect` inset `(2, INPUT_BAR_Y+2, BOT_W-52, INPUT_BAR_H-4)` in `COL_CHAT_BG` (darker than bar's `COL_INPUT_BG`, reads as a field) with a 1 px `COL_DIVIDER` outline.
   - Draft: white `"%s_"` via a **literal** format string at `(6, INPUT_BAR_Y+6, 0.38f)`. Placeholder when empty: gray `"Touch here to chat..."`. Never render `app.status_msg` here — fixes the format-string bug by construction (requirement: all format strings literal).
   - SEND label/color unchanged (stays actionable in both states since empty-draft SEND composes).
   - **Tail-trim for long drafts:** box interior is ~260 px. Each frame, parse the full draft with `C2D_TextFontParse` + `C2D_TextGetDimensions`; if width fits, draw whole. Else advance a start pointer from the front — skipping UTF-8 continuation bytes (`(b & 0xC0) == 0x80`) so no glyph is split — until the suffix (measured the same way) fits behind a leading `"..."`. Linear scan worst case ~127 measurements once per frame on New3DS (804 MHz) is acceptable; the text buffer is 16 K glyphs and cleared per frame, so extra parses are safe. Alternative (cache trimmed string on draft change) is premature — draft changes are the only mutation and the scan is cheap, but if profiling ever shows cost, memoize on a dirty flag.

6. **Status message routing.** `set_status()` keeps working (top-screen STATE_ERROR view reads it). Chat input bar stops displaying it. Most user-critical events emit `chat_push("System", ...)` lines (joined channel, login success, code expiry). Known gaps (out-of-scope for this change; cheap follow-ups if users complain): DCF poll-failure status "DCF: poll failed, retrying..." (main.c:659) and auto-reconnect failure statuses "DNS failed"/"Connect failed"/"TLS err" (main.c:345/353/380) currently use only `set_status()` without chat lines — after this change they have no bottom-screen surface. Mitigation: polls/reconnects auto-retry; expiry/final-failure still visible; top-screen STATE_ERROR view shows persistent errors. If critical, add `chat_push("System", ...)` for those paths in a follow-up.

## Risks / Trade-offs

- [Draft lost on send-while-disconnected in old flow] → handled: `irc_send_msg` returns bool; caller clears draft only on success.
- [Draft survives channel switch, wrong-channel send] → handled: `join_channel()` clears draft. Spec scenario added.
- ["Connected to #chan" no longer visible anywhere on the bottom screen] → acceptable: join events appear as System chat lines; top screen shows the connection dot (main.c:876). If users miss it, a follow-up can add a slim status strip — out of scope.
- [DCF poll-failure and auto-reconnect failure status invisible] → accepted for this change. Polls/reconnects auto-retry; final failures reach STATE_ERROR (visible on top screen). Follow-up can add `chat_push("System", ...)` if critical.
- [swkbd initial text >127 bytes] → impossible: draft buffer is the same 128-byte `app.input` the keyboard writes back into; `swkbdInit` maxlen stays `len-1`.
- [UTF-8 tail-trim off-by-one corrupting glyphs] → trim loop skips continuation bytes; spec scenario covers it; verify with a multi-byte draft on hardware.
- [Behavior change surprises existing users (A used to send)] → intended fix; B-to-clear and SEND placement unchanged; new placeholder text teaches the flow.

## Migration Plan

Single-file UI change; no data migration. Build with devkitARM, test on hardware:
1. Tap box → keyboard → OK → text visible, nothing sent.
2. Tap box again → keyboard pre-filled → edit → OK → updated draft shown.
3. SEND → message appears in chat under own nick, box shows placeholder.
4. SEND with empty box → keyboard opens; OK stores draft only.
5. B clears; tab switch round-trip preserves draft; logged-out send preserves draft; draft with `%s` renders verbatim.
6. Join new channel with draft → draft cleared, placeholder shown in new channel's chat tab.
7. (Optional if testable) disconnect WiFi mid-send → no echo, draft preserved.
Rollback: revert the commit.

## Open Questions

- None blocking. (Placeholder wording "Touch here to chat..." final unless user objects.)
