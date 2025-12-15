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
