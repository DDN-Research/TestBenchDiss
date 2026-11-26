/**
 * @file  proto.c
 * @brief Text command protocol "PC <-> stand" over USB CDC (virtual COM port).
 *
 *  PC -> stand : one command per line, e.g.  "SET HEAT_TSET 26.5\n"
 *  stand -> PC : lines starting with a tag:
 *     #TEL key=value;...   telemetry (period STREAM_MS)
 *     #OK  <text>          command accepted
 *     #ERR <code> <text>   command rejected
 *     #PAR name=value;min=..;max=..;def=..;unit=..
 *     #EVT <text>          event (relay switching, alarms, faults)
 *     #BUS <bus> <dir> <hex bytes>   bus monitor (MON 485|SPI 1)
 */
#include "app/proto.h"
#include "app/app.h"
#include "app/params.h"
#include "app/sensors.h"
#include "app/control.h"
#include "app/d2417.h"
#include "app/bsp.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>

#define RX_RING   512u
#define LINE_MAX  128u

static volatile uint8_t  s_ring[RX_RING];
static volatile uint16_t s_head, s_tail;
static char     s_line[LINE_MAX];
static uint16_t s_len;
static bool     s_mon_485, s_mon_spi;

void proto_init(void) { s_head = s_tail = 0; s_len = 0; s_mon_485 = s_mon_spi = false; }

void proto_rx(const uint8_t *b, uint32_t n)          /* called from USB ISR */
{
    while (n--) {
        uint16_t nx = (uint16_t)((s_head + 1u) % RX_RING);
        if (nx == s_tail) return;                      /* overflow: drop      */
        s_ring[s_head] = *b++; s_head = nx;
    }
}

void proto_send(const char *s)
{
    bsp_usb_write(s, (uint16_t)strlen(s));
    bsp_usb_write("\n", 1);
}

static void reply(const char *fmt, ...)
{
    char b[192]; va_list a; va_start(a, fmt);
    vsnprintf(b, sizeof b, fmt, a); va_end(a);
    proto_send(b);
}

void proto_event(const char *fmt, ...)
{
    char b[160]; int n = snprintf(b, sizeof b, "#EVT ");
    va_list a; va_start(a, fmt);
    vsnprintf(b + n, sizeof b - (size_t)n, fmt, a); va_end(a);
    proto_send(b);
}

void proto_bus(const char *bus, const char *dir, const uint8_t *d, uint16_t n)
{
    if ((bus[0] == '4' && !s_mon_485) || (bus[0] == 'S' && !s_mon_spi)) return;
    char b[16 + 3 * 48]; int p = snprintf(b, sizeof b, "#BUS %s %s", bus, dir);
    for (uint16_t i = 0; i < n && i < 48u; i++) p += snprintf(b + p, sizeof b - (size_t)p, " %02X", d[i]);
    proto_send(b);
}

/* ---------- telemetry ---------- */
static int put_f(char *b, int p, int sz, const char *key, bool ok, float v, int dec)
{
    if (!ok) return p + snprintf(b + p, (size_t)(sz - p), "%s=-;", key);
    return p + snprintf(b + p, (size_t)(sz - p), "%s=%.*f;", key, dec, (double)v);
}

