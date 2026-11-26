/**
 * @file  bsp_stm32.c
 * @brief Implementation of bsp.h for STM32H723VET6 (HAL, STM32CubeIDE).
 *
 * Requires the CubeMX-generated handles: hi2c1, huart4, htim1, htim3 and
 * the USB device (CDC class) "hUsbDeviceHS" with CDC_Transmit_HS().
 * See firmware/CubeMX_SETUP.md.
 */
#include "app/bsp.h"
#include "app/app.h"
#include "app/board.h"
#include "usbd_cdc_if.h"
#include <string.h>

extern I2C_HandleTypeDef  hi2c1;
extern UART_HandleTypeDef huart4;
extern TIM_HandleTypeDef  htim1, htim3;
extern USBD_HandleTypeDef hUsbDeviceHS;

/* ------------------------------------------------------------------ */
/* microsecond delay on the DWT cycle counter                          */
/* ------------------------------------------------------------------ */
static void dwt_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

void bsp_delay_us(uint32_t us)
{
    uint32_t t0 = DWT->CYCCNT, n = us * (SystemCoreClock / 1000000u);
    while (DWT->CYCCNT - t0 < n) { }
}

uint32_t bsp_millis(void) { return HAL_GetTick(); }

static void gpio_out(GPIO_TypeDef *p, uint32_t pins, uint32_t speed)
{
    GPIO_InitTypeDef g = {0};
    g.Pin = pins; g.Mode = GPIO_MODE_OUTPUT_PP; g.Pull = GPIO_NOPULL; g.Speed = speed;
    HAL_GPIO_Init(p, &g);
}
static void gpio_in(GPIO_TypeDef *p, uint32_t pins, uint32_t pull)
{
    GPIO_InitTypeDef g = {0};
    g.Pin = pins; g.Mode = GPIO_MODE_INPUT; g.Pull = pull;
    HAL_GPIO_Init(p, &g);
}
#define PIN_SET(p, n)  ((p)->BSRR = (n))
#define PIN_CLR(p, n)  ((p)->BSRR = (uint32_t)(n) << 16)
#define PIN_GET(p, n)  (((p)->IDR & (n)) != 0u)

/* ------------------------------------------------------------------ */
/* RS-485 receive ring buffer (UART4 RX interrupt, 1 byte at a time)    */
/* ------------------------------------------------------------------ */
static uint8_t           s_rs_byte;
static volatile uint8_t  s_rs_buf[256];
static volatile uint16_t s_rs_h, s_rs_t;

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *h)
{
    if (h == &huart4) {
        uint16_t nx = (uint16_t)((s_rs_h + 1u) & 0xFFu);
        if (nx != s_rs_t) { s_rs_buf[s_rs_h] = s_rs_byte; s_rs_h = nx; }
        HAL_UART_Receive_IT(&huart4, &s_rs_byte, 1);
    }
}
void HAL_UART_ErrorCallback(UART_HandleTypeDef *h)
{
    if (h == &huart4) HAL_UART_Receive_IT(&huart4, &s_rs_byte, 1);
}

void bsp_rs485_config(uint32_t baud)
{
    HAL_UART_AbortReceive(&huart4);
    huart4.Init.BaudRate = baud;
    HAL_UART_Init(&huart4);
    s_rs_h = s_rs_t = 0;
    HAL_UART_Receive_IT(&huart4, &s_rs_byte, 1);
}

void bsp_rs485_send(const uint8_t *buf, uint16_t len)
{
    PIN_SET(RS485_DE_PORT, RS485_DE_PIN);                   /* DE = 1: transmit */
    HAL_UART_Transmit(&huart4, (uint8_t *)buf, len, 100);
    while (__HAL_UART_GET_FLAG(&huart4, UART_FLAG_TC) == RESET) { }
    PIN_CLR(RS485_DE_PORT, RS485_DE_PIN);                   /* DE = 0: receive  */
}

int bsp_rs485_getc(void)
{
    if (s_rs_t == s_rs_h) return -1;
    int c = s_rs_buf[s_rs_t];
    s_rs_t = (uint16_t)((s_rs_t + 1u) & 0xFFu);
    return c;
}

