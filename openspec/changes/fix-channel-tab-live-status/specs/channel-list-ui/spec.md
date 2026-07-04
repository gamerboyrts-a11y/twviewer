## ADDED Requirements

### Requirement: Channel list rendering bounds
The channel list rendering area SHALL extend from the top of the content area down to `CHAT_BOT - 22` pixels, ensuring no overlap with the Clear History button positioned at `CHAT_BOT - 18`.

#### Scenario: List renders within bounds
- **WHEN** the Channels tab is displayed
- **THEN** the last visible channel entry SHALL stop at or before `CHAT_BOT - 22` pixels
- **THEN** no channel entry SHALL overlap with the Clear History button

#### Scenario: Adequate spacing for button
- **WHEN** the channel list is fully rendered
- **THEN** there SHALL be at least 4 pixels of vertical space between the last channel row and the Clear History button

### Requirement: Live status indicator display
The system SHALL display a red circle icon next to live channels and no icon next to offline channels.

#### Scenario: Live channel icon rendering
- **WHEN** a channel in the list has `is_live` set to true
- **THEN** a red circle icon SHALL be drawn to the left of the channel name

#### Scenario: Offline channel has no icon
- **WHEN** a channel in the list has `is_live` set to false
- **THEN** no status icon SHALL be drawn for that channel

#### Scenario: Icon does not obscure channel name
- **WHEN** a live channel is rendered with the red circle icon
- **THEN** the channel name SHALL remain fully readable with no text overlap

### Requirement: D-pad scroll navigation
The system SHALL support D-pad Up and D-pad Down button presses to scroll through the channel list when it exceeds the visible area.

#### Scenario: Scroll down with D-pad Down
- **WHEN** the user presses D-pad Down in the Channels tab
- **THEN** the scroll offset SHALL increment by one row
- **THEN** the next channel SHALL become visible at the bottom of the list

#### Scenario: Scroll up with D-pad Up
- **WHEN** the user presses D-pad Up in the Channels tab
- **THEN** the scroll offset SHALL decrement by one row
- **THEN** the previous channel SHALL become visible at the top of the list

#### Scenario: Scroll bounds at top
- **WHEN** the scroll offset is at 0 (top of list)
- **WHEN** the user presses D-pad Up
- **THEN** the scroll offset SHALL remain at 0 and the list SHALL not scroll further upward

#### Scenario: Scroll bounds at bottom
- **WHEN** the scroll offset is at the maximum position (last channel visible)
- **WHEN** the user presses D-pad Down
- **THEN** the scroll offset SHALL not increase and the list SHALL not scroll further downward

#### Scenario: List wrap behavior disabled
- **WHEN** the user attempts to scroll past the top or bottom of the channel list
- **THEN** the scroll position SHALL clamp to the valid range without wrapping to the opposite end

### Requirement: Live-first sorting with viewer count
The channel list SHALL be sorted with live channels first (ordered by viewer count descending), followed by offline channels.

#### Scenario: Live channels appear before offline channels
- **WHEN** the channel list contains both live and offline channels
- **THEN** all live channels SHALL appear before any offline channels in the list

#### Scenario: Live channels sorted by viewer count descending
- **WHEN** multiple channels are live
- **THEN** channels with higher `viewer_count` SHALL appear before channels with lower `viewer_count`

#### Scenario: Offline channels sorted consistently
- **WHEN** multiple channels are offline
- **THEN** offline channels SHALL be sorted by name alphabetically or by history order

#### Scenario: Single live channel at top
- **WHEN** only one channel is live among many offline channels
- **THEN** that live channel SHALL appear as the first entry in the list

### Requirement: Channel metadata storage
Each channel entry in `app.history[]` SHALL include `is_live` (boolean) and `viewer_count` (u32) fields.

#### Scenario: Live channel metadata populated
- **WHEN** a channel is fetched and determined to be live
- **THEN** the channel's `is_live` field SHALL be set to true
- **THEN** the channel's `viewer_count` field SHALL contain the current viewer count from the API

#### Scenario: Offline channel metadata populated
- **WHEN** a channel is fetched and determined to be offline
- **THEN** the channel's `is_live` field SHALL be set to false
- **THEN** the channel's `viewer_count` field MAY be set to 0 or left unchanged

### Requirement: Empty history handling
The system SHALL handle an empty channel history gracefully without rendering errors.

#### Scenario: Empty history display
- **WHEN** `app.history[]` contains no entries
- **THEN** the Channels tab SHALL display an empty list or a message indicating no channels
- **THEN** no rendering errors or crashes SHALL occur

#### Scenario: D-pad navigation with empty history
- **WHEN** `app.history[]` is empty
- **WHEN** the user presses D-pad Up or Down
- **THEN** no scroll action SHALL be taken and no errors SHALL occur

### Requirement: All offline channels handling
The system SHALL display the channel list correctly when all channels are offline.

#### Scenario: All offline channels rendering
- **WHEN** all channels in `app.history[]` have `is_live` set to false
- **THEN** the list SHALL render all channels without any red circle icons
- **THEN** channels SHALL be sorted by name or history order

### Requirement: All live channels handling
The system SHALL display the channel list correctly when all channels are live.

#### Scenario: All live channels rendering
- **WHEN** all channels in `app.history[]` have `is_live` set to true
- **THEN** the list SHALL render all channels with red circle icons
- **THEN** channels SHALL be sorted by `viewer_count` descending

### Requirement: Live status fetch failure handling
The system SHALL handle failures in fetching live status data without crashing or displaying incorrect information.

#### Scenario: API fetch failure
- **WHEN** the bulk live status API call fails
- **THEN** existing channel list SHALL remain displayed with previously cached live status
- **THEN** no crash or rendering error SHALL occur

#### Scenario: Partial API response
- **WHEN** the API returns live status for only some channels
- **THEN** channels with returned status SHALL display correct icons and sort positions
- **THEN** channels without returned status SHALL default to offline state or retain cached state
