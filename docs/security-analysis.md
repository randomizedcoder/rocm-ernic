# rocm-ernic security & stability analysis

## What this is

This is a triaged report of the security- and stability-relevant findings
from a full static-analysis sweep of the rocm-ernic userspace target. It
leads with the issues worth acting on, each validated by reading the
source, and keeps the long tail of lower-priority findings in an appendix.

The analysis was produced by the Nix analysis toolkit in this repo:

```
nix build .#analysis-deep
```

which runs seven tools over the CMake compilation database and raw
sources, then runs a triage pass that scores and de-duplicates the
results:

| Tool | Findings | What it looks for |
|---|---|---|
| clang-tidy | 3756 | broad C lint (bugprone-, cert-, readability-, misc-) |
| cppcheck | 193 | correctness, undefined behaviour, dead code |
| flawfinder | 140 | dangerous API usage patterns |
| clang-analyzer (scan-build) | 4 | path-sensitive null-deref / uninit |
| gcc-warnings | 4 | `-Wall -Wextra -Wpedantic -Wformat=2 …` |
| gcc-analyzer (`-fanalyzer`) | 1 | path-sensitive null-argument |
| semgrep | 124 | custom C hygiene rules |

Triage flagged **185 high-confidence** findings out of ~2274 in-scope;
this report is the security/stability subset of those, after manual
validation.

Scope is the **userspace** build only (`src/`). The kernel driver and the
rdma-core provider are out of scope. Both our own code (`src/rocm_ernic_*`)
and the `src/from-qemu/**` tree are in scope and treated as
security-sensitive, because that tree holds the wire-protocol parsers
(DHCP, rdma_cm, TCP, Ethernet RX) that handle untrusted input.

**A note on `src/from-qemu/`:** despite the directory name, these files
are rocm-ernic's own code (Copyright 2025 AMD) — the minimal DHCP server,
the loopback CM stub, the TCP mesh backend, and the PVRDMA device model.
They are not code carried from upstream QEMU, so findings here are ours to
fix, not to forward upstream.

**Provenance:** generated from commit `ea252a5` on branch `nix`, nixpkgs
`nixos-unstable`, analysis derivation
`/nix/store/rb0l0bahls21qq3ds5k5dyzf88zcb7hb-rocm-ernic-analysis-deep`.

## Executive summary

Nine distinct security/stability issues survived validation, grouped by
how they were found and their status:

| # | Issue | File | Severity | Status |
|---|---|---|---|---|
| 1 | OOB read in rdma_cm debug hex-dump | `rdma_cm_proto.c` | High | Fix in PR #62 (open) |
| 2 | OOB read on short DHCP request | `dhcp_server.c` | High | Fix in PR #63 (open) |
| 3 | DHCP server leaked at shutdown | `rocm_ernic_compat.c` | Low | Fix in PR #64 (open) |
| 4 | Leased-IP leak (NULL value destructor) | `dhcp_server.c` | Low | Fix in PR #64 (open) |
| 5 | NULL-deref in TCP/UDP checksum on empty payload | `net_headers.h` | Medium | Open |
| 6 | NULL-deref in verbs query_port fallback | `rdma_backend.c` | Medium | Open |
| 7 | Signed-shift-by-31 undefined behaviour | `rocm_ernic_eth.h` + uses | Low | Open |
| 8 | Dead-code guards in loopback SGE copy | `rdma_backend_loopback.c` | Low | Open |
| 9 | Unchecked I/O return values (theme) | many | Low | Open (backlog) |

Issues 1–4 are the memory-safety bugs found earlier via the fuzz/sanitizer
harnesses; fixes are already up as PRs #62–#64. They are worth restating
here because **those PRs are not yet merged into this branch**, so the
bugs are still live in the code as it stands, and because static analysis
did **not** independently catch them — a useful reminder that the static
and dynamic passes cover different ground.

Issues 5–9 are newly surfaced by this static-analysis sweep and are not
yet addressed anywhere.

Four notable tool reports were validated as **false positives** and are
documented, with reasons, in the appendix — they need no code change.

## Security & stability findings

### 1. Out-of-bounds read in the rdma_cm debug hex-dump (High)

`src/from-qemu/utils/rdma_cm_proto.c:114`
— reachable from untrusted wire data (loopback / TCP-mesh CM path).

`rdma_cm_process_message()` unconditionally reads the first 32 bytes of
the payload for a debug log once `payload_len >= 4`:

```c
rdma_info_report("rdma_cm: Message bytes (first 32): ...",
    ((const uint8_t *)tcp_payload)[0], ... ((const uint8_t *)tcp_payload)[31]);
```

For a payload of 4..31 bytes this reads past the end of the buffer,
crashing the server or leaking adjacent memory into the log. Not flagged
by static analysis (fixed-count reads defeat bounds inference); found by
fuzzing.

**Fix:** bound the dump to `min(payload_len, 32)` and format with
`snprintf`. Proposed in **PR #62** (with a table-driven ASan test).

### 2. Out-of-bounds read on short DHCP requests (High)

