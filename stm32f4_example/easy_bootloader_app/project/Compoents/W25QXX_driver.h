#ifndef __BSP_W25QXX_H
#define __BSP_W25QXX_H

#include "main.h"
#include "spi.h"

//W25Q64 Instruction Set Table 1
#define	W25Q64_WriteEnable 										0x06
#define	W25Q64_WriteDisable 									0x04
#define	W25Q64_Read_Status_Register_1 				0x05
#define	W25Q64_Read_Status_Register_2 				0x35
#define	W25Q64_Write_Status_Register 					0x01
#define	W25Q64_Page_Program 									0x02
#define	W25Q64_Quad_Page_Program 							0x32
#define	W25Q64_Block_Erase_64KB 							0xD8
#define	W25Q64_Block_Erase_32KB 							0x52
#define	W25Q64_Sector_Erase_4KB 							0x20
#define	W25Q64_Chip_Erase 										0xC7
#define	W25Q64_Erase_Suspend 									0x75
#define	W25Q64_Erase_Resume 									0x7A
#define	W25Q64_Power_down 										0xB9
#define	W25Q64_High_Performance_Mode 					0xA3
#define	W25Q64_Continuous_Read_ModeReset			0xFF
#define	W25Q64_Release_Power_down							0xAB
#define	W25Q64_HPM_OR_Device_ID								0xAB
#define	W25Q64_Manufacturer_OR_Device_ID 			0x90
#define	W25Q64_Read_Unique_ID	 								0x4B
#define	W25Q64_JEDEC_ID 											0x9F

//W25Q64 Instruction Set Table 2(Read Instructions)
#define	W25Q64_Read_Data 									0x03
#define	W25Q64_Fast_Read 									0x0B
#define	W25Q64_Fast_Read_Dual_Output 			0x3B
#define	W25Q64_Fast_Read_Dual_I_O 				0xBB
#define	W25Q64_Fast_Read_Quad_Output 			0x6B
#define	W25Q64_Fast_Read_Quad_I_O 				0xEB
#define	W25Q64_Octal_Word Read_Quad_I_O 	0xE3

#define Dummy_Byte												0xFF
/**************************************************************/



#define W25Qx_OK            ((uint8_t)0x00)
#define W25Qx_ERROR         ((uint8_t)0x01)
#define W25Qx_BUSY          ((uint8_t)0x02)
#define W25Qx_TIMEOUT				((uint8_t)0x03)


/* Flag Status Register */
#define W25Q128FV_FSR_BUSY                    ((uint8_t)0x01)    /*!< busy */
#define W25Q128FV_FSR_WREN                    ((uint8_t)0x02)    /*!< write enable */
#define W25Q128FV_FSR_QE                      ((uint8_t)0x02)    /*!< quad enable */



#define W25QXX_CS_GPIO_Port		GPIOA
#define W25QXX_CS_Pin					GPIO_PIN_4

#define W25Qxx_CS_LOW() 			HAL_GPIO_WritePin(W25QXX_CS_GPIO_Port, W25QXX_CS_Pin, GPIO_PIN_RESET)
#define W25Qxx_CS_HIGH() 			HAL_GPIO_WritePin(W25QXX_CS_GPIO_Port, W25QXX_CS_Pin, GPIO_PIN_SET)

#define W25Qxx_TIMEOUT_VALUE			1000
#define W25Q64_PageSize						256


uint8_t BSP_W25Qxx_Read_Byte(void);
uint8_t BSP_W25Qxx_Write_Byte(uint8_t Tx_Byte);
uint8_t BSP_W25Qxx_WriteEnable(void);

uint32_t BSP_W25Qxx_Read_ID(void);
uint8_t BSP_W25Qxx_BufferRead(uint8_t *ReadBuffer, uint32_t ReadAddr, uint16_t NumByteToRead);

uint8_t BSP_W25Qxx_PageWrite(uint8_t *WriteBuffer, uint32_t WriteAddr, uint16_t NumByteToWrite);
uint8_t BSP_W25Qxx_BufferWrite(uint8_t *WriteBuffer, uint32_t ReadAddr, uint16_t NumByteToWrite);

uint8_t BSP_W25Qxx_SectorErase(uint32_t Address);
void BSP_W25Qxx_BlockErase(void);

#endif

