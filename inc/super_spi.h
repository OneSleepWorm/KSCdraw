#ifndef __SUPER_SPI_H__
#define __SUPER_SPI_H__

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

#define SSPI_XFER       0x10

#define SSPI_MODE(dev_id, op)  (((dev_id) << 4) | (op))

#endif
