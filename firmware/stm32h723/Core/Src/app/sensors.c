/**
 * @file  sensors.c
 * @brief Acquisition of measurements.
 *
 *  - Temperatures DS1..DS8 are read by the auxiliary MCU STM32F030 and
 *    requested over SPI using protocol M8SPI_V1.0 (DDN-ST-15/24):
 *        frame  [MODE][DATA 1..255][0x55]
 *        MODE 0x01 identification   -> answer [ANY,0x32,0x38,0x35,0x01]
 *        MODE 0x02 sensor data (32) -> [A1H,A1L..A8H,A8L,B,CH,CL,D1..D5,...]
 *        MODE 0x03 set parameters   -> [EH,EL,D1..D5,...]
 *        MODE 0x04 AM2301A (EXTENSION proposed for the stand, see docs)
 *  - Analog inputs A1..A12: divider 18k/6k8 (Kd = 3.647), MCP3208, Vref 2.495 V.
 */
#include "app/sensors.h"
#include "app/bsp.h"
#include "app/proto.h"
#include "app/params.h"
#include <string.h>

meas_t g_meas;
static bool s_fault[FLT_COUNT];
const char *const g_fault_name[FLT_COUNT] = {
    "T1", "T2", "T3", "RH", "W1", "W2", "TANK", "F030", "RS485" };

#define VREF_MV     2495.0f                 /* TL431 reference            */
#define KDIV        (24.8f / 6.8f)          /* (18k + 6k8) / 6k8          */
#define M8_LEN      32u                     /* DATA length for modes 2, 3 */

/* terminal A1..A12 -> index in raw[] (U17 CH0..7 = 0..7, U21 CH0..7 = 8..15)
 * obtained from the netlist of GreenHouse_4_1 (terminal J7 pins 15..4)   */
static const uint8_t k_ain_idx[N_AIN] = { 6, 5, 4, 7, 0, 3, 1, 2, 10, 9, 8, 11 };
#define RAW_V33  12u    /* U21 CH4 */

void sensors_init(void)
{
    memset(&g_meas, 0, sizeof g_meas);
    memset(s_fault, 0, sizeof s_fault);
}

void sensors_set_fault(fault_t f, bool on) { if (f < FLT_COUNT) s_fault[f] = on; }
bool sensors_fault(fault_t f)             { return f < FLT_COUNT && s_fault[f]; }

int sensors_find_fault(const char *name)
{
    for (int i = 0; i < FLT_COUNT; i++)
        if (strcmp(name, g_fault_name[i]) == 0) return i;
    return -1;
}

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* two-point calibration: dry -> 0 %, wet -> 100 % (voltage falls with moisture) */
static float moisture_pct(float mv, int32_t dry, int32_t wet, bool *ok)
{
    *ok = (dry != wet) && mv > 100.0f;           /* < 0.1 V: sensor disconnected */
    if (!*ok) return 0.0f;
    return clampf(((float)dry - mv) * 100.0f / (float)(dry - wet), 0.0f, 100.0f);
}

void sensors_poll_adc(void)
{
    if (!bsp_adc_read_all(g_meas.raw)) return;
    for (int i = 0; i < N_AIN; i++)
        g_meas.ain_mv[i] = (float)g_meas.raw[k_ain_idx[i]] * VREF_MV / 4096.0f * KDIV;
    g_meas.v33_mv = (float)g_meas.raw[RAW_V33] * VREF_MV / 4096.0f; /* divider: see docs */

    if (s_fault[FLT_W1]) g_meas.ain_mv[AIN_W1 - 1] = 0.0f;   /* open wire */
    if (s_fault[FLT_W2]) g_meas.ain_mv[AIN_W2 - 1] = 0.0f;

    g_meas.w[0] = moisture_pct(g_meas.ain_mv[AIN_W1 - 1], par(P_W1_DRY), par(P_W1_WET), &g_meas.w_ok[0]);
    g_meas.w[1] = moisture_pct(g_meas.ain_mv[AIN_W2 - 1], par(P_W2_DRY), par(P_W2_WET), &g_meas.w_ok[1]);
    g_meas.uv_mv     = g_meas.ain_mv[AIN_UV - 1];
    g_meas.light_pct = clampf(g_meas.ain_mv[AIN_LIGHT - 1] / 50.0f, 0.0f, 100.0f);
    /* WaterControl OUT_DATA levels: > 2 V = electrode in water */
    g_meas.lvl_min  = g_meas.ain_mv[AIN_LVLMIN - 1] > 2000.0f;
    g_meas.lvl_full = g_meas.ain_mv[AIN_LVLMAX - 1] > 2000.0f;
    if (s_fault[FLT_TANK]) { g_meas.lvl_min = false; g_meas.lvl_full = false; }
}

