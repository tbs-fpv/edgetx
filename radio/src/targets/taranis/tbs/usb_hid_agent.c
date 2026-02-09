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

/* Includes ------------------------------------------------------------------*/
#include "usbd_hid.h"
#include "usbd_ctlreq.h"
#include "usbd_desc.h"
#include "debug.h"
#include "usb_hid_agent.h"

#define USB_AGENT_CONFIG_DESC_SIZ     41

/** @addtogroup STM32_USB_OTG_DEVICE_LIBRARY
  * @{
  */


/** @defgroup USBD_HID
  * @brief usbd core module
  * @{
  */

/** @defgroup USBD_HID_Private_TypesDefinitions
  * @{
  */
/**
  * @}
  */


/** @defgroup USBD_HID_Private_Defines
  * @{
  */

/**
  * @}
  */


/** @defgroup USBD_HID_Private_Macros
  * @{
  */
/**
  * @}
  */


/** @defgroup USBD_HID_Private_FunctionPrototypes
  * @{
  */


static uint8_t USBD_HID_Init (USBD_HandleTypeDef  *pdev, uint8_t cfgidx);
static uint8_t USBD_HID_DeInit (USBD_HandleTypeDef  *pdev, uint8_t cfgidx);
static uint8_t USBD_HID_Setup (USBD_HandleTypeDef  *pdev,  USBD_SetupReqTypedef *req);
static uint8_t USBD_HID_DataIn (USBD_HandleTypeDef  *pdev, uint8_t epnum);
static uint8_t USBD_HID_DataOut (USBD_HandleTypeDef  *pdev, uint8_t epnum);
static uint8_t *USBD_HID_GetFSCfgDesc(uint16_t *length);
static uint8_t *USBD_HID_GetHSCfgDesc(uint16_t *length);
static uint8_t *USBD_HID_GetOtherSpeedCfgDesc(uint16_t *length);
static uint8_t *USBD_HID_GetDeviceQualifierDesc(uint16_t *length);
/**
  * @}
  */

__ALIGN_BEGIN static const uint8_t HID_AGENT_ReportDesc[] __ALIGN_END =
{
	0x06, 0x00, 0xFF,   // Usage Page = 0xFF00 (Vendor Defined Page 1)
	0x09, 0x01,             // Usage (Vendor Usage 1)
	0xA1, 0x01,             // Colsslection (Application)
	0x19, 0x01,             // Usage Minimum
	0x29, 0x40,             // Usage Maximum 	//64 input usages total (0x01 to 0x40)
	0x15, 0x01,             // Logical Minimum (data bytes in the report may have minimum value = 0x00)
	0x25, 0x40,      	    // Logical Maximum (data bytes in the report may have maximum value = 0x00FF = unsigned 255)
	0x75, 0x08,             // Report Size: 8-bit field size
	0x95, 0x40,             // Report Count: Make sixty-four 8-bit fields (the next time the parser hits an "Input", "Output", or "Feature" item)
	0x81, 0x00,             // Input (Data, Array, Abs): Instantiates input packet fields based on the above report size, count, logical min/max, and usage.
	0x19, 0x01,             // Usage Minimum
	0x29, 0x40,             // Usage Maximum 	//64 output usages total (0x01 to 0x40)
	0x91, 0x00,             // Output (Data, Array, Abs): Instantiates output packet fields.  Uses same report size and count as "Input" fields, since nothing new/different was specified to the parser since the "Input" item.
	0xC0                    // End Collection
};


/** @defgroup USBD_HID_Private_Variables
  * @{
  */

USBD_ClassTypeDef USBD_AGENT =
{
  USBD_HID_Init,
  USBD_HID_DeInit,
  USBD_HID_Setup,
  NULL,             /*EP0_TxSent*/
  NULL,             /*EP0_RxReady*/
  USBD_HID_DataIn,  /*DataIn*/
  USBD_HID_DataOut, /*DataOut*/
  NULL,             /*SOF */
  NULL,
  NULL,
  USBD_HID_GetHSCfgDesc,
  USBD_HID_GetFSCfgDesc,
  USBD_HID_GetOtherSpeedCfgDesc,
  USBD_HID_GetDeviceQualifierDesc,
};

