# BadAvatar / XeUnshackle development notes

Context: WirelessInput360 is being adapted and tested as a DashLaunch plugin loaded through BadAvatar / XeUnshackle on an Xbox 360 dashboard 17559-class setup.

## Confirmed success

Status: full controller support works.

Confirmed by Dreamy from Rigor Core during live hardware testing:

- BadAvatar / XeUnshackle loaded successfully.
- DashLaunch loaded `Usb:\WirelessInput360.xex`.
- The Windows SDL3 WebSocket server accepted the Xbox client.
- The WebSocket handshake completed.
- After roughly 3 seconds, the controller worked immediately on the Xbox dashboard.
- No Wi-Fi adapter was required; this test succeeded over Ethernet.

Working USB layout:

```text
E:\
  BadUpdatePayload\
  Content\
  Dash_Launch_v3.21\
  Aurora\
  launch.ini
  WirelessInput360.ini
  WirelessInput360.xex
```

Working `launch.ini` plugin entry:

```ini
[Plugins]
plugin1 = Usb:\WirelessInput360.xex
plugin2 =
plugin3 =
plugin4 =
plugin5 =
```

Working `WirelessInput360.ini`:

```ini
ip=<pc-ipv4-address>
port=3000
```

## Current breakthrough

The plugin successfully connected to the Windows WebSocket server after changing the network initialization path.

Observed Windows server output:

```text
[WS] Client connected! Performing Handshake...
[WS] Handshake complete. Connection established.
```

This proves:

- BadAvatar / XeUnshackle can reach the DashLaunch plugin phase.
- `launch.ini` can load `Usb:\WirelessInput360.xex`.
- The PC server, port 3000, firewall/listener path, and Xbox-to-PC network route can work.
- Ethernet is not the blocker.

## Root cause found

The plugin was mixing normal XDK networking APIs with `NetDll_*` socket calls.

Problematic pattern:

```cpp
WSAStartup(...)
XNetStartup(...)
NetDll_socket(XNCALLER_SYSAPP, ...)
NetDll_connect(XNCALLER_SYSAPP, ...)
```

For a DashLaunch plugin running under this environment, the network stack must stay in the same caller/API family.

Working pattern:

```cpp
NetDll_XNetStartupEx(XNCALLER_SYSAPP, ...)
NetDll_WSAStartupEx(XNCALLER_SYSAPP, ...)
NetDll_socket(XNCALLER_SYSAPP, ...)
NetDll_connect(XNCALLER_SYSAPP, ...)
NetDll_recv(XNCALLER_SYSAPP, ...)
NetDll_send(XNCALLER_SYSAPP, ...)
```

The original mixed initialization caused early failure symptoms:

- Console powered off or returned to dashboard.
- No `WirelessInput360.log`.
- No `crashlog.txt`.
- Windows server stayed at `Waiting for client...`.

After using `NetDll_XNetStartupEx` and `NetDll_WSAStartupEx`, the plugin reached the server and completed the WebSocket handshake.

## Current branch state

The branch keeps the full controller path enabled:

- Hooks are installed after a delayed bootstrap thread and function pointer resolution.
- Networking uses the working `NetDll_*` initialization path.
- `WirelessInput360.ini` is required and must provide `ip=`.
- WebSocket frame parsing validates payload size, player index, and hex input before touching controller state.
- `XamUserBindDeviceCallback` is guarded so invalid player indexes do not mark a controller active.
- `XamInputGetCapabilitiesExHook` returns the original status when it does not synthesize a virtual controller.

Hardware test result: this path did not crash and did control the dashboard, so the current hook path is valid for the tested setup.

## Ethernet and Wi-Fi behavior

The plugin uses TCP to connect from the Xbox to the PC server IP and port.

It should be transport-independent as long as the Xbox network stack has a working route to the PC:

- Ethernet should work.
- Official Xbox 360 Wi-Fi adapter should work.
- Bridged/shared PC network should work if routing and firewall allow it.

The plugin does not care whether the packets leave through Ethernet or Wi-Fi. XNet exposes network connectivity above that hardware layer.

Practical requirements:

- Xbox and PC must be on reachable subnets.
- `WirelessInput360.ini` must point to the PC IPv4 reachable from the Xbox.
- Windows server must listen on `0.0.0.0:3000` or the correct interface.
- Windows firewall must allow inbound TCP 3000.
- Router/client isolation must not block Xbox-to-PC traffic.

## Expected BadAvatar / XeUnshackle flow

Expected runtime sequence:

1. Console boots with BadAvatar / XeUnshackle payload available.
2. XeUnshackle loads DashLaunch in memory.
3. DashLaunch reads `launch.ini`.
4. Plugins normally become active when exiting the exploit app / returning to dashboard / loading the next executable.
5. `plugin1 = Usb:\WirelessInput360.xex` loads the plugin.
6. Plugin starts its delayed bootstrap thread.
7. Plugin connects to the Windows WebSocket server.

If server does not show a connection, check for:

- `WirelessInput360.log`
- `crashlog.txt`
- Correct `launch.ini` path.
- Correct USB mount alias (`Usb:\` versus `Usb0:\`, if needed).

## Project direction recommendation

Best path: keep this as a fork first.

Reason:

- The protocol and project structure still come from WirelessInput360.
- The Windows app still matches the original server/client flow.
- The goal is currently compatibility with BadAvatar / XeUnshackle, not a brand-new architecture.
- A fork preserves upstream attribution and makes diffs easier to audit.

Recommended branch name:

```text
badavatar-xeunshackle
```

Only split into a separate project if the code diverges heavily, for example:

- New protocol.
- New GUI/launcher.
- Different plugin architecture.
- Support matrix beyond WirelessInput360's original scope.
- Installer/packaging designed specifically for BadAvatar users.

## Next implementation steps

1. Keep the working `NetDll_*` network initialization permanently.
2. Keep controller hooks enabled only after the delayed bootstrap and function pointer checks.
3. Replace any environment-specific config with `WirelessInput360.ini`.
4. Add a release checklist for USB layout and `launch.ini`.
5. Validate on more dashboard/runtime combinations before tagging a release.
