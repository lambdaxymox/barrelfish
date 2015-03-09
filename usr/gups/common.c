/*
 * common.c
 *
 *  Created on: Mar 6, 2015
 *      Author: acreto
 */

#include <RandomAccess.h>
#include <numa.h>

void *HPCC_malloc(size_t bytes, size_t alignment)
{
    errval_t err;

    vregion_flags_t flags = VREGION_FLAGS_READ_WRITE;
    switch (alignment) {
        case BASE_PAGE_SIZE:
            break;
        case LARGE_PAGE_SIZE:
            flags |= VREGION_FLAGS_LARGE;
            break;
        case HUGE_PAGE_SIZE:
            if (vspace_has_hugepage_support()) {
                flags |= VREGION_FLAGS_HUGE;
            } else {
                printf("WARNING: Machine has no hugepage support\n");
                alignment = BASE_PAGE_SIZE;
            }
            break;
        default:
            USER_PANIC("Invalid alignment: %" PRIu64, alignment)
            ;
            break;
    }
    // round up bytes
    bytes = (bytes + (alignment - 1)) & ~(alignment - 1);

    uint64_t minbase, maxlimit;
    ram_get_affinity(&minbase, &maxlimit);
    if (!numa_available()) {
        uint64_t nodebase = numa_node_base(numa_current_node());
        uint64_t nodesize = numa_node_size(numa_current_node(), NULL);
        if (nodesize > bytes) {
            printf("# Setting physical range: [0x%016" PRIx64 "..0x%016" PRIx64 "]\n",
                   nodebase, nodesize+nodebase);

            ram_set_affinity(nodebase, nodebase + nodesize);
        } else {
            printf("# WARNING: current node has too less memory\n");
        }
    }



    printf("# Allocating frame of size: %" PRIu64 " with alignment %" PRIu64 "\n",
           bytes, alignment);
    struct capref frame;
    err = frame_alloc(&frame, bytes, NULL);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "Allocating the frame");
    }

    ram_set_affinity(minbase, maxlimit);

    void *vbuf;
    err = vspace_map_one_frame_attr_aligned(&vbuf, bytes, frame, flags, alignment,
                                            NULL, NULL);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "mapping the frame");
        return NULL;
    }


    return vbuf;
}

void HPCC_free(void *addr)
{
    errval_t err;

    err = vspace_unmap(addr);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "unmapping table");
    }
}

uint64_t parse_memory(char *str)
{
    size_t length = strlen(str);
    size_t shift = 0;
    switch (str[length - 1]) {
        case 'G':
        case 'g':
            shift = 30;
            str[length - 1] = 0;
            break;
        case 'M':
        case 'm':
            shift = 20;
            str[length - 1] = 0;
            break;
        case 'K':
        case 'k':
            shift = 10;
            str[length - 1] = 0;
            break;
        default:
            break;
    }

    uint64_t bytes = atol(str);
    return (bytes << shift);
}

void common_main(int argc, char *argv[], HPCC_Params *params)
{
    if (argc < 4) {
        printf("usage: %s <size> <pagesize> <numreps>\n", argv[0]);
        exit(1);
    }

    printf("# GUPS invoked as '");
    for (int i = 0; i < argc; ++i) {
        printf("%s ", argv[i]);
    }
    printf("\n");

    /* parse total mem */
    params->HPLMaxProcMem = parse_memory(argv[1]);
    params->TableAlignment = parse_memory(argv[2]);
    params->NumReps = parse_memory(argv[3]);
    bench_init();

    srand(time(NULL));
}
