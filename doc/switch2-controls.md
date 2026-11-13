# Switch 2 controls

Native libounce HID supplies physical Nintendo button names. SDL controller events are disabled on Switch 2 to avoid duplicate handling; desktop input is unchanged.

| Control | Action |
| --- | --- |
| Left stick or D-pad | WASD movement, up/down/duck |
| D-pad Down | Also hold Left Shift; left-stick Down does not |
| Right stick | Cursor movement with 12% radial deadzone and fine center response |
| ZR | Left mouse button, including hold/drag |
| ZL | Right mouse button, including hold |
| Y | E / interact immediately |
| Hold Y longer than two seconds | Release E and hold Left Alt until Y is released |
| B | F / ability |
| A | Space / jump |
| L / R | One wheel up/down hotbar step per press |
| X | Escape / pause/back |
| Minus | Open native keyboard for chat/commands during gameplay |
| Plus | Unassigned |
| Touch | Position cursor; release a stationary tap within 0.5 s to left-click |

A swipe beyond 12 pixels in HID reference space, long touch, extra finger or focus loss cancels tap-to-click. HID coordinates are normalized to the selected 720p/1080p render size. Right-stick speed scales with resolution. Analog movement uses 35% press / 25% release hysteresis independently of D-pad state. Either movement source can keep the same direction held.

Minus releases synthetic movement, mouse buttons and modifiers before opening the blocking native keyboard. Done submits a nonempty result through MainInterface::doChat, preserving slash-command processing, send mode and normal history. Cancel or an empty result sends nothing. Chat input is cleared and unfocused on return. Buttons held when the applet closes are ignored until physically released, preventing Done/Cancel button presses leaking into gameplay. Holding Minus across the overlay cannot reopen it. Minus does not open chat from the title/character menus.

Mappings use default keyboard bindings; rebinding WASD/E/F/Space/Escape/Left Alt/Left Shift changes their resulting actions. Long Y retains the initial interaction rather than delaying ordinary taps. Focus loss/disconnection releases held state and resets its timer.

## Validation

Host tests cover stick/D-pad handoff and diagonals, ability state, Y threshold/Alt lifetime, focus/disconnect cancellation, Minus edge retention across applets, existing touch gestures, cursor scaling and held button releases. Both native resolutions must compile/link/package successfully before delivery. The native keyboard and actual command/message dispatch require console validation.

## Console acceptance

1. Use X to pause and back out of menus; confirm Plus no longer pauses.
2. Walk with D-pad and stick, including holding one while releasing the other. Check diagonals and ducking.
3. Tap Y to interact. Hold Y past two seconds and confirm the default Left Alt action; release Y and check it stops. Repeat HOME/resume while holding Y.
4. Press/hold/release B to test the default F ability. Confirm A jump, triggers, hotbar and touch still work.
5. In a local world press Minus, type a message and select Done. Confirm exactly one message is sent and chat loses focus. Repeat with a harmless command such as /help.
6. Test Cancel, empty Done, held Minus, and movement/Y held before opening chat. No unintended message or stuck input should remain.
7. Quit normally and fully close the host title before retrieving logs over FTP.
