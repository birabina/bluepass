/**
 * @file    main.c
 * @brief   Firmware Principal — Leitor RFID RC522 com LED RGB no STM32F103C8T6
 *
 * Hardware:
 * LED RGB (ânodo comum): PA0=R, PA1=G, PA2=B
 * RFID RC522 (SPI1):     PA4=CS, PA5=SCK, PA6=MISO, PA7=MOSI, PB0=RST
 *
 * Nota: Uso exclusivo de CMSIS (<stm32f1xx.h>). Sem HAL ou LL.
 */

#include <stm32f1xx.h>
#include <stdint.h>
#include "rgb.h"
#include "rc522.h"

static const uint8_t MASTER_UID[4U] = { 0x12U, 0x34U, 0x56U, 0x78U };
volatile uint32_t SysTick_tick = 0U;

void SysTick_Handler(void)
{
    SysTick_tick++;
}

void delay_ms(uint32_t ms)
{
    uint32_t start = SysTick_tick;
    while ((SysTick_tick - start) < ms)
    {
        __NOP();
    }
}

static void SystemClock_Config(void)
{
    RCC->CR |= RCC_CR_HSEON;
    while (!(RCC->CR & RCC_CR_HSERDY))
    {
        __NOP();
    }

    FLASH->ACR = FLASH_ACR_PRFTBE | FLASH_ACR_LATENCY_2;

    RCC->CFGR &= ~(RCC_CFGR_HPRE  |
                   RCC_CFGR_PPRE1 |
                   RCC_CFGR_PPRE2);

    RCC->CFGR |= RCC_CFGR_PPRE1_DIV2;

    RCC->CFGR |= RCC_CFGR_PLLSRC;
    RCC->CFGR &= ~RCC_CFGR_PLLXTPRE;
    RCC->CFGR &= ~RCC_CFGR_PLLMULL;
    RCC->CFGR |=  RCC_CFGR_PLLMULL9;

    RCC->CR |= RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY))
    {
        __NOP();
    }

    RCC->CFGR &= ~RCC_CFGR_SW;
    RCC->CFGR |=  RCC_CFGR_SW_PLL;

    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL)
    {
        __NOP();
    }
}

static void Peripheral_Clock_Enable(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN  
                 |  RCC_APB2ENR_IOPBEN  
                 |  RCC_APB2ENR_SPI1EN  
                 |  RCC_APB2ENR_AFIOEN;

    __NOP();
    __NOP();
}

static void SysTick_Config_1ms(void)
{
    SysTick->LOAD = 72000U - 1U;
    SysTick->VAL  = 0U;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk 
                  | SysTick_CTRL_TICKINT_Msk    
                  | SysTick_CTRL_ENABLE_Msk;
}

typedef enum
{
    STATE_IDLE   = 0U,
    STATE_ACCESS,
    STATE_DENIED,
} AppState_t;

int main(void)
{
    uint8_t     tag_type[2U];
    uint8_t     uid[5U];
    uint8_t     rc_status;
    AppState_t  state      = STATE_IDLE;
    uint32_t    state_tick = 0U;

    SystemClock_Config();
    Peripheral_Clock_Enable();
    SysTick_Config_1ms();
    RGB_Init();
    MFRC522_Init();

    delay_ms(100U);

    while (1)
    {
        switch (state)
        {
            case STATE_IDLE:
            {
                RGB_BlinkBlue(500U);
                rc_status = MFRC522_Request(PICC_CMD_REQA, tag_type);

                if (rc_status == MFRC522_OK)
                {
                    rc_status = MFRC522_Anticoll(uid);

                    if (rc_status == MFRC522_OK)
                    {
                        rc_status = MFRC522_CompareUID(uid, MASTER_UID);

                        if (rc_status == MFRC522_OK)
                        {
                            state      = STATE_ACCESS;
                            state_tick = SysTick_tick;
                            RGB_SET_GREEN();
                        }
                        else
                        {
                            state      = STATE_DENIED;
                            state_tick = SysTick_tick;
                            RGB_SET_RED();
                        }

                        MFRC522_Halt();
                    }
                }
                break;
            }

            case STATE_ACCESS:
            {
                if ((SysTick_tick - state_tick) >= 3000U)
                {
                    RGB_ALL_OFF();
                    state = STATE_IDLE;
                }
                break;
            }

            case STATE_DENIED:
            {
                if ((SysTick_tick - state_tick) >= 3000U)
                {
                    RGB_ALL_OFF();
                    state = STATE_IDLE;
                }
                break;
            }

            default:
            {
                RGB_ALL_OFF();
                state = STATE_IDLE;
                break;
            }
        }
    }

    return 0;
}
