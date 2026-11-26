/**
 * @file  d2417.h
 * @brief RS-485 master for protocol D2417/2 (D2.GreenHouse remote modules).
 *
 * Frame: [A][B][C][DD][EE][F...][G]
 *   A  start: 0x53 master->slave, 0x35 slave->master
 *   B  network ID 0x14
 *   C  device number 0x01..0xFE (0x00/0xFF broadcast)
 *   DD length from A to the last byte of F (MSB first)       (*)
 *   EE device ID (2 bytes)
 *   F  body, up to 1024 bytes
 *   G  control byte = XOR of all bytes A..F                    (*)
 * (*) byte order / checksum are fixed in this file (D2417_* macros) and
 *     must match the firmware of the remote modules.
 */
#ifndef D2417_H
#define D2417_H
#include <stdint.h>
#include <stdbool.h>

#define D2417_START_REQ  0x53u
#define D2417_START_ANS  0x35u
#define D2417_NET_ID     0x14u
#define D2417_CMD_READ   0x01u      /* stand profile: "read data" request body */
#define D2417_MAX_NODES  8u
#define D2417_MAX_BODY   64u        /* the stand needs short frames only        */

/* device type codes from the D2.GreenHouse manual (+ stand extension 0x30) */
enum { DT_TS11 = 0x01, DT_TS12 = 0x02, DT_TS41 = 0x03, DT_TS42 = 0x04,
       DT_TS81 = 0x05, DT_TS82 = 0x06, DT_MT03 = 0x07, DT_TH01 = 0x08,
       DT_CO2  = 0x30 };

typedef struct {
    bool     ok;           /* answered during the last poll       */
    uint8_t  type;         /* device type (first byte of body)    */
    uint16_t id;           /* EE field                            */
    uint8_t  n;            /* number of values                    */
    int16_t  val[8];       /* values (tenths for temperature/RH)  */
    uint8_t  st[8];        /* status per value (1 = normal)       */
    uint16_t errors;       /* consecutive missed answers          */
} d2417_node_t;

extern d2417_node_t g_node[D2417_MAX_NODES];

void     d2417_init(void);
void     d2417_task(void);            /* call every 1 ms .. 10 ms           */
uint16_t d2417_build(uint8_t *out, uint8_t start, uint8_t dev, uint16_t id,
                     const uint8_t *body, uint16_t blen);
/* parses a complete frame; returns body length or -1 */
int      d2417_parse(const uint8_t *f, uint16_t len, uint8_t *dev, uint16_t *id,
                     const uint8_t **body);
bool     d2417_any_error(void);

#endif
