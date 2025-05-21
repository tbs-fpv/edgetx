/*
 * Copyright (C) EdgeTX
 *
 * Based on code named
 *   opentx - https://github.com/opentx/opentx
 *   th9x - http://code.google.com/p/th9x
 *   er9x - http://code.google.com/p/er9x
 *   gruvin9x - http://code.google.com/p/gruvin9x
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "hal/gpio.h"
#include "hal/rgbleds.h"
#include "stm32_gpio.h"
#include "stm32_dma.h"
#include "boards/generic_stm32/rgb_leds.h"
#include "board.h"
#if defined(LED_STRIP_GPIO)
#include "stm32_ws2812.h"
#endif

#define GET_RED(color) (((color) & 0xFF0000) >>16)
#define GET_GREEN(color) (((color) & 0x000FF00) >> 8)
#define GET_BLUE(color) (((color) & 0xFF))

#if defined(FUNCTION_SWITCHES) && !defined(FUNCTION_SWITCHES_RGB_LEDS)
static const uint32_t fsLeds[] = {FSLED_GPIO_PIN_1, FSLED_GPIO_PIN_2,
				  FSLED_GPIO_PIN_3, FSLED_GPIO_PIN_4,
				  FSLED_GPIO_PIN_5, FSLED_GPIO_PIN_6};
#endif

#if defined(PWM_LED)
#include "stm32_timer.h"
#include "timers_driver.h"
#include "edgetx.h"

#define RGB_LED_CODE_1_TIME 66
#define RGB_LED_CODE_0_TIME 24
#define RGB_LED_RGB_BITS 24
#define RGB_LED_BIT_BUF_SIZE (RGB_LED_RGB_BITS + 1)

#define RGB_COLOUR_OFF 0, 0, 0
#define RGB_COLOUR_RED 25, 0, 0
#define RGB_COLOUR_GREEN 0, 25, 0
#define RGB_COLOUR_BLUE 0, 0, 25
#define RGB_COLOUR_WHITE 25, 25, 25

#define RGB2BUFFER(red, green, blue) \
  (((uint32_t)green << 16) | ((uint32_t)red << 8) | (uint32_t)blue)

static uint32_t rgb_led_bit_buffer[RGB_LED_BIT_BUF_SIZE] __DMA;

void ledSetColour(uint8_t red, uint8_t green, uint8_t blue)
{
  LED_DMA_STREAM->CR &= ~DMA_SxCR_EN;
  LED_TIMER->CCR4 = 0;

  uint32_t rgb_buffer = RGB2BUFFER(red, green, blue);
  uint32_t shift_reg_mask = 1 << (RGB_LED_RGB_BITS - 1);
  for (uint8_t i = 0; i < RGB_LED_RGB_BITS; i++) {
    if ((rgb_buffer & shift_reg_mask) != 0)
      rgb_led_bit_buffer[i] = RGB_LED_CODE_1_TIME;
    else
      rgb_led_bit_buffer[i] = RGB_LED_CODE_0_TIME;
    shift_reg_mask >>= 1;
  }

  static uint32_t lastLedUpdate = 0;
  while ((ticksNow() - lastLedUpdate) < 600 * ((CPU_FREQ + 500000)  / 1000000));
  lastLedUpdate = ticksNow();

  LED_TIMER->CR1 &= ~TIM_CR1_CEN;

  DMA1->HIFCR = LED_DMA_FLAG_TC | LED_DMA_FLAG_ERRORS;
  LED_DMA_STREAM->PAR = CONVERT_PTR_UINT(&LED_TIMER->CCR4);
  LED_DMA_STREAM->M0AR = CONVERT_PTR_UINT(rgb_led_bit_buffer);
  LED_DMA_STREAM->NDTR = RGB_LED_BIT_BUF_SIZE;

  LED_DMA_STREAM->CR |= DMA_SxCR_EN;
  LED_TIMER->CR1 |= TIM_CR1_CEN;
}
#endif

void ledInit()
{
#if defined(LED_GREEN_GPIO)
  gpio_init(LED_GREEN_GPIO, GPIO_OUT, GPIO_PIN_SPEED_LOW);
#endif

#if defined(LED_RED_GPIO)
  gpio_init(LED_RED_GPIO, GPIO_OUT, GPIO_PIN_SPEED_LOW);
#endif

#if defined(LED_BLUE_GPIO)
  gpio_init(LED_BLUE_GPIO, GPIO_OUT, GPIO_PIN_SPEED_LOW);
#endif

#if defined(FUNCTION_SWITCHES) && !defined(FUNCTION_SWITCHES_RGB_LEDS)
  for (size_t i = 0; i < DIM(fsLeds); i++) {
    gpio_init(fsLeds[i], GPIO_OUT, GPIO_PIN_SPEED_LOW);
  }
#endif

#if defined(PWM_LED)
  gpio_init_af(LED_GPIO, LED_GPIO_AF, GPIO_PIN_SPEED_HIGH);

  stm32_dma_enable_clock(LED_DMA);
  stm32_timer_enable_clock(LED_TIMER);
  LED_DMA_STREAM->CR &= ~DMA_SxCR_EN;
  LED_TIMER->CR1 &= ~TIM_CR1_CEN;
  LED_TIMER->PSC = 0;
  LED_TIMER->ARR = 92;

  LED_TIMER->CCER |= TIM_CCER_CC4E;
  LED_TIMER->BDTR = TIM_BDTR_MOE;                          // Enable outputs
  LED_TIMER->CCMR2 = TIM_CCMR2_OC3M_2 | TIM_CCMR2_OC3M_0;  // Force O/P high
  LED_TIMER->EGR = 1;                                      // Restart
  LED_TIMER->DIER |= TIM_DIER_CC4DE;                       // Enable DMA on CC4
  LED_TIMER->CCMR2 = TIM_CCMR2_OC4M_2 | TIM_CCMR2_OC4M_1 | TIM_CCMR2_OC4PE;

  LED_DMA_STREAM->CR = LED_DMA_CHANNEL | DMA_SxCR_DIR_0 | DMA_SxCR_MINC |
                       DMA_SxCR_PSIZE_1 | DMA_SxCR_MSIZE_1;

  memset(rgb_led_bit_buffer, 0, sizeof(uint32_t) * RGB_LED_BIT_BUF_SIZE);
#endif
}

#if defined(FUNCTION_SWITCHES_RGB_LEDS)
// used to map switch number to led number in the rgbled chain
uint8_t ledMapping[] = {0, 1, 2, 3, 4, 5, 6};

void fsLedRGB(uint8_t index, uint32_t color)
{
  ws2812_set_color(ledMapping[index], GET_RED(color), GET_GREEN(color),
                   GET_BLUE(color));
}

uint32_t fsGetLedRGB(uint8_t index)
{
  return rgbGetLedColor(ledMapping[index]);
}

uint8_t getRGBColorIndex(uint32_t color)
{
  for (uint8_t i = 0; i < DIM(colorTable); i++) {
    if (color == colorTable[i])
      return(i + 1);
  }
  return 0; // Custom value set with Companion
}
#elif defined(FUNCTION_SWITCHES)
void fsLedOff(uint8_t index)
{
  gpio_clear(fsLeds[index]);
}

void fsLedOn(uint8_t index)
{
  gpio_set(fsLeds[index]);
}

bool fsLedState(uint8_t index)
{
  return gpio_read(fsLeds[index]) ? true : false;
}
#endif

void ledOff()
{
#if defined(LED_RED_GPIO)
  GPIO_LED_GPIO_OFF(LED_RED_GPIO);
#endif
#if defined(LED_BLUE_GPIO)
  GPIO_LED_GPIO_OFF(LED_BLUE_GPIO);
#endif
#if defined(LED_GREEN_GPIO)
  GPIO_LED_GPIO_OFF(LED_GREEN_GPIO);
#endif
#if defined(PWM_LED)
  ledSetColour(RGB_COLOUR_OFF);
#endif
}

void ledRed()
{
#if defined(LED_RED_GPIO)
  ledOff();
  GPIO_LED_GPIO_ON(LED_RED_GPIO);
#endif
#if defined(PWM_LED)
  ledSetColour(RGB_COLOUR_RED);
#endif
}

void ledGreen()
{
#if defined(LED_GREEN_GPIO)
  ledOff();
  GPIO_LED_GPIO_ON(LED_GREEN_GPIO);
#endif
#if defined(PWM_LED)
  ledSetColour(RGB_COLOUR_GREEN);
#endif
}

void ledBlue()
{
#if defined(LED_BLUE_GPIO)
  ledOff();
  GPIO_LED_GPIO_ON(LED_BLUE_GPIO);
#endif
#if defined(PWM_LED)
  ledSetColour(RGB_COLOUR_BLUE);
#endif
}

void ledWhite()
{
#if defined(PWM_LED)
  ledSetColour(RGB_COLOUR_WHITE);
#endif
}
