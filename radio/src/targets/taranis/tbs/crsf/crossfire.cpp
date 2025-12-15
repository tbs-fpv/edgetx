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

#include "stamp.h"
#include "crossfire.h"

#include "stm32_hal.h"
#include "stm32_dma.h"
#include "stm32_gpio.h"

#include "pulses/crossfire.h"
#include "mixer_scheduler.h"
#include "tasks.h"

extern void crsfToUsbHid(uint8_t * pArr);

#define ARRAY_SIZE(array) (sizeof(array) / sizeof(array[0]))

libCrsfPort CrsfPorts[] = {
  {DEVICE_INTERNAL,   &crsfThisDevice},
  {USB_HID,           &crsfToUsbHid},
  {CRSF_SHARED_FIFO,  &crsfToSharedFIFO},
};

uint8_t libCrsfMySlaveAddress = 0;
char * libCrsfMyDeviceName = nullptr;
uint32_t libCrsfMyHwID = 0;
uint32_t libCrsfMySerialNo = 0;
uint32_t libCrsfMyFwID = 0;

int16_t currentCrsfModelId = -1;
bool    crossfireStarted = false;

Fifo<uint8_t, CROSSFIRE_FIFO_SIZE> crsfRxFifo;
Fifo<uint8_t, CROSSFIRE_FIFO_SIZE> crsfTelemetryFifo;

const opentx_rtos_info_t *crossfireRtosInfo = (const opentx_rtos_info_t *)CROSSFIRE_ADDRESS;
volatile opentx_shared_data_t *crossfireSharedData = (volatile opentx_shared_data_t *)CROSSFIRE_SHARED_MEMORY_ADDRESS;

struct task_holder_t {
  const char *name;
  UBaseType_t prio;
  task_handle_t *handle;
  StackType_t *stack;
};

task_handle_t tbsRadioTaskId;
TASK_DEFINE_STACK(tbsRadioTaskStack, TBS_TASK_STACK_SIZE);

task_handle_t tbsMainTaskId;
TASK_DEFINE_STACK(tbsMainTaskStack, TBS_TASK_STACK_SIZE);

static task_holder_t task_holders[] = {
    [TASK_RADIO] =
        {
            .name = "radio",
            .prio = TBS_RADIO_TASK_PRIO,
            .handle = &tbsRadioTaskId,
            .stack = tbsRadioTaskStack,
        },
    [TASK_MAIN] =
        {
            .name = "main",
            .prio = TBS_MAIN_TASK_PRIO,
            .handle = &tbsMainTaskId,
            .stack = tbsMainTaskStack,
        },
};

static void rtos_create_task(task_id_t id, task_func_t func)
{
  task_holder_t *holder = &task_holders[id];
  task_create(holder->handle, func, holder->name, holder->stack, TBS_TASK_STACK_SIZE, holder->prio);
}

static uint32_t rtos_notify_wait()
{
  uint32_t notification_value = 0;
  xTaskNotifyWait(ULONG_MAX, ULONG_MAX, &notification_value, portMAX_DELAY);
  return notification_value;
}

static void rtos_notify(task_id_t id, uint32_t notification)
{
  task_holder_t *holder = &task_holders[id];
  if (holder->handle->_rtos_handle == NULL) return;

  xTaskNotify(holder->handle->_rtos_handle, notification, eSetValueWithOverwrite);
}

static void rtos_notify_isr(task_id_t id, uint32_t notification)
{
  task_holder_t *holder = &task_holders[id];
  if (holder->handle->_rtos_handle == NULL) return;

  BaseType_t woken = pdFALSE;
  xTaskNotifyFromISR(holder->handle->_rtos_handle, notification, eSetValueWithOverwrite, &woken);
  portYIELD_FROM_ISR(woken);
}

static void rtos_notify_clear(task_id_t id)
{
  task_holder_t *holder = &task_holders[id];
  if (holder->handle->_rtos_handle == NULL) return;
  xTaskNotifyStateClear(holder->handle->_rtos_handle);
}

static void rtos_enter_critical() { taskENTER_CRITICAL(); }
static void rtos_exit_critical() { taskEXIT_CRITICAL(); }
static void rtos_delay(uint32_t ticks) { vTaskDelay(ticks); }

