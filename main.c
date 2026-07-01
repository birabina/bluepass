/**
 * @file    main.c
 * @brief   Firmware Principal — Leitor RFID RC522 com LED RGB no STM32F103C8T6
 *
 * ⊹₊˚‧︵‿₊୨ᰔ୧₊‿︵‧˚₊⊹⊹₊˚‧︵‿₊୨ᰔ୧₊‿︵‧˚₊⊹⊹₊˚‧︵‿₊୨ᰔ୧₊‿︵‧˚₊⊹⊹₊˚‧︵‿₊୨ᰔ୧₊‿︵‧˚₊⊹⊹₊˚‧︵‿₊୨ᰔ୧₊‿︵‧˚₊⊹
 *                                              BluePass
 * Trabalho Final de Microcontroladores
 * 
 * Professor: Rodolfo Coutinho
 * 
 * Autoras: Kaylanne Castro Evangelista
 *          Sabrina Rodrigues Malveira
 *
 * Funcionalidade:
 *   - LED pisca em AZUL aguardando uma tag RFID
 *   - Tag com UID correto  → LED acende em VERDE por 3 segundos
 *   - Tag com UID errado   → LED acende em VERMELHO por 3 segundos
 *
 * Clocks:
 *   - SYSCLK = 72 MHz via PLL (HSE 8 MHz × 9)
 *   - AHB    = 72 MHz (HPRE = /1)
 *   - APB1   = 36 MHz (PPRE1 = /2, limite do barramento)
 *   - APB2   = 72 MHz (PPRE2 = /1)
 *   - SysTick → interrupção a cada 1 ms
 *
 * Hardware:
 *   LED RGB (ânodo comum): PA0=R, PA1=G, PA2=B (lógica invertida)
 *   RFID RC522 (SPI1):     PA4=CS, PA5=SCK, PA6=MISO, PA7=MOSI, PB0=RST
 *
 * ⊹₊˚‧︵‿₊୨ᰔ୧₊‿︵‧˚₊⊹⊹₊˚‧︵‿₊୨ᰔ୧₊‿︵‧˚₊⊹⊹₊˚‧︵‿₊୨ᰔ୧₊‿︵‧˚₊⊹⊹₊˚‧︵‿₊୨ᰔ୧₊‿︵‧˚₊⊹⊹₊˚‧︵‿₊୨ᰔ୧₊‿︵‧˚₊⊹
 */

#include <stm32f1xx.h>
#include <stdint.h>
#include "rgb.h"
#include "rc522.h"
#include "usart.h"

/* =========================================================================
 * UID Mestre (hardcoded) — altere conforme o cartão/chaveiro desejado
 * ========================================================================= */
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

/* =========================================================================
 * SystemClock_Config — Configura SYSCLK a 72 MHz usando PLL + HSE
 * ========================================================================= */
static void SystemClock_Config(void)
{
    /* --- 1. Garante que HSI está ligado e estável --- */
    RCC->CR |= RCC_CR_HSION;
    while (!(RCC->CR & RCC_CR_HSIRDY))
    {
        __NOP();
    }

    /* --- 2. Latência do Flash: 1 wait state para 48 MHz (24–48 MHz) --- */
    FLASH->ACR = FLASH_ACR_PRFTBE | FLASH_ACR_LATENCY_1;

    /* --- 3. Pre-escalodor --- */
    RCC->CFGR &= ~(RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2);
    RCC->CFGR |= RCC_CFGR_PPRE1_DIV2; /* APB1 = AHB/2 = 24 MHz */

    /* --- 4. PLL: fonte = HSI/2, multiplicador = x12 → 4*12 = 48 MHz --- */
    RCC->CFGR &= ~RCC_CFGR_PLLSRC;    /* PLL source = HSI/2 (bit=0) */
    RCC->CFGR &= ~RCC_CFGR_PLLMULL;
    RCC->CFGR |=  RCC_CFGR_PLLMULL12; /* x12 */

    /* --- 5. Liga PLL e aguarda lock --- */
    RCC->CR |= RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY))
    {
        __NOP();
    }

    /* --- 6. Seleciona PLL como SYSCLK --- */
    RCC->CFGR &= ~RCC_CFGR_SW;
    RCC->CFGR |=  RCC_CFGR_SW_PLL;

    /* --- 7. Aguarda confirmação --- */
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL)
    {
        __NOP();
    }
}

