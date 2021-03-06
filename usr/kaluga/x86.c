/**
 * \file
 * \brief Device manager for Barrelfish.
 *
 * Interacts with the SKB / PCI to start cores, drivers etc.
 *
 * x86 specific startup code
 *
 */

/*
 * Copyright (c) 2007-2010, 2016 ETH Zurich.
 * Copyright (c) 2015, Hewlett Packard Enterprise Development LP.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetstrasse 6, CH-8092 Zurich. Attn: Systems Group.
 */

#include <barrelfish/barrelfish.h>
#include <skb/skb.h>
#include <pci/pci.h>
#include <acpi_client/acpi_client.h>
#include <if/monitor_blocking_defs.h>
#include "kaluga.h"

#define SERIAL_IRQ 4
#define SERIAL_BINARY "serial_pc16550d"
#define SERIAL_MODULE "serial_pc16550d"

#define LPC_TIMER_IRQ 0
#define LPC_TIMER_BINARY "lpc_timer"

static errval_t start_serial(void){
    errval_t err;
    struct module_info * mi = find_module(SERIAL_BINARY);
    if (mi != NULL) {
        // Get internal int number. COM1 uses ISA nr. 4
        struct driver_argument arg;
        err = init_driver_argument(&arg);
        arg.module_name = SERIAL_MODULE;
        if(err_is_fail(err)){
            DEBUG_ERR(err, "init_driver_argument failed\n");
            return err;
        }
        err = skb_execute_query("isa_irq_to_int(%d,N), writeln(N).", SERIAL_IRQ);
        if(err_is_fail(err)){
            DEBUG_SKB_ERR(err, "skb_execute_query");
            return err;
        }
        int int_nr;
        err = skb_read_output("%d", &int_nr);
        if(err_is_fail(err)){
            DEBUG_ERR(err, "skb_read_output");
            return err;
        }
        KALUGA_DEBUG("Found internal int number (%d) for serial.\n", int_nr);
        arg.int_arg.int_range_start = int_nr;
        arg.int_arg.int_range_end = int_nr;
        arg.int_arg.model = INT_MODEL_LEGACY;

        err = store_int_cap(int_nr, int_nr, &arg);
        if(err_is_fail(err)){
            DEBUG_ERR(err, "int_src_cap");
            return err;
        }

        // Obtain I/O  cap
        struct capref io;
        err = slot_alloc(&io);
        assert(err_is_ok(err));

        struct capref io_dst = {
            .cnode = arg.argnode_ref,
            .slot = PCIARG_SLOT_IO
        };
        struct monitor_blocking_binding *cl = get_monitor_blocking_binding();
        errval_t msg_err;
        err = cl->rpc_tx_vtbl.get_io_cap(cl, &io, &msg_err);
        assert(err_is_ok(err));
        err = cap_copy(io_dst, io);
        assert(err_is_ok(err));

        {
            char caps[1024];
            debug_print_cap_at_capref(caps, sizeof(caps), io_dst);
            debug_printf("io_dst = %s\n", caps);
        }

        err = default_start_function_pure(0, mi, "hw.legacy.uart.1 {}", &arg);
        if(err_is_fail(err)){
            USER_PANIC_ERR(err, "serial->start_function");
        }
    } else {
        printf("Kaluga: Not starting \"%s\", binary not found\n", SERIAL_BINARY);
        return KALUGA_ERR_MODULE_NOT_FOUND;
    }
    return SYS_ERR_OK;
}

static errval_t start_lpc_timer(void){
    errval_t err;
    struct module_info * mi = find_module(LPC_TIMER_BINARY);
    if (mi != NULL) {
        // Get internal int number.
        struct driver_argument arg;
        init_driver_argument(&arg);
        
        err = skb_execute_query("isa_irq_to_int(%d,N), writeln(N).", LPC_TIMER_IRQ);
        if(err_is_fail(err)){
            DEBUG_SKB_ERR(err, "skb_execute");
            return err;
        }
        int int_nr;
        err = skb_read_output("%d", &int_nr);
        if(err_is_fail(err)){
            DEBUG_ERR(err, "skb_read_output");
            return err;
        }
        KALUGA_DEBUG("Found internal int number (%d) for lpc_timer.\n", int_nr);
        arg.int_arg.int_range_start = int_nr;
        arg.int_arg.int_range_end = int_nr;
        arg.int_arg.model = INT_MODEL_LEGACY;

        err = store_int_cap(int_nr, int_nr, &arg);
        if(err_is_fail(err)){
            DEBUG_ERR(err, "store_int_cap");
            return err;
        }


        err = mi->start_function(0, mi, "hw.legacy.timer.1 {}", &arg);
        if(err_is_fail(err)){
            USER_PANIC_ERR(err, "serial->start_function");
        }
    } else {
        printf("Kaluga: Not starting \"%s\", binary not found\n", LPC_TIMER_BINARY);
        return KALUGA_ERR_MODULE_NOT_FOUND;
    }
    return SYS_ERR_OK;
}

