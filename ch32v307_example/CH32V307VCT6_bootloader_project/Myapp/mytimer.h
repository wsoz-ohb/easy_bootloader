#ifndef MYAPP_MYTIMER_H_
#define MYAPP_MYTIMER_H_

#include "bsp_sys.h"
extern uint32_t uwtick;

uint32_t get_uwtick(void);
void mytim6_init(void);

#endif /* MYAPP_MYTIMER_H_ */
