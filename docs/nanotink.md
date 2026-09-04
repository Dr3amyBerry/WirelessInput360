# nanotink crash report — PANIC_STACK_SWITCH (0x2b)

Status: **open.** The ranked root-cause candidates in section 5 are all still
unfixed. Section 5c lists logging and config changes that did land on this branch
while investigating -- they improve diagnosability but do not address the crash.

Reported on PR #3 (`badavatar-xeunshackle` -> `UncreativeXenon:master`).

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

Note: dashboard 17559 is in the supported list, so the version gate in
`BootstrapThread` passed. Winchester is a late Corona revision and is one of the
main reasons people use BadAvatar / XeUnshackle in the first place, so this board
will be a common target. It is **not** the board this fork was validated on.

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

## 3. What the log rules out

These stages all completed, so they are **not** the problem:

- BadAvatar / XeUnshackle reached the DashLaunch plugin phase.
- `launch.ini` loaded the `.xex` and `DllMain` ran.
- The 8-second bootstrap delay elapsed and `InitializePluginPaths` resolved `Usb0:\`.
- The 17559/17489 dashboard gate and the tray check passed.
- `initFunctionPointers()` returned true, so `XexGetProcedureAddress` resolved
  ordinals 401 / 402 / 685 from `xam.xex`, and the devkit probe selected retail.
- All four detours completed `Install()` without faulting during the call itself.
- `NetDll_XNetStartupEx` and `NetDll_WSAStartupEx` both succeeded.
- Link status `0x0000000B` = active + 100 Mbps + full duplex. **The network is fine.**

So this is not the networking regression this fork was created to solve. It is a
new and separate failure in the hook path.

## 4. Reading the crash

### 4.1 What the stack says

Oldest frames at the bottom, newest at the top:

```
0x80075E5C    kernel
0x81C19F14  \
0x81C11E80   |  xam.xex
0x81C1142C   |
0x81C10654  /
0x80085F30  \
0x80076D80   |  kernel
0x800728DC   |
0x80072814  /
0x91007284  \
0x91006EB4   |  plugin (module base ~0x91000000)
0x91F05CB0   |  <-- anomaly
0x9100A4E0  /   LR
0x80075538      faulting address
```

A xam thread went through the kernel and into our module. That is a **hook firing**,
not our bootstrap thread. Our network thread was still inside `ReadConfig()` or
`connect` at that moment, which is exactly why `Attempting connect to ...` never
reached the log — the console died on a different thread first.

### 4.2 The smoking gun

`0x91F05CB0` sits roughly 15 MB past the module base. The `.xex` is about 36 KB.
That address **cannot be code in this plugin**. A return address that far out of
range means the stack was already corrupt, or control was transferred to garbage
and then unwound through garbage.

That is fully consistent with stop code `0x2b`: the stack pointer left its legal
range and the kernel panicked on the stack switch.

## 5. Root cause candidates, ranked

### A. No instruction-cache invalidation after patching code — PRIMARY SUSPECT

`Detour::Install()` in `Detours.h` writes new PowerPC instructions into live
`xam.xex` code with a plain `memcpy`, and builds the trampoline inside
`Detour::TrampolineBuffer`, a static `BYTE` array living in the plugin's data
section.

On Xenon, **the data cache and the instruction cache are not coherent**. Writing
instruction words through normal stores only puts them in the D-cache. The
required sequence after emitting code is:

```
dcbst   (flush each modified line out of the D-cache)
sync    (wait for the flushes to land)
icbi    (invalidate the matching I-cache lines)
isync   (discard the speculative instruction pipeline)
```

None of that happens. Consequences:

- A core can keep executing the **stale** original xam instructions.
- Worse, it can execute a **mix**: some patched words visible, some not.
- The trampoline can be entered before its instruction words are visible as code,
  so `GetOriginal()` jumps into whatever the I-cache happens to hold.

Any of those produces exactly one wild branch to an address like `0x91F05CB0`,
followed immediately by a stack panic.

**This also explains why the fork works on one console and dies on another.**
Cache state, core scheduling and boot timing differ per board and per boot. It is
a race, not a deterministic incompatibility. Winchester timing differing from the
validated console is enough to flip the outcome.

Fix: add explicit cache synchronization after every code write — after building
the trampoline, and after patching the hook site. Cover the full byte range of
each write, one cache line at a time.

### B. Non-atomic patch of a hot, actively-executing function

`WriteFarBranch` emits four instructions:

```asm
lis   r0, target@hi
ori   r0, r0, target@lo
mtctr r0
bctr
```

and installs them with a single `memcpy` over the first 16 bytes of the target.

`XamInputGetState` is polled continuously by the dashboard. Another core can be
executing that function while the `memcpy` is only partially complete, so it runs
`lis` and `ori` and then falls into the *original* third and fourth instructions
with `r0` already clobbered.

Fix: write words 2, 3 and 4 first, cache-sync, then publish word 1 last as a
single aligned 4-byte store, then cache-sync again. A 4-byte aligned store is
atomic with respect to instruction fetch, so a core sees either the old first
instruction or the new branch, never a half-installed hook.

This must land together with A. Fixing either alone leaves the other race open.

### C. Hardcoded absolute addresses are never validated

`initFunctionPointers()` and `BootstrapThread()` use raw literals:

| Address | Used as |
|---|---|
| `0x8010D334` | devkit/retail probe, dereferenced before any validation |
| `0x816D9060` | `XamUserBindDeviceCallback` (retail) |
| `0x81AAC2A0` | `XampInputRoutedToSysapp` (retail) |
| `0x81695DE8` | `XamInactivityDetectRecentActivity` detour target (retail) |

The byte signatures are already written down in the comments next to them:

```
XamUserBindDeviceCallback           7C 8B 23 78 7C A4 2B 78 54 CA 06 3F
XamInactivityDetectRecentActivity   3D 60 81 ?? 3D 40 81 ?? E8 6B ?? ?? E9 6A ?? ?? 7F 23 58 40 40 98 00 0C
```

but they are never actually checked. If any address is off on this console,
`Detour((void*)0x81695DE8, ...)` overwrites 16 bytes in the middle of an unrelated
xam function, and `XamUserBindDeviceCallback` becomes a call into arbitrary code.

The kernel build gate (`XboxKrnlVersion->Build`) is not sufficient on its own. It
proves the kernel version, not that xam is laid out where we assume.

Fix: verify each signature at its address before using it. On mismatch, log the
address and the bytes actually found, then abort cleanly instead of patching.
This is cheap, cannot regress a working console, and turns a hard crash into a
readable log line for every future reporter. **Do this one first regardless of
what else gets fixed**, because it makes all remaining reports diagnosable.

### D. Thread created with no explicit stack size

`MakeThread()` passes `0` as the stack size to `ExCreateThread`.

The bootstrap thread then runs `StartWSConnection`, whose frames include:

- `char buf[4096]`
- `char wsHandshake[512]`
- `char text[256]` and `uint8_t raw[128]`
- `LogMessage`'s `char message[512]`, plus `vsnprintf` and **four** `fopen` /
  `fputs` / `fclose` cycles per call. XDK CRT stdio is stack-hungry.

Fix: pass an explicit stack size, 0x10000 is a reasonable starting point, and move
`buf[4096]` off the stack into a static or heap allocation.

Partially mitigated already: `LogMessage` now performs one file write per line
instead of four, so its contribution to stack depth is much smaller. The explicit
stack size and the `buf[4096]` move are still outstanding.

This is a textbook direct cause of `0x2b` on its own. It ranks below A and B only
because the reported stack shows a xam thread rather than the bootstrap thread,
but it should be fixed in the same pass since it is a second independent way to
reach the same stop code.

### E. Blocking network I/O inside a hook

`XamInputSetStateHook` calls `SendVibrationUpdate`, which performs `std::string`
and `std::vector` heap allocation and then a blocking `NetDll_send` — all on
**xam's own thread**, inside an input callback.

This is not implicated in this specific report, since it requires an ACTIVE
controller and none was ever registered before the crash. It is still a real
hazard: a stalled or half-open TCP connection would block a xam input thread
indefinitely.

Fix: never touch the socket from a hook. Write the vibration values into a small
lock-free slot and let the network thread pick them up and send them.

### F. WebSocket parser hardening

In the 127-length extended-payload branch, `payloadLen` is an `int` accumulated
over 8 bytes, so it can overflow negative. A negative value passes the
`(offset + payloadLen) > bytes` guard and then reaches
`memcpy(text, data + offset, payloadLen)`, which converts to a huge `size_t`.

Also, `raw[128]` is filled by `raw[raw_len++]` with no bound check. With `text`
clamped to 255 characters the maximum index is 127, so it is *just* in range, but
it survives on arithmetic that nothing enforces.

Not the cause here — the crash predates any frame being received — but both should
be closed while the file is open.

## 5b. Provenance: what the fork actually introduced

Verified by diffing `badavatar-xeunshackle` against the merge base `105f2b1`.

`Detours.h` and `Detours.cpp` are **byte-identical to upstream**. The fork never
touched the hook engine. The hardcoded addresses (`0x816D9060`, `0x81AAC2A0`,
`0x81695DE8`, `0x8010D334`) are also identical to upstream.

So candidates A, B and C are all inherited upstream code. The fork did not
introduce any of them.

The fork did, however, change *when* they fire. Upstream installed the detours
inside `DllMain` during module load. The fork moved installation into a bootstrap
thread that runs after `Sleep(8000)` -- a change that was necessary for
BadAvatar / XeUnshackle, but which means the hooks are now patched into
`XamInputGetState` **while the dashboard is already polling it at full rate**,
instead of during load when almost nothing was calling it.

That is the causal link: the fork did not create the race in A and B, it moved
hook installation into the window where the race can actually be lost. Which is
consistent with the crash showing up in this fork and not upstream, and with it
being intermittent across consoles.

Practical consequence for the fix order: A and B remain the priority even though
they are upstream bugs, because this fork is what exposes them.

## 5c. Fixes already applied on this branch

Not part of the nanotink fix plan, but landed while investigating and relevant to
any future crash report:

- `WriteLogLine` no longer uses CRT `fopen`. It uses `CreateFileA` with
  `FILE_SHARE_READ | FILE_SHARE_WRITE` and `OPEN_ALWAYS`, plus `FlushFileBuffers`
  on every line. The old `fopen(path, "a")` returned NULL on the reporter's own
  console and the function bailed out silently, which is why no log file was ever
  produced. Flushing per line also means an abrupt power-off no longer discards
  the last and most interesting entries.
- `LogMessage` probes its candidate paths only until one accepts a write, then
  remembers it. This replaces four `fopen`/`fputs`/`fclose` cycles per line with a
  single write, which directly reduces the stack pressure described in D.
- The `Hdd:` fallback was removed from both `ReadConfig` and the log candidates.
  Config and logs are USB-only now, so a stale ini on the console's internal drive
  can no longer be picked up silently.
- `BootstrapThread` writes one log line *before* its 8 second sleep. Without it,
  a crash during that window is indistinguishable from a broken log path: both
  produce an empty USB. With it, an empty USB now means the write path is still
  failing, and a single line means the console died before any hook was installed.

## 6. Plan of action

Order matters. Do not skip straight to the interesting one.

1. **C — signature validation.** Smallest, safest change. Makes every future
   report legible instead of a raw stop code. Ship it even if nothing else lands.
2. **A — cache synchronization.** The actual suspected root cause.
3. **B — atomic hook publication.** Must land together with A.
4. **D — explicit thread stack, and move `buf[4096]` off the stack.**
5. **E — vibration send moved out of the hook.**
6. **F — parser hardening.**

Steps 1 to 4 are the crash fix. Steps 5 and 6 are hardening that belongs in the
same review pass.

## 7. Verification

Cannot be validated locally: the Xbox 360 XDK is required to produce a `.xex` and
is not currently installed. See the build notes in `README.md`.

Once a build exists:

- Regression-test on the console this fork was originally validated on. A, B and D
  all touch the path that currently works, so proving no regression matters as
  much as proving the fix.
- Then have nanotink retest on the Winchester / 17559 console.
- Because A and B are races, "it booted once" is not a pass. Require several cold
  boots, and boots with other plugins loaded, before calling it fixed.

## 8. Information worth requesting from nanotink

- The contents of `Usb0:\WirelessInput360.log`. The plugin writes to four
  candidate paths, so a file should exist even though the console died.
- Whether any other DashLaunch plugin is listed in `launch.ini`, and whether
  Aurora or FSD was loading at the same time.
- Whether the crash is immediate and reproducible on every boot, or intermittent.
  Intermittent strongly confirms A and B. Deterministic points at C.
- The exact xam version, not just the dashboard version.
- Whether the crash still occurs with the Windows server **not** running, which
  isolates the hook path from the network path entirely.
