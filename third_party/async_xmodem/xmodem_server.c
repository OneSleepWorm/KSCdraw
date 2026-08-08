#include "xmodem_server.h"

#define XS_CANCEL      -1
#define XS_IDLE        0
#define XS_SEQ         2
#define XS_NSEQ        3
#define XS_DATA        4
#define XS_CRC_H       5
#define XS_CRC_L       6
#define XS_DONE        7

#define MAX_RETRIES 500

static uint16_t xmodem_crc16(const uint8_t* data, int len)
{
    uint16_t crc = 0;
    for (int i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++) {
            if (crc & 0x8000)
                crc = (uint16_t)((crc << 1) ^ 0x1021);
            else
                crc <<= 1;
        }
    }
    return crc;
}

void xmodem_server_init(xmodem_server* xdm, xmodem_tx_byte_t tx_byte, void* cb_data)
{
    xdm->state = XS_IDLE;
    xdm->expected_seq = 1;
    xdm->pos = 0;
    xdm->done = 0;
    xdm->retries = 0;
    xdm->packet_ready = 0;
    xdm->tx_byte = tx_byte;
    xdm->cb_data = cb_data;
}

void xmodem_server_rx_byte(xmodem_server* xdm, uint8_t byte)
{
    switch (xdm->state) {
    case XS_IDLE:
        if (byte == XMODEM_CAN) {
            xdm->state = XS_CANCEL;
            xdm->done = 1;
            return;
        }
        if (byte == XMODEM_SOH) {
            xdm->state = XS_SEQ;
            xdm->pos = 0;
        } else if (byte == XMODEM_EOT) {
            xdm->state = XS_DONE;
            xdm->done = 1;
            /* 标准 XMODEM: 接收方收到 EOT 应回 ACK, 发送方确认后结束 */
            if (xdm->tx_byte)
                xdm->tx_byte(xdm, XMODEM_ACK, xdm->cb_data);
        }
        break;

    case XS_SEQ:
        xdm->seq = byte;
        xdm->state = XS_NSEQ;
        break;

    case XS_NSEQ:
        if ((uint8_t)(xdm->seq ^ byte) != 0xFF) {
            xdm->state = XS_IDLE;
            if (xdm->tx_byte)
                xdm->tx_byte(xdm, XMODEM_CAN, xdm->cb_data);
        } else {
            xdm->state = XS_DATA;
            xdm->pos = 0;
        }
        break;

    case XS_DATA:
        if (xdm->pos < 128)
            xdm->buf[xdm->pos++] = byte;
        if (xdm->pos >= 128)
            xdm->state = XS_CRC_H;
        break;

    case XS_CRC_H:
        xdm->crc = (uint16_t)byte << 8;
        xdm->state = XS_CRC_L;
        break;

    case XS_CRC_L:
        xdm->crc |= byte;
        xdm->state = XS_IDLE;
        xdm->packet_ready = 1;
        break;

    default:
        break;
    }
}

int xmodem_server_process(xmodem_server* xdm, uint8_t* pkt, uint32_t* blk, uint32_t now_ms)
{
    (void)now_ms;

    if (xdm->done) return 0;

    /* Process received packet before any handshake */
    if (xdm->packet_ready) {
        xdm->packet_ready = 0;
        uint16_t crc_calc = xmodem_crc16(xdm->buf, 128);
        if (crc_calc != xdm->crc) {
            if (xdm->tx_byte)
                xdm->tx_byte(xdm, XMODEM_NAK, xdm->cb_data);
            xdm->retries = 0;
            return 0;
        }
        if (xdm->seq != xdm->expected_seq) {
            if (xdm->tx_byte)
                xdm->tx_byte(xdm, XMODEM_ACK, xdm->cb_data);
            return 0;
        }
        for (int i = 0; i < 128; i++)
            pkt[i] = xdm->buf[i];
        *blk = xdm->expected_seq;
        if (xdm->tx_byte)
            xdm->tx_byte(xdm, XMODEM_ACK, xdm->cb_data);
        xdm->expected_seq++;
        xdm->retries = 0;
        return 128;
    }

    if (xdm->state == XS_IDLE && xdm->retries < MAX_RETRIES) {
        if (xdm->retries == 0 || xdm->retries > 3) {
            if (xdm->tx_byte)
                xdm->tx_byte(xdm, XMODEM_CRC, xdm->cb_data);
        } else {
            if (xdm->tx_byte)
                xdm->tx_byte(xdm, XMODEM_NAK, xdm->cb_data);
        }
        xdm->retries++;
        return 0;
    }

    if (xdm->retries >= MAX_RETRIES && xdm->state == XS_IDLE) {
        xdm->done = 1;
        return -1;
    }

    return 0;
}

int xmodem_server_is_done(xmodem_server* xdm)
{
    return xdm->done;
}
