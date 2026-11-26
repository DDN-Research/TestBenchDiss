/**
 * @file  bsp_sim.c
 * @brief PC (Linux/macOS) implementation of bsp.h: the SAME application code
 *        as on the STM32 runs against a simple physical model of the stand
 *        (mini greenhouse chamber). The "USB" port is a pseudo-terminal, so the
 *        PC program connects to it exactly like to the real virtual COM port.
 *
 *  Simulated: air/soil/water temperature, air humidity (via M8SPI 0x04),
 *  soil moisture sensors, light and UV, tank level (WaterControl),
 *  STM32F030 answers (M8SPI), two RS-485 nodes (CO2 node, TS11), EEPROM, RTC.
 *
 *  Build: make      Run: ./stand_sim [link-name]   (default /tmp/d2gh_stand)
 *  Env:   SIM_SPEED=5  -> physical processes run 5x faster
 */
#define _XOPEN_SOURCE 700
#define _DEFAULT_SOURCE
#include "app/bsp.h"
#include "app/app.h"
#include "app/d2417.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <termios.h>

static int      s_pty = -1;
static double   s_t0;
static double   s_speed = 1.0;
static uint8_t  s_relay, s_pwm[4];
static uint8_t  s_ee[512];
static long     s_rtc_offset;
static uint8_t  s_rs_rx[256]; static int s_rs_h, s_rs_t;

/* ---- plant state ---- */
static double Ta = 21.0, Ts = 20.0, Tw = 19.0, RH = 62.0, W = 42.0, Vol = 80.0, CO2 = 650.0, Tamb = 20.0;
static double s_last;

static double now_s(void)
{
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + ts.tv_nsec * 1e-9;
}
static double noise(double a) { return a * ((double)rand() / RAND_MAX - 0.5); }

static void plant_step(double dt)
{
    dt *= s_speed;
    double heat = (s_relay & 1) ? 1.0 : 0.0, soilh = (s_relay & 4) ? 1.0 : 0.0;
    double pump = (s_relay & 2) && Vol > 2.0 ? 1.0 : 0.0, fan = s_pwm[0] / 100.0;
    /* air: heater 12 V ~ +0.12 °C/s, losses grow with fan speed */
    Ta += dt * (0.12 * heat - (Ta - Tamb) * (0.004 + 0.02 * fan));
    Ts += dt * (0.04 * soilh + (Ta - Ts) * 0.002);
    Tw += dt * ((Ta - Tw) * 0.001);
    double rh_eq = 62.0 - (Ta - 20.0) * 2.0 + pump * 5.0;
    RH += dt * ((rh_eq - RH) * 0.01 - fan * 0.05);
    RH = RH < 15 ? 15 : (RH > 98 ? 98 : RH);
    W += dt * (-0.03 * (1.0 + (Ta - 20.0) / 20.0) + 1.2 * pump);
    W = W < 5 ? 5 : (W > 95 ? 95 : W);
    Vol -= dt * 0.6 * pump; if (Vol < 0) Vol = 0;
    CO2 += dt * (0.8 - fan * 6.0 * (CO2 - 420.0) / 400.0); if (CO2 < 420) CO2 = 420;
}

static double day_light(void)          /* 0..1 by local time of day */
{
    time_t t = time(NULL) + s_rtc_offset; struct tm tm; localtime_r(&t, &tm);
    double h = tm.tm_hour + tm.tm_min / 60.0;
    double d = sin((h - 6.0) / 12.0 * M_PI);
    return d > 0 ? d * 0.55 : 0.0;
}

/* ---- bsp API ---- */
void bsp_init(void)
{
    s_t0 = now_s(); s_last = s_t0;
    const char *sp = getenv("SIM_SPEED"); if (sp) s_speed = atof(sp);
    FILE *f = fopen("sim_eeprom.bin", "rb");
    if (f) { if (fread(s_ee, 1, sizeof s_ee, f) != sizeof s_ee) memset(s_ee, 0xFF, sizeof s_ee); fclose(f); }
    else memset(s_ee, 0xFF, sizeof s_ee);
}

uint32_t bsp_millis(void) { return (uint32_t)((now_s() - s_t0) * 1000.0); }
void bsp_delay_us(uint32_t us) { usleep(us); }

