# Creator reward and Granny production rollout

Launcher 0.1.39 enables the accepted Granny purchase-tab and creator inventory-refresh code without experimental environment flags. Native SHA-256: `6f2433abebdee451edfc9060fa58eb3e2a3e73e5f7063de05386316b52d7b30b`. The client, friend-kit and Windows server-kit pins agree. No test-account fixtures, quest completions, account unlocks or inventory grants are part of deployment.

`Void` grants 10000 ordinary Rams once per account. The permanent account receipt and inventory grant commit atomically, including across competing backend processes and character switches. The authenticated account controls the recipient; request account IDs, quantities and source strings cannot authorize grants. GET is read-only. Invalid authentication/codes, repeats, malformed inventories and unsafe native integer balances cannot consume another reward.

Granny keeps the game's quest requirements, recipe prices, crafting handlers and native server transactions. Only initialized potion tabs are shown. The shared backend now always refuses an excessive `CURRENCY_NOTES` debit, even if legacy material clamping is enabled: a stale native balance cannot grant a tonic after funds run out. Other material behavior is unchanged. The isolated accepted gameplay audit recorded 18000 Rams spent, 40 purchased items and 10 crafted items with matching material removal.

Validation: 130 backend tests pass, including simultaneous purchases with funds for exactly one, request replay, forged raw inventory mutations, creator claims from independent processes and native integer limits. Launcher typecheck passes; 305 launcher tests pass and 3 are skipped. Release x64 DLL and Windows installer build pass. Native DualSense, console visibility, Rams protocol, locale and privileged RPC guard tests pass. Repository secret/game-file checks pass for tracked files and the pending canonical-branch history.

Deployment artifacts are staged under each host's `staging/creator-granny-20261018`. Backend patch scope is `routes/store.js`, `controllers/creator.js` and `controllers/inventory.js`, with existing helper/rate-limit dependency hashes verified. No schema migration is needed. Deployment scripts verify old/new hashes, back up source, and roll back source on restart or health-check failure.

Existing native game images can remain mapped while `[IO.File]::Replace` atomically installs the new DLL and saves the old image. This was verified against a loaded isolated test DLL. Only exact Ramsgate-map processes are recycled for the vendor feature; allocator services and other game processes are not stopped. In-flight hunts retain their prior image until they exit normally.

Preflight found the live database approximately 25GB with only 24GB free on C:. The external-connection backup was stopped before activation. The replacement backup uses the existing writer-owned `/internal/backup` endpoint into a new NTFS-compressed backup directory, avoiding repeated backup restarts from external live writes and a second uncompressed database. The database remains unchanged; no source VACUUM or deletion is performed. Backup verification and final live activation results will be recorded below.

Status: production build checked and staged; live activation and published launcher verification pending.
