/*
 * MemoryMapping.cpp
 *
 *  Created on: Mar 9, 2023
 *      Author: Administrator
 */

#include <pthread.h>

#include "InterfaceModule/MemoryMapping.h"
/*
 * Memory mapping are divided into separate Regions
 * i.e. TEC, SEQ, CLUT, REG2, OCM.. They all have
 * separate read write functions with their unique
 * mutex locks. This is done to optimize and improve
 * the parallel accessing of registers from different
 * threads
 */
MemoryMapping::MemoryMapping(int mapFlag)
{
	switch(mapFlag)
	{
	case(MAP::CLUT):
	{
		InitializeMMap_CLUT();
		break;
	}
	case(MAP::OCMREG):
	{
		InitializeMMap_OCM();
		break;
	}
	case(MAP::REG):
	{
		InitializeMMAP_Register2();
		break;
	}
	case(MAP::SEQ):
	{
		InitializeMMap_SEQ();
		break;
	}
	case(MAP::TEC):
	{
		InitializeMMap_TEC();
		break;
	}
	case(MAP::OCMMEM):
	{
		InitializeMMap_OCMMEM();
		break;
	}
	case(MAP::GPIO):
	{
		InitializeMMap_GPIO();
		break;
	}

	default:
		printf("Driver <Memory Mapping> Unknowing mapping flag\n\n");
	}
}

int MemoryMapping::InitializeMMap_OCMMEM(void)
{
	if((fd = open("/dev/mem", O_RDWR | O_SYNC)) == -1)
	{
		printf("Error opening /dev/mem\n");
		return (-1); //error
	}

	if(MMAP_OCMMemory(fd) == -1)
		return (-1);

	return (0);
}

int MemoryMapping::InitializeMMAP_Register2(void)
{
	if((fd = open("/dev/mem", O_RDWR | O_SYNC)) == -1)
	{
		printf("Error opening /dev/mem\n");
		return (-1); //error
	}

	if(MMAP_Register2(fd) == -1)
		return (-1);

	return (0);
}
int MemoryMapping::InitializeMMap_CLUT(void)
{
	if((fd = open("/dev/mem", O_RDWR | O_SYNC)) == -1)
	{
		printf("Error opening /dev/mem\n");
		return (-1); //error
	}

	if(MMAP_CLUT(fd) == -1)
		return (-1);

	return (0);
}
int MemoryMapping::InitializeMMap_SEQ(void)
{
	if((fd = open("/dev/mem", O_RDWR | O_SYNC)) == -1)
	{
		printf("Error opening /dev/mem\n");
		return (-1); //error
	}

	if(MMAP_SEQ(fd) == -1)
		return (-1);

	return (0);
}
int MemoryMapping::InitializeMMap_OCM(void)
{
	if((fd = open("/dev/mem", O_RDWR | O_SYNC)) == -1)
	{
		printf("Error opening /dev/mem\n");
		return (-1); //error
	}

	if(MMAP_OCMRegister(fd) == -1)
		return (-1);

	return (0);
}
int MemoryMapping::InitializeMMap_TEC(void)
{
	if((fd = open("/dev/mem", O_RDWR | O_SYNC)) == -1)
	{
		printf("Error opening /dev/mem\n");
		return (-1); //error
	}

	if(MMAP_TECRegister(fd) == -1)
		return(-1);

	return (0);
}


int MemoryMapping::InitializeMMap_GPIO(void)
{
	if((fd = open("/dev/mem", O_RDWR | O_SYNC)) == -1)
	{
		printf("Error opening /dev/mem\n");
		return (-1); //error
	}

	if(MMAP_GPIO(fd) == -1)
		return(-1);

	return (0);
}

MemoryMapping::~MemoryMapping() {

	if(reg_mappedBase != NULL)
	{	printf("MemoryMapping DESCT regBase\n");
		if(munmap((void *)reg_mappedBase, MAP_SIZE) == -1)
		{
			printf("Can't unmap memory regBase\n");
		}
	}

	if(ocm_mappedBase != NULL)
	{	printf("MemoryMapping DESCT ocm_mappedBase\n");
		if(munmap((void *)ocm_mappedBase, MAP_SIZE) == -1)
		{
			printf("Can't unmap memory ocmBase\n");
		}
	}

	if(clut_mappedBase != NULL)
	{	printf("MemoryMapping DESCT clut_mappedBase\n");
		if(munmap((void *)clut_mappedBase, MAP_SIZE) == -1)
		{
			printf("Can't unmap memory clutBase\n");
		}
	}

	if(seq_mappedBase != NULL)
	{	printf("MemoryMapping DESCT seq_mappedBase\n");
		if(munmap((void *)seq_mappedBase, MAP_SIZE) == -1)
		{
			printf("Can't unmap memory seqBase\n");
		}
	}

	if(tec_mappedBase != NULL)
	{	printf("MemoryMapping DESCT tec_mappedBase\n");
		if(munmap((void *)tec_mappedBase, MAP_SIZE) == -1)
		{
			printf("Can't unmap memory tecBase\n");
		}
	}

	if(ocm_Memory != NULL)
	{	printf("MemoryMapping DESCT ocm_Memory\n");
		if(munmap((void *)ocm_Memory, HEC7020_OCM_TRANS_SIZE) == -1)
		{
			printf("Can't unmap memory ocm_Memory\n");
		}
	}

	if(fd > 0)
	{
		close(fd);
	}
}

