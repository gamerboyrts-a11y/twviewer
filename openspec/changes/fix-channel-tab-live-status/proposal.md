## Why

The Channels tab list currently overlaps with the Clear History button (last row extends 16px into button area), shows no live/offline status indicators, lacks scrolling support, and displays channels in arbitrary order rather than prioritizing live streams.

## What Changes

- Extend channel list rendering area down to `CHAT_BOT - 22` (stopping before Clear History button at `CHAT_BOT - 18`) to eliminate overlap
- Add red circle icon for live channels, no icon for offline channels
- Implement D-pad Up/Down scrolling to navigate through channel history
- Sort channel list with live channels first (by viewer count descending), followed by offline channels
- Add per-channel live status metadata (is_live, viewer_count) to `app.history[]` entries
- Fetch bulk live status from Twitch API on tab switch or periodic refresh

## Capabilities

### New Capabilities
- `channel-list-ui`: Channel list rendering with scroll offset, live status icons (red circle for live, none for offline), D-pad navigation, live-first sorting by viewer count

### Modified Capabilities
<!-- No existing capabilities require spec-level requirement changes -->

## Impact

- **Code**: `source/main.c`
  - `draw_channels_tab()`: list rendering bounds, scroll offset, live icon drawing, sorted rendering
  - `handle_touch()` and `handle_buttons()`: D-pad Up/Down handling for `TAB_CHANNELS`
  - `app.history[]` structure: add `bool is_live` and `u32 viewer_count` fields
- **APIs**: New Twitch Helix API call for bulk live status (`GET /streams?user_login=...&user_login=...`)
- **Dependencies**: Existing curl/mbedtls for HTTPS requests, GraphQL token reuse
