/*
 * Copyright (C) EdgeTX
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
#pragma once

#include "fifo.h"

#define CROSSFIRE_MAGIC 0xBAAAAAAD
#define CROSSFIRE_ADDRESS 0x080C0000
#define CROSSFIRE_RTOS_API_VERSION 0x200

#define CROSSFIRE_SHARED_MEMORY_ADDRESS 0x10000000
#define CROSSFIRE_SHARED_MEMORY_SIZE 0x400

#define CROSSFIRE_FIFO_SIZE 512
#define CROSSFIRE_CHANNELS_COUNT 16

enum _CRSF_PORT_NAME {
  DEVICE_INTERNAL = 0,
  USB_HID,
  CRSF_SHARED_FIFO,
  CRSF_ESP,
  LAST_CRSF_PORT
};

enum opentx_crsf_flag_t
{
  CRSF_FLAG_SCHEDULE_REBOOT = 0,
  CRSF_FLAG_POWER_OFF,
  CRSF_FLAG_RF_OFF,
  CRSF_FLAG_XF_UPDATE_REQUIRED,
  CRSF_FLAG_SHOW_BOOTLOADER_ICON,
  CRSF_FLAG_COUNT,
  CRSF_FLAG_MAX = 32
};

enum opentx_bkregs_t
{
  BKREG_PREPARE_FWUPDATE = 0x0,
  BKREG_HW_ID,
  BKREG_SKIP_BOARD_OFF,
  BKREG_SERIAL_NO,
  BKREG_HW_ID_RADIO,
  BKREG_HW_ID_XF,
  BKREG_SERIAL_NO_RADIO,
  BKREG_SERIAL_NO_XF,
  BKREG_STATUS_FLAG,
  BKREG_DEFAULT_WORD,
  BKREG_INDEX_COUNT
};

enum opentx_bkreg_flag_t
{
  DEVICE_RESTART_WITHOUT_WARN_FLAG = 0x0,
  STORAGE_ERASE_STATUS,
  BKREG_STATUS_FLAG_COUNT,
};

enum task_id_t : uint8_t {
  TASK_RADIO,
  TASK_MAIN,
  TASK_MAX,
};

typedef void (*task_func_t)(void *);

struct opentx_trampoline_t {
  void (*rtos_create_task)(task_id_t id, task_func_t func);
  uint32_t (*rtos_notify_wait)(void);
  void (*rtos_notify)(task_id_t id, uint32_t notification);
  void (*rtos_notify_isr)(task_id_t id, uint32_t notification);
  void (*rtos_notify_clear)(task_id_t id);
  void (*rtos_enter_critical)(void);
  void (*rtos_exit_critical)(void);
  void (*rtos_delay)(uint32_t ticks);

  bool (*crsf_to_opentx)(const uint8_t *data, const uint32_t length);  // from XF to OpenTX
  bool (*opentx_to_crsf)(const uint8_t *data, const uint32_t length);  // from OpenTX to XF

  bool (*sd_read)(const char* filename, uint32_t offset, uint8_t* data, uint32_t length);
  bool (*sd_write)(const char* filename, uint32_t offset, uint8_t* data, uint32_t length);
  bool (*sd_erase)(const char* filename);
  uint32_t (*sd_get_file_size)(const char* filename);

  void (*mixer_notify)();
  void (*read_sticks)(uint32_t period, uint16_t *sticks);

  void (*dio_interrupt_handler)(void);
  void (*tx_tmr_interrupt_handler)(void);
  void (*aux_tmr_interrupt_handler)(void);
  void (*wifi_rx_dma_interrupt)(void);
};

struct opentx_shared_data_t {
  uint32_t rtos_api_version;
  opentx_trampoline_t trampoline;

  uint32_t flags;
  uint8_t stick_state;

  int32_t channels[16];
};

extern volatile opentx_shared_data_t *crossfireSharedData;
struct  __attribute__((packed)) opentx_rtos_info_t {
  uint32_t magic;
  uint32_t rtos_api_version;
  uint8_t _padding[25];
  uint32_t entrypoint;
};
static_assert(offsetof(opentx_rtos_info_t, entrypoint) == 0x21, "wrong offset for entrypoint");

extern const opentx_rtos_info_t *crossfireRtosInfo;

// internal module driver
#if defined(SIMU)
  #define readBackupReg(...)                        0
  #define writeBackupReg()
  #define bkregGetStatusFlag(...)                   0
  #define bkregSetStatusFlag(...)
  #define bkregClrStatusFlag(...)

  #define crossfirePowerOff()
  #define crossfireSuspend()
  #define crossfireResume()
  #define crossfireTurnOffRf(...)
  #define crossfireTurnOnRf()
  #define crossfireFirmwareStart()
#else
  uint32_t readBackupReg(uint8_t index);
  void writeBackupReg(uint8_t index, uint32_t data);
  uint8_t bkregGetStatusFlag(uint32_t flag);
  void bkregSetStatusFlag(uint32_t flag);
  void bkregClrStatusFlag(uint32_t flag);

  void crossfirePowerOff();
  void crossfireSuspend();
  void crossfireResume();
  void crossfireTurnOffRf();
  void crossfireTurnOnRf();
  bool isCrossfireRfOn();
  bool isCrossfireStarted();
  void crossfireFirmwareStart();
#endif

#define getCrsfFlag(i) (crossfireSharedData->flags & (1UL << i) ? true : false)
#define setCrsfFlag(i) (crossfireSharedData->flags |= (1UL << i))
#define clearCrsfFlag(i) (crossfireSharedData->flags &= ~(1UL << i))

#define setStickState(state) crossfireSharedData->stick_state = (state);

void crsfInit(void);
void crsfSetModelID(uint8_t modelId);
void crsfGetModelID(void);
void crsfToSharedFIFO(uint8_t * p_arr);
void crsfThisDevice(uint8_t * p_arr);
void crsfWriteByte(uint8_t data);
void crsfFifoHandler();

extern Fifo<uint8_t, CROSSFIRE_FIFO_SIZE> crsfTelemetryFifo;
