# Tasks — fix-chat-input-box

## 1. Keyboard helper

- [x] 1.1 Add `initial` parameter to `swkbd_get()` (source/main.c:468); call `swkbdSetInitialText()` when non-NULL/non-empty
- [x] 1.2 Update the two channel-search call sites (main.c:1048, main.c:1116) to pass `NULL` initial text

## 2. Send path

- [x] 2.1 Change `irc_send_msg()` (main.c:418) to return `bool`: true only when the PRIVMSG was successfully written to the TLS socket (check `mbedtls_ssl_write` return at main.c:423); false on not-connected, not-logged-in, or write failure (keep the "Login to send messages" System line)
- [x] 2.2 Add `app.input[0] = '\0'` in `join_channel()` (main.c:487-519) after clearing chat state, before `app.tab = TAB_CHAT` (main.c:518) to prevent wrong-channel sends

## 3. Input handling

- [x] 3.1 Rewrite `handle_touch()` TAB_CHAT (main.c:1027): SEND rect sends `app.input` when non-empty (clear draft only on successful send); empty draft falls back to compose; add input-box hit region `(2, INPUT_BAR_Y+2, BOT_W-52, INPUT_BAR_H-4)` that opens swkbd pre-filled with `app.input` and stores the confirmed text without sending; `return` after handling any input-bar tap
- [x] 3.2 Rewrite `handle_buttons()` TAB_CHAT KEY_A (main.c:1100): compose/edit draft (same as input-box tap), no send; keep KEY_B clear

## 4. Rendering

- [x] 4.1 Rewrite input bar in `draw_chat_tab()` (main.c:934): draw inset input box with `COL_DIVIDER` outline; draft rendered with literal `"%s_"` format; gray placeholder "Touch here to chat..." when empty; remove `app.status_msg` from the bar (fixes format-string bug at main.c:938)
- [x] 4.2 Implement UTF-8-safe tail-trim: measure draft with `C2D_TextFontParse`/`C2D_TextGetDimensions`; when wider than box interior, render "..." + suffix that fits, advancing over continuation bytes

## 5. Build and verify on hardware

- [x] 5.1 Build clean with devkitARM (`make`)
- [x] 5.2 Verify spec scenarios: tap-box compose; pre-filled edit; SEND sends and clears; empty-SEND composes; cancel preserves draft; B clears; draft survives tab switch; channel-join clears draft; logged-out send preserves draft; `%s` in draft renders verbatim; long/multi-byte draft tail-trims cleanly
- [x] 5.3 Self-review diff against spec + design, then commit and push (git backup policy)
