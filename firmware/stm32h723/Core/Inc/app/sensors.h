/**
 * @file  sensors.h
 * @brief Measurements of the stand: temperatures from STM32F030 (M8SPI),
 *        analog inputs A1..A12 (MCP3208), water level, fault injection.
 */
#ifndef SENSORS_H
#define SENSORS_H
#include <stdint.h>
#include <stdbool.h>

#define N_TEMP    8      /* DS1..DS8 on the auxiliary MCU */
#define N_AIN    12      /* terminal inputs A1..A12      */

/* Fault injection for laboratory work 8 (імітація несправностей) */
typedef enum { FLT_T1, FLT_T2, FLT_T3, FLT_RH, FLT_W1, FLT_W2, FLT_TANK,
               FLT_F030, FLT_485, FLT_COUNT } fault_t;
extern const char *const g_fault_name[FLT_COUNT];

typedef struct {
    /* digital temperatures, °C (DS1 = air, DS2 = soil, DS3 = water) */
    float   t[N_TEMP];
    bool    t_ok[N_TEMP];
    /* air humidity / temperature AM2301A (M8SPI extension, see docs) */
    float   rh, ta;
    bool    rh_ok;
    /* analog terminal inputs, voltage at the terminal, mV */
    float   ain_mv[N_AIN];
    uint16_t raw[16];
    float   v33_mv;          /* MCP3208 U21 CH4: 3.3 V rail monitor (scaled raw) */
    /* derived values */
    float   w[2];            /* soil moisture W1, W2, % (after calibration) */
    bool    w_ok[2];
    float   light_pct;       /* A6, relative units 0..100 */
    float   uv_mv;           /* A5, GUVA-S12SD module output */
    bool    lvl_min;         /* true = water above MIN electrode */
    bool    lvl_full;        /* true = tank full (MAX electrode) */
    float   co2_ppm;         /* from RS-485 node, 0 if not present */
    /* status of the auxiliary MCU link */
    bool    f030_ok;
    uint8_t f030_ac_v;       /* mains voltage reported by F030, V */
} meas_t;

extern meas_t g_meas;

void sensors_init(void);
void sensors_poll_adc(void);   /* call every 200 ms */
void sensors_poll_f030(void);  /* call every 1000 ms */
void sensors_set_fault(fault_t f, bool on);
bool sensors_fault(fault_t f);
int  sensors_find_fault(const char *name);
void sensors_triacs_off(void); /* M8SPI mode 0x03: all triac channels = 0 */

/* analog channel assignment of the stand (terminal number 1..12) */
#define AIN_W1     1
#define AIN_W2     2
#define AIN_UV     5
#define AIN_LIGHT  6
#define AIN_LVLMIN 11
#define AIN_LVLMAX 12

#endif
