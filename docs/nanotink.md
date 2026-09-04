# nanotink crash report — PANIC_STACK_SWITCH (0x2b)

Status: **root cause found and fixed in `badavatar-xeunshackle-v0.2.0`.** Awaiting
confirmation from the reporter.

Reported on PR #3 (`badavatar-xeunshackle` -> `UncreativeXenon:master`).

This file was originally a ranked list of hypotheses, written before there was any
way to observe the plugin at runtime. Most of them were wrong. It has been
rewritten around what the evidence actually showed. Section 6 keeps the discarded
theories and says why they were wrong, because the reasoning that led away from
them is worth not repeating.

---

## 1. Reporter setup

| Item | Value |
|---|---|
| Console | Xbox 360, softmodded |
| Kernel / dashboard | 17559 |
| Motherboard | Winchester |
| Runtime | BadAvatar / XeUnshackle -> DashLaunch |
| Plugin device | `Usb0:\` |
| Transport | Ethernet |

## 2. Reported log

```text
xbWatson: Connection to Xbox successful
[WirelessInput360] Bootstrap entered
[WirelessInput360] Plugin directory: Usb0:\
[WirelessInput360] Config path: Usb0:\WirelessInput360.ini
[WirelessInput360] Running in retail mode
[WirelessInput360] Hooks installed
[WirelessInput360] XNet initialized. Ethernet link status: 0x0000000B

--------------------------------------------------------------------
  stop code: 0x2b (PANIC_STACK_SWITCH)
     (0x3A097900,0x80075538,0x3A15E640,0x800760B8)
--------------------------------------------------------------------
Call Stack:
    0x80075538 (EADDR)
    0x9100A4E0 (LR)
    0x91F05CB0
    0x91006EB4
    0x91007284
    0x80072814
    0x800728DC
    0x80076D80
    0x80085F30
    0x81C10654
    0x81C1142C
    0x81C11E80
    0x81C19F14
    0x80075E5C
--------------------------------------------------------------------
Dump Done
```

Note on how this log exists at all: nanotink captured it with **xbWatson**, which
receives `DbgPrint` output over the network. It is not the plugin's log file.
`LogMessage` wrote to both `DbgPrint` and a file; only the `DbgPrint` half has
ever worked. See section 4.

## 3. The decisive clue

The log stops at:

```text
[WirelessInput360] XNet initialized. Ethernet link status: 0x0000000B
```

A second console, on a completely different network, stopped at **the same line**,
byte for byte. That turned a one-off report into a reproducible bug.

Here is what follows that line in `StartWSConnection`:

```cpp
DWORD linkStatus = NetDll_XNetGetEthernetLinkStatus(...);
LogMessage("[WirelessInput360] XNet initialized. Ethernet link status: 0x%08X\n", linkStatus);

