/*
 * Copyright (C) OpenTX
 *
 * Based on code named
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

#include "edgetx.h"

#include "hal/adc_driver.h"
#include "os/sleep.h"

#if defined(SIMU)
// Forward declaration - implemented in gui/common/stdlcd/calibration.cpp for SIMU
void menuCommonCalib(event_t event);
#endif

#define XPOT_DELTA 10
#define XPOT_DELAY 10 /* cycles */

#define CALIB_POINT_COUNT (CALIB_SET_P8 - CALIB_SET_P0 + 1)

#define LLABEL_CENTERX (15)
#define BOTHLABEL_CENTERX (53)
#define RLABEL_CENTERX (90)
#define POINT_CAL_COUNTDOWN (3)
#if LCD_H > 64
#define LABEL_YPOS MENU_HEADER_HEIGHT + 4 * FH
#else
#define LABEL_YPOS MENU_HEADER_HEIGHT + 4
#endif

#if defined(TBS_CROSSFIRE_TASK) && !defined(SIMU)
#define SET_STICK_STATE(state)  setStickState(state)
#else
#define SET_STICK_STATE(state)
#endif

#if !defined(SIMU)
static const int16_t point_pos[CALIB_POINT_COUNT][2] = {{0, 0}, {1024, 0}, {1024, 1024}, {0, 1024}, {-1024, 1024}, {-1024, 0}, {-1024, -1024}, {0, -1024}, {1024, -1024}};
static const char* STR_MOVESTICK[] = {
    STR_MOVESTICK_P0,
    STR_MOVESTICK_P1,
    STR_MOVESTICK_P2,
    STR_MOVESTICK_P3,
    STR_MOVESTICK_P4,
    STR_MOVESTICK_P5,
    STR_MOVESTICK_P6,
    STR_MOVESTICK_P7,
    STR_MOVESTICK_P8,
};
#endif

