#include "stm32f4xx.h"
#include <stdio.h>
#include <string.h>

/* =============================================================
   BOARD PIN MAP (STM32F405RGTx)
   I2C2  PB10=SCL  PB11=SDA   -> LCD (0x27) + RTC DS3231 (0x68)
   ADC1  PC2=ch12 (POT1)      PC1=ch11 (POT2)
   SW1=PB7  SW2=PB3  SW3=PB4  SW4=PA15  (Active LOW, pull-up)
   LED1=PC6  LED2=PB15  LED3=PB14  LED4=PB13  (Active LOW)
   BUZZER=PC9 (Active HIGH)
   Internal Flash Sector 7 (0x080E0000, 128KB) used as EEPROM emulation

   MODES:
   - NORMAL  : Clock + Date display
   - SW1 (1x): ADMIN -> POT1 value
   - SW1 (2x): ADMIN -> POT2 value
   - SW1 (3x): ADMIN -> LOG page (auto-scrolling through last 5 records)
   - SW1 (4x): wraps back to POT1
   - SW4     : instantly back to NORMAL (clock) mode
   - SW3     : manual save of a log record (works in any mode)
   =============================================================*/

/* -------- LCD -------- */
#define LCD_ADDR        0x27
#define LCD_CLEAR       0x01
#define LCD_HOME        0x02
#define LCD_DDRAM       0x80
#define LCD_BL          0x08
#define LCD_EN          0x04
#define LCD_RS_CMD      0x00
#define LCD_RS_DATA     0x01

/* -------- RTC -------- */
#define RTC_ADDR        0x68

/* -------- Modes -------- */
typedef enum {
    MODE_NORMAL = 0,
    MODE_POT1   = 1,
    MODE_POT2   = 2,
    MODE_LOG    = 3,
    ADMIN_MODE_COUNT = 3
} Mode;

/* -------- Button pins -------- */
#define SW1_PIN  7
#define SW2_PIN  3
#define SW3_PIN  4
#define SW4_PIN  15

/* -------- LED pins -------- */
#define LED1_PIN 6
#define LED2_PIN 15
#define LED3_PIN 14
#define LED4_PIN 13

/* =====================================================================
   SYSTICK
   ===================================================================== */
volatile uint32_t g_ms = 0;
void SysTick_Handler(void) { g_ms++; }
static uint32_t Ms(void) { return g_ms; }
void DelayUs(uint32_t us) { us *= 21; while (us--) __NOP(); }
void DelayMs(uint32_t ms) { uint32_t s = Ms(); while ((Ms()-s) < ms); }

/* =====================================================================
   SYSTEM CLOCK
   ===================================================================== */
void SystemClock_Config(void) {
    RCC->CR |= RCC_CR_HSION;
    while (!(RCC->CR & RCC_CR_HSIRDY));
    FLASH->ACR = FLASH_ACR_ICEN|FLASH_ACR_DCEN|FLASH_ACR_PRFTEN|FLASH_ACR_LATENCY_2WS;
    RCC->PLLCFGR = (84<<RCC_PLLCFGR_PLLN_Pos)|(8<<RCC_PLLCFGR_PLLM_Pos)
                  |(0<<RCC_PLLCFGR_PLLP_Pos)|(7<<RCC_PLLCFGR_PLLQ_Pos)
                  |RCC_PLLCFGR_PLLSRC_HSI;
    RCC->CFGR |= RCC_CFGR_PPRE1_DIV2 | RCC_CFGR_PPRE2_DIV1;
    RCC->CR |= RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY));
    RCC->CFGR |= RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL);
}

/* =====================================================================
   GPIO INIT
   ===================================================================== */