void proto_telemetry(void)
{
    char b[640]; int p = 0, sz = (int)sizeof b; char k[8];
    p += snprintf(b + p, (size_t)(sz - p), "#TEL ms=%lu;", (unsigned long)bsp_millis());
    if (g_time_ok)
        p += snprintf(b + p, (size_t)(sz - p), "time=%04u-%02u-%02u %02u:%02u:%02u;", g_time.year,
                      g_time.month, g_time.day, g_time.hour, g_time.min, g_time.sec);
    p += snprintf(b + p, (size_t)(sz - p), "mode=%s;", g_out.mode == MODE_AUTO ? "AUTO" : "MANUAL");
    for (int i = 0; i < N_TEMP; i++) { snprintf(k, sizeof k, "T%d", i + 1); p = put_f(b, p, sz, k, g_meas.t_ok[i], g_meas.t[i], 2); }
    p = put_f(b, p, sz, "TA", g_meas.rh_ok, g_meas.ta, 1);
    p = put_f(b, p, sz, "RH", g_meas.rh_ok, g_meas.rh, 1);
    p = put_f(b, p, sz, "W1", g_meas.w_ok[0], g_meas.w[0], 1);
    p = put_f(b, p, sz, "W2", g_meas.w_ok[1], g_meas.w[1], 1);
    p = put_f(b, p, sz, "LIGHT", true, g_meas.light_pct, 1);
    p = put_f(b, p, sz, "UV", true, g_meas.uv_mv, 0);
    p = put_f(b, p, sz, "CO2", g_meas.co2_ppm > 0.0f, g_meas.co2_ppm, 0);
    p += snprintf(b + p, (size_t)(sz - p), "LVL=%d;FULL=%d;", g_meas.lvl_min, g_meas.lvl_full);
    for (int i = 0; i < N_AIN; i++)
        p += snprintf(b + p, (size_t)(sz - p), "A%d=%d;", i + 1, (int)(g_meas.ain_mv[i] + 0.5f));
    p += snprintf(b + p, (size_t)(sz - p), "K=");
    for (unsigned i = 0; i < BSP_RELAYS; i++) b[p++] = (g_out.relay >> i) & 1u ? '1' : '0';
    p += snprintf(b + p, (size_t)(sz - p), ";P1=%u;P2=%u;P3=%u;P4=%u;RGB=%06lX;WAT=%u;UVD=%.1f;F030=%d;ERR=%08lX",
                  g_out.pwm[0], g_out.pwm[1], g_out.pwm[2], g_out.pwm[3], (unsigned long)g_out.rgb,
                  g_out.wat_state, (double)g_out.uv_dose, g_meas.f030_ok, (unsigned long)g_err);
    proto_send(b);
}

/* ---------- commands ---------- */
static void send_param(int i)
{
    char v[24], mn[24], mx[24], df[24];
    const param_desc_t *d = &g_param_desc[i];
    params_format((param_id_t)i, v, sizeof v);
    /* min/max/def formatted with the same number of decimals */
    int32_t save = par((param_id_t)i);
    params_set((param_id_t)i, d->min); params_format((param_id_t)i, mn, sizeof mn);
    params_set((param_id_t)i, d->max); params_format((param_id_t)i, mx, sizeof mx);
    params_set((param_id_t)i, d->def); params_format((param_id_t)i, df, sizeof df);
    params_set((param_id_t)i, save);
    reply("#PAR %s=%s;min=%s;max=%s;def=%s;unit=%s", d->name, v, mn, mx, df, d->unit);
}

static const char *const k_help[] = {
    "PING | INFO | HELP",
    "STREAM <ms 0|100..10000>      - telemetry period",
    "MODE AUTO|MANUAL | STOP       - STOP = safe state",
    "OUT K1..K5 0|1 | PWM 1..4 <0..100> | RGB <RRGGBB> | BUZ <ms>  (MANUAL)",
    "PARS | GET <name> | SET <name> <value> | SAVE | DEFAULTS",
    "TIME [YYYY-MM-DD hh:mm:ss] | CAL W1|W2 DRY|WET | ERRCLR",
    "FAULT <T1|T2|T3|RH|W1|W2|TANK|F030|RS485> 0|1 | FAULTS",
    "MON 485|SPI 0|1 | NODES",
};

static uint32_t s_buz_off;

