/*
 * LcosDisplayModule.h
 *
 *  Created on: Mar 9, 2023
 *      Author: Administrator
 */

#ifndef SRC_INTERFACEMODULE_EEPROMUPDATE_H_
#define SRC_INTERFACEMODULE_EEPROMUPDATE_H_

#include "InterfaceModule/I2CProtocol.h"
#include <unistd.h>
#include <string.h>
#include <iostream>
#include <fstream>

#define HEX_FILE_SIZE 65577			// .hex File size
#define HOLO_HEX_DATA_SIZE 13824    // Start 2A, End 362A ; 362A-2A = 13824 in decimal
#define PAGESIZE 128
#define MAX_SIZE 65577     			// Index in Hex file - 2A starting point + 1 .... e.g. 22f-2a+1

class EEPROMUpdate {
public:
					EEPROMUpdate();
	virtual 		~EEPROMUpdate();

	int 			InitiateUpdate(void);
	int 			VerifyWriteOperation(void);					// Test if the data written to EEPROM is same as the one in .hec file or not
	int 			PrintEEPROM(unsigned int size=HOLO_HEX_DATA_SIZE);
	int 			LoadAt(const char* fileName, unsigned int address, unsigned int size);

private:

	I2CProtocol 	*i2c = NULL;

	unsigned int 	userInput;
	char 			inputBuffer_Hex_NoHeader[HEX_FILE_SIZE]{0};	// This buffer only contains EEPROM data, no header from .hec for MCU

	std::fstream 	file;
	bool 			g_bFileOpen = false;
	const char*		hecfilename = "/mnt/EEPROM_LINUX_OCM.hec";		// File configured to have OCM working not FPGA

	unsigned char 	ReadBuffer[MAX_SIZE]{1};							/* Read buffer for reading whole EEPROM. */

private:

	int 			OpenFile(const char* fileName, unsigned int at, char buff[] ,unsigned int size);
	int 			ReadEEPROM(unsigned int size=HOLO_HEX_DATA_SIZE);
	int 			WriteEEPROM(unsigned int eepromAddr, char* data , unsigned int dataLength);
};

#endif /* SRC_INTERFACEMODULE_EEPROMUPDATE_H_ */