/* USB HID device Configuration Descriptor */
__ALIGN_BEGIN static uint8_t USBD_HID_CfgDesc[USB_AGENT_CONFIG_DESC_SIZ] __ALIGN_END =
{
  0x09,                         /* bLength: Configuration Descriptor size */
  USB_DESC_TYPE_CONFIGURATION,  /* bDescriptorType: Configuration */
  USB_AGENT_CONFIG_DESC_SIZ,    /* wTotalLength: Bytes returned */
  0x00,
  0x01,                         /*bNumInterfaces: 1 interface*/
  0x01,                         /*bConfigurationValue: Configuration value*/
  0x00,                         /*iConfiguration: Index of string descriptor describing
                                  the configuration*/
  0xE0,                         /*bmAttributes: bus powered and Support Remote Wake-up */
  USBD_MAX_POWER,               /*MaxPower 100 mA: this current is used for detecting Vbus*/

  /************** Descriptor of Joystick Mouse interface ****************/
  /* 09 */
  0x09,                         /*bLength: Interface Descriptor size*/
  USB_DESC_TYPE_INTERFACE,      /*bDescriptorType: Interface descriptor type*/
  0x00,                         /*bInterfaceNumber: Number of Interface*/
  0x00,                         /*bAlternateSetting: Alternate setting*/
  0x02,                         /*bNumEndpoints*/
  0x03,                         /*bInterfaceClass: HID*/
  0x00,                         /*bInterfaceSubClass : 1=BOOT, 0=no boot*/
  0x00,                         /*nInterfaceProtocol : 0=none, 1=keyboard, 2=mouse*/
  0,                            /*iInterface: Index of string descriptor*/
  /******************** Descriptor of Joystick Mouse HID ********************/
  /* 18 */
  0x09,                         /*bLength: HID Descriptor size*/
  HID_DESCRIPTOR_TYPE,          /*bDescriptorType: HID*/
  0x11,                         /*bcdHID: HID Class Spec release number*/
  0x01,
  0x00,                         /*bCountryCode: Hardware target country*/
  0x01,                         /*bNumDescriptors: Number of HID class descriptors to follow*/
  0x22,                         /*bDescriptorType*/
  sizeof(HID_AGENT_ReportDesc), /*wItemLength: Total length of Report descriptor*/
  0x00,
  /******************** Descriptor of Mouse endpoint ********************/
  /* 27 */
  0x07,                         /*bLength: Endpoint Descriptor size*/
  USB_DESC_TYPE_ENDPOINT,       /*bDescriptorType:*/
  HID_AGENT_IN_EP,              /*bEndpointAddress: Endpoint Address (IN)*/
  0x03,                         /*bmAttributes: Interrupt endpoint*/
  HID_AGENT_IN_SIZE,          /*wMaxPacketSize: 64 Byte max */
  0x00,
  0x01,                         /*bInterval: Polling Interval (1 ms)*/
  /******************** Descriptor of Mouse endpoint ********************/
  /* 34 */
  0x07,                         /*bLength: Endpoint Descriptor size*/
  USB_DESC_TYPE_ENDPOINT,       /*bDescriptorType:*/
  HID_AGENT_OUT_EP,             /*bEndpointAddress: Endpoint Address (OUT)*/
  0x03,                         /*bmAttributes: Interrupt endpoint*/
  HID_AGENT_OUT_SIZE,         /*wMaxPacketSize: 64 Byte max */
  0x00,
  0x01,                         /*bInterval: Polling Interval (1 ms)*/
  /* 41 */
};

/* USB HID device Configuration Descriptor */

__ALIGN_BEGIN static uint8_t USBD_HID_Desc[USB_HID_DESC_SIZ] __ALIGN_END =
{
  /* 18 */
  0x09,                                               /* bLength: HID Descriptor size */
  HID_DESCRIPTOR_TYPE,                                /* bDescriptorType: HID */
  0x11,                                               /* bcdHID: HID Class Spec release number */
  0x01,
  0x00,                                               /* bCountryCode: Hardware target country */
  0x01,                                               /* bNumDescriptors: Number of HID class descriptors to follow */
  0x22,                                               /* bDescriptorType */
  HID_MOUSE_REPORT_DESC_SIZE,                         /* wItemLength: Total length of Report descriptor */
  0x00,
};

