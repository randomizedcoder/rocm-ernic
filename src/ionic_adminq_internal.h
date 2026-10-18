/*
 * Private admin queue layouts and statistics metadata shared with tests.
 *
 * Copyright (C) Advanced Micro Devices, Inc.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef IONIC_ADMINQ_INTERNAL_H
#define IONIC_ADMINQ_INTERNAL_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "ionic_adminq.h"
#include "ionic_datapath.h"
#include "rocm_ernic_compat.h"

/* -------------------------------------------------------------------------
 * ionic_fw.h constants (keep in sync with pinned kernel ref)
 * -------------------------------------------------------------------------
 */

/* Admin WQE stride and header length */
#define ADMIN_WQE_STRIDE  64
#define ADMIN_WQE_HDR_LEN 4

/* Admin opcodes (enum ionic_v1_admin_op) */
enum ionic_v1_admin_op {
    IONIC_V1_ADMIN_NOOP = 0,
    IONIC_V1_ADMIN_CREATE_CQ = 1,
    IONIC_V1_ADMIN_CREATE_QP = 2,
    IONIC_V1_ADMIN_CREATE_MR = 3,
    IONIC_V1_ADMIN_STATS_HDRS = 4,
    IONIC_V1_ADMIN_STATS_VALS = 5,
    IONIC_V1_ADMIN_DESTROY_MR = 6,
    /* 7 reserved */
    IONIC_V1_ADMIN_DESTROY_CQ = 8,
    IONIC_V1_ADMIN_MODIFY_QP = 9,
    IONIC_V1_ADMIN_QUERY_QP = 10,
    IONIC_V1_ADMIN_DESTROY_QP = 11,
    IONIC_V1_ADMIN_DEBUG = 12,
    IONIC_V1_ADMIN_CREATE_AH = 13,
    IONIC_V1_ADMIN_QUERY_AH = 14,
    IONIC_V1_ADMIN_MODIFY_DCQCN = 15,
    IONIC_V1_ADMIN_DESTROY_AH = 16,
    IONIC_V1_ADMIN_QP_STATS_HDRS = 17,
    IONIC_V1_ADMIN_QP_STATS_VALS = 18,
    /* 19-25 reserved upstream for series in flight (RCQ and friends); the
     * SRQ opcodes were assigned explicitly at 26 and must keep those
     * numbers. See enum ionic_v1_admin_op in the kernel's ionic_fw.h. */
    IONIC_V1_ADMIN_CREATE_SRQ = 26,
    IONIC_V1_ADMIN_MODIFY_SRQ = 27,
    IONIC_V1_ADMIN_QUERY_SRQ = 28,
    IONIC_V1_ADMIN_DESTROY_SRQ = 29,
};

/*
 * ionic_v1_admin_wqe header (4 bytes):
 *   u8  op
 *   u8  rsvd
 *   le16 len   (total byte length of cmd body)
 *
 * Followed by the opcode-specific command body.  A WQE may span multiple
 * 64-byte strides if len+4 > 64.
 *
 * ionic_v1_cqe (32 bytes, big-endian):
 *   For admin CQE type (IONIC_V1_CQE_TYPE_ADMIN = 0):
 *     le16 cmd_idx     — AQ consumer index this completes
 *     u8   cmd_op
 *     u8   rsvd[17]
 *     le16 old_sq_cindex
 *     le16 old_rq_cq_cindex
 *   Followed by (at byte 28):
 *     be32 status_length   (status in high byte when error bit set)
 *     be32 qid_type_flags
 *       bit 0:  color
 *       bit 1:  error
 *       bits[7:5]: type  (0 = admin)
 *       bits[31:8]: qid
 */
#define CQE_SIZE 32

/* Color bit in qid_type_flags (big-endian byte [3] bit 0). */
#define CQE_COLOR_BIT 0x01u
#define CQE_ERROR_BIT 0x02u
/* Type field bits[7:5] = 0 means admin CQE; we always write 0 there. */

/* -------------------------------------------------------------------------
 * Per-AQ ring state
 * -------------------------------------------------------------------------
 */
#define MAX_AQ     4
#define MAX_CQ_MAP 256
#define MAX_QP_MAP 256
/* Tied to the advertised ceiling, not merely equal to it today: max_srq comes
 * from IONIC_EMU_SRQ_COUNT, and a map smaller than that silently loses the
 * mappings for every SRQ past the end. */
#define MAX_SRQ_MAP IONIC_EMU_SRQ_COUNT
/*
 * One entry per region the rdma_rm table can hold.  At 256 against a
 * 1024-entry table, every MR past the 256th was created successfully and then
 * had no map entry, so DESTROY_MR could not resolve its handle and silently
 * dropped it -- leaking the resource for the lifetime of the process.  An
 * nvme-rdma connect allocates 128 regions per queue, so that ceiling was
 * reached two queues in.
 */
#define MAX_MR_MAP IONIC_MAX_MR

/* enum ionic_mrf_bits: the low 12 bits are IB access flags. */
#define IONIC_MRF_ACCESS_MASK 0x0fffu

struct ionic_aq_ring {
    bool valid;

    /* AQ WQE ring */
    uint64_t aq_dma;   /* guest PA of WQE ring */
    uint32_t aq_depth; /* number of entries (2^depth_log2) */
    uint32_t aq_cons;  /* our consumer index (next WQE to read) */
    uint32_t aq_prod;  /* last producer index from doorbell write */

    /* Admin CQ ring */
    uint64_t cq_dma;   /* guest PA of CQE ring */
    uint32_t cq_depth; /* number of entries */
    uint32_t cq_prod;  /* our producer index */
    bool cq_color;     /* current color (flips on ring wrap) */
    uint32_t aq_id;    /* AQ id the driver knows this ring by */
    uint32_t cq_id;    /* CQ id the driver knows this ring by */
    uint32_t eq_id;    /* EQ that carries this CQ's notifications */
};

