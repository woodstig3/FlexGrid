/*
 * LcosDisplayModule.cpp
 *
 *  Created on: Mar 9, 2023
 *      Author: Administrator
 */

#include "InterfaceModule/EEPROMUpdate.h"

/*
 * This Module is designed to perform operations
 * on EEPROM using i2c protocol. The module can
 * read/write individual registers in EEPROM
 * and can update whole EEPROM from the given
 * .hex/.hec file on FileSystem.
 */

EEPROMUpdate::EEPROMUpdate() {

	// Create I2c Interface for Update
	unsigned char eepromAddr = 0x50;
	i2c = new I2CProtocol(eepromAddr);
																								// Makes ure pointer is at 0x2A position now
	int Status = OpenFile(hecfilename , 0x2A, inputBuffer_Hex_NoHeader, HOLO_HEX_DATA_SIZE);	// Actual EEPROM data starts from 0x2A in .hec file

	if(Status != 0)
	{
		printf("Driver<EEPROMUpdate>: EEPROM Configuration .hec file not found.\n");
		g_bFileOpen = false;
	}
	else
	{
		g_bFileOpen = true;
	}

}

EEPROMUpdate::~EEPROMUpdate() {

	if(file.is_open())
	{
	    file.seekg(0, std::ios::end);
	    file.close();
	}

	if(i2c != NULL)
	{
		delete i2c;
	}
}

int EEPROMUpdate::OpenFile(const char* fileName, unsigned int at, char buff[] ,unsigned int size)
{
	if(file.is_open())		// if any file is open close it
	{
	    file.seekg(0, std::ios::end);
	    file.close();
	}

	std::fstream::pos_type line = 0;

	file.open(fileName);

	if(file.is_open())
	{
		file.seekg(at, std::ios::beg);		// Actual EEPROM data starts from 0x2A in .hec file
		line = file.tellg();					// Makes Sure pointer is at 0x2A position now

		if(line == -1)
		{
			file.seekg(0, std::ios::end);
			file.close();
			return line;
		}

		std::cout << " begin is : " << line << std::endl;


		file.read(buff, size);

	}
	else
	{
		std::cout << "failed to open file" << std::endl;
		return -1;

	}

//	for(int i =0; i< 100; ++i)
//		std::cout << static_cast<int>(buff[i]) << std::endl;

	return 0;
}

int EEPROMUpdate::InitiateUpdate(void)
{
	if(!g_bFileOpen)
		return -1;

	 return (WriteEEPROM(0, &inputBuffer_Hex_NoHeader[0], HOLO_HEX_DATA_SIZE));
}

int EEPROMUpdate::WriteEEPROM(unsigned int eepromAddr, char* p_data , unsigned int dataLength)
{
	unsigned int  num_writes;
	unsigned char first_write_size = PAGESIZE;
	unsigned char last_write_size;
	unsigned char write_size;
	unsigned int  page_space;

	unsigned int writeAddress = eepromAddr;

//	p_data = &inputBuffer_Hex_NoHeader[0];

	// Update: on May-29-2023

	// Calculate space available in first page
	// Dividing eeaddress by PAGESIZE: This division determines the number of PAGESIZE-byte blocks that fit before the given address.
	// Adding 1: The code adds 1 to the calculated page number to determine the next page number.
	// Multiplying by 128: The resulting page number is then multiplied by 128 to get the address of the next page.
	// Subtracting eeaddress: Finally, the starting address (eeaddress) is subtracted from the calculated next page address. This gives the space available in the first page.

	page_space = int(((eepromAddr / PAGESIZE) + 1) * PAGESIZE) - eepromAddr;

	// Calculate first write size
	if (page_space > PAGESIZE) {
	   first_write_size = PAGESIZE;
	}
	else {
	   first_write_size = page_space;
	}
	// Update ends here

	std::cout << "page_space = " << page_space << " first_write_size " << (int)first_write_size << " eepromAddr " << writeAddress << std::endl;
	// calculate size of last write
	if (dataLength>first_write_size)
	{
	 //last_write_size = (HOLO_HEX_DATA_SIZE-first_write_size)%16;   <-- stable EEPROM version, why 16 though>?
	 last_write_size = (dataLength-first_write_size)%PAGESIZE;
	 printf("last_write_size %d \r\n", last_write_size);
	}

	// Calculate how many writes we need
	if (dataLength>first_write_size)
	{
	  num_writes = ((dataLength-first_write_size)/PAGESIZE)+2;
	  printf("num_writes %d \r\n", num_writes);
	}
	else
	{
	  num_writes = 1;
	}

	// Write to EEPROM PAGE BY PAGE...

	for(unsigned int page=0; page<num_writes; page++)
	{
		printf("Address %d \r\n", writeAddress);

	    if(page==0) write_size= first_write_size;
	    else if(page==(num_writes-1)) write_size=last_write_size;
	    else write_size= PAGESIZE;

		if(i2c->IicWrite(writeAddress, (Xuint8*)p_data, write_size) != 0)
			return -1;

		writeAddress = writeAddress + write_size;
		p_data = p_data + write_size;
	}

	return 0;
}