errval_t arch_startup(char * add_device_db_file)
{
    errval_t err = SYS_ERR_OK;
    // We need to run on core 0
    // (we are responsible for booting all the other cores)
    assert(my_core_id == BSP_CORE_ID);
    KALUGA_DEBUG("Kaluga running on x86.\n");


    err = skb_client_connect();
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "Connect to SKB.");
    }

    // Make sure the driver db is loaded
    err = skb_execute("[device_db].");
    if (err_is_fail(err)) {
        USER_PANIC_SKB_ERR(err, "Device DB not loaded.");
    }
    if(add_device_db_file != NULL){
        err = skb_execute_query("[%s].", add_device_db_file);
        if(err_is_fail(err)){
            USER_PANIC_SKB_ERR(err,"Additional device db file %s not loaded.", add_device_db_file);
        }
    }

    // The current boot protocol needs us to have
    // knowledge about how many CPUs are available at boot
    // time in order to start-up properly.
    char* record = NULL;
    err = oct_barrier_enter("barrier.acpi", &record, 2);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "Could not wait for ACPI Barrier '%s'\n",
                       "barrier.acpi");
    }
    if (record) {
        free(record);
    }

    KALUGA_DEBUG("Kaluga: watch_for_cores\n");

    err = watch_for_cores();
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "Watching cores.");
    }

    KALUGA_DEBUG("Kaluga: wait_for_all_spawnds\n");

    err = wait_for_all_spawnds(1);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "Unable to wait for spawnds failed.");
    }

    KALUGA_DEBUG("Kaluga: ACPI connect...\n");
    err = connect_to_acpi();
    if (err_is_fail(err) && err != KALUGA_ERR_MODULE_NOT_FOUND) {
        USER_PANIC_ERR(err, "start_lpc_timer");
    }

    err = watch_for_iommu();
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "Watching for IOMMUS.");
    }

    KALUGA_DEBUG("Kaluga: pci_root_bridge\n");

    err = watch_for_pci_root_bridge();
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "Watching PCI root bridges.");
    }

    KALUGA_DEBUG("Kaluga: int_controller_devices\n");

    /* The IOMMU needs to have knowledge of the PCI Bridges and devices. */

    record = NULL;
    debug_printf("barrier.pci.bridges\n");
    err = oct_barrier_enter("barrier.pci.bridges", &record, 3);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "Could not wait for PCI Barrier 'barrier.pci.bridges'\n");
    }

    if (record) {
        free(record);
    }

    err = watch_for_int_controller();
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "Watching interrupt controllers.");
    }

    KALUGA_DEBUG("Kaluga: wait_for_all_spawnds\n");

    if(0){ // Disable HPET for now.
        err = watch_for_hpet();
        if (err_is_fail(err)) {
            USER_PANIC_ERR(err, "Unable to watch for hpet");
        }
    }

    err = wait_for_all_spawnds(1);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "Unable to wait for spawnds failed.");
    }


    KALUGA_DEBUG("Kaluga: pci_devices\n");

    err = watch_for_pci_devices();
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "Watching PCI devices.");
    }

    KALUGA_DEBUG("Kaluga: Starting serial...\n");
    err = start_serial();
    if (err_is_fail(err) && err != KALUGA_ERR_MODULE_NOT_FOUND) {
        USER_PANIC_ERR(err, "start_serial");
    }

    KALUGA_DEBUG("Kaluga: Starting lpc timer...\n");
    err = start_lpc_timer();
    if (err_is_fail(err) && err != KALUGA_ERR_MODULE_NOT_FOUND) {
        USER_PANIC_ERR(err, "start_lpc_timer");
    }

    
    

    return SYS_ERR_OK;
}