void GPIO_Init(void) {
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN|RCC_AHB1ENR_GPIOBEN|RCC_AHB1ENR_GPIOCEN;

    GPIOC->MODER &= ~(3u<<(LED1_PIN*2)); GPIOC->MODER |= (1u<<(LED1_PIN*2));
    GPIOC->OTYPER &= ~(1u<<LED1_PIN);    GPIOC->BSRR   =  (1u<<LED1_PIN);

    GPIOB->MODER &= ~((3u<<(LED2_PIN*2))|(3u<<(LED3_PIN*2))|(3u<<(LED4_PIN*2)));
    GPIOB->MODER |=  ((1u<<(LED2_PIN*2))|(1u<<(LED3_PIN*2))|(1u<<(LED4_PIN*2)));
    GPIOB->OTYPER &= ~((1u<<LED2_PIN)|(1u<<LED3_PIN)|(1u<<LED4_PIN));
    GPIOB->BSRR = (1u<<LED2_PIN)|(1u<<LED3_PIN)|(1u<<LED4_PIN);

    GPIOB->MODER &= ~((3u<<(SW1_PIN*2))|(3u<<(SW2_PIN*2))|(3u<<(SW3_PIN*2)));
    GPIOB->PUPDR &= ~((3u<<(SW1_PIN*2))|(3u<<(SW2_PIN*2))|(3u<<(SW3_PIN*2)));
    GPIOB->PUPDR |=  ((1u<<(SW1_PIN*2))|(1u<<(SW2_PIN*2))|(1u<<(SW3_PIN*2)));
    GPIOA->MODER &= ~(3u<<(SW4_PIN*2));
    GPIOA->PUPDR &= ~(3u<<(SW4_PIN*2)); GPIOA->PUPDR |= (1u<<(SW4_PIN*2));

    GPIOC->MODER |= (3u<<(2*2))|(3u<<(1*2));
    GPIOC->PUPDR &= ~((3u<<(2*2))|(3u<<(1*2)));

    GPIOC->MODER &= ~(3u<<(9*2)); GPIOC->MODER |= (1u<<(9*2));
    GPIOC->OTYPER &= ~(1u<<9);    GPIOC->BSRR   =  (1u<<(9+16));
}

static void LED_Set(uint8_t led, uint8_t on) {
    switch(led) {
        case 1: GPIOC->BSRR = on?(1u<<(LED1_PIN+16)):(1u<<LED1_PIN); break;
        case 2: GPIOB->BSRR = on?(1u<<(LED2_PIN+16)):(1u<<LED2_PIN); break;
        case 3: GPIOB->BSRR = on?(1u<<(LED3_PIN+16)):(1u<<LED3_PIN); break;
        case 4: GPIOB->BSRR = on?(1u<<(LED4_PIN+16)):(1u<<LED4_PIN); break;
    }
}
static void LEDs_AllOff(void) {
    LED_Set(1,0); LED_Set(2,0); LED_Set(3,0); LED_Set(4,0);
}
static void Buzzer_Beep(uint16_t ms) {
    GPIOC->BSRR = (1u<<9); DelayMs(ms);
    GPIOC->BSRR = (1u<<(9+16));
}

/* =====================================================================
   BUTTON EDGE DETECTOR
   ===================================================================== */
typedef struct { uint8_t state; uint32_t timer; } BtnFSM;

static uint8_t Btn_Poll(BtnFSM *b, uint8_t raw) {
    uint8_t fired = 0;
    switch (b->state) {
        case 0: if (raw)  { b->timer=Ms(); b->state=1; } break;
        case 1:
            if (!raw) { b->state=0; }
            else if ((Ms()-b->timer)>=30) { fired=1; b->state=2; }
            break;
        case 2: if (!raw) { b->timer=Ms(); b->state=3; } break;
        case 3:
            if (raw)  { b->state=2; }
            else if ((Ms()-b->timer)>=30) { b->state=0; }
            break;
    }
    return fired;
}
static uint8_t SW_Raw(uint8_t sw) {
    switch(sw) {
        case 1: return !(GPIOB->IDR & (1u<<SW1_PIN));
        case 2: return !(GPIOB->IDR & (1u<<SW2_PIN));
        case 3: return !(GPIOB->IDR & (1u<<SW3_PIN));
        case 4: return !(GPIOA->IDR & (1u<<SW4_PIN));
    }
    return 0;
}

/* =====================================================================
   ADC1
   ===================================================================== */
void ADC_Init(void) {
    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;
    ADC->CCR &= ~ADC_CCR_ADCPRE; ADC->CCR |= ADC_CCR_ADCPRE_0;
    ADC1->CR1 = 0; ADC1->CR2 = 0;
    ADC1->CR1 &= ~ADC_CR1_RES;
    ADC1->CR2 &= ~ADC_CR2_ALIGN;
    ADC1->CR2 &= ~ADC_CR2_CONT;
    ADC1->SMPR1 &= ~((7u<<(3*2))|(7u<<(3*1)));
    ADC1->SMPR1 |=  ((3u<<(3*2))|(3u<<(3*1)));
    ADC1->SQR1 = 0; ADC1->SQR3 = 12;
    ADC1->CR2 |= ADC_CR2_ADON; DelayUs(10);
}
static uint16_t ADC_Read_Ch(uint8_t ch) {
    ADC1->SQR3 = ch;
    ADC1->CR2 |= ADC_CR2_SWSTART;
    while (!(ADC1->SR & ADC_SR_EOC));
    return (uint16_t)(ADC1->DR & 0x0FFF);
}

