#ifndef __SUPER_SPI_V2_H__
#define __SUPER_SPI_V2_H__

#include <stdint.h>

#define SSPI_PIN_NONE   0xFF

#define SSPI_CS     0
#define SSPI_DC     1
#define SSPI_R1     2
#define SSPI_R2     3

#define SSPI_CS_LOW      0x00
#define SSPI_CS_HIGH     0x01
#define SSPI_DC_LOW      0x02
#define SSPI_DC_HIGH     0x03
#define SSPI_R1_LOW      0x04
#define SSPI_R1_HIGH     0x05
#define SSPI_R2_LOW      0x06
#define SSPI_R2_HIGH     0x07
#define SSPI_SEND        0x08
#define SSPI_SEND_CS     0x09
#define SSPI_SEND_CMD    0x0A
#define SSPI_SEND_DAT    0x0B
#define SSPI_SEND_DMA    0x0C
#define SSPI_SEND_CS_DMA 0x0D
#define SSPI_SEND_DAT_DMA 0x0E
#define SSPI_PULSE_R1    0x0F

#define SSPI_MODE(spi_inst, dev_id, op)  \
    (((((spi_inst)-1) & 1) << 6) | (((dev_id) & 3) << 4) | ((op) & 0x0F))

#define SSPI_XFER        0x80
#define SSPI_XFER_INST(i) (0x80 | ((((i)-1) & 1) << 6))

typedef struct {
    void*    tx_buf;
    uint16_t tx_len;
    void*    rx_buf;
    uint16_t rx_len;
} spi_xfer_t;

#endif
