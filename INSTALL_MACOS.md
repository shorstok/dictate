# dictate on macOS — install and setup

What to do once `cmake --build --preset mac-release` has produced
`out/build/mac-release/dictate_cpp.app`. For building it in the first place see
[README.md](README.md); for how the macOS layer works internally see
[MACOS.md](MACOS.md).

## 1. Install to a stable location

macOS keys privacy permissions to the app's **path and code signature**. If you
run dictate straight out of `out/build/…`, a `rm -rf out` costs you all three
grants and you get to click through the prompts again. Copy it somewhere stable
once and always launch it from there:

```sh
cp -R out/build/mac-release/dictate_cpp.app /Applications/dictate.app
```

`~/Applications` works just as well if you'd rather not touch `/Applications`.
Locally built apps carry no quarantine flag, so Gatekeeper will not block them —
you only need `xattr -dr com.apple.quarantine` if you copied the bundle from
another machine.

## 2. Set your OpenAI API key

dictate reads `OPENAI_API_KEY` from the environment at transcription time. A GUI
app inherits **nothing** from your shell profile, so `export` in `.zshrc` has no
effect here. Register the key with launchd instead:

```sh
launchctl setenv OPENAI_API_KEY "sk-..."
```

Verify, and note that only apps started *after* this command see the new value —
if dictate is already running, quit and relaunch it:

```sh
launchctl getenv OPENAI_API_KEY
```

### Make it survive a reboot

`launchctl setenv` lasts until logout. To reapply it at every login, create
`~/Library/LaunchAgents/com.shorstok.dictate.env.plist`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key>
    <string>com.shorstok.dictate.env</string>
    <key>ProgramArguments</key>
    <array>
        <string>/bin/launchctl</string>
        <string>setenv</string>
        <string>OPENAI_API_KEY</string>
        <string>sk-...</string>
    </array>
    <key>RunAtLoad</key>
    <true/>
</dict>
</plist>
```

Then load it:

```sh
launchctl bootstrap gui/$(id -u) ~/Library/LaunchAgents/com.shorstok.dictate.env.plist
```

Two things to be aware of: the key sits in that plist in plaintext
(`chmod 600` it), and `launchctl setenv` publishes it to **every** GUI app in
your session, not just dictate. That's the trade-off for having a GUI app read
an environment variable at all.

To remove it later:

```sh
launchctl bootout gui/$(id -u)/com.shorstok.dictate.env
launchctl unsetenv OPENAI_API_KEY
```

## 3. First launch and permissions

```sh
open -n /Applications/dictate.app
```

Launch it this way (or from Finder/Spotlight) — **not** by running
`Contents/MacOS/dictate_cpp` from a terminal. A terminal-launched process makes
Terminal the responsible process for privacy purposes, so the permissions get
attributed to Terminal and dictate stays broken.

dictate needs three separate permissions, each with its own prompt:

| Permission | Used for | If you decline |
|---|---|---|
| **Input Monitoring** | detecting the Ctrl+Cmd chord | falls back to a Ctrl+Option+Shift+F9 toggle |
| **Microphone** | recording | recordings come back empty |
| **Accessibility** | pressing Cmd+V for you | transcripts are copied but not pasted |

Grant all three in System Settings → Privacy & Security, then **quit and
relaunch dictate**. The Input Monitoring grant in particular never applies to
the already-running process, so on the very first run you will always see the
"falling back to the toggle hotkey" notice — it should not reappear after a
relaunch.

You'll know it worked when the menu-bar icon appears with no warning dialogs
behind it.

## 4. Daily use

| Action | Result |
|---|---|
| Hold **Ctrl+Cmd** | Records while held ("Listening…" overlay) |
| Release | Stops, transcribes, pastes into whatever had focus |
| Tap the *other* Ctrl while holding | **Latch** — recording continues hands-free |
| Tap either Ctrl while latched | Stops and transcribes |
| Click the menu-bar icon | Show status · Copy last · Quit |

Feedback is a status blip near the top of the screen plus four sounds: Tink on
start, Pop on stop, Glass on success, Basso on error. The menu-bar icon changes
with state.

Two things are discarded on purpose, with a grey blip and no API call: holds
shorter than 700 ms ("Too short"), and holds with no detectable speech ("No
speech detected") — a muted or wrong input device would otherwise come back as a
hallucinated transcript.

If Input Monitoring is not granted, the chord is replaced by
**Ctrl+Option+Shift+F9**: press once to start, again to stop.

### Files

Everything lives in `~/Library/Application Support/dictate`:

| Path | Purpose |
|---|---|
| `config.json` | `language` (ISO-639-1, e.g. `en`) and `prompt` (a hint that biases transcription toward your jargon). Created as a stub on first run; edit and relaunch. |
| `transcribe.log` | Every transcript, one per line |
| `out/transcript_*.txt` | One timestamped file per transcription |
| `out/dictation_error.log` | Error log |
| `mic_input.mp3` | The recording — deleted after a successful transcription, **kept on failure** |

## 5. Start at login (optional)

System Settings → General → Login Items → **Open at Login** → `+` → pick
`/Applications/dictate.app`. dictate is a menu-bar agent, so it will not open a
window or appear in the Dock.

If the API key occasionally isn't picked up at login — the env-setting agent
from step 2 and the login item race each other — replace both with a single
agent at `~/Library/LaunchAgents/com.shorstok.dictate.plist` that sets the key
and starts the app itself:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key>
    <string>com.shorstok.dictate</string>
    <key>ProgramArguments</key>
    <array>
        <string>/Applications/dictate.app/Contents/MacOS/dictate_cpp</string>
    </array>
    <key>EnvironmentVariables</key>
    <dict>
        <key>OPENAI_API_KEY</key>
        <string>sk-...</string>
    </dict>
    <key>RunAtLoad</key>
    <true/>
</dict>
</plist>
```

