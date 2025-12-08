#include "mytimer.h"

/*
 --定时器2用于产生ms时间戳

*/
uint32_t  uwtick;
void TIM6_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void TIM6_IRQHandler(void)
{
    if(TIM_GetITStatus(TIM6, TIM_IT_Update)==SET)
    {
        uwtick++;
    }
    TIM_ClearITPendingBit( TIM6, TIM_IT_Update );
}

void mytim6_init(void)
{
    NVIC_InitTypeDef NVIC_InitStructure = {0};
    TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure = {0};

    // 使能 TIM6 时钟（TIM6 在 APB1 上）
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM6, ENABLE);

    u16 psc = (SystemCoreClock / 1000000) - 1; // 定时器时钟为 1MHz
    u16 arr = 1000 - 1; // 1ms 中断

    // 配置 TIM6 定时器
    TIM_TimeBaseInitStructure.TIM_Period = arr;           // 自动重载值
    TIM_TimeBaseInitStructure.TIM_Prescaler = psc;        // 预分频器
    TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1; // 时钟分频 1
    TIM_TimeBaseInitStructure.TIM_CounterMode = TIM_CounterMode_Up; // 向上计数
    // TIM6 是基本定时器，不支持重复计数器和一些其他高级功能
    TIM_TimeBaseInit(TIM6, &TIM_TimeBaseInitStructure);

    // 清除中断标志
    TIM_ClearITPendingBit(TIM6, TIM_IT_Update);

    // 配置 NVIC
    NVIC_InitStructure.NVIC_IRQChannel = TIM6_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0; // 抢占优先级
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;        // 子优先级
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;           // 使能中断通道
    NVIC_Init(&NVIC_InitStructure);

    // 使能 TIM6 更新中断
    TIM_ITConfig(TIM6, TIM_IT_Update, ENABLE);

    // 启动 TIM6
    TIM_Cmd(TIM6, ENABLE);
}

uint32_t get_uwtick(void)
{
    return uwtick;
}