static bool sd_read(const char* filename, uint32_t offset, uint8_t* data, uint32_t length)
{
  FIL file;
  if (f_open(&file, filename, FA_OPEN_ALWAYS | FA_WRITE | FA_READ) != FR_OK) return false;
  if (f_lseek(&file, offset) != FR_OK) {
    f_close(&file);
    return false;
  }

  UINT byteRead = 0;
  if (f_read(&file, data, length, &byteRead) != FR_OK)  {
    f_close(&file);
    return false;
  }

  return f_close(&file) == FR_OK;
}

static bool sd_write(const char* filename, uint32_t offset, uint8_t* data, uint32_t length)
{
  FIL file;
  if (f_open(&file, filename, FA_OPEN_ALWAYS | FA_WRITE | FA_READ) != FR_OK) return false;
  if (f_lseek(&file, offset) != FR_OK) {
    f_close(&file);
    return false;
  }

  UINT byteWrite = 0;
  if (f_write(&file, data, length, &byteWrite) != FR_OK)  {
    f_close(&file);
    return false;
  }

  return f_close(&file) == FR_OK;
}

static bool sd_erase(const char* filename)
{
  return f_unlink(filename) == FR_OK;
}

static uint32_t sd_get_file_size(const char* filename)
{
  FIL file;
  if (f_open(&file, filename, FA_OPEN_ALWAYS | FA_WRITE | FA_READ) != FR_OK) return 0;

  const uint32_t size = f_size(&file);
  f_close(&file);
  return size;
}

static bool crsf_to_opentx(const uint8_t *data, const uint32_t length)
{
  if (!crsfRxFifo.hasSpace(length))
    return false;

  for (uint32_t i = 0; i < length; i++)
    crsfRxFifo.push(data[i]);

  return true;
}

extern "C" void ESP_DMA_Stream_IRQHandler()
{
  DEBUG_INTERRUPT(INT_WIFI_DMA);
  if (!stm32_dma_check_tc_flag(ESP_DMA, ESP_DMA_Stream_RX))
    return;

  if (crossfireSharedData->trampoline.wifi_rx_dma_interrupt)
    crossfireSharedData->trampoline.wifi_rx_dma_interrupt();
}

extern "C" void INTERRUPT_EXTI_IRQHandler()
{
  DEBUG_INTERRUPT(INT_EXTI15_10);
  EXTI->PR |= (1 << 10) | (1 << 11) | (1 << 12) | (1 << 13);

  if (crossfireSharedData->trampoline.dio_interrupt_handler)
    crossfireSharedData->trampoline.dio_interrupt_handler();
}

extern "C" void EXTMODULE_TIMER_IRQHandler();

extern "C" void INTERRUPT_TIMER_IRQHandler()
{
  if (LL_TIM_IsEnabledIT_UPDATE(EXTMODULE_TIMER) && LL_TIM_IsActiveFlag_UPDATE(EXTMODULE_TIMER)) {
    // annoyingly INTERRUPT_TIMER_IRQHandler covers both TIM13 and TIM8.
    // lets call the handler here.
    EXTMODULE_TIMER_IRQHandler();
  }

  if (LL_TIM_IsEnabledIT_UPDATE(INTERRUPT_TIMER) && LL_TIM_IsActiveFlag_UPDATE(INTERRUPT_TIMER)) {
    DEBUG_INTERRUPT(INT_TIM13);
    LL_TIM_ClearFlag_UPDATE(INTERRUPT_TIMER);

    if (crossfireSharedData->trampoline.tx_tmr_interrupt_handler)
      crossfireSharedData->trampoline.tx_tmr_interrupt_handler();
  }
}

extern "C" void AUX_TIMER_IRQHandler()
{
  DEBUG_INTERRUPT(INT_TIM5);
  AUX_TIMER->SR &= ~TIM_SR_UIF;

  if (crossfireSharedData->trampoline.aux_tmr_interrupt_handler)
    crossfireSharedData->trampoline.aux_tmr_interrupt_handler();
}


