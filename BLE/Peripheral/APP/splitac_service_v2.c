#include "splitac_service_v2.h"
#include "protocol_v2.h"
#include "config_store_v2.h"
#include "board.h"
#include "CH58x_common.h"
#include "health_v2.h"
#include <string.h>

#define CAPABILITY_BITMAP 0x000006FFUL

typedef struct __attribute__((packed)) {
    uint16_t node_id;
    uint8_t channel;
    uint8_t link_role;
    uint16_t parent_id;
    uint8_t work_mode;
    uint8_t ir_action_type;
    uint16_t ir_type;
    uint8_t ir_index;
    uint32_t expected_revision;
} staged_config_t;

static staged_config_t staged_config;
static uint8_t staged_config_valid;
static DEV_RULE_T staged_rules[MAX_RULES];
static uint8_t staged_rules_valid;
static DEV_RULE_T rollback_rules[MAX_RULES];
static char device_identity[16];
static uint8_t device_identity_len;
static uint32_t identify_until;
static uint32_t maintenance_until;

static uint16_t get16(const uint8_t *p){return (uint16_t)p[0]|((uint16_t)p[1]<<8);}
static uint32_t get32(const uint8_t *p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static void put16(uint8_t *p,uint16_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void put32(uint8_t *p,uint32_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);}
static uint8_t before(uint32_t a,uint32_t b){return (int32_t)(a-b)<0;}
static char hex_digit(uint8_t value){value&=0x0Fu;return (char)(value<10u?'0'+value:'A'+value-10u);}

void SplitAcV2_Init(void)
{
    uint8_t uid[8] __attribute__((aligned(4)));
    staged_config_valid=0;staged_rules_valid=0;identify_until=0;maintenance_until=0;
    GET_UNIQUE_ID(uid);memcpy(device_identity,"CH583-",6);device_identity[6]=hex_digit(uid[3]>>4);device_identity[7]=hex_digit(uid[3]);device_identity[8]=hex_digit(uid[4]>>4);device_identity[9]=hex_digit(uid[4]);device_identity[10]=hex_digit(uid[5]>>4);device_identity[11]=hex_digit(uid[5]);device_identity_len=12;
}
void SplitAcV2_ResetSession(void){staged_config_valid=0;staged_rules_valid=0;identify_until=0;maintenance_until=0;}
uint8_t SplitAcV2_IdentifyActive(void){if(!identify_until)return 0;if(!before(CurTick,identify_until)){identify_until=0;return 0;}return 1;}
static uint8_t maintenance_active(void){if(!maintenance_until)return 0;if(!before(CurTick,maintenance_until)){maintenance_until=0;return 0;}return 1;}

static uint8_t parse_config(const uint8_t *p,uint16_t len,staged_config_t *cfg)
{
    if(len!=15u)return V2_STATUS_INVALID_ARG;
    memset(cfg,0,sizeof(*cfg));
    cfg->node_id=get16(p);cfg->channel=p[2];cfg->link_role=p[3];cfg->parent_id=get16(p+4);cfg->work_mode=p[6];
    cfg->ir_action_type=p[7];cfg->ir_type=get16(p+8);cfg->ir_index=p[10];cfg->expected_revision=get32(p+11);
    if(!cfg->node_id||cfg->channel>32u||cfg->link_role>LINK_CHILD||cfg->work_mode>1u||cfg->ir_action_type>ACT_TYPE_LEARN)return V2_STATUS_INVALID_ARG;
    if(cfg->link_role==LINK_CHILD&&(!cfg->parent_id||cfg->parent_id==cfg->node_id))return V2_STATUS_INVALID_ARG;
    if(cfg->link_role!=LINK_CHILD)cfg->parent_id=0;
    if(cfg->ir_index!=0xFFu&&cfg->ir_index>=IR_BRAND_COUNT)return V2_STATUS_INVALID_ARG;
    if(cfg->expected_revision!=ConfigV2_GetRevision())return V2_STATUS_CONFLICT;
    return V2_STATUS_OK;
}

static uint8_t parse_rules(const uint8_t *p,uint16_t len)
{
    uint8_t count,i;uint16_t off=1;uint32_t revision;
    if(len<5u)return V2_STATUS_INVALID_ARG;
    count=p[0];
    if(count>MAX_RULES||len!=(uint16_t)(1u+(uint16_t)count*14u+4u))return V2_STATUS_INVALID_ARG;
    revision=get32(p+len-4u);if(revision!=ConfigV2_GetRevision())return V2_STATUS_CONFLICT;
    memset(staged_rules,0,sizeof(staged_rules));
    for(i=0;i<count;i++,off=(uint16_t)(off+14u)){
        DEV_RULE_T *r=&staged_rules[i];uint16_t start=get16(p+off+3),end=get16(p+off+5),threshold=get16(p+off+7),hysteresis=get16(p+off+9),minimum=get16(p+off+11);
        uint8_t type=p[off+1],action=p[off+13];
        if(type<TRIG_TIME||type>TRIG_TEMP_BELOW||minimum<1u||minimum>1440u||action<1u||action>2u)return V2_STATUS_INVALID_ARG;
        if(type==TRIG_TIME&&(start>1439u||end>1439u||!(p[off+2]&0x7Fu)))return V2_STATUS_INVALID_ARG;
        if(type!=TRIG_TIME&&(threshold<160u||threshold>400u||hysteresis<5u||hysteresis>100u))return V2_STATUS_INVALID_ARG;
        r->ctrl.enable=p[off]?1:0;r->ctrl.trig_type=type;r->flags=p[off+2]&0x7Fu;r->trig_val=(type==TRIG_TIME)?start:threshold;
        r->trig_val2=(type==TRIG_TIME)?end:hysteresis;r->sched=0;r->act.ir.onOff=(action==1u)?1u:0u;
        r->act.ir.mode=Mode_Auto;r->act.ir.wind=Wind_Auto;r->act.ir.temSet=25;
        r->act.raw[2]=(uint8_t)minimum;r->act.raw[3]=(uint8_t)(minimum>>8);
    }
    staged_rules_valid=1;return V2_STATUS_OK;
}

static uint8_t execute_control(const uint8_t *p,uint16_t len)
{
    uint8_t control;uint16_t value;IR_CMD_t cmd;
    if(len!=3u)return V2_STATUS_INVALID_ARG;
    control=p[0];value=get16(p+1);
    switch(control){
    case V2_CONTROL_POWER: if(value>1u)return V2_STATUS_INVALID_ARG;cmd=value?IR_CMD_POWER_ON:IR_CMD_POWER_OFF;break;
    case V2_CONTROL_MODE: if(value>Mode_Heat)return V2_STATUS_INVALID_ARG;cmd=(IR_CMD_t)(IR_CMD_MODE_AUTO+value);break;
    case V2_CONTROL_TEMPERATURE: if(value<160u||value>310u||value%10u)return V2_STATUS_INVALID_ARG;cmd=(IR_CMD_t)(IR_CMD_TEMP_16+(value/10u)-16u);break;
    case V2_CONTROL_FAN: if(value>Wind_High)return V2_STATUS_INVALID_ARG;cmd=(IR_CMD_t)(IR_CMD_FAN_AUTO+value);break;
    case V2_CONTROL_WIND_DIRECTION: if(value>2u)return V2_STATUS_INVALID_ARG;cmd=(IR_CMD_t)(IR_CMD_WIND_UP+value);break;
    case V2_CONTROL_WIND_AUTO: if(value>1u)return V2_STATUS_INVALID_ARG;cmd=value?IR_CMD_WIND_AUTO_ON:IR_CMD_WIND_AUTO_OFF;break;
    case V2_CONTROL_SLEEP: if(value>1u)return V2_STATUS_INVALID_ARG;cmd=value?IR_CMD_SLEEP_ON:IR_CMD_SLEEP_OFF;break;
    case V2_CONTROL_AUX_HEAT: if(value>1u)return V2_STATUS_INVALID_ARG;cmd=value?IR_CMD_AUX_HEAT_ON:IR_CMD_AUX_HEAT_OFF;break;
    case V2_CONTROL_LIGHT: if(value>1u)return V2_STATUS_INVALID_ARG;cmd=value?IR_CMD_LIGHT_ON:IR_CMD_LIGHT_OFF;break;
    case V2_CONTROL_ENERGY: if(value>1u)return V2_STATUS_INVALID_ARG;cmd=value?IR_CMD_SLEEP_ENERGY_ON:IR_CMD_SLEEP_ENERGY_OFF;break;
    case V2_CONTROL_FAST_MODE: if(value>1u)return V2_STATUS_INVALID_ARG;cmd=value?IR_CMD_FAST_HEAT:IR_CMD_FAST_COOL;break;
    case V2_CONTROL_MUTE: if(value>1u)return V2_STATUS_INVALID_ARG;cmd=value?IR_CMD_MUTE_ON:IR_CMD_MUTE_OFF;break;
    case V2_CONTROL_TEMP_STEP: if(value>1u)return V2_STATUS_INVALID_ARG;cmd=value?IR_CMD_TEMP_UP:IR_CMD_TEMP_DOWN;break;
    default:return V2_STATUS_NOT_SUPPORTED;
    }
    if(!Ir_ExecuteVerified(cmd))return Dev.errorCode.bit.irMatch?V2_STATUS_NOT_SUPPORTED:V2_STATUS_TIMEOUT;
    if(control==V2_CONTROL_POWER)Dev.onOff=value?PowerOn:PowerOff;
    else if(control==V2_CONTROL_MODE)Dev.ctlMode=(Mode_t)value;
    else if(control==V2_CONTROL_TEMPERATURE)Dev.temSet=value/10u;
    else if(control==V2_CONTROL_FAN)Dev.wind=(Wind_t)value;
    else if(control==V2_CONTROL_TEMP_STEP){if(Dev.temSet<16u||Dev.temSet>31u)Dev.temSet=25u;if(value&&Dev.temSet<31u)Dev.temSet++;else if(!value&&Dev.temSet>16u)Dev.temSet--;}
    else if(control==V2_CONTROL_FAST_MODE){Dev.onOff=PowerOn;Dev.ctlMode=value?Mode_Heat:Mode_Cool;}
    return V2_STATUS_OK;
}

static uint8_t dispatch(const v2_ble_frame_t *req,uint8_t *payload,uint16_t *payload_len)
{
    uint8_t status=V2_STATUS_OK;uint32_t revision;staged_config_t cfg;
    *payload_len=0;
    switch(req->opcode){
    case V2_OP_GET_CAPABILITIES:
        put32(payload,CAPABILITY_BITMAP);payload[4]=V2_PROTOCOL_VERSION;payload[5]=2;payload[6]=6;payload[7]=1;*payload_len=8;break;
    case V2_OP_GET_DEVICE_INFO:{
        payload[0]=1;payload[1]=device_identity_len;memcpy(payload+2,device_identity,device_identity_len);*payload_len=(uint16_t)(2u+device_identity_len);break;}
    case V2_OP_AUTH_BEGIN:
    case V2_OP_AUTH_PROVE:
        status=V2_STATUS_NOT_SUPPORTED;break;
    case V2_OP_IDENTIFY:{
        uint8_t mode=req->payload_len?req->payload[0]:1u;if(req->payload_len>1u||(mode!=1u&&mode!=2u)){status=V2_STATUS_INVALID_ARG;break;}
        identify_until=CurTick+10000u;if(mode==2u)maintenance_until=CurTick+1800000u;payload[0]=mode;*payload_len=1;break;}
    case V2_OP_GET_STATE:{
        int16_t room=(int16_t)(Dev.tem*10.0f);
        payload[0]=(Dev.onOff==PowerOn)?1u:0u;payload[1]=(uint8_t)Dev.ctlMode;put16(payload+2,(uint16_t)(Dev.temSet*10u));put16(payload+4,(uint16_t)room);
        payload[6]=(uint8_t)Dev.wind;put16(payload+7,Dev.errorCode.u16Val);payload[9]=(Dev.loraStatus>=Status_Connected)?1u:0u;
        payload[10]=(Dev.mode==0u||Dev.loraStatus<Status_Connected)?1u:0u;put32(payload+11,Dev.meter.run_minutes);put32(payload+15,ConfigV2_GetRevision());*payload_len=19;break;}
    case V2_OP_GET_CONFIG:
        put16(payload,Dev.nodeId);payload[2]=(uint8_t)Dev.channel;payload[3]=Dev.linkRole;put16(payload+4,Dev.parentRelayId);payload[6]=Dev.mode;
        payload[7]=(uint8_t)Dev.irActType;put16(payload+8,Dev.irType);payload[10]=Dev.irIdx;put32(payload+11,ConfigV2_GetRevision());*payload_len=15;break;
    case V2_OP_VALIDATE_CONFIG:
        status=parse_config(req->payload,req->payload_len,&staged_config);if(status==V2_STATUS_OK)staged_config_valid=1;break;
    case V2_OP_COMMIT_CONFIG:
        status=parse_config(req->payload,req->payload_len,&cfg);if(status!=V2_STATUS_OK)break;
        if(staged_config_valid&&memcmp(&cfg,&staged_config,sizeof(cfg))!=0){status=V2_STATUS_CONFLICT;break;}
        staged_config_t previous={Dev.nodeId,(uint8_t)Dev.channel,Dev.linkRole,Dev.parentRelayId,Dev.mode,(uint8_t)Dev.irActType,Dev.irType,Dev.irIdx,ConfigV2_GetRevision()};
        memcpy(rollback_rules,Dev.rules,sizeof(rollback_rules));
        Dev.nodeId=cfg.node_id;Dev.channel=cfg.channel;Dev.linkRole=cfg.link_role;Dev.parentRelayId=cfg.parent_id;Dev.mode=cfg.work_mode;
        Dev.irActType=(ActType_t)cfg.ir_action_type;Dev.irType=cfg.ir_type;Dev.irIdx=cfg.ir_index;if(staged_rules_valid)memcpy(Dev.rules,staged_rules,sizeof(staged_rules));
        status=ConfigV2_Commit(cfg.expected_revision);if(status==V2_STATUS_OK){revision=ConfigV2_GetRevision();put32(payload,revision);*payload_len=4;staged_config_valid=0;staged_rules_valid=0;Dev.loraStatus=Status_Logining;Timer_Lora=LORA_SEC_TO_TICKS(300);}
        else {Dev.nodeId=previous.node_id;Dev.channel=previous.channel;Dev.linkRole=previous.link_role;Dev.parentRelayId=previous.parent_id;Dev.mode=previous.work_mode;Dev.irActType=(ActType_t)previous.ir_action_type;Dev.irType=previous.ir_type;Dev.irIdx=previous.ir_index;memcpy(Dev.rules,rollback_rules,sizeof(rollback_rules));}
        break;
    case V2_OP_EXEC_CONTROL:
        status=execute_control(req->payload,req->payload_len);break;
    case V2_OP_GET_RULES:{
        uint8_t i,count=0;uint16_t off=1;
        for(i=0;i<MAX_RULES;i++)if(Dev.rules[i].ctrl.enable)count++;
        payload[0]=count;
        for(i=0;i<MAX_RULES;i++){DEV_RULE_T *r=&Dev.rules[i];if(!r->ctrl.enable)continue;payload[off]=1;payload[off+1]=r->ctrl.trig_type;payload[off+2]=r->flags&0x7Fu;
            put16(payload+off+3,(r->ctrl.trig_type==TRIG_TIME)?r->trig_val:0u);put16(payload+off+5,(r->ctrl.trig_type==TRIG_TIME)?r->trig_val2:0u);
            put16(payload+off+7,(r->ctrl.trig_type==TRIG_TIME)?0u:r->trig_val);put16(payload+off+9,(r->ctrl.trig_type==TRIG_TIME)?5u:r->trig_val2);
            put16(payload+off+11,(uint16_t)r->act.raw[2]|((uint16_t)r->act.raw[3]<<8));payload[off+13]=r->act.ir.onOff?1u:2u;off=(uint16_t)(off+14u);}
        put32(payload+off,ConfigV2_GetRevision());*payload_len=(uint16_t)(off+4u);break;}
    case V2_OP_SET_RULES:
        status=parse_rules(req->payload,req->payload_len);break;
    case V2_OP_IR_CONFIG:
        if(req->payload_len<1u){status=V2_STATUS_INVALID_ARG;break;}
        if(req->payload[0]==1u&&req->payload_len==4u){uint8_t idx=req->payload[1];uint16_t type=get16(req->payload+2);if(idx>=IR_BRAND_COUNT||!type||type==0xFFFFu){status=V2_STATUS_INVALID_ARG;break;}Dev.irIdx=idx;Dev.irType=type;Dev.irActType=ACT_TYPE_IR;Dev.errorCode.bit.irMatch=0;}
        else if(req->payload[0]==2u&&req->payload_len==1u)status=Ir_StartMatch()?V2_STATUS_OK:V2_STATUS_BUSY;
        else if(req->payload[0]==3u&&req->payload_len==2u)status=Ir_StartLearning(req->payload[1])?V2_STATUS_OK:V2_STATUS_BUSY;
        else if(req->payload[0]==4u&&req->payload_len==2u)status=Ir_SendLearnedVerified(req->payload[1])?V2_STATUS_OK:V2_STATUS_VERIFY_FAILED;
        else if(req->payload[0]==5u&&req->payload_len==1u){uint16_t learned=Ir_GetLearnedMask();payload[0]=(uint8_t)IrBuf.type;payload[1]=IrBuf.isFinish;payload[2]=Dev.errorCode.bit.irMatch?1u:0u;payload[3]=Dev.errorCode.bit.irLearn?1u:0u;put16(payload+4,Dev.irType);payload[6]=Dev.irIdx;payload[7]=Dev.learnNum;put16(payload+8,learned);payload[10]=IrLearnChannel;*payload_len=11;}
        else if(req->payload[0]==6u&&req->payload_len==1u)status=Ir_CancelOperation()?V2_STATUS_OK:V2_STATUS_IO_ERROR;
        else if(req->payload[0]==7u&&req->payload_len==2u){if(req->payload[1]>=MAX_IR_LEARNNUM)status=V2_STATUS_INVALID_ARG;else status=Ir_ResetLearned(req->payload[1])?V2_STATUS_OK:V2_STATUS_BUSY;}
        else if(req->payload[0]==8u&&req->payload_len==1u)status=Ir_ResetAllLearned()?V2_STATUS_OK:V2_STATUS_BUSY;
        else status=V2_STATUS_INVALID_ARG;
        break;
    case V2_OP_IR_ACTION:
        if(req->payload_len!=1u||req->payload[0]!=1u){status=V2_STATUS_NOT_SUPPORTED;break;}status=Ir_ExecuteVerified(IR_CMD_POWER_OFF)?V2_STATUS_OK:V2_STATUS_VERIFY_FAILED;break;
    case V2_OP_GET_DIAGNOSTICS:
        payload[0]=Dev.errorCode.bit.flash?1u:0u;payload[1]=(uint8_t)SYS_GetLastResetSta();
        put16(payload+2,Dev.errorCode.u16Val);payload[4]=0;payload[5]=Dev.loraStatus;payload[6]=HealthV2_ConsecutiveResets();payload[7]=HealthV2_LastResetReason();
        put32(payload+8,LocalTimestamp);put32(payload+12,Dev.lastReportTime);put16(payload+16,Dev.loadPower);*payload_len=18;break;
    case V2_OP_GET_LORA_PARAMS:
        if(!maintenance_active()){status=V2_STATUS_UNAUTHORIZED;break;}
        payload[0]=Dev.loraRegisterSf;payload[1]=Dev.loraRegisterBw;
        payload[2]=Dev.loraListenSf;payload[3]=Dev.loraListenBw;*payload_len=4;break;
    case V2_OP_SET_LORA_PARAMS:
        if(!maintenance_active()){status=V2_STATUS_UNAUTHORIZED;break;}
        if(req->payload_len!=4u){status=V2_STATUS_INVALID_ARG;break;}
        status=LoraParamsV2_Save(req->payload[0],req->payload[1],req->payload[2],req->payload[3]);
        if(status==V2_STATUS_OK){Dev.loraStatus=Status_Logining;Timer_Lora=LORA_SEC_TO_TICKS(300);}
        break;
    case V2_OP_FACTORY_RESET:
        if(!maintenance_active()){status=V2_STATUS_UNAUTHORIZED;break;}EEPROM_ERASE(V2_CONFIG_SLOT_A,EEPROM_BLOCK_SIZE);EEPROM_ERASE(V2_CONFIG_SLOT_B,EEPROM_BLOCK_SIZE);
        EEPROM_ERASE(V2_RESERVED_PAGE,EEPROM_BLOCK_SIZE);EEPROM_ERASE(V2_RUNTIME_PAGE,EEPROM_BLOCK_SIZE);EEPROM_ERASE(V2_IR_PAGE,EEPROM_BLOCK_SIZE);ConfigV2_FactoryDefaults();status=ConfigV2_Commit(0);break;
    case V2_OP_OTA_BEGIN:
    case V2_OP_OTA_CHUNK:
    case V2_OP_OTA_FINISH:
        status=V2_STATUS_NOT_SUPPORTED;break;
    default:status=V2_STATUS_NOT_SUPPORTED;break;
    }
    return status;
}

uint8_t SplitAcV2_HandleFrame(const uint8_t *request,uint16_t request_len,uint8_t *response,uint16_t capacity,uint16_t *response_len)
{
    v2_ble_frame_t req,rsp;uint8_t payload[V2_MAX_PAYLOAD];uint16_t payload_len=0;uint8_t status;
    status=V2_BleDecode(request,request_len,&req);if(status!=V2_STATUS_OK||req.type!=V2_FRAME_REQUEST)return status;
    status=dispatch(&req,payload,&payload_len);rsp.type=V2_FRAME_RESPONSE;rsp.seq=req.seq;rsp.opcode=req.opcode;rsp.status=status;rsp.payload_len=payload_len;rsp.payload=payload;
    return V2_BleEncode(&rsp,response,capacity,response_len);
}
