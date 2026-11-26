/**
 * @file  app.c
 * @brief Main cycle of the "Stand" firmware (cooperative scheduler).
 *
 *  App_Init():  hardware -> parameters -> sensors -> control -> protocol
 *  App_Poll():  every loop     : USB, command parser, RS-485 polling
 *               every 20 ms    : keys
 *               every 100 ms   : control laws + outputs
 *               every 200 ms   : analog inputs (MCP3208)
 *               every 1000 ms  : STM32F030 (temperatures), RTC, error word
 *               every STREAM_MS: telemetry line to the PC
 */
#include "app/app.h"
#include "app/params.h"
#include "app/sensors.h"
#include "app/control.h"
#include "app/proto.h"
#include "app/d2417.h"
#include <stdio.h>
#include <stdarg.h>

bsp_time_t g_time;
bool       g_time_ok;
uint32_t   g_err;

static uint32_t t_ctl, t_adc, t_slow, t_tel, t_key, t_beep;
static uint8_t  s_keys_prev;

int app_fmt(char *buf, size_t size, const char *fmt, ...)
{
    va_list a; va_start(a, fmt);
    int n = vsnprintf(buf, size, fmt, a);
    va_end(a);
    return n;
}

void app_usb_rx(const uint8_t *buf, uint32_t len) { proto_rx(buf, len); }

static void beep(uint32_t ms) { bsp_buzzer(true); t_beep = bsp_millis() + ms; }

void App_Init(void)
{
    bsp_init();
    params_init();
    sensors_init();
    control_init();
    proto_init();
    control_apply();               /* all outputs off ...              */
    bsp_outputs_enable(true);      /* ... only then enable 74AHC595 /OE */
    sensors_triacs_off();          /* mains outputs are never used     */
    d2417_init();
    g_time_ok = bsp_rtc_get(&g_time);
    uint32_t now = bsp_millis();
    t_ctl = t_adc = t_slow = t_tel = t_key = now;
    beep(80);
    proto_event("START %s %s", FW_NAME, FW_VERSION);
}

static void update_errors(void)
{
    uint32_t e = 0, before = g_err;
    if (!g_meas.f030_ok)                         e |= ERR_F030 | ERR_DTS;
    else if (!g_meas.t_ok[0])                    e |= ERR_DTS;
    if (!g_meas.w_ok[0] && !g_meas.w_ok[1])      e |= ERR_HUM_ALL;
    else if (!g_meas.w_ok[0] || !g_meas.w_ok[1]) e |= ERR_HUM_SOME;
    if (!g_meas.lvl_min)                         e |= ERR_NO_WATER;
    if (d2417_any_error())                       e |= ERR_RS485;
    if (!params_load_ok())                       e |= ERR_PARAMS;
    if (!g_time_ok)                              e |= ERR_RTC;
    if (g_out.overheat)                          e |= ERR_OVERHEAT;
    if (!g_meas.rh_ok)                           e |= ERR_AM2301;
    g_err = e;
    if ((e & ~before) & (ERR_OVERHEAT | ERR_NO_WATER | ERR_F030)) beep(300);
}

static void keys_task(void)
{
    uint8_t k = bsp_keys(), pressed = (uint8_t)(k & ~s_keys_prev);
    s_keys_prev = k;
    if (!pressed) return;
    beep(30);
    if (pressed & 0x01u) {                      /* KEY1: AUTO <-> MANUAL */
        g_out.mode = g_out.mode == MODE_AUTO ? MODE_MANUAL : MODE_AUTO;
        proto_event("KEY1 MODE %s", g_out.mode == MODE_AUTO ? "AUTO" : "MANUAL");
    }
    if (pressed & 0x02u) control_safe_state();  /* KEY2: STOP             */
    if (pressed & 0x04u) proto_event("KEY3 MARK");   /* marker for the lab log */
    if (pressed & 0x08u) proto_event("KEY4 MARK");
}

void App_Poll(void)
{
    uint32_t now = bsp_millis();
    bsp_poll();
    proto_poll();
    d2417_task();

    if (t_beep && (int32_t)(now - t_beep) >= 0) { bsp_buzzer(false); t_beep = 0; }
    if (now - t_key >= 20u)  { t_key = now; keys_task(); }
    if (now - t_adc >= 200u) { t_adc = now; sensors_poll_adc(); }
    if (now - t_ctl >= 100u) { control_step(now - t_ctl); t_ctl = now; control_apply(); }
    if (now - t_slow >= 1000u) {
        t_slow = now;
        sensors_poll_f030();
        g_time_ok = bsp_rtc_get(&g_time);
        update_errors();
    }
    uint32_t per = (uint32_t)par(P_STREAM_MS);
    if (per >= 100u && now - t_tel >= per) { t_tel = now; proto_telemetry(); }
}
