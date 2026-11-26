/**
 * @file  control.c
 * @brief Laws of control implemented on the stand:
 *   - two-position control with hysteresis (air heater K1, soil heater K3);
 *   - proportional PWM control of the fan (temperature / humidity / CO2);
 *   - watering by soil moisture with interlock by water level and time limit;
 *   - lighting by photoperiod (RTC) and light level (WS2812, K5, PW2);
 *   - UV dose accumulation (K4);
 *   - protective interlocks (overheat, dry run, sensor failure) in ALL modes.
 */
#include "app/control.h"
#include "app/params.h"
#include "app/sensors.h"
#include "app/bsp.h"
#include "app/proto.h"
#include "app/app.h"
#include <string.h>

outputs_t g_out;
static uint32_t s_k_last[BSP_RELAYS];    /* time of last switching, ms */
static uint32_t s_wat_t0;
static int      s_wat_day_done = -1;      /* schedule mode: day of last run */
static int      s_uv_day = -1;

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static bool k_is(uint8_t k) { return (g_out.relay >> k) & 1u; }

/* switch relay k with minimum switching interval; 'force' for safety-off */
static void k_set(uint8_t k, bool on, bool force, const char *why)
{
    if (k_is(k) == on) return;
    uint32_t now = bsp_millis();
    if (!force && now - s_k_last[k] < (uint32_t)par(P_RELAY_MIN_S) * 1000u) return;
    s_k_last[k] = now;
    if (on) g_out.relay |= (uint8_t)(1u << k); else g_out.relay &= (uint8_t)~(1u << k);
    proto_event("K%u %s %s", k + 1u, on ? "ON" : "OFF", why ? why : "");
}

void control_init(void)
{
    memset(&g_out, 0, sizeof g_out);
    g_out.mode = MODE_AUTO;
    for (unsigned i = 0; i < BSP_RELAYS; i++) s_k_last[i] = 0u - 3600000u;
}

/* ---------- two-position control with hysteresis ---------- */
static void hyst(uint8_t k, bool valid, float t, float set, float hyst_w, const char *name)
{
    static char why[48];
    if (!valid) { k_set(k, false, true, "sensor failure -> safe OFF"); return; }
    if (t < set - hyst_w / 2.0f) {
        app_fmt(why, sizeof why, "%s=%.1f < %.1f", name, (double)t, (double)(set - hyst_w / 2.0f));
        k_set(k, true, false, why);
    } else if (t > set + hyst_w / 2.0f) {
        app_fmt(why, sizeof why, "%s=%.1f > %.1f", name, (double)t, (double)(set + hyst_w / 2.0f));
        k_set(k, false, false, why);
    }   /* inside the band: state is kept */
}

static float air_t(bool *ok)
{
    if (g_meas.t_ok[0]) { *ok = true; return g_meas.t[0]; }
    if (g_meas.rh_ok)   { *ok = true; return g_meas.ta; }
    *ok = false; return 0.0f;
}

static void heating(void)
{
    int32_t en = par(P_HEAT_EN);
    bool ok; float t = air_t(&ok);
    if (en == 2 || en == 3) hyst(K_HEAT, ok, t, parf(P_HEAT_TSET), parf(P_HEAT_HYST), "T1");
    else k_set(K_HEAT, false, true, "HEAT_EN");
    if (en == 1 || en == 3) hyst(K_SOIL, g_meas.t_ok[1], g_meas.t[1], parf(P_GND_TSET), parf(P_GND_HYST), "T2");
    else k_set(K_SOIL, false, true, "HEAT_EN");
}

