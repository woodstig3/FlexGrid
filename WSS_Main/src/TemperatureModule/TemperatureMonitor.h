/*
 * TemperatureMonitor.h
 *
 *  Created on: Mar 9, 2023
 *      Author: Administrator
 */

#ifndef SRC_TEMPERATUREMODULE_TEMPERATUREMONITOR_H_
#define SRC_TEMPERATUREMODULE_TEMPERATUREMONITOR_H_

#include <iostream>
#include <math.h>
#include <cmath>		//cmath.h for fmod()
#include <algorithm>
#include <pthread.h>
#include <fstream>
#include <sstream>
#include <cstring>	// for memset and strerror
#include <unistd.h>
#include <vector>

#include "GlobalVariables.h"
#include "SerialModule.h"
#include "InterfaceModule/MemoryMapping.h"

constexpr int WINDOW_SIZE = 5;					// Define the window size for the SMA moving average filter
constexpr int LCOS_MIN_TEMP_HEX = 0xC4C;		// Hex is based on LUT table defined by us
constexpr int LCOS_MAX_TEMP_HEX = 0xD16;		// Hex is based on LUT table defined by us
constexpr int HEATER_MAX_TEMP_HEX = 0xCB7;		// Hex is based on LUT table > 65C given by ZTE
constexpr int HEATER_MIN_TEMP_HEX = 0xA1B;		// Hex is based on LUT table < 40C max given by ZTE
constexpr int LUT_MIN_HEX = 0xC;				// Hex is based on LUT table
constexpr int LUT_MAX_HEX = 0x6A5;				// Hex is based on LUT table

#define LCOS_OPERATING_TEMP_MIN 60
#define LCOS_OPERATING_TEMP_MAX 67
#define HEATER_OPERATING_TEMP_MIN 50
#define HEATER_OPERATING_TEMP_MAX 56

constexpr int TEC1_PERIOD_ADDR = 0x00d8;
constexpr int TEC2_PERIOD_ADDR = 0x00E4;

constexpr int HEATER1_START_ADDR = 0x00d4;
constexpr int HEATER2_START_ADDR = 0x00E0;

constexpr int SAMPLING_TIMER_ADDR = 0x0190;
constexpr int TARGET_TEMP_ADDR = 0x0194;
constexpr int LCOS_TEMP_TARGET = 0x157C;		// 65.43C = 0x198F; 55.00C = 0x157C

constexpr int KP_PID1_ADDR = 0x0198;
constexpr int KI_PID1_ADDR = 0x019C;
constexpr int KD_PID1_ADDR = 0x0A00;

constexpr int KP_PID1_VAL = 0x168;				 // Multiply float value by 100 i.e 3.6*100 = 360 = 0x168
constexpr int KI_PID1_VAL = 0x0;				 // Multiply float value by 100 i.e 3.6*100 = 360 = 0x168
constexpr int KD_PID1_VAL = 0x168;

constexpr int KP_PID2_ADDR = 0x0A08;
constexpr int KI_PID2_ADDR = 0x0A0C;
constexpr int KD_PID2_ADDR = 0x0A10;

constexpr int KP_PID2_VAL = 0x5DC;
constexpr int KI_PID2_VAL = 0x1;
constexpr int KD_PID2_VAL = 0x118;

constexpr int LCOS_TEMP_ADDR = 0x013c;
constexpr int HEATER1_TEMP_ADDR = 0x0120;
constexpr int HEATER2_TEMP_ADDR = 0x0134;

constexpr int FPGA_TEC_STABLE_SIGNAL = 0x0A14;

constexpr int TEMP_INTERRUPT_GARTM = 0xA0;
constexpr int TEMP_INTERRUPT_GARTH = 0xA4;
constexpr int TEMP_INTERRUPT_LCOSM = 0xA8;
constexpr int TEMP_INTERRUPT_LCOSH = 0xAC;

class TemperatureMonitor {
protected:

	TemperatureMonitor();
	virtual ~TemperatureMonitor();

public:
    /**
     * Singletons should not be cloneable.
     */
					TemperatureMonitor(TemperatureMonitor &other) = delete;

