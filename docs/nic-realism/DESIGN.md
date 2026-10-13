# Design: a realistic counter surface for the emulated ionic NIC

Status: draft, 2026-10-12 (§11 profiles added the same day). This doc lives on the fork-only `docs/nic-realism` branch. The work is tracked in [IMPLEMENTATION-PLAN.md](IMPLEMENTATION-PLAN.md) and [STATUS.md](STATUS.md).

Code references are to upstream `develop` (`e912e84`) unless marked **#222**, which means `feat/ionic-hw-stats` at `66c5141`. Kernel references are to Linux 7.1 `drivers/net/ethernet/pensando/ionic/` (**eth**) and `drivers/infiniband/hw/ionic/` (**rdma**). Line numbers drift; the function names are the stable anchors.

## Contents

1. [Why](#1-why)
2. [Goals and non-goals](#2-goals-and-non-goals)
3. [Reference profile: what modern NICs expose](#3-reference-profile-what-modern-nics-expose)
4. [The driver contract](#4-the-driver-contract)
5. [Common design](#5-common-design)
6. [A: LIF statistics](#6-a-lif-statistics)
7. [B: port (MAC) statistics](#7-b-port-mac-statistics)
8. [C: queues and interrupt vectors](#8-c-queues-and-interrupt-vectors)
9. [D: RDMA hardware and QP counters](#9-d-rdma-hardware-and-qp-counters)
10. [E: fault injection](#10-e-fault-injection)
11. [F: configurable NIC profiles](#11-f-configurable-nic-profiles)
12. [Testing strategy](#12-testing-strategy)
13. [Consumers](#13-consumers)
14. [Open questions](#14-open-questions)

## 1. Why

rocm-ernic lets a guest run the real `ionic` and `ionic_rdma` drivers against an emulated device. That makes it a useful place to test monitoring tools, for example go-link-monitor in xtcp2, on a real driver instead of fakes. But the counters the guest sees look nothing like a production NIC:

- `ip -s link` and `/proc/net/dev` on the ionic netdev read **zero forever**. `ionic_get_stats64` builds them from a stats block the firmware DMAs into guest memory, and the emulator never writes that block.
- The 126 MAC counters in `ethtool -S` (`frames_rx_*`, size buckets, per-priority frames, pause) are always zero for the same reason.
- 4 queue pairs are offered, and only RX queue 0 is ever served. A modern 100/200G NIC runs 48–128.
- RDMA `hw_counters` has 5 entries (**#222**). Real NICs have 30–90, mostly error and congestion counters.
- No error counter can ever become non-zero, so error paths in monitoring tools are never exercised on a real driver.

## 2. Goals and non-goals

Goals:

- The guest sees counters with the **shape** of a modern NIC: the same families, similar numbers of queues and counters, and errors that move when errors happen.
- Every counter is either **real** (counted from an emulator event), **derived** (computed from real events, e.g. size buckets), or **inject-only** (moves only under fault injection). Each counter is labelled with one of these in the tables below.
- The values the guest reads agree with the emulator's own `--stats-file`.
- Faults can be reproduced deterministically, so tests can assert exact counts.

Non-goals:

- Throughput. The emulator reports 100 Gb/s link speed and keeps doing so, but counters only move as fast as the emulated traffic does.
- The standard ethtool statistics groups (`--all-groups`: eth-mac, eth-phy, eth-ctrl, rmon) and the netlink FEC and pause statistics. `ionic_ethtool.c` does not implement those callbacks, so no device-side change can produce them.
- Matching a particular Pensando firmware release bit for bit. Where names are not fixed by the driver ABI we choose clear ones and ask the maintainers (§14).

## 3. Reference profile: what modern NICs expose

These figures come from a read-only scan of a production fleet on 2026-10-12: 343 hosts and 1,625 RDMA ports. They come from `ethtool -l` (current combined channels), `ethtool -S` and `/sys/class/infiniband/*/ports/*/hw_counters`, and are aggregated by NIC family. "Error-type" is a name match on err/drop/discard/miss/crc/fcs/pause/pfc/fec/cnp/ecn/nak/seq and similar. "Non-zero" is the median number of error-type counters above zero on a port.

| NIC family | Link speeds | Queues (median) | `ethtool -S` total | Non-queue stats | Error-type | Non-zero | RDMA `hw_counters` |
|---|---|---|---|---|---|---|---|
| mlx5 (ConnectX-4 Lx … ConnectX-8) | 25–400G | 63 | ~6,800–7,200 | ~265–300 | ~112–127 | 7–16 | ~22–32 |
| ice (E810) | 25–100G | 32–128 | ~220–860 | 94 | 46 | 1–2 | ~47 |
| bnxt_en (BCM574xx/575xx) | 10–25G | 12–48 | ~210 | ~210 | ~94 | 4 | 55–93 |
| i40e (X710) | 10–25G | 32–119 | ~300–680 | — | 62 | 0–2 | 43 |
| **rocm-ernic ionic today** | 100G (reported) | **4** | small | — | **0 real** | **0** | **5** |

RDMA `hw_counters` name families:

- mlx5: IB-standard names: `out_of_buffer`, `out_of_sequence`, `packet_seq_err`, `local_ack_timeout_err`, `rnr_nak_retry_err`, `req_cqe_error`, `resp_cqe_flush_error`, `np_cnp_sent`, `rp_cnp_handled`, `np_ecn_marked_roce_packets` and so on.
- bnxt_re: `active_*`/`watermark_*` resource gauges, the `res_*` responder errors, `tx_roce_*`/`rx_roce_*`, and `rx_cnp_pkts`/`rx_ecn_marked_pkts`.
- irdma (ice): protocol-family counters (`ip4*`, `ip6*`, `tcp*`, `cnp*`).
- ionic: device-defined. The firmware sends the header table (`STATS_HDRS`), so the emulator chooses the set (§9).

## 4. The driver contract

The emulator can only show what the ionic drivers read. These are the facts each change has to respect.

### 4.1 `lif_info`: LIF status and stats

- `struct ionic_lif_info` (eth `ionic_if.h`, about line 3094):
  - `union ionic_lif_config`: 256 bytes
  - `struct ionic_lif_status`: 64 bytes at **256**
  - `struct ionic_lif_stats` at **320**
- The driver allocates it as `ALIGN(sizeof, PAGE_SIZE)` (eth `ionic_lif.c`, about 3300) and passes its address in `LIF_INIT`.
- Today `handle_lif_init` (`src/ionic_eth_emu.c`) writes only the 64-byte status: link up and 100000 Mb/s. It does **not** keep `info_pa`.
- `struct ionic_lif_stats` (eth `ionic_if.h`, about 2937) is an array of `__le64`. In qword order:
  - 0–5: `rx_{ucast,mcast,bcast}_{bytes,packets}`
  - 8–14: `rx_*_drop_{bytes,packets}`, `rx_dma_error`
  - 16–21: `tx_{ucast,mcast,bcast}_{bytes,packets}`
  - 24–30: `tx_*_drop_*`, `tx_dma_error`
  - 32–36: `rx_queue_disabled`, `rx_queue_empty`, `rx_queue_error`, `rx_desc_fetch_error`, `rx_desc_data_error`
  - 40–44: `tx_queue_disabled`, `tx_queue_error`, `tx_desc_fetch_error`, `tx_desc_data_error`, `tx_queue_empty`
  - then `tx_rdma_{ucast,mcast}_{bytes,packets}`, `tx_rdma_cnp_packets`, `rx_rdma_*`, `rx_rdma_cnp_packets` and `rx_rdma_ecn_packets`
  - after a reserved run come the RDMA requester (`rdma_req_*`) and responder (`rdma_resp_*`) error counters
  - the `rsvdN` slots must stay zero
- `ionic_get_stats64` (eth `ionic_lif.c`, about 1272) reads it directly:
  - `rx/tx_packets` and `rx/tx_bytes` are the sums over ucast, mcast and bcast
  - `rx/tx_dropped` is the sum of the `*_drop_packets` counters
  - `multicast` = `rx_mcast_packets`
  - `rx_over_errors` = `rx_queue_empty`
  - `rx_missed_errors` = `rx_dma_error` + `rx_queue_disabled` + `rx_desc_fetch_error` + `rx_desc_data_error`
  - `tx_aborted_errors` is the tx equivalent
  - `rx_errors` / `tx_errors` are the sums of those
- **The driver never asks for a refresh.** `IONIC_LIF_ATTR_STATS_CTRL` is defined but not used. The device has to keep writing the block.

### 4.2 `port_info`: port status and MAC stats

- `struct ionic_port_info` (eth `ionic_if.h`, about 2915):
  - config: 256 bytes
  - `struct ionic_port_status`: 320 bytes at 256 (packed, including the 256-byte xcvr sprom)
  - a union of `ionic_port_stats` and `ionic_mgmt_port_stats` at **576**
  - sprom pages, then reserved, then `extra_stats`
- The driver allocates it as `ALIGN(sizeof, PAGE_SIZE)` (eth `ionic_main.c`, about 725). Today `handle_port_init` writes the first 320 bytes, which covers config and the start of status.
- `ionic_port_stats` is 126 × `__le64`. `ethtool -S` lists all 126, in `ionic_port_stats_desc` order (eth `ionic_stats.c`): `frames_{rx,tx}_*`, `octets_*`, the 12 rx and 12 tx size buckets, `frames_{rx,tx}_pri_0..7`, `{tx,rx}_pripause_N_1us_count`, `rx_pause_1us_count` and `frames_tx_truncated`.
- `extra_stats` is read only when `IONIC_DEV_CAP_EXTRA_STATS` is set. In that case `rx_bits_phy` must be real, or `IONIC_STAT_INVALID` (`~0`) to hide it. We leave the capability **unset** (§14).

### 4.3 Queues and interrupts

- `ionic_lif_size` (eth `ionic_lif.c`, about 4021):
  - `nxqs = min(txq_count, rxq_count, num_online_cpus())`
  - `neqs = min(rdma eq_count, num_online_cpus())`
  - `nintrs = 1 + nxqs + neqs` (one vector for the adminq and notifyq)
- If that exceeds the identity's `nintrs`, or `pci_alloc_irq_vectors` cannot allocate it exactly, `try_fewer` halves the notify queues, then the EQs, then the queues.
- RDMA (rdma `ionic_admin.c`, about 1085) creates EQs until `ionic_intr_alloc` runs out. Fewer than 4 is fatal.
- The emulator advertises 32 vectors (`IONIC_MSIX_MAX_VECTORS`, `src/rocm_ernic_internal.h`), 4 queue pairs and 32 EQs. At 28 or more guest vCPUs that already doesn't fit, and the EQs are halved.
- The driver also keeps 30 software per-queue counters: `tx_N_*` (14) and `rx_N_*` (16). These appear in `ethtool -S` with no device work.

### 4.4 RDMA statistics

- `STATS_HDRS` and `QP_STATS_HDRS` are read into one `PAGE_SIZE` buffer. `ionic_v1_stat_normalize` (rdma `ionic_hw_stats.c`) scans at most `PAGE_SIZE / 32` = **128** entries and stops at the first `NONE`.
- `name[27]` is forced to NUL, so names are at most **27 characters**. They become `/sys/class/infiniband/<dev>/ports/1/hw_counters/<name>` exactly as written.
- `STATS_VALS` is issued on each `get_hw_stats` call (default lifespan about 10 ms). One admin command fetches every counter, so one failure fails every file. `ionic_admin_wait` turns **any** CQE error into `-EINVAL` and logs `opcode %u error %u`.
- Per-QP stats (`QP_STATS_VALS`) are used through `rdma statistic qp set/bind`. The driver sums the values over the QPs bound to a counter.

## 5. Common design

### 5.1 One counter block

A new `struct ionic_emu_stats` holds every counter this design adds, split into `lif`, `port`, `rdma` and `fault` sections.

- It lives in a new `src/ionic_emu_stats.{c,h}`, not in the vendored `third-party/qemu/hw/rdma/vmw/pvrdma.h`, which CLAUDE.md says must not be edited.
- The existing `pvrdma_*_count` hooks (`src/rocm_ernic_compat.c`) stay as they are. New hooks are added next to them.
- Counters are plain `uint64_t`, updated on the main thread. The doorbell handler (`eth_txq_service`), `ionic_eth_emu_poll_rx`, the datapath poll and the admin queue all run there.
- The one producer that runs off the main thread is the mesh receive thread that appends to the eth inbox. Its "inbox full" drop is counted in an `_Atomic uint64_t` and folded in on the main thread, so TSAN stays clean.
- The layout helpers (`lif_stats_index()` and `port_stats_index()`) are the **only** place that knows the qword positions, and a unit test pins them (§12).

### 5.2 The publish tick

- `handle_lif_init` and `handle_port_init` now store `lif_info_pa` and `port_info_pa` in `struct ionic_eth_emu`.
- A new `ionic_eth_emu_publish_stats(emu, now_ms)` runs from the main loop (`rocm_ernic_server.c`, next to the ~1 s `pvrdma_write_stats` call). Every `stats_interval_ms` it:
  - fills a `__le64` buffer from the counter block, with reserved slots zeroed
  - makes one DMA write to `lif_info_pa + 320` and one to `port_info_pa + 576`
- It publishes only after `LIF_INIT` / `PORT_INIT` have run. `LIF_RESET`, device reset and `PORT_RESET` stop it and clear the stored addresses. A guest that frees the DMA area must never be written to.
- Counters are zeroed on device reset, as real firmware does.
- `--stats-interval-ms <n>` (default 1000, minimum 10) lets tests read values without waiting a second.
- Torn reads are acceptable. Each `__le64` is written whole, and real firmware has the same property.

### 5.3 One source, two views

Each event increments one counter. The publish tick copies the counters to guest memory, and `pvrdma_write_stats` copies them to the `--stats-file`. A test can therefore compare guest-visible values with the emulator's own file.

## 6. A: LIF statistics

**Change.** Count frames in the Ethernet datapath and publish `ionic_lif_stats`.

| Counter(s) | Kind | Source |
|---|---|---|
| `tx_{ucast,mcast,bcast}_{bytes,packets}` | real | `eth_txq_service` after `eth_tx_gather`, classified by destination MAC (group bit; all-ones is bcast) |
| `rx_{ucast,mcast,bcast}_{bytes,packets}` | real | `eth_rx_deliver` on success, same classification |
| `rx_*_drop_{bytes,packets}` + `rx_queue_empty` | real | `eth_rx_deliver` returning `-ENOBUFS` on the TAP path, where the frame is discarded |
| `rx_*_drop_*` | real | mesh inbox full (`-ENOSPC`), counted with the atomic from §5.1 |
| `rx_desc_data_error` | real | `-EMSGSIZE`: frame larger than the posted buffer |
| `rx_dma_error` | real | rx DMA write failure (`-EIO`) |
| `tx_*_drop_*` | real | `ionic_eth_net_send` failure; the return value is ignored today |
| `tx_dma_error` | real | tx gather or completion DMA failure |
| `tx_rdma_ucast_{bytes,packets}`, `rx_rdma_ucast_*` | real | the RDMA datapath byte hooks (`pvrdma_rdma_bytes_count` call sites); one WQE/message counts as one packet |
| `*_rdma_cnp_packets`, `rx_rdma_ecn_packets` | inject-only | E |
| `rdma_req_*`, `rdma_resp_*` | real or inject-only | D fills them; A only reserves the layout |
| `rx/tx_queue_disabled`, `*_desc_fetch_error` | real | doorbell on a queue that is not enabled, or a bad descriptor address |

The inbox path's re-queue on `-ENOBUFS` is back-pressure, not a drop, and is **not** counted.

**Guest result:** `ip -s link`, `/proc/net/dev`, rtnetlink `IFLA_STATS64`, and the `hw_tx_dropped`/`hw_rx_dropped`/`hw_rx_over_errors`/`hw_rx_missed_errors`/`hw_tx_aborted_errors` lines in `ethtool -S` become real.

**Files:** `src/ionic_eth_emu.c`, the new `src/ionic_emu_stats.{c,h}`, `src/rocm_ernic_server.c` (tick and option), and `CMakeLists.txt`.

**Risks:**
- A DMA write after the guest has freed the area. This is avoided by clearing the stored addresses on reset.
- Double counting a frame that is re-queued. Frames are counted only on final delivery or drop.

## 7. B: port (MAC) statistics

**Change.** Publish `ionic_port_stats` from the same tick, counting at the "wire" side, which is the TAP/mesh boundary.

| Counter(s) | Kind | Rule |
|---|---|---|
| `frames_{rx,tx}_ok`, `octets_{rx,tx}_ok`, `frames_*_all`, `octets_*_all`/`_total` | real | every frame crossing the boundary; `octets` includes the 4-byte FCS the emulator does not carry, so it is frame length + 4, which is how real MACs count |
| `frames_{rx,tx}_{unicast,multicast,broadcast}` | real | destination MAC |
| the 12 rx and 12 tx size buckets (`less_than_64b` … `8192b_9215b`, `other`) | derived | frame length + 4 |
| `frames_rx_too_long`, `frames_rx_oversized` | real | rx frame longer than MTU + header; the frame is still dropped as today |
| `frames_rx_undersized` | real | rx frame shorter than 60 bytes before FCS |
| `frames_{rx,tx}_vlan_good` / `frames_tx_vlan` | real | 802.1Q TPID present |
| `frames_{rx,tx}_pri_0..7` | derived | VLAN PCP; untagged frames count as priority 0 |
| `frames_rx_dropped` | real | same events as the A rx drops |
| `frames_rx_bad_fcs`, `frames_rx_bad_all`, `frames_rx_stomped_crc` | inject-only | E `eth_rx` → `corrupt`; the frame is dropped |
| `frames_rx_fragments`, `frames_rx_jabber`, `frames_rx_bad_length`, `frames_tx_bad`, `frames_tx_truncated` | inject-only | not reachable without a PHY; zero unless injected |
| `frames_*_pause`, `frames_*_pripause`, `*_pripause_N_1us_count`, `rx_pause_1us_count` | inject-only | E `eth_rx` → `pause`; no flow control is emulated |

**Guest result:** all 126 MAC lines in `ethtool -S` carry meaning, and the per-priority and size-bucket shape matches a real MAC.

**Files:** `src/ionic_eth_emu.c` and `src/ionic_emu_stats.{c,h}`.

## 8. C: queues and interrupt vectors

**Change:**

- The profile key `queues.eth` (1–64), or `--eth-queues <n>`, sets the advertised RX and TX queue counts (§11). This replaces the `IONIC_EMU_ETH_QCOUNT 4` constant. The `compat` profile keeps 4; the `generic-*` profiles use 16 or 64.
- `IONIC_MSIX_MAX_VECTORS` rises from 32 to 128: 1 + 64 + 32 = 97 fits with room to spare.
- The MSI-X table and PBA sizing that `nr_intrs` feeds (`rocm_ernic_server.c`, the capability setup and `vfu_setup_device_nr_irqs`), and the per-vector arrays in `struct ionic_eth_emu`, are checked and resized.
- **Every** RX queue is served, not just queue 0 (`ionic_eth_emu_poll_rx`). Steering is FNV-1a over the IPv4/IPv6 5-tuple (src/dst address, protocol, src/dst port), falling back to the source and destination MAC for non-IP frames, modulo the number of active queues. A fixed, simple hash lets tests predict which queue a frame lands on; Toeplitz RSS is not needed because the guest never programs an RSS key the emulator honours.
- TX already works per queue id.

**Why not always 64?** The guest gets `min(queues, online vCPUs)`, so 64 queues mean nothing on a 4-vCPU test VM, and each active queue costs interrupt vectors and poll work. So the queue count is a profile choice: `generic-100g` uses 16, which fits common test VM sizes, and `generic-400g` uses 64, the fleet-like shape when the VM has the vCPUs.

**Guest result:** `ethtool -l` shows the configured count. `ethtool -S` gains 30 software counters per queue: 16 queues give about 480 and 64 give about 1,920, the same order as mlx5. Traffic spreads across queues.

**Risks:**
- More vectors widen the vfio-user IRQ setup.
- Serving several RX queues changes the order of delivery across flows, though not within a flow.

## 9. D: RDMA hardware and QP counters

**Change:**

- Raise `STATS_MAX_COUNT` (**#222** `src/ionic_adminq.c`) from 8 to 64; the kernel allows 128.
- Keep the five **#222** names (`commands`, `total_bytes_{sent,received,rdma_read,rdma_write}`) so existing consumers keep working.
- Add requester and responder error counters, plus traffic counters.

**Naming rule:**

- Start from the driver ABI names in `struct ionic_lif_stats`, which are the only Pensando names we have, and drop the `rdma_` prefix.
- Of the 27 `rdma_req_*`/`rdma_resp_*` names, 11 are over the 27-character limit. Dropping the prefix fixes 10 of them.
- The one left, `resp_rx_out_of_atomic_resource` (30), becomes `resp_rx_out_of_atomic_res`.
- A unit test asserts that every name is ≤ 27 characters and unique.
- If the maintainers provide the real firmware names, we switch to them before merge (§14).

**Mapping from emulator events.** Error completions all pass through `cq_post` (`src/ionic_datapath.c`), which already gets `error`. The site also knows the status and whether the QP was acting as requester or responder.

| Counter | Kind | Emulator event (`ionic_datapath.c`) |
|---|---|---|
| `req_tx_local_access_err` | real | `IONIC_STS_LOCAL_ACC_ERR`: local copy/gather/scatter DMA failure |
| `req_tx_local_oper_err` | real | `IONIC_STS_LOCAL_QP_OPER_ERR`: unsupported opcode |
| `req_tx_memory_mgmt_err` | real | `IONIC_STS_LOCAL_PROT_ERR`: lkey/VA not covered by an MR |
| `req_rx_remote_access_err` | real | `IONIC_STS_REMOTE_ACC_ERR` on a requester: rkey does not cover, remote write/read/atomic fails |
| `req_rx_remote_inv_req_err` | real | SEND to an unknown or failed remote QP |
| `req_rx_rnr_retry_err` | real | `IONIC_STS_RNR_RETRY_EXCEEDED`: no receive posted within `RNR_RETRY_MS` |
| `req_rx_implied_nak_seq_err` | real | `IONIC_STS_RETRY_EXCEEDED`: pending request older than `PENDING_MS` |
| `req_rx_cqe_err`, `resp_rx_cqe_err` | real | any error CQE, by role |
| `req_rx_cqe_flush_err`, `resp_rx_cqe_flush_err` | real | WQEs flushed when a QP moves to ERR |
| `resp_rx_local_len_err` | real | `IONIC_STS_LOCAL_LEN_ERR`: receive truncated |
| `resp_tx_rnr_retry_err` | real | each RNR re-queue pass (the responder had no receive posted) |
| `resp_rx_out_of_buffer` | real | the same event, counted once per message, as the mlx5 `out_of_buffer` |
| `resp_tx_remote_access_err` | real | `REMOTE_ACC` returned for an inbound request |
| `req_rx_pkt_seq_err`, `resp_rx_out_of_seq_pkts`, `resp_tx_pkt_seq_err`, `req_rx_dup_responses`, `resp_rx_dup_requests` | inject-only | there is no packet loss on loopback/TCP backends; E `rdma_rx` → `seq_err` |
| `tx_rdma_cnp_packets`, `rx_rdma_cnp_packets`, `rx_rdma_ecn_packets` (as `tx_cnp_pkts`, `rx_cnp_pkts`, `rx_ecn_pkts`) | inject-only | E `rdma_tx` → `cnp`, `rdma_rx` → `ecn` |
| `tx_rdma_ucast_{bytes,pkts}`, `rx_rdma_ucast_{bytes,pkts}` | real | the byte hooks from A |

- The same counters fill the `rdma_*` qwords of `ionic_lif_stats` (A), so `ethtool`-side and `hw_counters`-side values agree.
- Per-QP (`QP_STATS_HDRS`) gains the per-QP equivalents of the error counters, kept in the QP's counter entry next to the existing doorbell/WQE/CQE counts.

**Guest result:** about 35 `hw_counters` files with error and congestion families like mlx5 and bnxt_re. Errors move when the guest provokes them, for example a receive buffer too small or an RDMA write with a bad rkey.

**Files:** **#222** `src/ionic_adminq.c`, `src/ionic_datapath.c`, `src/rocm_ernic_compat.{c,h}` (hook declarations) and `src/ionic_emu_stats.{c,h}`.

## 10. E: fault injection

### 10.1 Model

A fault has four parts, the same way `tc netem` separates a loss model from what gets lost:

- **Site.** Where in the emulator it can fire. Each site has one kind of *eligible event*: an rx frame, a tx frame, an inbound RDMA message, a `STATS_VALS` command, and so on.
- **Pattern.** On which eligible events it fires (§10.3).
- **Effect.** What happens when it fires (§10.4).
- **Gate.** Optional limits: a time window, a start delay, a maximum number of fires (§10.3).

A site can carry several faults. Each is an ini section `[fault.<site>]` or `[fault.<site>.<label>]`, for example `[fault.eth_rx.fcs]` and `[fault.eth_rx.nobuf]`.

- On each eligible event, every fault on the site advances its own pattern, so faults stay independent (§10.5).
- The first fault, in profile order, whose pattern fires applies its effect. Later faults count the event as `masked`, so their own numbers stay correct.
- Each fault has its own random stream.

### 10.2 Sites

| Site | Eligible event | Effects | Hook (from A, B, D) |
|---|---|---|---|
| `eth_rx` | frame about to be delivered to an RX queue | `drop`, `nobuf`, `corrupt`, `duplicate`, `pause` | `eth_rx_deliver` |
| `eth_tx` | frame gathered from a TX queue | `drop`, `corrupt`, `duplicate` | `eth_txq_service` |
| `rdma_rx` | inbound RDMA message (SEND, WRITE, READ request, atomic) | `rnr`, `seq_err`, `ecn` | datapath receive path |
| `rdma_tx` | outbound RDMA message | `cnp`, `retransmit` | datapath send path |
| `stats_vals` | `STATS_VALS` command (device `hw_counters`) | the stats effects (§10.4.3) | `handle_stats_op` |
| `qp_stats_vals` | `QP_STATS_VALS` command (per-QP counters) | the stats effects | `handle_stats_op` |
| `stats_publish` | one publish tick of the lif/port DMA blocks (§5.2) | `skip`, `zero`, `reset` | `ionic_eth_emu_publish_stats` |

### 10.3 Patterns

**Deterministic patterns.** These need no seed and give exact counts.

| Pattern | Syntax | Fires on |
|---|---|---|
| every | `every N` | events N, 2N, 3N, … |
| after | `after N [count M]` | events N … N+M-1 (M defaults to 1) |
| burst | `burst N every M` | the first N of every M events, i.e. an outage of N in each period of M |
| sequence | `sequence "1x3,0x10,1,0x100"` or `sequence @file` | an explicit fire/pass pattern (`1` fire, `0` pass, `xK` repeat), looped; `@file` replays a recorded trace (§10.6) |

**Random patterns.** These have the same names and meaning as `tc netem` loss models, so a user who knows netem doesn't have to learn new terms.

| Pattern | Syntax | Meaning |
|---|---|---|
| random | `random P [C]` | each event fires with probability P%, with optional correlation C% (netem `loss random`) |
| state | `state p13 [p31 [p32 [p23 [p14]]]]` | the 4-state Markov (GI) model of netem `loss state`: good reception, good gap, burst loss and isolated loss |
| gemodel | `gemodel p [r [1-h [1-k]]]` | Gilbert-Elliott, as netem `loss gemodel`: p = good→bad, r = bad→good (default 1-p), 1-h = fire probability in bad (default 100 %), 1-k = fire probability in good (default 0 %). The defaults are tc's, so `gemodel p r` is the Simple Gilbert model |

- The models behave as Linux netem's (`net/sched/sch_netem.c`: `get_crandom`, `loss_4state`, `loss_gilb_ell`). With an explicit `seed`, a fault fires on exactly the events where netem with the same parameters and `seed` would drop (§10.5). How the code is obtained is §10.8.
- netem's correlation is, in its own man page's words, "an approximation" rather than true statistical correlation. We copy it as is, so the two tools agree.
- Percentages accept netem's forms (`0.1%`, `25%`) and are converted to a `uint32_t` threshold as netem does.

**Gates.** These apply on top of any pattern.

| Gate | Syntax | Meaning |
|---|---|---|
| window | `window on T1 off T2` | the pattern is active for T1, inactive for T2, and repeats; for example, a 30 s outage every 5 min |
| start | `start T` | inactive for the first T after the guest driver's `LIF_INIT` (not after server start, so a slow guest boot doesn't shift it) |
| stop | `stop T` | inactive after T |
| limit | `limit N` | stops after N fires |

Times use netem's units (`ms`, `s`, `min`) and are measured on `CLOCK_MONOTONIC` (`dp_now_ms()`). Events are counted only while the gate is open, so `after 3` inside a window means the third eligible event *in* that window.

### 10.4 Effects

#### 10.4.1 Ethernet

| Effect | What happens | Counters it moves |
|---|---|---|
| `drop` | frame silently lost on the "wire": tx is completed but not sent; rx is never delivered | `tx_*_drop_*` / `frames_rx_dropped` |
| `nobuf` | rx treated as "no descriptor posted" | `rx_queue_empty`, `rx_*_drop_*` |
| `corrupt` | the frame fails FCS; a MAC discards bad-FCS frames, so it is dropped, not delivered corrupted | `frames_rx_bad_fcs`, `frames_rx_bad_all` (rx) / `frames_tx_bad` (tx) |
| `duplicate` | the frame is delivered or sent twice | the normal ok counters, twice |
| `pause` | the frame is delivered and counted as a received pause quantum | `frames_rx_pause`, `rx_pause_1us_count` |

#### 10.4.2 RDMA

| Effect | What happens | Counters it moves |
|---|---|---|
| `rnr` | one forced RNR pass; the message is delivered on the next poll | `resp_tx_rnr_retry_err`, `resp_rx_out_of_buffer` |
| `seq_err` | counted as an out-of-sequence packet, still delivered (loopback/TCP backends never lose packets, so no real retransmit happens) | `resp_rx_out_of_seq_pkts`, `req_rx_pkt_seq_err` |
| `ecn` | counted as an ECN-marked arrival | `rx_ecn_pkts` (lif `rx_rdma_ecn_packets`) |
| `cnp` | counted as a CNP sent and handled | `tx_cnp_pkts`, `rx_cnp_pkts` (lif `*_rdma_cnp_packets`) |
| `retransmit` | counted as a requester retransmit, message sent once | `req_rx_implied_nak_seq_err` |

#### 10.4.3 Stats reads: flaky `hw_counters`

The guest-visible result of each effect follows from what the ionic RDMA driver does (§4.4):

- One `STATS_VALS` fetches every counter.
- The command waits with `IONIC_ADMIN_F_INTERRUPT`: an interruptible wait of `HZ`, i.e. 1 s (`drivers/infiniband/hw/ionic/ionic_admin.c`, `ionic_admin_wait`). On timeout the driver returns `-ETIMEDOUT`, logs `wait status -110` and calls `ionic_admin_cancel`.
- Any CQE error becomes `-EINVAL`, with the log line `opcode %u error %u`.
- The admin watchdog kills an admin queue that makes no progress for `IONIC_ADMIN_TIMEOUT` (`HZ * 2`) and resets the RDMA LIF.

| Effect | Emulator does | Guest sees | Real-world analogue |
|---|---|---|---|
| `fail [status S]` | completes with CQE error status S (`eio` default, `enoent`, `eversion`, …) | **every** `hw_counters` read fails with `EINVAL`; the kernel logs `opcode 5 error <S>` | the fleet's ConnectX-7 IB ports that refuse PortCounters |
| `stale` | returns the values from the previous successful reply | counters freeze (zero delta) while traffic flows | firmware that stopped refreshing its counters |
| `zero` | returns all zeros once | counters drop to 0 and then jump back on the next good read | a firmware glitch; tests "counter went backwards" handling |
| `reset` | zeroes the emulator's RDMA counters, then replies | counters restart from 0 and keep counting from there | a firmware or device reset that clears counters |
| `wrap` | adds an offset so each counter is within 2³² (or `wrap 64`: 2⁶⁴) of wrapping, then replies | the next reads cross the wrap | old 32-bit counters, or a long-lived 64-bit counter |
| `delay D [J [distribution X]]` | holds the completion for D, with optional jitter J drawn from a netem distribution table (`normal`, `pareto`, `paretonormal` or a `.dist` file, §10.8); D + J is capped at 900 ms | the read succeeds but is slow; `pareto` gives the long tail real firmware shows | a firmware busy with other admin work; tests scrape latency and timeouts in the monitor |
| `timeout D` | holds the completion for D, with 1100 ms ≤ D ≤ 1900 ms | the read fails with **`ETIMEDOUT`**, the kernel logs `wait status -110` and cancels the request, and the late completion is discarded | a firmware stall that recovers |
| `hang` | never completes the command | after 1 s the read fails with `ETIMEDOUT`; after 2 s the driver's watchdog kills the admin queue and **resets the RDMA LIF**, destroying the guest's RDMA resources | a firmware hang; **destructive** |

- `hang` is refused unless the profile also sets `faults.allow_destructive = true`. It is never in a built-in overlay.
- The `hang` result (the watchdog fires after the cancel and resets the LIF) is read from the source but not yet observed. NR-E09 confirms it in a guest before the effect is documented for users.
- The ranges keep a 100 ms margin around the 1 s wait and the 2 s watchdog, so jiffy rounding and guest scheduling can never turn a `delay` into a timeout, or a `timeout` into a LIF reset. A value outside its range is a startup error.
- `stats_vals` and `qp_stats_vals` are separate sites. A profile can make only per-QP stats flaky, which affects `rdma statistic qp` users and leaves the port `hw_counters` alone.

`stats_publish` effects:

| Effect | Guest sees |
|---|---|
| `skip` | `ip -s link` / `ethtool -S` stay frozen for that interval, then catch up |
| `zero` | the lif/port blocks read zero for one interval |
| `reset` | the lif/port counters restart from 0 |

They act on the Ethernet-side views the same way the stats-read effects act on `hw_counters`.

**Event counting and the kernel cache.** The RDMA core caches `hw_counters` for the driver's lifespan (about 10 ms). So the eligible event is a `STATS_VALS` **command**, not a file read: reading 30 files within 10 ms is one event. The `fault_<section>_eligible` counter (§10.6) lets a test see how many commands its reads produced.

### 10.5 Randomness and reproducibility

- **Generator.** The generator is the same one netem uses: L'Ecuyer's four-component Tausworthe generator (LFSR113, as in the kernel's `prandom_u32_state`), seeded from a 64-bit value the same way as `prandom_seed_state`.
  - The point is compatibility, not quality. LFSR113 is good enough for fault patterns, and sharing it means a netem `seed` means the same here.
- **netem-exact mode.** When a fault sets an explicit `seed`, its stream starts in the same state as a netem qdisc created with that `seed`. It then consumes the generator exactly as netem's loss path does for the same model.
  - The fault fires on the same event indices as `tc qdisc add … netem loss <model> … seed <seed>` drops packets.
  - This holds when the netem qdisc has loss as its only random option, because netem's duplicate, corrupt, reorder and jitter decisions draw from the same stream. §12 tests it against the real kernel.
- **Derived seeds.** A fault without its own `seed` uses `fnv1a64(faults.seed ‖ section name)` (`faults.seed` defaults to 1).
  - Adding, removing or retuning one fault never changes another fault's sequence.
  - A run is reproducible from one top-level seed.
- **Event-driven, not timer-driven.** A stream advances only on eligible events while the gate is open. The same seed and the same guest workload give the same fires.
- Wall-clock gates (`window`, `start`, `stop`) are the only time-dependent part, so they are noted in the trace (§10.6).
- **No `rand()`** and no global state, so TSAN stays quiet and tests can run faults in parallel.

### 10.6 Syntax, observability and replay

**Profile form** (§11): one ini section per site.

```ini
[fault.stats_vals]
pattern = gemodel 1% 30% 100% 0%     # outages averaging 1/r ≈ 3.3 commands
effect  = fail status eio
window  = on 10min off 50min          # flaky for 10 minutes each hour
seed    = 42

[fault.eth_rx]
pattern = random 0.1% 25%
effect  = corrupt
```

**CLI form.** This is one netem-style string per `--inject`, parsed into the same structure: `--inject 'stats_vals: gemodel 1% 30% effect fail window on 10min off 50min'`. It is a shorthand for quick runs; `--print-profile` shows it in profile form.

**Observability:**
- At startup, each active fault is logged in canonical form, along with the seed in use.
- Per fault (named by its section, e.g. `stats_vals` or `eth_rx_fcs`), the `--stats-file` carries `fault_<section>_eligible`, `fault_<section>_fired` and `fault_<section>_masked`.
- `faults.trace = <path>` writes one line per fire: monotonic ms, site, eligible-event index, effect. This is off by default.
- A trace (or an edited one) can be replayed exactly with `pattern = sequence @<path>`. This turns a flaky run that broke a monitor into a deterministic regression test.

**Cost.** With no faults configured, each site costs one predictable branch on a `const` pointer. An active fault costs one PRNG step per eligible event for the random patterns, and a counter compare for the deterministic ones.

### 10.7 Status codes

The same PR fixes the status codes the stats handler returns (**#222** `handle_stats_op`, currently `1` = `IONIC_RC_EVERSION`): `IONIC_RC_ENOENT` for an unknown QP id and `IONIC_RC_EIO` for a counter or DMA failure. The kernel maps both to `-EINVAL`, but the firmware ABI and the kernel log become correct. They are also the defaults for `fail status` above.

**Files:** the new `src/ionic_fault.{c,h}` (model, patterns, PRNG, parser, trace), `src/rocm_ernic_server.c` (options and usage), the hook sites from A, B and D, and `src/ionic_adminq.c` (stats effects, including deferred completion for `delay`/`timeout`/`hang`).

### 10.8 Reusing netem: library, copy or reimplement

Users want netem's behaviour, so the obvious question is whether to reuse netem's code instead of writing our own. Where each part lives decides it:

| Part | Where it lives | Licence | Usable as a library? |
|---|---|---|---|
| Loss models, correlation, PRNG use (`loss_4state`, `loss_gilb_ell`, `get_crandom`, `tabledist`) | Linux kernel, `net/sched/sch_netem.c` | GPL-2.0-only | No. It is kernel code, built against `sk_buff`, qdisc and `prandom` internals |
| `tc … netem` argument parser | iproute2, `tc/q_netem.c` | GPL-2.0-only | No. It only parses arguments into a netlink message for the kernel; iproute2's `libnetlink`/`libutil` are internal static archives with no stable API |
| Delay distribution tables and their generators | iproute2, `netem/` (`normal.c`, `pareto.c`, `paretonormal.c`, `maketable.c`, `experimental.dat`) | NIST-PD (public domain) | Yes, as **data**: the built `*.dist` files are installed in tc's lib directory (`$TC_LIB_DIR`, else `/usr/lib/tc`; under Nix `${iproute2}/lib/tc`) |
| rocm-ernic `src/` | here | GPL-2.0-or-later | – |

So there is no netem library to link. That leaves three options:

1. **Copy `sch_netem.c` functions.** This is rejected. GPL-2.0-only code would make `src/ionic_fault.c`, and so the server binary, effectively GPL-2.0-only, which is a licence change for the maintainers to make, not a side effect of a feature PR. The functions are also tied to kernel types and would need rewriting anyway.
2. **Run real netem.** This is supported as a complement, not a replacement (below).
3. **Reimplement compatibly (chosen).**
   - Write the models from their published descriptions: Gilbert-Elliott, and the 4-state model from the NetemCLG work of Salsano, Ludovici and Ordine, which the netem man page cites.
   - Use the same parameter meaning and `u32` threshold arithmetic, and LFSR113 from L'Ecuyer's paper.
   - This is about 150 lines. Prove agreement by **testing against the real kernel netem** (§12), not by copying it.
   - The kernel stays the reference, and our code stays GPL-2.0-or-later. Whether matching netem's seed folding bit-for-bit is acceptable is put to the maintainers (§14 q7).

**Distribution tables.** `delay … distribution X` (§10.4.3) reads netem's `.dist` format, which is whitespace-separated `int16` values, `#` comment lines, at most `NETEM_DIST_MAX` (16384) entries and scaled by `NETEM_DIST_SCALE` (8192). It reads from tc's lib directory, or from a path. The parser accepts what `tc/q_netem.c` `get_distribution` accepts.
- We read the user's installed files at runtime instead of vendoring them. That keeps the repo small and lets a user's own `maketable` output (for example, from measured firmware latencies) work unchanged.
- Since the tables are public domain, vendoring the four standard ones as a fallback is also allowed, if the maintainers prefer no runtime lookup.

**Real netem where it fits.** With the TAP backend, the emulator's host-side TAP device is an ordinary netdev. So `tc qdisc add dev <tap> root netem loss gemodel 1% 30% delay 5ms 1ms distribution pareto` already gives the full netem on guest Ethernet traffic, with no emulator code. That is the right tool for *wire* behaviour:
- Packets netem drops never reach the NIC, so the guest's counters correctly show nothing, exactly as with a real lossy cable upstream of a switch.
- The emulator's own faults are for what netem cannot reach:
  - NIC-side accounting, such as bad-FCS frames that real MACs count
  - RDMA error and congestion paths, since TCP-backed RDMA traffic only sees netem loss as TCP retransmits
  - the device's own stats and admin behaviour
- `docs/ionic.rst` will show both, so users pick the right layer.

## 11. F: configurable NIC profiles

**Why.** No single shape is right for everyone. A monitoring developer wants a fleet-like 64-queue 400G NIC. A CI job on a 2-vCPU runner wants something small. A test of error handling wants a lossy link or counters the kernel can't read. So every value A–E introduces, plus the identity values that are hard-coded today, comes from a **NIC profile** instead of a constant.

### 11.1 Format and precedence

- **Format.** Profiles are ini files read with glib's `GKeyFile`. glib is already a required dependency (`CMakeLists.txt`, `pkg_check_modules(GLIB2 REQUIRED glib-2.0)`), so this adds no new library. Ini is readable and easy to diff, and supports comments.
- **Selection.** `--nic-profile <name|path>` picks a profile. A bare name selects a built-in profile; anything with a `/` or ending in `.ini` is read from disk.
- **Layering.** The option can be repeated, and later layers override earlier ones key by key. This lets a scenario profile go on top of a base profile, for example `--nic-profile generic-400g --nic-profile lossy-ethernet`.
- **Precedence**, lowest first:
  1. the built-in `compat` profile, which is always the base
  2. each `--nic-profile`, in order
  3. individual CLI flags (`--eth-queues`, `--stats-interval-ms`, `--inject`), which stay as shortcuts

  `--print-profile` prints the merged result as ini and exits, so any run can be reproduced from one file.
- **Built-in profiles** are compiled-in ini strings (`src/ionic_profiles.c`), parsed through the same loader as files, so they can't drift from the file format. The ini source also ships in `profiles/` for users to copy and edit.
- **Strict validation.** An unknown section or key, a value out of range or a bad type is a startup error naming the file, line and key, and the server does not start. A mistyped key such as `queue.eth = 64` (for `queues.eth`) must not silently leave the count at 4.
- **Logging.** The merged profile's name chain and a hash of its content are logged at startup and written to the `--stats-file` header, so a stats file records the configuration that produced it.

### 11.2 Schema

The `compat` column is the default. `compat` keeps today's behaviour exactly, so existing users see no change. The only exception is that A and B now publish real stats, which fixes a bug rather than changing shape.

| Key | Type / range | `compat` | Drives | PR |
|---|---|---|---|---|
| `identity.asic_type` | `none`, `capri`, `elba`, `giglio`, `salina` | `none` | dev_info `asic_type` (`IONIC_ASIC_TYPE_*`) | F |
| `identity.asic_rev` | 0–255 | 0 | dev_info `asic_rev` | F |
| `identity.fw_version` | string, ≤ 31 chars | `rocm-ernic-1.0` | dev_info `fw_version`; `ethtool -i` and `fw_ver` in sysfs | F |
| `link.speed_mbps` | one of the `IONIC_SPEED_*` values (1000 … 400000) | 100000 | `lif_status.link_speed` and `port_status.speed`; `ethtool` and sysfs `rate` | F |
| `link.xcvr` | `copper`, `fiber`, `none` | `copper` | `port_status.xcvr` phy/pid; `ethtool` port type | F |
| `link.mtu_max` | 1500 up to the emulator frame limit (`IONIC_ETH_NET_MTU_MAX`) | 9000 (`IONIC_ETH_MTU`) | identity max MTU; B's too-long rule | F |
| `queues.eth` | 1–64 | 4 | LIF identity RX/TX queue counts | C |
| `queues.rdma_eqs` | 4–32 | 32 | RDMA `eq_qtype` count | C |
| `queues.intr_vectors` | 2–128, or `auto` | 32 | dev identity `nintrs` and MSI-X size; `auto` = 1 + `eth` + `rdma_eqs` | C |
| `stats.interval_ms` | 10–60000 | 1000 | publish tick (§5.2) | A |
| `stats.extra_stats` | bool | false | `IONIC_DEV_CAP_EXTRA_STATS`; when true, `rx_bits_phy` = `octets_rx_all × 8` | B |
| `rdma.stats` | `none`, `global`, `qp`, `global,qp` | `global,qp` | `rdma.stats_type`; `none` means no `hw_counters` directory, the state before #222 | D |
| `rdma.counters` | list of counter ids, or `all` | the five #222 counters | which entries `STATS_HDRS` lists, and in what order | D |
| `rdma.names.<id>` | string, ≤ 27 chars, unique | the §9 name | the sysfs file name for counter `<id>` | D |
| `faults.seed` | u64 | 1 | base for derived fault seeds (§10.5) | E |
| `faults.trace` | path, or empty | empty | fire trace (§10.6) | E |
| `faults.allow_destructive` | bool | false | permits `hang` (§10.4.3) | E |
| `fault.<site>[.<label>].pattern` | a §10.3 pattern, or `none` | unset | when the fault fires; `none` removes a fault set by an earlier layer | E |
| `fault.<site>[.<label>].effect` | a §10.4 effect valid for the site | required if `pattern` is set | what happens | E |
| `fault.<site>[.<label>].window` / `start` / `stop` / `limit` | §10.3 gates | unset | when the pattern may fire | E |
| `fault.<site>[.<label>].seed` | u64 | derived | netem-exact stream (§10.5) | E |

**Rules applied after merging:**

- If `queues.intr_vectors` is too small for `1 + min(eth, cpus) + rdma_eqs`, the server warns. The driver will halve EQs or queues (§4.3). This is allowed, because it is a real-world scenario a user may want, but it is never silent.
- A `[fault.*]` section is replaced as a whole by a later layer that defines the same section, so a scenario overlay never inherits half of another fault's pattern.
- Counter ids come from a fixed catalog. Each id is one emulator event, and a profile can choose and rename ids but cannot invent events.
- After renaming, every name must be ≤ 27 characters and unique, or startup fails.
- Not configurable: anything the driver ABI fixes, such as the `lif_stats` and `port_stats` layout and the MAC counter names. Changing those would only break the guest driver.

### 11.3 Built-in profiles

| Profile | Purpose | Key settings |
|---|---|---|
| `compat` | today's device; the default | as the table above |
| `small-vm` | CI runners and 1–2 vCPU guests | `queues.eth = 2`, `rdma_eqs = 4`, `intr_vectors = auto` |
| `generic-100g` | a typical modern 100G RoCE NIC | 100G, `fiber`, `queues.eth = 16`, `rdma.counters = all` |
| `generic-400g` | the fleet's large-NIC shape | 400G, `fiber`, `queues.eth = 64`, `intr_vectors = auto`, `rdma.counters = all` |
| `lossy-ethernet` (overlay) | Ethernet error paths | `eth_rx.fcs`: `random 0.1% 25%` → `corrupt`; `eth_rx.nobuf`: `gemodel 0.2% 20%` → `nobuf` (ring-full bursts); `eth_rx.pause`: `every 200` → `pause` |
| `rdma-errors` (overlay) | RDMA error and congestion paths | `rdma_rx.rnr`: `every 100` → `rnr`; `rdma_rx.seq`: `random 0.1%` → `seq_err`; `rdma_tx`: `gemodel 2% 10%` → `cnp` (congestion episodes) |
| `unreadable-hw-counters` (overlay) | the fleet's ConnectX-7 case, through ionic | `stats_vals`: `every 1` → `fail` |
| `flaky-hw-counters` (overlay) | intermittent read failures in bursts, with recovery | `stats_vals`: `gemodel 2% 50%` → `fail` (about 2 % of commands start an outage averaging 2 commands) |
| `stuck-hw-counters` (overlay) | counters that freeze for minutes | `stats_vals`: `every 1` → `stale`, `window on 5min off 25min` |
| `resetting-hw-counters` (overlay) | counters cleared by a device reset | `stats_vals`: `every 600` → `reset` (about every 10 min at a 1 s scrape) |
| `wrapping-hw-counters` (overlay) | counter wrap handling | `stats_vals`: `after 5` → `wrap` |
| `slow-hw-counters` (overlay) | slow and occasionally timed-out reads | `stats_vals.slow`: `random 20%` → `delay 50ms 200ms distribution pareto`; `stats_vals.timeout`: `random 0.5%` → `timeout 1500ms` |

- The `*-hw-counters` overlays cover each flaky-counter behaviour a monitor must survive. They can be stacked, since they use distinct sections only where the behaviours can coexist.
- **No vendor names in built-in profiles.** None is named after a real product. `asic_type` can say `salina`, but the counter names and queue counts are not calibrated against real hardware (§14 q1).
- A `pensando-<model>` profile is added only when we have real data from that card: a `hw_counters` listing, `ethtool -S` and `ethtool -l`.
- Users with such hardware can contribute a profile file, which is the easiest way for the project to grow realistic shapes.

### 11.4 Example

```ini
# my-400g-lossy.ini: a 400G NIC with a slightly dirty link
[identity]
asic_type = salina
fw_version = 1.110.0-a-42

[link]
speed_mbps = 400000
xcvr = fiber

[queues]
eth = 32
intr_vectors = auto

[rdma]
counters = all
# mimic another tool's naming while testing a dashboard
names.resp_rx_out_of_buffer = out_of_buffer

[faults]
seed = 7

[fault.eth_rx.fcs]
pattern = random 0.02%
effect  = corrupt

# a 30 s burst of unreadable hw_counters every hour, starting 10 min in
[fault.stats_vals]
pattern = every 1
effect  = fail
window  = on 30s off 59min30s
start   = 10min
```

```
rocm-ernic-server --nic-profile generic-400g --nic-profile ./my-400g-lossy.ini --print-profile
```

### 11.5 Out of scope for F

- **Changing a profile while the guest is running.** Queue counts and identity are read by the driver at probe time, so changing them live would mean a device reset. Fault knobs are different and could change live. A later control socket could flip `faults.*` at runtime; that is noted as follow-up, not designed here.

## 12. Testing strategy

**Unit tests.** Every change gets C unit tests in the repo's table style (`tests/test_ionic_stats.c`):

- Each row has `category` (positive / negative / boundary / corner), `description` and `expected`, and is printed as `ok` or `FAIL <category>: <description>; expected: <expected>`.
- The binary exits non-zero on any failure.
- Tests are registered in `tests/CMakeLists.txt` with `${ERNIC_UNIT_TEST_SAN}`, the unit-test target list, and `add_test` / `TIMEOUT`.
- Planned tests:
  - `test_ionic_emu_stats.c`: layout pins (lif stats at 320, port stats at 576, every named qword index, reserved slots zero), MAC classification, size buckets, PCP priority, reset clears.
  - `test_ionic_stats_publish.c`: no DMA before INIT or after RESET, interval honoured, one write per block.
  - `test_ionic_queues.c`: identity queue counts, vector arithmetic against the `ionic_lif_size` rules, flow-hash steering is stable and in range.
  - `test_ionic_stats.c` (extended): the D names fit 27 chars, are unique and NONE-terminated, and their values follow the counters.
  - `test_ionic_fault.c`:
    - the parser, for both the profile and the CLI forms, with netem's percent and time units
    - exact counts for `every`, `after … count`, `burst` and `sequence`
    - golden fire sequences for `random`, `state` and `gemodel` at fixed seeds
    - the LFSR113 output matches published test vectors
    - statistical bounds over 10⁶ events: `random P` within 3σ of P; `gemodel` mean burst length ≈ 1/r; `state` stationary loss as the model predicts
    - boundaries: 0 % never fires and 100 % always fires; correlation 0 equals plain `random`
    - independence: retuning one fault leaves another's sequence unchanged
    - gates: window/start/stop/limit, with an injected clock
    - multi-fault order and `masked` counting
    - a trace written then replayed with `sequence @file` gives identical fires
    - rejections: `timeout` outside 1100–1900 ms; `hang` without `faults.allow_destructive`; an effect invalid for its site; `delay` over 900 ms
  - `test_ionic_fault_dist.c`: the `.dist` parser (good file, bad line, empty, over-long), and delay samples whose mean and σ match the table within bounds.
  - `test_ionic_adminq_stats.c`: each stats effect (`fail`, `stale`, `zero`, `reset`, `wrap`), and deferred completion for `delay` and `timeout`, including discarding a completion after the guest's cancel.
  - `test_ionic_profile.c`: each built-in profile parses and validates; layering and CLI precedence; unknown key / out-of-range / bad type / duplicate or over-long counter name each fail with file, line and key; `--print-profile` output re-parses to the same merged profile (round trip); `compat` reproduces today's identity byte for byte.

**Regression proofs.** Each fix lists at least one row that fails with the fix reverted. These are recorded in STATUS.md.

**Guest checks.** Through `nix run .#run-ionic-guest-test` and `.#run-ionic-pair-test`:

- After N pings, `ip -s link` rx/tx packets grow by ≥ N.
- `ethtool -S` has every `ionic_port_stats_desc` name, and size buckets sum to `frames_rx_all`.
- `ethtool -l` matches `--eth-queues` capped by vCPUs.
- `hw_counters` holds the D names. A bad-rkey write moves `req_rx_remote_access_err` by exactly 1.
- With `--inject 'stats_vals: after 1 effect fail'`, one `hw_counters` read returns `EINVAL` (log `opcode 5 error 3`) and the next one succeeds.
- With `stats_vals: after 1 effect timeout 1500ms`, the read returns `ETIMEDOUT` after about 1 s (log `wait status -110`), the next read succeeds, and the RDMA LIF is **not** reset.
- With `flaky-hw-counters`, `fault_stats_vals_fired` equals the number of failed reads the guest saw.
- `hang` with `faults.allow_destructive = true`: confirm the 2 s watchdog LIF reset and record it (NR-E09).

**netem conformance** (a Nix VM test, `nix/tests/fault-netem-conformance.nix`):
- In a guest network namespace, send 10⁵ sequence-numbered UDP packets over a veth with `tc qdisc … netem loss <model> seed S`, and record which sequence numbers were dropped.
- Run our model with the same parameters and `seed S` over 10⁵ events.
- The two index lists must be identical, for `random` (with and without correlation), `state` and `gemodel`.
- The test runs against the kernel the flake pins. A mismatch after a kernel bump is a finding to log in STATUS, not something to silence.

**Gates** (repo CLAUDE.md):

- clang-format 18
- gcc and clang with zero warnings
- ctest under ASan/UBSan and under TSAN
- codespell
- the AMD header with no year
- the five analysis flake targets checked with `newfindings.py` against `develop`

## 13. Consumers

go-link-monitor (xtcp2) is the first consumer:

- Its ionic VM test can assert rtnetlink link stats against `ip -s link`, per-queue shape, and RDMA error counters on a real driver.
- It can also check that it skips and recovers from unreadable `hw_counters`.
- On the xtcp2 side this needs a generic `hw_counters` metric for names outside its catalog. That is tracked in xtcp2, not here.

## 14. Open questions

1. **Real Pensando `hw_counters` names.** We will ask the maintainers in the D PR. Until then §9's rule applies.
2. **Default profile.** We propose `compat` as the default, so upgrading changes nothing but the now-real stats. The alternative is `generic-100g`, which is more realistic but changes what existing guests see (4 → 16 queues).
3. **`IONIC_DEV_CAP_EXTRA_STATS`.** We propose off in every built-in profile, available as `stats.extra_stats`.
4. **Stats interval.** We propose a default of 1000 ms. Real firmware rates vary, and nothing in the driver depends on the value.
5. **Profile format.** We propose ini via `GKeyFile`, with no new dependency. JSON would need a new library or a hand-written parser.
6. **Vendor profiles.** Do the maintainers have, or want to accept, calibrated `pensando-<model>` profiles (§11.3)?
7. **netem compatibility.** Are the maintainers happy with a clean reimplementation that matches kernel netem's PRNG and seed handling exactly (§10.8)? The alternative is a different generator, which keeps the statistics but loses `seed` equality with netem. Would they rather vendor the public-domain `.dist` tables or read them at runtime?