void MemoryMapping::GetOCMMemoryRegion(void **memRegion)
{
	*memRegion = ocm_Memory;
}

int MemoryMapping::ReadRegister_Reg2(uint8_t addr, uint8_t *value)
{
	if (pthread_mutex_lock(&global_mutex[LOCK_REG2_REGISTERS]) != 0)
	{
		std::cout << "global_mutex[LOCK_REG2_REGISTERS] lock unsuccessful" << std::endl;
		return -1;
	}

	*value = regBase[addr];

	if (pthread_mutex_unlock(&global_mutex[LOCK_REG2_REGISTERS]) != 0)
	{
		std::cout << "global_mutex[LOCK_REG2_REGISTERS] unlock unsuccessful" << std::endl;
		return -1;
	}

	return (0);
}

int MemoryMapping::ReadRegistersPart_Reg2(uint8_t startAddr, uint8_t *data, int size)
{
	if (pthread_mutex_lock(&global_mutex[LOCK_REG2_REGISTERS]) != 0)
	{
		std::cout << "global_mutex[LOCK_REG2_REGISTERS] lock unsuccessful" << std::endl;
		return -1;
	}

	for (int i = 0; i < size; ++i)
		data[i] = regBase[startAddr + i];

	if (pthread_mutex_unlock(&global_mutex[LOCK_REG2_REGISTERS]) != 0)
	{
		std::cout << "global_mutex[LOCK_REG2_REGISTERS] unlock unsuccessful" << std::endl;
		return -1;
	}

	return (0);
}

int MemoryMapping::WriteRegister_Reg2(uint8_t addr, uint8_t value)
{
	if (pthread_mutex_lock(&global_mutex[LOCK_REG2_REGISTERS]) != 0)
	{
		std::cout << "global_mutex[LOCK_REG2_REGISTERS] lock unsuccessful" << std::endl;
		return -1;
	}

	regBase[addr] = value;

	if (pthread_mutex_unlock(&global_mutex[LOCK_REG2_REGISTERS]) != 0)
	{
		std::cout << "global_mutex[LOCK_REG2_REGISTERS] unlock unsuccessful" << std::endl;
		return -1;
	}

	return (0);
}

int MemoryMapping::WriteRegistersPart_Reg2(uint8_t startAddr, uint8_t *data, int size)
{
	if (pthread_mutex_lock(&global_mutex[LOCK_REG2_REGISTERS]) != 0)
	{
		std::cout << "global_mutex[LOCK_REG2_REGISTERS] lock unsuccessful" << std::endl;
		return -1;
	}

	for (int i = 0; i < size; ++i)
		regBase[startAddr + i] = data[i];

	if (pthread_mutex_unlock(&global_mutex[LOCK_REG2_REGISTERS]) != 0)
	{
		std::cout << "global_mutex[LOCK_REG2_REGISTERS] unlock unsuccessful" << std::endl;
		return -1;
	}

	return (0);
}

int MemoryMapping::ReadRegister_OCM(uint8_t addr, uint8_t *value)
{
	if (pthread_mutex_lock(&global_mutex[LOCK_OCM_REGISTERS]) != 0)
	{
		std::cout << "global_mutex[LOCK_OCM_REGISTERS] lock unsuccessful" << std::endl;
		return -1;
	}

	*value = ocmBase[addr];

	if (pthread_mutex_unlock(&global_mutex[LOCK_OCM_REGISTERS]) != 0)
	{
		std::cout << "global_mutex[LOCK_OCM_REGISTERS] unlock unsuccessful" << std::endl;
		return -1;
	}

	return (0);
}

