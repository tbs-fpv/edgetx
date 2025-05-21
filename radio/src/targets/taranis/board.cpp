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

#include "stm32_hal_ll.h"
#include "stm32_gpio.h"
#include "stm32_ws2812.h"

#include "hal/adc_driver.h"
#include "hal/trainer_driver.h"
#include "hal/switch_driver.h"
#include "hal/module_port.h"
#include "hal/abnormal_reboot.h"
#include "hal/usb_driver.h"
#include "hal/gpio.h"
#include "hal/rgbleds.h"

#include "board.h"
#include "boards/generic_stm32/module_ports.h"
#include "boards/generic_stm32/intmodule_heartbeat.h"
#include "boards/generic_stm32/analog_inputs.h"
#include "boards/generic_stm32/rgb_leds.h"

#include "debug.h"
#include "rtc.h"

#include "timers_driver.h"
#include "dataconstants.h"
#include "trainer.h"

#if defined(FLYSKY_GIMBAL)
  #include "flysky_gimbal_driver.h"
#endif

#if !defined(BOOT)
  #include "edgetx.h"
  #if defined(CHARGING_ANIMATION) && defined(ROTARY_ENCODER_NAVIGATION)
    #include "hal/rotary_encoder.h"
  #endif
  #if defined(PXX1)
    #include "pulses/pxx1.h"
  #endif
#endif

#if defined(BLUETOOTH)
  #include "bluetooth_driver.h"
#endif

#if defined(CSD203_SENSOR)
  #include "csd203_sensor.h"
#endif

HardwareOptions hardwareOptions;

#if !defined(BOOT)

#if defined(FUNCTION_SWITCHES)
#include "storage/storage.h"
#endif

#if defined(SIXPOS_SWITCH_INDEX)
uint8_t lastADCState = 0;
uint8_t sixPosState = 0;
bool dirty = true;
uint16_t getSixPosAnalogValue(uint16_t adcValue)
{
  uint8_t currentADCState = 0;
  if (adcValue > 3800)
    currentADCState = 6;
  else if (adcValue > 3100)
    currentADCState = 5;
  else if (adcValue > 2300)
    currentADCState = 4;
  else if (adcValue > 1500)
    currentADCState = 3;
  else if (adcValue > 1000)
    currentADCState = 2;
  else if (adcValue > 400)
    currentADCState = 1;
  if (lastADCState != currentADCState) {
    lastADCState = currentADCState;
  } else if (lastADCState != 0 && lastADCState - 1 != sixPosState) {
    sixPosState = lastADCState - 1;
    dirty = true;
  }
  if (dirty) {
    for (uint8_t i = 0; i < 6; i++) {
      if (i == sixPosState)
        ws2812_set_color(i, SIXPOS_LED_RED, SIXPOS_LED_GREEN, SIXPOS_LED_BLUE);
      else
        ws2812_set_color(i, 0, 0, 0);
    }
    rgbLedColorApply();
  }
  return (4096/5)*(sixPosState);
}
#endif

#if defined(SALED_PWR_GPIO)
void SALEDpwrInit()
{
  gpio_init(SALED_PWR_GPIO, GPIO_OUT, GPIO_PIN_SPEED_LOW);
  SALED_PWR_ON();
}
#endif
#if defined(SDLED_PWR_GPIO)
void SDLEDpwrInit()
{
  gpio_init(SDLED_PWR_GPIO, GPIO_OUT, GPIO_PIN_SPEED_LOW);
  SDLED_PWR_ON();
}
#endif

void boardInit()
{
  LL_APB1_GRP1_EnableClock(AUDIO_RCC_APB1Periph);
  LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_SYSCFG);

#if defined(USB_CHARGE_LED) && !defined(DEBUG)
  usbInit();
  // prime debounce state...
  usbPlugged();

  if (usbPlugged()) {
    delaysInit();
#if defined(AUDIO_MUTE_GPIO)
     // Charging can make a buzzing noise
     gpio_init(AUDIO_MUTE_GPIO, GPIO_OUT, GPIO_PIN_SPEED_LOW);
     gpio_set(AUDIO_MUTE_GPIO);
 #endif
    while (usbPlugged()) {
      delay_ms(1000);
    }
    pwrOff();
  }
#endif

#if defined(BLUETOOTH) && !defined(PCBX9E)
  bluetoothInit(BLUETOOTH_DEFAULT_BAUDRATE, true);
#endif

#if defined(MANUFACTURER_RADIOMASTER) && defined(STM32F407xx)
  void board_set_bor_level();
  board_set_bor_level();