void crsfPackParam(uint8_t * pArr)
{
  uint32_t count = 0;

  libUtilWrite8(pArr, &count, LIBCRSF_UART_SYNC); /* device address */
  libUtilWrite8(pArr, &count, 0);                 /* frame length */
  libUtilWrite8(pArr, &count, LIBCRSF_EX_PARAM_SETTING_ENTRY); /* cmd type */
  libUtilWrite8(pArr, &count, LIBCRSF_USB_HOST_ADD);     /* Destination Address */
  libUtilWrite8(pArr, &count, LIBCRSF_REMOTE_ADD);/* Origin Address */
  libUtilWrite8(pArr, &count, 0x0);              /* param number */

  uint8_t crc1 = libCRC8GetCRCArr(&pArr[2], count-2, POLYNOM_1);
  libUtilWrite8(pArr, &count, crc1);

  pArr[LIBCRSF_LENGTH_ADD] = count - 2;
}

void crsfInit(void)
{
  const uint32_t serial_num = readBackupReg(BKREG_SERIAL_NO_RADIO);
  const uint32_t hw_id = readBackupReg(BKREG_HW_ID_RADIO);
  const uint32_t fw_id = (VERSION_MAJOR << 8) | VERSION_MINOR;

  libCrsfInit(LIBCRSF_REMOTE_ADD, (char *)MY_DEVICE_NAME, serial_num, hw_id, fw_id);
  libCrsfAddDeviceFunctionList( &CrsfPorts[0], ARRAY_SIZE(CrsfPorts));

  memset((void *)CROSSFIRE_SHARED_MEMORY_ADDRESS, 0, CROSSFIRE_SHARED_MEMORY_SIZE);

  crossfireSharedData->rtos_api_version = CROSSFIRE_RTOS_API_VERSION;
  crossfireSharedData->trampoline.rtos_create_task = rtos_create_task;
  crossfireSharedData->trampoline.rtos_notify_wait = rtos_notify_wait;
  crossfireSharedData->trampoline.rtos_notify = rtos_notify;
  crossfireSharedData->trampoline.rtos_notify_isr = rtos_notify_isr;
  crossfireSharedData->trampoline.rtos_notify_clear = rtos_notify_clear;
  crossfireSharedData->trampoline.rtos_enter_critical = rtos_enter_critical;
  crossfireSharedData->trampoline.rtos_exit_critical = rtos_exit_critical;
  crossfireSharedData->trampoline.rtos_delay = rtos_delay;

  crossfireSharedData->trampoline.sd_read = sd_read;
  crossfireSharedData->trampoline.sd_write = sd_write;
  crossfireSharedData->trampoline.sd_erase = sd_erase;
  crossfireSharedData->trampoline.sd_get_file_size = sd_get_file_size;

  crossfireSharedData->trampoline.crsf_to_opentx = crsf_to_opentx;

  crossfireSharedData->trampoline.mixer_notify = mixerSchedulerSoftTrigger;

#if defined(RADIO_TANGO2)
  const uint8_t hw_revision = hw_id & 0xFF;
  hardwareOptions.pcbrev = (hw_revision == 0x01) ? PCBREV_Tango2_V1 :
                           (hw_revision == 0x02) ? PCBREV_Tango2_V2 :
                           (hw_revision >= 0x03) ? PCBREV_Tango2_V3 : PCBREV_Tango2_Unknown;

  gpio_init(INTMODULE_PWR_GPIO, GPIO_OUT, GPIO_PIN_SPEED_LOW);
  if (IS_PCBREV_01() || IS_PCBREV_02()) gpio_clear(INTMODULE_PWR_GPIO);
  else gpio_set(INTMODULE_PWR_GPIO);
#elif defined(RADIO_MAMBO)
  const uint8_t hw_revision = hw_id & 0xFF;
  hardwareOptions.pcbrev = (hw_revision == 0x01) ? PCBREV_Mambo_V1 :
                           (hw_revision >= 0x02) ? PCBREV_Mambo_V2 : PCBREV_Mambo_Unknown;

  gpio_init(INTMODULE_PWR_GPIO, GPIO_OUT, GPIO_PIN_SPEED_LOW);
  gpio_set(INTMODULE_PWR_GPIO);
#endif

  gpio_init(ESP_EN_GPIO, GPIO_OUT, GPIO_PIN_SPEED_LOW);
  gpio_clear(ESP_EN_GPIO);

  setCrsfFlag(CRSF_FLAG_RF_OFF);
}