int MemoryMapping::ReadRegister_OCM32(int addr, int *value)
{
	if (pthread_mutex_lock(&global_mutex[LOCK_OCM_REGISTERS]) != 0)
	{
		std::cout << "global_mutex[LOCK_OCM_REGISTERS] lock unsuccessful" << std::endl;
		return -1;
	}

	*value = ocmBase32[addr];

	if (pthread_mutex_unlock(&global_mutex[LOCK_OCM_REGISTERS]) != 0)
	{
		std::cout << "global_mutex[LOCK_OCM_REGISTERS] unlock unsuccessful" << std::endl;
		return -1;
	}

	return (0);
}

int MemoryMapping::ReadRegistersPart_OCM(uint8_t startAddr, uint8_t *data, int size)
{
	if (pthread_mutex_lock(&global_mutex[LOCK_OCM_REGISTERS]) != 0)
	{
		std::cout << "global_mutex[LOCK_OCM_REGISTERS] lock unsuccessful" << std::endl;
		return -1;
	}

	for (int i = 0; i < size; ++i)
		data[i] = ocmBase[startAddr + i];

	if (pthread_mutex_unlock(&global_mutex[LOCK_OCM_REGISTERS]) != 0)
	{
		std::cout << "global_mutex[LOCK_OCM_REGISTERS] unlock unsuccessful" << std::endl;
		return -1;
	}

	return (0);
}

int MemoryMapping::WriteRegister_OCM(uint8_t addr, uint8_t value)
{
	if (pthread_mutex_lock(&global_mutex[LOCK_OCM_REGISTERS]) != 0)
	{
		std::cout << "global_mutex[LOCK_OCM_REGISTERS] lock unsuccessful" << std::endl;
		return -1;
	}

	ocmBase[addr] = value;

	if (pthread_mutex_unlock(&global_mutex[LOCK_OCM_REGISTERS]) != 0)
	{
		std::cout << "global_mutex[LOCK_OCM_REGISTERS] unlock unsuccessful" << std::endl;
		return -1;
	}

	return (0);
}

int MemoryMapping::WriteRegister_OCM32(int addr, int value)
{
	if (pthread_mutex_lock(&global_mutex[LOCK_OCM_REGISTERS]) != 0)
	{
		std::cout << "global_mutex[LOCK_OCM_REGISTERS] lock unsuccessful" << std::endl;
		return -1;
	}

	ocmBase32[addr] = value;

	if (pthread_mutex_unlock(&global_mutex[LOCK_OCM_REGISTERS]) != 0)
	{
		std::cout << "global_mutex[LOCK_OCM_REGISTERS] unlock unsuccessful" << std::endl;
		return -1;
	}

	return (0);
}

int MemoryMapping::WriteRegistersPart_OCM(uint8_t startAddr, uint8_t *data, int size)
{
	if (pthread_mutex_lock(&global_mutex[LOCK_OCM_REGISTERS]) != 0)
	{
		std::cout << "global_mutex[LOCK_OCM_REGISTERS] lock unsuccessful" << std::endl;
		return -1;
	}

	for (int i = 0; i < size; ++i)
		ocmBase[startAddr + i] = data[i];

	if (pthread_mutex_unlock(&global_mutex[LOCK_OCM_REGISTERS]) != 0)
	{
		std::cout << "global_mutex[LOCK_OCM_REGISTERS] unlock unsuccessful" << std::endl;
		return -1;
	}

	return (0);
}

int MemoryMapping::ReadRegister_CLUT(uint8_t addr, uint8_t *value)
{
	if (pthread_mutex_lock(&global_mutex[LOCK_CLUT_REGISTERS]) != 0)
	{
		std::cout << "global_mutex[LOCK_CLUT_REGISTERS] lock unsuccessful" << std::endl;
		return -1;
	}

	*value = clutBase[addr];

	if (pthread_mutex_unlock(&global_mutex[LOCK_CLUT_REGISTERS]) != 0)
	{
		std::cout << "global_mutex[LOCK_CLUT_REGISTERS] unlock unsuccessful" << std::endl;
		return -1;
	}

	return (0);
}

int MemoryMapping::ReadRegistersPart_CLUT(uint8_t startAddr, uint8_t *data, int size)
{
	if (pthread_mutex_lock(&global_mutex[LOCK_CLUT_REGISTERS]) != 0)
	{
		std::cout << "global_mutex[LOCK_CLUT_REGISTERS] lock unsuccessful" << std::endl;
		return -1;
	}

	for (int i = 0; i < size; ++i)
		data[i] = clutBase[startAddr + i];

	if (pthread_mutex_unlock(&global_mutex[LOCK_CLUT_REGISTERS]) != 0)
	{
		std::cout << "global_mutex[LOCK_CLUT_REGISTERS] unlock unsuccessful" << std::endl;
		return -1;
	}

	return (0);
}