#endif

  board_trainer_init();

  // Sets 'hardwareOption.pcbrev' as well
  pwrInit();
  boardInitModulePorts();

#if defined(INTERNAL_MODULE_PXX1) && defined(PXX_FREQUENCY_HIGH)
  pxx1SetInternalBaudrate(PXX1_FAST_SERIAL_BAUDRATE);
#endif

#if defined(INTMODULE_HEARTBEAT) &&                                     \
  (defined(INTERNAL_MODULE_PXX1) || defined(INTERNAL_MODULE_PXX2))
  pulsesSetModuleInitCb(_intmodule_heartbeat_init);
  pulsesSetModuleDeInitCb(_intmodule_heartbeat_deinit);
  trainerSetChangeCb(_intmodule_heartbeat_trainer_hook);
#endif
  
// #if defined(AUTOUPDATE)
//   telemetryPortInit(FRSKY_SPORT_BAUDRATE, TELEMETRY_SERIAL_WITHOUT_DMA);
//   sportSendByteLoop(0x7E);
// #endif

  delaysInit();

#if defined(STATUS_LEDS)
  ledInit();
#if !defined(POWER_LED_BLUE)
  ledBlue();
#else
  ledGreen();
#endif
#endif

#if defined(CSD203_SENSOR)
  IICcsd203init();
  initCSD203();
#endif

#if defined(TBS_CROSSFIRE_TASK)
  crsfInit();
#endif

#if defined(RTCLOCK)
  rtcInit(); // RTC must be initialized before rambackupRestore() is called
#endif

#if defined(USB_CHARGER)
  usbChargerInit();
#endif

  keysInit();
  switchInit();

#if defined(SALED_PWR_GPIO)
  SALEDpwrInit();
#endif
#if defined(SDLED_PWR_GPIO)
  SDLEDpwrInit();
#endif

#if defined(ROTARY_ENCODER_NAVIGATION)
  rotaryEncoderInit();
#endif

  gimbalsDetect();

  if (!adcInit(&_adc_driver))
    TRACE("adcInit failed");

// If the radio was powered on by dual use USB, halt the boot process, let battery charge
// TODO: needs refactoring if any other manufacturer implements either of the following:
// - function switches and the radio supports charging
// - single USB for data + charge which powers on radio
#if (defined(MANUFACTURER_JUMPER) || defined(MANUFACTURER_TBS)) && !defined(USB_CHARGE_LED)
  bool skipCharging = false;
#if defined(TBS_CROSSFIRE_TASK)
  if (readBackupReg(BKREG_SKIP_BOARD_OFF) ||
    bkregGetStatusFlag(DEVICE_RESTART_WITHOUT_WARN_FLAG) ||
    getCrsfFlag(CRSF_FLAG_XF_UPDATE_REQUIRED)) {
    skipCharging = true;
#if defined(USB_AGENT)
    setSelectedUsbMode(USB_AGENT_MODE);
#endif
  }
