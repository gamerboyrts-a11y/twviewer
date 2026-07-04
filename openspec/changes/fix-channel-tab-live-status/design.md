## Context

The Channels tab in Twitch3DS currently displays a simple history list of channel names (`app.history[]`) rendered from `CHAT_TOP + 44` downward, stopping when `y > CHAT_BOT - 20`. Each row is 20px tall (18px content + 2px gap). The Clear History button sits at `CHAT_BOT - 18`, creating a 16px overlap zone where the last rendered row can extend into the button area.

Current limitations:
- No live/offline status indicators
- No scrolling support when history exceeds visible area
- Channels displayed in arbitrary order (history insertion order)
- Rendering bounds allow overlap with Clear History button

**Constraints:**
- New 3DS hardware (limited CPU, runs on devkitARM/libctru)
- Existing OAuth token infrastructure via GraphQL (reusable for Helix API)
- UI rendering via citro2d in main thread
- Limited screen real estate (bottom screen: 320x240)

## Goals / Non-Goals

**Goals:**
- Eliminate Clear History button overlap by stopping list rendering at `CHAT_BOT - 22`
- Display live status with red circle icon (3px radius) for live channels
- Enable D-pad Up/Down scrolling through channel history
- Sort channels live-first (by viewer count descending), then offline channels
- Fetch bulk live status from Twitch Helix API with caching to minimize rate limit impact

**Non-Goals:**
- Real-time live status updates (polling interval is acceptable)
- Touch scrolling (D-pad only)
- Viewer count text display (icon only, count used for sorting)
- Historical viewer count tracking
- Offline channel sub-sorting by viewer count (alphabetical or history order is sufficient)

## Decisions

### 1. Live Status API: Twitch Helix GET /streams (bulk query)

**Decision:** Use `GET https://api.twitch.tv/helix/streams?user_login=channel1&user_login=channel2&...` to fetch live status for all channels in `app.history[]` in a single request.

**Rationale:**
- Helix `/streams` endpoint supports up to 100 `user_login` query parameters per request
- Single bulk request minimizes network overhead and rate limit consumption vs. per-channel requests
- Returns `viewer_count` and live status atomically
- Existing OAuth token from GraphQL flow works with Helix (same `Authorization: Bearer <token>` header)

**Alternatives considered:**
- EventSub webhooks: Requires persistent server, unsuitable for client-only homebrew
- Per-channel `/streams` requests: 100x more rate limit usage, slower
- GraphQL streams query: Helix is the stable, documented Twitch API for stream data

**Implementation notes:**
- Response JSON contains array of live stream objects; channels not in response are offline
- Cache response for 60 seconds (global `last_status_fetch` timestamp)
- Trigger fetch on tab switch to Channels tab or manual refresh (A button)

### 2. Data Model: Extend `app.history[]` with live metadata

**Decision:** Add two fields to each history entry struct:
```c
typedef struct {
    char name[256];      // existing
    bool is_live;        // NEW
    u32 viewer_count;    // NEW
} ChannelHistoryEntry;
```

**Rationale:**
- Storing metadata per-entry avoids separate parallel arrays and index mismatches
- `bool is_live` explicit flag simplifies rendering logic (no need to check `viewer_count > 0`)
- `u32 viewer_count` sufficient for Twitch's viewer counts (max ~200k typical)

**Alternatives considered:**
- Separate `live_status[]` array: Fragile (requires index sync), more error-prone
- Single global live channels list: Loses history context, complicates UI

**Migration:**
- Initialize `is_live = false`, `viewer_count = 0` for existing history entries
- Backwards compatible (no on-disk history persistence in current implementation)

### 3. Rendering: Stop at CHAT_BOT - 22, red circle for live

**Decision:**
- Change loop condition from `y > CHAT_BOT - 20` to `y > CHAT_BOT - 22`
- Render red `C2D_DrawCircleSolid(x_icon, y_center, 3.0f, C2D_Color32(255, 0, 0, 255))` 8px left of channel name if `is_live == true`
- No icon for offline channels

**Rationale:**
- 4px gap (`CHAT_BOT - 22` to `CHAT_BOT - 18`) ensures clear visual separation from button
- Red circle is simple, instantly recognizable, minimal rendering cost
- 3px radius fits comfortably in 18px row height with 6px vertical margins

**Alternatives considered:**
- Text label ("LIVE"): Takes horizontal space, harder to read at small size
- Green circle: Red is standard Twitch live indicator color
- Filled rectangle: Less visually distinct than circle

### 4. Scrolling: Integer offset, D-pad Up/Down, clamped bounds

**Decision:**
- Add `int channels_scroll_offset` to app state (global or per-tab)
- D-pad Down: `channels_scroll_offset++`, clamp to `max(0, app.history_count - visible_rows)`
- D-pad Up: `channels_scroll_offset--`, clamp to `0`
- Render from `app.history[channels_scroll_offset]` onward

