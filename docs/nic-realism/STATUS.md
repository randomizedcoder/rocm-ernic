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
| A: LIF stats | `feat/ionic-lif-stats` | planned | – | – |
| B: port stats | `feat/ionic-port-stats` | planned | – | – |
| C: queues/vectors | `feat/ionic-eth-queues` | planned | – | – |
| D: RDMA counters | `feat/ionic-rdma-error-stats` | planned | – | – |
| E: fault injection | `feat/ionic-fault-inject` | in-progress (E01) | – | – |
| F: NIC profiles | `feat/ionic-nic-profiles` | planned | – | – |

## Task trace

| ID | Task | State | Commit(s) | Evidence | Regression proof | Deviation from plan |
|---|---|---|---|---|---|---|
| NR-A01 | counter block + lif layout helper | planned | | | | |
| NR-A02 | keep `lif_info_pa`; clear on reset | planned | | | | |
| NR-A03 | tx/rx MAC-class and drop counting | planned | | | | |
| NR-A04 | publish tick + `--stats-interval-ms` | planned | | | | |
| NR-A05 | RDMA bytes → lif `*_rdma_ucast_*` | planned | | | | |
| NR-A06 | lif counters in `--stats-file` | planned | | | | |
| NR-A07 | `docs/ionic.rst` Statistics section | planned | | | | |
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
| NR-F01 | profile loader + strict validation + layering | planned | | | | |
| NR-F02 | `--nic-profile` / `--print-profile` / logging | planned | | | | |
| NR-F03 | identity and link keys | planned | | | | |
| NR-F04 | built-in `compat`, `small-vm` + `profiles/` | planned | | | | |
| NR-F05 | A–E keys and built-ins, per PR | planned | | | | |
| NR-F06 | `docs/ionic.rst` NIC profiles section | planned | | | | |

## Gate record

One row per push of a feature branch.

| Date | Branch @ commit | clang-format | gcc / clang warnings | ctest default / ASan / TSAN | codespell | analysis (`newfindings.py`) | guest test |
|---|---|---|---|---|---|---|---|
| 2026-10-12 | baseline `upstream/develop` @ `e912e84` | – | – | – | – | `analysis-deep`: clang-tidy 4390, cppcheck 214, flawfinder 374, clang-analyzer 3, gcc-warnings 16, gcc-analyzer 11, semgrep 165, triage 404; `analysis-sanitizers` and `analysis-tsan` clean; `analysis-valgrind` ERROR SUMMARY 3 (1 definitely-lost block); `fuzz-run` 0 crashes | – |
| 2026-10-12 | baseline #222 `feat/ionic-hw-stats` @ `66c5141` | – | – | – | – | `analysis-deep`: clang-tidy 4415, cppcheck 215, flawfinder 383, rest as develop; sanitizers and tsan clean; valgrind ERROR SUMMARY 3 (1 definitely-lost block, as develop); `fuzz-run` 0 crashes (rdma_cm_proto, net_headers, ionic_stats) | `run-ionic-guest-test` on `xtcp2-integration` + counter dump: PASS |
| 2026-10-12 | `feat/ionic-fault-inject` @ `29796c4` (gates run on the identical tree before it was committed) | v18: clean | gcc 15: 0 warnings besides `-Wleading-whitespace`; clang: only `-Wused-but-marked-unused` (glibc fortify), the kind the base has | 34/34 in each of gcc, clang, ASan, TSAN; `test_ionic_fault` 71 ok, `test_ionic_stats` 37 ok | clean | `analysis-deep` vs #222: no new high-confidence triage entries in new code (the two new ones are the existing `fprintf(stderr)` CLI-error style in `rocm_ernic_server.c`); cppcheck `constParameterPointer` fixed; flawfinder `vsnprintf` [4] ×2 in format-attributed helpers, as `nvmeof_target.c` and `s3_http.c`; the rest clang-tidy style kinds the base has throughout. `analysis-sanitizers`, `analysis-tsan` clean; `analysis-valgrind` ERROR SUMMARY 3, as base; `fuzz-run` and a 60 s `run-fuzzers`: 0 crashes (`fault_parse` 3.3M execs, cov 274→336, corpus 22→194, no leaks on replay) | `after 2` PASS with dmesg `opcode 5 error 3`; `after 1` FAIL by design (see Log) |

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