/* ---------- proportional fan control ---------- */
static void ventilation(void)
{
    int32_t en = par(P_VEN_EN);
    float band = parf(P_VEN_BAND), e = -1.0f;
    bool ok; float t = air_t(&ok);
    if ((en == 3 || en == 4) && ok)         e = (t - parf(P_VEN_TMP)) / band;
    if ((en == 2 || en == 4) && g_meas.rh_ok) {
        float er = (g_meas.rh - parf(P_VEN_HUMM)) / band;
        if (er > e) e = er;
    }
    if (en == 1 && g_time_ok && g_time.min < 10) e = 1.0f;   /* 10 min every hour */
    if (en != 0 && par(P_VEN_CO2) > 0 && g_meas.co2_ppm > 0.0f) {
        float ec = (g_meas.co2_ppm - (float)par(P_VEN_CO2)) / ((float)par(P_VEN_CO2) * 0.2f);
        if (ec > e) e = ec;
    }
    float duty = 0.0f;
    if (e > 0.0f) {
        float mn = (float)par(P_VEN_PWM_MN), mx = (float)par(P_VEN_PWM_MX);
        duty = mn + (mx - mn) * clampf(e, 0.0f, 1.0f);
    }
    if (g_out.overheat) duty = (float)par(P_VEN_PWM_MX);
    g_out.pwm[PW_FAN] = (uint8_t)(duty + 0.5f);
}

/* ---------- watering with interlock ---------- */
static bool soil_w(float *w)
{
    int n = 0; float s = 0;
    for (int i = 0; i < 2; i++) if (g_meas.w_ok[i]) { s += g_meas.w[i]; n++; }
    if (!n) return false;
    *w = s / (float)n; return true;
}

static void watering(void)
{
    int32_t en = par(P_WAT_EN);
    uint32_t now = bsp_millis();
    float w; bool wok = soil_w(&w);

    if (en == 0) { k_set(K_PUMP, false, true, "WAT_EN=0"); g_out.wat_state = 0; return; }
    switch (g_out.wat_state) {
    case 0: /* idle */
        if (!g_meas.lvl_min) break;
        if ((en == 2 && wok && w < parf(P_WAT_HUM_S)) ||
            (en == 1 && g_time_ok && g_time.hour == par(P_WAT_HOUR) && s_wat_day_done != g_time.day)) {
            if (en == 1) s_wat_day_done = g_time.day;
            k_set(K_PUMP, true, true, en == 2 ? "W < WAT_HUM_S" : "schedule");
            g_out.wat_state = 1; s_wat_t0 = now;
        }
        break;
    case 1: /* pump */
        if (!g_meas.lvl_min) { k_set(K_PUMP, false, true, "no water -> interlock"); g_out.wat_state = 2; s_wat_t0 = now; break; }
        if ((en == 2 && wok && w >= parf(P_WAT_HUM_E)) || now - s_wat_t0 >= (uint32_t)par(P_WAT_TMAX) * 1000u) {
            k_set(K_PUMP, false, true, (now - s_wat_t0 >= (uint32_t)par(P_WAT_TMAX) * 1000u) ? "WAT_TMAX" : "W >= WAT_HUM_E");
            g_out.wat_state = 2; s_wat_t0 = now;
        }
        break;
    default: /* soak pause */
        if (now - s_wat_t0 >= (uint32_t)par(P_WAT_PAUSE) * 1000u) g_out.wat_state = 0;
        break;
    }
}

/* ---------- lighting and UV ---------- */
static bool photoperiod(void)
{
    if (!g_time_ok) return true;
    int on = (int)par(P_BRG_ON), off = (int)par(P_BRG_OFF), h = g_time.hour;
    return on <= off ? (h >= on && h < off) : (h >= on || h < off);
}

