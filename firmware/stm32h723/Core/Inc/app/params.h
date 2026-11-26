/**
 * @file  params.h
 * @brief Parameter table of the "Stand" mode (stored in EEPROM AT24CS04).
 *        Таблиця параметрів режиму «Стенд» (зберігаються в EEPROM).
 *
 * Values are integers; 'dec' = number of decimal places (25.0 °C -> 250, dec=1).
 */
#ifndef PARAMS_H
#define PARAMS_H
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    P_HEAT_EN = 0, P_HEAT_TSET, P_HEAT_HYST, P_GND_TSET, P_GND_HYST, P_SAFE_TMAX,
    P_RELAY_MIN_S,
    P_VEN_EN, P_VEN_TMP, P_VEN_HUMM, P_VEN_BAND, P_VEN_PWM_MN, P_VEN_PWM_MX, P_VEN_CO2,
    P_WAT_EN, P_WAT_HUM_S, P_WAT_HUM_E, P_WAT_TMAX, P_WAT_PAUSE, P_WAT_HOUR,
    P_W1_DRY, P_W1_WET, P_W2_DRY, P_W2_WET,
    P_BRG_EN, P_BRG_ON, P_BRG_OFF, P_BRG_LUX, P_BRG_RGB, P_LED_N,
    P_UV_EN, P_UV_DOSE,
    P_STREAM_MS, P_RS485_BAUD, P_RS485_NODES, P_PWM_HZ,
    P_COUNT
} param_id_t;

typedef struct {
    const char *name;   /* name used in the PC protocol */
    int32_t     def, min, max;
    uint8_t     dec;    /* decimal places */
    const char *unit;
} param_desc_t;

extern const param_desc_t g_param_desc[P_COUNT];

void     params_init(void);             /* load from EEPROM or defaults */
bool     params_save(void);
void     params_defaults(void);
int32_t  par(param_id_t id);            /* raw scaled value              */
float    parf(param_id_t id);           /* value as float (scaled back)  */
bool     params_set(param_id_t id, int32_t v);
int      params_find(const char *name); /* -1 if not found               */
/* parse "25.5" according to dec of the parameter; returns false on error */
bool     params_parse(param_id_t id, const char *txt, int32_t *out);
/* format value into buf ("25.5"); returns length */
int      params_format(param_id_t id, char *buf, int size);
bool     params_load_ok(void);          /* true if EEPROM image was valid */

#endif
