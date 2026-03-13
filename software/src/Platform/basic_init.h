#ifndef BASIC_INIT_H
#define BASIC_INIT_H
#include <stdint.h>

typedef int grendel_err_t;
#define GRENDEL_ERR_OK 0

static inline grendel_err_t disable_firewall_filters(int tile_id, uint32_t hsio_tile) { return GRENDEL_ERR_OK; }
static inline grendel_err_t disable_ker_smc_filters(void) { return GRENDEL_ERR_OK; }
static inline grendel_err_t disable_ker_smn_filters(uint32_t hsio_tile) { return GRENDEL_ERR_OK; }
static inline grendel_err_t disable_all_ker_firewall_filters(void) { return GRENDEL_ERR_OK; }

#endif