if (!ReadConfig()) {        // <-- everything below happens here
```

`0x0000000B` also decodes as active + 100 Mbps + full duplex, so the network was
never the problem on either console.

## 4. Getting a diagnostic channel

This took longer than the fix. Three channels were tried; two are dead ends worth
recording.

**File logging does not work from this plugin.** `WriteLogLine` used
`fopen(path, "a")` and returned silently on failure, so it produced nothing and
said nothing about why. Switching to `CreateFileA` with explicit share flags did
not help either. The reason is the same one behind the actual bug — see 5.2.

**On-screen notification kills Aurora.** Both `XNotifyQueueUI` (the toast) and
`XShowMessageBoxUI` (the system dialog) were tried. Calling either from the plugin
caused Aurora to die during startup, reproducibly. Do not use them from here.

**UDP works.** Log lines are now sent as datagrams to the subnet broadcast address
on port 3001, and `tools/wi360logger` receives them. This is the channel that
found the bug, and it works precisely because it is connectionless: it needs no
successful `connect()` and no listener to accept, so it still reports when the TCP
connection is itself what fails. Broadcast means it does not depend on having read
the ini either — which mattered, because the ini was the problem.

With probes added around `ReadConfig`, the console said this:

```text
probe: entering ReadConfig
probe: trying \Device\Mass0\WirelessInput360.ini
probe: trying Usb0:\WirelessInput360.ini
probe: trying Usb:\WirelessInput360.ini
probe: trying Hdd:\WirelessInput360.ini
Failed to open config from any path
```

## 5. Root cause: two bugs stacked

### 5.1 Stack overflow in `ReadConfig`

`MakeThread` created the bootstrap thread with a stack size of `0`:

```cpp
ExCreateThread(&handle, 0, 0, XapiThreadStartup, address, arg, ...);
//                      ^ stack size
```

`ReadConfig` then ran `char buffer[256]` plus `char lower[256]` — 512 bytes of
stack locals — on top of CRT stdio, which is not cheap on the XDK runtime. That is
enough to run off the end of a minimal stack.

Overrunning the stack is exactly what stop code `0x2b` PANIC_STACK_SWITCH reports,
and it explains the wild `0x91F05CB0` in the call stack: that address sits roughly
15 MB past a module that is only 40 KB, so it was never a real return address. The
stack was already corrupt.

Fixed: explicit 64 KB stack, and the parser buffers are `static`.

### 5.2 The config could not be read at all

With a working stack the console stopped dying, and reported the real failure:
every candidate path failed.

A DashLaunch plugin runs inside the **dashboard process**. The `Usb:` / `Usb0:` /
`Hdd:` mount aliases are not guaranteed to exist there — each application creates
the ones it needs. Aurora's own log shows it doing exactly that at startup:

```text
"filter": "SystemDrive", "message": "Mounting results:  \??\Usb0:, \Device\Mass0"
```

That mount belongs to Aurora's process. `NormalizePath` in this plugin only
rewrites strings: it turns `\Device\Mass0\` into `Usb0:\` and assumes the alias
exists. It does not.

The raw NT path from `FullDllName` does not rescue it either. `CreateFileA`
resolves names through the `\??\` DOS device namespace and **cannot open a native
object path** like `\Device\Mass0\file`. That is why the raw candidate failed too.

Fixed by opening the config with the native API, which addresses the device object
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

### 5.3 Why this looked like a hook problem

It never was. With no ini, `ReadConfig` returned false and `StartWSConnection`
returned before ever calling `connect`. The plugin looked like it had died in the
hook path when it had simply given up.

Result on hardware after both fixes:

```text
Config read from \Device\Mass0\WirelessInput360.ini (25 bytes)
Loaded config. ip=192.168.1.1 port=3000
Attempting connect to 192.168.1.1:3000
Connected to server
Handshake response received
```

## 6. Hypotheses that were wrong

Recorded so they are not chased again.

### Instruction-cache coherency — WRONG

The first version of this document named missing `dcbst`/`sync`/`icbi`/`isync`
after patching code as the primary suspect, reasoning that stale I-cache lines
could produce a wild branch. It is true that `Detour::Install()` does no cache
synchronisation. It is not what was happening here, and there is no evidence it
has ever caused a fault on this console.

### Non-atomic hook patching — WRONG

The same document argued that `memcpy`ing 16 bytes over a live `XamInputGetState`
could be observed half-written by another core. Plausible in theory, not what
happened.

Both of these were invented to fit a symptom rather than derived from an
observation — there was no diagnostic channel at the time, so there was nothing to
derive from. The actual cause was two functions further down, in code that has
nothing to do with hooking.

### The Aurora crash log — NOT RELATED

`Aurora/Data/Logs/*.crash.log` on the reporter's USB showed an access violation at
`0x8221698C`, inside Aurora's own module, with a deep repeating recursion right
after `ContentManager: Starting ScanPath: \Xbox360\System\Dvd\`. That is Aurora
crashing while scanning a disc. No frame in that stack belongs to this plugin. It
also could not be dated: the console clock sits at Nov 2005, with no battery and
no NTP.

### Trampoline in `.data` — REAL, BUT NOT THIS

`Detour::TrampolineBuffer` is a plain `static BYTE[4000]`, so the generated
trampoline code lives in `.data`, a non-executable section. `hiddriver360` places
it in `.text` instead:

```cpp
#pragma section(".text", read, execute)
__declspec(allocate(".text")) static BYTE TrampolineBuffer[200 * 20];
```

Measured: applying that moves exactly 4000 bytes from `.data` to `.text`.

This is a genuine correctness issue and worth fixing, but it is **not** the cause
of this crash, and it does not appear to fault on the retail dashboard — the
plugin's hooks work as-is. EinTim23's comment scopes it specifically to running
"inside the OG xbox emulator", i.e. original Xbox backward compatibility. That is
the context where it would bite. Still open.

## 7. Still open

- **Trampoline section.** See section 6. Correctness fix, not urgent.
- **`NetDll_XNetCleanup` on the shared stack.** This fork added three calls that
  upstream does not have. `XNCALLER_SYSAPP` is the system network stack, which
  Aurora also uses for NTP and HTTP. Tearing it down is not this plugin's to do.
  Not implicated in this crash, but it should go.
- **`XamInputSetStateHook` does blocking network I/O inside a hook**, including
  `std::string` and `std::vector` allocation, on xam's own thread. A stalled
  socket would block a xam input thread. Should be queued to the network thread.
- **Hardcoded xam addresses are never validated**, despite the byte signatures
  being written in the comments next to them. Validating them would turn a
  potential hard crash into a readable log line, which is now cheap to emit.

## 8. Verification

Fixed and confirmed on hardware here: config read, connected, handshake completed.

Not yet confirmed on the reporter's console. They were pointed at
`badavatar-xeunshackle-v0.2.0` on PR #3, along with `wi360logger`, so that a
remaining failure reports its own location instead of needing xbWatson.