/* ------------------------------------------------------------------ */
/* init                                                                 */
/* ------------------------------------------------------------------ */
void bsp_init(void)
{
    dwt_init();
    __HAL_RCC_GPIOA_CLK_ENABLE(); __HAL_RCC_GPIOB_CLK_ENABLE(); __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE(); __HAL_RCC_GPIOE_CLK_ENABLE();

    /* 74AHC595: /OE = 1 (outputs disabled) until the first valid write */
    PIN_SET(HC595_PORT, HC595_OE_PIN);
    gpio_out(HC595_PORT, HC595_LAT_PIN | HC595_CLK_PIN | HC595_SER_PIN | HC595_OE_PIN, GPIO_SPEED_FREQ_LOW);

    /* MCP3208 lines */
    PIN_SET(ADC_PORT, ADC_SEL_PIN);
    gpio_out(ADC_PORT, ADC_DIN_PIN | ADC_CLK_PIN | ADC_SEL_PIN, GPIO_SPEED_FREQ_LOW);
    gpio_in(ADC_PORT, ADC_DOUT_PIN, GPIO_NOPULL);

    /* M8SPI */
    PIN_SET(M8_CS_PORT, M8_CS_PIN);
    gpio_out(M8_CS_PORT, M8_CS_PIN, GPIO_SPEED_FREQ_LOW);
    gpio_out(M8_PORT, M8_SCK_PIN | M8_MOSI_PIN, GPIO_SPEED_FREQ_LOW);
    gpio_in(M8_PORT, M8_MISO_PIN, GPIO_PULLUP);

    /* RS-485 DE, EEPROM address pins, buzzer, WS2812 */
    PIN_CLR(RS485_DE_PORT, RS485_DE_PIN); gpio_out(RS485_DE_PORT, RS485_DE_PIN, GPIO_SPEED_FREQ_LOW);
    PIN_CLR(EE_A_PORT, EE_A_PINS);        gpio_out(EE_A_PORT, EE_A_PINS, GPIO_SPEED_FREQ_LOW);
    PIN_CLR(BUZ_PORT, BUZ_PIN);           gpio_out(BUZ_PORT, BUZ_PIN, GPIO_SPEED_FREQ_LOW);
    PIN_CLR(WS_PORT, WS_PIN);             gpio_out(WS_PORT, WS_PIN, GPIO_SPEED_FREQ_VERY_HIGH);

    /* keys */
    gpio_in(KEY_PORT, KEY1_PIN | KEY2_PIN | KEY3_PIN | KEY4_PIN,
            KEY_ACTIVE_HIGH ? GPIO_PULLDOWN : GPIO_PULLUP);

    /* PWM: timers count at 1 MHz (set Prescaler in CubeMX), see bsp_pwm_set */
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_4);   /* PWM0 PB1  -> PW1 */
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_3);   /* PWM1 PB0  -> PW2 */
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_2);   /* PWM2 PE11 -> PW3 */
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);   /* PWM3 PE9  -> PW4 */
    for (uint8_t i = 0; i < BSP_PWM_CH; i++) bsp_pwm_set(i, 0);

    bsp_rs485_config(9600);
}

/* ------------------------------------------------------------------ */
/* relays via 74AHC595 (QA..QE = K1..K5, QF/QG = LED D1/D2)             */
/* ------------------------------------------------------------------ */
void bsp_relays_write(uint8_t k_mask, uint8_t led_mask)
{
    uint8_t v = (uint8_t)((k_mask & 0x1Fu) | ((led_mask & 0x03u) << 5));
    for (int i = 7; i >= 0; i--) {                  /* QH first, QA last */
        if (v & (1u << i)) PIN_SET(HC595_PORT, HC595_SER_PIN); else PIN_CLR(HC595_PORT, HC595_SER_PIN);
        bsp_delay_us(1); PIN_SET(HC595_PORT, HC595_CLK_PIN);
        bsp_delay_us(1); PIN_CLR(HC595_PORT, HC595_CLK_PIN);
    }
    PIN_SET(HC595_PORT, HC595_LAT_PIN); bsp_delay_us(1); PIN_CLR(HC595_PORT, HC595_LAT_PIN);
}