/* =====================================================================
   INTERNAL FLASH "EEPROM" EMULATION
   Sector 7: 0x080E0000, 128KB on STM32F405RGTx

   Flash layout (all 32-bit words):
     Offset 0x00 : magic word (0xA55AA55A) — confirms the sector was
                   written by this firmware (not erased/blank)
     Offset 0x04 : head index byte (0..4), rest of word unused
     Offset 0x08 : count of valid records (0..5)
     Offset 0x0C : reserved (written 0)
     Offset 0x10 + i*8 : record[i] word0  (i = 0..4)
     Offset 0x14 + i*8 : record[i] word1

   Pack layout:
     w0 = sec[7:0] | min[15:8] | hour[23:16] | day[31:24]
     w1 = month[7:0] | pot2_pct[15:8] | pot1[31:16]
          (pot1 is 12-bit max 4095, fits cleanly in bits [31:16])

   Storing the count explicitly in flash means Log_Init() never has
   to guess which slots are "real" vs. zero-padded empty — the exact
   number of valid records is always known after a power cycle.
   ===================================================================== */
#define FLASH_LOG_BASE    ((uint32_t)0x080E0000)
#define FLASH_SECTOR_NUM  7
#define EE_LOG_MAX        5
#define FLASH_MAGIC       0xA55AA55AUL

/* Word offsets inside the sector */
#define FW_MAGIC   0   /* offset 0x00 */
#define FW_HEAD    1   /* offset 0x04 */
#define FW_COUNT   2   /* offset 0x08 */
#define FW_RSVD    3   /* offset 0x0C */
#define FW_REC0    4   /* offset 0x10 — first record w0 */

typedef struct {
    uint8_t  sec, min, hour, day, month;
    uint16_t pot1;
    uint8_t  pot2_pct;
    uint8_t  pad;
} LogRecord;

static uint8_t   g_logHead  = 0;
static uint8_t   g_logCount = 0;
static LogRecord g_logBuf[EE_LOG_MAX];

/* ---- Flash primitives ---- */
static void Flash_Unlock(void) {
    if (FLASH->CR & FLASH_CR_LOCK) {
        FLASH->KEYR = 0x45670123;
        FLASH->KEYR = 0xCDEF89AB;
    }
}
static void Flash_Lock(void) { FLASH->CR |= FLASH_CR_LOCK; }
static void Flash_WaitBusy(void) { while (FLASH->SR & FLASH_SR_BSY); }

static void Flash_EraseSector7(void) {
    Flash_WaitBusy();
    Flash_Unlock();
    FLASH->CR &= ~FLASH_CR_PSIZE;
    FLASH->CR |= FLASH_CR_PSIZE_1;
    FLASH->CR &= ~(0xF << 3);
    FLASH->CR |= (FLASH_SECTOR_NUM << 3);
    FLASH->CR |= FLASH_CR_SER;
    FLASH->CR |= FLASH_CR_STRT;
    Flash_WaitBusy();
    FLASH->CR &= ~FLASH_CR_SER;
    Flash_Lock();
}

static void Flash_WriteWord(uint32_t addr, uint32_t data) {
    Flash_WaitBusy();
    Flash_Unlock();
    FLASH->CR &= ~FLASH_CR_PSIZE;
    FLASH->CR |= FLASH_CR_PSIZE_1;
    FLASH->CR |= FLASH_CR_PG;
    *(volatile uint32_t*)addr = data;
    Flash_WaitBusy();
    FLASH->CR &= ~FLASH_CR_PG;
    Flash_Lock();
}

