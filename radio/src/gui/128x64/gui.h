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

#pragma once

#include "gui_common.h"
#include "menus.h"
#include "widgets_common.h"

#include "navigation/navigation.h"
#include "draw_functions.h"

#define MENUS_SCROLLBAR_WIDTH          0
#if LCD_DEPTH == 4
#define GREY(x)                        ((x) * 0x010000)
#define GREY_DEFAULT                   GREY(11)
#define FILL_WHITE                     0x10
#endif
#define MIXES_2ND_COLUMN               (10*FW)

#if defined(MANUFACTURER_TBS) && !defined(SIMU)
typedef enum {
  MAINSCREEN_GRAPHICS_NONE = 0,
  MAINSCREEN_GRAPHICS_STICKS = 0x01,
  MAINSCREEN_GRAPHICS_POTS = 0x02,
  MAINSCREEN_GRAPHICS_ALL = 0xFF
} MainScreenGraphicsView;
void drawDownload();
void doMainScreenGraphicsCalibration(uint8_t viewMask, int16_t * sticksOverride);
#endif

void drawWheel(coord_t centrex, int16_t wval);
void drawThrottle(coord_t centrex, int16_t tval);

void drawProgressScreen(const char * title, const char * message, int num, int den);

#define SET_SCROLLBAR_X(x)
#define LOAD_MODEL_BITMAP()

#define IS_OTHER_VIEW_DISPLAYED()      menuHandlers[0] == menuChannelsView