**Rationale:**
- Integer row offset is simplest scrolling model (no sub-pixel smoothing needed)
- Clamping prevents out-of-bounds access and eliminates wrap-around behavior
- `visible_rows = (CHAT_BOT - 22 - (CHAT_TOP + 44)) / 20` calculated at runtime

**Alternatives considered:**
- Pixel-based scrolling: Overkill for text list, adds complexity
- Touchscreen dragging: D-pad is primary input for homebrew, touch is secondary
- Page-based scrolling: Less precise control for small lists

**Edge cases:**
- Empty history: `visible_rows >= history_count`, no scrolling (both buttons no-op)
- Single page: Same as empty history, `channels_scroll_offset` stays 0

### 5. Sorting: qsort with stable live-first comparator

**Decision:** Sort `app.history[]` in-place using stdlib `qsort` with comparator:
1. Live channels before offline channels (`is_live` bool comparison)
2. Among live channels, higher `viewer_count` first (descending)
3. Among offline channels, preserve original history order (stable sort via index tiebreaker)

**Rationale:**
- `qsort` is stdlib, no new dependencies
- Live-first sorting surfaces active streams immediately (primary user goal)
- Viewer count descending shows most popular streams first
- Stable sort for offline channels preserves chronological history context

**Alternatives considered:**
- Insertion sort: O(n²), slower for large histories (though n ≤ MAX_HISTORY ~100)
- Two-pass render (live then offline): Complicates scroll offset math, harder to debug
- Alphabetical offline sort: Loses temporal context, less useful for "recent channels"

**Implementation notes:**
- Sort on every draw if data changed, OR cache sorted indices and only re-sort on status update
- Comparator must handle tie-breaking: `(a.is_live == b.is_live && a.viewer_count == b.viewer_count) ? index_a - index_b : ...`

## Risks / Trade-offs

### Risk: Twitch API rate limiting
- **Mitigation:** 60-second cache per bulk fetch, fetch only on tab switch or manual refresh (not on every frame). Single bulk request per fetch minimizes API calls.

### Risk: OAuth token expiry mid-session
- **Mitigation:** Handle 401 response by invalidating token and displaying error message. Reuse existing token refresh flow if available, else prompt re-login.

### Risk: Sort instability causing visual churn
- **Scenario:** Channel goes live/offline during rendering, sort order changes mid-draw.
- **Mitigation:** Sort only when status data updated, not on every frame. Atomic update of `app.history[]` metadata before sorting.

### Risk: Large history (100+ channels) causing scroll lag
- **Mitigation:** qsort is O(n log n), acceptable for n ≤ 100. Rendering loop already bounded by visible rows (~10-12), no performance impact.

### Trade-off: No real-time status updates
- **Accepted:** 60-second cache means live status can be stale. Acceptable for homebrew UX; manual refresh (A button) provides escape hatch.

### Trade-off: D-pad only, no touch scrolling
- **Accepted:** Touch scrolling adds complexity (drag state, momentum). D-pad is sufficient and standard for homebrew navigation.

## Migration Plan

**Development steps:**
1. Extend `app.history[]` struct definition with `is_live` and `viewer_count` fields
2. Implement Helix `/streams` bulk fetch function (reuse curl/mbedtls from existing GraphQL code)
3. Add 60-second cache logic with `last_status_fetch` timestamp
4. Implement qsort comparator and integrate into `draw_channels_tab()` pre-render
5. Add `channels_scroll_offset` state variable and D-pad Up/Down handlers
6. Update rendering loop: start from `scroll_offset`, stop at `CHAT_BOT - 22`
7. Add red circle rendering for live channels (`C2D_DrawCircleSolid`)

**Testing checklist:**
- Scroll bounds: Verify no crash at top/bottom, no wrap-around
- Icon rendering: Red circle appears for live channels, correct positioning (no overlap with text)
- Sort stability: Live channels always first, viewer count descending among live, offline order consistent
- Empty history: No crash, no scroll
- All-live/all-offline: Correct rendering and sorting
- API failure: Graceful fallback, no crash
- Button overlap: Last row ends before `CHAT_BOT - 22`, Clear History button fully clickable

**Rollback strategy:**
- If Helix API fails: Display channels unsorted with no icons (degraded but functional)
- If scroll causes crashes: Disable D-pad handlers, render from offset 0
- Git: Revert commit if blocking issues found during user testing

## Open Questions

- **MAX_HISTORY value:** Not visible in provided code range. If `MAX_HISTORY > 100`, need paginated Helix requests (100 user_login params max per call).
- **Offline channel sort order:** Alphabetical vs. original history order? Proposal says "or by history order" — need user preference or default decision.
- **Manual refresh trigger:** A button, X button, or touchscreen icon? Need input mapping decision.
- **Token refresh flow:** Does existing GraphQL code handle 401 refresh? If not, need explicit error handling.
