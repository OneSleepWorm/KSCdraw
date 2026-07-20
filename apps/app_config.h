#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <stdint.h>
#include "../inc/KSCdraw.h"   /* KSCCOLOR, ksc_obj_t */
#include "../inc/app.h"       /* app_t */

/* super_spi: tx command dynamic parameters */
typedef struct {
    uint16_t len;       /* number of bytes to send/receive */
} sspi_mode_t;

/* ================================================================
 * super_spi — constants & types
 * ================================================================ */

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

int sspi_setpin(app_t* sspi, int inst, int dev_id, int sel, int pin);

/* ================================================================
 * kscgui — tile handle
 * ================================================================ */

typedef uint8_t tile_h_t;

typedef struct {
    tile_h_t  handle;
    uint16_t  x, y, w, h;
    KSCCOLOR  bk;
    uint8_t   visible;
    uint8_t   z;
    uint8_t   is_active;
    uint16_t  obj_count;
} tile_info_t;

/* ================================================================
 * list — widget types & constants
 * ================================================================ */

typedef struct {
    uint8_t  x, y, w, h, item_h;
} list_pos_t;

typedef struct {
    KSCCOLOR sel_bg, bg, fg, sel_fg;
} list_colors_t;

#define LIST_STYLE_NONE      0
#define LIST_STYLE_FILLROW   1
#define LIST_STYLE_FILLBAR   2
#define LIST_STYLE_TEXTONLY  3
#define LIST_STYLE_ARROW     4

/* ================================================================
 * ctrl_list — event types
 * ================================================================ */

#define CTRL_EVENT_CONFIRM  0
#define CTRL_EVENT_QUIT     1

typedef void (*ctrl_event_cb_t)(void* user_data, int event);

typedef struct {
    uint8_t up;
    uint8_t down;
    uint8_t left;
    uint8_t right;
    uint8_t ok;
    uint8_t quit;
} ctrl_keymap_t;

/* ================================================================
 * transfer — XMODEM file transfer state
 * ================================================================ */

#include "../third_party/async_xmodem/xmodem_server.h"

typedef struct transfer_ctx_t {
    struct app_t*           uart;
    struct app_t*           lfs;
    struct xmodem_server    xdm;
    int                     active;
} transfer_ctx_t;

#endif