static void lighting(uint32_t dt_ms)
{
    int32_t en = par(P_BRG_EN);
    bool need = (en == 2) || (en == 1 && photoperiod() && g_meas.light_pct < (float)par(P_BRG_LUX));
    float lvl = 0.0f;
    if (need) {
        lvl = en == 2 ? 100.0f
            : 20.0f + 80.0f * ((float)par(P_BRG_LUX) - g_meas.light_pct) / (float)(par(P_BRG_LUX) ? par(P_BRG_LUX) : 1);
        lvl = clampf(lvl, 20.0f, 100.0f);
    }
    k_set(K_LIGHT, need, false, need ? "light < BRG_LUX" : "light ok / night");
    g_out.pwm[PW_LED] = (uint8_t)(lvl + 0.5f);
    g_out.rgb = need ? (uint32_t)par(P_BRG_RGB) : 0u;

    /* UV dose: H = sum(Uuv * dt), reset at a new day */
    if (g_time_ok && s_uv_day != g_time.day) { s_uv_day = g_time.day; g_out.uv_dose = 0.0f; }
    g_out.uv_dose += g_meas.uv_mv / 1000.0f * ((float)dt_ms / 1000.0f);
    bool uv = par(P_UV_EN) && photoperiod() && g_out.uv_dose < (float)par(P_UV_DOSE);
    k_set(K_UV, uv, false, uv ? "UV dose < UV_DOSE" : "UV dose reached / off");
}

/* ---------- interlocks: active in AUTO and MANUAL ---------- */
static void interlocks(void)
{
    bool ok; float t = air_t(&ok);
    bool oh = ok && t > parf(P_SAFE_TMAX);
    if (oh && !g_out.overheat) proto_event("ALARM overheat T=%.1f > SAFE_TMAX", (double)t);
    g_out.overheat = oh;
    if (oh) { k_set(K_HEAT, false, true, "overheat interlock"); k_set(K_SOIL, false, true, "overheat interlock"); }
    g_out.no_water = !g_meas.lvl_min;
    if (g_out.no_water && k_is(K_PUMP)) k_set(K_PUMP, false, true, "no water -> interlock");
}

void control_step(uint32_t dt_ms)
{
    if (g_out.mode == MODE_AUTO) {
        heating(); ventilation(); watering(); lighting(dt_ms);
    }
    interlocks();
}

void control_apply(void)
{
    static uint8_t grb[BSP_WS2812_MAX * 3];
    uint8_t leds = (uint8_t)((g_out.overheat ? 1u : 0u) | (g_out.no_water ? 2u : 0u));
    bsp_relays_write(g_out.relay, leds);
    for (uint8_t i = 0; i < BSP_PWM_CH; i++) bsp_pwm_set(i, (uint16_t)(g_out.pwm[i] * 10u));
    uint16_t n = (uint16_t)par(P_LED_N);
    for (uint16_t i = 0; i < n; i++) {
        grb[3 * i]     = (uint8_t)(g_out.rgb >> 8);    /* G */
        grb[3 * i + 1] = (uint8_t)(g_out.rgb >> 16);   /* R */
        grb[3 * i + 2] = (uint8_t)(g_out.rgb);         /* B */
    }
    bsp_ws2812_write(grb, n);
}

void control_safe_state(void)
{
    g_out.mode = MODE_MANUAL;
    g_out.relay = 0; memset(g_out.pwm, 0, sizeof g_out.pwm); g_out.rgb = 0;
    g_out.wat_state = 0;
    control_apply();
    sensors_triacs_off();
    proto_event("SAFE STATE: all outputs OFF, mode MANUAL");
}

bool control_manual_relay(uint8_t k, bool on, const char **why)
{
    *why = "";
    if (g_out.mode != MODE_MANUAL || k >= BSP_RELAYS) { *why = "not in MANUAL mode"; return false; }
    if (on && k == K_PUMP && !g_meas.lvl_min) { *why = "interlock: no water"; return false; }
    if (on && (k == K_HEAT || k == K_SOIL) && g_out.overheat) { *why = "interlock: overheat"; return false; }
    k_set(k, on, true, "manual");
    return true;
}

bool control_manual_pwm(uint8_t ch, uint8_t pct)
{
    if (g_out.mode != MODE_MANUAL || ch >= BSP_PWM_CH || pct > 100) return false;
    g_out.pwm[ch] = pct;
    return true;
}
