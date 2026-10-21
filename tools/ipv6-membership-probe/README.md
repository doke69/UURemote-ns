# IPv6 multicast inventory 0.16 (read-only)

PR review found that the proposed multicast experiment cannot safely coordinate
whole-list replacement with existing system clients. The active experiment is
withdrawn, not repaired by adding more cache checks. No writable backend or
runtime/compile-time enable option remains in this build.

## What this build does

On HOS23.0.1, A queries wlan:nd creator0 then driver129 once, at most16 records
of0xb0 bytes. It prints validated interface names and inferred cached multicast
MAC addresses, closes sessions, and stops. + exits. It writes its own report only.
There is no interface open, multicast setter, BPF access, packet sender,
recovery action, attempt marker, queue operation or system-module installation.

The prior0.15 device observation contained one cached multicast MAC,
01:00:5e:00:00:01, and no33:33 prefix. That cache is not independent hardware
readback and does not establish where IPv6 traffic was dropped.

## Review fixes

- Whole-list check/set/restore race: remove all mutation paths pending a proven
  service-side ownership/serialization design.
- Undurable markers: remove markers and the experiment they purported to protect.
- Cleanup dependent on marker readability: remove that recovery mechanism.
- Cache-only clearing after partial failure: no recovery or clearing code remains.

These changes remove unsafe operations; they do not implement safe live writes
or IPv6 connectivity. See DESIGN.md for the remaining research gates.

## Build and deployment

build.sh requires an existing SDK via DEVKITPRO. The Windows SDK entry is built
locally; the portable shell entry has not been executed here. Firmware, keys,
console backups and user reports must never be included in this PR.

This revision has not been deployed or device-verified. Stable UUv52 and the
installed Inspector0.15 remain unchanged. Repeating the same inventory on the
console is not needed for the current offline research.
