#ifndef XMODEM_SERVER_H
#define XMODEM_SERVER_H

#include <stdint.h>

#define XMODEM_MAX_PACKET_SIZE 128

#define XMODEM_SOH 0x01
#define XMODEM_EOT 0x04
#define XMODEM_ACK 0x06
#define XMODEM_NAK 0x15
#define XMODEM_CAN 0x18
#define XMODEM_CRC 0x43

typedef struct xmodem_server xmodem_server;

typedef void (*xmodem_tx_byte_t)(xmodem_server* xdm, uint8_t byte, void* cb_data);

struct xmodem_server {
    uint8_t  buf[128];
    uint8_t  seq;
    uint8_t  expected_seq;
    uint16_t crc;
    int      pos;
    int      state;
    int      done;
    int      retries;
    int      packet_ready;
    xmodem_tx_byte_t tx_byte;
    void*    cb_data;
};

void xmodem_server_init(xmodem_server* xdm, xmodem_tx_byte_t tx_byte, void* cb_data);
void xmodem_server_rx_byte(xmodem_server* xdm, uint8_t byte);
int  xmodem_server_process(xmodem_server* xdm, uint8_t* pkt, uint32_t* blk, uint32_t now_ms);
int  xmodem_server_is_done(xmodem_server* xdm);

#endif
