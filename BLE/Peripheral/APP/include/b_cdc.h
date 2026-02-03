#ifndef __B_CDC_H__
#define __B_CDC_H__

void InitUSBDevPara(void);
void InitUSBDevice(void);
void SendUSBData(UINT8 *p_send_dat,UINT16 send_len);
void USB_Printf(const char *fmt, ...);

#endif