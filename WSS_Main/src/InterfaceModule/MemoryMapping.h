/*
 * MemoryMapping.h
 *
 *  Created on: Mar 9, 2023
 *      Author: Administrator
 */

#ifndef SRC_INTERFACEMODULE_MEMORYMAPPING_H_
#define SRC_INTERFACEMODULE_MEMORYMAPPING_H_

#include <stdint.h>
#include <fcntl.h>              // Flags for open()
#include <sys/stat.h>           // Open() system call
#include <sys/types.h>          // Types for open()
#include <unistd.h>             // Close() system call
#include <string.h>             // Memory setting and copying
#include <getopt.h>             // Option parsing
#include <errno.h>              // Error codes
#include <sys/mman.h>			// for ahb lite

#include <iostream>
#include "GlobalVariables.h"

#define MAP_SIZE 4096UL			// Mapping size of register for AHB LITE register virtual to physical mapping
#define MAP_MASK (MAP_SIZE -1)

#ifdef _XILINX_
#define OCM_TRANS_BASEADDR 0x43C00000
#define REGISTER2_BASEADDR 0x43C10000
#define CLUT_BASEADDR 0x43C20000
#define SEQ_BASEADDR 0x43C30000
#define TEC_BASEADDR 0x43C40000
#define GPIO_BASEADDR 0x41200000
#define HEC7020_OCM_LOC 	0xFFFC0000			//0xe0880000//
#else
#define OCM_TRANS_BASEADDR 0x80300000     //0x43C00000
#define REGISTER2_BASEADDR 0x80000000     //0x43C10000
#define CLUT_BASEADDR 0x80100000          //0x43C20000
#define SEQ_BASEADDR 0x80200000           //0x43C30000
#define TEC_BASEADDR 0x80500000           //0x43C40000
#define GPIO_BASEADDR 0x80400000          //0x41200000
#define HEC7020_OCM_LOC 	0x61000000			//0xe0880000//
#endif

#define HEC7020_OCM_TRANS_SIZE 	256*1024


class MemoryMapping {
public:
					MemoryMapping(int mapFlag);
	virtual 		~MemoryMapping();

	enum 			MAP{REG = 0, CLUT, SEQ, OCMREG, OCMMEM, TEC, GPIO};

	// REGISTER2
	int 			ReadRegister_Reg2(uint8_t addr, uint8_t *value);
	int 			ReadRegistersPart_Reg2(uint8_t startAddr, uint8_t *data, int size);

	int 			WriteRegister_Reg2(uint8_t addr, uint8_t value);
	int 			WriteRegistersPart_Reg2(uint8_t startAddr, uint8_t *data, int size);

	// OCM
	int 			ReadRegister_OCM(uint8_t addr, uint8_t *value);
	int 			ReadRegister_OCM32(int addr, int *value);
	int 			ReadRegistersPart_OCM(uint8_t startAddr, uint8_t *data, int size);

	int 			WriteRegister_OCM(uint8_t addr, uint8_t value);
	int 			WriteRegister_OCM32(int addr, int value);
	int 			WriteRegistersPart_OCM(uint8_t startAddr, uint8_t *data, int size);

	// CLUT
	int 			ReadRegister_CLUT(uint8_t addr, uint8_t *value);
	int 			ReadRegistersPart_CLUT(uint8_t startAddr, uint8_t *data, int size);

	int 			WriteRegister_CLUT(uint8_t addr, uint8_t value);
	int 			WriteRegistersPart_CLUT(uint8_t startAddr, uint8_t *data, int size);

	// SEQ
	int 			ReadRegister_SEQ(uint8_t addr, uint8_t *value);
	int 			ReadRegistersPart_SEQ(uint8_t startAddr, uint8_t *data, int size);

	int 			WriteRegister_SEQ(uint8_t addr, uint8_t value);
	int 			WriteRegistersPart_SEQ(uint8_t startAddr, uint8_t *data, int size);

	// GPIO
	int 			ReadRegister_GPIO(int addr, unsigned int *value);
	int 			WriteRegister_GPIO(int addr, unsigned int value);

	// TEC
	int 			ReadRegister_TEC32(int addr, unsigned int *value);
	int 			WriteRegister_TEC32(int addr, unsigned int value);

	// OCM memory region for pattern transfer
	void 			GetOCMMemoryRegion(void **memRegion);

private:

	using vUChar = 	volatile unsigned char;
	//using vULong = 	volatile unsigned long;
	using vULong = 	volatile uint32_t;

	int 			fd;

	vUChar*			regBase;
	vUChar*			ocmBase;
	vULong*			ocmBase32;
	vUChar*			clutBase;
	vUChar*			seqBase;
	vULong*			tecBase;
	vULong*			gpioBase;

	void*			reg_mappedBase = NULL;

	void*			ocm_mappedBase = NULL;


	void*			clut_mappedBase = NULL;


	void*			seq_mappedBase = NULL;


	void*			tec_mappedBase = NULL;


	void*			ocm_Memory = NULL;

	void*           gpio_mappedBase = NULL;

private:

	int 			InitializeMMAP_Register2(void);
	int 			InitializeMMap_CLUT(void);
	int 			InitializeMMap_SEQ(void);
	int 			InitializeMMap_OCM(void);
	int 			InitializeMMap_TEC(void);
	int 			InitializeMMap_OCMMEM(void);
	int             InitializeMMap_GPIO(void);

	int 			MMAP_Register2(int fd);
	int 			MMAP_CLUT(int fd);
	int 			MMAP_SEQ(int fd);
	int 			MMAP_OCMRegister(int fd);
	int 			MMAP_OCMMemory(int fd);
	int 			MMAP_TECRegister(int fd);
	int             MMAP_GPIO(int fd);

};

#endif /* SRC_INTERFACEMODULE_MEMORYMAPPING_H_ */