void libCrsfUnpackRemote(uint8_t * pArr, libCrsfRemoteDataU * remoteData)
{
  uint32_t j = LIBCRSF_EXT_PAYLOAD_START_ADD;
  uint32_t *i = &j;
  libCrsfRemoteFrame remote_command_id = (libCrsfRemoteFrame)libUtilRead8(pArr, i);

  switch (remote_command_id) {
#ifdef LIBCRSF_ENABLE_SD
    case LIBCRSF_REMOTE_SD_MOUNT_STATUS:
      remoteData->mountStatus.isMounted = libUtilReadInt8(pArr, i);
      break;
#endif // LIBCRSF_ENABLE_SD
    default:
      break;
  }
}

static bool crsfRemoteRelatedHandler(uint8_t * pArr)
{
#ifdef LIBCRSF_ENABLE_SD
  libCrsfRemoteDataU data;
  libCrsfUnpackRemote(pArr, &data);

  libCrsfRemoteDataU reply;

  switch (pArr[LIBCRSF_EXT_PAYLOAD_START_ADD])
  {
    case LIBCRSF_REMOTE_SD_MOUNT_STATUS:
      reply.mountStatus.isMounted = (uint8_t)sdMounted();
      libCrsfWrite(LIBCRSF_OPENTX_RELATED, &pArr[LIBCRSF_LENGTH_ADD], pArr[LIBCRSF_EXT_HEAD_ORG_ADD], LIBCRSF_REMOTE_SD_MOUNT_STATUS, &reply);
      libCrsfRouting(DEVICE_INTERNAL, &pArr[0]);
      return true;
    default:
      return false;
  }
#endif
}

void crsfThisDevice(uint8_t * pArr)
{
  uint8_t arr[LIBCRSF_MAX_BUFFER_SIZE];
  /* handle parameter and command frames */
  switch (*(pArr + LIBCRSF_TYPE_ADD))
  {
    case LIBCRSF_EX_PARAM_PING_DEVICE:
      if (libCrsfCheckIfDeviceCalled(pArr, true)) {
        // Parameter_Pack_Device_Information( &arr[LIBCRSF_LENGTH_ADD] );
        libCrsfWrite(LIBCRSF_EX_PARAM_DEVICE_INFO, &arr[LIBCRSF_LENGTH_ADD]);
        libCrsfRouting(DEVICE_INTERNAL, &arr[0]);
      }
      break;

    case LIBCRSF_EX_PARAM_SETTING_READ:
      crsfPackParam(pArr);
      libCrsfRouting(DEVICE_INTERNAL, &pArr[0]);
      break;

#ifdef LIBCRSF_ENABLE_COMMAND
    case LIBCRSF_CMD_FRAME:
      if ((*(pArr + *(pArr + LIBCRSF_LENGTH_ADD) + LIBCRSF_HEADER_OFFSET - 1)) !=
          libCRC8GetCRCArr(( pArr + LIBCRSF_TYPE_ADD), *(pArr + LIBCRSF_LENGTH_ADD) - 2, POLYNOM_2)) {
        break;
      }
      if (*(pArr + LIBCRSF_PAYLOAD_START_ADD + 2) == LIBCRSF_GENERAL_CMD &&
          *(pArr + LIBCRSF_PAYLOAD_START_ADD + 3) == LIBCRSF_GENERAL_START_BOOTLOADER_SUBCMD) {
        writeBackupReg(BKREG_PREPARE_FWUPDATE, 1);
        writeBackupReg(BKREG_HW_ID, readBackupReg(BKREG_HW_ID_RADIO));
        writeBackupReg(BKREG_SERIAL_NO, libCrsfMySerialNo);
        bkregSetStatusFlag(DEVICE_RESTART_WITHOUT_WARN_FLAG);

        setCrsfFlag(CRSF_FLAG_SHOW_BOOTLOADER_ICON);
        setCrsfFlag(CRSF_FLAG_SCHEDULE_REBOOT);
      } else if (*(pArr + LIBCRSF_EXT_PAYLOAD_START_ADD) == LIBCRSF_RC_RX_CMD &&
                 *(pArr + LIBCRSF_EXT_PAYLOAD_START_ADD + 1) == LIBCRSF_RC_RX_REPLY_CURRENT_MODEL_SUBCMD) {
        currentCrsfModelId = *(pArr + LIBCRSF_EXT_PAYLOAD_START_ADD + 2);
      }
      break;
#endif

    case LIBCRSF_OPENTX_RELATED:
      if (crsfRemoteRelatedHandler(pArr))
        break;
      // fallthrough if unhandled

    default:
      // push to fifo to make available to internal impl
      for (uint32_t i = 0; i < (uint32_t)(pArr[LIBCRSF_LENGTH_ADD] + 2); i++) {
        crsfTelemetryFifo.push(*(pArr + i));
      }
      telemetryFrameTrigger(INTERNAL_MODULE, &CrossfireDriver);
      break;
  }
}

