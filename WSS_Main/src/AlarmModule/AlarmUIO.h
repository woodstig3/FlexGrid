#ifndef SRC_ALARMMODULE_ALARMUIO_H_
#define SRC_ALARMMODULE_ALARMUIO_H_

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <string>
#include <iostream>
#include <sstream>

#include "GlobalVariables.h"
#include "SpiCmdDecoder.h"
#include "MemoryMapping.h"

#define UIO_0 66
#define UIO_1 67
#define UIO_2 68
#define UIO_3 84

#define TEST_LEN 4
#define GPIO_WRR 913

constexpr int ADC_REG_ADDR = 0x0100;
constexpr int DAC_REG_ADDR = 0x011C;
constexpr double DAC_REF_VOLTAGE = 2.048;
constexpr double ADC_REF_VOLTAGE = 2.5;
constexpr uint16_t ADC_DATA_MASK = 0x0FFF;

using namespace std;

class AlarmModule
{

public:
	AlarmModule();
	virtual ~AlarmModule();

	int UIO_DAC_OA;
	int UIO_DAC_OD;
	int UIO_GRID_Temp;
	int UIO_LCOS_Temp;
	int UIO_LCOS_Ready;

	int HisCon_OA;
	int HisCon_OD;
	int HisCon_GRIDTemp;
	int HisCon_LCOSTemp;

	bool DeOA_Flag;
	bool DeOD_Flag;
	bool DeGRID_Flag;
	bool DeLCOS_Flag;

	int GPIO_valuefd;
	int GPIO_exportfd;
	int GPIO_directionfd;

	static AlarmModule *GetInstance();
	static AlarmModule *pinstance_;
	pthread_t 		thread_id{0};							    // Create Thread id
	pthread_attr_t 	thread_attrb;								// Create Attributes


	int 			MoveToThread();
	void 			StopThread();
	static void 	*ThreadHandle(void *);
	void            ProcessUIODevice(int fd, int& hisCon, bool& deFlag, FaultsName logName);
	void 			ProcessUIOAlarmMonitoring(void);
	void            GpioWrite(int fd, char level);
private:
	FaultsAttr UIOMess{0};
	MemoryMapping 	*mmapTEC;

	void            checkADCPowerSupply();
	void            checkDACPowerSupply();
};

#endif