void bsp_poll(void)
{
    double t = now_s(); plant_step(t - s_last); s_last = t;
    uint8_t b[256];
    ssize_t n = read(s_pty, b, sizeof b);
    if (n > 0) app_usb_rx(b, (uint32_t)n);
}

void bsp_relays_write(uint8_t k, uint8_t led) { (void)led; s_relay = k; }
void bsp_outputs_enable(bool en) { (void)en; }
void bsp_pwm_set(uint8_t ch, uint16_t pm) { if (ch < 4) s_pwm[ch] = (uint8_t)(pm / 10); }
void bsp_ws2812_write(const uint8_t *g, uint16_t n) { (void)g; (void)n; }
void bsp_buzzer(bool on) { (void)on; }
uint8_t bsp_keys(void) { return 0; }

/* inverse of the terminal mapping used in sensors.c */
static const uint8_t k_ain_idx[12] = { 6, 5, 4, 7, 0, 3, 1, 2, 10, 9, 8, 11 };
static uint16_t mv2raw(double mv)
{
    double r = mv / (24.8 / 6.8) / 2495.0 * 4096.0;
    r = r < 0 ? 0 : (r > 4095 ? 4095 : r);
    return (uint16_t)r;
}

bool bsp_adc_read_all(uint16_t raw[BSP_ADC_CH])
{
    double ain[12] = {0};
    double light = day_light() + ((s_relay & 16) ? 0.25 : 0.0) + s_pwm[1] / 400.0;
    ain[0] = 2800.0 - W / 100.0 * 1500.0 + noise(10);           /* W1 */
    ain[1] = 2800.0 - (W - 3.0) / 100.0 * 1500.0 + noise(10);   /* W2 */
    ain[4] = day_light() * 900.0 + ((s_relay & 8) ? 300.0 : 0.0) + noise(5); /* UV  */
    ain[5] = light * 5000.0 > 5000.0 ? 5000.0 : light * 5000.0; /* light */
    ain[10] = Vol > 10.0 ? 4800.0 : 50.0;                        /* LVL MIN */
    ain[11] = Vol > 90.0 ? 4800.0 : 50.0;                        /* LVL MAX */
    memset(raw, 0, BSP_ADC_CH * sizeof raw[0]);
    for (int i = 0; i < 12; i++) raw[k_ain_idx[i]] = mv2raw(ain[i]);
    raw[12] = (uint16_t)(1650.0 / 2495.0 * 4096.0);
    return true;
}

/* ---- STM32F030 emulation (M8SPI_V1.0) ---- */
static void put_t(uint8_t *p, double t) { int16_t v = (int16_t)lround(t * 10.0); p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }

void bsp_m8spi_xfer(const uint8_t *tx, uint8_t *rx, uint16_t len)
{
    memset(rx, 0xFF, len);
    uint8_t *a = rx + 1; uint16_t n = (uint16_t)(len - 2);
    switch (tx[0]) {
    case 0x01: if (n >= 5) { a[0] = 0; a[1] = 0x32; a[2] = 0x38; a[3] = 0x35; a[4] = 0x01; } break;
    case 0x02:
        memset(a, 0, n);
        put_t(&a[0], Ta + noise(0.1)); put_t(&a[2], Ts + noise(0.1)); put_t(&a[4], Tw + noise(0.1));
        for (int i = 3; i < 8; i++) { a[2 * i] = 0x7F; a[2 * i + 1] = 0xFF; }   /* not connected */
        a[16] = 228; break;
    case 0x03: memset(a, 0, n); break;
    case 0x04: if (n >= 5) { uint16_t h = (uint16_t)lround((RH + noise(0.4)) * 10.0);
               a[0] = (uint8_t)(h >> 8); a[1] = (uint8_t)h; put_t(&a[2], Ta + 0.3); a[4] = 1; } break;
    }
}