/* ---- Pack / Unpack ---- */
static void Pack(const LogRecord *r, uint32_t *w0, uint32_t *w1) {
    *w0 = ((uint32_t)r->sec)
        | ((uint32_t)r->min      <<  8)
        | ((uint32_t)r->hour     << 16)
        | ((uint32_t)r->day      << 24);
    *w1 = ((uint32_t)r->month)
        | ((uint32_t)r->pot2_pct <<  8)
        | ((uint32_t)r->pot1     << 16);
}
static void Unpack(LogRecord *r, uint32_t w0, uint32_t w1) {
    r->sec      = (uint8_t) (w0         & 0xFF);
    r->min      = (uint8_t)((w0 >>  8)  & 0xFF);
    r->hour     = (uint8_t)((w0 >> 16)  & 0xFF);
    r->day      = (uint8_t)((w0 >> 24)  & 0xFF);
    r->month    = (uint8_t) (w1         & 0xFF);
    r->pot2_pct = (uint8_t)((w1 >>  8)  & 0xFF);
    r->pot1     = (uint16_t)((w1 >> 16) & 0xFFFF);
    r->pad      = 0;
}

/* -----------------------------------------------------------------------
   Log_Init — load from flash at boot.

   FIX: We now store the magic word + explicit count in flash.
   If magic is absent (blank sector) we start fresh.
   We read back ONLY g_logCount records, so zero-padded empty slots
   are never mistaken for real records.
   ----------------------------------------------------------------------- */
static void Log_Init(void) {
    volatile uint32_t *flash = (volatile uint32_t*)FLASH_LOG_BASE;

    /* No valid data in flash */
    if (flash[FW_MAGIC] != FLASH_MAGIC) {
        g_logHead  = 0;
        g_logCount = 0;
        memset(g_logBuf, 0, sizeof(g_logBuf));
        return;
    }

    g_logHead = (uint8_t)(flash[FW_HEAD] & 0xFF);
    if (g_logHead >= EE_LOG_MAX) g_logHead = 0;

    g_logCount = (uint8_t)(flash[FW_COUNT] & 0xFF);
    if (g_logCount > EE_LOG_MAX) g_logCount = EE_LOG_MAX;

    /* Read ALL 5 physical slots into RAM (only g_logCount are valid,
       but we need the whole ring for correct circular indexing) */
    memset(g_logBuf, 0, sizeof(g_logBuf));
    for (uint8_t i = 0; i < EE_LOG_MAX; i++) {
        uint32_t w0 = flash[FW_REC0 + i*2];
        uint32_t w1 = flash[FW_REC0 + i*2 + 1];
        /* Only unpack slots that actually hold real records.
           Determine if slot i is within the valid range of the ring. */
        uint8_t validSlot = 0;
        if (g_logCount == EE_LOG_MAX) {
            /* Ring is full — every slot is valid */
            validSlot = 1;
        } else {
            /* Ring is partially filled: valid slots are
               (EE_LOG_MAX - g_logCount + j) % EE_LOG_MAX  for j=0..count-1
               which simplifies to: slot index < g_logHead (writing wraps)
               OR g_logHead == 0 and i < g_logCount */
            /* Simpler: just check if i is one of the occupied positions */
            for (uint8_t j = 0; j < g_logCount; j++) {
                uint8_t idx = (uint8_t)((g_logHead + EE_LOG_MAX - g_logCount + j) % EE_LOG_MAX);
                if (idx == i) { validSlot = 1; break; }
            }
        }
        if (validSlot) {
            Unpack(&g_logBuf[i], w0, w1);
        }
    }
}

/* -----------------------------------------------------------------------
   Log_Write — erase sector and rewrite everything including
   magic + count so Log_Init() always knows exactly how many
   records are real.
   ----------------------------------------------------------------------- */
static void Log_Write(const LogRecord *r) {
    g_logBuf[g_logHead] = *r;
    g_logHead = (g_logHead + 1) % EE_LOG_MAX;
    if (g_logCount < EE_LOG_MAX) g_logCount++;

    Flash_EraseSector7();

    /* Write header */
    Flash_WriteWord(FLASH_LOG_BASE + FW_MAGIC * 4, FLASH_MAGIC);
    Flash_WriteWord(FLASH_LOG_BASE + FW_HEAD  * 4, (uint32_t)g_logHead);
    Flash_WriteWord(FLASH_LOG_BASE + FW_COUNT * 4, (uint32_t)g_logCount);
    Flash_WriteWord(FLASH_LOG_BASE + FW_RSVD  * 4, 0x00000000UL);

    /* Write all 5 record slots (empty ones are written as 0) */
    for (uint8_t i = 0; i < EE_LOG_MAX; i++) {
        uint32_t w0 = 0, w1 = 0;
        /* Only pack real records; leave empty slots as 0 */
        uint8_t validSlot = 0;
        if (g_logCount == EE_LOG_MAX) {
            validSlot = 1;
        } else {
            for (uint8_t j = 0; j < g_logCount; j++) {
                uint8_t idx = (uint8_t)((g_logHead + EE_LOG_MAX - g_logCount + j) % EE_LOG_MAX);
                if (idx == i) { validSlot = 1; break; }
            }
        }
        if (validSlot) Pack(&g_logBuf[i], &w0, &w1);
        Flash_WriteWord(FLASH_LOG_BASE + (FW_REC0 + i*2)     * 4, w0);
        Flash_WriteWord(FLASH_LOG_BASE + (FW_REC0 + i*2 + 1) * 4, w1);
    }
}

