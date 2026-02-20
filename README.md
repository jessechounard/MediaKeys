# MediaKeys

A lightweight Windows utility for triggering media functions via customizable hotkeys. Runs in the system tray and intercepts keyboard/mouse events to control volume, playback, and track navigation.

## Features

- Volume up/down/mute
- Play/pause
- Previous/next track
- Customizable modifier key combinations (Ctrl, Shift, Alt, WinKey)
- Support for keyboard keys, mouse buttons, and mouse wheel

## Why?

My favorite keyboard doesn't have convenient media keys or a volume knob. I have to hit function key combinations and/or reach across my desk to the speaker knob. Holding the Windows Key and scrolling the mousewheel to adjust the volume is very convenient!

For years I've used an [AutoHotkey](https://www.autohotkey.com/) script to do exactly what this program does, and it's been perfect. I highly recommend that program if you can use it. However, lately I've been running into a problem with certain online games that think that if you have AHK running, you must be cheating. So you have to remember to turn off your AHK scripts before launching the game, then turn them back on again later. Not a huge deal, but why wouldn't I spend a whole bunch of hours coding and debugging to save myself a few seconds and a very minor annoyance?

## Notes

I did not know what I was doing with hooking into the input system when I started writing this. Specifically I couldn't figure out how to suppress the Windows Key key-up event (popping up the Start Menu) without leaving Windows thinking you were always holding the Windows Key down. Eventually I read about a hack to replace it with a left Control Key key-up event. It seems to work, but doesn't feel great to me. Please let me know if you encounter problems.

Also new for me with this project is using Unicode strings in Win32. I normally just configure everything to basic ASCII C-strings, but I wanted to experiment. Shout if this breaks and I can switch them out. If it doesn't break, maybe I'll experiment with adding translations. We'll see.

## Installing

You have a few options.
- You can build from the source code (see below)
- Use a setup installer from the [latest releases](https://github.com/jessechounard/MediaKeys/releases/latest)
- Grab a portable build from the [latest releases](https://github.com/jessechounard/MediaKeys/releases/latest) - Just unzip and run. To uninstall, make sure the program isn't running (see System Tray below) and delete the file.
- Install with scoop:
  ```
  scoop install https://github.com/jessechounard/MediaKeys/releases/latest/download/MediaKeys.json
  ```
  Then run with `MediaKeys` (or `start MediaKeys` if using Git Bash). To have it start automatically with Windows, right-click the system tray icon and enable "Run at startup".

When the app starts, it'll look like nothing happened. Check the system tray for the icon. I should probably add a popup the first time you run it to let you know that the app runs invisibly.

If you grab one of the prebuilt binaries, you're likely to see a Windows Defender warning that it's an unrecognized app. This is because it's an unsigned app, for now. I'm looking into options there. You can click "More info" then "Run anyway" to get past that if you're willing to ignore the warning.

## Building

```bash
zig build -Doptimize=ReleaseSmall
```

## System Tray

The app runs without a window. The only controls are through the configuration file and the system tray. Currently there are two system tray options when you click on the icon. (A music note.) 
- Run at startup (Toggle on and off.)
- Exit

I want to add a user interface to configure the keys in here, but it's not there yet.

## Configuration

On first run, a `config.json` file is created in `%APPDATA%\MediaKeys\`. You can also place a `config.json` in the same directory as the executable to override.

### Default Configuration

```json
{
  "bindings": [
    { "win": "left", "trigger": "wheel_up", "action": "volume_up" },
    { "win": "left", "trigger": "wheel_down", "action": "volume_down" },
    { "win": "left", "trigger": "mouse_x2", "action": "next_track" },
    { "win": "left", "trigger": "mouse_x1", "action": "prev_track" },
    { "win": "left", "trigger": "mouse_middle", "action": "play_pause" }
  ]
}
```

### Modifiers

Each binding can specify modifier key requirements:

| Value | Description |
|-------|-------------|
| `"none"` | Modifier must not be pressed (default) |
| `"left"` | Only left modifier |
| `"right"` | Only right modifier |
| `"either"` | Either left or right |
| `"both"` | Both left and right must be pressed |

Modifier keys: `ctrl`, `shift`, `alt`, `win`

### Triggers

| Trigger | Description |
|---------|-------------|
| `wheel_up` | Mouse wheel up |
| `wheel_down` | Mouse wheel down |
| `mouse_left` | Left mouse button |
| `mouse_right` | Right mouse button |
| `mouse_middle` | Middle mouse button |
| `mouse_x1` | Mouse back button |
| `mouse_x2` | Mouse forward button |
| `key_<name>` | Keyboard key by name (see below) |
| `key_<code>` | Keyboard key by virtual key code (e.g., `key_0x41` for 'A') |

Named keys: `key_a` through `key_z`, `key_0` through `key_9`, `key_f1` through `key_f12`,
`key_space`, `key_enter`, `key_tab`, `key_escape`, `key_backspace`, `key_delete`, `key_insert`,
`key_home`, `key_end`, `key_pageup`, `key_pagedown`, `key_up`, `key_down`, `key_left`, `key_right`,
`key_printscreen`, `key_scrolllock`, `key_pause`, `key_numlock`, `key_capslock`,
`key_num0` through `key_num9`, `key_nummultiply`, `key_numadd`, `key_numsubtract`, `key_numdecimal`, `key_numdivide`,
`key_semicolon`, `key_equals`, `key_comma`, `key_minus`, `key_period`, `key_slash`, `key_backtick`,
`key_lbracket`, `key_rbracket`, `key_backslash`, `key_quote`

### Actions

| Action | Description |
|--------|-------------|
| `volume_up` | Increase volume |
| `volume_down` | Decrease volume |
| `volume_mute` | Toggle mute |
| `play_pause` | Play/pause media |
| `prev_track` | Previous track |
| `next_track` | Next track |
| `screenshot_client_clipboard` | Capture active window's client area to clipboard |
| `screenshot_client_file` | Capture active window's client area to PNG file |
| `screenshot_client_file_clipboard` | Capture to PNG file and copy the file to clipboard |

## Attribution

cJSON library - Ultralightweight JSON parser in ANSI C  
Downloaded from [cJSON github](https://github.com/DaveGamble/cJSON).

Original [music note image](assets/music_17877340.png) by meaicon, downloaded from [freepik.com](https://www.freepik.com/icon/music_17877340#fromView=keyword&page=2&position=26&uuid=2c7277d3-3eaf-433f-aeba-96c78c4a41bf).

Resized to icon file with ImageMagick:
```bash
magick assets/music_17877340.png -resize 32x32 assets/icon.ico
```

Converted to C header file with xxd:
```bash
xxd -i assets/icon.ico > src/icon_data.h
```

## License

[MIT License](./LICENSE)