/* ---- RS-485 bus with two emulated nodes ---- */
void bsp_rs485_config(uint32_t baud) { (void)baud; }
static void rs_push(const uint8_t *b, uint16_t n) { for (uint16_t i = 0; i < n; i++) { s_rs_rx[s_rs_h] = b[i]; s_rs_h = (s_rs_h + 1) % 256; } }
void bsp_rs485_send(const uint8_t *buf, uint16_t len)
{
    uint8_t dev; uint16_t id; const uint8_t *body; uint8_t ans[32], b[8]; uint16_t n;
    if (d2417_parse(buf, len, &dev, &id, &body) < 1) return;
    if (dev == 1) {        /* CO2 node: [type][VH][VL][ST] */
        int16_t v = (int16_t)lround(CO2); b[0] = DT_CO2; b[1] = (uint8_t)(v >> 8); b[2] = (uint8_t)v; b[3] = 1;
        n = d2417_build(ans, D2417_START_ANS, dev, 0xC021, b, 4); rs_push(ans, n);
    } else if (dev == 2) { /* TS11 outdoor temperature */
        int16_t v = (int16_t)lround((Tamb - 5.0) * 10.0); b[0] = DT_TS11; b[1] = (uint8_t)(v >> 8); b[2] = (uint8_t)v; b[3] = 1;
        n = d2417_build(ans, D2417_START_ANS, dev, 0x1A07, b, 4); rs_push(ans, n);
    }
}
int bsp_rs485_getc(void)
{
    if (s_rs_t == s_rs_h) return -1;
    int c = s_rs_rx[s_rs_t]; s_rs_t = (s_rs_t + 1) % 256; return c;
}

/* ---- EEPROM (file), RTC (system clock + offset) ---- */
bool bsp_eeprom_read(uint16_t a, uint8_t *b, uint16_t n)
{ if (a + n > sizeof s_ee) return false; memcpy(b, s_ee + a, n); return true; }
bool bsp_eeprom_write(uint16_t a, const uint8_t *b, uint16_t n)
{
    if (a + n > sizeof s_ee) return false;
    memcpy(s_ee + a, b, n);
    FILE *f = fopen("sim_eeprom.bin", "wb"); if (!f) return false;
    fwrite(s_ee, 1, sizeof s_ee, f); fclose(f); return true;
}
bool bsp_rtc_get(bsp_time_t *t)
{
    time_t x = time(NULL) + s_rtc_offset; struct tm tm; localtime_r(&x, &tm);
    t->year = (uint16_t)(tm.tm_year + 1900); t->month = (uint8_t)(tm.tm_mon + 1); t->day = (uint8_t)tm.tm_mday;
    t->hour = (uint8_t)tm.tm_hour; t->min = (uint8_t)tm.tm_min; t->sec = (uint8_t)tm.tm_sec;
    return true;
}
bool bsp_rtc_set(const bsp_time_t *t)
{
    struct tm tm = {0};
    tm.tm_year = t->year - 1900; tm.tm_mon = t->month - 1; tm.tm_mday = t->day;
    tm.tm_hour = t->hour; tm.tm_min = t->min; tm.tm_sec = t->sec; tm.tm_isdst = -1;
    s_rtc_offset = (long)(mktime(&tm) - time(NULL)); return true;
}

void bsp_usb_write(const char *s, uint16_t len) { if (s_pty >= 0) { ssize_t r = write(s_pty, s, len); (void)r; } }
uint32_t bsp_uid(void) { return 0x51A10001u; }

int main(int argc, char **argv)
{
    const char *link = argc > 1 ? argv[1] : "/tmp/d2gh_stand";
    s_pty = posix_openpt(O_RDWR | O_NOCTTY);
    if (s_pty < 0 || grantpt(s_pty) || unlockpt(s_pty)) { perror("pty"); return 1; }
    struct termios tio; tcgetattr(s_pty, &tio); cfmakeraw(&tio); tcsetattr(s_pty, TCSANOW, &tio);
    fcntl(s_pty, F_SETFL, O_NONBLOCK);
    unlink(link);
    if (symlink(ptsname(s_pty), link)) perror("symlink");
    printf("D2.GreenHouse stand simulator: port %s -> %s (SIM_SPEED=%s)\n", link, ptsname(s_pty),
           getenv("SIM_SPEED") ? getenv("SIM_SPEED") : "1");
    fflush(stdout);
    App_Init();
    for (;;) { App_Poll(); usleep(1000); }
}
