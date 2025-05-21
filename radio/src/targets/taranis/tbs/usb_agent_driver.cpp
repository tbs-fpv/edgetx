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

#include "rtos.h"
#include "debug.h"
#include "fifo.h"
#include "usb_hid_agent.h"

#include "hal/usb_driver.h"
#include "hal/watchdog_driver.h"

#include "crsf/crossfire.h"
#include "crsf/crsf.h"

extern USBD_HandleTypeDef hUsbDevice;
extern usbMode selectedUsbMode;

#define HID_FIFO_SIZE 1024
#define LIBCRSF_BF_LINK_STATISTICS 0x14

struct HIDPacket_t {
  uint8_t buf[HID_AGENT_IN_SIZE];
};

static Fifo<uint8_t, HID_FIFO_SIZE> hidRxFifo;
static Fifo<HIDPacket_t, HID_FIFO_SIZE / sizeof(HIDPacket_t)> hidTxFifo;

extern "C" void USB_HID_Start_Receive(USBD_HandleTypeDef  *pdev);
extern "C" bool USBD_AGENT_Is_Busy(USBD_HandleTypeDef *pdev);

void usbAgentUpdate()
{
  uint8_t byte = 0;
  static libCrsfParseData crsfData;
  while (hidRxFifo.pop(byte)) {
    if (libCrsfParse(&crsfData, byte))
      libCrsfRouting(USB_HID, crsfData.payload);
    WDG_RESET();
  }
  USB_HID_Start_Receive(&hUsbDevice);
}

extern "C" void usbAgentTransmit() {
  static HIDPacket_t pkt;
  if (hidTxFifo.pop(pkt))
    USBD_AGENT_SendReport(&hUsbDevice, pkt.buf, HID_AGENT_IN_SIZE);
}

void crsfToUsbHid(uint8_t* pArr)
{
  if (!usbStarted() || selectedUsbMode != USB_AGENT_MODE) {
    return;
  }
  if (pArr[LIBCRSF_TYPE_ADD] == LIBCRSF_BF_LINK_STATISTICS ||
      pArr[LIBCRSF_TYPE_ADD] == LIBCRSF_OPENTX_RELATED) {
    return;
  }

  static HIDPacket_t pkt;

  pkt.buf[0] = LIBCRSF_UART_SYNC;

  const uint8_t length = pArr[LIBCRSF_LENGTH_ADD] + 2;
  for (uint32_t i = 1; i < length; i++)
    pkt.buf[i] = pArr[i];
  for (uint32_t i = length; i < HID_AGENT_IN_SIZE; i++)
    pkt.buf[i] = 0x0;

  if (USBD_AGENT_Is_Busy(&hUsbDevice)) {
    hidTxFifo.push(pkt);
  } else {
    USBD_AGENT_SendReport(&hUsbDevice, pkt.buf, HID_AGENT_IN_SIZE);
  }
}

extern "C" bool usbAgentReceive(const uint8_t* data)
{
  const uint8_t length = MIN(data[LIBCRSF_LENGTH_ADD] + 2, HID_AGENT_OUT_SIZE);
  if (hidRxFifo.hasSpace(length)) {
    for (uint32_t i = 0; i < length; i++)
      hidRxFifo.push(data[i]);
  }
  return hidRxFifo.hasSpace(HID_AGENT_OUT_SIZE);
}