/* slot 0 = oldest, slot (count-1) = newest */
static void Log_Read(uint8_t slot, LogRecord *r) {
    if (g_logCount == 0) { memset(r, 0, sizeof(LogRecord)); return; }
    if (slot >= g_logCount) slot = g_logCount - 1;
    uint8_t idx = (uint8_t)((g_logHead + EE_LOG_MAX - g_logCount + slot) % EE_LOG_MAX);
    *r = g_logBuf[idx];
}

/* =====================================================================
   I2C2 - PB10=SCL, PB11=SDA
   ===================================================================== */
static void I2C2_Recover(void) {
    I2C2->CR1 |= I2C_CR1_SWRST; DelayMs(1); I2C2->CR1 &= ~I2C_CR1_SWRST;
    I2C2->CR2=42; I2C2->CCR=210; I2C2->TRISE=43; I2C2->CR1|=I2C_CR1_PE;
}
void I2C2_Init(void) {
    RCC->APB1ENR |= RCC_APB1ENR_I2C2EN;
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;
    for (volatile int i=0;i<1000;i++);
    GPIOB->MODER  &= ~((3u<<(10*2))|(3u<<(11*2)));
    GPIOB->MODER  |=  ((2u<<(10*2))|(2u<<(11*2)));
    GPIOB->OTYPER |=   (1u<<10)|(1u<<11);
    GPIOB->PUPDR  &= ~((3u<<(10*2))|(3u<<(11*2)));
    GPIOB->PUPDR  |=  ((1u<<(10*2))|(1u<<(11*2)));
    GPIOB->OSPEEDR|=   (3u<<(10*2))|(3u<<(11*2));
    GPIOB->AFR[1] &= ~((0xFu<<8)|(0xFu<<12));
    GPIOB->AFR[1] |=  ((4u<<8)|(4u<<12));
    I2C2->CR1=0; I2C2->CR1|=I2C_CR1_SWRST; DelayMs(1);
    I2C2->CR1 &= ~I2C_CR1_SWRST;
    I2C2->CR2=42; I2C2->CCR=210; I2C2->TRISE=43; I2C2->CR1|=I2C_CR1_PE;
}
static void I2C2_WriteByte(uint8_t dev, uint8_t data) {
    volatile uint32_t t;
    t=50000; while((I2C2->SR2&I2C_SR2_BUSY)&&t--); if(!t){I2C2_Recover();return;}
    I2C2->CR1|=I2C_CR1_START;
    t=50000; while(!(I2C2->SR1&I2C_SR1_SB)&&t--); if(!t)return;
    I2C2->DR=(dev<<1);
    t=50000; while(!(I2C2->SR1&I2C_SR1_ADDR)&&t--);
    if(!t){I2C2->CR1|=I2C_CR1_STOP;return;}
    (void)I2C2->SR1;(void)I2C2->SR2;
    t=50000; while(!(I2C2->SR1&I2C_SR1_TXE)&&t--); if(!t)return;
    I2C2->DR=data;
    t=50000; while(!(I2C2->SR1&I2C_SR1_BTF)&&t--);
    I2C2->CR1|=I2C_CR1_STOP;
}
static void I2C2_WriteReg(uint8_t dev, uint8_t reg, uint8_t val) {
    volatile uint32_t t;
    t=50000; while((I2C2->SR2&I2C_SR2_BUSY)&&t--); if(!t){I2C2_Recover();return;}
    I2C2->CR1|=I2C_CR1_START;
    t=50000; while(!(I2C2->SR1&I2C_SR1_SB)&&t--); if(!t)return;
    I2C2->DR=(dev<<1);
    t=50000; while(!(I2C2->SR1&I2C_SR1_ADDR)&&t--);
    if(!t){I2C2->CR1|=I2C_CR1_STOP;return;}
    (void)I2C2->SR1;(void)I2C2->SR2;
    t=50000; while(!(I2C2->SR1&I2C_SR1_TXE)&&t--); I2C2->DR=reg;
    t=50000; while(!(I2C2->SR1&I2C_SR1_TXE)&&t--); I2C2->DR=val;
    t=50000; while(!(I2C2->SR1&I2C_SR1_BTF)&&t--);
    I2C2->CR1|=I2C_CR1_STOP; DelayMs(1);
}
static uint8_t I2C2_ReadReg(uint8_t dev, uint8_t reg) {
    volatile uint32_t t; uint8_t data=0;
    t=50000; while((I2C2->SR2&I2C_SR2_BUSY)&&t--); if(!t){I2C2_Recover();return 0;}
    I2C2->CR1|=I2C_CR1_START;
    t=50000; while(!(I2C2->SR1&I2C_SR1_SB)&&t--); if(!t)return 0;
    I2C2->DR=(dev<<1);
    t=50000; while(!(I2C2->SR1&I2C_SR1_ADDR)&&t--);
    if(!t){I2C2->CR1|=I2C_CR1_STOP;return 0;}
    (void)I2C2->SR1;(void)I2C2->SR2;
    t=50000; while(!(I2C2->SR1&I2C_SR1_TXE)&&t--); if(!t)return 0;
    I2C2->DR=reg;
    t=50000; while(!(I2C2->SR1&I2C_SR1_BTF)&&t--); if(!t)return 0;
    I2C2->CR1|=I2C_CR1_START;
    t=50000; while(!(I2C2->SR1&I2C_SR1_SB)&&t--); if(!t)return 0;
    I2C2->DR=(dev<<1)|0x01;
    t=50000; while(!(I2C2->SR1&I2C_SR1_ADDR)&&t--);
    if(!t){I2C2->CR1|=I2C_CR1_STOP;return 0;}
    I2C2->CR1 &= ~I2C_CR1_ACK;
    (void)I2C2->SR1;(void)I2C2->SR2;
    I2C2->CR1|=I2C_CR1_STOP;
    t=50000; while(!(I2C2->SR1&I2C_SR1_RXNE)&&t--); if(!t)return 0;
    data=(uint8_t)I2C2->DR;
    I2C2->CR1|=I2C_CR1_ACK;
    return data;
}