/* one M8SPI transaction: [mode][data...][0x55]; answer = bytes clocked
 * back during the DATA phase (to be confirmed with the F030 firmware)    */
static void m8_xfer(uint8_t mode, const uint8_t *data, uint8_t n, uint8_t *ans)
{
    uint8_t tx[2 + 255], rx[2 + 255];
    tx[0] = mode;
    if (data) memcpy(&tx[1], data, n); else memset(&tx[1], 0, n);
    tx[1 + n] = 0x55;
    bsp_m8spi_xfer(tx, rx, (uint16_t)(n + 2u));
    proto_bus("SPI", "TX", tx, (uint16_t)(n + 2u));
    proto_bus("SPI", "RX", rx, (uint16_t)(n + 2u));
    memcpy(ans, &rx[1], n);
}

/* temperature word: signed tenths of °C, valid range -25.0..+125.0 */
static bool m8_temp(uint8_t hi, uint8_t lo, float *t)
{
    int16_t v = (int16_t)((uint16_t)hi << 8 | lo);
    if (v < -250 || v > 1250) return false;
    *t = (float)v / 10.0f;
    return true;
}

void sensors_poll_f030(void)
{
    uint8_t a[M8_LEN];
    bool link = false;

    if (!s_fault[FLT_F030]) {
        uint8_t id[5];
        m8_xfer(0x01, NULL, 5, id);
        link = (id[1] == 0x32 && id[2] == 0x38 && id[3] == 0x35 && id[4] == 0x01);
    }
    g_meas.f030_ok = link;
    if (!link) {
        for (int i = 0; i < N_TEMP; i++) g_meas.t_ok[i] = false;
        g_meas.rh_ok = false;
        return;
    }
    m8_xfer(0x02, NULL, M8_LEN, a);
    for (int i = 0; i < N_TEMP; i++)
        g_meas.t_ok[i] = m8_temp(a[2 * i], a[2 * i + 1], &g_meas.t[i]);
    g_meas.f030_ac_v = a[16];
    if (s_fault[FLT_T1]) g_meas.t_ok[0] = false;
    if (s_fault[FLT_T2]) g_meas.t_ok[1] = false;
    if (s_fault[FLT_T3]) g_meas.t_ok[2] = false;

    /* AM2301A via proposed extension MODE 0x04: [RHH,RHL,TH,TL,STATUS] */
    uint8_t h[5];
    m8_xfer(0x04, NULL, 5, h);
    uint16_t rh = (uint16_t)(h[0] << 8 | h[1]);
    g_meas.rh_ok = (h[4] == 1) && rh <= 1000 && m8_temp(h[2], h[3], &g_meas.ta) && !s_fault[FLT_RH];
    if (g_meas.rh_ok) g_meas.rh = (float)rh / 10.0f;
}

void sensors_triacs_off(void)
{
    uint8_t d[M8_LEN] = {0}, a[M8_LEN];
    m8_xfer(0x03, d, M8_LEN, a);   /* EH,EL = 0; D1..D5 = 0 (off) */
}
