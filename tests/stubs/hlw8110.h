#ifndef TEST_SPLITAC_HLW8110_H
#define TEST_SPLITAC_HLW8110_H

#include <stdint.h>

typedef struct {
    uint8_t valid;
} HLW8110_Status_t;

const HLW8110_Status_t *HLW8110_GetStatus(void);

#endif
