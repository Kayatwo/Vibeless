# Upstream sync log

Upstream: `https://github.com/mixutin/dauntless-revived`, branch `dauntless-revived`.

| Date | Upstream range | Result |
| --- | --- | --- |
| 2026-10-19 | `import/c39bcfa` | Imported as the base of `main` (the revision the backport lab was built from). |
| 2026-10-19 | `c39bcfa..175f4e9` (29 commits) | Reviewed; 5 adopted, details below. |

### Review of `c39bcfa..175f4e9` (2026-10-19)

| Upstream | Subject | Decision |
| --- | --- | --- |
| `c7cde20` | Honor capacity retry delays (metagame); gateway session refresh | **Adopted.** Matchmaking fix; the gateway part is not shipped in Vibeless bundles. |
| `2afe534` | Serialize oldest hunt allocations; private retries never join the public queue | **Adopted.** Keeps private sessions such as the Test Arena out of public queues. |
| `87bc989` | Party capacity lease expiry and orphaned queue deadlocks | **Adopted.** Party matchmaking fix. |
| `98be0bc` | Grant Trials weapon parts as instanced items | **Adopted.** The client requires instanced parts; economy-neutral. |
| `055702e` | OpenBSD CI loopback peer | **Adopted.** CI only; likely cause of the intermittent OpenBSD job. |
| `cf1b8cc`, `528af31`, `d9e99bd`, `67b2179` | Native hunt idle grace 120 s; deploy reaps empty, silent and over-age hunts (hard 2 h ceiling) | **Deferred to Phase 2** (decided 2026-10-20). Vibeless already retires hunts after two minutes of confirmed vacancy (inherited from `c39bcfa`). The additions target fleet-scale leaks; the hard age ceiling would end long Test Arena or solo sessions, and the shorter native grace affects slow first loads. Revisit as a per-profile setting with a real-game test. |
| `d955566` | Align shop rotation with the Middleman weekly reset | **Deferred.** Store behaviour; optional. |
| `a481193` | Halve Rams cosmetic prices | **Excluded.** Economy change. |
| `18d6ce0`, `bd9c74a` | Auto-suspend accounts using a known unlocker tool; its test | **Excluded.** Public-server moderation; not needed for a private profile. |
| `b3db5d6`, `d88d939`, `00db014`, `8036e70` | Granny purchase pilot, account-once creator reward, launcher 0.1.39 release | **Excluded.** Public-service features and economy; `00db014` also always refuses `CURRENCY_NOTES` overspend, which Vibeless already does by default. |
| `14d15b9` | Launcher public relay restriction | Not applicable (upstream launcher not used). |
| `87bd878` | Worker capacity back-off across queues | Not applicable (fleet overflow workers). |
| `a81404d`, `2305480`, `bb05bd5` | Discord bot and fleet dashboard tools | Not applicable. |
| `55a0109`, `331b834`, `5e5b309`, `b59fbfc`, `0c2e751`, `d13252c`, `175f4e9` | Operations records, README | Not applicable. |