struct ionic_adminq_ctx {
    vfu_ctx_t *vfu_ctx;
    struct ionic_aq_ring aq[MAX_AQ];
    int aq_count;

    /* pvrdma handle for calling ionic_rm_* compat wrappers. */
    pvrdma_handle_t pvrdma_handle;

    /* Bytes a handler DMA'd into the command's response buffer, for the
     * completion's status_length. Reset before every dispatch; only the
     * handlers that write a response set it. */
    uint32_t resp_len;

    /* Raises an EQ event once a CQE has been written. */
    ionic_adminq_cq_event_fn_t cq_event_fn;
    void *cq_event_opaque;

    /* The driver names CQs by its own cqid; rdma_rm hands back its own
     * handles.  CREATE_QP references CQs by cqid, so keep the translation. */
    struct ionic_cq_map_entry {
        bool valid;
        uint32_t cq_id;
        uint32_t handle;
    } cq_map[MAX_CQ_MAP];

    /* Same translation for QPs: the driver's qpid vs the backend's QPN. */
    struct ionic_qp_map_entry {
        bool valid;
        uint32_t qp_id;
        uint32_t qpn;
    } qp_map[MAX_QP_MAP];

    /* And for MRs: the driver's mrid vs the backend's handle. */
    struct ionic_mr_map_entry {
        bool valid;
        uint32_t mr_id;
        uint32_t handle;
    } mr_map[MAX_MR_MAP];

    /*
     * SRQs differ from every map above: WE pick the id, not the driver.
     * The LIF identity advertises IONIC_LIF_RDMA_ALLOC_QID_SRQ, which tells
     * the driver that firmware allocates SRQ qids, so CREATE_SRQ carries the
     * driver's qid only as a hint and takes the real one out of the DMA'd
     * response (ionic_controlpath.c: srq->rq.qid = le32_to_cpu(resp_buf->id)).
     * adminq_alloc_srq() is that allocator, and the slot index below is the
     * id: see the comment there for why it is not a counter.
     */
    struct ionic_srq_map_entry {
        bool valid;
        uint32_t srq_id;
        uint32_t handle;
        /*
         * Ours, not the resource manager's. rdma_rm_modify_srq() rejects a
         * limit of 0 outright (rdma_rm.c), but 0 is the legal value meaning
         * "no low-watermark", and the guest driver accepts it -- so routing
         * every limit through the resource manager turned a valid
         * ibv_modify_srq(srq_limit=0) into an error. The resource manager
         * never does anything with the value either way: nothing reads it
         * back except QUERY_SRQ, which now reads this.
         */
        uint32_t srq_limit;
    } srq_map[MAX_SRQ_MAP];

    /* ionic allocates protection domains entirely in the driver, so no PD
     * ever reaches us.  rdma_rm still needs one, so share a single backend PD
     * across every QP. */
    bool pd_valid;
    uint32_t pd_handle;

    /* Data path that owns the SQ/RQ/CQ rings these commands describe. */
    struct ionic_datapath *dp;
};

#define STATS_BODY_LEN  17
#define STAT_HDR_SIZE   32
#define STAT_NAME_LEN   28
#define STAT_TYPE_SHIFT 28
#define STAT_TYPE_LE64  4u
#define STAT_VAL_SIZE   8
#define STATS_MAX_COUNT 8

struct adminq_stat {
    const char *name;
    size_t off; /* of the uint64_t in the counters struct */
};

/* The names match the --stats-file labels, so the two views line up. */
static const struct adminq_stat dev_stats[] = {
    {"commands", offsetof(struct pvrdma_dev_counters, commands)},
    {"total_bytes_sent",
     offsetof(struct pvrdma_dev_counters, total_bytes_sent)},
    {"total_bytes_received",
     offsetof(struct pvrdma_dev_counters, total_bytes_received)},
    {"total_bytes_rdma_read",
     offsetof(struct pvrdma_dev_counters, total_bytes_rdma_read)},
    {"total_bytes_rdma_write",
     offsetof(struct pvrdma_dev_counters, total_bytes_rdma_write)},
};
#define DEV_STATS_COUNT (sizeof(dev_stats) / sizeof(dev_stats[0]))

static const struct adminq_stat qp_stats[] = {
    {"doorbell_send", offsetof(struct pvrdma_qp_counters, doorbell_send)},
    {"doorbell_recv", offsetof(struct pvrdma_qp_counters, doorbell_recv)},
    {"wqes_processed", offsetof(struct pvrdma_qp_counters, wqes_processed)},
    {"cqes_posted", offsetof(struct pvrdma_qp_counters, cqes_posted)},
    {"bytes_sent", offsetof(struct pvrdma_qp_counters, bytes_sent)},
    {"bytes_received", offsetof(struct pvrdma_qp_counters, bytes_received)},
    {"bytes_rdma_read", offsetof(struct pvrdma_qp_counters, bytes_rdma_read)},
    {"bytes_rdma_write", offsetof(struct pvrdma_qp_counters, bytes_rdma_write)},
};
#define QP_STATS_COUNT (sizeof(qp_stats) / sizeof(qp_stats[0]))

_Static_assert(DEV_STATS_COUNT <= STATS_MAX_COUNT, "dev_stats too long");
_Static_assert(QP_STATS_COUNT <= STATS_MAX_COUNT, "qp_stats too long");

/* Run one admin command body, as the poll loop does for a WQE, and return
 * its status. For the tests and fuzzers, which have no guest ring. */
uint8_t ionic_adminq_dispatch(struct ionic_adminq_ctx *ctx, uint8_t op,
                              const uint8_t *body, uint16_t len);

#endif
