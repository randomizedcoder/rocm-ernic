# Implementation plan: NIC realism (A–F)

This plan implements [DESIGN.md](DESIGN.md). Progress against it is tracked in [STATUS.md](STATUS.md). Task IDs (`NR-A01` …) are stable: STATUS.md, commit messages and PR bodies refer to them.

## Ground rules

- **Branches.** These docs stay on the fork-only `docs/nic-realism` branch. Feature work goes on its own branches, each in its own worktree, and no feature branch carries `docs/nic-realism/`.

  | PR | Branch | Base | Worktree |
  |---|---|---|---|
  | A | `feat/ionic-lif-stats` | `upstream/develop` | `~/Downloads/rocm-ernic-lif-stats` |
  | B | `feat/ionic-port-stats` | A | `~/Downloads/rocm-ernic-port-stats` |
  | C | `feat/ionic-eth-queues` | `upstream/develop` | `~/Downloads/rocm-ernic-eth-queues` |
  | D | `feat/ionic-rdma-error-stats` | #222 `feat/ionic-hw-stats` | `~/Downloads/rocm-ernic-rdma-stats` |
  | E | `feat/ionic-fault-inject` | A + D (or #222 `feat/ionic-hw-stats` for E01 alone) | `~/Downloads/rocm-ernic-fault-inject` |
  | F | `feat/ionic-nic-profiles` | `upstream/develop` | `~/Downloads/rocm-ernic-nic-profiles` |

- **One feature per PR.** Stacked PRs say so in the body and are rebased when their base merges.
- **Tests.** Every behaviour change has a table-driven C test. Each row has `category` (positive/negative/boundary/corner), `description` and `expected`, and is printed as `ok` or `FAIL <category>: <description>; expected: <expected>`. Every fix names at least one row that fails when the fix is reverted, and that regression proof is recorded in STATUS.md.
- **Gates** (repo CLAUDE.md), all run before each push:
  1. `clang-format-18 --dry-run --Werror` on the changed files
  2. gcc and clang builds with zero warnings
  3. `ctest` in a default build, a `-DERNIC_USE_SANITIZERS=ON` build and a `-DERNIC_USE_THREAD_SANITIZER=ON` build
  4. `codespell`
  5. new files carry the AMD header with no year (`src/`, `tests/`)
  6. the five analysis flake targets, with `newfindings.py` against `develop`: no new high-confidence findings, and every new raw finding explained
  7. live: `nix run .#run-ionic-guest-test`, plus `.#run-ionic-pair-test` for A, D and E
- **Commits** are signed off (`git commit -s`) and made only when asked. PR bodies are friendly, mention go-link-monitor as the motivating consumer, and link this design on the fork.
- **Integration.** After each PR is opened (or merged), the fork's `xtcp2-integration` branch is rebuilt (`develop` plus the open PRs) and xtcp2's pin moves to it.

## Order and dependencies

```
F (profile loader, identity/link keys) ──► every later PR adds its keys to the schema
A (lif stats + publish tick) ──► B (port stats)
        │
        └────────────► D (rdma counters; also fills lif rdma_* qwords) ──► E (fault injection)
C (queues/vectors) — independent
E01 (fault core + stats_vals fail + status codes) — may go first, on top of #222 only
```

Suggested sequence: **E01 → F → A → C → D → B → E**.

- E01 is small and unblocks the xtcp2 "unreadable hw_counters" test.
  - It already uses the final netem-style `--inject` grammar, with only the deterministic patterns, so the syntax never changes.
  - F later maps `--inject` onto `[fault.*]` sections.
- The rest of E (E02–E11) follows D, because its sites are the A, B and D hooks.
- F comes before the rest so that each later PR adds its knobs as profile keys, not as one-off constants or flags.
- A fixes the most visible gap (`ip -s link` = 0). C and D are independent of each other.
- If F is delayed, A–E add CLI flags and F maps them onto keys later. The flags stay as shortcuts either way (DESIGN §11.1).

## A: LIF statistics (`feat/ionic-lif-stats`)

| ID | Task | Files |
|---|---|---|
| NR-A01 | `struct ionic_emu_stats` with lif/port/rdma/fault sections; `lif_stats_index()` layout helper; `ionic_emu_stats_reset()` | new `src/ionic_emu_stats.{c,h}`, `CMakeLists.txt` |
| NR-A02 | Store `lif_info_pa` in `handle_lif_init`; clear it on `LIF_RESET` and device reset | `src/ionic_eth_emu.c` |
| NR-A03 | Count tx/rx by MAC class in `eth_txq_service` / `eth_rx_deliver`; count the drop sites (DESIGN §6 table), with an `_Atomic` for the inbox-full drop | `src/ionic_eth_emu.c` |
| NR-A04 | `ionic_eth_emu_publish_stats()` with the `--stats-interval-ms` option; called from the main loop next to `pvrdma_write_stats` | `src/ionic_eth_emu.c`, `src/rocm_ernic_server.c` |
| NR-A05 | RDMA byte hooks feed `tx/rx_rdma_ucast_*` | `src/rocm_ernic_compat.c` |
| NR-A06 | Add the lif counters to `--stats-file` | `src/rocm_ernic_compat.c` |
| NR-A07 | Docs: `docs/ionic.rst` "Statistics" subsection (upstream doc, in the PR) | `docs/ionic.rst` |

Tests:

- New `tests/test_ionic_emu_stats.c`:
  - positive: a unicast/multicast/broadcast frame lands in the right qwords; `rx_dropped` equals the sum of the `*_drop_packets` qwords
  - negative: a frame with a zero-length gather counts nothing
  - boundary: a 60-byte frame; a 9216-byte frame; a counter at `UINT64_MAX - 1` plus 1
  - corner: every reserved qword stays zero; reset clears everything
  - layout pins: `lif_stats` at 320 and each named qword index against `ionic_if.h`
- New `tests/test_ionic_stats_publish.c`, with fake guest memory shared with `test_ionic_stats.c`:
  - positive: one write per interval to `pa + 320`
  - negative: no write before `LIF_INIT`; no write after `LIF_RESET`
  - boundary: interval 10 ms; interval equal to the elapsed time
  - corner: `LIF_INIT` again with a new `info_pa` moves the target
- Regression proof: drop the `LIF_RESET` clear and the "no write after reset" row fails; skip the publish and the positive row fails.
- Guest check: after `ping -c 100`, `ip -s link show <ionic>` rx/tx packets ≥ 100 and agree with `--stats-file` within one interval.

## B: port (MAC) statistics (`feat/ionic-port-stats`)

| ID | Task | Files |
|---|---|---|
| NR-B01 | Store `port_info_pa` in `handle_port_init`; clear it on `PORT_RESET` and device reset | `src/ionic_eth_emu.c` |
| NR-B02 | `port_stats_index()`; classify at the wire boundary: ok/all, uni/multi/broadcast, 12+12 size buckets, VLAN/PCP priorities, too long / undersized | `src/ionic_emu_stats.{c,h}`, `src/ionic_eth_emu.c` |
| NR-B03 | Publish to `port_info_pa + 576` from the A tick | `src/ionic_eth_emu.c` |

Tests (extend `test_ionic_emu_stats.c`):

- positive: bucket boundaries 64/65/127/128/…/1518/1519
- negative: an rx frame over the MTU counts `frames_rx_too_long` and is not delivered
- boundary: 59/60-byte frames (undersized edge); PCP 7
- corner: an untagged frame counts as priority 0; the buckets sum to `frames_*_all`
- layout pin: `port_stats` at 576, with all 126 indices against `ionic_port_stats_desc` order
- Regression proof: an off-by-one bucket edge fails the boundary rows.
- Guest check: `ethtool -S` shows every `ionic_port_stats_desc` name, and `frames_rx_ok` grows with traffic.

## C: queues and interrupt vectors (`feat/ionic-eth-queues`)

| ID | Task | Files |
|---|---|---|
| NR-C01 | `queues.eth` / `queues.rdma_eqs` / `queues.intr_vectors` profile keys (`--eth-queues` shortcut); `compat` keeps 4, and `generic-100g` (16) and `generic-400g` (64) are added; identity `PUT_QCOUNT` uses them | `src/ionic_eth_emu.c`, `src/rocm_ernic_server.c` |
| NR-C02 | `IONIC_MSIX_MAX_VECTORS` 32 → 128; check the MSI-X capability, table/PBA region sizes, `vfu_setup_device_nr_irqs` and the per-vector arrays | `src/rocm_ernic_internal.h`, `src/rocm_ernic_server.c`, `src/ionic_eth_emu.c` |
| NR-C03 | Serve every RX queue; deterministic flow hash for steering | `src/ionic_eth_emu.c` |

Tests: new `tests/test_ionic_queues.c`.

- positive: identity advertises `n`; `1 + min(n, cpus) + min(32, cpus)` fits in 128 for n = 64 and cpus = 64
- negative: `--eth-queues 0` and `65` are rejected with a usage message
- boundary: n = 1; n = 64
- corner: the same 5-tuple always maps to the same queue; non-IP falls back to the L2 hash; every queue is reachable
- Regression proof: restoring `IONIC_MSIX_MAX_VECTORS 32` fails the vector-fit row; serving only RXQ 0 fails "every queue reachable".
- Guest check: with an 8-vCPU guest and `--eth-queues 16`, `ethtool -l` shows 8 combined, `ethtool -S` has `rx_7_pkts`, and `iperf3 -P 8` traffic lands on more than one rx queue.

## D: RDMA hardware and QP counters (`feat/ionic-rdma-error-stats`, on #222)

| ID | Task | Files |
|---|---|---|
| NR-D01 | `STATS_MAX_COUNT` 8 → 64; the name table under the DESIGN §9 rule; a compile-time check that each name is ≤ 27 characters | `src/ionic_adminq.c` |
| NR-D02 | Error hooks at `cq_post` (role + status); RNR pass hook; flush hook | `src/ionic_datapath.c`, `src/rocm_ernic_compat.{c,h}` |
| NR-D03 | Per-QP error counters in `QP_STATS_*` | `src/ionic_adminq.c`, `src/rocm_ernic_compat.c` |
| NR-D04 | The same counters fill the lif `rdma_*` qwords (once A has merged) | `src/ionic_emu_stats.c` |

Tests:

- Extend `tests/test_ionic_stats.c`:
  - positive: the header table lists every D name in order; values follow the counters
  - negative: an unknown QP still writes nothing
  - boundary: `length` exactly fits 64 entries; one entry short
  - corner: names unique; all ≤ 27; NONE terminator
- New datapath rows (in the existing datapath test, if the harness allows; otherwise a new `test_ionic_dp_errors.c`):
  - positive: a bad lkey moves `req_tx_memory_mgmt_err` by 1; a bad rkey moves `req_rx_remote_access_err` by 1
  - negative: a successful SEND moves no error counter
  - boundary: RNR at exactly `RNR_RETRY_MS`
  - corner: a fault on a flushed QP counts the flush, not a second error
- Regression proof: drop the `cq_post` hook and the positive rows fail.
- Guest check (pair): `ernic-pair-pingpong` moves the byte counters. A deliberate bad-rkey `ibv_rc_pingpong` variant (or `rping` with a wrong key) moves `req_rx_remote_access_err` by exactly 1.

## E: fault injection (`feat/ionic-fault-inject`)

E is larger than the other PRs, so it is split into reviewable PRs on the same branch line: **E01** alone, **E02–E06** (the model) and **E07–E11** (the sites). Each PR's tests stand alone.

| ID | Task | Files |
|---|---|---|
| NR-E01 | Fault core: site/fault model and the deterministic patterns (`every`, `after … count`, `burst`, `sequence "…"`). The netem-style `--inject` grammar with that subset. The `stats_vals` site with effect `fail [status S]`. The correct status codes in `handle_stats_op` (`IONIC_RC_ENOENT` for an unknown QP, `IONIC_RC_EIO` for a counter or DMA failure). It can ship alone on #222. | new `src/ionic_fault.{c,h}`, `src/ionic_adminq.c`, `src/rocm_ernic_server.c` |
| NR-E02 | LFSR113 generator with netem-compatible seeding; `random P [C]` (netem correlation); `state` (4-state); `gemodel` with tc's defaults and `1-h`/`1-k` handling; derived seeds `fnv1a64(faults.seed ‖ section)` | `src/ionic_fault.c` |
| NR-E03 | Gates `window`, `start` (from `LIF_INIT`), `stop`, `limit`, through an injectable monotonic clock; several faults per site with first-fire order and `masked` counting | `src/ionic_fault.c` |
| NR-E04 | Stats effects `stale`, `zero`, `reset`, `wrap [64]`; the `qp_stats_vals` site; `stats_publish` site effects `skip`, `zero`, `reset` (after A/B) | `src/ionic_adminq.c`, `src/ionic_eth_emu.c` |
| NR-E05 | Deferred admin completion for `delay`, `timeout` and `hang`; discard a late completion after the guest cancels; the `timeout` < 2 s rule; `faults.allow_destructive` | `src/ionic_adminq.c`, `src/rocm_ernic_server.c` (main-loop timer) |
| NR-E06 | `.dist` reader (tc lib dir, `$TC_LIB_DIR`, or a path) and `delay D [J [distribution X]]` jitter | `src/ionic_fault_dist.{c,h}` |
| NR-E07 | Ethernet sites `eth_rx` and `eth_tx`, effects `drop`, `nobuf`, `corrupt`, `duplicate`, `pause`, at the A/B hooks | `src/ionic_eth_emu.c` |
| NR-E08 | RDMA sites `rdma_rx` and `rdma_tx`, effects `rnr`, `seq_err`, `ecn`, `cnp`, `retransmit`, at the D hooks | `src/ionic_datapath.c` |
| NR-E09 | Observability: startup log in canonical form; `fault_<section>_{eligible,fired,masked}` in `--stats-file`; `faults.trace`; replay with `sequence @file`. Guest confirmation of the `hang` watchdog reset (DESIGN §10.4.3) | `src/ionic_fault.c`, `src/rocm_ernic_compat.c` |
| NR-E10 | netem conformance VM test (DESIGN §12) | new `nix/tests/fault-netem-conformance.nix` |
| NR-E11 | `[fault.*]` profile sections and the fault overlays (`lossy-ethernet`, `rdma-errors`, the `*-hw-counters` set), as F05 work; `docs/ionic.rst` "Fault injection" section, including when to use real netem on the TAP device instead | `src/ionic_profiles.c`, `profiles/`, `docs/ionic.rst` |

Tests: new `tests/test_ionic_fault.c`, `tests/test_ionic_fault_dist.c` and `tests/test_ionic_adminq_stats.c`, plus rows in `tests/test_ionic_stats.c`.

- positive:
  - `every 100` over 1,000 events fires exactly 10 times
  - `after 3 count 2` fires on events 3 and 4
  - `burst 5 every 100` fires 50 times in 1,000 events, in runs of 5
  - golden fire sequences for `random 1%`, `random 1% 25%`, `state 1% 30%` and `gemodel 1% 30%` at seed 42
  - LFSR113 matches published test vectors
  - a written trace replays to identical fires
- negative:
  - an unknown site or effect, or an effect invalid for its site (`eth_rx` with `rnr`)
  - `every 0`; a percentage over 100 %; trailing tokens; a missing `effect`
  - `timeout 1000ms` and `timeout 2s`; `delay 1s` on `stats_vals`; `hang` without `faults.allow_destructive`
  - a malformed `.dist` line, or a table over 16384 entries
- boundary:
  - `every 1` always fires; `random 0%` never fires and `random 100%` always fires
  - `timeout 1100ms` and `timeout 1900ms` are accepted; `delay 900ms` is accepted
  - a window of exactly one tick
  - `limit 1`
- corner:
  - no faults: no site ever fires, and the hooks cost one branch
  - correlation 0 equals plain `random`
  - retuning fault A leaves fault B's sequence unchanged
  - two faults on one site: the first fires and the second counts `masked`
  - `stale` before any successful read returns zeros
  - `wrap` crosses 2³² on the next read
  - a late completion after the guest's cancel is discarded and does not corrupt the next command
- statistical (10⁶ events, fixed seeds, bounds at 4σ so they never flake):
  - `random P` rate
  - `gemodel` mean burst length ≈ 1/r and stationary rate p/(p+r)
  - `state` stationary loss
  - jitter mean and σ against the table
- Regression proofs:
  - an off-by-one in `every` fails the positive count row
  - swapping `1-h` and `h` fails the gemodel golden row
  - reverting the status codes fails the `test_ionic_stats.c` status rows
  - dropping the cancel-discard fails the late-completion corner row
- Guest checks (DESIGN §12):
  - `fail` → `EINVAL` with `opcode 5 error 3` (`IONIC_RC_EIO`)
  - `timeout 1500ms` → `ETIMEDOUT` with `wait status -110`, and no LIF reset
  - `flaky-hw-counters` → `fault_stats_vals_fired` equals the failed reads seen
  - `hang` → watchdog reset observed and recorded (NR-E09)

## F: configurable NIC profiles (`feat/ionic-nic-profiles`)

| ID | Task | Files |
|---|---|---|
| NR-F01 | Profile model + `GKeyFile` loader with strict validation (file, line and key in every error); layering; CLI precedence | new `src/ionic_profile.{c,h}` |
| NR-F02 | `--nic-profile` (repeatable) and `--print-profile`; usage text; log the name chain and a content hash at startup and in the `--stats-file` header | `src/rocm_ernic_server.c`, `src/rocm_ernic_compat.c` |
| NR-F03 | `identity.*` and `link.*` keys drive dev_info, LIF/port status and identity MTU, replacing the constants in `src/ionic_eth_emu.c` | `src/ionic_eth_emu.c` |
| NR-F04 | Built-in `compat` and `small-vm`, compiled in from ini text; `profiles/*.ini` shipped | new `src/ionic_profiles.c`, `profiles/` |
| NR-F05 | Each later PR (A–E) adds its keys, range checks and built-in profile values (`generic-*`, the overlays) in the same PR | per PR |
| NR-F06 | `docs/ionic.rst` "NIC profiles" section with the schema table (upstream doc, in the PR) | `docs/ionic.rst` |

Tests: new `tests/test_ionic_profile.c`.

- positive: each built-in profile parses and validates; `compat` + `generic-400g` + an overlay merges key by key; a CLI flag beats every profile layer
- negative: an unknown section or key, a value out of range, a bad type (`queues.eth = many`), a missing file, and a renamed counter over 27 chars or duplicated; each fails naming file, line and key
- boundary: every numeric key at its minimum and maximum; `fw_version` of exactly 31 chars; an empty file is the same as no layer
- corner: the `--print-profile` output re-parses to an identical merged profile; `compat` produces today's identity bytes exactly (a golden buffer captured from `develop`); `intr_vectors` too small warns but starts
- Regression proof: make the loader ignore unknown keys and the mistyped-key negative row fails; drop a `compat` constant and the golden-identity row fails.
- Guest check: `--nic-profile generic-400g` on a 16-vCPU guest shows `ethtool -l` 16 combined, sysfs `rate` 400 Gb/sec and `ethtool -i` with the profile's firmware version; `--nic-profile compat` matches today's `run-ionic-guest-test` output.

## Done when

- A–F are each merged upstream, or open with their gates green and recorded in STATUS.md.
- The guest shows real `ip -s link`, 126 meaningful MAC counters, a configurable 1–64 queue shape, about 35 RDMA counters, and a reproducible fault for every inject-only counter.
- All of it is selectable through built-in or user profiles, and `compat` is byte-identical to today's device.
- The random fault models match kernel netem event for event at equal seeds (NR-E10), and every `*-hw-counters` overlay has a passing guest check.
- xtcp2 pins an integration commit that contains them, and its ionic VM test asserts on them (tracked in xtcp2).