/* =====================================================================
   RTC DS3231
   ===================================================================== */
static uint8_t B2D(uint8_t b){return((b>>4)*10)+(b&0x0F);}
static uint8_t D2B(uint8_t d){return((d/10)<<4)|(d%10);}
typedef struct { uint8_t sec,min,hour,day,month; uint16_t year; } RTC_Time;

void RTC_SetTime(uint8_t h,uint8_t m,uint8_t s){
    I2C2_WriteReg(RTC_ADDR,0x00,D2B(s));
    I2C2_WriteReg(RTC_ADDR,0x01,D2B(m));
    I2C2_WriteReg(RTC_ADDR,0x02,D2B(h));
}
void RTC_SetDate(uint8_t d,uint8_t mo,uint16_t y){
    I2C2_WriteReg(RTC_ADDR,0x04,D2B(d));
    I2C2_WriteReg(RTC_ADDR,0x05,D2B(mo));
    I2C2_WriteReg(RTC_ADDR,0x06,D2B((uint8_t)(y-2000)));
}
void RTC_Read(RTC_Time *t){
    t->sec  =B2D(I2C2_ReadReg(RTC_ADDR,0x00)&0x7F);
    t->min  =B2D(I2C2_ReadReg(RTC_ADDR,0x01)&0x7F);
    t->hour =B2D(I2C2_ReadReg(RTC_ADDR,0x02)&0x3F);
    t->day  =B2D(I2C2_ReadReg(RTC_ADDR,0x04)&0x3F);
    t->month=B2D(I2C2_ReadReg(RTC_ADDR,0x05)&0x1F);
    t->year =B2D(I2C2_ReadReg(RTC_ADDR,0x06))+2000;
}
void RTC_Init(void){
    DelayMs(100);
    uint8_t s=I2C2_ReadReg(RTC_ADDR,0x0F); s&=~0x80; I2C2_WriteReg(RTC_ADDR,0x0F,s);
    uint8_t c=I2C2_ReadReg(RTC_ADDR,0x0E); c&=~0x80; I2C2_WriteReg(RTC_ADDR,0x0E,c);
    DelayMs(50);
    /* Comment out after first flash */
    RTC_SetTime(10,30,0);
    RTC_SetDate(12,6,2026);
}

