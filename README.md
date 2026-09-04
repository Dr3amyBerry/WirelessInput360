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

## Config loading fix

The plugin could reach the network stage and then stop, never connecting. The
cause was reading `WirelessInput360.ini`.

A DashLaunch plugin runs inside the dashboard process. The `Usb:` / `Usb0:` /
`Hdd:` mount aliases are **not** guaranteed to exist there -- each application
creates the ones it needs, which is why Aurora's own log shows it mounting
`\??\Usb0: -> \Device\Mass0` for itself at startup. Every `fopen` against those
aliases failed, `ReadConfig` returned false, and the plugin gave up before it ever
called `connect`.

The un-normalized NT path from `FullDllName` (`\Device\Mass0\...`) does not help
with Win32 either: `CreateFileA` resolves names through the `\??\` DOS device
namespace and cannot open a native object path.

The fix is to open the file with the native API, which addresses the device object
directly and needs no alias:

```c
OBJECT_STRING name = { len, len + 1, (PCHAR)ntPath };
OBJECT_ATTRIBUTES oa;
InitializeObjectAttributes(&oa, &name, OBJ_CASE_INSENSITIVE, NULL);
NtOpenFile(&h, GENERIC_READ | SYNCHRONIZE, &oa, &iosb,
           FILE_SHARE_READ | FILE_SHARE_WRITE,
           FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE);
```

`ReadConfig` now tries the raw device path first, falls back to the aliases, and
parses in memory instead of using `fgets`.

Related fixes in the same area:

- The bootstrap thread was created with a stack size of `0`. `ReadConfig` used 512
  bytes of stack locals plus CRT stdio on top, which was enough to overflow it and
  take the console down. The thread now gets an explicit 64 KB stack and the
  parser buffers are static.
- Log output is sent over UDP instead of written to a file. See
  `tools/wi360logger/`.

## Log output

File logging never worked from this plugin, for the same reason config loading did
not. Log lines are now broadcast as UDP datagrams on port 3001.

Run `tools/wi360logger/wi360logger.exe` on the PC before powering on the console
to watch them live. It needs no runtime or redistributable. See
`tools/wi360logger/README.md`.

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
