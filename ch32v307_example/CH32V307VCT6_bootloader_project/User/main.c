#include "debug.h"
#include "bsp_sys.h"


int main(void)
{
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
	SystemCoreClockUpdate();
	Delay_Init();
	USART_Printf_Init(115200);	
	scheduler_init();
	while(1)
    {
	    scheduler_run();
	}
}

