/*
 * PatternCalibModule.h
 *
 *  Created on: Feb 3, 2023
 *      Author: mib_n
 */

#ifndef SRC_PATTERNCALIBMODULE_PATTERNCALIBMODULE_H_
#define SRC_PATTERNCALIBMODULE_PATTERNCALIBMODULE_H_

#include <iostream>
#include <pthread.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "ParametersStructure.h"
#include "SerialModule.h"

extern bool b_LoopOn;												// Loop running on threads

#define LUT_OPT_FREQ_NUM 6
#define LUT_ATT_FREQ_NUM 6
#define LUT_SIGMA_FREQ_NUM 6
#define LUT_PIXELPOS_FREQ_NUM 11
#define LUT_ATT_ATT_NUM 6
#define LUT_SIGMA_TEMP_NUM 3
#define LUT_PIXELPOS_TEMP_NUM 9
#define PORT_NUM 23

class PatternCalibModule{
protected:

					PatternCalibModule();
	virtual 		~PatternCalibModule();

public:

	/**
     * Singletons should not be cloneable.
     */
					PatternCalibModule(PatternCalibModule &other) = delete;

	/**
     * Singletons should not be assignable.
     */
	void 			operator=(const PatternCalibModule &) = delete;

    /**
     * This is the static method that controls the access to the singleton
     * instance. On the first run, it creates a singleton object and places it
     * into the static field. On subsequent runs, it returns the client existing
     * object stored in the static field.
     */
    static PatternCalibModule *GetInstance();

	SerialModule*	g_serialMod;

	int 			MoveToThread();
	void 			StopThread();

	// Input structure defined. Pattern Gen can modify the values for calculations
	Aopt_Kopt_ThreadArgs Aopt_Kopt_params;
	Aatt_Katt_ThreadArgs Aatt_Katt_params;
	Sigma_ThreadArgs Sigma_params;
	Pixel_Pos_ThreadArgs Pixel_Pos_params;

	enum 			InterpolationStatus{ERROR = -1, SUCCESS = 0};
	InterpolationStatus g_Status_Opt, g_Status_Att, g_Status_Sigma, g_Status_PixelPos = SUCCESS;		// If any interpolation has an error it will turn -1

	// Thread arguments set by Pattern Generatio Module
	int 			Set_Aopt_Kopt_Args(int port, double freq);
	int 			Set_Aatt_Katt_Args(int port, double freq, double ATT);
	int 			Set_Sigma_Args(int port, double freq, double temperature);
	int 			Set_Pixel_Pos_Args(double f1, double f2, double fc, double temperature);

	int 			Get_Interpolation_Status();		// Return -1 for error otherwise 0
	int 			Get_LUT_Load_Status();


private:

	static PatternCalibModule *pinstance_;
	bool 			m_bCalibDataOk = false;

	/* Look-up Tables and there independent variables*/

	// Optimized Aopt/Kopt vs Freq vs Port

	double 			lut_Opt_Freq[LUT_OPT_FREQ_NUM];								// Independent variables
	double 			lut_Opt_Aopt[PORT_NUM][LUT_OPT_FREQ_NUM];
	double 			lut_Opt_Kopt[PORT_NUM][LUT_OPT_FREQ_NUM];

	// Attenuated Aatt/Katt vs Freq vs ATT vs Port

	double 			lut_Att_Freq[LUT_ATT_FREQ_NUM];								// Independent variables
	double 			lut_Att_ATT[LUT_ATT_ATT_NUM];								// Independent variables
	double 			lut_Att_Aatt[PORT_NUM];
	double 			lut_Att_Katt[PORT_NUM][LUT_ATT_FREQ_NUM][LUT_ATT_ATT_NUM];

	// Sigma vs Freq vs Temperature vs Port

	double 			lut_Sigma_Temp[LUT_SIGMA_TEMP_NUM];							// Independent variables
	double 			lut_Sigma_Freq[LUT_SIGMA_FREQ_NUM];							// Independent variables
	double 			lut_Sigma_Sigma[PORT_NUM][LUT_SIGMA_FREQ_NUM][LUT_SIGMA_TEMP_NUM];

	// Pixel Positio vs Freq vs Temperature

	double 			lut_PixelPos_Temp[LUT_PIXELPOS_TEMP_NUM];					// Independent variables
	double 			lut_PixelPos_Freq[LUT_PIXELPOS_FREQ_NUM];					// Independent variables
	double 			lut_PixelPos_Pos[LUT_PIXELPOS_FREQ_NUM][LUT_PIXELPOS_TEMP_NUM];

	pthread_t 		thread1_id{0}, thread2_id{0}, thread3_id{0}, thread4_id{0};										// Create Thread id
	pthread_attr_t 	thread_attrb;								// Create Attributes

	bool 			b_LoopOn;

private:

	static void 	*ThreadHandle1(void *);
	static void 	*ThreadHandle2(void *);
	static void 	*ThreadHandle3(void *);
	static void 	*ThreadHandle4(void *);

	int 			BreakThreadLoops();				// Control when to end all thread loops, usually when class dies

	void 			Calculation_Aopt_Kopt();		// For a given Frequency and Port find Optimized A and K values
	int 			Interpolate_Aopt_Kopt_Linear(double frequency, int port, double &result_Aopt, double &result_Kopt);

	void 			Calculation_Aatt_Katt();		// For a given Frequency, Port and ATT value, find Attenuate A and K values
	int 			Interpolate_Aatt_Katt_Bilinear(double frequency, int port, double Attenuation, double &result_Aatt, double &result_Katt);

	void 			Calculation_Sigma();			// For a given Freq, Port, Temperature, find SIGMA value
	int 			Interpolate_Sigma_Bilinear(double temperature, double frequency, unsigned int portNum, double &result_sigma);

	void 			Calculation_Pixel_Shift();	// For a given Freq and Temperature, find Pixel value for the channel position (Related to GRISM freq shift)
	int 			Interpolate_PixelPos_Bilinear(double temperature, double frequency, double& result_pixelPos);

	int 			BinarySearch_LowIndex(const double *array, int size, double target, int &index);

	int 			PatternCalib_Initialize(void);
	void 			PatternCalib_Closure(void);

	int 			PatternCalib_LoadLUTs(void);
	int 			Load_Opt_LUT(void);
	int 			Load_Att_LUT(void);
	int 			Load_Sigma_LUT(void);
	int 			Load_PixelPos_LUT(void);
};

#endif /* SRC_PATTERNCALIBMODULE_PATTERNCALIBMODULE_H_ */
