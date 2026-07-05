#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct mbedtls_timing_hr_time {
    uint64_t start_ms;
};

typedef struct mbedtls_timing_delay_context {
    struct mbedtls_timing_hr_time timer;
    uint32_t int_ms;
    uint32_t fin_ms;
} mbedtls_timing_delay_context;

#ifdef __cplusplus
}
#endif
