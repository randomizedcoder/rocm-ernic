# Status: NIC realism

This file traces what has been built against [IMPLEMENTATION-PLAN.md](IMPLEMENTATION-PLAN.md), which in turn implements [DESIGN.md](DESIGN.md).

**Rule.** Every work session that changes a feature branch also updates this file on `docs/nic-realism`:

- the task row: state, commit, PR and evidence
- a log entry for any decision, finding or deviation

Evidence is the exact command and its result, not a summary like "tests pass".

States: `planned`, `in-progress`, `review` (PR open), `merged`, `dropped`. A dropped task gives its reason in the Deviation column.

## Summary

| PR | Branch | State | PR link | Integration commit |
|---|---|---|---|---|
| A: LIF stats | `feat/ionic-lif-stats` | in-progress (A01–A07 committed on F, not pushed) | – | – |
| B: port stats | `feat/ionic-port-stats` | planned | – | – |
| C: queues/vectors | `feat/ionic-eth-queues` | planned | – | – |
| D: RDMA counters | `feat/ionic-rdma-error-stats` | planned | – | – |
| E: fault injection | `feat/ionic-fault-inject` | in-progress (E01) | – | – |
| F: NIC profiles | `feat/ionic-nic-profiles` | in-progress (F01–F04, F06 committed, pushed to the fork) | – | – |

## Task trace