/* =====================================================================
   LCD DRIVER
   ===================================================================== */
static void LCD_4b(uint8_t data){
    uint8_t d=data|LCD_BL;
    I2C2_WriteByte(LCD_ADDR,d|LCD_EN); DelayUs(1);
    I2C2_WriteByte(LCD_ADDR,d&~LCD_EN); DelayUs(50);
}
static void LCD_Cmd(uint8_t cmd){
    LCD_4b((cmd&0xF0)|LCD_RS_CMD);
    LCD_4b(((cmd<<4)&0xF0)|LCD_RS_CMD);
    if(cmd==LCD_CLEAR||cmd==LCD_HOME) DelayMs(2); else DelayUs(100);
}
static void LCD_Data(uint8_t d){
    LCD_4b((d&0xF0)|LCD_RS_DATA);
    LCD_4b(((d<<4)&0xF0)|LCD_RS_DATA);
    DelayUs(100);
}
void LCD_Init(void){
    DelayMs(50);
    LCD_4b(0x30|LCD_RS_CMD);DelayMs(5);
    LCD_4b(0x30|LCD_RS_CMD);DelayUs(150);
    LCD_4b(0x30|LCD_RS_CMD);DelayUs(150);
    LCD_4b(0x20|LCD_RS_CMD);DelayMs(2);
    LCD_Cmd(0x28); LCD_Cmd(0x0C); LCD_Cmd(0x01); LCD_Cmd(0x06);
}
static void LCD_Pos(uint8_t row,uint8_t col){
    LCD_Cmd(LCD_DDRAM | ((row==0?0x00:0x40)+col));
}
static void LCD_Line(uint8_t row,const char *str){
    char buf[17]; snprintf(buf,sizeof(buf),"%-16s",str);
    LCD_Pos(row,0);
    for(const char *p=buf;*p;p++) LCD_Data((uint8_t)*p);
}
static void LCD_Clear_Display(void){ LCD_Cmd(LCD_CLEAR); }
static void LCD_LoadCustomChars(void){
    LCD_Cmd(0x40);
    for(uint8_t i=0;i<8;i++) LCD_Data(0x1F);
    LCD_Cmd(0x40+8);
    LCD_Data(0x1F);
    for(uint8_t i=0;i<6;i++) LCD_Data(0x00);
    LCD_Data(0x1F);
}

/* =====================================================================
   PAGE RENDERERS
   ===================================================================== */
static void Page_Clock(const RTC_Time *t){
    char L[17];
    uint8_t h12=t->hour%12; if(!h12)h12=12;
    const char *ap=(t->hour<12)?"AM":"PM";
    snprintf(L,sizeof(L)," %02d:%02d:%02d %s  ",h12,t->min,t->sec,ap);
    LCD_Line(0,L);
    snprintf(L,sizeof(L),"  %02d/%02d/%04d  ",t->day,t->month,t->year);
    LCD_Line(1,L);
}
static void Page_Pot1(uint16_t raw){
    char L[17];
    uint8_t pct=(uint8_t)((uint32_t)raw*100/4095);
    LCD_Line(0," ADMIN: POT1   ");
    snprintf(L,sizeof(L),"VAL:%4u (%3u%%)",raw,pct);
    LCD_Line(1,L);
}
static void Page_Pot2(uint16_t raw){
    char L[17];
    uint8_t pct=(uint8_t)((uint32_t)raw*100/4095);
    LCD_Line(0," ADMIN: POT2   ");
    snprintf(L,sizeof(L),"VAL:%4u (%3u%%)",raw,pct);
    LCD_Line(1,L);
}

/* slot 0 = newest, slot (count-1) = oldest (display order) */
static void Page_Log(uint8_t slot){
    char L[17];
    if (g_logCount == 0) {
        LCD_Line(0,"  NO LOG DATA  ");
        LCD_Line(1,"  PRESS SW3    ");
        return;
    }
    LogRecord r;
    /* Convert display slot (0=newest) to storage slot (0=oldest) */
    uint8_t readSlot = (uint8_t)(g_logCount - 1 - slot);
    Log_Read(readSlot, &r);
    snprintf(L,sizeof(L),"LOG%d %02d/%02d %02d:%02d",
             slot+1, r.day, r.month, r.hour, r.min);
    LCD_Line(0,L);
    snprintf(L,sizeof(L),"P1:%4u P2:%3u%%", r.pot1, r.pot2_pct);
    LCD_Line(1,L);
}

