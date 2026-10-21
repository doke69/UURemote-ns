# Experimental IPv6 multicast membership probe 0.16

Prepared for code review only. Not deployed or run. This is not an IPv6 stack.

HOS23.0.1 only. The prior read-only tool observed wl0 with exactly one cached
multicast address01:00:5e:00:00:01. The earlier six-datagram experiment saw all
three IPv4 controls and no IPv6 frames. Neither result establishes where IPv6
was dropped or that WLAN hardware lacks IPv6 support.

## Proposed experiment (requires separate owner authorization)

- Read current interface list. Stop unless wl0's validated cached list exactly
  matches the fixed one-entry baseline.
- Obtain only that interface service; do not BringUp/Down, start communication,
  consume RX/TX queues, change promiscuity or call unknown ioctls.
- Prepare an independent BPF copy descriptor before changing membership.
- Create one-shot/pending markers, append only33:33:00:00:00:01 while preserving
  the existing IPv4 group, then verify cached-list readback.
- Observe counters passively for at most60seconds. No UDP sender or telemetry.
- On ordinary completion, cancellation, or detected capture failure, attempt
  guarded restoration once. Only restore if cache exactly matches our temporary
  two-entry list; never overwrite a concurrent different list.
- Verify original cache before clearing pending state. Do not claim independent
  firmware-state readback or successful IPv6 connectivity.

Normal path has at most two filter-setting calls (append and restore).
If interrupted, X offers one guarded restoration attempt per launch. Such a
recovery is not automatically authorized by the original passive research;
the operator must bound/authorize recovery separately. A process/system crash
can leave a temporary list; closing the interface service does not auto-restore.

## Important unresolved limits

Read/check/set/read is not atomic against other network-service clients.
The guard reduces unintended replacement but cannot exclude races. Do not run
with concurrent network reconfiguration. Cached membership can differ from
firmware state; AP multicast delivery is a separate gate. Raw IPv6 transmission,
neighbor discovery, addressing and ICE are not implemented or validated.

## Build and review status

inspector.c and membership.inc compile with the installed libnx/devkitA64 toolchain.
Local draft NRO compiled; no device validation and no deployment approval.
build.sh provides a portable SDK-based build entry; that shell entry has not
been executed in this work. Requires DEVKITPRO pointing to an existing SDK.
Never include firmware modules, console backups, credentials or keys in this PR.

This tool writes only its Inspector report and v016 attempt/recovery marker
files on SD. It does not modify the UU streaming app, pairing, games or saves.