#if defined(HARDWARE_POT)
void menuCommonCalibPots(event_t event)
{
#if defined(SIMU)
  reusableBuffer.calib.state = CALIB_FINISHED;
#else
  for (uint8_t i = adcGetMaxInputs(ADC_INPUT_MAIN); i < adcGetMaxInputs(ADC_INPUT_FLEX); i++) {  // get low and high vals for sticks and trims
    int16_t vt = anaIn(i);
    reusableBuffer.calib.inputs[i].input.loVal = min(vt, reusableBuffer.calib.inputs[i].input.loVal);
    reusableBuffer.calib.inputs[i].input.hiVal = max(vt, reusableBuffer.calib.inputs[i].input.hiVal);
    if (i >= POT1 && i <= POT_LAST) {
      if (IS_POT_WITHOUT_DETENT(i)) {
        reusableBuffer.calib.inputs[i].input.midVal = (reusableBuffer.calib.inputs[i].input.hiVal + reusableBuffer.calib.inputs[i].input.loVal) / 2;
      }
      uint8_t idx = i - POT1;
      int count = reusableBuffer.calib.inputs[idx].xpot.stepsCount;
      if (IS_POT_MULTIPOS(i) && count <= XPOTS_MULTIPOS_COUNT) {
        // use raw analog value for multipos calibraton, anaIn() already has multipos decoded value
        vt = getAnalogValue(i) >> 1;
        if (reusableBuffer.calib.inputs[idx].xpot.lastCount == 0 || vt < reusableBuffer.calib.inputs[idx].xpot.lastPosition - XPOT_DELTA || vt > reusableBuffer.calib.inputs[idx].xpot.lastPosition + XPOT_DELTA) {
          reusableBuffer.calib.inputs[idx].xpot.lastPosition = vt;
          reusableBuffer.calib.inputs[idx].xpot.lastCount = 1;
        } else {
          if (reusableBuffer.calib.inputs[idx].xpot.lastCount < 255) reusableBuffer.calib.inputs[idx].xpot.lastCount++;
        }
        if (reusableBuffer.calib.inputs[idx].xpot.lastCount == XPOT_DELAY) {
          int16_t position = reusableBuffer.calib.inputs[idx].xpot.lastPosition;
          bool found = false;
          for (int j = 0; j < count; j++) {
            int16_t step = reusableBuffer.calib.inputs[idx].xpot.steps[j];
            if (position >= step - XPOT_DELTA && position <= step + XPOT_DELTA) {
              found = true;
              break;
            }
          }
          if (!found) {
            if (count < XPOTS_MULTIPOS_COUNT) {
              reusableBuffer.calib.inputs[idx].xpot.steps[count] = position;
            }
            reusableBuffer.calib.inputs[idx].xpot.stepsCount += 1;
          }
        }
      }
    }
  }

  menuCalibrationState = reusableBuffer.calib.state;  // make sure we don't scroll while calibrating

  switch (event) {
    case EVT_KEY_BREAK(KEY_PAGEDN):
      chainMenu(menuRadioCalibration);
      break;

    case EVT_ENTRY:
    case EVT_KEY_BREAK(KEY_EXIT):
      reusableBuffer.calib.state = CALIB_START;
      break;

    case EVT_KEY_BREAK(KEY_ENTER):
      if (reusableBuffer.calib.state == CALIB_START)
        reusableBuffer.calib.state = CALIB_SET_MIDPOINT;
      else
        reusableBuffer.calib.state++;
      break;
  }

  switch (reusableBuffer.calib.state) {
    case CALIB_START:
      // START CALIBRATION
      lcdDrawCenteredText(MENU_HEADER_HEIGHT + 2 * FH, STR_MENUTOSTART);
      break;

    case CALIB_SET_MIDPOINT:
      // SET MIDPOINT
      lcdDrawCenteredText(MENU_HEADER_HEIGHT + FH, STR_SETMIDPOINT);
      lcdDrawCenteredText(MENU_HEADER_HEIGHT + 2 * FH, STR_MENUWHENDONE);
      for (uint8_t i = adcGetMaxInputs(ADC_INPUT_MAIN); i < adcGetMaxInputs(ADC_INPUT_FLEX); i++) {
        reusableBuffer.calib.inputs[i].input.loVal = 15000;
        reusableBuffer.calib.inputs[i].input.hiVal = -15000;
        reusableBuffer.calib.inputs[i].input.midVal = getAnalogValue(i) >> 1;
        if (i < POT_LAST) {
          reusableBuffer.calib.inputs[i].xpot.stepsCount = 0;
          reusableBuffer.calib.inputs[i].xpot.lastCount = 0;
        }
      }
      break;

    case CALIB_MOVE_STICKS:
      // MOVE STICKS/POTS
      lcdDrawCenteredText(MENU_HEADER_HEIGHT + FH, STR_MOVESTICKSPOTS);
      lcdDrawCenteredText(MENU_HEADER_HEIGHT + 2 * FH, STR_MENUWHENDONE);
      for (uint8_t i = adcGetMaxInputs(ADC_INPUT_MAIN); i < adcGetMaxInputs(ADC_INPUT_FLEX); i++) {
        if (abs(reusableBuffer.calib.inputs[i].input.loVal - reusableBuffer.calib.inputs[i].input.hiVal) > 50) {
          g_eeGeneral.calib[i].mid = reusableBuffer.calib.inputs[i].input.midVal;
          int16_t v = reusableBuffer.calib.inputs[i].input.midVal - reusableBuffer.calib.inputs[i].input.loVal;
          g_eeGeneral.calib[i].spanNeg = v - v / STICK_TOLERANCE;
          v = reusableBuffer.calib.inputs[i].input.hiVal - reusableBuffer.calib.inputs[i].input.midVal;
          g_eeGeneral.calib[i].spanPos = v - v / STICK_TOLERANCE;
        }
      }
      break;

    case CALIB_STORE:
      for (uint8_t i = POT1; i <= POT_LAST; i++) {
        int idx = i - POT1;
        int count = reusableBuffer.calib.inputs[idx].xpot.stepsCount;
        if (IS_POT_MULTIPOS(i)) {
          if (count > 1 && count <= XPOTS_MULTIPOS_COUNT) {
            for (int j = 0; j < count; j++) {
              for (int k = j + 1; k < count; k++) {
                if (reusableBuffer.calib.inputs[idx].xpot.steps[k] < reusableBuffer.calib.inputs[idx].xpot.steps[j]) {
                  SWAP(reusableBuffer.calib.inputs[idx].xpot.steps[j], reusableBuffer.calib.inputs[idx].xpot.steps[k]);
                }
              }
            }
            StepsCalibData* calib = (StepsCalibData*)&g_eeGeneral.calib[i];
            calib->count = count - 1;
            for (int j = 0; j < calib->count; j++) {
              calib->steps[j] = (reusableBuffer.calib.inputs[idx].xpot.steps[j + 1] + reusableBuffer.calib.inputs[idx].xpot.steps[j]) >> 5;
            }
          } else {
            g_eeGeneral.potsConfig &= ~(0x03 << (2 * idx));
          }
        }
      }
      g_eeGeneral.chkSum = evalChkSum();
      storageDirty(EE_GENERAL);
      storageCheck(true);
      reusableBuffer.calib.state = CALIB_FINISHED;
      break;

    default:
      reusableBuffer.calib.state = CALIB_START;
      break;
  }

  doMainScreenGraphicsCalibration(MAINSCREEN_GRAPHICS_POTS, 0);
#endif
}

