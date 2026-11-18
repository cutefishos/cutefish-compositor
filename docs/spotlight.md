# Koifish Spotlight

Spotlight is drawn inside the compositor. It replaces the fullscreen Launchpad and the separate clipboard picker in the session; it does not replace or capture the desktop background. The backdrop uses the existing FishUI PopupGlass implementation with a neutral light/dark tint.

- **Super+Space** toggles general search. In Cutefish's Command convention use **Command+Space** (the key next to Space).
- The dock's application button (and a tap of Super in the PC convention) opens **Applications** directly. Applications, Files, Actions and Clipboard are also available from the four buttons next to the search field, or by pressing **Super+1…4** while Spotlight is open (**Command+1…4** in the Command convention).
- **Arrow keys** navigate results, **Enter** opens the selection, and **Esc** dismisses the panel. Right-click a result for its context menu. In Files, select a folder and press **Tab** to search within it; file results can also be dragged into other applications.
- Applications uses the desktop-entry registry. Its five-column grid, list preference and recently launched apps live in `~/.config/cutefish/spotlight.conf`. Categories can be combined with text search. The file index is in RAM, scans visible entries under the user's home directory in the background, skips symlinks and large dependency subtrees, and is bounded (depth 7, 35,000 visited entries). It matches file names, not contents. Recents in Files are ordered by file modification time.
- Actions displays built-in Linux actions and installed `.desktop` actions. Quick keys are editable by clicking the chip beside an action. Compose Email prepares a `mailto:` draft in the default client; it does not send mail. There is no App Intents, calendar/route provider, contact autocomplete or iPhone app mirroring.
- Clipboard uses the compositor's existing payload store and pastes back into the previous application. Its `…` menu can clear history or disable recording (which also clears history). Entries expire after eight hours; large and marked-sensitive selections follow Cutefish's existing exclusions.

The legacy `org.cutefish.Launchpad` and `org.cutefish.ClipboardHistory` session D-Bus entry points remain compatible. `org.cutefish.Spotlight` provides `show`, `hide`, `toggle` on `/Spotlight`.

Build and run the targeted tests from a configured build tree:

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --target cutefish-compositor spotlight_test accelerator_test
ctest --test-dir build -R '^(spotlight_test|accelerator_test)$' --output-on-failure
```