`src/from-qemu/utils/dhcp_server.c` (`dhcp_server_process`, from line 199)
— reachable from untrusted DHCP packets on the loopback / TCP-manager path.

`dhcp_server_process()` and `dhcp_find_option()` treat the request as a
full, fixed-size `struct dhcp_packet` (including the 312-byte `options`
array) but never check `request_len`. A request shorter than the struct,
down to zero bytes, is read past its end — first in the options debug
dump, then in the option scan.

**Fix:** reject `request_len < sizeof(struct dhcp_packet)` up front.
Proposed in **PR #63** (with a table-driven ASan test that still proves a
well-formed DISCOVER yields an OFFER).

### 3. DHCP server never freed at shutdown (Low)

`src/rocm_ernic_compat.c` (`pvrdma_device_destroy`)

`pvrdma_device_realize()` creates a `DhcpServer` (loopback and
TCP-manager modes), but `pvrdma_device_destroy()` frees every other
member and then `free(pvrdma)` without ever freeing `pvrdma->dhcp_server`.
The whole server leaks at teardown.

**Fix:** call `dhcp_server_destroy()` in the teardown path. Proposed in
**PR #64**.

### 4. Leased IPs never freed (Low)

`src/from-qemu/utils/dhcp_server.c:46`

`dhcp_server_create()` builds the `allocations` table with a NULL value
destructor:

```c
server->allocations = g_hash_table_new_full(mac_hash, mac_equal, g_free, NULL);
server->leases      = g_hash_table_new_full(mac_hash, mac_equal, g_free, g_free);
```

so the heap-allocated IP copy stored for each lease leaks even when the
table is destroyed — note the `leases` table right below already uses
`g_free` for its value.

**Fix:** use `g_free` for the `allocations` value too. Proposed in **PR
#64** (bundled with issue 3, one table-driven ASan+LSan lifecycle test).

### 5. NULL-deref in TCP/UDP checksum on an empty payload (Medium)

`src/from-qemu/utils/net_headers.h:305` (`tcp_checksum`) and the identical
pattern in `udp_checksum` (~line 255).
Cross-tool: **clang-analyzer** `core.NullDereference` and **cppcheck**
`nullPointerRedundantCheck` both flag `net_headers.h:313`.

The checksum helpers dereference `payload` without a null guard:

```c
const uint16_t *payload_words = (const uint16_t *)payload;
for (i = 0; i < payload_len / 2; i++)
    sum += ntohs(payload_words[i]);
if (payload_len % 2)
    sum += ((uint8_t *)payload)[payload_len - 1] << 8;
```

The caller in `tcp_conn.c:276` guards the payload **copy** with
`if (payload && payload_len > 0)` but calls `tcp_checksum(..., payload,
payload_len)` unconditionally. A caller passing `payload == NULL` with
`payload_len > 0` therefore dereferences NULL inside the loop. Today's
in-tree callers keep `payload` and `payload_len` consistent, so this is a
latent defensive-hardening bug rather than a live crash — but two
independent tools agree on the site, and the fix is trivial.

**Fix:** guard the payload accumulation with `if (payload)` (or treat a
NULL payload as `payload_len = 0`) in both `tcp_checksum` and
`udp_checksum`.

### 6. NULL-deref in the verbs `query_port` fallback (Medium)

`src/from-qemu/hw/rdma/rdma_backend.c:362`
— **clang-analyzer** `core.NullDereference`.

`rdma_backend_query_port()` guards the backend-ops path but not the verbs
fallback:

```c
if (backend_dev && backend_dev->backend_ops &&
    backend_dev->backend_ops->query_port) {
    ...
    return 0;
}
/* Verbs backend fallback */
rc = ibv_query_port(backend_dev->context, backend_dev->port_num, port_attr);
```

If `backend_dev` is NULL, the first `if` short-circuits to false and the
fallback dereferences `backend_dev->context` — a NULL deref. The
function's own null-check at the top establishes that `backend_dev` may be
NULL, so the fallback should not assume otherwise.

**Fix:** add an early `if (!backend_dev) return -EINVAL;` (or fold the
null-check into the fallback path).

### 7. Signed-shift-by-31 undefined behaviour (Low)

`src/rocm_ernic_eth.h:38` (root) and use sites
`src/from-qemu/hw/rdma/vmw/pvrdma_eth.c:125,132`,
`pvrdma_main.c:916,928,948,969,988`,
`include/qemu-extra/.../pvrdma_dev_api.h:360`.
— **cppcheck** `shiftTooManyBitsSigned` (error severity).

Our own reset-bit macro is defined as a signed shift:

```c
#define ROCM_ERNIC_ETH_CTL_RESET (1 << 31) /* Software Reset */
```

`1 << 31` shifts into the sign bit of a signed `int`, which is undefined
behaviour in C. It works on today's compilers/targets but is a real
portability/correctness defect. The `pvrdma_*` use sites flagged are the
same class, rooted in mask macros from the imported PVRDMA uAPI headers.

**Fix:** make the shift unsigned — `(1U << 31)` — in
`ROCM_ERNIC_ETH_CTL_RESET` (ours to fix directly) and, where practical, in
the uAPI mask definitions.

