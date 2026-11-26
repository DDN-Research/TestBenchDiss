/**
 * @file  params.c
 * @brief Parameter storage: defaults, limits, EEPROM image with CRC-16.
 */
#include "app/params.h"
#include "app/bsp.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* Names follow the D2.GreenHouse manual where the meaning is the same
 * (HEAT_EN, VEN_EN, WAT_EN, BRG_EN, UV_EN ...).                          */
const param_desc_t g_param_desc[P_COUNT] = {
 /* name           def     min     max  dec unit */
 {"HEAT_EN",         2,      0,      3, 0, ""   }, /* 0 off,1 soil,2 air,3 both */
 {"HEAT_TSET",     250,     50,    450, 1, "C"  }, /* air set point            */
 {"HEAT_HYST",      20,      2,    100, 1, "C"  }, /* hysteresis (full width)  */
 {"GND_TSET",      220,     50,    400, 1, "C"  }, /* soil set point           */
 {"GND_HYST",       10,      2,     50, 1, "C"  },
 {"SAFE_TMAX",     450,    300,    600, 1, "C"  }, /* overheat cut-off         */
 {"RELAY_MIN_S",     5,      0,    600, 0, "s"  }, /* min time between switch. */
 {"VEN_EN",          3,      0,      4, 0, ""   }, /* 0 off,1 sched,2 RH,3 T,4 RH|T */
 {"VEN_TMP",       280,     50,    500, 1, "C"  },
 {"VEN_HUMM",      800,    100,   1000, 1, "%"  },
 {"VEN_BAND",       50,      5,    300, 1, ""   }, /* proportional band        */
 {"VEN_PWM_MN",     20,      0,    100, 0, "%"  },
 {"VEN_PWM_MX",    100,     10,    100, 0, "%"  },
 {"VEN_CO2",         0,      0,   5000, 0, "ppm"}, /* 0 = CO2 not used         */
 {"WAT_EN",          2,      0,      2, 0, ""   }, /* 0 off,1 schedule,2 moisture */
 {"WAT_HUM_S",     350,     50,    900, 1, "%"  }, /* start watering below     */
 {"WAT_HUM_E",     600,    100,   1000, 1, "%"  }, /* stop watering above      */
 {"WAT_TMAX",       20,      1,    600, 0, "s"  }, /* max pump run             */
 {"WAT_PAUSE",      60,      5,   3600, 0, "s"  }, /* soak pause               */
 {"WAT_HOUR",        8,      0,     23, 0, "h"  }, /* schedule mode: hour      */
 {"W1_DRY",       2800,      0,   9000, 0, "mV" }, /* calibration points       */
 {"W1_WET",       1300,      0,   9000, 0, "mV" },
 {"W2_DRY",       2800,      0,   9000, 0, "mV" },
 {"W2_WET",       1300,      0,   9000, 0, "mV" },
 {"BRG_EN",          1,      0,      2, 0, ""   }, /* 0 off,1 auto,2 manual    */
 {"BRG_ON",          6,      0,     23, 0, "h"  }, /* photoperiod start        */
 {"BRG_OFF",        20,      0,     23, 0, "h"  }, /* photoperiod end          */
 {"BRG_LUX",        40,      0,    100, 0, "%"  }, /* light threshold          */
 {"BRG_RGB",  0xFF60A0,      0, 0xFFFFFF,0, "rgb"},
 {"LED_N",           8,      0,     60, 0, ""   },
 {"UV_EN",           0,      0,      1, 0, ""   },
 {"UV_DOSE",       100,      1,  10000, 0, "u"  },
 {"STREAM_MS",    1000,      0,  10000, 0, "ms" },
 {"RS485_BAUD",   9600,   1200, 115200, 0, "bd" },
 {"RS485_NODES",     0,      0,      8, 0, ""   }, /* polled addresses 1..N    */
 {"PWM_HZ",        500,     50,   5000, 0, "Hz" },
};

static int32_t s_val[P_COUNT];
static bool    s_load_ok;

#define EE_MAGIC   0x53474432u      /* "2DGS" little endian: 'D','2','G','S' */
#define EE_VER     1u
#define EE_ADDR    0u

