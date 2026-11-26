/**
 * @file  app.h
 * @brief Entry points of the "Stand" application (called from main.c).
 */
#ifndef APP_H
#define APP_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "app/bsp.h"

#define FW_NAME     "D2GH-STAND"
#define FW_VERSION  "1.0.0"

/* error word: low bits as in the D2.GreenHouse manual (ErrorLow),
 * high bits are specific to the stand firmware                        */
#define ERR_WIFI        0x00000001u
#define ERR_DTS         0x00000004u   /* temperature sensor not ready     */
#define ERR_RS485       0x00000010u   /* an RS-485 module does not answer */
#define ERR_HUM_SOME    0x00004000u   /* some soil moisture sensors absent */
#define ERR_HUM_ALL     0x00008000u   /* all soil moisture sensors absent  */
#define ERR_NO_WATER    0x00010000u   /* tank: no water                   */
#define ERR_F030        0x01000000u   /* no link with STM32F030 (M8SPI)   */
#define ERR_PARAMS      0x02000000u   /* EEPROM image invalid -> defaults */
#define ERR_RTC         0x04000000u   /* RTC not answering                */
#define ERR_OVERHEAT    0x08000000u   /* air temperature > SAFE_TMAX      */
#define ERR_AM2301      0x10000000u   /* air humidity sensor absent       */

extern bsp_time_t g_time;
extern bool       g_time_ok;
extern uint32_t   g_err;

void App_Init(void);
void App_Poll(void);                       /* call in the main while(1) loop */
void app_usb_rx(const uint8_t *buf, uint32_t len); /* from USB CDC ISR     */
int  app_fmt(char *buf, size_t size, const char *fmt, ...);

#endif
