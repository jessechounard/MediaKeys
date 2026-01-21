# MediaKeys

A lightweight Windows utility for triggering media functions via customizable hotkeys. Runs in the system tray and intercepts keyboard/mouse events to control volume, playback, and track navigation.

## Features

- Volume up/down/mute
- Play/pause
- Previous/next track
- Customizable modifier key combinations (Ctrl, Shift, Alt, WinKey)
- Support for keyboard keys, mouse buttons, and mouse wheel

## Building

```bash
zig build -Doptimize=ReleaseSmall
```

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
| `key_<code>` | Keyboard key by virtual key code (e.g., `key_0x41` for 'A') |

### Actions

| Action | Description |
|--------|-------------|
| `volume_up` | Increase volume |
| `volume_down` | Decrease volume |
| `volume_mute` | Toggle mute |
| `play_pause` | Play/pause media |
| `prev_track` | Previous track |
| `next_track` | Next track |

## Usage

Run `MediaKeys.exe`. The application will appear in the system tray. Right-click the tray icon and select "Exit" to close.

## Attribution

cJSON library - Ultralightweight JSON parser in ANSI C  
Downloaded from [cJSON github](https://github.com/DaveGamble/cJSON).

Original [music note image](music_17877340.png) by meaicon, downloaded from [freepik.com](https://www.freepik.com/icon/music_17877340#fromView=keyword&page=2&position=26&uuid=2c7277d3-3eaf-433f-aeba-96c78c4a41bf).

Resized to icon file with ImageMagick:
```bash
magick assets/music_17877340.png -resize 32x32 assets/icon.ico
```

Converted to C header file with xxd:
```bash
xxd -i assets/icon.ico > src/icon_data.h
```

## License

[MIT License](https://mit-license.org/)
