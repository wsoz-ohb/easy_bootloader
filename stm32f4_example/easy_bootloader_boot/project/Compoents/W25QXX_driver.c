#include "W25QXX_driver.h"

/**
  * @brief  通过 SPI 写入 1 字节并返回同时读到的数据
  * @param  Tx_Byte: 要发送的字节
  * @retval SPI 同步返回的字节
  */
uint8_t BSP_W25Qxx_Write_Byte(uint8_t Tx_Byte)
{
    uint8_t Rx_Byte = 0xFF;
    HAL_SPI_TransmitReceive(&hspi1, &Tx_Byte, &Rx_Byte, 1, W25Qxx_TIMEOUT_VALUE);
    return Rx_Byte;
}

/**
  * @brief  通过 SPI 读取 1 字节
  * @param  none
  * @retval 读取到的字节
  */
uint8_t BSP_W25Qxx_Read_Byte(void)
{
    return BSP_W25Qxx_Write_Byte(Dummy_Byte);
}

/**
  * @brief  读取 JEDEC ID
  * @param  none
  * @retval 24bit JEDEC ID
  */
uint32_t BSP_W25Qxx_Read_ID(void)
{
    uint8_t cmd = W25Q64_JEDEC_ID;
    uint8_t id[3] = {0};

    W25Qxx_CS_LOW();
    HAL_SPI_Transmit(&hspi1, &cmd, 1, W25Qxx_TIMEOUT_VALUE);
    HAL_SPI_Receive(&hspi1, id, 3, W25Qxx_TIMEOUT_VALUE);
    W25Qxx_CS_HIGH();

    return ((uint32_t)id[0] << 16) | ((uint32_t)id[1] << 8) | id[2];
}


/**
  * @brief  向FLASH发送 写使能 命令
  * @param  none
  * @retval none
  */
void BSP_W25Qxx_WriteEnable(void)
{
	uint8_t cmd = W25Q64_WriteEnable;
	
	/* 通讯开始，CS拉低 */
	W25Qxx_CS_LOW();
	
  /* 发送写使能命令*/
  HAL_SPI_Transmit(&hspi1, &cmd, 1, W25Qxx_TIMEOUT_VALUE);
	
	/* 通讯结束，CS拉高 */
	W25Qxx_CS_HIGH();
}

/**
  * @brief  FLASH 等待写结束
  * @param  none
  * @retval none
  */
static void BSP_W25Qxx_Wait_for_Write_End(void)
{
  uint8_t state = 0;
	uint8_t cmd = W25Q64_Read_Status_Register_1;

	/* 通讯开始，CS拉低 */
  W25Qxx_CS_LOW();

  /* 发送命令 */
  HAL_SPI_Transmit(&hspi1, &cmd, 1, W25Qxx_TIMEOUT_VALUE);
	
  do
  {
		HAL_SPI_Receive(&hspi1, &state, 1, W25Qxx_TIMEOUT_VALUE);
  }
  while((state & 0x01) == SET);

	/* 通讯结束，CS拉高 */
  W25Qxx_CS_HIGH();
}


/**
  * @brief   读取FLASH数据
  * @param 	 ReadBuffer，存储读出的数据的指针
  * @param   ReadAddr，读取地址
  * @param   NumByte，读取数据长度
  * @retval  无
  */
uint8_t BSP_W25Qxx_BufferRead(uint8_t *ReadBuffer, uint32_t ReadAddr, uint16_t NumByteToRead)
{
	uint8_t cmd[4];
	cmd[0] = W25Q64_Read_Data;
	cmd[1] = (uint8_t)(ReadAddr >> 16);
	cmd[2] = (uint8_t)(ReadAddr >> 8);
	cmd[3] = (uint8_t)ReadAddr;

	/* 通讯开始，CS拉低 */
	W25Qxx_CS_LOW();
  
	/* 写入指令、地址 */	
	HAL_SPI_Transmit(&hspi1, cmd, 4, W25Qxx_TIMEOUT_VALUE);
		
		
	/* 读取数据 */
	HAL_SPI_Receive(&hspi1, ReadBuffer, NumByteToRead, W25Qxx_TIMEOUT_VALUE);
		
	/* 通讯结束，CS拉高 */
	W25Qxx_CS_HIGH();
	
	return W25Qx_OK;
}

 /**
  * @brief  对FLASH进行页写入数据，调用本函数写入数据前需要先擦除扇区
  * @param	WriteBuffer，要写入的数据的指针
  * @param  WriteAddr，写入地址
  * @param  NumByteToWrite，写入数据长度
  * @retval 无
  */