### 8. Dead-code guards in the loopback SGE copy (Low)

`src/from-qemu/hw/rdma/rdma_backend_loopback.c:369` and the mirror sites
`:509`, `:606`.
— **cppcheck** `oppositeInnerCondition` ("opposite inner condition leads
to a dead code block").

The SGE-copy loop is bounded by `while (src_idx < num_src_sge && ...)`,
then re-checks `if (src_idx >= num_src_sge) break;` without `src_idx`
having changed since the loop condition — so the inner guard can never
fire. Harmless at runtime, but it signals confused control flow and
should be removed or corrected (e.g. if the intent was to re-check after
an increment).

**Fix:** delete the redundant guards, or move the index increment so the
re-check is meaningful.

### 9. Unchecked I/O return values — a hardening theme (Low)

Not a single bug but a recurring pattern worth a pass:

- `cert-err33-c` — **146** findings (clang-tidy): return values of
  functions like `snprintf`, `write`, and `read` used without checking.
- `nix.store.getenv-unchecked` / `nix.store.atoi-atol-usage` (semgrep):
  `getenv()` results used without a NULL check and `atoi()` used without
  error handling, notably in `rdma_backend_tcp.c` env parsing.

Individually low-risk, but tightening the network- and config-parsing
paths (checking short writes, validating parsed integers) would remove a
class of silent-failure and misconfiguration bugs. Best handled as a
focused cleanup rather than one-off fixes.

## Appendix A — validated false positives

These tool reports were checked against the source and need **no** code
change:

- **`rocm_ernic_server.c:668` — `parse_verbs_options` NULL `backend_str`**
  (gcc-analyzer `-Wanalyzer-null-argument`, CWE-476). Not reachable: the
  only caller (`validate_backend_options`) first calls
  `get_backend_type_base()`, which maps a NULL string to `"none"`, so
  `is_verbs` is false and `parse_verbs_options()` is never entered with
  NULL. (Adding a defensive null-check to match its sibling would be cheap
  but is not required.)
- **`rdma_backend_tcp.c:2133` — uninitialized `client_addr.sin_addr`**
  (clang-analyzer `core.CallAndMessage`). False positive: `client_addr`
  is filled by a successful `accept()`, and the code `continue`s on
  `sockfd < 0` before reaching `inet_ntoa()`. The analyzer cannot model
  the kernel writing through the `accept()` pointer.
- **`rdma_backend.c:497,508,1339,1361` — `memcpy` with `sizeof(pointer)`**
  (semgrep `nix.store.memcpy-sizeof-pointer`). False positive: the
  `sizeof` operand is a struct member (`msg.hdr.sgid`, a 16-byte
  `union ibv_gid`), not a pointer, and equals the size of the `.raw`
  array being copied.
- **`rdma_backend_tcp.c:3199` — duplicate `if (dgid)`** (cppcheck
  `duplicateCondition`). Two consecutive `if (dgid)` blocks (3183, 3199)
  guarding separate logical steps; benign, though they could be merged.

## Appendix B — lower-priority findings (style / convention)

The bulk of the 2274 in-scope findings are style and convention lint, not
security or stability defects. Grouped by volume:

| Category | Count | Tool | Nature |
|---|---|---|---|
| misc-include-cleaner | 437 | clang-tidy | unused/missing includes |
| readability-magic-numbers | 423 | clang-tidy | literals should be named constants |
| readability-identifier-length | 347 | clang-tidy | short identifier names |
| cert-err33-c | 146 | clang-tidy | unchecked return values (see finding 9) |
| misc-unused-parameters | 90 | clang-tidy | unused function parameters |
| bugprone-easily-swappable-parameters | 83 | clang-tidy | adjacent same-type params |
| nix.store.fprintf-stderr | 74 | semgrep | direct `fprintf(stderr, …)` logging |
| buffer.memcpy | 62 | flawfinder | `memcpy` usage (informational) |
| readability-braces-around-statements | 53 | clang-tidy | brace style |
| constParameterCallback / constVariablePointer | 49 / 33 | cppcheck | missing `const` |
| readability-isolate-declaration | 47 | clang-tidy | one declaration per statement |
| nix.store.strerror-thread-unsafe | 22 | semgrep | `strerror()` vs `strerror_r()` |

These are best treated as a formatting/cleanup backlog (many are
auto-fixable via clang-tidy `--fix`), not as part of the security work.

## How to reproduce

```
nix build .#analysis-deep          # full sweep + triage
cat result/summary.txt             # per-tool counts
cat result/triage/high-confidence.txt
cat result/triage/cross-ref.txt    # multi-tool correlations
cat result/clang-analyzer/report.txt
cat result/gcc-analyzer/report.txt
```

Per-tool targets (`nix build .#analysis-cppcheck`, `.#analysis-clang-analyzer`,
etc.) and the dynamic passes (`.#analysis-sanitizers`, `.#analysis-valgrind`,
`.#fuzzers`) are documented in `nix/README.md`.
