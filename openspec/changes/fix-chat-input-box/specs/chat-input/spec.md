# chat-input — Delta Spec

## ADDED Requirements

### Requirement: Touchable chat input box
The chat tab input bar SHALL contain a visually distinct input box occupying the area left of the SEND button. Touching the input box SHALL open the software keyboard pre-filled with the current draft text. The input box SHALL NOT display connection status text; it SHALL display only the draft or a placeholder.

#### Scenario: Tap input box with empty draft
- **WHEN** the user taps the input box and no draft exists
- **THEN** the software keyboard opens with empty content and a "Type a message" hint

#### Scenario: Tap input box with existing draft
- **WHEN** the user taps the input box while a draft exists
- **THEN** the software keyboard opens pre-filled with the draft so it can be edited

#### Scenario: Input box shows placeholder, not status
- **WHEN** the chat tab is visible and the draft is empty
- **THEN** the input box shows a neutral placeholder (e.g., "Touch here to chat...") and never the "Connected to ..." status string

### Requirement: Draft persistence and display
Confirming the software keyboard SHALL store the entered text as a draft without sending it. The draft SHALL be rendered inside the input box with a trailing caret so the user can review it before sending. The draft SHALL persist across tab switches and frames until sent or cleared. The draft SHALL be cleared when joining a new channel to prevent sending messages intended for one channel into another.

#### Scenario: Keyboard confirm stores draft
- **WHEN** the user confirms the keyboard with non-empty text
- **THEN** the text is stored as the draft, displayed in the input box, and no IRC message is sent

#### Scenario: Keyboard cancel preserves draft
- **WHEN** the user cancels the keyboard (or confirms empty text)
- **THEN** the previously stored draft remains unchanged

#### Scenario: Draft survives tab switch
- **WHEN** the user switches to another tab and returns to the chat tab with a draft stored
- **THEN** the draft is still displayed in the input box

#### Scenario: Draft cleared on channel join
- **WHEN** the user joins a new channel while a draft exists
- **THEN** the draft is cleared and the input box shows the placeholder in the new channel's chat tab

#### Scenario: Long draft shows tail
- **WHEN** the draft is wider than the input box
- **THEN** the box renders the tail end of the draft with a leading ellipsis, keeping the most recent characters and caret visible, without splitting a multi-byte UTF-8 character

### Requirement: SEND button sends the draft
Tapping SEND with a non-empty draft SHALL send the draft as an IRC PRIVMSG to the current channel, echo it into the local chat log, and clear the draft. Tapping SEND with an empty draft SHALL open the software keyboard to compose; confirming stores the draft for review and does not send.

#### Scenario: Send non-empty draft
- **WHEN** the user taps SEND while a non-empty draft exists, logged in and IRC-connected
- **THEN** the message is sent to the channel, appears in the chat log under the user's nick, and the input box returns to placeholder state

#### Scenario: SEND with empty draft composes
- **WHEN** the user taps SEND while the draft is empty
- **THEN** the software keyboard opens; confirming stores the text as a draft and nothing is sent

#### Scenario: Send while logged out
- **WHEN** the user taps SEND with a non-empty draft while not logged in
- **THEN** a System line "Login to send messages" appears, no IRC message is sent, and the draft is preserved

#### Scenario: Send while IRC disconnected
- **WHEN** the user taps SEND with a non-empty draft while IRC is not connected
- **THEN** no message is sent and the draft is preserved (it can be sent after automatic reconnect)

#### Scenario: Send while TLS write fails
- **WHEN** the user taps SEND with a non-empty draft and the underlying TLS socket write fails or returns an error
- **THEN** no local echo appears in the chat log, the draft is preserved, and the send operation returns failure

### Requirement: Chat tab button mapping
On the chat tab, pressing A SHALL behave identically to tapping the input box (compose/edit the draft; confirm stores, does not send). Pressing B SHALL clear the draft.

#### Scenario: A composes without sending
- **WHEN** the user presses A on the chat tab and confirms the keyboard with text
- **THEN** the text is stored and displayed as the draft, and no message is sent

#### Scenario: B clears draft
- **WHEN** the user presses B on the chat tab while a draft exists
- **THEN** the draft is cleared and the input box shows the placeholder

### Requirement: Input bar rendering is format-string safe
The input bar renderer SHALL NOT pass runtime-provided strings (status messages, drafts, chat text) as printf-style format arguments; all format strings SHALL be literals.

#### Scenario: Draft containing percent signs
- **WHEN** the draft or a status message contains `%` characters (e.g., "100%s")
- **THEN** the input bar renders the text verbatim with no crash or garbage output