int MemoryMapping::WriteRegister_CLUT(uint8_t addr, uint8_t value)
{
	if (pthread_mutex_lock(&global_mutex[LOCK_CLUT_REGISTERS]) != 0)
	{
		std::cout << "global_mutex[LOCK_CLUT_REGISTERS] lock unsuccessful" << std::endl;
		return -1;
	}

	clutBase[addr] = value;

	if (pthread_mutex_unlock(&global_mutex[LOCK_CLUT_REGISTERS]) != 0)
	{
		std::cout << "global_mutex[LOCK_CLUT_REGISTERS] unlock unsuccessful" << std::endl;
		return -1;
	}

	return (0);
}

int MemoryMapping::WriteRegistersPart_CLUT(uint8_t startAddr, uint8_t *data, int size)
{
	if (pthread_mutex_lock(&global_mutex[LOCK_CLUT_REGISTERS]) != 0)
	{
		std::cout << "global_mutex[LOCK_CLUT_REGISTERS] lock unsuccessful" << std::endl;
		return -1;
	}

	for (int i = 0; i < size; ++i)
		clutBase[startAddr + i] = data[i];

	if (pthread_mutex_unlock(&global_mutex[LOCK_CLUT_REGISTERS]) != 0)
	{
		std::cout << "global_mutex[LOCK_CLUT_REGISTERS] unlock unsuccessful" << std::endl;
		return -1;
	}

	return (0);
}

int MemoryMapping::ReadRegister_SEQ(uint8_t addr, uint8_t *value)
{
	if (pthread_mutex_lock(&global_mutex[LOCK_SEQ_REGISTERS]) != 0)
	{
		std::cout << "global_mutex[LOCK_SEQ_REGISTERS] lock unsuccessful" << std::endl;
		return -1;
	}

	*value = seqBase[addr];

	if (pthread_mutex_unlock(&global_mutex[LOCK_SEQ_REGISTERS]) != 0)
	{
		std::cout << "global_mutex[LOCK_SEQ_REGISTERS] unlock unsuccessful" << std::endl;
		return -1;
	}

	return (0);
}

int MemoryMapping::ReadRegistersPart_SEQ(uint8_t startAddr, uint8_t *data, int size)
{
	if (pthread_mutex_lock(&global_mutex[LOCK_SEQ_REGISTERS]) != 0)
	{
		std::cout << "global_mutex[LOCK_SEQ_REGISTERS] lock unsuccessful" << std::endl;
		return -1;
	}

	for (int i = 0; i < size; ++i)
		data[i] = seqBase[startAddr + i];

	if (pthread_mutex_unlock(&global_mutex[LOCK_SEQ_REGISTERS]) != 0)
	{
		std::cout << "global_mutex[LOCK_SEQ_REGISTERS] unlock unsuccessful" << std::endl;
		return -1;
	}

	return (0);
}

int MemoryMapping::WriteRegister_SEQ(uint8_t addr, uint8_t value)
{
	if (pthread_mutex_lock(&global_mutex[LOCK_SEQ_REGISTERS]) != 0)
	{
		std::cout << "global_mutex[LOCK_SEQ_REGISTERS] lock unsuccessful" << std::endl;
		return -1;
	}

	seqBase[addr] = value;

	if (pthread_mutex_unlock(&global_mutex[LOCK_SEQ_REGISTERS]) != 0)
	{
		std::cout << "global_mutex[LOCK_SEQ_REGISTERS] unlock unsuccessful" << std::endl;
		return -1;
	}

	return (0);
}

int MemoryMapping::WriteRegistersPart_SEQ(uint8_t startAddr, uint8_t *data, int size)
{
	if (pthread_mutex_lock(&global_mutex[LOCK_SEQ_REGISTERS]) != 0)
	{
		std::cout << "global_mutex[LOCK_SEQ_REGISTERS] lock unsuccessful" << std::endl;
		return -1;
	}

	for (int i = 0; i < size; ++i)
		seqBase[startAddr + i] = data[i];

	if (pthread_mutex_unlock(&global_mutex[LOCK_SEQ_REGISTERS]) != 0)
	{
		std::cout << "global_mutex[LOCK_SEQ_REGISTERS] unlock unsuccessful" << std::endl;
		return -1;
	}

	return (0);
}

