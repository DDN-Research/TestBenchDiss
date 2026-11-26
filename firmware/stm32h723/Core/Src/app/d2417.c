/**
 * @file  d2417.c
 * @brief Cyclic polling of RS-485 nodes (protocol D2417/2, stand profile).
 *
 * Algorithm (see Fig. "RS-485 polling" in the docs):
 *   for node i = 1..N: DE=1, send request; DE=0, wait answer <= 100 ms;
 *   check start byte, network ID, address, length, control byte;
 *   store values or increase the error counter of the node.
 */
#include "app/d2417.h"
#include "app/bsp.h"
#include "app/params.h"
#include "app/sensors.h"
#include "app/proto.h"
#include <string.h>

d2417_node_t g_node[D2417_MAX_NODES];

#define ANSWER_TIMEOUT_MS  100u
#define POLL_PERIOD_MS    2000u
#define ERR_LIMIT            3u

typedef enum { ST_IDLE, ST_WAIT } st_t;
static st_t     s_state;
static uint8_t  s_cur;          /* node index 0..N-1 */
static uint32_t s_t0, s_cycle_t0;
static uint8_t  s_rx[8 + D2417_MAX_BODY];
static uint16_t s_rxn;
static uint32_t s_baud;

uint16_t d2417_build(uint8_t *o, uint8_t start, uint8_t dev, uint16_t id,
                     const uint8_t *body, uint16_t blen)
{
    uint16_t n = 0, len = (uint16_t)(7u + blen);   /* A..F */
    o[n++] = start; o[n++] = D2417_NET_ID; o[n++] = dev;
    o[n++] = (uint8_t)(len >> 8); o[n++] = (uint8_t)len;
    o[n++] = (uint8_t)(id >> 8);  o[n++] = (uint8_t)id;
    memcpy(&o[n], body, blen); n = (uint16_t)(n + blen);
    uint8_t x = 0;
    for (uint16_t i = 0; i < n; i++) x ^= o[i];
    o[n++] = x;
    return n;
}

int d2417_parse(const uint8_t *f, uint16_t len, uint8_t *dev, uint16_t *id,
                const uint8_t **body)
{
    if (len < 8u || f[1] != D2417_NET_ID) return -1;
    uint16_t l = (uint16_t)(f[3] << 8 | f[4]);
    if (l + 1u != len || l < 7u) return -1;
    uint8_t x = 0;
    for (uint16_t i = 0; i < l; i++) x ^= f[i];
    if (x != f[l]) return -1;
    *dev = f[2]; *id = (uint16_t)(f[5] << 8 | f[6]); *body = &f[7];
    return (int)(l - 7u);
}

void d2417_init(void)
{
    memset(g_node, 0, sizeof g_node);
    s_state = ST_IDLE; s_cur = 0; s_cycle_t0 = bsp_millis();
    s_baud = (uint32_t)par(P_RS485_BAUD);
    bsp_rs485_config(s_baud);
}

static void store_answer(d2417_node_t *nd, const uint8_t *b, int bl)
{
    nd->type = b[0];
    nd->n = 0;
    /* body after type: [VH][VL][ST] repeated (TS.., MT03, TH01, CO2) */
    for (int p = 1; p + 2 < bl && nd->n < 8; p += 3) {
        nd->val[nd->n] = (int16_t)(b[p] << 8 | b[p + 1]);
        nd->st[nd->n]  = b[p + 2];
        nd->n++;
    }
    if (nd->type == DT_CO2 && nd->n > 0 && nd->st[0] == 1)
        g_meas.co2_ppm = (float)nd->val[0];
}

static void send_request(void)
{
    uint8_t f[16], body[1] = { D2417_CMD_READ };
    uint16_t n = d2417_build(f, D2417_START_REQ, (uint8_t)(s_cur + 1u), 0x0000, body, 1);
    while (bsp_rs485_getc() >= 0) { }          /* flush */
    bsp_rs485_send(f, n);
    proto_bus("485", "TX", f, n);
    s_rxn = 0; s_t0 = bsp_millis(); s_state = ST_WAIT;
}

void d2417_task(void)
{
    uint8_t nodes = (uint8_t)par(P_RS485_NODES);
    if ((uint32_t)par(P_RS485_BAUD) != s_baud) { s_baud = (uint32_t)par(P_RS485_BAUD); bsp_rs485_config(s_baud); }
    if (nodes == 0) { s_state = ST_IDLE; return; }

    if (s_state == ST_IDLE) {
        if (s_cur == 0 && bsp_millis() - s_cycle_t0 < POLL_PERIOD_MS) return;
        if (s_cur == 0) s_cycle_t0 = bsp_millis();
        send_request();
        return;
    }
    /* ST_WAIT: collect bytes */
    int c;
    while ((c = bsp_rs485_getc()) >= 0 && s_rxn < sizeof s_rx) s_rx[s_rxn++] = (uint8_t)c;

    d2417_node_t *nd = &g_node[s_cur];
    bool done = false, good = false;
    if (s_rxn >= 5u) {
        uint16_t need = (uint16_t)((s_rx[3] << 8 | s_rx[4]) + 1u);
        if (s_rx[0] != D2417_START_ANS || need > sizeof s_rx) done = true;
        else if (s_rxn >= need) {
            uint8_t dev; uint16_t id; const uint8_t *b;
            int bl = d2417_parse(s_rx, need, &dev, &id, &b);
            proto_bus("485", "RX", s_rx, need);
            if (bl > 0 && dev == s_cur + 1u && !sensors_fault(FLT_485)) {
                nd->id = id; store_answer(nd, b, bl); good = true;
            }
            done = true;
        }
    }
    if (!done && bsp_millis() - s_t0 > ANSWER_TIMEOUT_MS) done = true;
    if (!done) return;

    if (good) { nd->ok = true; nd->errors = 0; }
    else if (++nd->errors >= ERR_LIMIT) nd->ok = false;
    s_state = ST_IDLE;
    if (++s_cur >= nodes || s_cur >= D2417_MAX_NODES) s_cur = 0;
}

bool d2417_any_error(void)
{
    uint8_t nodes = (uint8_t)par(P_RS485_NODES);
    for (uint8_t i = 0; i < nodes && i < D2417_MAX_NODES; i++)
        if (!g_node[i].ok && g_node[i].errors >= ERR_LIMIT) return true;
    return false;
}