## 6. Updating after a rebuild

```sh
osascript -e 'quit app "dictate"' 2>/dev/null || killall dictate_cpp
cmake --build --preset mac-release
cp -R out/build/mac-release/dictate_cpp.app /Applications/dictate.app
open -n /Applications/dictate.app
```

Keeping the same destination path preserves your permissions most of the time.
Every build does change the ad-hoc signature hash, though, so macOS may
occasionally decide the app is "new" and drop a grant — if dictation silently
stops working after an update, that's the first thing to check.

## 7. Uninstall

```sh
osascript -e 'quit app "dictate"' 2>/dev/null || killall dictate_cpp
rm -rf /Applications/dictate.app
rm -rf ~/Library/Application\ Support/dictate          # config + transcripts
launchctl bootout gui/$(id -u)/com.shorstok.dictate.env 2>/dev/null
rm -f ~/Library/LaunchAgents/com.shorstok.dictate*.plist
launchctl unsetenv OPENAI_API_KEY

tccutil reset Microphone    com.shorstok.dictate
tccutil reset ListenEvent   com.shorstok.dictate
tccutil reset Accessibility com.shorstok.dictate
tccutil reset PostEvent     com.shorstok.dictate
```

## 8. Troubleshooting

| Symptom | Cause |
|---|---|
| Ctrl+Cmd does nothing, F9 toggle works | Input Monitoring not granted, or granted but dictate wasn't relaunched afterwards |
| "OPENAI_API_KEY environment variable is not set" | `launchctl setenv` wasn't run, or dictate was started before it — check `launchctl getenv OPENAI_API_KEY`, then relaunch |
| Text is on the clipboard but never pasted | Accessibility not granted. ⌘V manually to confirm |
| Every recording says "No speech detected" | Wrong or muted input device — play `~/Library/Application Support/dictate/mic_input.mp3` to hear what was actually captured |
| Nothing happens at all, no menu-bar icon | Launched the inner binary from a terminal instead of `open`ing the app |
| Permission prompts keep reappearing | Running from `out/build/…` instead of a stable install path (step 1) |

The single most useful diagnostic is `mic_input.mp3`: it only survives a
*failed* transcription. If it sounds fine, the problem is the network or the API
key; if it's silent, the problem is the microphone.

For anything else, check `~/Library/Application Support/dictate/out/dictation_error.log`,
or capture the app's stderr with:

```sh
open -n /Applications/dictate.app --stderr /tmp/dictate.err
```