void bsp_outputs_enable(bool en)
{
    if (en) PIN_CLR(HC595_PORT, HC595_OE_PIN); else PIN_SET(HC595_PORT, HC595_OE_PIN);
}

/* ------------------------------------------------------------------ */
/* PWM                                                                   */
/* ------------------------------------------------------------------ */
#include "app/params.h"
void bsp_pwm_set(uint8_t ch, uint16_t permille)
{
    static TIM_HandleTypeDef *const tim[4] = { &htim3, &htim3, &htim1, &htim1 };
    static const uint32_t chan[4] = { TIM_CHANNEL_4, TIM_CHANNEL_3, TIM_CHANNEL_2, TIM_CHANNEL_1 };
    if (ch >= BSP_PWM_CH) return;
    uint32_t arr = 1000000u / (uint32_t)par(P_PWM_HZ) - 1u;   /* 1 MHz timer clock */
    if (__HAL_TIM_GET_AUTORELOAD(tim[ch]) != arr) __HAL_TIM_SET_AUTORELOAD(tim[ch], arr);
    if (permille > 1000u) permille = 1000u;
    uint32_t ccr = (arr + 1u) * permille / 1000u;
    if (!PWM_ACTIVE_HIGH) ccr = arr + 1u - ccr;
    __HAL_TIM_SET_COMPARE(tim[ch], chan[ch], ccr);
}

/* ------------------------------------------------------------------ */
/* WS2812: bit-banged with the cycle counter, interrupts disabled        */
/* ------------------------------------------------------------------ */
void bsp_ws2812_write(const uint8_t *grb, uint16_t n)
{
    static uint8_t last[BSP_WS2812_MAX * 3]; static uint16_t last_n = 0xFFFF;
    if (n > BSP_WS2812_MAX) n = BSP_WS2812_MAX;
    if (n == last_n && memcmp(last, grb, (size_t)n * 3u) == 0) return;   /* unchanged */
    memcpy(last, grb, (size_t)n * 3u); last_n = n;

    const uint32_t f = SystemCoreClock / 1000000u;            /* cycles per µs */
    const uint32_t t0h = f * 40u / 100u, t1h = f * 80u / 100u, tbit = f * 125u / 100u;
    uint32_t prim = __get_PRIMASK(); __disable_irq();
    for (uint32_t i = 0; i < (uint32_t)n * 3u; i++) {
        for (uint8_t m = 0x80; m; m >>= 1) {
            uint32_t t = DWT->CYCCNT;
            WS_PORT->BSRR = WS_PIN;
            uint32_t th = (grb[i] & m) ? t1h : t0h;
            while (DWT->CYCCNT - t < th) { }
            WS_PORT->BSRR = (uint32_t)WS_PIN << 16;
            while (DWT->CYCCNT - t < tbit) { }
        }
    }
    if (!prim) __enable_irq();
    bsp_delay_us(300);                                        /* reset > 280 µs */
}

void bsp_buzzer(bool on) { if (on) PIN_SET(BUZ_PORT, BUZ_PIN); else PIN_CLR(BUZ_PORT, BUZ_PIN); }

uint8_t bsp_keys(void)
{
    uint8_t k = 0;
    if (PIN_GET(KEY_PORT, KEY1_PIN)) k |= 1u;
    if (PIN_GET(KEY_PORT, KEY2_PIN)) k |= 2u;
    if (PIN_GET(KEY_PORT, KEY3_PIN)) k |= 4u;
    if (PIN_GET(KEY_PORT, KEY4_PIN)) k |= 8u;
    return KEY_ACTIVE_HIGH ? k : (uint8_t)(~k & 0x0Fu);
}

