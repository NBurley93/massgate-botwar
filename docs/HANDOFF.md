# massgate-botwar handoff

Last updated: 2026-09-25. Branch: `phase0/modern-build` (not pushed, not merged into `master`).

## Goal

Bring back World in Conflict's ranked multiplayer as a single-player experience: run
Massgate (the game's online backend, open-sourced by Ubisoft) locally, redirect the game
and its dedicated server (`wic_ds.exe`) to it, and play **ranked matches against bots**,
with a ladder and ranks that feel alive because they're populated by **simulated "ghost"
players**.

## Where things stand

| Phase | Status |
|---|---|
| 0: prove the stack | **Done.** Builds with VS2022, runs on MariaDB 13. The game creates an account and logs in; `wic_ds.exe` registers as a **ranked** server; the game shows it and joins. |
| Bot research | **Done.** Bots are built into `wic_ds.exe`. Ranked play blocks them in two places (addresses below). |
| Massgate side of ranked bots | **Done.** Bot stats entries (profile 0) are ignored instead of disconnecting the server. |
| 3a: ghost ladder (stage 1) | **Done.** 154+ ghosts from a callsign file, seeded careers, simulation on every reported match. |
| 2: dedicated-server hook | **Next.** Fork `Nukem9/wic-client`, point its redirect at 127.0.0.1, add the two ranked-bot patches. |
| 3b: bots named from the callsign pool (stage 2) | After the hook exists. |
| 3c: bots *are* ghosts (stage 3, stretch) | Give bot slots ghost profile IDs so real bot performance updates ghosts. |
| 1 / 4: one-click launcher, x64, SQLite | Deferred (SQLite only after there's a baseline to test against). |

Nothing has yet been tested **in-game** for the ghost ladder (ladder view, profiles).
It was verified through the database and with the fake dedicated server.

## Environment (this machine)

- Repo: `C:\Users\nickb\Development\wic-botwar\massgate-botwar`
- Game: Steam, **1.0.1.1 (build 35)** with Soviet Assault, symlinked into the repo as
  `World in Conflict` (gitignored) → `C:\Games\Steam\steamapps\common\World in Conflict`.
- Toolchain: VS 2022 Community (MSVC 14.44), CMake 4.4 and Python 3.13 via scoop.
  MariaDB server 13.0.2 via scoop.
- Hosts file (the user added these; needed until the Phase 2 hook exists):
  ```
  127.0.0.1 liveaccount.massgate.net
  127.0.0.1 liveaccountbackup.massgate.net
  127.0.0.1 stats.massgate.net
  127.0.0.1 www.massgate.net
  ```
  Without them these names resolve to a live Ubisoft AWS redirect; never run the game or
  DS against real DNS.
- `wic_ds.ini` in the game folder was edited by the user: `ReportToMassgate 1`,
  `RankedFlag 1`, `UseCDKey yes`, `GameName "Test Server!!"`. Backup: `wic_ds_backup.ini`.
- **CD key:** HKCU now holds the repo's *sample* key (sequence 1; the user imported
  `share/sql/install_cdkey.reg`). The user's own key is preserved only in
  `CdKeys`, encoded (`MMassgateServers -dbname live -getkey <encoded>`
  decodes it). Never print it. Offer a restore script if wanted.
- Local account: profile **Cabal** (id 1). Test profile **StatsTest** (id 900001) is on the
  ladder with fake matches; remove when no longer needed.

## Running it

```
.\scripts\bootstrap-deps.ps1     # once: builds 32-bit MariaDB Connector/C into deps/
cmake --preset win32
cmake --build --preset release   # or debug
.\scripts\setup-db.ps1           # idempotent: data dir in runtime/, users, schema, fixes, ghosts table
.\scripts\start-local.ps1        # MariaDB, web server on :80, Massgate on :3001 (with ghosts)
```

Then start `wic_ds.exe` from the game folder, and the game.

- `start-local.ps1 -NoGhosts` runs without the ghost ladder. `-Configuration Debug` uses the Debug build.
- `reset-ghosts.ps1` deletes all ghost rows (Massgate must be stopped); they're recreated on next start.
- `register-cdkey.ps1 [-Key ...]` adds a CD key to `CdKeys` without printing it.
- Massgate logs: `runtime/massgate/massgate_log_*.txt` and `massgate_sql_*.txt` (with `-logsql`).
  They're buffered; the console output is often more current.
- In this session Massgate was usually run as a background process from the Debug build:
  `cd runtime/massgate && ../../build/bin/Debug/MMassgateServers.exe live -noboom -all -dbname live -massgateport 3001 -logsql`

### Test tool

`build/bin/<cfg>/FakeDedicatedServer.exe -massgateserver 127.0.0.1 -cdkey LABGU3MFRG9G95GBAYTH -profile 900001 [-bots]`
registers a ranked server through the same client library `wic_ds` uses
(`MMG_TrackableServer`), reports end-of-match stats (optionally with a bot entry first),
and exits 0 if Massgate kept the connection. It also triggers the ghost simulation.
Run it from a scratch directory: it writes log files into its working directory.

## What changed in the codebase (commits on the branch)

1. **Build**: CMake presets (Win32 / VS2022), `scripts/bootstrap-deps.ps1`, `FindMySQL`
   fixed (`PATH`→`PATHS` typo) and taught MariaDB, DLL copied next to the exe,
   `MYSQL_OPT_RECONNECT` instead of the removed `st_mysql::reconnect`.
2. **printf-argument bugs** found with MSVC `/analyze` after annotating the variadic APIs
   with `_Printf_format_string_`: string objects passed to `%s` (crashed on the DB-connect
   failure path), a `%I64u` given a 32-bit ID in a `ClanInvitations` DELETE, missing args.
3. **Local stack scripts** (`setup-db`, `start-local`, `register-cdkey`, `common.ps1`) and
   `config.ini` → `127.0.0.1` (`localhost` resolved to `::1` first and stalled every pooled connection).
4. **SQL NULL → 0** in `MDB_DataItem` numeric conversions. MariaDB returns NULL for
   `UNIX_TIMESTAMP('0000-00-00 00:00:00')`; `atoi(NULL)` crashed the login path.
5. **`share/sql/fixes.sql`**: the missing `Authentication` table (the token refresher
   updated it every few minutes and the Debug build stops on SQL errors).
6. **Bot stats**: `PrivHandleReportPlayerStats` drops `profileId == 0` entries before
   processing. `GAME_FINISHED` acquaintance pairs skip profile 0. Plus `tools/FakeDedicatedServer`.
7. **Ghost ladder**: `src/Server/MMS_GhostLadder.{h,cpp}`, `share/sql/ghosts.sql`,
   `share/ghosts/callsigns.txt`, `scripts/reset-ghosts.ps1`, README section.

## Key facts discovered

### Massgate / database
- Ranked DS permission = `CdKeys.groupMembership` bit `isRankedServer`. The seeded keys
  have 255.
- Rank = highest `WICRankDefinitions` row with `totalScore <= career score` **and**
  `ladderPercentage <= ladder percentile`. Up to 2nd Lieutenant needs 0%; 1st Lt 33%, ...
  General 99%.
- Player ladder = `MMS_BestOfLadder` / table `BestOfLadder`: sum of the best
  `BestOfLadderNumGames` (20) scores within `BestOfLadderNumDays` (21). Percentile =
  `100 - position*100/count`. Winners' ladder score is ×`PlayerWinningScoreMultiplier` (1.5).
- Anti-cheat: a match score > `PlayerScoreCap` (5000) is ignored; > `PlayerScoreSuspect`
  (3000) is logged. Ghost matches are capped at 2900.
- `MMS_PlayerStats::UpdatePlayerStats` needs a `PlayerStats` row (created with the profile).
- `MC_CommandLine` lowercases the whole command line, truncates it at 255 chars, and a `-`
  inside a value starts a new option. That's why the ghost callsign path is in `config.ini`
  (`[ghosts] callsigns=`), and why CD keys are passed without dashes.
- Massgate `-all` opens ~150 DB connections, so `max_connections=500` is in `my.ini`.

### wic_ds.exe 1.0.1.1 (image base 0x400000)
- Hidden bot settings in `wic_ds.ini`: `[BotMode]` (0 none, 1 even teams, 2 players-vs-bots,
  3 advanced), `BotMode_AutoEven_NumPlayersPerTeam/_BotDifficulty`,
  `BotMode_PvsB_BotTeam/_NumBots/_BotDifficulty`,
  `BotMode_Advanced_Team{1,2}Bot%u_{Active,Difficulty,RoleId,AIDefinition}`.
  (Difficulty values: not yet verified.)
- `EXD_DedicatedServer::ReadIniFile` fields: `RankedFlag` +270h, `TournamentFlag` +26Ch,
  `ClanMatchFlag` +26Dh, `BotMode` +2ACh (read at 0x406CC2). The ranked-rules block
  starts at 0x40721F and runs when Ranked && !Tournament && !ClanMatch.
- **Gate 1**: `0x4073EA  je 0x40741C` (`74 30`) skips "Ranked game, forcing bot mode to
  none". **Patch to `EB 30`.**
- **Gate 2**: in `EXR_MOSGameFinderServer::ReportStatsAtEndOfGame` (0x5624B0) a loop
  calls 0x678E80 (returns `slot+DF6Ch`, probably the profile ID) and on 0 does
  `0x56258C  je 0x562792` (`0F 84 00 02 00 00`), meaning "BOTS DETECTED IN RANKED GAME",
  report aborted. **Patch: NOP the 6 bytes.** The following loop builds one
  `PlayerMatchStats` per slot, deduplicated by profile ID, so all bots become a single
  profile-0 entry, which Massgate now ignores.
- Also in the ranked block: MinPlayers is forced into 3..5 per team, TimeLimitMultiplier to
  1.0, password and mod to "no".
- Bot names ("Infantry"/"Armor"/... + "Aggressive"/"Defensive"/"Balanced") come from data
  (`mySkirmishAIDefs` in the game archives), not strings in the exe.
- `EXCO_PlayerInfo` (from wic-client): `myType` +0x4 (0 human, 1 AI),
  `myMassgateProfileId` +0x8, `myName` +0x238 (`MC_Str<wchar_t>`), `myAIConfigFile` +0x23C.
  Unverified guess: the player container's `slot+DF64h` is a `PlayerInfo`, which would make
  `slot+DF6Ch` its profile ID. Candidate hook point for renaming bots:
  `EX_AIPlayerContainer::CreatePlayer` (log string "created ai player from file %s on slot %d").
- `wic_ds.exe` contains the `massgateserver` override string (it may honor
  `-massgateserver`); `wic.exe` does not.

### massgate.org / wic-client
- Source of the community "MP fix": **github.com/Nukem9/wic-client** (LGPL-3.0). It's a
  `dbghelp.dll` proxy (forwards to the original, renamed `dbghelp_old.dll`) that loads
  `wic_{ds,cl,bt,ed}_hook.dll` according to the exe. The hooks use Detours with fixed
  addresses and check the build string at 0x753C64. The DS hook removes many Commander-AI
  asserts (bots crash the stock DS otherwise).
- **Do not install the prebuilt DLLs** (the user downloaded them to `mp_fix_massgateorg/`,
  gitignored): every hook rewrites `*.massgate.net` → `*.massgate.org` (a public server),
  and CL/DS/BT change the protocol to skip CD-key checks (incompatible with stock Massgate).
- `Nukem9/massgate` is just a stale 2017 fork; the massgate.org backend isn't public.

## Ghost ladder design (implemented)

- Every valid callsign becomes an ordinary profile owned by the locked account
  `ghosts@botwar.invalid`. `GhostPlayers` holds skill, matchesPerDay, favoriteRole,
  lastSimulated and isActive. Persona = hash of the name (stable across restarts).
- At startup (in the `MMS_MasterServer` ctor, **before** `MMS_LadderUpdater` loads the
  ladder), missing ghosts are created. The career is simulated in memory (recent joiners
  weighted) and written to `PlayerStats`; the best ladder scores of the window go to
  `BestOfLadder`; ranks are computed like the real flush (off-ladder counts as 0%).
- A thread (`MMS_GhostLadder::Run`) runs at startup and whenever
  `PrivHandleReportPlayerStats` finishes. For each active ghost it plays
  Poisson(matchesPerDay × elapsed) matches (elapsed capped at the ladder window, at most
  20 per run) through `UpdatePlayerStats`, and updates `Profiles.lastLogin`.
- Callsign rules = the game's profile rules: 3–13 printable ASCII chars, no
  `CLAN`/`PLAYER`/`PROFILE`/`|`/`\`; also no `'` (for SQL). Names taken by real players
  are skipped.

## Open items / next steps

0. **Verify the medals fix** (commit 7528750; built, not yet run). Opening a ghost's profile in-game
   disconnected the game (Massgate log: "Post mortem debugging: 197" =
   `SERVERTRACKER_USER_PLAYER_MEDALS_REQ`), because ghosts were seeded without the
   `PlayerMedals`/`PlayerBadges` rows that profile creation adds.
   `PlayerStatsEntry::Load` fails without the medals row, which also meant each
   such ghost's first simulated match was dropped. The fix inserts both rows when seeding
   and backfills existing ghosts (`INSERT IGNORE ... SELECT FROM GhostPlayers`) on startup.
   **Verify:** start the stack, open several ghost profiles in-game (medals and badges tabs), and
   check the log has no "Post mortem debugging" lines. The next start also creates the
   user's newly added friend-name ghosts.
1. **Callsign file**: the user added friend names; **19 of 239 are skipped** (18 longer
   than 13 chars: e.g. "Classified_Information", "Vincent Van Gogurt", "Bill Nye the High
   Guy", "Microsoft Excel"; 1 with an apostrophe: `FRaG'[D]`), and `Bearcat` is listed twice.
   Options: shorten them, or allow ghosts up to the full `MMG_ProfilenameStringSize`
   (ghosts never have a clan tag) and escape `'` with `MakeSqlString`. Check the client's
   name buffers before relaxing the length.
2. **Phase 2 hook**: fork wic-client → minimal `dbghelp.dll` proxy + DS hook with the build
   check, a DNS redirect to 127.0.0.1 (makes the hosts file unnecessary), the two
   ranked-bot patches, and their AI assert fixes. Decide what to do with their CD-key
   protocol change (probably drop it). Then a client hook for the redirect only.
3. **In-game test of ranked + bots**: `RankedFlag 1` + `BotMode 1`, and check a finished
   match writes stats. Open questions: do bots count toward MinPlayers for match start?
   What does the `GAME_FINISHED` player list contain for bots? (Massgate handles profile
   0 either way.)
4. **Stage 2**: rename bots from the callsign pool (hook after AI player creation, before
   the player list is sent to clients), e.g. "Kowalski (Armor)".
5. **Stage 3 (stretch)**: set a ghost's profile ID on its bot so bot stats update that
   ghost. Risk: the DS may then treat bots as Massgate players (auth, invites, kicks).
6. Cleanup: remove test profile StatsTest (900001) and the one junk profile-0 row in
   `MatchStatsPerPlayer`. Offer a restore script for the user's own CD key.
7. Unexplained: before the key swap the game reported a different key sequence
   than the one the server's validator computes for the registry key.
8. Later: one-click launcher (Phase 1), x64 port (inline `__asm` in MCommon2), SQLite
   (after regression tests exist), Release builds without PDBs (consider RelWithDebInfo).

## Working agreements with the user

- Plan before big moves; the user makes the calls on scope.
- Ask before downloading anything or touching the game install / registry / hosts file.
- The user runs anything that handles their real CD key.
- Commit on the feature branch when asked; line endings in `src/` are CRLF.