void crsfFifoHandler()
{
  static libCrsfParseData crsfData;

  uint8_t data = 0;
  while (crsfRxFifo.pop(data))
    if (libCrsfParse(&crsfData, data)) {
      WDG_RESET();
      libCrsfRouting(CRSF_SHARED_FIFO, crsfData.payload);
    }
}

void crsfToSharedFIFO(uint8_t * pArr)
{
  const uint8_t length = pArr[LIBCRSF_LENGTH_ADD] + LIBCRSF_HEADER_OFFSET + LIBCRSF_CRC_SIZE;
  *pArr = LIBCRSF_UART_SYNC;

  if (!crossfireSharedData->trampoline.opentx_to_crsf)
    return;

  while(!crossfireSharedData->trampoline.opentx_to_crsf(pArr, length)) {
    crsfFifoHandler();
    vTaskDelay(5);
  }
}

void crsfSetModelID(uint8_t modelId)
{
  uint32_t count = 0;
  BYTE txBuf[LIBCRSF_MAX_BUFFER_SIZE];

  libUtilWrite8(txBuf, &count, LIBCRSF_UART_SYNC); /* device address */
  libUtilWrite8(txBuf, &count, 0);                 /* frame length */
  libUtilWrite8(txBuf, &count, LIBCRSF_CMD_FRAME); /* cmd type */
  libUtilWrite8(txBuf, &count, LIBCRSF_RC_TX);     /* Destination Address */
  libUtilWrite8(txBuf, &count, LIBCRSF_REMOTE_ADD);/* Origin Address */
  libUtilWrite8(txBuf, &count, LIBCRSF_RC_RX_CMD); /* sub command */
  libUtilWrite8(txBuf, &count, LIBCRSF_RC_RX_MODEL_SELECTION_SUBCMD);    /* command of set model/receiver id */
  libUtilWrite8(txBuf, &count, modelId); /* model ID */

  uint8_t crc2 = libCRC8GetCRCArr(&txBuf[2], count-2, POLYNOM_2);
  libUtilWrite8(txBuf, &count, crc2);
  uint8_t crc1 = libCRC8GetCRCArr(&txBuf[2], count-2, POLYNOM_1);
  libUtilWrite8(txBuf, &count, crc1);

  txBuf[LIBCRSF_LENGTH_ADD] = count - 2;

  crsfToSharedFIFO(txBuf);
}

void crsfGetModelID(void)
{
  uint32_t count = 0;
  BYTE txBuf[LIBCRSF_MAX_BUFFER_SIZE];

  libUtilWrite8(txBuf, &count, LIBCRSF_UART_SYNC); /* device address */
  libUtilWrite8(txBuf, &count, 0);                 /* frame length */
  libUtilWrite8(txBuf, &count, LIBCRSF_CMD_FRAME); /* cmd type */
  libUtilWrite8(txBuf, &count, LIBCRSF_RC_TX);     /* Destination Address */
  libUtilWrite8(txBuf, &count, LIBCRSF_REMOTE_ADD);/* Origin Address */
  libUtilWrite8(txBuf, &count, LIBCRSF_RC_RX_CMD); /* sub command */
  libUtilWrite8(txBuf, &count, LIBCRSF_RC_RX_CURRENT_MODEL_SELECTION_SUBCMD);  /* command of set model/receiver id */
  libUtilWrite8(txBuf, &count, 0);                 /* the dummy byte of model ID */

  uint8_t crc2 = libCRC8GetCRCArr(&txBuf[2], count-2, POLYNOM_2);
  libUtilWrite8(txBuf, &count, crc2);
  uint8_t crc1 = libCRC8GetCRCArr(&txBuf[2], count-2, POLYNOM_1);
  libUtilWrite8(txBuf, &count, crc1);

  txBuf[LIBCRSF_LENGTH_ADD] = count - 2;

  crsfToSharedFIFO(txBuf);
}