void menuPotsCalibration(event_t event)
{
  char menu_title[32];
  strcpy(&menu_title[0], STR_MENUCALIBRATION);
  menu_title[strlen(STR_MENUCALIBRATION)] = '-';
  strcpy(&menu_title[strlen(STR_MENUCALIBRATION) + 1], STR_POTS);

  check_submenu_simple(event, 0);
  title(menu_title);
  menuCommonCalibPots(event);
  if (menuEvent) {
    menuCalibrationState = CALIB_START;
  }
}
#endif

#if !defined(SIMU)
void menuCommonCalib(event_t event)
{
#if defined(SIMU)
  reusableBuffer.calib.state = CALIB_FINISHED;
#else
  int16_t force_point_pos[4] __attribute__ ((unused));
  int16_t* countdown_timer = &reusableBuffer.calib.inputs[5].input.loVal;
  int16_t* count = &reusableBuffer.calib.inputs[5].input.hiVal;

  // give the crsf task some time to think
  sleep_ms(10);
#if defined(TBS_CROSSFIRE_TASK) && !defined(SIMU)
  if (reusableBuffer.calib.state > CALIB_START && reusableBuffer.calib.state < CALIB_FINISHED && crossfireSharedData->stick_state > reusableBuffer.calib.state) {
    // to sync the state from crossfire
    reusableBuffer.calib.state = crossfireSharedData->stick_state;
    *countdown_timer = get_tmr10ms() / 100;
    *count = POINT_CAL_COUNTDOWN;
  }
#endif

  resetBacklightTimeout();

  menuCalibrationState = reusableBuffer.calib.state;  // make sure we don't scroll while calibrating

  switch (event) {
#if defined(HARDWARE_POT)
    case EVT_KEY_BREAK(KEY_PAGEDN):
      if (adcGetMaxInputs(ADC_INPUT_FLEX))
        chainMenu(menuPotsCalibration);
      break;
#endif

    case EVT_ENTRY:
    case EVT_KEY_BREAK(KEY_EXIT):
      reusableBuffer.calib.state = CALIB_START;
      SET_STICK_STATE(CALIB_FINISHED);
      break;

    case EVT_KEY_BREAK(KEY_ENTER):
      if (reusableBuffer.calib.state == CALIB_START) {
        reusableBuffer.calib.state++;
        SET_STICK_STATE(reusableBuffer.calib.state);
      }
      if (reusableBuffer.calib.state > CALIB_CAL_POINTS) {
        reusableBuffer.calib.state++;
        SET_STICK_STATE(reusableBuffer.calib.state);
      }
      break;

    case EVT_ROTARY_LEFT:
    case EVT_ROTARY_RIGHT:
    case EVT_KEY_BREAK(KEY_MENU):
      break;
  }

  switch (reusableBuffer.calib.state) {
    case CALIB_START:
      lcdDrawCenteredText(MENU_HEADER_HEIGHT + 2 * FH, STR_MENUTOSTART);
      *countdown_timer = get_tmr10ms() / 100;
      *count = POINT_CAL_COUNTDOWN;
      break;

    case CALIB_SET_P0 ... CALIB_SET_P8: {
      force_point_pos[inputMappingConvertMode(1)] = point_pos[reusableBuffer.calib.state - CALIB_SET_P0][1];
      force_point_pos[inputMappingConvertMode(0)] = point_pos[reusableBuffer.calib.state - CALIB_SET_P0][0];
      force_point_pos[inputMappingConvertMode(2)] = point_pos[reusableBuffer.calib.state - CALIB_SET_P0][1];
      force_point_pos[inputMappingConvertMode(3)] = point_pos[reusableBuffer.calib.state - CALIB_SET_P0][0];

      lcdDrawCenteredText(LABEL_YPOS, STR_MOVESTICK[reusableBuffer.calib.state - CALIB_SET_P0]);

      // 3s countdown
      const int16_t curr_time = get_tmr10ms() / 100;
      if (curr_time - *countdown_timer >= 1) {
        if (*count != 0) {
          playNumber(*count, 0, 0, 0);
          (*count)--;
        }
        *countdown_timer = curr_time;
      }
      break;
    }
    case CALIB_CAL_POINTS: {
      lcdDrawCenteredText(LABEL_YPOS, STR_CAL_POINTS);
      lcdDrawCenteredText(MENU_HEADER_HEIGHT + 2 * FH, STR_RELEASE_STICK);
      for (uint8_t i = 0; i < MAX_STICKS; i++) {
        auto& calib = reusableBuffer.calib.inputs[i];
        calib.input.loVal = 30000;
        calib.input.hiVal = -30000;
        calib.input.midVal = 15000 / 2;
      }
      SET_STICK_STATE(reusableBuffer.calib.state);
      break;
    }
    case CALIB_SET_MIDPOINT:
      SET_STICK_STATE(CALIB_SET_MIDPOINT);

      force_point_pos[inputMappingConvertMode(1)] = 0;
      force_point_pos[inputMappingConvertMode(0)] = 0;
      force_point_pos[inputMappingConvertMode(2)] = 0;
      force_point_pos[inputMappingConvertMode(3)] = 0;

      lcdDrawCenteredText(LABEL_YPOS, STR_CENTER_STICK);
      lcdDrawCenteredText(MENU_HEADER_HEIGHT + 2 * FH, STR_MENUWHENDONE);
      adcCalibSetMidPoint();
      break;

    case CALIB_MOVE_STICKS:
      SET_STICK_STATE(CALIB_MOVE_STICKS);
      lcdDrawCenteredText(LABEL_YPOS, STR_MOVE_PLUS);
      lcdDrawCenteredText(MENU_HEADER_HEIGHT + 2 * FH, STR_MENUWHENDONE);
      adcCalibSetMinMax();
      break;

    case CALIB_STORE:
      g_eeGeneral.chkSum = evalChkSum();
      storageDirty(EE_GENERAL);
      storageCheck(true);
      SET_STICK_STATE(CALIB_FINISHED);
      reusableBuffer.calib.state = CALIB_FINISHED;
      break;

    default:
      reusableBuffer.calib.state = CALIB_START;
      break;
  }

#if defined(TBS_CROSSFIRE_TASK) && !defined(SIMU)
  if (crossfireSharedData->stick_state >= CALIB_SET_P0 && crossfireSharedData->stick_state <= CALIB_SET_MIDPOINT)
    doMainScreenGraphicsCalibration(MAINSCREEN_GRAPHICS_STICKS, force_point_pos);
  else
    doMainScreenGraphicsCalibration(MAINSCREEN_GRAPHICS_STICKS, 0);
#endif
#endif
}
#endif

void menuRadioCalibration(event_t event)
{
  char menu_title[32];
  strcpy(&menu_title[0], STR_MENUCALIBRATION);
  menu_title[strlen(STR_MENUCALIBRATION)] = '-';
  strcpy(&menu_title[strlen(STR_MENUCALIBRATION) + 1], STR_STICKS);

  check_submenu_simple(event, 0);
  title(menu_title);
  menuCommonCalib(event);
  if (menuEvent) {
    menuCalibrationState = CALIB_START;
  }
}

void menuFirstCalib(event_t event)
{
  if (event == EVT_KEY_BREAK(KEY_EXIT) || reusableBuffer.calib.state == CALIB_FINISHED) {
    menuCalibrationState = CALIB_START;
    chainMenu(menuMainView);
  } else {
    lcdDrawText(LCD_W / 2, 0, STR_MENUCALIBRATION, CENTERED);
    lcdInvertLine(0);
    menuCommonCalib(event);
  }
}
