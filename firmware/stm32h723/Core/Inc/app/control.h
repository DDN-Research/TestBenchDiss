/**
 * @file  control.h
 * @brief Control algorithms of the stand and output state.
 *
 * Actuators of the stand (призначення виходів стенда):
 *   K1 – air heater 12 V      K2 – pump 12 V        K3 – soil heater 12 V
 *   K4 – UV lamp 12 V         K5 – grow light (relay)
 *   PW1 – fan 12 V (PWM)      PW2 – LED grow light (PWM)   PW3, PW4 – spare
 *   LED – WS2812 strip        buzzer – alarms
 */
#ifndef CONTROL_H
#define CONTROL_H
#include <stdint.h>
#include <stdbool.h>

enum { K_HEAT = 0, K_PUMP = 1, K_SOIL = 2, K_UV = 3, K_LIGHT = 4 };
enum { PW_FAN = 0, PW_LED = 1 };
typedef enum { MODE_AUTO = 0, MODE_MANUAL = 1 } mode_t_;

typedef struct {
    mode_t_  mode;
    uint8_t  relay;          /* bit i = K(i+1)            */
    uint8_t  pwm[4];         /* 0..100 %                  */
    uint32_t rgb;            /* 0xRRGGBB for all LEDs     */
    uint8_t  wat_state;      /* 0 idle, 1 pump, 2 soak     */
    float    uv_dose;        /* accumulated UV dose today */
    bool     overheat;
    bool     no_water;
} outputs_t;

extern outputs_t g_out;

void control_init(void);
void control_step(uint32_t dt_ms);       /* call every 100 ms              */
void control_apply(void);                /* write g_out to the hardware    */
void control_safe_state(void);           /* everything off, MANUAL mode    */
bool control_manual_relay(uint8_t k, bool on, const char **why);
bool control_manual_pwm(uint8_t ch, uint8_t pct);

#endif
