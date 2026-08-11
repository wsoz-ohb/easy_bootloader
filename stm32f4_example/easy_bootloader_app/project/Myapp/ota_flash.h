#ifndef OTA_FLASH_H
#define OTA_FLASH_H

#include <stdbool.h>
#include <stdint.h>


#define DATA_SOURCE 1	//测试数据源来源于串口2

void ota_flash_init(void);
bool ota_flash_stream_ready(void);
uint32_t ota_flash_read(uint8_t *buf, uint32_t max_len);

void ota_flash_set_write_addr(uint32_t addr);
uint32_t ota_flash_write_buffer(const uint8_t *buffer, uint32_t len);
uint32_t ota_flash_get_write_addr(void);

#endif /* OTA_FLASH_H */