void BSP_W25Qxx_PageWrite(uint8_t *WriteBuffer, uint32_t WriteAddr, uint16_t NumByteToWrite)
{
//	uint16_t i;
	
	uint8_t cmd[4];
	cmd[0] = W25Q64_Page_Program;
	cmd[1] = (uint8_t)(WriteAddr >> 16);
	cmd[2] = (uint8_t)(WriteAddr >> 8);
	cmd[3] = (uint8_t)WriteAddr;
	
	/* 发送FLASH写使能命令 */
	BSP_W25Qxx_WriteEnable();
	
	/* 通讯开始，CS拉低 */
	W25Qxx_CS_LOW();

  /* 写入指令、地址、数据 */
	HAL_SPI_Transmit(&hspi1, cmd, 4, W25Qxx_TIMEOUT_VALUE);
	
	/* 写入数据 */
	HAL_SPI_Transmit(&hspi1, WriteBuffer, NumByteToWrite, W25Qxx_TIMEOUT_VALUE);
	
	/* 通讯结束，CS拉高 */
	W25Qxx_CS_HIGH();
	
	//需要在 W25Qxx_CS_HIGH 之后，即数据传输开始之后
	BSP_W25Qxx_Wait_for_Write_End();
}

/**
  * @brief  对FLASH写入数据，调用本函数写入数据前需要先擦除扇区
  * @param	WriteBuffer，要写入数据的指针
  * @param  WriteAddr，写入地址
  * @param  NumByteToWrite，写入数据的长度
  * @retval none
  */