/* USB Standard Device Descriptor */
__ALIGN_BEGIN static uint8_t USBD_HID_DeviceQualifierDesc[USB_LEN_DEV_QUALIFIER_DESC] __ALIGN_END =
{
  USB_LEN_DEV_QUALIFIER_DESC,
  USB_DESC_TYPE_DEVICE_QUALIFIER,
  0x00,
  0x02,
  0x00,
  0x00,
  0x00,
  0x40,
  0x01,
  0x00,
};


uint8_t HID_Buffer[HID_AGENT_OUT_SIZE];
static uint8_t HIDInEpAdd = HID_AGENT_IN_EP;
static uint8_t HIDOutEpAdd = HID_AGENT_OUT_EP;

/**
  * @}
  */

/** @defgroup USBD_HID_Private_Functions
  * @{
  */

/**
  * @brief  USBD_HID_Init
  *         Initialize the HID interface
  * @param  pdev: device instance
  * @param  cfgidx: Configuration index
  * @retval status
  */
static uint8_t  USBD_HID_Init (USBD_HandleTypeDef  *pdev, uint8_t cfgidx)
{
  UNUSED(cfgidx);
  uint8_t hid_in_pkt_size = HID_AGENT_IN_SIZE;
  uint8_t hid_out_pkt_size = HID_AGENT_OUT_SIZE;
  USBD_HID_HandleTypeDef *hhid;

  hhid = (USBD_HID_HandleTypeDef *)USBD_malloc(sizeof(USBD_HID_HandleTypeDef));

  if (hhid == NULL)
  {
    pdev->pClassDataCmsit[pdev->classId] = NULL;
    return (uint8_t)USBD_EMEM;
  }

  pdev->pClassDataCmsit[pdev->classId] = (void *)hhid;
  pdev->pClassData = pdev->pClassDataCmsit[pdev->classId];

  pdev->ep_in[HIDInEpAdd & 0xFU].bInterval = 1;
  pdev->ep_out[HIDOutEpAdd & 0xFU].bInterval = 1;

  /* Open EP IN */
  USBD_LL_OpenEP(pdev, HIDInEpAdd, USBD_EP_TYPE_INTR, hid_in_pkt_size);
  pdev->ep_in[HIDInEpAdd & 0xFU].is_used = 1U;
  /* Open EP OUT */
  USBD_LL_OpenEP(pdev, HIDOutEpAdd, USBD_EP_TYPE_INTR, hid_out_pkt_size);
  pdev->ep_out[HIDOutEpAdd & 0xFU].is_used = 1U;

  //hhid->state = USBD_HID_IDLE;
  USBD_LL_PrepareReceive(pdev,HID_AGENT_OUT_EP,HID_Buffer,HID_AGENT_OUT_SIZE);
  return (uint8_t)USBD_OK;
}

/**
  * @brief  USBD_HID_DeInit
  *         DeInitialize the HID layer
  * @param  pdev: device instance
  * @param  cfgidx: Configuration index
  * @retval status
  */
static uint8_t  USBD_HID_DeInit (USBD_HandleTypeDef *pdev, uint8_t cfgidx)
{
  UNUSED(cfgidx);
  /* Close HID EPs */
  /* Close HID EPs */
  (void)USBD_LL_CloseEP(pdev, HIDInEpAdd);
  pdev->ep_in[HIDInEpAdd & 0xFU].is_used = 0U;
  pdev->ep_in[HIDInEpAdd & 0xFU].bInterval = 0U;

  (void)USBD_LL_CloseEP(pdev, HIDOutEpAdd);
  pdev->ep_out[HIDOutEpAdd & 0xFU].is_used = 0U;
  pdev->ep_out[HIDOutEpAdd & 0xFU].bInterval = 0U;

  /* Free allocated memory */
  if (pdev->pClassDataCmsit[pdev->classId] != NULL)
  {
    (void)USBD_free(pdev->pClassDataCmsit[pdev->classId]);
    pdev->pClassDataCmsit[pdev->classId] = NULL;
  }
  return (uint8_t)USBD_OK;
}

/**
  * @brief  USBD_HID_Setup
  *         Handle the HID specific requests
  * @param  pdev: instance
  * @param  req: usb requests
  * @retval status
  */
