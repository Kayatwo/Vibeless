# Vibeless

Vibeless is the maintained foundation fork used by the Dauntless 1.4.4 backport project: the native
server/client runtime, deploy server, metagame and their release tooling. Content ports, package
recipes and the launcher live in a separate repository (Phase 0 decision D2). The 1.4.4 game client
is always supplied by the user and is never part of this repository or its releases.

## Branches and tags

| Ref | Meaning |
| --- | --- |
| `upstream/dauntless-revived` | Read-only mirror of `mixutin/dauntless-revived`, branch `dauntless-revived`. Never commit here. |
| `import/c39bcfa` | The upstream commit the backport lab was built from (v0.0.5, 2026-10-17). |
| `main` | Vibeless integration branch, rooted at `import/c39bcfa`. |
| `reconciled/installed-2026-10-19` | Source equivalent to the backport release installed on 2026-10-19 (runtime `9fe01048…3546`, BackportDev tag `installed-2026-10-19-test-arena`). |

## Changes on `main` (relative to `import/c39bcfa`)

1. `TArray` allocation hardening, SDK delegate assert fix, crash-report script frames, array tests.
2. Backport native features from the installed release: shared creature services, Deepfrost
   Gnasher, Thunderdeep Drask, Test Arena, opt-in reward diagnostics and parked development
   probes (compiled out). Lab-specific paths are still present and are being replaced by
   configuration.
3. Deploy/metagame TypeScript reproducing the installed JavaScript snapshots (see
   Reconciliation below).
4. Overspend refusal on by default; `INVENTORY_REFUSE_OVERSPEND=0` restores upstream clamping.
5. Repository rules: LF storage for text files (`.gitattributes`), Windows CI for SDK-dependent
   tests.

## Reconciliation evidence

Native: every tracked BackportDev runtime file equals `UndauntedInternalServer/` after CRLF→LF
normalization. Backend: TypeScript 6.0.3 emit compared with the installed `deploy-dist` and
`metagame-dist`: 117 files byte-identical, both hunt tables equal as parsed JSON (the lab writer had
re-serialized `0.0` as `0`), and `gameservers.js` differing only by calling `node:timers`
`setTimeout` through its import (the same function as the global) and one line break. The native
DLL is not expected to be byte-identical to the installed one (different compiler build and
timestamps); functional equivalence is checked by tests and, separately, by gameplay.

## Updating from upstream

Review upstream before each foundation release. Fetch into `upstream/dauntless-revived`, classify
every new commit (security/fix, foundation feature, public-fleet operation, economy/store change),
cherry-pick fixes onto `main`, and leave economy and pricing changes out unless explicitly chosen.
Run the backend, native and save-compatibility checks on disposable databases, then record the
result in [UPSTREAM-SYNC.md](UPSTREAM-SYNC.md). Installed servers are never updated automatically.

## Releases

A foundation release records the Vibeless commit it was built from (metagame `build-info.json`,
`SOURCE_URL` and release manifest), includes `LICENSE.txt`, `NOTICE.md` and MinHook's license, and
keeps a corresponding source archive ready for anyone who receives it or plays on a server running
it (AGPL-3.0 section 13). Game files and credentials are never included. Until Vibeless has its own
published-source proxy loader (decision D4), the prebuilt `dxgi.dll` from upstream remains a pinned
input whose source is unpublished, so releases are not yet source-complete for that component.
