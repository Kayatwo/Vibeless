# Hunt lifecycle and locale rollout, 2026-10-17

Prepared, not activated: four live game hosts have a checked patch under
`staging/hunt-patch-20261017`. Main EU is a deployed folder, not a Git checkout.
The patch preserves each host's existing deploy code, adding readiness timestamps
and a 15-second non-overlapping watchdog, plus the tested hunt-lifetime module.

Ready hunts require 120 seconds of continuously observed zero connections before
closing (normally 120–135 seconds with polling). Positive counts, missing/stale
telemetry or long observation gaps reset the timer. Connections still joining count
as occupied. Ramsgate and Training are exempt. Age alone no longer kills occupied
hunts. Existing child-exit cleanup releases crashed server allocations.

Set `HUNT_EMPTY_MINUTES=2` and remove extra startup grace during activation.
Prior NA/EU overflow settings used ten minutes. Proposed caps: NA 26→32 and EU
overflow 30→36. Both retain CPU limit 80/resume 65 and memory admission. Read-only
preflight found NA at 26 hunts, ~61% CPU, 14.6 GiB free; EU overflow at 30 hunts,
~50% CPU, 41.4 GiB free. A higher cap does not override resource admission.

Native candidate: exact source commit `9245d3f`, differing from guard-era production
native code by locale initialization only. DLL SHA-256:
`90E47D3432702B790D47888BE4DC86EF3A8EA941A544FB338F4D11D1425AEE4E`.
French/German/Italian decimal parsing regression tests passed for current and new
threads. The command-line hook returns `-CULTURE=en-US`; CRT locale is set to C
before server hooks. An actual hunt part-break check remains necessary.

Observed live disk DLLs: main EU, NA and Germany used `BEB17A8F…`, containing the
culture argument; EU overflow used `87AECD7B…`, lacking it. Absence of the locale
diagnostic text in a binary string scan is not evidence that its code is absent:
the tested Release compiler splits that literal. Verify new process runtime logs.

DeployServer suite: 99 tests passed. Per-host staged JavaScript syntax checks pass.
Live files/config hashes are recorded in staged manifests to detect concurrent edits.
Activation requires a drain or maintenance restart, backup and health verification.

The historical zero-health/no-down report remains unresolved. TFBTA and Laila were
named, but no currently stuck character was identified. Display health and server
health are separate; do not force healing or death without identifying which state
is wrong. No speculative combat changes are included in this patch.