static uint8_t  USBD_HID_Setup (USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req)
{
  USBD_HID_HandleTypeDef *hhid = (USBD_HID_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];
  USBD_StatusTypeDef ret = USBD_OK;
  uint16_t len;
  uint8_t *pbuf;
  uint16_t status_info = 0U;

  if (hhid == NULL)
  {
    return (uint8_t)USBD_FAIL;
  }

  switch (req->bmRequest & USB_REQ_TYPE_MASK)
  {
    case USB_REQ_TYPE_CLASS :
      switch (req->bRequest)
      {
        case USBD_HID_REQ_SET_PROTOCOL:
          hhid->Protocol = (uint8_t)(req->wValue);
          break;

        case USBD_HID_REQ_GET_PROTOCOL:
          (void)USBD_CtlSendData(pdev, (uint8_t *)&hhid->Protocol, 1U);
          break;

        case USBD_HID_REQ_SET_IDLE:
          hhid->IdleState = (uint8_t)(req->wValue >> 8);
          break;

        case USBD_HID_REQ_GET_IDLE:
          (void)USBD_CtlSendData(pdev, (uint8_t *)&hhid->IdleState, 1U);
          break;

        default:
          USBD_CtlError(pdev, req);
          ret = USBD_FAIL;
          break;
      }
      break;
    case USB_REQ_TYPE_STANDARD:
      switch (req->bRequest)
      {
        case USB_REQ_GET_STATUS:
          if (pdev->dev_state == USBD_STATE_CONFIGURED)
          {
            (void)USBD_CtlSendData(pdev, (uint8_t *)&status_info, 2U);
          }
          else
          {
            USBD_CtlError(pdev, req);
            ret = USBD_FAIL;
          }
          break;

        case USB_REQ_GET_DESCRIPTOR:
          if ((req->wValue >> 8) == HID_REPORT_DESC)
          {
            len = MIN(sizeof(HID_AGENT_ReportDesc) , req->wLength);
            pbuf = (uint8_t*)HID_AGENT_ReportDesc; // wiiccReportDescriptor; //
          }
          else if ((req->wValue >> 8) == HID_DESCRIPTOR_TYPE)
          {
            pbuf = USBD_HID_Desc;
            len = MIN(USB_HID_DESC_SIZ, req->wLength);
          }
          else
          {
            USBD_CtlError(pdev, req);
            ret = USBD_FAIL;
            break;
          }
          (void)USBD_CtlSendData(pdev, pbuf, len);
          break;

        case USB_REQ_GET_INTERFACE :
          if (pdev->dev_state == USBD_STATE_CONFIGURED)
          {
            (void)USBD_CtlSendData(pdev, (uint8_t *)&hhid->AltSetting, 1U);
          }
          else
          {
            USBD_CtlError(pdev, req);
            ret = USBD_FAIL;
          }
          break;

        case USB_REQ_SET_INTERFACE:
          if (pdev->dev_state == USBD_STATE_CONFIGURED)
          {
            hhid->AltSetting = (uint8_t)(req->wValue);
          }
          else
          {
            USBD_CtlError(pdev, req);
            ret = USBD_FAIL;
          }
          break;

        case USB_REQ_CLEAR_FEATURE:
          break;

        default:
          USBD_CtlError(pdev, req);
          ret = USBD_FAIL;
          break;
      }
      break;

    default:
      USBD_CtlError(pdev, req);
      ret = USBD_FAIL;
      break;
  }

  return (uint8_t)ret;
}

/**
  * @brief  USBD_HID_SendReport
  *         Send HID Report
  * @param  pdev: device instance
  * @param  buff: pointer to report
  * @retval status
  */
uint8_t USBD_AGENT_SendReport(USBD_HandleTypeDef *pdev, uint8_t *report, uint16_t len)
{
  USBD_HID_HandleTypeDef *hhid = (USBD_HID_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];

  if (hhid == NULL)
    return (uint8_t)USBD_FAIL;

  if (pdev->dev_state == USBD_STATE_CONFIGURED)
  {
    if (hhid->state == USBD_HID_IDLE)
    {
      hhid->state = USBD_HID_BUSY;
      (void)USBD_LL_Transmit(pdev, HIDInEpAdd, report, len);
    } else {
      return (uint8_t)USBD_BUSY;
    }
  }

  return (uint8_t)USBD_OK;
}

