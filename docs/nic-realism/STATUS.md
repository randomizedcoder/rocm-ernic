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
| E: fault injection | `feat/ionic-fault-inject` | planned | – | – |
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
| NR-E01 | fault core, deterministic patterns, `stats_vals` `fail`, status codes | planned | | | | |
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
