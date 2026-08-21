#include "device_service.h"
#include "device_protocol.h"
#include "config_store.h"
#include "board.h"
#include "CH58x_common.h"
#include "health.h"
#include "hlw8110.h"
#include "timer.h"
#include "ml307r.h"
#include "peripheral.h"
#include "lora.h"
#include "ota_update.h"
#include <stddef.h>
#include <string.h>

#define CAPABILITY_BASE             0x00000AF0UL
#define CAPABILITY_PROFILE_CONTROLS 0x0000040FUL
#define CAPABILITY_POWER            0x00000001UL
#define CAPABILITY_MODE             0x00000002UL
#define CAPABILITY_CELLULAR         0x00001000UL

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

static uint32_t current_capability_bitmap(void)
{
    uint32_t bitmap=CAPABILITY_BASE|CAPABILITY_CELLULAR;
    uint16_t learned;

    if(Dev.irActType==ACT_TYPE_IR) {
        if(Ir_ConfiguredCommandSupported(IR_CMD_POWER_OFF)) bitmap|=CAPABILITY_PROFILE_CONTROLS;
        return bitmap;
    }

    learned=Ir_GetLearnedMask();
    /* 学习模式只有开、关都具备时，才声明可安全执行状态切换。 */
    if((learned&0x0003u)==0x0003u) bitmap|=CAPABILITY_POWER;
    /* 制冷/制热/除湿/送风任一存在时，界面按通道精确显示。 */
    if(learned&0x003Cu) bitmap|=CAPABILITY_MODE;
    return bitmap;
}

void DeviceService_Init(void)
{
    uint8_t uid[8] __attribute__((aligned(4)));
    uint16_t short_id;
    staged_config_valid=0;identify_until=0;maintenance_until=0;
    GET_UNIQUE_ID(uid);short_id=DeviceProtocol_Crc16(uid,6u);memcpy(device_identity,"SplitAC-",8);device_identity[8]=hex_digit((uint8_t)(short_id>>12));device_identity[9]=hex_digit((uint8_t)(short_id>>8));device_identity[10]=hex_digit((uint8_t)(short_id>>4));device_identity[11]=hex_digit((uint8_t)short_id);device_identity_len=12;
}
void DeviceService_ResetSession(void){staged_config_valid=0;identify_until=0;maintenance_until=0;}
uint8_t DeviceService_IdentifyActive(void){if(!identify_until)return 0;if(!before(CurTick,identify_until)){identify_until=0;return 0;}return 1;}
uint8_t DeviceService_MaintenanceActive(void){if(!maintenance_until)return 0;if(!before(CurTick,maintenance_until)){maintenance_until=0;return 0;}return 1;}

static uint8_t parse_config(const uint8_t *p,uint16_t len,staged_config_t *cfg)
{
    if(len!=15u)return DEVICE_STATUS_INVALID_ARG;
    memset(cfg,0,sizeof(*cfg));
    cfg->node_id=get16(p);cfg->channel=p[2];cfg->link_role=p[3];cfg->parent_id=get16(p+4);cfg->work_mode=p[6];
    cfg->ir_action_type=p[7];cfg->ir_type=get16(p+8);cfg->ir_index=p[10];cfg->expected_revision=get32(p+11);
    if(!cfg->node_id||cfg->channel>32u||cfg->link_role>LINK_CHILD||cfg->work_mode>1u||cfg->ir_action_type>ACT_TYPE_LEARN)return DEVICE_STATUS_INVALID_ARG;
    if(cfg->link_role==LINK_CHILD&&(!cfg->parent_id||cfg->parent_id==cfg->node_id))return DEVICE_STATUS_INVALID_ARG;
    if(cfg->link_role!=LINK_CHILD)cfg->parent_id=0;
    if(cfg->ir_index!=0xFFu&&cfg->ir_index>=IR_BRAND_COUNT)return DEVICE_STATUS_INVALID_ARG;
    if(cfg->expected_revision!=Config_GetRevision())return DEVICE_STATUS_CONFLICT;
    return DEVICE_STATUS_OK;
}