	/**
     * Singletons should not be assignable.
     */
    void 			operator=(const TemperatureMonitor &) = delete;

    /**
     * This is the static method that controls the access to the singleton
     * instance. On the first run, it creates a singleton object and places it
     * into the static field. On subsequent runs, it returns the client existing
     * object stored in the static field.
     */
    static TemperatureMonitor *GetInstance();

	int 			MoveToThread();
	void 			StopThread();
	double 			GetLCOSTemperature(void);

private:

	enum 			Sensors{lCOS, HEATER1, HEATER2};

	enum 			ConversionStatus {NORMAL_LUT_RANGE, BELOW_LUT_RANGE, ABOVE_LUT_RANGE};

	static TemperatureMonitor *pinstance_;

	MemoryMapping 	*mmapTEC;

	SerialModule 	*g_serialMod;

	bool 			m_bTempLUTOk = false;

	pthread_t 		thread_id{0};										// Create Thread id
	pthread_attr_t 	thread_attrb;								// Create Attributes

	bool 			b_LoopOn;												// Loop running on thread

	bool 			m_testRigTemperatureSwitch = 0;

	std::vector<double> LUT;									// LUT to store temperature readings for a fixed range of input from FPGA.
	/* Require separate LUTs For heater sensors 2023-11-1 */
	//std::vector<double> LUT_heater;

	unsigned int 	g_startHexValue;
	std::vector<double> OLDLUT;						// LUT to store temperature readings for a fixed range of input from FPGA.
	unsigned int 	g_oldstartHexValue;
	bool 			b_isTECStable;
	bool 			initial_cond_reached_LCOS = false;
	bool 			initial_cond_reached_Heater1 = false;
	bool 			initial_cond_reached_Heater2 = false;
    double 			previousTemp_LCOS = 0;
    double 			previousTemp_GRID = 0;

    //Cpu temperature
	float 			in_temp0_offset, in_temp0_scale;
	bool 			m_bFilesOk{false};

private:

	int 			BreakThreadLoop(void);						// Control when to end all thread loops, usually when class dies
	void 			TemperatureMonitor_Closure(void);
	static void 	*ThreadHandle(void *);
	void 			ProcessTemperatureMonitoring(void);								// Thread looping while in the function
	int 			TemperatureMon_Initialize(void);
	int 			ReadTemperature(double *temp, int sensor);
	double 			ConvertToCelsius(unsigned int hexTemp);
	double          ConvertToOldCelsius(unsigned int hexTemp);
	int 			Load_TempSensor_LUT(void);
	int             Load_OldTempSensor_LUT(void);
	int 			Config_FPGA_TEC(void);									// Setup FPGA registers according to Fpga engineer guide to enable TEC and temperature reading
	void 			ProcessLCOS(double& filteredTemp, double buf[], int *buf_index, double& currentTemp);
	void 			ProcessHeater1(double& filteredTemp, double buf[], int *buf_index, double& currentTemp);
	void 			ProcessHeater2(double& filteredTemp, double buf[], int *buf_index, double& currentTemp);

	bool 			ReturnWhenTECStable();
	bool 			isTECStableInFPGA();
	bool 			Check_Need_For_TEC_Data_Transfer_To_PC();

	int 			SetTargetTemp(unsigned int);
	int 			SetPID1_Kp(unsigned int p);
	int 			SetPID1_Ki(unsigned int i);
	int 			SetPID1_Kd(unsigned int d);
	int 			SetPID2_Kp(unsigned int p);
	int 			SetPID2_Ki(unsigned int i);
	int 			SetPID2_Kd(unsigned int d);
	int 			SetHeaters(bool state);

	// For Simple moving average filter
	double 			SMA_Filter(double buf[], int *buf_index, double currentTemp);		// Outputs the filtered temperature

	int 			EWMA_Filter(void);

	void 			GetZYNQTempVars(void);
	float 			CpuTemp();

};

#endif /* SRC_TEMPERATUREMODULE_TEMPERATUREMONITOR_H_ */