static void exec(char *line)
{
    char *argv[6]; int argc = 0;
    for (char *t = strtok(line, " \t"); t && argc < 6; t = strtok(NULL, " \t")) argv[argc++] = t;
    if (!argc) return;
    for (char *c = argv[0]; *c; c++) *c = (char)toupper((unsigned char)*c);
    const char *cmd = argv[0];

    if (!strcmp(cmd, "PING")) { reply("#OK PONG"); return; }
    if (!strcmp(cmd, "INFO")) {
        reply("#INFO fw=%s;ver=%s;uid=%08lX;params=%s;board=D2.GreenHouse GreenHouse_4", FW_NAME, FW_VERSION,
              (unsigned long)bsp_uid(), params_load_ok() ? "eeprom" : "defaults");
        return;
    }
    if (!strcmp(cmd, "HELP")) { for (unsigned i = 0; i < sizeof k_help / sizeof k_help[0]; i++) reply("#OK %s", k_help[i]); return; }
    if (!strcmp(cmd, "STREAM") && argc == 2) {
        int32_t v = atoi(argv[1]);
        if (!params_set(P_STREAM_MS, v) || (v > 0 && v < 100)) { reply("#ERR 2 range 0|100..10000"); return; }
        reply("#OK STREAM %ld", (long)v); return;
    }
    if (!strcmp(cmd, "MODE") && argc == 2) {
        if (!strcmp(argv[1], "AUTO"))   { g_out.mode = MODE_AUTO;   proto_event("MODE AUTO"); }
        else if (!strcmp(argv[1], "MANUAL")) { g_out.mode = MODE_MANUAL; proto_event("MODE MANUAL"); }
        else { reply("#ERR 2 AUTO|MANUAL"); return; }
        reply("#OK MODE %s", argv[1]); return;
    }
    if (!strcmp(cmd, "STOP")) { control_safe_state(); reply("#OK STOP"); return; }
    if (!strcmp(cmd, "OUT") && argc == 3 && (argv[1][0] == 'K' || argv[1][0] == 'k')) {
        const char *why; int k = atoi(argv[1] + 1);
        if (k < 1 || k > (int)BSP_RELAYS || !control_manual_relay((uint8_t)(k - 1), atoi(argv[2]) != 0, &why)) {
            reply("#ERR 3 %s", (k < 1 || k > (int)BSP_RELAYS) ? "K1..K5" : why); return;
        }
        reply("#OK OUT K%d %d", k, atoi(argv[2]) != 0); return;
    }
    if (!strcmp(cmd, "PWM") && argc == 3) {
        int ch = atoi(argv[1]), v = atoi(argv[2]);
        if (ch < 1 || ch > 4 || v < 0 || v > 100 || !control_manual_pwm((uint8_t)(ch - 1), (uint8_t)v)) {
            reply("#ERR 3 MANUAL mode, PWM 1..4 0..100"); return;
        }
        reply("#OK PWM %d %d", ch, v); return;
    }
    if (!strcmp(cmd, "RGB") && argc == 2) {
        if (g_out.mode != MODE_MANUAL) { reply("#ERR 3 not in MANUAL mode"); return; }
        g_out.rgb = (uint32_t)strtoul(argv[1], NULL, 16) & 0xFFFFFFu; reply("#OK RGB %06lX", (unsigned long)g_out.rgb); return;
    }
    if (!strcmp(cmd, "BUZ") && argc == 2) {
        uint32_t ms = (uint32_t)atoi(argv[1]); if (ms > 5000u) ms = 5000u;
        bsp_buzzer(ms > 0u); s_buz_off = bsp_millis() + ms; reply("#OK BUZ %lu", (unsigned long)ms); return;
    }
    if (!strcmp(cmd, "PARS")) { for (int i = 0; i < P_COUNT; i++) send_param(i); reply("#OK PARS %d", P_COUNT); return; }
    if ((!strcmp(cmd, "GET") || !strcmp(cmd, "SET")) && argc >= 2) {
        for (char *c = argv[1]; *c; c++) *c = (char)toupper((unsigned char)*c);
        int i = params_find(argv[1]);
        if (i < 0) { reply("#ERR 4 unknown parameter %s", argv[1]); return; }
        if (cmd[0] == 'S') {
            int32_t v;
            if (argc != 3 || !params_parse((param_id_t)i, argv[2], &v) || !params_set((param_id_t)i, v)) {
                reply("#ERR 2 value out of range"); return;
            }
            proto_event("SET %s", argv[1]);
        }
        send_param(i); return;
    }
    if (!strcmp(cmd, "SAVE"))     { reply(params_save() ? "#OK SAVE" : "#ERR 5 EEPROM write"); return; }
    if (!strcmp(cmd, "DEFAULTS")) { params_defaults(); reply("#OK DEFAULTS (not saved)"); return; }
    if (!strcmp(cmd, "TIME")) {
        if (argc == 3) {
            bsp_time_t t; unsigned y, mo, d, h, mi, s;
            if (sscanf(argv[1], "%u-%u-%u", &y, &mo, &d) != 3 || sscanf(argv[2], "%u:%u:%u", &h, &mi, &s) != 3 ||
                y < 2000 || y > 2099 || mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || s > 59) {
                reply("#ERR 2 TIME YYYY-MM-DD hh:mm:ss"); return;
            }
            t.year = (uint16_t)y; t.month = (uint8_t)mo; t.day = (uint8_t)d; t.hour = (uint8_t)h; t.min = (uint8_t)mi; t.sec = (uint8_t)s;
            if (!bsp_rtc_set(&t)) { reply("#ERR 5 RTC"); return; }
            g_time = t; g_time_ok = true;
        }
        reply("#OK TIME %04u-%02u-%02u %02u:%02u:%02u", g_time.year, g_time.month, g_time.day, g_time.hour, g_time.min, g_time.sec);
        return;
    }
    if (!strcmp(cmd, "CAL") && argc == 3) {
        int w = (argv[1][1] == '2') ? 1 : 0;
        for (char *c = argv[2]; *c; c++) *c = (char)toupper((unsigned char)*c);
        int32_t mv = (int32_t)(g_meas.ain_mv[(w ? AIN_W2 : AIN_W1) - 1] + 0.5f);
        param_id_t id = !strcmp(argv[2], "DRY") ? (w ? P_W2_DRY : P_W1_DRY) : (w ? P_W2_WET : P_W1_WET);
        if (!params_set(id, mv)) { reply("#ERR 2 value out of range"); return; }
        reply("#OK CAL W%d %s %ld mV (SAVE to keep)", w + 1, argv[2], (long)mv); return;
    }
    if (!strcmp(cmd, "ERRCLR")) { g_err = 0; reply("#OK ERRCLR"); return; }
    if (!strcmp(cmd, "FAULT") && argc == 3) {
        for (char *c = argv[1]; *c; c++) *c = (char)toupper((unsigned char)*c);
        int f = sensors_find_fault(argv[1]);
        if (f < 0) { reply("#ERR 4 unknown fault"); return; }
        sensors_set_fault((fault_t)f, atoi(argv[2]) != 0);
        proto_event("FAULT %s %s", argv[1], atoi(argv[2]) ? "ON" : "OFF");
        reply("#OK FAULT %s %d", argv[1], atoi(argv[2]) != 0); return;
    }
    if (!strcmp(cmd, "FAULTS")) {
        char b[96]; int p = snprintf(b, sizeof b, "#OK FAULTS");
        for (int i = 0; i < FLT_COUNT; i++) p += snprintf(b + p, sizeof b - (size_t)p, " %s=%d", g_fault_name[i], sensors_fault((fault_t)i));
        proto_send(b); return;
    }
    if (!strcmp(cmd, "MON") && argc == 3) {
        bool on = atoi(argv[2]) != 0;
        if (!strcmp(argv[1], "485")) s_mon_485 = on;
        else if (!strcmp(argv[1], "SPI") || !strcmp(argv[1], "spi")) s_mon_spi = on;
        else { reply("#ERR 2 485|SPI"); return; }
        reply("#OK MON %s %d", argv[1], on); return;
    }
    if (!strcmp(cmd, "NODES")) {
        for (unsigned i = 0; i < (unsigned)par(P_RS485_NODES) && i < D2417_MAX_NODES; i++) {
            char b[128]; int p = snprintf(b, sizeof b, "#NODE %u ok=%d;type=%02X;id=%04X;err=%u;v=", i + 1, g_node[i].ok,
                                          g_node[i].type, g_node[i].id, g_node[i].errors);
            for (unsigned j = 0; j < g_node[i].n; j++) p += snprintf(b + p, sizeof b - (size_t)p, "%d/%u ", g_node[i].val[j], g_node[i].st[j]);
            proto_send(b);
        }
        reply("#OK NODES"); return;
    }
    reply("#ERR 1 unknown command (HELP)");
}

void proto_poll(void)
{
    if (s_buz_off && (int32_t)(bsp_millis() - s_buz_off) >= 0) { bsp_buzzer(false); s_buz_off = 0; }
    while (s_tail != s_head) {
        char c = (char)s_ring[s_tail];
        s_tail = (uint16_t)((s_tail + 1u) % RX_RING);
        if (c == '\r') continue;
        if (c == '\n') { s_line[s_len] = 0; if (s_len) exec(s_line); s_len = 0; continue; }
        if (s_len < LINE_MAX - 1u) s_line[s_len++] = c;
    }
}
