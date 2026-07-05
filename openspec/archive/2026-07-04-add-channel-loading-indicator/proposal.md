## Why

When user switch channel from Channels tab, app continue show previous channel video/UI until new stream playback begin. No visual feedback that channel switch happening. User experience confusing — unclear if tap registered or stream loading.

## What Changes

- Add loading indicator display when channel switch initiated from Channels tab
- Show indicator immediately after channel selection
- Hide indicator when new stream playback begins or error occurs
- Maintain existing channel switch behavior (no functional changes to video pipeline)

## Capabilities

### New Capabilities
- `channel-loading-state`: Visual feedback system for channel switching operations. Covers loading indicator display logic, timing (show on switch initiation, hide on playback start or error), and UI integration with existing channel tab flow.

### Modified Capabilities
<!-- No existing specs require requirement changes. This is pure UI addition. -->

## Impact

**Affected Code:**
- `source/main.c` — Channel tab selection handler, UI rendering for loading state
- Possibly `source/video.c` — Signal when video pipeline starts (if not already exposed)

**User-facing:**
- Improved UX clarity during channel switches
- No breaking changes to existing functionality
