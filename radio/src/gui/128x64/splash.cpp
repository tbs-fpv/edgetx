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

#include "edgetx.h"

const unsigned char splashdata[]  = {
  'S','P','S',0,
#if defined(RADIO_TANGO2)
  #include "bitmaps/128x64/splash_tango2.lbm"
#elif defined(RADIO_MAMBO)
  #include "bitmaps/128x64/splash_mambo.lbm"
#else
  #include "bitmaps/128x64/splash.lbm"
#endif
  'S','P','E',0 };

const unsigned char * const splash_lbm = splashdata+4;

void drawSplash()
{
  lcdClear();
  lcdDraw1bitBitmap(0, 0, splash_lbm, 0, 0);
  lcdRefresh();
}

#if defined(TBS_CROSSFIRE_TASK) && !defined(SIMU)
const unsigned char downloaddata[]  = {
  'S','F','S',0,
#if defined(RADIO_TANGO2)
#include "bitmaps/128x64/download_96.lbm"
#elif defined(RADIO_MAMBO)
#include "bitmaps/128x64/download.lbm"
#endif
  'S','F','E',0 };

const unsigned char * const download_lbm = downloaddata+4;
void drawDownload()
{
  lcdClear();
  lcdDraw1bitBitmap(0, 0, download_lbm, 0, 0);
  if (getCrsfFlag(CRSF_FLAG_XF_UPDATE_REQUIRED))
  {
    clearCrsfFlag(CRSF_FLAG_XF_UPDATE_REQUIRED);
    lcdDrawText(LCD_W>>1, LCD_H-FH, STR_UPDATE_CROSSFIRE, CENTERED);
  }
  lcdRefresh();
  lcdRefreshWait();
}
#endif