#endif
  // This is needed to prevent radio from starting when usb is plugged to charge
  usbInit();
  // prime debounce state...
  usbPlugged();


  if (usbPlugged() && !skipCharging) {
    __enable_irq();
    getADC();
    pwrOn();  // required to get bat adc reads
    INTERNAL_MODULE_OFF();
    EXTERNAL_MODULE_OFF();
    delay_ms(2000); // let this stabilize
#if defined(CHARGING_ANIMATION)
    lcdInit();
    backlightInit();
    backlightDisable();

    const uint32_t TIMEOUT_MS = 5000;
    const uint32_t TIMEOUT_TICKS = TIMEOUT_MS * (CPU_FREQ / 1000);
    uint32_t backlightTimeout = ticksNow() + TIMEOUT_TICKS;
    bool screenActive = true;
    uint32_t lastKeyState = readKeys();
  #if defined(ROTARY_ENCODER_NAVIGATION)
    rotenc_t lastRotaryValue = rotaryEncoderGetValue();
  #endif
#endif
    while (usbPlugged() && !skipCharging) {
      //    // Let it charge ...
      getADC();  // Warning: the value read does not include VBAT calibration
      checkBattery();
      delay_ms(20);
#if defined(FUNCTION_SWITCHES)
      // Support for FS Led to indicate battery charge level
      if (getBatteryVoltage() >= 660) setFSLedON(0);
      if (getBatteryVoltage() >= 700) setFSLedON(1);
      if (getBatteryVoltage() >= 740) setFSLedON(2);
      if (getBatteryVoltage() >= 780) setFSLedON(3);
      if (getBatteryVoltage() >= 820) setFSLedON(4);
      if (getBatteryVoltage() >= 842) setFSLedON(5);
#elif defined(STATUS_LEDS)
      // Use Status LED to indicate battery charge level instead
  #if defined(RADIO_TANGO2)
      if (usbChargerLed()) ledRed();
      else ledGreen();
  #else
      if (getBatteryVoltage() <= 660) ledRed();         // low discharge
      else if (getBatteryVoltage() <= 842) ledBlue();   // charging
      else ledGreen();                                  // charging done
  #endif
  #if defined(MANUFACTURER_TBS)
      rtcSleep(1);
      uint32_t ms = 0;
      while (pwrPressed() && ++ms <= 500) delay_ms(1);
      skipCharging = (ms > 500);
  #else
      delay_ms(1000);
  #endif
#elif defined(CHARGING_ANIMATION)
      uint32_t keys = readKeys();
      bool changed = (keys != lastKeyState);
      if (changed) lastKeyState = keys;

  #if defined(ROTARY_ENCODER_NAVIGATION)
      rotenc_t rotary = rotaryEncoderGetValue();
      if (rotary != lastRotaryValue) {
        lastRotaryValue = rotary;
        changed = true;
      }
  #endif

      if (changed) {
        backlightTimeout = ticksNow() + TIMEOUT_TICKS;
        screenActive = true;
      }

      if (screenActive) {
        if ((int32_t)(ticksNow() - backlightTimeout) >= 0) {
          lcdClear();
          backlightDisable();
          screenActive = false;
        } else {
          lcdClear();
          if (usbChargerLed()) drawChargingState();
          else drawFullyCharged();
          lcdRefresh();
          backlightFullOn();
        }
      }
#endif
    }
    if (!skipCharging) pwrOff();
  }
#endif

  timersInit();
  __enable_irq();


  lcdInit(); // delaysInit() must be called before
  audioInit();
  usbInit();

#if defined(LED_STRIP_GPIO)
  rgbLedInit();
#endif

#if defined(HAPTIC)
  hapticInit();
#endif

#if defined(PXX2_PROBE)
  intmodulePxx2Probe();
#endif

#if defined(DEBUG)
  // Freeze timers & watchdog when core is halted
  DBGMCU->APB1FZ = 0x00E009FF;
  DBGMCU->APB2FZ = 0x00070003;
#endif

#if defined(PWR_BUTTON_PRESS)
  if (WAS_RESET_BY_WATCHDOG_OR_SOFTWARE()) {
    pwrOn();
  }
#endif

#if defined(TOPLCD_GPIO)
  toplcdInit();
#endif

  backlightInit();

#if defined(GUI)
  lcdSetContrast(true);
#endif

#if defined(RADIO_GX12)
  gpio_init(HALL_SYNC, GPIO_OUT, GPIO_PIN_SPEED_LOW);
#endif

}
#endif

void boardOff()
{
#if defined(LED_STRIP_GPIO) && !defined(BOOT)
  rgbLedStop();
  rgbLedClearAll();
#endif

#if defined(STATUS_LEDS) && !defined(BOOT)
  ledOff();
#endif

#if defined(TBS_CROSSFIRE_TASK)
  crossfirePowerOff();
#endif

  BACKLIGHT_DISABLE();

#if defined(TOPLCD_GPIO) && !defined(BOOT)
  toplcdOff();
#endif

#if defined(PWR_BUTTON_PRESS)
  while (pwrPressed()) {
    WDG_RESET();
  }
#endif

#if defined(MANUFACTURER_RADIOMASTER) && defined(STM32F407xx)
  lcdInit(); 
#endif

  lcdOff();
  SysTick->CTRL = 0; // turn off systick
  pwrOff();

  // disable interrupts
  __disable_irq();

  while (1) {
    WDG_RESET();
#if defined(PWR_BUTTON_PRESS)
    // X9E/X7 needs watchdog reset because CPU is still running while
    // the power key is held pressed by the user.
    // The power key should be released by now, but we must make sure
    if (!pwrPressed()) {
      // Put the CPU into sleep to reduce the consumption,
      // it might help with the RTC reset issue
      PWR->CR |= PWR_CR_CWUF;
      /* Select STANDBY mode */
      PWR->CR |= PWR_CR_PDDS;
      /* Set SLEEPDEEP bit of Cortex System Control Register */
      SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;
      /* Request Wait For Event */
      __WFE();
    }
#endif
  }

  // this function must not return!
}