bool USBD_AGENT_Is_Busy(USBD_HandleTypeDef *pdev)
{
  USBD_HID_HandleTypeDef *hhid = (USBD_HID_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];
  if (hhid == NULL)
    return true;

  if (hhid->state == USBD_HID_IDLE)
    return false;

  return true;
}

/**
  * @brief  USBD_HID_GetCfgFSDesc
  *         return FS configuration descriptor
  * @param  speed : current device speed
  * @param  length : pointer data length
  * @retval pointer to descriptor buffer
  */
static uint8_t *USBD_HID_GetFSCfgDesc(uint16_t *length)
{
  USBD_EpDescTypeDef *pEpDesc = USBD_GetEpDesc(USBD_HID_CfgDesc, HID_EPIN_ADDR);

  if (pEpDesc != NULL)
  {
    pEpDesc->bInterval = 1;
  }

  *length = (uint16_t)sizeof(USBD_HID_CfgDesc);
  return USBD_HID_CfgDesc;
}

/**
  * @brief  USBD_HID_GetCfgHSDesc
  *         return HS configuration descriptor
  * @param  speed : current device speed
  * @param  length : pointer data length
  * @retval pointer to descriptor buffer
  */
static uint8_t *USBD_HID_GetHSCfgDesc(uint16_t *length)
{
  USBD_EpDescTypeDef *pEpDesc = USBD_GetEpDesc(USBD_HID_CfgDesc, HID_EPIN_ADDR);

  if (pEpDesc != NULL)
  {
    pEpDesc->bInterval = 1;
  }

  *length = (uint16_t)sizeof(USBD_HID_CfgDesc);
  return USBD_HID_CfgDesc;
}

/**
  * @brief  USBD_HID_GetOtherSpeedCfgDesc
  *         return other speed configuration descriptor
  * @param  speed : current device speed
  * @param  length : pointer data length
  * @retval pointer to descriptor buffer
  */
static uint8_t *USBD_HID_GetOtherSpeedCfgDesc(uint16_t *length)
{
  USBD_EpDescTypeDef *pEpDesc = USBD_GetEpDesc(USBD_HID_CfgDesc, HID_EPIN_ADDR);

  if (pEpDesc != NULL)
  {
    pEpDesc->bInterval = 1;
  }

  *length = (uint16_t)sizeof(USBD_HID_CfgDesc);
  return USBD_HID_CfgDesc;
}

/**
  * @brief  USBD_HID_DataIn
  *         handle data IN Stage
  * @param  pdev: device instance
  * @param  epnum: endpoint index
  * @retval status
  */
static uint8_t  USBD_HID_DataIn (USBD_HandleTypeDef  *pdev, uint8_t epnum)
{
  UNUSED(epnum);
  /* Ensure that the FIFO is empty before a new transfer, this condition could
  be caused by  a new transfer before the end of the previous transfer */
  ((USBD_HID_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId])->state = USBD_HID_IDLE;

  extern void usbAgentTransmit();
  usbAgentTransmit();

  return (uint8_t)USBD_OK;
}

static volatile bool rx_stalled = false;

void USB_HID_Start_Receive(USBD_HandleTypeDef  *pdev) {
  if (!rx_stalled)
    return;
  rx_stalled = false;

  memset(HID_Buffer, 0, HID_AGENT_OUT_SIZE);
  USBD_LL_PrepareReceive(pdev, HID_AGENT_OUT_EP, HID_Buffer, HID_AGENT_OUT_SIZE);
}

static uint8_t  USBD_HID_DataOut (USBD_HandleTypeDef  *pdev, uint8_t epnum)
{
  rx_stalled = true;

  extern bool usbAgentReceive(const uint8_t*data);
  if (usbAgentReceive(HID_Buffer))
    USB_HID_Start_Receive(pdev);
  return USBD_OK;
}

/**
  * @brief  DeviceQualifierDescriptor
  *         return Device Qualifier descriptor
  * @param  length : pointer data length
  * @retval pointer to descriptor buffer
  */
static uint8_t *USBD_HID_GetDeviceQualifierDesc(uint16_t *length)
{
  *length = (uint16_t)sizeof(USBD_HID_DeviceQualifierDesc);

  return USBD_HID_DeviceQualifierDesc;
}
/**
  * @}
  */


/**
  * @}
  */


/**
  * @}
  */
