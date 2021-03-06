/**
 * \file
 * \brief Benchmark revoke of cap with no remote relations
 */

/*
 * Copyright (c) 2017, ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetstr 6, CH-8092 Zurich. Attn: Systems Group.
 */

#include <barrelfish/barrelfish.h>
#include <if/bench_distops_defs.h>

#include <bitmacros.h>

#include <bench/bench.h>
#include <trace/trace.h>

#include "benchapi.h"

#define REVOKE_COPIES 10

//{{{1 debugging helpers
static void debug_capref(const char *prefix, struct capref cap)
{
    char buf[128];
    debug_print_capref(buf, 128, cap);
    printf("%s capref = %s\n", prefix, buf);
}

//{{{1 shared commands
enum bench_cmd {
    BENCH_CMD_CREATE_COPIES,
    BENCH_CMD_COPIES_DONE,
    BENCH_CMD_DO_REVOKE,
    BENCH_CMD_PRINT_STATS,
    BENCH_CMD_PRINT_DONE,
};

//{{{1 Managment node: implement orchestration for benchmark

//{{{2 Management node: state management

struct global_state {
    struct capref ram;
    coreid_t *nodes;
    int nodes_seen;
    int nodecount;
    int copies_done;
    int printnode;
    int currcopies;
};

errval_t mgmt_init_benchmark(void **st, int nodecount)
{
     *st = calloc(1, sizeof(struct global_state));
     if (!*st) {
         return LIB_ERR_MALLOC_FAIL;
     }
     struct global_state *gs = *st;
     gs->nodes = calloc(nodecount, sizeof(coreid_t));
     gs->nodecount = nodecount;
     gs->copies_done = 0;
     gs->printnode = 0;
     return ram_alloc(&gs->ram, BASE_PAGE_BITS);
}

static int sort_coreid(const void *a_, const void *b_)
{
    // deref pointers as coreids, store as ints
    int a = *((coreid_t*)a_);
    int b = *((coreid_t*)b_);
    // subtract as ints
    return a-b;
}

void mgmt_register_node(void *st, coreid_t nodeid)
{
    struct global_state *gs = st;
    gs->nodes[gs->nodes_seen++] = nodeid;
    // if we've seen all nodes, sort nodes array and configure printnode
    if (gs->nodes_seen == gs->nodecount) {
        qsort(gs->nodes, gs->nodecount, sizeof(coreid_t), sort_coreid);
    }
}

struct mgmt_node_state {
};

errval_t mgmt_init_node(void **st)
{
     *st = malloc(sizeof(struct mgmt_node_state));
     if (!*st) {
         return LIB_ERR_MALLOC_FAIL;
     }
    return SYS_ERR_OK;
}

//{{{2 Management node: benchmark impl
void mgmt_run_benchmark(void *st)
{
    struct global_state *gs = st;

    printf("All clients sent hello! Benchmark starting...\n");

    printf("# Benchmarking REVOKE NO REMOTE: nodes=%d\n", gs->nodecount);

    printf("# Starting out with %d copies, will double up to %d...\n",
            NUM_COPIES_START, NUM_COPIES_END);

    TRACE(CAPOPS, START, 0);

    gs->currcopies = NUM_COPIES_START;
    broadcast_caps(BENCH_CMD_CREATE_COPIES, NUM_COPIES_START, gs->ram);
}

void mgmt_cmd(uint32_t cmd, uint32_t arg, struct bench_distops_binding *b)
{
    struct global_state *gs = get_global_state(b);
    switch(cmd) {
        case BENCH_CMD_COPIES_DONE:
            gs->copies_done++;
            if (gs->copies_done == gs->nodecount) {
                printf("# All copies made!\n");
                broadcast_cmd(BENCH_CMD_DO_REVOKE, ITERS);
                printf("# sending initial print command to %d\n", gs->printnode);
                unicast_cmd(gs->nodes[gs->printnode++], BENCH_CMD_PRINT_STATS, 0);
            }
            break;
        case BENCH_CMD_PRINT_DONE:
            if (gs->printnode == gs->nodecount) {
                if (gs->currcopies == NUM_COPIES_END) {
                    printf("# Benchmark done!\n");
                    TRACE(CAPOPS, STOP, 0);
                    mgmt_trace_flush(NOP_CONT);
                    return;
                }
                printf("# Round done!\n");
                // Reset counters for next round
                gs->currcopies *= 2;
                gs->copies_done = 0;
                gs->printnode = 0;
                // Start new round
                broadcast_cmd(BENCH_CMD_CREATE_COPIES, gs->currcopies);
                return;
            }
            printf("# sending print command to node %d\n", gs->printnode);
            unicast_cmd(gs->nodes[gs->printnode++], BENCH_CMD_PRINT_STATS, 0);
            break;
        default:
            printf("mgmt node got unknown command %d over binding %p\n", cmd, b);
            break;
    }
}

void mgmt_cmd_caps(uint32_t cmd, uint32_t arg, struct capref cap1,
                   struct bench_distops_binding *b)
{
    printf("mgmt node got caps + command %"PRIu32", arg=%d over binding %p:\n",
            cmd, arg, b);
    debug_capref("cap1:", cap1);
}

//{{{1 Node

struct node_state {
    struct capref cap;
    struct capref ram;
    uint32_t numcopies;
    struct capref *copies;
    uint64_t *delcycles;
    uint32_t benchcount;
};

static coreid_t my_core_id = -1;

void init_node(struct bench_distops_binding *b)
{
    printf("%s: binding = %p\n", __FUNCTION__, b);

    my_core_id = disp_get_core_id();

    bench_init();

    // Allocate client state struct
    b->st = malloc(sizeof(struct node_state));
    assert(b->st);
    if (!b->st) {
        USER_PANIC("state malloc() in client");
    }
}

