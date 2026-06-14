# WirelessInput360 - BadAvatar / XeUnshackle fork

WirelessInput360 sends controller input from a Windows PC to an Xbox 360 over the local network.

This fork adds support verified on a BadAvatar / XeUnshackle Xbox 360 setup, where DashLaunch is loaded through that runtime instead of a typical persistent RGH/JTAG environment.

Confirmed working by Dreamy from Rigor Core over Ethernet.

## What changed in this fork

- Reworked Xbox networking initialization to use the `NetDll_*` / `XNCALLER_SYSAPP` path required by this plugin runtime.
- Added a delayed plugin bootstrap thread so DashLaunch/XeUnshackle has time to finish loading before hooks and networking start.
- Kept controller hooks enabled after bootstrap and function pointer validation.
- Added safer WebSocket frame parsing before touching controller state.
- Added guards around virtual controller registration and controller indexes.
- Fixed `XamInputGetCapabilitiesExHook` so it returns the original status when no virtual controller is synthesized.
- Added `WirelessInput360.ini` path fallbacks for common USB/HDD plugin layouts.
- Added a sample `WirelessInput360.ini.example`.
- Disabled automatic XDK deploy during build so local MSBuild can generate the XEX without requiring a configured devkit target.
- Documented the BadAvatar / XeUnshackle setup and root cause in `docs/badavatar-xeunshackle-notes.md`.

## Requirements

- Xbox 360 running dashboard 17559 or 17489.
- BadAvatar / XeUnshackle flow capable of loading DashLaunch plugins.
- DashLaunch `launch.ini` on the USB drive.
- Windows PC on the same reachable network as the Xbox.
- SDL3 controller server app from this project.
- `WirelessInput360.xex` and `WirelessInput360.ini` placed together on the Xbox-accessible device.

Ethernet is confirmed working. Wi-Fi should also work if the Xbox can route to the PC IP and Windows firewall allows the selected TCP port.

## USB layout

Recommended root layout:

```text
Usb:\
  BadUpdatePayload\
  Dash_Launch_v3.21\
  launch.ini
  WirelessInput360.ini
  WirelessInput360.xex
```

The extra folders depend on your local setup, but `launch.ini`, `WirelessInput360.ini`, and `WirelessInput360.xex` must be reachable by DashLaunch.

## launch.ini

Example plugin entry:

```ini
[Plugins]
plugin1 = Usb:\WirelessInput360.xex
plugin2 =
plugin3 =
plugin4 =
plugin5 =
```

## WirelessInput360.ini

Copy `XboxPlugin/WirelessInput360/WirelessInput360.ini.example` next to `WirelessInput360.xex` and rename it to `WirelessInput360.ini`.

Example:

```ini
ip=192.168.1.100
port=3000
```

Use the Windows PC IPv4 address that the Xbox can reach.

## Windows server

Run the Windows controller server first. It should print something like:

```text
[WS] Server started on port 3000. Waiting for client...
```

When the Xbox plugin loads successfully:

```text
[WS] Client connected! Performing Handshake...
[WS] Handshake complete. Connection established.
```

After the handshake, controller input should work on the Xbox dashboard.

## Build notes

The Xbox plugin is built with the Microsoft Xbox 360 SDK/XDK.

The project expects `XEDK` to point to the SDK install path, for example:

```text
C:\Program Files (x86)\Microsoft Xbox 360 SDK
```

The `Release Retail|Xbox 360` configuration generates:

```text
XboxPlugin\WirelessInput360\Release Retail\WirelessInput360.xex
```

Build output and local tools such as `xextool.exe` are intentionally ignored by git.

## Technical notes

See `docs/badavatar-xeunshackle-notes.md` for:

- The root cause of the original network failure.
- Why `WSAStartup` / `XNetStartup` was replaced with `NetDll_WSAStartupEx` / `NetDll_XNetStartupEx`.
- The confirmed BadAvatar / XeUnshackle runtime flow.
- Hardware test notes from Dreamy / Rigor Core.

## Credits

Original project by UncreativeXenon.

Based on and made possible by EinTim23's `hiddriver360` work.

BadAvatar / XeUnshackle compatibility work and hardware validation by Dreamy from Rigor Core.

## License

MIT License.

See `LICENSE` for full details.