static uint16_t crc16(const uint8_t *d, uint32_t n)
{
    uint16_t c = 0xFFFF;
    while (n--) {
        c ^= (uint16_t)(*d++) << 8;
        for (int i = 0; i < 8; i++) c = (c & 0x8000) ? (uint16_t)((c << 1) ^ 0x1021) : (uint16_t)(c << 1);
    }
    return c;
}

typedef struct { uint32_t magic; uint8_t ver, count, rsv[2]; int32_t v[P_COUNT]; uint16_t crc; } ee_img_t;

void params_defaults(void)
{
    for (int i = 0; i < P_COUNT; i++) s_val[i] = g_param_desc[i].def;
}

void params_init(void)
{
    ee_img_t img;
    params_defaults();
    s_load_ok = false;
    if (!bsp_eeprom_read(EE_ADDR, (uint8_t *)&img, sizeof img)) return;
    if (img.magic != EE_MAGIC || img.ver != EE_VER || img.count != P_COUNT) return;
    if (crc16((const uint8_t *)&img, offsetof(ee_img_t, crc)) != img.crc) return;
    for (int i = 0; i < P_COUNT; i++) {
        const param_desc_t *d = &g_param_desc[i];
        if (img.v[i] >= d->min && img.v[i] <= d->max) s_val[i] = img.v[i];
    }
    s_load_ok = true;
}

bool params_save(void)
{
    ee_img_t img;
    memset(&img, 0, sizeof img);
    img.magic = EE_MAGIC; img.ver = EE_VER; img.count = P_COUNT;
    memcpy(img.v, s_val, sizeof s_val);
    img.crc = crc16((const uint8_t *)&img, offsetof(ee_img_t, crc));
    return bsp_eeprom_write(EE_ADDR, (const uint8_t *)&img, sizeof img);
}

bool    params_load_ok(void)        { return s_load_ok; }
int32_t par(param_id_t id)          { return s_val[id]; }

float parf(param_id_t id)
{
    float v = (float)s_val[id];
    for (int i = 0; i < g_param_desc[id].dec; i++) v /= 10.0f;
    return v;
}

bool params_set(param_id_t id, int32_t v)
{
    if (id >= P_COUNT) return false;
    if (v < g_param_desc[id].min || v > g_param_desc[id].max) return false;
    s_val[id] = v;
    return true;
}

int params_find(const char *name)
{
    for (int i = 0; i < P_COUNT; i++)
        if (strcmp(name, g_param_desc[i].name) == 0) return i;
    return -1;
}

/* Fixed-point parser: "25", "25.5", "-3.25", "0xFF60A0" */
bool params_parse(param_id_t id, const char *t, int32_t *out)
{
    uint8_t dec = g_param_desc[id].dec;
    if (t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) {
        char *e; long v = strtol(t, &e, 16);
        if (*e) return false;
        *out = (int32_t)v; return true;
    }
    int neg = 0; int64_t v = 0; int frac = -1;
    if (*t == '-') { neg = 1; t++; }
    if (!*t) return false;
    for (; *t; t++) {
        if (*t == '.' || *t == ',') { if (frac >= 0) return false; frac = 0; continue; }
        if (*t < '0' || *t > '9') return false;
        if (frac >= 0) { if (frac >= dec) continue; frac++; }
        v = v * 10 + (*t - '0');
        if (v > 100000000) return false;
    }
    if (frac < 0) frac = 0;
    for (; frac < dec; frac++) v *= 10;
    *out = (int32_t)(neg ? -v : v);
    return true;
}

int params_format(param_id_t id, char *buf, int size)
{
    int32_t v = s_val[id];
    uint8_t dec = g_param_desc[id].dec;
    if (g_param_desc[id].unit[0] == 'r') return snprintf(buf, (size_t)size, "0x%06lX", (unsigned long)v);
    if (dec == 0) return snprintf(buf, (size_t)size, "%ld", (long)v);
    int32_t p = 1; for (int i = 0; i < dec; i++) p *= 10;
    int32_t a = v < 0 ? -v : v;
    return snprintf(buf, (size_t)size, "%s%ld.%0*ld", v < 0 ? "-" : "",
                    (long)(a / p), (int)dec, (long)(a % p));
}
