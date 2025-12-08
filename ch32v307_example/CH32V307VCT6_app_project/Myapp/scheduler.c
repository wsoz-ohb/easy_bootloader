#include "scheduler.h"

// 全局变量，用于存储任务数量
uint8_t task_num;

typedef struct {
    void (*task_func)(void);
    uint32_t rate_ms;
    uint32_t last_run;
} task_t;

void printf_tick(void)
{
    static uint32_t tick=0;
    tick++;
    printf("systick:%d\r\n",tick);
}

// 静态任务数组，每个任务包含：任务函数、执行周期(毫秒)、上次运行时间(毫秒)
static task_t scheduler_task[] =
{
    {easy_bootloader_app_run, 10, 0},
    {printf_tick,1000,0},
};

/**
 * @brief 调度器初始化函数
 * 计算任务数组元素个数，并将其存储到 task_num 中
 */
void scheduler_init(void)
{
    task_num = sizeof(scheduler_task) / sizeof(task_t);
    mytim6_init();
    myuart2_init();
    __enable_irq();  // 开启全局中断，保证 TIM6/USART2 中断工作
    easy_bootloader_app_init();
}

/**
 * @brief 调度器运行函数
 * 遍历任务数组，检查是否有任务需要执行。如果当前时间已经超过任务执行周期，则执行该任务并更新上次运行时间
 */
void scheduler_run(void)
{
    for (uint8_t i = 0; i < task_num; i++)
    {
        uint32_t now_time = get_uwtick();

        if (now_time >= scheduler_task[i].rate_ms + scheduler_task[i].last_run)
        {
            scheduler_task[i].last_run = now_time;
            scheduler_task[i].task_func();
        }
    }
}
