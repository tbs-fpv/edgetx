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
#ifndef __USB_HID_AGENT_H_
#define __USB_HID_AGENT_H_

#if defined(__cplusplus)
extern "C" {
#endif

#include "usbd_desc.h"

#define HID_AGENT_IN_EP              0x81
#define HID_AGENT_OUT_EP             0x01

#define HID_AGENT_IN_SIZE            64
#define HID_AGENT_OUT_SIZE           64

extern USBD_ClassTypeDef  USBD_AGENT;	// modified by OpenTX

uint8_t USBD_AGENT_SendReport (USBD_HandleTypeDef  *pdev,
                                 uint8_t *report,
                                 uint16_t len);

#if defined(__cplusplus)
}
#endif

#endif  // __USB_HID_AGENT_H_
