/*
 * Copyright (c) 2018, ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetstrasse 6, CH-8092 Zurich. Attn: Systems Group.
 */

#ifndef QUEUE_MANAGER_H_
#define QUEUE_MANAGER_H_ 1

#include <queue_manager_client.h>

/**
 * @brief represents a device queue or channel
 */
struct device_queue
{
    ///< the type of the queue
    queue_t type;

    ///< the id of the queue
    uint64_t id;

    ///< device capability
    struct capref devcap;

    ///< capability representing the protection domain
    struct capref domcap;

    ///< the flounder binding to the device
    void *binding;

    ///< allocated flag
    bool allocated;

    ///< queue list
    struct device_queue *next;
    struct device_queue *prev;
};

errval_t dqm_queue_manager_init(void);

errval_t dqm_init_queue(struct capref dev, uint64_t id, queue_t type,
                        struct device_queue *q);


errval_t dqm_add_queue(struct device_queue *q);
errval_t dqm_alloc_by_id(queue_t type, uint64_t id, struct device_queue **q);
errval_t dqm_alloc_queue(queue_t type, struct device_queue **q);
errval_t dqm_free_queue(struct device_queue *q);

#endif /// QUEUE_MANAGER_H_