static uint8_t parse_rules(const uint8_t *p,uint16_t len,DEV_RULE_T *rules)
{
    uint8_t count,i;uint16_t off=1;uint32_t revision;
    if(len<5u)return DEVICE_STATUS_INVALID_ARG;
    count=p[0];
    if(count>MAX_RULES||len!=(uint16_t)(1u+(uint16_t)count*14u+4u))return DEVICE_STATUS_INVALID_ARG;
    revision=get32(p+len-4u);if(revision!=Config_GetRevision())return DEVICE_STATUS_CONFLICT;
    memset(rules,0,sizeof(Dev.rules));
    for(i=0;i<count;i++,off=(uint16_t)(off+14u)){
        DEV_RULE_T *r=&rules[i];uint16_t start=get16(p+off+3),end=get16(p+off+5),threshold=get16(p+off+7),hysteresis=get16(p+off+9),minimum=get16(p+off+11);
        uint8_t type=p[off+1],action=p[off+13];
        if(type<TRIG_TIME||type>TRIG_TEMP_BELOW||minimum<1u||minimum>1440u||action<1u||action>2u)return DEVICE_STATUS_INVALID_ARG;
        if(type==TRIG_TIME&&(start>1439u||end>1439u||!(p[off+2]&0x7Fu)))return DEVICE_STATUS_INVALID_ARG;
        if(type!=TRIG_TIME&&(threshold<160u||threshold>400u||hysteresis<5u||hysteresis>100u))return DEVICE_STATUS_INVALID_ARG;
        r->ctrl.enable=p[off]?1:0;r->ctrl.trig_type=type;r->flags=p[off+2]&0x7Fu;r->trig_val=(type==TRIG_TIME)?start:threshold;
        r->trig_val2=(type==TRIG_TIME)?end:hysteresis;r->sched=0;
        r->act.ir.onOff=(action==1u)?1u:0u;r->act.ir.mode=Mode_Auto;r->act.ir.wind=Wind_Auto;r->act.ir.temSet=25;
        r->act.raw[2]=(uint8_t)minimum;r->act.raw[3]=(uint8_t)(minimum>>8);
    }
    return DEVICE_STATUS_OK;
}

static void update_power_state(uint8_t on)
{
    uint8_t previous=(Dev.onOff==PowerOn)?1u:0u;
    Dev.onOff=on?PowerOn:PowerOff;
    if(previous!=on) {
        if(Dev.meter.onoff_count!=0xFFFFu)Dev.meter.onoff_count++;
        if(on)Dev.lastOnTime=LocalTimestamp;
        Dev.lastPowerChange=LocalTimestamp;
    }
}

static void update_learned_state(uint8_t channel)
{
    switch(channel) {
    case 0:update_power_state(1);break;
    case 1:update_power_state(0);break;
    case 2:Dev.ctlMode=Mode_Cool;break;
    case 3:Dev.ctlMode=Mode_Heat;break;
    case 4:Dev.ctlMode=Mode_Dry;break;
    case 5:Dev.ctlMode=Mode_Fan;break;
    case 6:
        if(Dev.temSet<16u||Dev.temSet>31u)Dev.temSet=25u;
        if(Dev.temSet<31u)Dev.temSet++;
        break;
    case 7:
        if(Dev.temSet<16u||Dev.temSet>31u)Dev.temSet=25u;
        if(Dev.temSet>16u)Dev.temSet--;
        break;
    default:
        /* 风速切换和自定义键没有可推断的绝对状态。 */
        break;
    }
}

