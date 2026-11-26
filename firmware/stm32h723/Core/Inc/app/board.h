/**
 * @file  board.h
 * @brief Pin map of the D2.GreenHouse controller (KiCad project GreenHouse_4,
 *        base board dD7.102.410 + power/IO board dD7.102.412), STM32H723VET6.
 *        Obtained from the netlists of the schematics (see docs/hardware.md).
 *
 * Peripherals configured by STM32CubeMX (see firmware/CubeMX_SETUP.md):
 *   USB_OTG_HS (internal FS PHY, PA11/PA12) + USB_DEVICE CDC  -> PC link
 *   I2C1  PB6 SCL / PB7 SDA                                   -> AT24CS04, RV-3028-C7
 *   UART4 PA0 TX / PA1 RX                                     -> RS-485 (SN65HVD11)
 *   TIM1  CH1 PE9 (PWM3 -> PW4), CH2 PE11 (PWM2 -> PW3)       -> PWM outputs
 *   TIM3  CH3 PB0 (PWM1 -> PW2), CH4 PB1 (PWM0 -> PW1)
 * All other pins below are configured by bsp_init() itself.
 */
#ifndef BOARD_H
#define BOARD_H
#include "main.h"

/* 74AHC595 -> ULN2003 -> relays K1..K5, LEDs D1, D2 (power board) */
#define HC595_PORT        GPIOD
#define HC595_LAT_PIN     GPIO_PIN_3    /* HC95LAT  */
#define HC595_CLK_PIN     GPIO_PIN_4    /* HC95CLK  */
#define HC595_SER_PIN     GPIO_PIN_5    /* HC95INP  */
#define HC595_OE_PIN      GPIO_PIN_7    /* HC95EN, active low */

/* MCP3208 x2 (U17, U21) sharing lines with CD4051B select inputs:
 *   HC51A = Din, HC51B = Dout, HC51C = CLK, HC51I = /CS of U17
 *   (U21 /CS = inverted HC51I via transistor Q9)                      */
#define ADC_PORT          GPIOC
#define ADC_DIN_PIN       GPIO_PIN_6    /* HC51A (PC2_C kept analog/hi-Z) */
#define ADC_DOUT_PIN      GPIO_PIN_7    /* HC51B (PC3_C kept analog/hi-Z) */
#define ADC_CLK_PIN       GPIO_PIN_4    /* HC51C */
#define ADC_SEL_PIN       GPIO_PIN_5    /* HC51I: 0 -> U17 selected, 1 -> U21 */

/* Link to STM32F030 (M8SPI_V1.0), bit-banged SPI master, mode 0 */
#define M8_PORT           GPIOE
#define M8_SCK_PIN        GPIO_PIN_12   /* SPI2SCK  -> F030 PB3 */
#define M8_MOSI_PIN       GPIO_PIN_13   /* SPI2MOSI -> F030 PB5 */
#define M8_MISO_PIN       GPIO_PIN_14   /* SPI2MISO <- F030 PB4 */
#define M8_CS_PORT        GPIOB
#define M8_CS_PIN         GPIO_PIN_2    /* MG8SS, active low    */
#define M8_HALF_US        10u           /* 50 kHz SCK           */

/* RS-485 driver enable */
#define RS485_DE_PORT     GPIOA
#define RS485_DE_PIN      GPIO_PIN_15

/* EEPROM AT24CS04 address pins driven by the MCU */
#define EE_A_PORT         GPIOB
#define EE_A_PINS         (GPIO_PIN_8 | GPIO_PIN_9)
#define EE_I2C_ADDR       0x50u         /* 7-bit, A2=A1=0       */
#define RTC_I2C_ADDR      0x52u         /* RV-3028-C7           */

/* WS2812 data line (LED terminal) */
#define WS_PORT           GPIOB
#define WS_PIN            GPIO_PIN_15

/* Buzzer, keys (display board) */
#define BUZ_PORT          GPIOD
#define BUZ_PIN           GPIO_PIN_11
#define KEY_PORT          GPIOE
#define KEY1_PIN          GPIO_PIN_7
#define KEY2_PIN          GPIO_PIN_8
#define KEY3_PIN          GPIO_PIN_15
#define KEY4_PIN          GPIO_PIN_10
#define KEY_ACTIVE_HIGH   1             /* TTP223B outputs: high when touched */

/* PWM output polarity: 1 = timer high -> output ON (via PC817) */
#define PWM_ACTIVE_HIGH   1

#endif
