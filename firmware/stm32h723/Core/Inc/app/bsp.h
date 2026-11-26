/**
 * @file    bsp.h
 * @brief   Board support API for the D2.GreenHouse "Stand" firmware.
 *
 * Прикладна логіка (app/<module>.c) звертається до апаратури ЛИШЕ через ці функції.
 * The application layer talks to hardware ONLY through these functions.
 *  - bsp_stm32.c : implementation for STM32H723VET6 (STM32CubeIDE, HAL)
 *  - bsp_sim.c   : implementation for a PC simulator (host build, tests)
 */
#ifndef BSP_H
#define BSP_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define BSP_RELAYS        5u   /* K1..K5 (74AHC595 QA..QE -> ULN2003 -> G5Q) */
#define BSP_PWM_CH        4u   /* PW1..PW4 (IRFR5410 high-side, PC817)      */
#define BSP_ADC_CH       16u   /* MCP3208 U17 CH0..7 = idx 0..7, U21 = 8..15 */
#define BSP_WS2812_MAX   60u   /* max addressable LEDs on the LED terminal  */

typedef struct {
    uint16_t year;      /* 2000..2099 */
    uint8_t  month, day, hour, min, sec;
} bsp_time_t;

/* ---- system ---- */
void     bsp_init(void);
void     bsp_poll(void);          /* called from the main loop (USB TX flush etc.) */
uint32_t bsp_millis(void);
void     bsp_delay_us(uint32_t us);

/* ---- outputs ---- */
void bsp_relays_write(uint8_t k_mask, uint8_t led_mask); /* bit0 = K1 ... */
void bsp_outputs_enable(bool en);                        /* 74AHC595 /OE  */
void bsp_pwm_set(uint8_t ch, uint16_t permille);         /* ch 0..3, 0..1000 */
void bsp_ws2812_write(const uint8_t *grb, uint16_t n_leds);
void bsp_buzzer(bool on);

/* ---- inputs ---- */
uint8_t bsp_keys(void);                                  /* bit0 = KEY1 ... */
/* Reads all MCP3208 channels, returns raw 12-bit codes. true = OK */
bool bsp_adc_read_all(uint16_t raw[BSP_ADC_CH]);

/* ---- link to the auxiliary MCU STM32F030 (protocol M8SPI_V1.0) ---- */
/* Full-duplex byte exchange, chip select is handled inside. */
void bsp_m8spi_xfer(const uint8_t *tx, uint8_t *rx, uint16_t len);

/* ---- RS-485 (UART4 + DE) ---- */
void     bsp_rs485_config(uint32_t baud);
void     bsp_rs485_send(const uint8_t *buf, uint16_t len); /* blocking, DE handled */
int      bsp_rs485_getc(void);                             /* -1 if empty */

/* ---- non-volatile memory AT24CS04 (512 bytes) ---- */
bool bsp_eeprom_read(uint16_t addr, uint8_t *buf, uint16_t len);
bool bsp_eeprom_write(uint16_t addr, const uint8_t *buf, uint16_t len);

/* ---- real-time clock RV-3028-C7 ---- */
bool bsp_rtc_get(bsp_time_t *t);
bool bsp_rtc_set(const bsp_time_t *t);

/* ---- link to the PC: USB CDC (virtual COM port) ---- */
void bsp_usb_write(const char *s, uint16_t len);   /* non-blocking, may drop */
/* bytes received from the PC are pushed by the BSP into app_usb_rx() */

/* ---- identification ---- */
uint32_t bsp_uid(void);

#endif /* BSP_H */