static void node_create_copies(struct node_state *ns)
{
    errval_t err;
    ns->copies = calloc(ns->numcopies, sizeof(struct capref));
    for (int i = 0; i < ns->numcopies; i++) {
        err = slot_alloc(&ns->copies[i]);
        PANIC_IF_ERR(err, "slot_alloc for copy %d\n", i);
        err = cap_copy(ns->copies[i], ns->ram);
        PANIC_IF_ERR(err, "cap_copy for copy %d\n", i);
    }
}

static size_t get_mdb_size(void)
{
    errval_t err;
    size_t cap_base_count = 0;
    err = sys_debug_get_mdb_size(&cap_base_count);
    assert(err_is_ok(err));
    return cap_base_count;
}

void node_cmd(uint32_t cmd, uint32_t arg, struct bench_distops_binding *b)
{
    struct node_state *ns = b->st;
    errval_t err;

    switch(cmd) {
        case BENCH_CMD_CREATE_COPIES:
            printf("# node %d: creating %d cap copies\n", my_core_id, arg);
            ns->numcopies = arg;
            node_create_copies(ns);
            printf("# node %d: %zu capabilities on node\n", my_core_id, get_mdb_size());
            err = bench_distops_cmd__tx(b, NOP_CONT, BENCH_CMD_COPIES_DONE, 1);
            PANIC_IF_ERR(err, "signaling cap_copy() done\n");
            break;
        case BENCH_CMD_DO_REVOKE:
            // printf("# node %d: doing %d revokes\n", my_core_id, arg);
            ns->benchcount = arg;
            ns->delcycles = calloc(arg, sizeof(uint64_t));
            assert(ns->delcycles);
            // printf("# node %d: allocated delcycles array\n", my_core_id);
            struct capref cap;
            err = ram_alloc(&cap, BASE_PAGE_BITS);
            assert(err_is_ok(err));
            // TODO: parametrize
            ns->benchcount = arg;
            // printf("# node %d: creating %d slots for copies of cap-to-revoke\n",
            //        my_core_id, REVOKE_COPIES);
            struct capref copies[REVOKE_COPIES];
            for (int c = 0; c < REVOKE_COPIES; c++) {
                err = slot_alloc(&copies[c]);
                assert(err_is_ok(err));
            }
            // printf("# node %d: starting benchmark iterations\n", my_core_id);
            for (int i = 0; i < ns->benchcount; i++) {
                uint64_t start, end;
                // Make some copies to be deleted during revoke
                // printf("# node %d: creating copies\n", my_core_id);
                for (int c = 0; c < REVOKE_COPIES; c++) {
                    err = cap_copy(copies[c], cap);
                    PANIC_IF_ERR(err, "creating copy for revoke");
                    assert(err_is_ok(err));
                }
                // printf("# node %d: doing revoke\n", my_core_id);
                start = bench_tsc();
                TRACE(CAPOPS, USER_REVOKE_CALL, (ns->numcopies << 16) | i);
                err = cap_revoke(cap);
                TRACE(CAPOPS, USER_REVOKE_RESP, (ns->numcopies << 16) | i);
                end = bench_tsc();
                ns->delcycles[i] = end - start;
                assert(err_is_ok(err));
                PANIC_IF_ERR(err, "# core %d: revoke failed", my_core_id);
                if (i % (ns->benchcount / 10) == 0) {
                    // printf("# node %d: %d percent done\n", my_core_id, i / (ns->benchcount/100));
                }
            }
            err = cap_destroy(cap);
            assert(err_is_ok(err));
            PANIC_IF_ERR(err, "# core %d: final cap_destroy", my_core_id);
            for (int c = 0; c < REVOKE_COPIES; c++) {
                err = slot_free(copies[c]);
                PANIC_IF_ERR(err, "# core %d: slot_free", my_core_id);
                assert(err_is_ok(err));
            }
            // printf("# node %d: revokes done\n", my_core_id);
            break;
        case BENCH_CMD_PRINT_STATS:
            printf("# node %d: tsc_per_us = %ld; numcopies = %d\n",
                    my_core_id, bench_tsc_per_us(), ns->numcopies);
            printf("# delete latency in cycles\n");
            for (int i = 0; i < ns->benchcount; i++) {
                printf("%ld\n", ns->delcycles[i]);
            }
            err = bench_distops_cmd__tx(b, NOP_CONT, BENCH_CMD_PRINT_DONE, 0);
            assert(err_is_ok(err));
            // Cleanup before next round
            for (int i = 0; i < ns->numcopies; i++) {
                err = cap_destroy(ns->copies[i]);
                assert(err_is_ok(err));
            }
            free(ns->copies);
            free(ns->delcycles);
            break;
        default:
            printf("node %d got command %"PRIu32"\n", my_core_id, cmd);
            break;
    }
}

void node_cmd_caps(uint32_t cmd, uint32_t arg, struct capref cap1,
                   struct bench_distops_binding *b)
{
    errval_t err;
    struct node_state *ns = b->st;

    switch (cmd) {
        case BENCH_CMD_CREATE_COPIES:
            printf("# node %d: creating %d cap copies\n", my_core_id, arg);
            ns->ram = cap1;
            ns->numcopies = arg;
            node_create_copies(ns);
            printf("# node %d: %zu caps on node\n", my_core_id, get_mdb_size());
            err = bench_distops_cmd__tx(b, NOP_CONT, BENCH_CMD_COPIES_DONE, 0);
            PANIC_IF_ERR(err, "signaling cap_copy() done\n");
            break;
        default:
            printf("node %d got caps + command %"PRIu32", arg=%d:\n",
                my_core_id, cmd, arg);
            debug_capref("cap1:", cap1);
            break;
    }
}
