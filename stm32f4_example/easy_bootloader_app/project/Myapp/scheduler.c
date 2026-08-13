#include "scheduler.h"

extern void bootloader_app_init(void);
extern void bootloader_app_loop(void);

uint8_t task_num;

typedef struct
{
    void (*task_func)(void);
    uint32_t rate_ms;
    uint32_t last_run;
} task_t;

static task_t scheduler_task[] =
{
    {uart1_task, 100U, 0U},
    {bootloader_app_loop, 10U, 0U},
};

void scheduler_init(void)
{
    task_num = sizeof(scheduler_task) / sizeof(scheduler_task[0]);
    myusart_init();
    bootloader_app_init();
}

void scheduler_run(void)
{
    uint8_t index;

    for (index = 0U; index < task_num; index++)
    {
        uint32_t now_time = HAL_GetTick();

        if ((uint32_t)(now_time - scheduler_task[index].last_run) >=
            scheduler_task[index].rate_ms)
        {
            scheduler_task[index].last_run = now_time;
            scheduler_task[index].task_func();
        }
    }
}
