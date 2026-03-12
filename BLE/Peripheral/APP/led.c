#include "board.h"

typedef struct{
    uint32_t Pin;
    bool IsBlinking;
    uint32_t BlinkInterval;
    uint32_t LastBlinkTime;
    bool DefaultState; // 停止闪烁时的默认状态：0-灭，1-亮
}LED_Ctx_t;

// 使用数组管理所有 LED，方便扩展和统一处理
static LED_Ctx_t LEDs[] = {
    {LED_RED_PIN,   FALSE, 0, 0, 0},
    {LED_BLUE_PIN,  FALSE, 0, 0, 0},
    {LED_WHITE_PIN, FALSE, 0, 0, 0},
    {LED_GREEN_PIN, FALSE, 0, 0, 0},
};

/**
 * @brief 设置 LED 闪烁参数
 * 
 * @param Pin LED 引脚
 * @param IsBlinking 是否开启闪烁
 * @param BlinkInterval 闪烁翻转间隔(ms)，0表示常亮
 * @param DefaultState 停止闪烁时的状态(FALSE:灭, TRUE:亮)
 */
static void LED_SetBlink(uint32_t Pin, bool IsBlinking, uint32_t BlinkInterval, bool DefaultState){
    for(int i=0; i<sizeof(LEDs)/sizeof(LEDs[0]); i++){
        if(LEDs[i].Pin == Pin){
            // 如果参数没有变化，直接返回，避免不必要的处理
            if((LEDs[i].IsBlinking == IsBlinking) && (LEDs[i].BlinkInterval == BlinkInterval)){
                return;
            }
            
            LEDs[i].IsBlinking = IsBlinking;
            LEDs[i].BlinkInterval = BlinkInterval;
            LEDs[i].DefaultState = DefaultState;
            // 重置计时器，从当前时刻开始计时
            LEDs[i].LastBlinkTime = CurTick; 
            
            // 如果停止闪烁，立即应用默认状态
            if(!IsBlinking){
                 if(DefaultState) GPIOB_SetBits(Pin);
                 else GPIOB_ResetBits(Pin);
            } else {
                // 如果开始闪烁且 Interval 为 0，立即点亮
                if(BlinkInterval == 0){
                    GPIOB_SetBits(Pin);
                }
            }
            return;
        }
    }
}

void LED_RED_BLINK(bool IsBlinking, uint32_t BlinkInterval){
    LED_SetBlink(LED_RED_PIN, IsBlinking, BlinkInterval, FALSE);
}

void LED_BLUE_BLINK(bool IsBlinking, uint32_t BlinkInterval){
    LED_SetBlink(LED_BLUE_PIN, IsBlinking, BlinkInterval, FALSE);
}

void LED_WHITE_BLINK(bool IsBlinking, uint32_t BlinkInterval){
    LED_SetBlink(LED_WHITE_PIN, IsBlinking, BlinkInterval, FALSE);
}

void LED_GREEN_BLINK(bool IsBlinking, uint32_t BlinkInterval){
    LED_SetBlink(LED_GREEN_PIN, IsBlinking, BlinkInterval, FALSE);
}

void LED_BLINK(void){
    uint32_t CurrentTime = CurTick;
    for(int i=0; i<sizeof(LEDs)/sizeof(LEDs[0]); i++){
        // 仅处理开启闪烁且 Interval > 0 的 LED
        // Interval == 0 的情况已在 SetBlink 中处理（常亮），无需在此轮询
        if(LEDs[i].IsBlinking && LEDs[i].BlinkInterval > 0){
            // 使用无符号减法处理时间回绕问题 (假设 CurTick 溢出)
            if(CurrentTime - LEDs[i].LastBlinkTime >= LEDs[i].BlinkInterval){
                LEDs[i].LastBlinkTime = CurrentTime;
                GPIOB_InverseBits(LEDs[i].Pin);
            }
        }
    }
}