uint8_t SplitAcControl_Execute(uint8_t control,uint16_t value)
{
    IR_CMD_t cmd;

    if(control==CONTROL_FIELD_LEARNED_CHANNEL) {
        if(value>=MAX_IR_LEARNNUM)return DEVICE_STATUS_INVALID_ARG;
        if(Dev.irActType!=ACT_TYPE_LEARN||!Dev.learnCode[value].enable)return DEVICE_STATUS_NOT_SUPPORTED;
        if(!Ir_SendLearnedVerified((uint8_t)value))return DEVICE_STATUS_BUSY;
        update_learned_state((uint8_t)value);
        SaveDevInfo(50u);
        Ml307_RequestReport();
        return DEVICE_STATUS_OK;
    }

    switch(control){
    case CONTROL_FIELD_POWER: if(value>1u)return DEVICE_STATUS_INVALID_ARG;cmd=value?IR_CMD_POWER_ON:IR_CMD_POWER_OFF;break;
    case CONTROL_FIELD_MODE: if(value>Mode_Heat)return DEVICE_STATUS_INVALID_ARG;cmd=(IR_CMD_t)(IR_CMD_MODE_AUTO+value);break;
    case CONTROL_FIELD_TEMPERATURE: if(value<160u||value>310u||value%10u)return DEVICE_STATUS_INVALID_ARG;cmd=(IR_CMD_t)(IR_CMD_TEMP_16+(value/10u)-16u);break;
    case CONTROL_FIELD_FAN: if(value>Wind_High)return DEVICE_STATUS_INVALID_ARG;cmd=(IR_CMD_t)(IR_CMD_FAN_AUTO+value);break;
    case CONTROL_FIELD_WIND_DIRECTION: if(value>2u)return DEVICE_STATUS_INVALID_ARG;cmd=(IR_CMD_t)(IR_CMD_WIND_UP+value);break;
    case CONTROL_FIELD_WIND_AUTO: if(value>1u)return DEVICE_STATUS_INVALID_ARG;cmd=value?IR_CMD_WIND_AUTO_ON:IR_CMD_WIND_AUTO_OFF;break;
    case CONTROL_FIELD_SLEEP: if(value>1u)return DEVICE_STATUS_INVALID_ARG;cmd=value?IR_CMD_SLEEP_ON:IR_CMD_SLEEP_OFF;break;
    case CONTROL_FIELD_AUX_HEAT: if(value>1u)return DEVICE_STATUS_INVALID_ARG;cmd=value?IR_CMD_AUX_HEAT_ON:IR_CMD_AUX_HEAT_OFF;break;
    case CONTROL_FIELD_LIGHT: if(value>1u)return DEVICE_STATUS_INVALID_ARG;cmd=value?IR_CMD_LIGHT_ON:IR_CMD_LIGHT_OFF;break;
    case CONTROL_FIELD_ENERGY: if(value>1u)return DEVICE_STATUS_INVALID_ARG;cmd=value?IR_CMD_SLEEP_ENERGY_ON:IR_CMD_SLEEP_ENERGY_OFF;break;
    case CONTROL_FIELD_FAST_MODE: if(value>1u)return DEVICE_STATUS_INVALID_ARG;cmd=value?IR_CMD_FAST_HEAT:IR_CMD_FAST_COOL;break;
    case CONTROL_FIELD_MUTE: if(value>1u)return DEVICE_STATUS_INVALID_ARG;cmd=value?IR_CMD_MUTE_ON:IR_CMD_MUTE_OFF;break;
    case CONTROL_FIELD_TEMP_STEP: if(value>1u)return DEVICE_STATUS_INVALID_ARG;cmd=value?IR_CMD_TEMP_UP:IR_CMD_TEMP_DOWN;break;
    default:return DEVICE_STATUS_NOT_SUPPORTED;
    }
    if(!Ir_ConfiguredCommandSupported(cmd))return DEVICE_STATUS_NOT_SUPPORTED;
    if(!Ir_ExecuteConfiguredVerified(cmd))return DEVICE_STATUS_BUSY;
    if(control==CONTROL_FIELD_POWER)update_power_state(value?1u:0u);
    else if(control==CONTROL_FIELD_MODE)Dev.ctlMode=(Mode_t)value;
    else if(control==CONTROL_FIELD_TEMPERATURE)Dev.temSet=value/10u;
    else if(control==CONTROL_FIELD_FAN)Dev.wind=(Wind_t)value;
    else if(control==CONTROL_FIELD_TEMP_STEP){if(Dev.temSet<16u||Dev.temSet>31u)Dev.temSet=25u;if(value&&Dev.temSet<31u)Dev.temSet++;else if(!value&&Dev.temSet>16u)Dev.temSet--;}
    else if(control==CONTROL_FIELD_FAST_MODE){update_power_state(1);Dev.ctlMode=value?Mode_Heat:Mode_Cool;}
    SaveDevInfo(50u);
    Ml307_RequestReport();
    return DEVICE_STATUS_OK;
}

static uint8_t execute_control(const uint8_t *p,uint16_t len)
{
    if(len!=3u)return DEVICE_STATUS_INVALID_ARG;
    return SplitAcControl_Execute(p[0],get16(p+1));
}

static uint8_t transport_online(void)
{
    return (Connectivity_LoraEnabled() && Dev.loraStatus >= Status_Connected) ||
           Ml307_IsOnline();
}

