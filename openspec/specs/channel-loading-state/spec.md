# Channel Loading State

## Purpose

Visual feedback system for channel switching operations in Twitch3DS. Provides loading indicator to show stream load progress between channel selection and first frame display.

## Requirements

### Requirement: Loading indicator display on channel switch

The system SHALL display a loading indicator on the bottom screen immediately when user initiate channel switch from Channels tab, and SHALL hide the indicator once first video frame available or error occur.

#### Scenario: User select channel from history list

- **WHEN** user press A button on channel in Channels tab history list
- **THEN** system display "Loading stream..." text centered on bottom screen
- **THEN** system switch to Chat tab
- **THEN** loading indicator remain visible until first video frame decoded OR error state reached

#### Scenario: Loading indicator hide on first frame ready

- **WHEN** loading indicator visible
- **WHEN** video pipeline decode and upload first frame (video_has_picture() return true)
- **THEN** system hide loading indicator
- **THEN** user see new channel video on top screen

#### Scenario: Loading indicator hide on stream error

- **WHEN** loading indicator visible
- **WHEN** stream fail to load (offline channel, network error, authentication failure)
- **THEN** system hide loading indicator
- **THEN** system display error message on top screen

#### Scenario: No loading indicator on initial app launch

- **WHEN** app start with saved channel
- **WHEN** video pipeline start initial stream
- **THEN** system SHALL NOT display loading indicator (user already see channel list UI)

### Requirement: Loading state persistence

The system SHALL maintain loading state flag that survive across tab switches during load period.

#### Scenario: User switch tabs during channel load

- **WHEN** loading indicator active
- **WHEN** user press X or Y to switch to Settings or Channels tab
- **THEN** loading indicator remain in loading state (flag not cleared)
- **WHEN** user return to Chat tab
- **THEN** loading indicator still visible if stream not yet loaded

#### Scenario: Loading state clear on video stop

- **WHEN** loading indicator active for channel A
- **WHEN** user immediately select different channel B before A finish loading
- **THEN** system clear loading state for channel A
- **THEN** system set new loading state for channel B
