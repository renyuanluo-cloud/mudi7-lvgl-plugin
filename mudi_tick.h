#ifndef MUDI_TICK_H
#define MUDI_TICK_H
#include <stdint.h>
#include <time.h>
static inline uint32_t mudi_tick_ms(void){
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}
#endif