/* Keep the large response scratch buffer out of this function's frame. */
static __attribute__((noinline)) uint8_t dispatch(const device_frame_t *req,uint8_t *payload,uint16_t *payload_len)
{
    uint8_t status=DEVICE_STATUS_OK;uint32_t revision;staged_config_t cfg;
    *payload_len=0;
    switch(req->opcode){
    case DEVICE_OP_GET_CAPABILITIES:
        put32(payload,current_capability_bitmap());payload[4]=DEVICE_PROTOCOL_VERSION;
        payload[5]=(uint8_t)(FIRMWARE_BUILD_VERSION>>16);
        payload[6]=(uint8_t)(FIRMWARE_BUILD_VERSION>>8);
        payload[7]=(uint8_t)FIRMWARE_BUILD_VERSION;
        payload[8]=(uint8_t)Dev.irActType;put16(payload+9,Ir_GetLearnedMask());*payload_len=11;break;
    case DEVICE_OP_GET_DEVICE_INFO:{
        const char *name=DeviceProfile_GetName();uint8_t name_len=(uint8_t)strlen(name);
        payload[0]=2;payload[1]=device_identity_len;memcpy(payload+2,device_identity,device_identity_len);
        payload[2u+device_identity_len]=name_len;memcpy(payload+3u+device_identity_len,name,name_len);
        *payload_len=(uint16_t)(3u+device_identity_len+name_len);break;}
    case DEVICE_OP_IDENTIFY:{
        uint8_t mode=req->payload_len?req->payload[0]:1u;if(req->payload_len>1u||(mode!=1u&&mode!=2u)){status=DEVICE_STATUS_INVALID_ARG;break;}
        identify_until=CurTick+10000u;if(mode==2u)maintenance_until=CurTick+1800000u;payload[0]=mode;*payload_len=1;break;}
    case DEVICE_OP_GET_STATE:{
        const HLW8110_Status_t *meter=HLW8110_GetStatus();
        int16_t room=Dev.roomTempX10;
        payload[0]=(Dev.onOff==PowerOn)?1u:0u;payload[1]=(uint8_t)Dev.ctlMode;put16(payload+2,(uint16_t)(Dev.temSet*10u));put16(payload+4,(uint16_t)room);
        payload[6]=(uint8_t)Dev.wind;put16(payload+7,Dev.errorCode.u16Val);payload[9]=(uint8_t)Dev.loraStatus;
        payload[10]=(Dev.mode==0u||!transport_online())?1u:0u;put32(payload+11,Dev.meter.run_minutes);put32(payload+15,Config_GetRevision());
        payload[19]=meter->valid;put16(payload+20,meter->voltage_dv);put16(payload+22,meter->current_ma);put16(payload+24,meter->power_w_x10);
        put32(payload+26,Dev.meter.energy_wh);put16(payload+30,meter->communication_errors);*payload_len=32;break;}
    case DEVICE_OP_GET_CONFIG:
        put16(payload,Dev.nodeId);payload[2]=(uint8_t)Dev.channel;payload[3]=Dev.linkRole;put16(payload+4,Dev.parentRelayId);payload[6]=Dev.mode;
        payload[7]=(uint8_t)Dev.irActType;put16(payload+8,Dev.irType);payload[10]=Dev.irIdx;put32(payload+11,Config_GetRevision());*payload_len=15;break;
    case DEVICE_OP_VALIDATE_CONFIG:
        status=parse_config(req->payload,req->payload_len,&staged_config);if(status==DEVICE_STATUS_OK)staged_config_valid=1;break;
    case DEVICE_OP_COMMIT_CONFIG:
    {
        DEV_RULE_T rollback_rules[MAX_RULES];
        status=parse_config(req->payload,req->payload_len,&cfg);if(status!=DEVICE_STATUS_OK)break;
        if(staged_config_valid&&memcmp(&cfg,&staged_config,sizeof(cfg))!=0){status=DEVICE_STATUS_CONFLICT;break;}
        if((cfg.ir_action_type!=(uint8_t)Dev.irActType||cfg.ir_type!=Dev.irType||cfg.ir_index!=Dev.irIdx)&&
           !Ir_PrepareConfigurationChange()){status=DEVICE_STATUS_BUSY;break;}
        staged_config_t previous={Dev.nodeId,(uint8_t)Dev.channel,Dev.linkRole,Dev.parentRelayId,Dev.mode,(uint8_t)Dev.irActType,Dev.irType,Dev.irIdx,Config_GetRevision()};
        memcpy(rollback_rules,Dev.rules,sizeof(rollback_rules));
        Dev.nodeId=cfg.node_id;Dev.channel=cfg.channel;Dev.linkRole=cfg.link_role;Dev.parentRelayId=cfg.parent_id;Dev.mode=cfg.work_mode;
        Dev.irActType=(ActType_t)cfg.ir_action_type;Dev.irType=cfg.ir_type;Dev.irIdx=cfg.ir_index;
        Dev.errorCode.bit.irMatch=(Dev.irActType==ACT_TYPE_IR&&
            (Dev.irIdx>=IR_BRAND_COUNT||!Dev.irType||Dev.irType==0xFFFFu))?1u:0u;
        status=Config_Commit(cfg.expected_revision);if(status==DEVICE_STATUS_OK){revision=Config_GetRevision();put32(payload,revision);*payload_len=4;staged_config_valid=0;Dev.loraStatus=Status_Logining;Timer_Lora=LORA_SEC_TO_TICKS(300);}
        else {Dev.nodeId=previous.node_id;Dev.channel=previous.channel;Dev.linkRole=previous.link_role;Dev.parentRelayId=previous.parent_id;Dev.mode=previous.work_mode;Dev.irActType=(ActType_t)previous.ir_action_type;Dev.irType=previous.ir_type;Dev.irIdx=previous.ir_index;
            Dev.errorCode.bit.irMatch=(Dev.irActType==ACT_TYPE_IR&&
                (Dev.irIdx>=IR_BRAND_COUNT||!Dev.irType||Dev.irType==0xFFFFu))?1u:0u;
            memcpy(Dev.rules,rollback_rules,sizeof(rollback_rules));}
        break;
    }
    case DEVICE_OP_EXEC_CONTROL:
        status=execute_control(req->payload,req->payload_len);break;
    case DEVICE_OP_GET_RULES:{
        uint8_t i,count=0;uint16_t off=1;
        for(i=0;i<MAX_RULES;i++)if(Dev.rules[i].ctrl.enable)count++;
        payload[0]=count;
        for(i=0;i<MAX_RULES;i++){DEV_RULE_T *r=&Dev.rules[i];if(!r->ctrl.enable)continue;payload[off]=1;payload[off+1]=r->ctrl.trig_type;payload[off+2]=r->flags&0x7Fu;
            put16(payload+off+3,(r->ctrl.trig_type==TRIG_TIME)?r->trig_val:0u);put16(payload+off+5,(r->ctrl.trig_type==TRIG_TIME)?r->trig_val2:0u);
            put16(payload+off+7,(r->ctrl.trig_type==TRIG_TIME)?0u:r->trig_val);put16(payload+off+9,(r->ctrl.trig_type==TRIG_TIME)?5u:r->trig_val2);
            put16(payload+off+11,(uint16_t)r->act.raw[2]|((uint16_t)r->act.raw[3]<<8));payload[off+13]=r->act.ir.onOff?1u:2u;off=(uint16_t)(off+14u);}
        put32(payload+off,Config_GetRevision());*payload_len=(uint16_t)(off+4u);break;}
    case DEVICE_OP_SET_RULES:
    {
        DEV_RULE_T previous_rules[MAX_RULES];
        uint32_t expected=req->payload_len>=4u?get32(req->payload+req->payload_len-4u):0u;
        memcpy(previous_rules,Dev.rules,sizeof(previous_rules));
        status=parse_rules(req->payload,req->payload_len,Dev.rules);
        if(status==DEVICE_STATUS_OK)status=Config_Commit(expected);
        if(status!=DEVICE_STATUS_OK)memcpy(Dev.rules,previous_rules,sizeof(previous_rules));
        else{put32(payload,Config_GetRevision());*payload_len=4u;}
        break;
    }
    case DEVICE_OP_IR_CONFIG:
        if(req->payload_len<1u){status=DEVICE_STATUS_INVALID_ARG;break;}
        if(req->payload[0]==1u&&req->payload_len==4u){uint8_t idx=req->payload[1];uint16_t type=get16(req->payload+2);if(idx>=IR_BRAND_COUNT||!type||type==0xFFFFu){status=DEVICE_STATUS_INVALID_ARG;break;}if(!Ir_PrepareConfigurationChange()){status=DEVICE_STATUS_BUSY;break;}Dev.irIdx=idx;Dev.irType=type;Dev.irActType=ACT_TYPE_IR;Dev.errorCode.bit.irMatch=0;}
        else if(req->payload[0]==2u&&req->payload_len==1u)status=Ir_StartMatch()?DEVICE_STATUS_OK:DEVICE_STATUS_BUSY;
        else if(req->payload[0]==3u&&req->payload_len==2u)status=Ir_StartLearning(req->payload[1])?DEVICE_STATUS_OK:DEVICE_STATUS_BUSY;
        else if(req->payload[0]==4u&&req->payload_len==2u){
            uint8_t ch=req->payload[1];
            if(ch>=MAX_IR_LEARNNUM)status=DEVICE_STATUS_INVALID_ARG;
            else if(!Dev.learnCode[ch].enable)status=DEVICE_STATUS_NOT_SUPPORTED;
            else status=Ir_SendLearnedVerified(ch)?DEVICE_STATUS_OK:DEVICE_STATUS_BUSY;
        }
        else if(req->payload[0]==5u&&req->payload_len==1u){uint16_t learned=Ir_GetLearnedMask();payload[0]=(uint8_t)IrBuf.type;payload[1]=IrBuf.isFinish;payload[2]=Dev.errorCode.bit.irMatch?1u:0u;payload[3]=Dev.errorCode.bit.irLearn?1u:0u;put16(payload+4,Dev.irType);payload[6]=Dev.irIdx;payload[7]=Dev.learnNum;put16(payload+8,learned);payload[10]=IrLearnChannel;*payload_len=11;}
        else if(req->payload[0]==6u&&req->payload_len==1u)status=Ir_CancelOperation()?DEVICE_STATUS_OK:DEVICE_STATUS_IO_ERROR;
        else if(req->payload[0]==7u&&req->payload_len==2u){if(req->payload[1]>=MAX_IR_LEARNNUM)status=DEVICE_STATUS_INVALID_ARG;else status=Ir_ResetLearned(req->payload[1])?DEVICE_STATUS_OK:DEVICE_STATUS_BUSY;}
        else if(req->payload[0]==8u&&req->payload_len==1u)status=Ir_ResetAllLearned()?DEVICE_STATUS_OK:DEVICE_STATUS_BUSY;
        else status=DEVICE_STATUS_INVALID_ARG;
        break;
    case DEVICE_OP_IR_ACTION:
        if(req->payload_len!=1u||req->payload[0]!=1u){status=DEVICE_STATUS_NOT_SUPPORTED;break;}
        if(!Ir_ConfiguredCommandSupported(IR_CMD_POWER_OFF)){status=DEVICE_STATUS_NOT_SUPPORTED;break;}
        status=Ir_ExecuteConfiguredVerified(IR_CMD_POWER_OFF)?DEVICE_STATUS_OK:DEVICE_STATUS_BUSY;
        if(status==DEVICE_STATUS_OK){update_power_state(0);SaveDevInfo(50u);Ml307_RequestReport();}
        break;
    case DEVICE_OP_GET_DIAGNOSTICS:{
        const HLW8110_Status_t *meter=HLW8110_GetStatus();
        payload[0]=(Dev.errorCode.bit.flash||Health_StorageError())?1u:0u;payload[1]=Health_LastResetReason();
        put16(payload+2,Dev.errorCode.u16Val);payload[4]=Health_LastUnhealthyMask();payload[5]=Dev.loraStatus;payload[6]=Health_ConsecutiveResets();payload[7]=payload[1];
        put32(payload+8,LocalTimestamp);put32(payload+12,Dev.lastReportTime);put16(payload+16,Dev.loadPower);
        put16(payload+18,meter->voltage_dv);put16(payload+20,meter->current_ma);payload[22]=meter->valid;payload[23]=meter->consecutive_errors;
        put16(payload+24,meter->communication_errors);put32(payload+26,Dev.meter.energy_wh);
        *payload_len=30;break;}
    case DEVICE_OP_GET_LORA_PARAMS:
        if(!DeviceService_MaintenanceActive()){status=DEVICE_STATUS_UNAUTHORIZED;break;}
        payload[0]=Dev.loraRegisterSf;payload[1]=Dev.loraRegisterBw;
        payload[2]=Dev.loraListenSf;payload[3]=Dev.loraListenBw;*payload_len=4;break;
    case DEVICE_OP_SET_LORA_PARAMS:
        if(!DeviceService_MaintenanceActive()){status=DEVICE_STATUS_UNAUTHORIZED;break;}
        if(req->payload_len!=4u){status=DEVICE_STATUS_INVALID_ARG;break;}
        status=LoraParams_Save(req->payload[0],req->payload[1],req->payload[2],req->payload[3]);
        if(status==DEVICE_STATUS_OK){Dev.loraStatus=Status_Logining;Timer_Lora=LORA_SEC_TO_TICKS(300);}
        break;
    case DEVICE_OP_GET_CONNECTIVITY_CONFIG:
        status=Connectivity_Encode(payload,DEVICE_MAX_PAYLOAD,payload_len);
        break;
    case DEVICE_OP_SET_CONNECTIVITY_CONFIG:
    {
        connectivity_config_t config;
        const connectivity_config_t *current=Connectivity_Get();
        uint8_t lora_changed;
        uint8_t cellular_changed;
        status=Connectivity_Decode(req->payload,req->payload_len,&config);
        if(status!=DEVICE_STATUS_OK)break;
        lora_changed=((current->transport_mask^config.transport_mask)&CONNECTIVITY_LORA)||
                     (current->lora_register_sf!=config.lora_register_sf)||
                     (current->lora_register_bw!=config.lora_register_bw)||
                     (current->lora_listen_sf!=config.lora_listen_sf)||
                     (current->lora_listen_bw!=config.lora_listen_bw);
        cellular_changed=((current->transport_mask^config.transport_mask)&CONNECTIVITY_CELLULAR)||
                         memcmp((const uint8_t *)current+offsetof(connectivity_config_t,cellular_pdp_type),
                                (const uint8_t *)&config+offsetof(connectivity_config_t,cellular_pdp_type),
                                sizeof(config)-offsetof(connectivity_config_t,cellular_pdp_type));
        status=Connectivity_Save(&config);
        if(status==DEVICE_STATUS_OK){
            put32(payload,Connectivity_GetGeneration());*payload_len=4u;
            if(lora_changed){Dev.loraStatus=Status_Logining;Timer_Lora=LORA_SEC_TO_TICKS(300);}
            if(cellular_changed)Ml307_ApplyConfiguration();
        }
        break;
    }
    case DEVICE_OP_GET_CONNECTIVITY_STATUS:
    {
        const connectivity_config_t *config=Connectivity_Get();
        const ml307_status_t *cell= Ml307_GetStatus();
        payload[0]=CONNECTIVITY_SCHEMA;payload[1]=config->transport_mask;payload[2]=cell->phase;
        payload[3]=cell->sim_ready;payload[4]=cell->network_registered;payload[5]=cell->mqtt_online;
        payload[6]=(uint8_t)cell->signal_rssi;payload[7]=cell->last_error;payload[8]=cell->consecutive_failures;
        put16(payload+9,cell->reset_count);put16(payload+11,cell->publish_count);
        put16(payload+13,cell->command_executed_count);payload[15]=0u;payload[16]=0u;
        put16(payload+17,cell->command_rejected_count);put16(payload+19,cell->rx_overflow_count);
        put32(payload+21,cell->last_connected_ms);put32(payload+25,cell->last_report_ms);
        payload[29]=(uint8_t)Dev.loraStatus;payload[30]=transport_online();put32(payload+31,CurTick);
        payload[35]=cell->uart_active;payload[36]=cell->waiting;put16(payload+37,cell->timeout_count);
        put32(payload+39,cell->rx_bytes);put32(payload+43,cell->tx_bytes);
        put32(payload+47,cell->retry_remaining_ms);payload[51]=Ml307_AtGetStatus()->state;
        payload[52]=(uint8_t)(Dev.loraStatus>=Status_Connected?Lora_GetRssi():-127);
        payload[53]=(uint8_t)cell->rsrp_dbm;put16(payload+54,(uint16_t)cell->rsrq_db_x10);
        payload[56]=15u;memcpy(payload+57,Ml307_GetDeviceId(),15u);*payload_len=72u;break;
    }
    case DEVICE_OP_RESTART_CELLULAR:
        if(!DeviceService_MaintenanceActive()){status=DEVICE_STATUS_UNAUTHORIZED;break;}
        if(req->payload_len!=0u){status=DEVICE_STATUS_INVALID_ARG;break;}
        Ml307_Restart();break;
    case DEVICE_OP_CELLULAR_AT:
    {
        const ml307_at_status_t *at;
        uint8_t response_length;
        if(!DeviceService_MaintenanceActive()){status=DEVICE_STATUS_UNAUTHORIZED;break;}
        if(req->payload_len<1u){status=DEVICE_STATUS_INVALID_ARG;break;}
        if(req->payload[0]==1u)
            status=Ml307_AtStart(req->payload+1,(uint8_t)(req->payload_len-1u));
        else if(req->payload[0]==2u){
            if(req->payload_len!=1u){status=DEVICE_STATUS_INVALID_ARG;break;}
            status=Ml307_AtCancel();
        } else if(req->payload[0]!=0u || req->payload_len!=1u){
            status=DEVICE_STATUS_INVALID_ARG;
        }
        if(status!=DEVICE_STATUS_OK)break;
        at=Ml307_AtGetStatus();
        payload[0]=2u;payload[1]=at->state;payload[2]=at->truncated;
        payload[3]=Ml307_GetStatus()->uart_active;put16(payload+4,at->generation);
        put32(payload+6,at->elapsed_ms);
        put16(payload+10,Ml307_AtGetTxDelta());put16(payload+12,Ml307_AtGetRxDelta());
        response_length=Ml307_AtCopyResponse(payload+15,(uint8_t)(DEVICE_MAX_PAYLOAD-15u));
        payload[14]=response_length;*payload_len=(uint16_t)response_length+15u;break;
    }
    case DEVICE_OP_SET_DEVICE_NAME:
        if(req->payload_len<2u||req->payload[0]!=(uint8_t)(req->payload_len-1u)){
            status=DEVICE_STATUS_INVALID_ARG;break;
        }
        status=DeviceProfile_SaveName((const char *)(req->payload+1),req->payload[0]);
        if(status==DEVICE_STATUS_OK){Peripheral_RefreshDeviceName();payload[0]=req->payload[0];*payload_len=1u;}
        break;
    case DEVICE_OP_RESTART_DEVICE:
        if(req->payload_len!=0u){status=DEVICE_STATUS_INVALID_ARG;break;}
        Peripheral_RequestReset();
        break;
    case DEVICE_OP_GET_REMOTE_OTA_STATUS:
    {
        const ota_metadata_t *ota=Ota_Get();
        if(!DeviceService_MaintenanceActive()){status=DEVICE_STATUS_UNAUTHORIZED;break;}
        payload[0]=1u;payload[1]=ota->state;
        put32(payload+2,ota->current_version);put32(payload+6,ota->update_version);
        put32(payload+10,ota->image_size);put32(payload+14,ota->downloaded_bytes);
        put32(payload+18,ota->image_crc32);*payload_len=22u;break;
    }
    case DEVICE_OP_FACTORY_RESET:
        if(!DeviceService_MaintenanceActive()){status=DEVICE_STATUS_UNAUTHORIZED;break;}
        if(Ota_Get()->state!=OTA_STATE_IDLE){status=DEVICE_STATUS_CONFLICT;break;}
        if(!Ir_PrepareConfigurationChange()){status=DEVICE_STATUS_BUSY;break;}
        status=Storage_FactoryReset();
        if(status==DEVICE_STATUS_OK){
            staged_config_valid=0u;
            Dev.loraStatus=Status_Logining;Timer_Lora=0u;
            Ml307_ApplyConfiguration();
        }
        break;
    default:status=DEVICE_STATUS_NOT_SUPPORTED;break;
    }
    return status;
}

uint8_t DeviceService_HandleFrame(const uint8_t *request,uint16_t request_len,uint8_t *response,uint16_t capacity,uint16_t *response_len)
{
    device_frame_t req,rsp;uint8_t payload[DEVICE_MAX_PAYLOAD];uint16_t payload_len=0;uint8_t status;
    status=DeviceProtocol_Decode(request,request_len,&req);if(status!=DEVICE_STATUS_OK||req.type!=DEVICE_FRAME_REQUEST)return status;
    status=dispatch(&req,payload,&payload_len);rsp.type=DEVICE_FRAME_RESPONSE;rsp.seq=req.seq;rsp.opcode=req.opcode;rsp.status=status;rsp.payload_len=payload_len;rsp.payload=payload;
    return DeviceProtocol_Encode(&rsp,response,capacity,response_len);
}