| ID | Task | State | Commit(s) | Evidence | Regression proof | Deviation from plan |
|---|---|---|---|---|---|---|
| NR-A01 | counter block + lif layout helper | in-progress (`feat/ionic-lif-stats`) | `139b6eb` | `test_ionic_emu_stats`: 194 rows ok (counting, MAC class, fill, every named index pinned against a mirror of the kernel struct, `offsetof(ionic_lif_info, stats)` 320) | remove the broadcast check → `FAIL positive: broadcast MAC; expected: IONIC_EMU_BCAST (2); got: 1`; fold with `atomic_load` → `FAIL corner: inbox-full atomic folded twice (and once by fill); expected: counted once (1); got: 3` |  |
| NR-A02 | keep `lif_info_pa`; clear on reset | in-progress (`feat/ionic-lif-stats`) | `fe49510` | `test_ionic_stats_publish` publish rows (LIF_RESET, device reset, re-init to a new pa) | drop the LIF_RESET clear → `FAIL negative: LIF_INIT, LIF_RESET, then publish twice; expected: writes 0; got: writes 2` | device reset also zeroes the counters |
| NR-A03 | tx/rx MAC-class and drop counting | in-progress (`feat/ionic-lif-stats`) | `fe49510` | `test_ionic_stats_publish` datapath rows: tx sink, send refused, rx TAP ok / no buffer / too big, tx disabled / len 0 / bad address, inbox back-pressure, inbox full | requeue every inbox error again → `FAIL corner: a mesh frame too big for the buffer, then one that fits; expected: rx_ucast 1, rx_ucast_drop 1, rx_desc_data_error 1, still queued 0 (no head-of-line block); got: rx_ucast 0, rx_ucast_drop 0, rx_desc_data_error 0, still queued 2` | frames counted in sink mode (no TAP); inbox head-of-line block fixed (non-ENOBUFS errors drop and count); `rx_queue_disabled` not counted (poll_rx skips invalid queues); tx descriptor/DMA failures have no MAC class, so only the ring counter moves |
| NR-A04 | publish tick + `--stats-interval-ms` | in-progress (`feat/ionic-lif-stats`) | `fe49510` | `test_ionic_stats_publish` 13 publish rows; `ionic-stats-interval-cli` 16 rows (10/60000 ok; 0, 9, 60001, `abc`, `100ms`, -5, empty, 2^32+10 rejected; flag beats `--nic-profile` in either order); `test_ionic_profile` 69 ok; guest (`nix run .#run-ionic-guest-test` on a throwaway merge of A into `xtcp2-integration` `3ae9d46`, test extended to ping and print the stats file): after `ping -c 100 -i 0.01` to a static neighbor (loopback server, so no replies), `ip -s link` TX 106 packets / 10316 bytes (2 / 180 before the ping; all zero before A), ethtool `tx_packets` 106; stats file at shutdown `tx_ucast_packets` 100, `tx_ucast_bytes` 9800, `tx_mcast_packets` 7, `tx_mcast_bytes` 586 (107 / 10386: one 70-byte multicast frame after the dump, within one interval); `ERNIC_GUEST_PASS` | skip the publish → 9 rows fail, first `FAIL positive: LIF_INIT(pa), then publish; expected: writes 1, block poisoned 0, before 320, after 64; got: writes 0, block poisoned 1024, before 320, after 64` | first write straight after LIF_INIT, not after one interval, so the emulator needs no clock; `[stats]` in `compat` changes its content hash |
| NR-A05 | RDMA bytes → lif `*_rdma_ucast_*` | in-progress (`feat/ionic-lif-stats`) | `36ca096` | `test_ionic_cq_arm` `dp_rdma_bytes` rows: tx, rx, local copy both, 0 bytes, 2^31 | drop the rx direction in `dp_rdma_bytes` → `FAIL: positive: a RECV or WRITE landing in guest memory; expected: file 4096, lif tx 0, lif rx 4096; got: file 4096, lif tx 0, lif rx 0` (and the local-copy row) | one packet per message, not per MTU segment |
| NR-A06 | lif counters in `--stats-file` | in-progress (`feat/ionic-lif-stats`) | `ad25bfc` | `test_ionic_stats_publish` 6 stats-file rows (values, shape and alignment, UINT64_MAX, folded inbox counted once, no emulator); the guest check in NR-A04 read the section from a live server: 74 lines, the counters matching `ip -s link` | `ionic_eth_emu_get_stats` without the fold → `FAIL corner: mesh inbox full off-thread, then the stats file twice; expected: rx_ucast_drop_packets 1, rx_ucast_drop_bytes 64, second write 1 (fold is not double-counted); got: rx_ucast_drop_packets 0, rx_ucast_drop_bytes 0, second write 0` (and the inbox-limit row) | a registered section callback (`pvrdma_set_stats_section`), since `src/rdma` must not depend on the eth emulator |
| NR-A07 | `docs/ionic.rst` Statistics section | in-progress (`feat/ionic-lif-stats`) | `9057008` | codespell clean |  |  |
| NR-B01 | keep `port_info_pa`; clear on reset | planned | | | | |
| NR-B02 | wire-side classification, buckets, PCP | planned | | | | |
| NR-B03 | publish port stats at +576 | planned | | | | |
| NR-C01 | queue profile keys + `--eth-queues` | planned | | | | |
| NR-C02 | 128 MSI-X vectors | planned | | | | |
| NR-C03 | serve all RX queues + flow hash | planned | | | | |
| NR-D01 | 64-entry stats table + name rule | planned | | | | |
| NR-D02 | `cq_post` / RNR / flush hooks | planned | | | | |
| NR-D03 | per-QP error counters | planned | | | | |
| NR-D04 | RDMA counters into lif `rdma_*` | planned | | | | |
| NR-E01 | fault core, deterministic patterns, `stats_vals` `fail`, status codes | in-progress (`feat/ionic-fault-inject`, from #222 `66c5141`) | `b58b588` status codes, `c90136b` fault core, `7d43656` fuzz `fault_parse`, `8691e36` `stats_vals` site + `--inject`, `29796c4` admin CQE status | `ERNIC_INJECT='stats_vals: after 2 effect fail' nix run .#run-ionic-guest-test` on the integration merge: startup log `Fault: stats_vals: after 2 count 1 effect fail status eio`; server `ionic_adminq STATS_VALS: injected fail status 3 (event 2)`; guest `cat …/hw_counters/commands: Invalid argument`, dmesg `ionic_0: opcode 5 error 3`; `lifespan` and the four `total_bytes_*` then read normally (6 STATS_VALS, 1 failed); `ERNIC_GUEST_PASS` | (1) `every` as `k % n == 1` → `FAIL positive: 'every 100' over 1000 events; expected: fires 10 times, at events 100, 200, ... 1000` / `got: event 1 (index 1): fired 1, want 0`. (2) status codes reverted to 1 → `FAIL negative: QP values for an unmapped qp_id; expected: ENOENT (7)…` / `got rc 1 (want 7)`, and `FAIL negative: device values when the counter snapshot fails; expected: EIO (3)…` / `got rc 1 (want 3)`. (3) hook removed → `FAIL positive: device values with 'stats_vals: every 1 effect fail'; expected: EIO (3) and no bytes written` / `command 1: got rc 0 (want 3), 40 bytes written`. (4) CQE `status << 24` restored → `FAIL positive: a completion with EIO; expected: status_length 3, so dmesg reads 'error 3', and the error bit set` / `got status_length 50331648 (want 3), error bit 1`. All via `ctest -R 'ionic-(fault|stats)-unit' --output-on-failure` in `nix develop` | `fault_*` stats-file lines deferred to E09; status codes extended beyond §10.7; guest check uses `after 2`, not `after 1` (registration read); admin CQE status encoding fixed (Log 2026-10-12, E01 decisions and E01 gates) |
| NR-E02 | LFSR113 + netem seeding; `random`/`state`/`gemodel` | planned | | | | |
| NR-E03 | gates; several faults per site | planned | | | | |
| NR-E04 | `stale`/`zero`/`reset`/`wrap`; `qp_stats_vals`; `stats_publish` | planned | | | | |
| NR-E05 | deferred completion: `delay`/`timeout`/`hang` | planned | | | | |
| NR-E06 | `.dist` reader + jitter | planned | | | | |
| NR-E07 | Ethernet sites and effects | planned | | | | |
| NR-E08 | RDMA sites and effects | planned | | | | |
| NR-E09 | counters, trace, replay; `hang` guest confirmation | planned | | | | |
| NR-E10 | netem conformance VM test | planned | | | | |
| NR-E11 | `[fault.*]` profile sections, overlays, `docs/ionic.rst` | planned | | | | |
| NR-F01 | profile loader + strict validation + layering | in-progress (`feat/ionic-nic-profiles`, from `upstream/develop` `e912e84`) | `87e02ee` loader, `e69d3df` fuzz `profile_parse` | `test_ionic_profile` 62 ok in gcc, clang, ASan, TSAN; `fuzz_profile_parse`: 26 seeds ok, 60 s `run-fuzzers` 3.53M execs, cov 302→343, corpus 25→188, 0 crashes | (1) the scanner lets unknown keys through → `FAIL negative: a mistyped key, link.sped_mbps; expected: error naming the line and the key; nothing changes` / `error: (none)`, plus 2 more rows. (2) values applied as they are read instead of staged → `FAIL corner: a layer that fails after a good one; expected: the good layer stays; nothing from the failed one, not even the two valid keys set before its bad value` / `got: … speed 40000 …`. All via `ctest -R ionic-(profile|identity)-unit --output-on-failure` in `nix develop`, each mutation applied by a scratch script and reverted | `[fault.*]` sections and CLI-over-profile precedence deferred to E11 and A (Log 2026-10-12, F decisions) |
| NR-F02 | `--nic-profile` / `--print-profile` / logging | in-progress (`feat/ionic-nic-profiles`, from `upstream/develop` `e912e84`) | `adb9af6` | startup log `NIC profile: compat (hash 0x257d6af01c11e7d2)`; with a file, `NIC profile: compat + …/fast.ini (hash 0x2c884c8e5f2f70b1)`; stats file `nic_profile` line in the Instance block | covered by the F01 rows (the CLI calls the same loader) | `nic_profile` stats line via a process-global in `pvrdma_stats.c`, not a `PVRDMADev` field (vendored) |
| NR-F03 | identity and link keys | in-progress (`feat/ionic-nic-profiles`, from `upstream/develop` `e912e84`) | `fc69e1d` | `test_ionic_identity` 21 ok (golden `tests/golden/ionic_compat_identity.h` from `e912e84`). Guest, `ERNIC_NIC_PROFILE=~/Downloads/rocm-ernic-tools/f/fast.ini nix run .#run-ionic-guest-test` on the integration merge (elba rev 3, fw `1.8.0-E-32`, 400000, fiber, mtu_max 9000): `ip -d link` maxmtu 9000, `ethtool -i` firmware-version `1.8.0-E-32`, `ethtool` Speed 400000Mb/s, Port FIBRE, RDMA `rate` `400 Gb/sec (4X NDR)`, dmesg `FW: 1.8.0-E-32` and `Link up - 400 Gbps`, `PORT_ACTIVE`, `ERNIC_GUEST_PASS`. With no profile: identical to the pre-F run apart from per-queue tx counts and timestamps (maxmtu 9198, `rocm-ernic-1.0`, 100000Mb/s, Direct Attach Copper, combined 4, `Link up - 100 Gbps`) | compat `fw_version` changed to `rocm-ernic-1.1` → `FAIL corner: a new emulator with no profile set; expected: every block byte-identical to develop e912e84` / `dev_info_regs differs at byte 25: got 0x31, golden 0x30`, plus the other golden and speed rows. All via `ctest -R ionic-(profile|identity)-unit --output-on-failure` in `nix develop`, each mutation applied by a scratch script and reverted | compat `link.mtu_max` is 9198 (the identity's `eth.max_mtu`); port `config.mtu` stays 9000, as `min(mtu_max, 9000)` |
| NR-F04 | built-in `compat`, `small-vm` + `profiles/` | in-progress (`feat/ionic-nic-profiles`, from `upstream/develop` `e912e84`) | `87e02ee` | `test_ionic_profile` rows: each built-in parses, and is byte-identical to `profiles/<name>.ini` | covered by the golden rows (F03) | `small-vm` equals `compat` until C adds queue keys (Phase 0 deviation) |
| NR-F05 | A–E keys and built-ins, per PR | planned (keys land with A–E) | | | | |
| NR-F06 | `docs/ionic.rst` NIC profiles section | in-progress (`feat/ionic-nic-profiles`, from `upstream/develop` `e912e84`) | `60590ee` | "NIC Profiles" section: usage, layering, validation, `--print-profile` and hash, key table; codespell clean | – | |

## Gate record

One row per push of a feature branch.

| Date | Branch @ commit | clang-format | gcc / clang warnings | ctest default / ASan / TSAN | codespell | analysis (`newfindings.py`) | guest test |
|---|---|---|---|---|---|---|---|
| 2026-10-12 | baseline `upstream/develop` @ `e912e84` | – | – | – | – | `analysis-deep`: clang-tidy 4390, cppcheck 214, flawfinder 374, clang-analyzer 3, gcc-warnings 16, gcc-analyzer 11, semgrep 165, triage 404; `analysis-sanitizers` and `analysis-tsan` clean; `analysis-valgrind` ERROR SUMMARY 3 (1 definitely-lost block); `fuzz-run` 0 crashes | – |
| 2026-10-12 | baseline #222 `feat/ionic-hw-stats` @ `66c5141` | – | – | – | – | `analysis-deep`: clang-tidy 4415, cppcheck 215, flawfinder 383, rest as develop; tsan clean; sanitizers: the pre-existing SIGTERM-shutdown leak (6 indirect blocks, 13 MB; see Log 2026-10-12, F gates; earlier recorded as clean in error); valgrind ERROR SUMMARY 3 (1 definitely-lost block, as develop); `fuzz-run` 0 crashes (rdma_cm_proto, net_headers, ionic_stats) | `run-ionic-guest-test` on `xtcp2-integration` + counter dump: PASS |
| 2026-10-12 | `feat/ionic-fault-inject` @ `29796c4` (gates run on the identical tree before it was committed) | v18: clean | gcc 15: 0 warnings besides `-Wleading-whitespace`; clang: only `-Wused-but-marked-unused` (glibc fortify), the kind the base has | 34/34 in each of gcc, clang, ASan, TSAN; `test_ionic_fault` 71 ok, `test_ionic_stats` 37 ok | clean | `analysis-deep` vs #222: no new high-confidence triage entries in new code (the two new ones are the existing `fprintf(stderr)` CLI-error style in `rocm_ernic_server.c`); cppcheck `constParameterPointer` fixed; flawfinder `vsnprintf` [4] ×2 in format-attributed helpers, as `nvmeof_target.c` and `s3_http.c`; the rest clang-tidy style kinds the base has throughout. `analysis-sanitizers`, `analysis-tsan` clean; `analysis-valgrind` ERROR SUMMARY 3, as base; `fuzz-run` and a 60 s `run-fuzzers`: 0 crashes (`fault_parse` 3.3M execs, cov 274→336, corpus 22→194, no leaks on replay) | `after 2` PASS with dmesg `opcode 5 error 3`; `after 1` FAIL by design (see Log) |
| 2026-10-12 | `feat/ionic-nic-profiles` @ `60590ee` (not pushed) | v18: clean | gcc 15: 0 warnings besides `-Wleading-whitespace`; clang: only `-Wused-but-marked-unused`, as the base | 33/33 in each of gcc, clang, ASan, TSAN; each of the 4 code commits builds and passes alone (31, 32, 32, 33 tests) | clean; cmakelint clean | `analysis-deep` vs develop: no new high-confidence findings in new code (triage in Log); cppcheck `constVariablePointer` and `variableScope` ×2 fixed; `analysis-tsan` clean; `analysis-sanitizers`: the pre-existing shutdown leak only, as #222; `analysis-valgrind` ERROR SUMMARY 3, as base; `fuzz-run` 0 crashes; 60 s `run-fuzzers` 0 crashes | compat: PASS, same as before F; `fast.ini`: PASS, values in NR-F03 |
| 2026-10-12 | `feat/ionic-lif-stats` @ `9057008` (on F `60590ee`, not pushed) | v18: clean | gcc 15: 0 warnings besides `-Wleading-whitespace`; clang: only `-Wused-but-marked-unused`, as the base | 36/36 in each of gcc, clang, ASan, TSAN; each of the 5 commits builds and passes alone (34, 36, 36, 36, 36 tests); `test_ionic_emu_stats` 194 ok, `test_ionic_stats_publish` 30 ok, `ionic-stats-interval-cli` 16 ok | clean; cmakelint clean | `analysis-deep` vs F: the 4 findings of the pre-fix run (`NonNullParamChecker` in `new_emu`, `shadowVariable`, `constVariablePointer`, implicit widening in `IONIC_LIF_STATS_BYTES`) fixed and gone; the rest are clang-tidy style kinds the base has throughout, and the existing `fprintf(stderr)` CLI-error style; cppcheck `knownConditionTrueFalse` at `ionic_eth_emu.c:1854` is a false positive (the test stub of `ionic_eth_net_send` returns 0; the real one returns `-errno`); flawfinder `memcpy` [2] in `ionic_emu_stats.c` copies one 8-byte qword into the fixed block. `analysis-sanitizers` and `analysis-tsan` clean; `analysis-valgrind` ERROR SUMMARY 3, as base; `fuzz-run` 0 crashes | PASS: `ip -s link` TX 106 packets after `ping -c 100`; stats file `tx_ucast_packets` 100 (NR-A04/A06) |

## Log

### 2026-10-12: design baseline

- **Fleet profile.** A read-only fleet scan gave the reference shape in DESIGN §3, aggregated by NIC family:
  - modern 100–400G NICs run 48–128 queues, carry 94–300 non-queue counters (46–127 of them error-type), and show 2–16 non-zero errors per port
  - RDMA `hw_counters` number 22–93
  - the emulated ionic NIC has 4 queues, 5 RDMA counters and no real error counters
- **Driver finding: `ip -s link` is always zero.** The ionic netdev stats come from the firmware-written `lif_info.stats` (offset 320), which the emulator never writes. The 126 MAC counters at `port_info` + 576 are in the same state. This is now PR A/B.
- **Driver finding: one STATS_VALS failure fails every `hw_counters` file.** `ionic_admin_wait` maps any admin CQE error to `-EINVAL`, and `ionic_get_hw_stats` fetches every counter with one `STATS_VALS`. This is the design basis for E01.
- **Naming constraint.** The kernel limits `hw_counters` names to 27 characters. 11 of the 27 `rdma_req_*`/`rdma_resp_*` ABI names exceed that, so DESIGN §9 sets the shortening rule.
- **Decisions:**
  - the docs live on the fork-only `docs/nic-realism` branch, in Markdown
  - feature PRs branch separately
  - `IONIC_DEV_CAP_EXTRA_STATS` stays unset, pending maintainer input (DESIGN §14)

### 2026-10-12: configurable profiles (PR F)

- **Decision: everything is configurable.** Every shape value becomes a NIC profile key: identity, link, queues, stats and RDMA counter set and names, and faults. DESIGN §11 describes the profiles.
- **Format:** ini profiles through glib `GKeyFile`, which is already a dependency. They can be layered with a repeatable `--nic-profile`, CLI flags override them, and validation is strict.
- **Default:** the `compat` profile is byte-identical to today's device. That replaces the earlier "default 16 queues" decision; queue counts now live in the `generic-*` profiles.
- **Naming:** no built-in profile is named after a real product until calibrated data from that card exists.
- **Order:** F goes after E01 and before A, so later PRs add keys instead of constants.

### 2026-10-12: netem-style fault model (PR E)

- **Decision: model the faults on `tc netem`.** A fault is site × pattern × effect × gate (DESIGN §10).
  - Patterns: the deterministic `every`, `after`, `burst` and `sequence`, plus netem's `random`, `state` and `gemodel` with the same parameters.
  - Gates: `window`, `start`, `stop`, `limit`.
  - A site can have several faults.
- **Flaky `hw_counters`.** The stats-read effects are `fail`, `stale`, `zero`, `reset`, `wrap`, `delay` (with netem distributions), `timeout` and `hang` (destructive, opt-in). Built-in overlays cover each behaviour (`flaky-`, `unreadable-`, `stuck-`, `resetting-`, `wrapping-` and `slow-hw-counters`).
- **Driver finding: the timeouts are confirmed in source.** `ionic_admin_wait` with `IONIC_ADMIN_F_INTERRUPT` waits `HZ` (1 s), returns `-ETIMEDOUT`, logs `wait status`, and calls `ionic_admin_cancel`. The watchdog fires on 2 s (`HZ * 2`) without progress. The `timeout` and `delay` ranges keep 100 ms margins around both. Whether `hang` triggers the LIF reset is still to be observed (NR-E09).
- **Decision: reimplement netem compatibly, don't copy or link it.**
  - netem's loss models are kernel code (`net/sched/sch_netem.c`, GPL-2.0-only), and iproute2's `tc/q_netem.c` is only a GPL-2.0-only argument parser. Neither is a library.
  - rocm-ernic `src/` is GPL-2.0-or-later, so copying either would change the licence.
  - We use the same generator as netem (LFSR113, the kernel's `prandom_u32_state`) and its seeding, so an explicit `seed` reproduces netem's drop sequence. NR-E10 proves it against the real kernel.
  - iproute2's `netem/` distribution tables are NIST-PD. We read the installed `.dist` files at runtime; vendoring is the fallback.
  - This is put to the maintainers as DESIGN §14 q7.
- **Guidance: use real netem on the TAP for wire loss.** DESIGN §10.8 says to use `tc netem` on the host TAP for wire behaviour, and emulator faults for NIC-side, RDMA and stats behaviour.

### 2026-10-12: Phase 0, baseline and setup

- **Baselines.** Built with `~/Downloads/rocm-ernic-tools/run-analysis.sh` (the flake targets `analysis-deep`, `analysis-sanitizers`, `analysis-tsan`, `analysis-valgrind` and `fuzz-run`, from `git+file://…?rev=`) at `upstream/develop` `e912e84` and #222 `66c5141`. The results are in the Gate record. `newfindings.py` now lives in `~/Downloads/rocm-ernic-tools/`, outside any repo.
- **Golden `compat` identity.** Captured from `develop` `e912e84`: `dev_info_regs`, `dev_identity` and its completion, `lif_identity` and its completion, `q_identity` for qtypes 0–3, the published `port_info` and `lif_status`. That is 20739 bytes, sha256 `e7f7b5d5…10159515`. The #222 capture (`b5739986…2a7aed06`) differs only in the lif identity `rdma.stats_type` byte, which #222 sets on purpose. PR F turns the develop capture into `tests/golden/ionic_compat_identity.h`.
- **"Before" guest evidence.** `nix run .#run-ionic-guest-test` on `xtcp2-integration` plus the counter dump: PASS.
  - `ip -s link`: rx and tx are all 0, while the driver's software `ethtool -S` shows `tx_packets 6`, `tx_bytes 516`. This confirms the `lif_info.stats` finding.
  - Every `hw_*` counter in `ethtool -S` is 0.
  - `ethtool -l`: maximums RX 2, TX 2, combined 4; current combined 4.
  - `hw_counters` on `ionic_0` port 1: `commands` (9), the four `total_bytes_*`, and `lifespan`. That is the 5 #222 counters.
- **Finding: shutdown abort.** After `rocm-ernic: Shutting down`, the server aborts with `Fatal glibc error: pthread_mutex_lock.c:426 (__pthread_mutex_lock_full): assertion failed: e != ESRCH || !robust`. This happens even with the teardown and tcp-fini fixes merged. It is outside NIC-realism scope and is recorded here for follow-up.
- **Finding: gcc 15 and `ERNIC_WERROR`.** `cmake/ErnicGNUCompilerOptions.cmake` (#179) adds `-Wleading-whitespace=tabs` for gcc ≥ 15. That flag warns on space indentation, which the whole tree uses. CI's gcc is older, so it never fires there, but the dev shell's gcc 15.3 with `-DERNIC_WERROR=ON` fails on untouched files (`src/qemu-compat/error-report.c` first). The flag probably wants `=spaces`. Until that is fixed upstream, the gcc gate here is a build without `-Werror` that has zero warnings other than `-Wleading-whitespace`. clang runs with `-DERNIC_WERROR=ON`.
- **Decision: guest counter dump on a fork-only branch.** The guest harness printed no counters, so `test/ionic-guest-counter-dump` (from `feat/ionic-vm-pair`) adds `ernic-ionic-counter-dump`. It prints `ip -s link`, `ethtool -l`, `ethtool -S` and every `hw_counters` file between the `ERNIC_DUMP_BEGIN` and `ERNIC_DUMP_END` markers. Guest checks run on a throwaway merge with `xtcp2-integration`.
- **Deviation: `small-vm` in PR F carries only identity and link keys.** Its queue, stats and RDMA keys arrive with C, A and D (F05).

### 2026-10-12: E01 decisions

- **Status codes.** `handle_stats_op` returned 1 (`EVERSION`) for every error. E01 maps each cause to its own code. §10.7 named only the first two below; the rest go beyond the design:

  | Cause | Status |
  |---|---|
  | unknown QP | `ENOENT` |
  | counter snapshot or DMA failure | `EIO` |
  | short body | `EINVAL` |
  | `dma_addr` 0 | `BAD_ADDR` |
  | `length` too small for one entry | `ENOSPC` |
  | unknown stats opcode | `EOPCODE` |

  The kernel still turns them all into `-EINVAL`. The codes now live in `src/ionic_status.h`, a copy of `enum ionic_status_code` from `ionic_if.h`, and `ionic_eth_emu.c` uses it instead of its own two macros.
- **Deviation: the `fault_*` stats-file lines move to E09.** E01's guest check reads the server log (`ionic_adminq STATS_VALS: injected fail status 3 (event 1)`) together with the guest `EINVAL` and the dmesg line `opcode 5 error 3`, instead of `fault_stats_vals_fired`.
- **Parser scope.** Names the design defines but this build doesn't yet implement are rejected with "… not supported in this build" rather than "unknown":
  - the other sites
  - `random`, `state` and `gemodel`
  - the gates
  - the other stats effects
  - `sequence @file`

  Effects that belong to other sites are rejected as "not valid for site". A saturated event counter (`UINT64_MAX`) stops the fault firing rather than wrapping.

### 2026-10-12: E01 gates and guest check

- **Finding: the first `STATS_VALS` is the kernel's registration read.** `ib_register_device()` calls `get_hw_stats()` (`drivers/infiniband/core/sysfs.c`, device and port hw-stats setup), so that read is eligible event 1. With `--inject 'stats_vals: after 1 effect fail'` the server logged `injected fail status 3 (event 1)`, then `RDMA_RESET_LIF`, and the guest printed `Failed to register ibdev`: no RDMA device, `ERNIC_GUEST_FAIL`. With `after 2`, registration succeeded, the `commands` read failed with `EINVAL`, and the next reads succeeded.
  - **Design change.** DESIGN §10.7 now says this, the `fail` row says what event 1 does, the §13 guest check uses `after 2`, and the `unreadable-hw-counters` overlay is `after 2 count 18446744073709551615` instead of `every 1`, so it keeps the device and fails every later read.
  - **Open (E03/E11).** The random overlays (`flaky-hw-counters`'s `gemodel`, and `slow-hw-counters`) can still land on event 1 and remove the device. They need the registration read exempted. One option is a count gate (for example `skip N`) in E03; a time gate (`start`) can't be sure to cover registration. Not decided.
- **Finding and fix: admin CQE status byte order.** `post_admin_cqe()` wrote `status << 24` into the be32 `status_length` (since `edd3cee`), but the driver logs `be32_to_cpu(status_length)` whole, as the datapath CQEs already encode it. So dmesg printed `opcode 5 error 50331648` for `EIO`. E01 writes the status as the whole be32 value. Five new `test_ionic_stats` rows pin the encoding, and the guest now logs `opcode 5 error 3`. The guest-visible errno is `-EINVAL` either way.
- **Finding: the shutdown abort can become a hang.** On the integration merge with no `--inject`, the server still dies with the glibc robust-mutex assertion (Phase 0). With any `--inject`, even `after 1000`, which never fires, it hangs after `Shutting down` and `ernic-run` sends `SIGKILL` 10 s after `SIGTERM`. The fault set is only allocated, and freed after `pvrdma_device_destroy()`. Hypothesis, not yet tested: the existing teardown locks a mutex in freed or uninitialised memory, and the heap layout decides whether glibc sees a dead owner (abort) or a live one (wait forever). This is outside E01's scope and tracked as a follow-up.
- **Correction to Phase 0: clang and `-Werror`.** clang with `-DERNIC_WERROR=ON` also fails in the dev shell on untouched files: the glibc fortify wrappers give `-Wused-but-marked-unused` on `printf`/`snprintf`, plus conversions in `net_headers.h`. The clang gate is a build without `-Werror`, and its warnings are diffed against the base by kind. The only kind E01 adds is that fortify warning, in the new files that print.
- **Analysis triage.** `newfindings.py` lists 231 keys against #222. Most are clang-tidy style kinds the base has throughout, or the fortify warning above. Neither of the 2 new high-confidence triage entries is in new code: they are the existing `fprintf(stderr)` CLI-error convention in `rocm_ernic_server.c`, which `--inject`'s error and usage lines follow. The cppcheck `constParameterPointer` on `ionic_fault_set_get()` is fixed. flawfinder `vsnprintf` [4] ×2 are `set_err()` and `out_add()`, both `format(printf)`-attributed with constant formats, like `nvmeof_target.c` and `s3_http.c` in the base.

### 2026-10-12: F decisions

- **Deferred: `[fault.*]` sections and CLI precedence.** F's loader knows only `[identity]` and `[link]`. `[fault.*]` arrives with E11, when the fault grammar is complete, and the "CLI flag beats every profile layer" rule with A's `--stats-interval-ms`, the first flag that has a profile key. Both plan rows move with them.
- **compat `link.mtu_max` is 9198.** That is today's `eth.max_mtu` in the LIF identity, which the guest shows as `maxmtu 9198`. The port `config.mtu` stays 9000, written as `min(mtu_max, 9000)`, and `max_frame` is `mtu_max + 18`. The range is 1500..9582.
- **The `nic_profile` stats-file line** lives in the Instance block, through a process-global in `pvrdma_stats.c`, so the vendored `PVRDMADev` struct is untouched.
- **GKeyFile decodes escapes.** `get_string` turns `\s` into a space and `\\` into `\`. The `fw_version` rule (printable ASCII, no backslash) applies after decoding, so `a\sb` is accepted as `a b` and `a\\b` is rejected. Unit and fuzz-seed rows pin both.
- **The F3 proof found a weak row.** The first "layer that fails after a good one" row failed on an unknown key, which the scanner rejects before any value is set, so making the apply non-atomic didn't fail it. The row now fails on a value (`mtu_max = 1`) after two valid keys, and the proof fails as it should.

### 2026-10-12: F gates and guest check

- **Gates.** See the Gate record. The gcc gate is the build without `-Werror` (Phase 0 finding).
- **Analysis triage** (`newfindings.py`, develop `e912e84` vs F: 8379 → 8768 findings, 398 new keys). Fixed: cppcheck `constVariablePointer` (`tests/ionic_eth_emu_fakes.h`), `variableScope` ×2 (tests), and `scan_add()` now copies the group with `strlen() + 1`, as it does the key, instead of relying on the caller's buffer size. False positives or the base's style, not changed:
  - semgrep `memcpy-sizeof-pointer` ×3 in `ionic_profile.c`: each `sizeof` is of an array or string literal, not a pointer
  - semgrep `strerror-thread-unsafe` ×2: the loader runs single-threaded at startup, and the tree uses `strerror` throughout
  - cppcheck `constParameterPointer` on `vfu_sgl_get()`'s `sgl`: that is the libvfio-user signature
  - clang-tidy style kinds the base has throughout (braces, identifier length, magic numbers, `cert-err33-c` on `snprintf`/`fprintf`), and flawfinder `strlen`/`memcpy`/`char` on bounded copies
- **Finding (pre-existing): `analysis-sanitizers` leak on SIGTERM shutdown.** When the loopback exercise reaches the SIGTERM path, LSan reports 6 indirect blocks (13 MB) from `ionic_device_init()`: the eth emulator, datapath and rdma devcmd. The shutdown block frees `dev` without destroying them. That block is byte-identical on develop. Which path a run takes is a race: develop's sanitizer run (and a `--rebuild`) exited early with `vfu_attach_ctx() failed`, while develop's TSAN run, #222's sanitizer run and F's took the shutdown path. #222's baseline has the same leak, so its Gate record row is corrected. A small upstream fix, outside NIC realism.
- **Finding (pre-existing): clang-analyzer `unix.Malloc` in `validate_backend_options()`.** `ethdev` leaks when `parse_verbs_options()` fails on a later `device=` `strdup`. That code is unchanged from develop (line 927 there, 939 in F). The develop baseline didn't report it, which looks like the analyzer's path budget. Also a small upstream fix.
- **TSAN flake.** One TSAN ctest run failed `nvmeof-ci` and `s3-ci`; the rerun and the later full runs passed. F touches neither. Not yet compared against a develop TSAN ctest run.
- **Guest check.** On a throwaway merge of F into `xtcp2-integration` `3ae9d46`, with `nix/ionic-guest-test.nix` extended to print `ip -d link`, `ethtool -i`, `ethtool` link and the RDMA `rate`, and to pass `ERNIC_NIC_PROFILE` through as `--nic-profile`. Values are in NR-F02 and NR-F03.
- **Merge note.** `nix/analysis/fuzz.nix` carries the same `FUZZ_EMIT_SEEDS` seeds step as #222. The guest merge with #222 conflicted in `fuzz.nix`, its README, `tests/CMakeLists.txt` and `tests/nix/CMakeLists.txt`. All were list unions, and the upstream merge will need the same.

### 2026-10-12: shutdown root cause and two small upstream fixes

- **Root cause of the shutdown abort and hang (Phase 0, E01).** This corrects the E01 hypothesis. `tcp_backend_set_ionic_recv_cb()` and `tcp_backend_send_ionic_v()` cast `backend_private` to `TcpBackendPrivate` without checking that the backend is TCP. On loopback, #224's `ionic_device_fini()` → `ionic_datapath_destroy()` unhooks the mesh receive callback once a client has attached. That locks a mutex past the end of the smaller loopback state and writes two pointers after it. Whether glibc then aborts on the robust mutex or hangs depends on the heap, which is why any `--inject` (one extra allocation) turned the abort into a hang. On `develop` it's latent, because destroy only runs before any handle is set.
  - Fix: [#226](https://github.com/ROCm/rocm-ernic/pull/226) `fix/tcp-ionic-hooks-backend-type` `93356ef`, from `develop` `e912e84`. It's independent of #224 but needed by it.
  - Regression proof: with the two checks reverted, `ctest -R tcp-ionic-hooks-unit` under ASan → `heap-buffer-overflow … in tcp_backend_set_ionic_recv_cb rdma_backend_tcp.c:4724`.
  - Evidence on `xtcp2-integration` `3ae9d46` + the fix: `nix run .#run-ionic-guest-test` 3 of 3 runs passed, each with "Shutdown complete" and no SIGKILL (before the fix, 1 of 3 aborted). On the E01 merge with `ERNIC_INJECT='stats_vals: after 1000 effect fail'`, the run without the fix logged "still running 10s after SIGTERM; sending SIGKILL"; the run with it logged "Shutdown complete".
  - Gates: ctest 31/31 in gcc, clang, ASan and TSAN; clang-format 18, codespell and cmakelint clean.
- **The `validate_backend_options()` ethdev leak** (the F gates finding).
  - Fix: [#227](https://github.com/ROCm/rocm-ernic/pull/227) `fix/verbs-options-ethdev-leak` `9ac4e53`. Every error path in `parse_verbs_options()` now goes to one `fail:` that frees and NULLs both outputs.
  - `analysis-deep` on develop + fix: no `Potential leak` in `rocm_ernic_server.c`. On F + fix, the `rocm_ernic_server.c:939 … 'ethdev' [unix.Malloc]` finding is gone. The only other delta is clang-tidy cognitive complexity of `parse_verbs_options`, 41 → 42.
  - Gates: ctest 30/30 in all four modes.
- **Still open.** The `analysis-sanitizers` SIGTERM-path leak (F gates) is #224's `ionic_device_fini()`; no separate PR.

### 2026-10-12: A commits, gates and guest check

- **Commits.** Five on F (`60590ee`): `139b6eb` A01, `fe49510` A02–A04, `36ca096` A05, `ad25bfc` A06, `9057008` A07. Each builds and passes alone (see the Gate record). Not pushed.
- **Design changes, as built.**
  - The first publish is straight after `LIF_INIT`, not one interval later, so the emulator needs no clock of its own; the main loop passes a monotonic ms time.
  - Frames are counted in sink mode (no TAP), so the loopback guest shows real tx counters.
  - The mesh inbox requeued a frame on every delivery error, so one frame too big for the posted buffer blocked every frame behind it. Now only `-ENOBUFS` (no buffer posted) requeues; other errors drop and count.
  - `rx_queue_disabled` is not counted: `poll_rx` skips queues that are not initialized, so there is no event to count. Tx descriptor and DMA failures have no MAC class (the frame was never read), so only the ring counter moves.
  - RDMA `*_packets` count messages, not MTU segments.
  - The stats-file section is a registered callback (`pvrdma_set_stats_section`), because `src/rdma` must not depend on the eth emulator.
  - `[stats]` in `compat` changes its content hash; the identity bytes are unchanged (the golden row still passes).
- **Analysis triage** (`newfindings.py` F `60590ee` vs A, before the fixes below: 8768 → 9157, 390 new keys, 150 once the clang `-Wused-but-marked-unused` glibc-fortify kind is dropped).
  - Fixed: clang-analyzer `core.NonNullParamChecker` in the publish test (`new_emu` now exits if the create fails); cppcheck `shadowVariable` ×3 (the test's global `frame` renamed); `constVariablePointer`; clang-tidy `bugprone-implicit-widening-of-multiplication-result` (`IONIC_LIF_STATS_BYTES` is now a literal with a `_Static_assert`). gcc `-Wswitch-default`, `-Wformat-truncation` and `-Wzero-as-null-pointer-constant` in the new tests were fixed before the commits.
  - False positives: cppcheck `knownConditionTrueFalse` on `ionic_eth_net_send() < 0` (it inlined a test fake; the real function returns `-EBADF`/`-EIO`); semgrep `memcpy-sizeof-pointer` on `memcpy(…, &v, sizeof(v))` with `v` a `uint64_t`; cppcheck `unusedStructMember` on the kernel layout mirror (`config`, `status` exist for `offsetof`).
  - The base's style, not changed: semgrep `fprintf-stderr` on the CLI error path; flawfinder `char`/`memcpy` on bounded buffers; clang-tidy `readability-identifier-length`, `braces-around-statements`, `misc-include-cleaner`, `cert-err33-c`, `magic-numbers`, `easily-swappable-parameters`.
- **Tools.** `newfindings.py` failed on this semgrep report, which has text after its JSON document; it now reads one document with `raw_decode`.
- **clang-format.** `nixpkgs#clang-tools_18` is now a throw; the v18 gate uses `nixpkgs#llvmPackages_18.clang-tools` (18.1.8). The dev shell's clang-format is 21 and bin-packs differently, so the emu-stats rows were restructured (inputs in a nested struct) to stay readable under 18.
- **Guest check.** Values in NR-A04. The ionic netdev carries no IP in the pair test (that uses a separate virtio link), so rx is not exercised on loopback; the rx paths are covered by the unit rows (TAP and mesh). An rx check needs a TAP or the pair's ionic link.
- **Merge note.** The guest merge conflicted in the same four files as F's (list unions), plus `tests/CMakeLists.txt` for A's tests.