int EEPROMUpdate::PrintEEPROM(unsigned int size)
{
	if(ReadEEPROM(size) != 0)
		return -1;

	// Perform Printing

	int EEPROM_Index = 0x2A;
	int line_counter = 0;

	printf("%0x: ", EEPROM_Index);

	for (unsigned int Index = 0; Index < size; Index++)
	{
		printf("%02x ", ReadBuffer[Index]);

		if(((Index == 5) || (line_counter == 15)) && (Index!= 0))
		{
			line_counter = 0;
			printf("\r\n");
			printf("%0x: ", ++EEPROM_Index);
		}
		else
		{
			line_counter++;
			EEPROM_Index++;
		}
	}

	return 0;
}

int EEPROMUpdate::ReadEEPROM(unsigned int size)
{
	int Status;
	unsigned int read_Index = 0;
	unsigned char *p_buf = 0;

	p_buf = &ReadBuffer[0];

	// Data in HEX FILE is until 0x362A = 13824.. HOLO_HEX_DATA_SIZE

	while(read_Index < size)
	{
		Status = i2c->IicRead(read_Index, (Xuint8*)p_buf, PAGESIZE);

		if (Status != 0)
		{
			printf("Print EEPROM Failed\r\n");
			return -1;
		}

		read_Index = read_Index + PAGESIZE;
		p_buf = p_buf + PAGESIZE;
	}

	return 0;
}

int EEPROMUpdate::LoadAt(const char* fileName, unsigned int address, unsigned int size)
{
	char buff[9000]{0};	// buffer to hold 1000 bytes maximum
	char path[6] = "/mnt/";
	strcat(path, fileName);

	if(size >= 9000)
	{
		printf("File size is too big\r\n");
		return -1;
	}

	int Status = OpenFile(path , 0, buff, size);

	if(Status == -1)
	{
		printf("Failed to open file LoadAt\r\n");
		return -1;
	}
	std::cout << " address= " << address << std::endl;
	// Write to eeprom at address
	Status = WriteEEPROM(address, buff, size);

	if(Status == 0)
	{
		// Print what is written
		PrintEEPROM(HOLO_HEX_DATA_SIZE + size);		// print preivous data in eeprom and this new one also
	}
	else
	{
		printf("Write EEPROM Failed\r\n");
		return -1;
	}
}

int EEPROMUpdate::VerifyWriteOperation(void)
{
	if(!g_bFileOpen)
		return -1;

	int EEPROM_addr = 0;
	int b_VerfyPass = 0;

	if(ReadEEPROM() != 0)
		return -1;

	for(int i =0; i<HOLO_HEX_DATA_SIZE; i++)		// 13824 is where data ends in .hec file
	{
		if(ReadBuffer[i] != inputBuffer_Hex_NoHeader[i])
		{
			b_VerfyPass = 1;
			printf("Difference At %0x: ReadBuffer = %0x : Original = %0x \r\n", EEPROM_addr, ReadBuffer[i], inputBuffer_Hex_NoHeader[i]);
		}
		++EEPROM_addr;
	}

	if(!b_VerfyPass)
	{
		printf("100%% Verification Success !!!\r\n");
	}

	return 0;
}
