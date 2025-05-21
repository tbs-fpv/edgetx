/*
 * Copyright (C) EdgeTx
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

#include "soft_internal_module_driver.h"

#include "edgetx.h"

#define MIN(a,b) (a < b ? a : b)

static void* software_internal_tx_init(void* hw_def, const etx_serial_init* params)
{
  crossfireTurnOnRf();
  return hw_def;
}

static void software_internal_tx_deinit(void* ctx)
{
  crossfireTurnOffRf();
}

static int software_internal_rx_get_byte(void* ctx, uint8_t* data)
{
  return crsfTelemetryFifo.pop(*data) ? 1 : 0;
}

static void software_internal_rx_clear_rx_buffer(void* ctx)
{
  crsfTelemetryFifo.clear();
}

static void software_internal_tx_send_byte(void* ctx, uint8_t byte)
{
  crossfireSharedData->trampoline.opentx_to_crsf(&byte, 1);
}

static void software_internal_tx_send_buffer(void* ctx, const uint8_t* data, uint32_t size)
{
  crossfireSharedData->trampoline.opentx_to_crsf(data, size);
}


static int software_internal_copy_rx_buffer(void* ctx, uint8_t* buf, uint32_t len) {
  auto cp_len = MIN(len, crsfTelemetryFifo.size());
  for (uint32_t i = 0; i < cp_len; i++)
  {
    if (!crsfTelemetryFifo.pop(buf[i]))
      break;
  }
  return cp_len;
}

const etx_serial_driver_t SoftwareModuleDriver = {
  .init = software_internal_tx_init,
  .deinit = software_internal_tx_deinit,
  .sendByte = software_internal_tx_send_byte,
  .sendBuffer = software_internal_tx_send_buffer,
  .txCompleted = nullptr,
  .waitForTxCompleted = nullptr,
  .enableRx = nullptr,
  .getByte = software_internal_rx_get_byte,
  .getLastByte = nullptr,
  .getBufferedBytes = nullptr,
  .copyRxBuffer = software_internal_copy_rx_buffer,
  .clearRxBuffer = software_internal_rx_clear_rx_buffer,
  .getBaudrate = nullptr,
  .setBaudrate = nullptr,
  .setPolarity = nullptr,
  .setHWOption = nullptr,
  .setReceiveCb = nullptr,
  .setIdleCb = nullptr,
  .setBaudrateCb = nullptr,
};