/* =====================================================================
   MAIN
   ===================================================================== */
int main(void) {
    SystemClock_Config();
    SysTick_Config(84000000/1000);

    GPIO_Init();
    I2C2_Init();
    ADC_Init();
    RTC_Init();
    LCD_Init();
    LCD_LoadCustomChars();
    Log_Init();

    LCD_Line(0," STM32F405RGTx ");
    LCD_Line(1,"  Multi-Display ");
    DelayMs(1500);
    LCD_Clear_Display();

    Mode    curMode    = MODE_NORMAL;
    uint8_t logSlot    = 0;
    uint8_t needRedraw = 1;
    uint8_t last_sec   = 0xFF;

    BtnFSM b1={0,0}, b2={0,0}, b3={0,0}, b4={0,0};

    uint32_t lastLogTime  = Ms();
    uint32_t lastScrollMs = Ms();

    RTC_Time now;
    uint16_t pot1=0, pot2=0;

    while (1) {
        uint8_t p1 = Btn_Poll(&b1, SW_Raw(1));
        uint8_t p2 = Btn_Poll(&b2, SW_Raw(2));
        uint8_t p3 = Btn_Poll(&b3, SW_Raw(3));
        uint8_t p4 = Btn_Poll(&b4, SW_Raw(4));

        if (p1) {
            if (curMode == MODE_NORMAL) {
                curMode = MODE_POT1;
            } else {
                curMode = (Mode)(((uint8_t)curMode % ADMIN_MODE_COUNT) + 1);
            }
            logSlot = 0;
            lastScrollMs = Ms();
            needRedraw = 1;
            LCD_Clear_Display();
            Buzzer_Beep(50);
        }

        if (p2) {
            Buzzer_Beep(30);
        }

        if (p3) {
            RTC_Read(&now);
            pot1 = ADC_Read_Ch(12);
            pot2 = ADC_Read_Ch(11);
            LogRecord r;
            r.sec=now.sec; r.min=now.min; r.hour=now.hour;
            r.day=now.day; r.month=now.month;
            r.pot1=pot1;
            r.pot2_pct=(uint8_t)((uint32_t)pot2*100/4095);
            r.pad=0;
            Log_Write(&r);
            LED_Set(1,1);LED_Set(2,1);LED_Set(3,1);LED_Set(4,1);
            Buzzer_Beep(200);
            LED_Set(1,0);LED_Set(2,0);LED_Set(3,0);LED_Set(4,0);
            needRedraw = 1;
        }

        if (p4) {
            curMode = MODE_NORMAL;
            logSlot = 0;
            needRedraw = 1;
            LCD_Clear_Display();
            Buzzer_Beep(50);
        }

        RTC_Read(&now);
        pot1 = ADC_Read_Ch(12);
        pot2 = ADC_Read_Ch(11);

        if ((Ms() - lastLogTime) >= 60000UL) {
            lastLogTime = Ms();
            LogRecord r;
            r.sec=now.sec; r.min=now.min; r.hour=now.hour;
            r.day=now.day; r.month=now.month; r.pot1=pot1;
            r.pot2_pct=(uint8_t)((uint32_t)pot2*100/4095);
            r.pad=0;
            Log_Write(&r);
        }

        if (curMode == MODE_LOG && g_logCount > 0) {
            if ((Ms() - lastScrollMs) >= 1500UL) {
                lastScrollMs = Ms();
                logSlot = (logSlot + 1) % g_logCount;
                needRedraw = 1;
            }
        }

        LEDs_AllOff();
        if (curMode == MODE_NORMAL) {
            if (now.sec % 2 == 0) LED_Set(1,1);
        } else {
            LED_Set((uint8_t)curMode, 1);
        }

        uint8_t secChanged = (now.sec != last_sec);
        if (secChanged) last_sec = now.sec;

        switch (curMode) {
            case MODE_NORMAL:
                if (secChanged || needRedraw) Page_Clock(&now);
                break;
            case MODE_POT1:
                Page_Pot1(pot1);
                break;
            case MODE_POT2:
                Page_Pot2(pot2);
                break;
            case MODE_LOG:
                if (needRedraw) Page_Log(logSlot);
                break;
            default: break;
        }
        needRedraw = 0;

        DelayMs(50);
    }
}
