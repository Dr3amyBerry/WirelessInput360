# wi360logger

Receives the plugin's log over the network.

The Xbox plugin sends every log line as a UDP datagram to the subnet broadcast
address on port **3001**. This tool listens for them, prints them as they arrive
and appends them to `wi360.log` next to the executable.

## Why this exists

File logging does not work from inside this plugin. It runs in the dashboard
process, where the `Usb:` / `Usb0:` / `Hdd:` mount aliases do not necessarily
exist -- each application creates its own. Every `fopen` and `CreateFileA` against
them fails, and the plugin has no console to print to.

On-screen notifications are not an option either: both `XNotifyQueueUI` and
`XShowMessageBoxUI` destabilise Aurora when called from the plugin.

UDP works because it is connectionless. It needs no successful `connect()` and
nothing listening on the other end to accept, so it still reports when the TCP
connection to the controller server is itself what is failing. Broadcast means it
does not depend on having read the ini either, so a config failure is still
visible.

## Usage

Run it on the PC **before** powering on the Xbox:

```
wi360logger.exe
```

Leave it running. It keeps listening across console reboots.

```
==============================================================
 WirelessInput360 - receptor de logs
 Escuchando UDP 0.0.0.0:3001      (Ctrl+C para salir)
 Guardando en wi360.log
==============================================================

[03:38:28] 192.168.1.2     [WirelessInput360] Config read from \Device\Mass0\WirelessInput360.ini (25 bytes)
[03:38:28] 192.168.1.2     [WirelessInput360] Loaded config. ip=192.168.1.1 port=3000
[03:38:28] 192.168.1.2     [WirelessInput360] Connected to server
```

Winsock error codes in the text are decoded automatically:

```
[03:34:04] 192.168.1.2     [WirelessInput360] connect failed ... wsa=10061
                           ^^^  WSAECONNREFUSED - llega, pero nadie escucha en ese puerto
```

## Building

No dependencies beyond the Windows SDK. Static CRT, so the resulting exe needs no
redistributable at all -- it imports only `WS2_32.dll` and `KERNEL32.dll`, which
ship with Windows. Built as 32-bit so it runs anywhere.

```
cl /O2 /MT /D_CRT_SECURE_NO_WARNINGS wi360logger.cpp /link ws2_32.lib
```

## Firewall

The tool listens for inbound UDP on port 3001. If Windows Firewall is on, allow
it when prompted, or add the rule manually:

```
netsh advfirewall firewall add rule name="wi360logger" dir=in action=allow protocol=UDP localport=3001
```
