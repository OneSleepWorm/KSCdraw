#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <stdint.h>

/* super_spi: tx command dynamic parameters */
typedef struct {
    uint16_t len;       /* number of bytes to send/receive */
} sspi_mode_t;

#endif
