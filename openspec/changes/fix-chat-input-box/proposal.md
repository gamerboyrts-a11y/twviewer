# Fix chat button and add persistent chat input box

## Why

Sending a chat message today is a blind, single-shot action: tapping SEND (or pressing A) opens the software keyboard and the message is sent the instant the keyboard closes — the user never sees the typed text in the app before it goes out, and there is no way to review or edit a drafted message. The left side of the input bar shows a status string ("Connected to #channel") instead of behaving like a chat input box, which users expect to be touchable.

## What Changes

- The left region of the chat-tab input bar becomes a real chat input box:
  - Touching it opens the software keyboard, pre-filled with the current draft.
  - Confirming the keyboard stores the text as a draft — it does NOT send.
  - The draft is displayed in the input box (with a caret), so the user sees what they typed before sending.
- The SEND button sends the currently drafted text (and clears the draft). It no longer opens the keyboard and fires immediately in one step. If the draft is empty, SEND opens the keyboard to compose (confirm stores the draft for review; nothing is sent until SEND is tapped with a non-empty draft).
- Button mapping on the chat tab follows the same model: A opens the keyboard to compose/edit the draft, B clears the draft (existing behavior kept).
- Status text ("Connected to …") no longer occupies the input box. Connection status is conveyed via System lines in the chat log (already emitted today) — the input box always looks and acts like an input box.
- Fixes a latent format-string bug: `app.status_msg` is currently passed as a printf format to `snprintf` when rendering the input bar (source/main.c:938); any `%` in a status message is undefined behavior. The new rendering never uses non-literal format strings.
- Draft text wider than the box renders as its tail end (leading ellipsis) so the most recently typed characters and the caret stay visible.

## Capabilities

### New Capabilities

- `chat-input`: Chat-tab message composition and sending on the bottom screen — touchable input box, draft persistence/display, keyboard pre-fill, SEND semantics, button (A/B) mapping, logged-out behavior, and input-bar status handling.

### Modified Capabilities

_None — no existing specs (openspec/specs/ is empty)._

## Impact

- `source/main.c` only:
  - `draw_chat_tab()` — input bar rendering (box, placeholder, draft tail, SEND button state).
  - `handle_touch()` TAB_CHAT branch — new input-box hit region; SEND sends draft.
  - `handle_buttons()` TAB_CHAT branch — A composes/edits instead of compose-and-send.
  - `swkbd_get()` — gains an initial-text parameter (swkbdSetInitialText) so editing pre-fills the draft; other call sites pass empty initial text.
- No changes to IRC protocol code, video, audio, settings, or persisted files.
- No new dependencies. UI-only change on the bottom screen chat tab.