#if !defined(SIMU)
uint32_t readBackupReg(uint8_t index) {
  return *(__IO uint32_t *) (BKPSRAM_BASE + index * 4);
}

void writeBackupReg(uint8_t index, uint32_t data)
{
  __HAL_RCC_PWR_CLK_ENABLE();
  HAL_PWREx_EnableBkUpReg();

  __HAL_RCC_BKPSRAM_CLK_ENABLE();
  HAL_PWR_EnableBkUpAccess();

  *(__IO uint32_t *)(BKPSRAM_BASE + index*4) = data;
}

uint8_t bkregGetStatusFlag(uint32_t flag)
{
  uint32_t value = (uint32_t)readBackupReg(BKREG_STATUS_FLAG);
  return ((value & (1 << flag)) ? 1 : 0);
}

void bkregSetStatusFlag(uint32_t flag)
{
  uint32_t value = (uint32_t)readBackupReg(BKREG_STATUS_FLAG);
  value |= (1 << flag);
  writeBackupReg(BKREG_STATUS_FLAG, value);
}

void bkregClrStatusFlag(uint32_t flag)
{
  uint32_t value = (uint32_t)readBackupReg(BKREG_STATUS_FLAG);
  value &= ~(1 << flag);
  writeBackupReg(BKREG_STATUS_FLAG, value);
}

void crossfirePowerOff()
{
  const uint32_t offTimeout = get_tmr10ms();
  setCrsfFlag(CRSF_FLAG_POWER_OFF);
  while (getCrsfFlag(CRSF_FLAG_POWER_OFF) && (get_tmr10ms() - offTimeout) < 250)
    WDG_RESET();

  for (uint32_t i = 0; i < TASK_MAX; i++) {
    task_holder_t *holder = &task_holders[i];
    vTaskDelete(holder->handle->_rtos_handle);
  }
}

void crossfireSuspend()
{
  for (uint32_t i = 0; i < TASK_MAX; i++) {
    task_holder_t *holder = &task_holders[i];
    vTaskSuspend(holder->handle->_rtos_handle);
  }
}

void crossfireResume()
{
  for (uint32_t i = 0; i < TASK_MAX; i++) {
    task_holder_t *holder = &task_holders[i];
    vTaskResume(holder->handle->_rtos_handle);
  }
}

bool isCrossfireRfOn()
{
  return !getCrsfFlag(CRSF_FLAG_RF_OFF);
}

bool isCrossfireStarted()
{
  return crossfireStarted;
}

void crossfireTurnOffRf()
{
  setCrsfFlag(CRSF_FLAG_RF_OFF);
}

void crossfireTurnOnRf()
{
  clearCrsfFlag(CRSF_FLAG_RF_OFF);
}

void crossfireFirmwareStart()
{
  if (crossfireRtosInfo->magic != CROSSFIRE_MAGIC ||
    crossfireRtosInfo->entrypoint == 0xFFFFFFFF ||
    crossfireRtosInfo->rtos_api_version < CROSSFIRE_RTOS_API_VERSION)
  {
    // No valid firmware found
    writeBackupReg(BKREG_PREPARE_FWUPDATE, 1);
    writeBackupReg(BKREG_HW_ID, readBackupReg(BKREG_HW_ID_XF));
    writeBackupReg(BKREG_SERIAL_NO, libCrsfMySerialNo);
    bkregSetStatusFlag(DEVICE_RESTART_WITHOUT_WARN_FLAG);

    setCrsfFlag(CRSF_FLAG_SHOW_BOOTLOADER_ICON);
    setCrsfFlag(CRSF_FLAG_XF_UPDATE_REQUIRED);
    return;
  }

  extern uint8_t startOptions;
  if (bkregGetStatusFlag(DEVICE_RESTART_WITHOUT_WARN_FLAG)) {
    startOptions = OPENTX_START_NO_SPLASH | OPENTX_START_NO_CALIBRATION | OPENTX_START_NO_CHECKS;
    bkregClrStatusFlag(DEVICE_RESTART_WITHOUT_WARN_FLAG);
  }

  void (*init_ptr)(void) = (void (*)())(&crossfireRtosInfo->entrypoint);
  crossfireStarted = true;
  return init_ptr();
}
#endif
