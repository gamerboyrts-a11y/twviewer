## 1. Data Structure Updates

- [x] 1.1 Extend ChannelHistoryEntry struct with `bool is_live` and `u32 viewer_count` fields
- [x] 1.2 Add `int channels_scroll_offset` to App struct
- [x] 1.3 Initialize new fields to safe defaults (is_live=false, viewer_count=0, scroll_offset=0)

## 2. Live Status API Integration

- [ ] 2.1 Implement fetch_live_status() function to call Helix GET /streams endpoint
- [ ] 2.2 Build bulk query string with user_login parameters from app.history[]
- [ ] 2.3 Configure curl with OAuth Bearer token and Client-ID headers
- [ ] 2.4 Parse JSON response to extract is_live and viewer_count for each channel
- [ ] 2.5 Update app.history[] entries with fetched live status data
- [ ] 2.6 Add 60-second cache mechanism using last_fetch_tick timestamp
- [ ] 2.7 Handle API failures gracefully (retain existing data, log error)

## 3. Channel Sorting Logic

- [ ] 3.1 Implement qsort comparator function for live-first sorting
- [ ] 3.2 Primary sort: live channels before offline channels (is_live comparison)
- [ ] 3.3 Secondary sort: among live channels, higher viewer_count first (descending)
- [ ] 3.4 Tertiary sort: among offline channels, maintain original index order
- [ ] 3.5 Integrate qsort call into draw_channels_tab() before rendering

## 4. Rendering Updates

- [ ] 4.1 Change list rendering loop condition from `y > CHAT_BOT - 20` to `y > CHAT_BOT - 22`
- [ ] 4.2 Calculate visible_rows from rendering bounds
- [ ] 4.3 Apply channels_scroll_offset to determine first rendered entry
- [ ] 4.4 Draw red circle (C2D_DrawCircleSolid, 3px radius, red) for live channels
- [ ] 4.5 Position circle 8px left of channel name text
- [ ] 4.6 Ensure no icon rendered for offline channels

## 5. Scroll Input Handling

- [ ] 5.1 Add D-pad Down handler to increment channels_scroll_offset
- [ ] 5.2 Add D-pad Up handler to decrement channels_scroll_offset
- [ ] 5.3 Clamp scroll_offset to [0, max(0, history_count - visible_rows)]
- [ ] 5.4 Trigger live status refresh on tab switch to Channels tab
- [ ] 5.5 Add manual refresh on Y button press (respect 60s cache)

## 6. Build and Verification

- [ ] 6.1 Build with make and verify no compilation errors
- [ ] 6.2 Test scroll navigation: D-pad Up/Down responsiveness, bounds clamping
- [ ] 6.3 Test live status icons: red circle appears for live channels only
- [ ] 6.4 Test sorting: live channels first, sorted by viewer count descending
- [ ] 6.5 Test edge cases: empty history, all-live, all-offline
- [ ] 6.6 Test API failure handling: no crash, fallback to cached/default state
- [ ] 6.7 Verify no overlap between last channel row and Clear History button
