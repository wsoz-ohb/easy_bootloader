#ifndef OTA_FLASH_H
#define OTA_FLASH_H

#include <stdbool.h>
#include <stdint.h>

void ota_flash_init(void);
bool ota_flash_stream_ready(void);
uint32_t ota_flash_read(uint8_t *buf, uint32_t max_len);

#endif /* OTA_FLASH_H */