/* ------------------------------------------------------------------ */
/* MCP3208: CS falling edge starts a conversion; one chip is always     */
/* selected (U21 /CS = NOT HC51I), so every transfer toggles HC51I.     */
/* ------------------------------------------------------------------ */
static uint16_t mcp3208_read(uint8_t chip, uint8_t ch)
{
    /* chip 0 = U17 (selected by HC51I = 0), chip 1 = U21 (HC51I = 1) */
    if (chip == 0) { PIN_SET(ADC_PORT, ADC_SEL_PIN); bsp_delay_us(2); PIN_CLR(ADC_PORT, ADC_SEL_PIN); }
    else           { PIN_CLR(ADC_PORT, ADC_SEL_PIN); bsp_delay_us(2); PIN_SET(ADC_PORT, ADC_SEL_PIN); }
    bsp_delay_us(2);
    /* start bit, SGL/DIFF = 1, D2 D1 D0 */
    uint8_t cmd = (uint8_t)(0x18u | (ch & 7u));
    uint16_t v = 0;
    for (int i = 4; i >= 0; i--) {
        if (cmd & (1u << i)) PIN_SET(ADC_PORT, ADC_DIN_PIN); else PIN_CLR(ADC_PORT, ADC_DIN_PIN);
        bsp_delay_us(1); PIN_SET(ADC_PORT, ADC_CLK_PIN); bsp_delay_us(1); PIN_CLR(ADC_PORT, ADC_CLK_PIN);
    }
    /* 1 clock: end of sampling + null bit, then 12 data bits B11..B0.
     * Data change on the falling edge, so each bit is read after it. */
    for (int i = 0; i < 13; i++) {
        bsp_delay_us(1); PIN_SET(ADC_PORT, ADC_CLK_PIN); bsp_delay_us(1); PIN_CLR(ADC_PORT, ADC_CLK_PIN);
        if (i >= 1) { bsp_delay_us(1); v = (uint16_t)((v << 1) | (PIN_GET(ADC_PORT, ADC_DOUT_PIN) ? 1u : 0u)); }
    }
    return (uint16_t)(v & 0x0FFFu);
}

bool bsp_adc_read_all(uint16_t raw[BSP_ADC_CH])
{
    for (uint8_t i = 0; i < 8; i++) raw[i]     = mcp3208_read(0, i);
    for (uint8_t i = 0; i < 8; i++) raw[8 + i] = mcp3208_read(1, i);
    PIN_SET(ADC_PORT, ADC_SEL_PIN);      /* idle: U21 selected, no clocks */
    return true;
}

/* ------------------------------------------------------------------ */
/* M8SPI: bit-banged SPI master, mode 0, MSB first                      */
/* ------------------------------------------------------------------ */
void bsp_m8spi_xfer(const uint8_t *tx, uint8_t *rx, uint16_t len)
{
    PIN_CLR(M8_CS_PORT, M8_CS_PIN);
    bsp_delay_us(50);
    for (uint16_t b = 0; b < len; b++) {
        uint8_t o = tx[b], in = 0;
        for (int i = 7; i >= 0; i--) {
            if (o & (1u << i)) PIN_SET(M8_PORT, M8_MOSI_PIN); else PIN_CLR(M8_PORT, M8_MOSI_PIN);
            bsp_delay_us(M8_HALF_US);
            PIN_SET(M8_PORT, M8_SCK_PIN);
            in = (uint8_t)((in << 1) | (PIN_GET(M8_PORT, M8_MISO_PIN) ? 1u : 0u));
            bsp_delay_us(M8_HALF_US);
            PIN_CLR(M8_PORT, M8_SCK_PIN);
        }
        rx[b] = in;
        bsp_delay_us(30);                /* inter-byte gap for the slave */
    }
    PIN_SET(M8_CS_PORT, M8_CS_PIN);
}

/* ------------------------------------------------------------------ */
/* EEPROM AT24CS04: 512 bytes, 16-byte pages, block bit in the address  */
/* ------------------------------------------------------------------ */
bool bsp_eeprom_read(uint16_t addr, uint8_t *buf, uint16_t len)
{
    while (len) {
        uint16_t n = (uint16_t)(256u - (addr & 0xFFu)); if (n > len) n = len;
        uint16_t dev = (uint16_t)((EE_I2C_ADDR | (addr >> 8)) << 1);
        if (HAL_I2C_Mem_Read(&hi2c1, dev, addr & 0xFFu, I2C_MEMADD_SIZE_8BIT, buf, n, 50) != HAL_OK) return false;
        addr = (uint16_t)(addr + n); buf += n; len = (uint16_t)(len - n);
    }
    return true;
}