void BSP_W25Qxx_BufferWrite(uint8_t *WriteBuffer, uint32_t WriteAddr, uint16_t NumByteToWrite)
{
	uint8_t NumOfPage = 0, NumOfSingle = 0, Addr = 0, count = 0, temp = 0;
	
/************************* 对于写入一共四种情况 *******************/
/**			起始地址 WriteAddr 和某一页的起始地址对齐
	*1、数据小于一页
	*2、数据大于一页
	*
	*		起始地址 WriteAddr 不和某一页的起始地址对齐
	*3、数据小于一页
	*4、数据大于一页
	*/
/******************************************************************/

/*mod运算求余，若WriteAddr是W25Q64_PageSize整数倍，运算结果Addr值为0*/
  Addr = WriteAddr % W25Q64_PageSize;
	/*差count个字节数据，刚好可以对齐到页地址*/
	count = W25Q64_PageSize - Addr;
	/*计算出要写多少整数页*/
	NumOfPage = NumByteToWrite / W25Q64_PageSize;
	/*mod运算求余，计算出剩余不满一页的字节数*/
	NumOfSingle = NumByteToWrite % W25Q64_PageSize;
	
	 /* Addr=0,则起始地址WriteAddr刚好是某一页的起始地址 */
	 if(Addr == 0)
	 {
			/* 写入的数据小于一页：NumByteToWrite < W25Q64_PageSize */
			if(NumOfPage == 0)
			{
				BSP_W25Qxx_PageWrite(WriteBuffer, WriteAddr, NumByteToWrite);
			}
			else /* 写入的数据不小于一页：NumByteToWrite >= W25Q64_PageSize */
			{
				/* 先把整数页都写了 */
				while(NumOfPage--)
				{
					BSP_W25Qxx_PageWrite(WriteBuffer, WriteAddr, W25Q64_PageSize);
					WriteAddr +=  W25Q64_PageSize;
					WriteBuffer += W25Q64_PageSize;
				}
				
				/* 若有不满一页的数据，则把它写完 */
				if(NumOfSingle)
					BSP_W25Qxx_PageWrite(WriteBuffer, WriteAddr, NumOfSingle);
			}
	 }
	 /* Addr!=0,则起始地址WriteAddr不是某一页的起始地址 */
	 else
	 {
			/* 写入的数据小于一页：NumByteToWrite < W25Q64_PageSize */
			if(NumOfPage == 0)
			{
				/* 但是尾地址在下一页 */
				if(NumOfSingle > count )
				{
					temp = NumOfSingle - count;
					
					/* 先写满当前页 */
					BSP_W25Qxx_PageWrite(WriteBuffer, WriteAddr, count);
					WriteAddr +=  count;
					WriteBuffer += count;
					
					/* 再写下一页的剩余数据 */
					BSP_W25Qxx_PageWrite(WriteBuffer, WriteAddr, temp);
				}
				/* 尾地址和起始地址WriteAddr在同一页 */
				else
				{
					BSP_W25Qxx_PageWrite(WriteBuffer, WriteAddr, NumByteToWrite);
				}
			}
			else/* 写入的数据不小于一页：NumByteToWrite >= W25Q64_PageSize */
			{
				/* 把头部不对齐的部分(count)单独处理一下，就跟对齐了的是一种情况了 */
				BSP_W25Qxx_PageWrite(WriteBuffer, WriteAddr, count);
				
				NumByteToWrite -= count;
				NumOfPage =  NumByteToWrite / W25Q64_PageSize;
				NumOfSingle = NumByteToWrite % W25Q64_PageSize;
				
				WriteAddr +=  count;
				WriteBuffer += count;
				
				/* 先把整数页都写了 */
				while(NumOfPage--)
				{
					BSP_W25Qxx_PageWrite(WriteBuffer, WriteAddr, W25Q64_PageSize);
					WriteAddr +=  W25Q64_PageSize;
					WriteBuffer += W25Q64_PageSize;
				}
				/* 若有不满一页的数据，则把它写完 */
				if(NumOfSingle)
					BSP_W25Qxx_PageWrite(WriteBuffer, WriteAddr, NumOfSingle);
			}
	 }
}

/**
  * @brief  擦除FLASH扇区
  * @param  SectorAddr：要擦除的扇区地址
  * @retval none
  */
void BSP_W25Qxx_SectorErase(uint32_t SectorAddr)
{
	uint8_t cmd[4];
	cmd[0] = W25Q64_Sector_Erase_4KB;
	cmd[1] = (uint8_t)(SectorAddr >> 16);
	cmd[2] = (uint8_t)(SectorAddr >> 8);
	cmd[3] = (uint8_t)SectorAddr;
	
	/* 发送FLASH写使能命令 */
	BSP_W25Qxx_WriteEnable();
	
	/* 通讯开始，CS拉低 */
	W25Qxx_CS_LOW();
	
	/* 发送命令和地址 */
	HAL_SPI_Transmit(&hspi1, cmd, 4, W25Qxx_TIMEOUT_VALUE);
	
	/* 通讯结束，CS拉高 */
	W25Qxx_CS_HIGH();
	
	/* 需要在W25Qxx_CS_HIGH 之后，即数据传输开始之后 */
	BSP_W25Qxx_Wait_for_Write_End();
}

/**
  * @brief  擦除FLASH块
  * @param  none
  * @retval none
  */
void BSP_W25Qxx_BlockErase(void)
{
	/* 发送FLASH写使能命令 */
	BSP_W25Qxx_WriteEnable();

	/* 通讯开始，CS拉低 */
	W25Qxx_CS_LOW();
	
  BSP_W25Qxx_Write_Byte(W25Q64_Chip_Erase);
		
	/* 通讯结束，CS拉高 */
	W25Qxx_CS_HIGH();
	
	/* 需要在W25Qxx_CS_HIGH 之后，即数据传输开始之后 */
	BSP_W25Qxx_Wait_for_Write_End();
}

