# Gate for a future service-side IPv6 membership adapter

## Why the application-only experiment was withdrawn

A read-only snapshot and a later SetMulticastFilter replace call are separate
IPC transactions. A private application mutex, second snapshot, one-shot marker
or exact-list restore cannot serialize other clients. Even a successful cached
readback is not an independent observation of firmware state. No safe writable
client-only implementation has been established.

## Candidate service boundary (offline hypothesis only)

In the inspected HOS23.0.1 WLAN image, setter0xe8e20 acquires the underlying
interface lock at object+0x2d8 through0x5140, calls the configured backend while
holding it, then updates the cached list on success. The record builder0xe9220
reads under the same lock object via a different helper0x5110. These helpers
must not be assumed interchangeable or callable from an unrelated process.

The future adapter must mediate every membership update for that interface,
retain the system-requested list separately, and union it with its own bounded
subscriptions inside the service's serialization domain. Intercepting only the
UU client's calls is insufficient. Removing the UU subscription must recompute
that union from the latest system request, never restore a stale snapshot.

Before implementation establish:
1. All mutation/lifecycle paths and the lock's ownership/reentrancy contract.
2. A supported installation/interception boundary covering system clients.
3. Effective WLAN callback capacity8 and fail-without-clobber overflow behavior.
4. Failure/partial-programming handling and a genuine reconciliation mechanism;
   persistent markers and cached equality alone are insufficient.
5. Crash, suspend/resume, reconnection and interface recreation behavior.
6. Separate explicit scope for any system-module deployment or live filter write.

No runtime adapter, kernel patch or sysmodule is included. This design neither
proves safe injection nor requests permission to execute one. Native IPv6 TX,
addressing/ND/SLAAC, UDP and ICE still require independent evidence and work.