int MemoryMapping::ReadRegister_TEC32(int addr, unsigned int *value)
{
	if (pthread_mutex_lock(&global_mutex[LOCK_TEC_REGISTERS]) != 0)
	{
		std::cout << "global_mutex[LOCK_TEC_REGISTERS] lock unsuccessful" << std::endl;
		return -1;
	}

	*value = tecBase[addr];

	if (pthread_mutex_unlock(&global_mutex[LOCK_TEC_REGISTERS]) != 0)
	{
		std::cout << "global_mutex[LOCK_TEC_REGISTERS] unlock unsuccessful" << std::endl;
		return -1;
	}

	return (0);
}
int MemoryMapping::WriteRegister_TEC32(int addr, unsigned int value)
{
	if (pthread_mutex_lock(&global_mutex[LOCK_TEC_REGISTERS]) != 0)
	{
		std::cout << "global_mutex[LOCK_TEC_REGISTERS] lock unsuccessful" << std::endl;
		return -1;
	}

	tecBase[addr] = value;

	if (pthread_mutex_unlock(&global_mutex[LOCK_TEC_REGISTERS]) != 0)
	{
		std::cout << "global_mutex[LOCK_TEC_REGISTERS] unlock unsuccessful" << std::endl;
		return -1;
	}

	return (0);
}

int MemoryMapping::ReadRegister_GPIO(int addr, unsigned int *value)
{
	*value = gpioBase[addr];
	return (0);
}
int MemoryMapping::WriteRegister_GPIO(int addr, unsigned int value)
{
	gpioBase[addr] = value;
	return (0);
}

int MemoryMapping::MMAP_Register2(int fd)
{
	reg_mappedBase = mmap(0, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, REGISTER2_BASEADDR & ~MAP_MASK);

	if(reg_mappedBase == MAP_FAILED)
	{
		printf("mmap failed\n");
		return -1;
	}
	else
	{
		regBase = ((volatile unsigned char *)reg_mappedBase + (REGISTER2_BASEADDR & MAP_MASK));
	}

	return (0);
}

int MemoryMapping::MMAP_CLUT(int fd)
{
	clut_mappedBase = mmap(0, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, CLUT_BASEADDR & ~MAP_MASK);

	if(clut_mappedBase == MAP_FAILED)
	{
		printf("mmap failed\n");
		return -1;
	}
	else
	{
		clutBase = ((volatile unsigned char *)clut_mappedBase + (CLUT_BASEADDR & MAP_MASK));
	}

	return (0);
}

int MemoryMapping::MMAP_SEQ(int fd)
{
	seq_mappedBase = mmap(0, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, SEQ_BASEADDR & ~MAP_MASK);

	if(seq_mappedBase == MAP_FAILED)
	{
		printf("mmap failed\n");
		return -1;
	}
	else
	{
		seqBase = ((volatile unsigned char *)seq_mappedBase + (SEQ_BASEADDR & MAP_MASK));
	}

	return (0);
}

int MemoryMapping::MMAP_OCMRegister(int fd)
{
	ocm_mappedBase = mmap(0, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, OCM_TRANS_BASEADDR & ~MAP_MASK);

	if(ocm_mappedBase == MAP_FAILED)
	{
		printf("mmap failed\n");
		return -1;
	}
	else
	{
		ocmBase = ((volatile unsigned char *)ocm_mappedBase + (OCM_TRANS_BASEADDR & MAP_MASK));
		ocmBase32 = ((volatile unsigned long *)ocm_mappedBase + (OCM_TRANS_BASEADDR & MAP_MASK));
	}

	return (0);
}

int MemoryMapping::MMAP_TECRegister(int fd)
{
	tec_mappedBase = mmap(0, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, TEC_BASEADDR & ~MAP_MASK);

	if(tec_mappedBase == MAP_FAILED)
	{
		printf("mmap failed\n");
		return -1;
	}
	else
	{
		tecBase = ((volatile unsigned long *)tec_mappedBase + (TEC_BASEADDR & MAP_MASK));
	}

	return (0);
}


int MemoryMapping::MMAP_GPIO(int fd)
{
	gpio_mappedBase = mmap(0, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, GPIO_BASEADDR & ~MAP_MASK);

	if(gpio_mappedBase == MAP_FAILED)
	{
		printf("gpio_mappedBase mmap failed\n");
		return -1;
	}
	else
	{
		gpioBase = ((volatile unsigned long *)gpio_mappedBase + (GPIO_BASEADDR & MAP_MASK));
	}

	return (0);
}

int MemoryMapping::MMAP_OCMMemory(int fd)
{	// can try setting to MAP_PRIVATE dont share with other processes the region
	ocm_Memory = mmap(0, HEC7020_OCM_TRANS_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, HEC7020_OCM_LOC);

	if(ocm_Memory == MAP_FAILED)
	{
		printf("ocm_Memory mmap failed\n");
		return -1;
	}

	std::cout << "MemoryMapping ocm_Memory = " << ocm_Memory << std::endl;

	return (0);
}


