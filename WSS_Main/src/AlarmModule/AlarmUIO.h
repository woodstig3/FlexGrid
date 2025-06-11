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
#include <iomanip>

#include "GlobalVariables.h"
#include "SpiCmdDecoder.h"
#include "MemoryMapping.h"
#include "Dlog.h"

#define UIO_0 66
#define UIO_1 67
#define UIO_2 68
#define UIO_3 84

#define TEST_LEN 4 //3
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
	int UIO_GRID_Temp_H;
	int UIO_LCOS_Temp_H;
	int UIO_Hard_Reset;
	int UIO_Pattern_Received;
	int UIO_EEPROM_Load_Ready;
	int UIO_GRID_Temp_L;
	int UIO_LCOS_Temp_L;

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
	//void            ProcessUIODevice(int fd, int& hisCon, bool& deFlag, FaultsName logName);
	void 			ProcessUIOAlarmMonitoring(void);
	void            GpioWrite(int fd, char level);

	void            CheckADCPowerSupply();
	void            CheckDACPowerSupply();
	void 			CheckVCC3A();
	void			CheckVDP1V8();
	void 			CheckDACOUTD_ADC();
	void 			WriteVoltageToFile(const std::string& voltageName, double voltage);
private:
	FaultsAttr m_heater1Temp;
	FaultsAttr m_heater2Temp;
	FaultsAttr m_tecTemp;
	FaultsAttr m_adcAccessFailure;
	FaultsAttr m_dacAccessFailure;
	FaultsAttr m_wssAccessFailure;
	MemoryMapping 	*mmapTEC;
	MemoryMapping   *mmapGPIO;


	void            HardReset();
};

#endif