bool bsp_eeprom_write(uint16_t addr, const uint8_t *buf, uint16_t len)
{
    while (len) {
        uint16_t n = (uint16_t)(16u - (addr & 0x0Fu)); if (n > len) n = len;
        uint16_t dev = (uint16_t)((EE_I2C_ADDR | (addr >> 8)) << 1);
        if (HAL_I2C_Mem_Write(&hi2c1, dev, addr & 0xFFu, I2C_MEMADD_SIZE_8BIT, (uint8_t *)buf, n, 50) != HAL_OK) return false;
        if (HAL_I2C_IsDeviceReady(&hi2c1, dev, 20, 20) != HAL_OK) return false;   /* write cycle <= 5 ms */
        addr = (uint16_t)(addr + n); buf += n; len = (uint16_t)(len - n);
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* RTC RV-3028-C7: registers 00h..06h = s, min, h, weekday, date, month, year (BCD) */
/* ------------------------------------------------------------------ */
static uint8_t bcd2bin(uint8_t b) { return (uint8_t)((b >> 4) * 10u + (b & 0x0Fu)); }
static uint8_t bin2bcd(uint8_t b) { return (uint8_t)(((b / 10u) << 4) | (b % 10u)); }

bool bsp_rtc_get(bsp_time_t *t)
{
    uint8_t r[7];
    if (HAL_I2C_Mem_Read(&hi2c1, RTC_I2C_ADDR << 1, 0x00, I2C_MEMADD_SIZE_8BIT, r, 7, 20) != HAL_OK) return false;
    t->sec = bcd2bin(r[0] & 0x7Fu); t->min = bcd2bin(r[1] & 0x7Fu); t->hour = bcd2bin(r[2] & 0x3Fu);
    t->day = bcd2bin(r[4] & 0x3Fu); t->month = bcd2bin(r[5] & 0x1Fu); t->year = (uint16_t)(2000u + bcd2bin(r[6]));
    return t->month >= 1u && t->month <= 12u;
}

bool bsp_rtc_set(const bsp_time_t *t)
{
    uint8_t r[7] = { bin2bcd(t->sec), bin2bcd(t->min), bin2bcd(t->hour), 0,
                     bin2bcd(t->day), bin2bcd(t->month), bin2bcd((uint8_t)(t->year - 2000u)) };
    return HAL_I2C_Mem_Write(&hi2c1, RTC_I2C_ADDR << 1, 0x00, I2C_MEMADD_SIZE_8BIT, r, 7, 20) == HAL_OK;
}

/* ------------------------------------------------------------------ */
/* USB CDC: transmit ring buffer, flushed from bsp_poll()               */
/* ------------------------------------------------------------------ */
#define USB_TX_RING 4096u
static uint8_t  s_tx[USB_TX_RING];
static uint16_t s_tx_h, s_tx_t;
static uint8_t  s_chunk[256];

void bsp_usb_write(const char *s, uint16_t len)
{
    if (hUsbDeviceHS.dev_state != USBD_STATE_CONFIGURED) return;   /* no host: drop */
    while (len--) {
        uint16_t nx = (uint16_t)((s_tx_h + 1u) % USB_TX_RING);
        if (nx == s_tx_t) return;                                    /* full: drop    */
        s_tx[s_tx_h] = (uint8_t)*s++; s_tx_h = nx;
    }
}

void bsp_poll(void)
{
    if (s_tx_t == s_tx_h || hUsbDeviceHS.dev_state != USBD_STATE_CONFIGURED) return;
    USBD_CDC_HandleTypeDef *cdc = (USBD_CDC_HandleTypeDef *)hUsbDeviceHS.pClassData;
    if (cdc == NULL || cdc->TxState != 0u) return;                  /* previous packet busy */
    uint16_t n = 0;
    while (s_tx_t != s_tx_h && n < sizeof s_chunk) { s_chunk[n++] = s_tx[s_tx_t]; s_tx_t = (uint16_t)((s_tx_t + 1u) % USB_TX_RING); }
    CDC_Transmit_HS(s_chunk, n);
}

uint32_t bsp_uid(void) { return HAL_GetUIDw0() ^ HAL_GetUIDw1() ^ HAL_GetUIDw2(); }
