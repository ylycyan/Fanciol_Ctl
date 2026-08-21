#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "device_protocol.h"

static const uint8_t BLE_GOLDEN[] = {0xA5,0x02,0x01,0x34,0x12,0x10,0x00,0x03,0x00,0x01,0x02,0x03,0xC8,0xE3};
static const uint8_t MQTT_MANAGEMENT_GOLDEN[] = {0xC7,0x01,0x08,0x01,0x34,0x12,0x02,0x00,0x05,0x06,0xDA,0x3D};

int main(void)
{
    uint8_t out[256],copy[256],payload_big[64],assembled[256],fragment[20]; uint16_t len,assembled_len; uint8_t i;
    const uint8_t payload[]={1,2,3};
    device_frame_t b={DEVICE_FRAME_REQUEST,0x1234,DEVICE_OP_GET_STATE,DEVICE_STATUS_OK,sizeof(payload),payload};
    assert(DeviceProtocol_Crc16((const uint8_t*)"123456789",9)==0x29B1);
    assert(DeviceProtocol_Crc16(MQTT_MANAGEMENT_GOLDEN,sizeof(MQTT_MANAGEMENT_GOLDEN)-2)==0x3DDA);
    assert(DeviceProtocol_Encode(&b,out,sizeof(out),&len)==DEVICE_STATUS_OK);assert(len==sizeof(BLE_GOLDEN));assert(memcmp(out,BLE_GOLDEN,len)==0);
    memcpy(copy,out,len);copy[9]^=1;{device_frame_t decoded;assert(DeviceProtocol_Decode(copy,len,&decoded)==DEVICE_STATUS_VERIFY_FAILED);}
    for(i=0;i<sizeof(payload_big);i++) {
        payload_big[i]=(uint8_t)(i*17u);
    }
    b.payload=payload_big;b.payload_len=sizeof(payload_big);b.seq=77;
    assert(DeviceProtocol_Encode(&b,out,sizeof(out),&len)==DEVICE_STATUS_OK);
    {device_reassembler_t reassembler;uint8_t count=(uint8_t)((len+14u)/15u),index;DeviceProtocol_Reset(&reassembler);
        for(index=0;index<count;index++){uint16_t offset=(uint16_t)index*15u,chunk=(uint16_t)(len-offset);if(chunk>15u)chunk=15u;
            fragment[0]=(index==0?0x80u:0u)|(index==count-1?0x40u:0u);fragment[1]=index;fragment[2]=count;fragment[3]=77;fragment[4]=0;memcpy(fragment+5,out+offset,chunk);
            assert(DeviceProtocol_Reassemble(&reassembler,fragment,(uint16_t)(chunk+5u),assembled,sizeof(assembled),&assembled_len)==(index==count-1?DEVICE_STATUS_OK:DEVICE_STATUS_BUSY));}
        assert(assembled_len==len&&memcmp(assembled,out,len)==0);
        fragment[0]=0;fragment[1]=0;fragment[2]=1;assert(DeviceProtocol_Reassemble(&reassembler,fragment,5,assembled,sizeof(assembled),&assembled_len)==DEVICE_STATUS_INVALID_ARG);}
    puts("CH583 BLE device protocol golden vectors: PASS"); return 0;
}