/* =========================================================================
 * Peripheral_Clock_Enable — Habilita os clocks dos periféricos usados
 * ========================================================================= */
static void Peripheral_Clock_Enable(void)
{
   
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN    
                 |  RCC_APB2ENR_IOPBEN    
                 |  RCC_APB2ENR_SPI1EN    
                 |  RCC_APB2ENR_USART1EN  
                 |  RCC_APB2ENR_AFIOEN;   

  
    __NOP();
    __NOP();
}

/* =========================================================================
 * SysTick_Config_1ms — Configura SysTick para gerar interrupção a cada 1 ms
 * ========================================================================= */
static void SysTick_Config_1ms(void)
{
    SysTick->LOAD = 48000U - 1U;         
    SysTick->VAL  = 0U;                  
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk
                  | SysTick_CTRL_TICKINT_Msk    
                  | SysTick_CTRL_ENABLE_Msk;   
}

/* =========================================================================
 * Máquina de Estados do Firmware
 * ========================================================================= */
typedef enum
{
    STATE_IDLE   = 0U, 
    STATE_ACCESS,     
    STATE_DENIED,     
} AppState_t;

/* =========================================================================
 * Programa Principal
 * ========================================================================= */
int main(void)
{
    uint8_t     tag_type[2U];          
    uint8_t     uid[5U];                
    uint8_t     rc_status;             
    AppState_t  state      = STATE_IDLE;
    uint32_t    state_tick = 0U;        
    uint32_t    hb_tick    = 0U;       

    SystemClock_Config();

    Peripheral_Clock_Enable();
 
    SysTick_Config_1ms();
    
    RGB_Init();
   
    MFRC522_Init();
   
    USART1_Init();
    USART1_SendString("RFID:BOOT\n");
    
    {
        static const char hex_digits[] = "0123456789ABCDEF";
        uint8_t ver = MFRC522_ReadReg(MFRC522_REG_VERSION);
        char msg[] = "RFID:DBG=RC522_VERSION=0xXX\n";
        msg[24] = hex_digits[(ver >> 4U) & 0x0FU];
        msg[25] = hex_digits[ver & 0x0FU];
        USART1_SendString(msg);

        if (ver == 0x91U || ver == 0x92U)
        {
            USART1_SendString("RFID:DBG=RC522_OK\n");
        }
        else if (ver == 0x00U)
        {
            USART1_SendString("RFID:ERR=RC522_VERSION=0x00_SPI_MOSI_MISO_CHECK\n");
        }
        else if (ver == 0xFFU)
        {
            USART1_SendString("RFID:ERR=RC522_VERSION=0xFF_CS_OR_POWER_CHECK\n");
        }
        else
        {
            USART1_SendString("RFID:DBG=RC522_VERSION_UNKNOWN_BUT_RESPONDING\n");
        }
    }
  
    delay_ms(100U);

    /* -------------------------------------------------------------------
     * 2. Loop Principal — Máquina de Estados
     * ------------------------------------------------------------------- */
    while (1)
    {
            if ((SysTick_tick - hb_tick) >= 5000U)
        {
            hb_tick = SysTick_tick;
            USART1_SendHeartbeat();
        }

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
                            USART1_SendRFIDEvent(uid, 1U); 
                        }
                        else
                        {
                            
                            state      = STATE_DENIED;
                            state_tick = SysTick_tick;
                            RGB_SET_RED();
                            USART1_SendRFIDEvent(uid, 0U);
                        }

                        
                        MFRC522_Halt();
                    }
                    else
                    {
                       
                        USART1_SendError("ANTICOLL_FAIL");
                    }
                }
               
                else if ((SysTick_tick - state_tick) >= 2000U)
                {
                    state_tick = SysTick_tick;
                    USART1_SendString("RFID:DBG=IDLE_SCANNING\n");
                }
                break;
            }

            /* ---------------------------------------------------------------
             * STATE_ACCESS: LED verde por 3 segundos
             * --------------------------------------------------------------- */
            case STATE_ACCESS:
            {
                if ((SysTick_tick - state_tick) >= 3000U)
                {
                    
                    RGB_ALL_OFF();
                    state = STATE_IDLE;
                }
                break;
            }

            /* ---------------------------------------------------------------
             * STATE_DENIED: LED vermelho por 3 segundos
             * --------------------------------------------------------------- */
            case STATE_DENIED:
            {
                if ((SysTick_tick - state_tick) >= 3000U)
                {
                    /* 3 segundos passaram → volta ao estado de espera */
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