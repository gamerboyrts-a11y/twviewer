# Twitch3DS

Twitch client for New 3DS / New 2DS XL homebrew — live video (MVD hardware
decode), AAC audio, and IRC chat.

## Install via FBI QR Code

Open this page on your phone or PC, then scan the QR code with FBI on your 3DS
(FBI → Remote Install → Scan QR Code). Always points to the latest release.

![QR Code](https://api.qrserver.com/v1/create-qr-code/?size=200x200&data=https://github.com/gamerboyrts-a11y/twitch3ds/releases/latest/download/twitch3ds.3dsx)

Or enter the URL manually:

```
https://github.com/gamerboyrts-a11y/twitch3ds/releases/latest/download/twitch3ds.3dsx
```

Alternatively, download `twitch3ds.3dsx` from the
[latest release](https://github.com/gamerboyrts-a11y/twitch3ds/releases/latest)
and copy it to `sd:/3ds/` for the Homebrew Launcher.

## Features

- Live stream video on the top screen (New 3DS MVD hardware H.264 decode)
- AAC audio playback (requires DSP firmware dump — run the DSP1 homebrew once)
- Twitch IRC chat (read + send)
- Device Code login (no password needed)
- Stream metadata overlay (title / game / viewers)
- Channel history
- New 3DS / New 2DS XL only (uses the MVD decoder and second CPU core)
