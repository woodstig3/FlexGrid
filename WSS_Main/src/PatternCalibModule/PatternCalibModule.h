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
#include "SerialModule.h"
#include "ParametersStructure.h"

//#define _DEBUG_

extern bool b_LoopOn;												// Loop running on threads

#define LUT_OPT_FREQ_NUM 8
#define LUT_ATT_FREQ_NUM  8
#define LUT_SIGMA_FREQ_NUM 14
#define LUT_PIXELPOS_FREQ_NUM 27
#define LUT_ATT_ATT_NUM 7       //so accordingly when loading att_lut,rownumber need to be modified > 12 which is the start of next port
#define LUT_SIGMA_TEMP_NUM 6    //so accordingly when loading sigma_lut, columnnumber needs to be modified <=7
#define LUT_PIXELPOS_TEMP_NUM 6
#define PORT_NUM 22

struct Opt{
	// Optimized Aopt/Kopt vs Freq vs Port
	double 			Freq[LUT_OPT_FREQ_NUM];								// Independent variables
	double 			Aopt[PORT_NUM][LUT_OPT_FREQ_NUM];
	double 			Kopt[PORT_NUM][LUT_OPT_FREQ_NUM];
};

struct Att{
	// Attenuated Aatt/Katt vs Freq vs ATT vs Port
	double 			Freq[LUT_ATT_FREQ_NUM];								// Independent variables
	double 			ATT[LUT_ATT_ATT_NUM];								// Independent variables
	double 			Aatt[PORT_NUM];
	double 			Katt[PORT_NUM][LUT_ATT_FREQ_NUM][LUT_ATT_ATT_NUM];
};

struct Sigma{
	// Sigma vs Freq vs Temperature vs Port
	double 			Temp[LUT_SIGMA_TEMP_NUM];							// Independent variables
	double 			Freq[LUT_SIGMA_FREQ_NUM];							// Independent variables
	double 			Sigma[PORT_NUM][LUT_SIGMA_FREQ_NUM][LUT_SIGMA_TEMP_NUM];
};

struct PixelPos{
	// Pixel Positio vs Freq vs Temperature
	double 			Temp[LUT_PIXELPOS_TEMP_NUM];						// Independent variables
	double 			Freq[LUT_PIXELPOS_FREQ_NUM];						// Independent variables
	double 			Pos[LUT_PIXELPOS_FREQ_NUM][LUT_PIXELPOS_TEMP_NUM];
};

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
	Sigma_ThreadArgs     Sigma_params;
	Pixel_Pos_ThreadArgs Pixel_Pos_params;

	enum InterpolationStatus{ERROR = -1, SUCCESS = 0};
	InterpolationStatus g_Status_Opt, g_Status_Att, g_Status_Sigma, g_Status_PixelPos;	// If any interpolation has an error it will turn -1

	// Thread arguments set by Pattern Generation Module
	int 			Set_Aopt_Kopt_Args(int port, double freq);
	int 			Set_Aatt_Katt_Args(int port, double freq, double ATT);
	int 			Set_Sigma_Args(int port, double freq, double temperature, int cmp);
	int 			Set_Pixel_Pos_Args(double f1, double f2, double fc, double temperature);
	void			Set_Current_Module(int moduleNo);
	int 			Get_Interpolation_Status();		// Return -1 for error otherwise 0
	int 			Get_LUT_Load_Status();



private:

	static PatternCalibModule *pinstance_;
	bool 			m_bCalibDataOk = false;

	bool 			b_LoopOn;
	pthread_t 		thread1_id{0}, thread2_id{0}, thread3_id{0}, thread4_id{0};										// Create Thread id
	pthread_attr_t 	thread_attrb;								// Create Attributes


	/* Module 1 DS */

	Opt				M1_lut_Opt;
	Att				M1_lut_Att;
	Sigma			M1_lut_Sigma;
	Sigma			M1_lut_SigmaL;  //drc added for L port
	PixelPos		M1_lut_PixelPos;

	/* Module 2 DS */

	Opt				M2_lut_Opt;
	Att				M2_lut_Att;
	Sigma			M2_lut_Sigma;
	Sigma			M2_lut_SigmaL;  //drc added for L port
	PixelPos		M2_lut_PixelPos;

	/* Active Module */
	Opt*			lut_Opt{nullptr};
	Att*			lut_Att{nullptr};
	Sigma*			lut_Sigma{nullptr};
	Sigma*			lut_SigmaL{nullptr};	  //drc added for L port
	PixelPos*		lut_PixelPos{nullptr};

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
	int 			Interpolate_Sigma_Bilinear(double temperature, double frequency, unsigned int portNum, int cmp, double &result_sigma);

	void 			Calculation_Pixel_Shift();	// For a given Freq and Temperature, find Pixel value for the channel position (Related to GRISM freq shift)
	int 			Interpolate_PixelPos_Bilinear(double temperature, double frequency, double& result_pixelPos);

	int 			BinarySearch_LowIndex(const double *array, int size, double target, int &index);

	int 			PatternCalib_Initialize(void);
	void 			PatternCalib_Closure(void);

	int 			PatternCalib_LoadLUTs(void);
	int 			Load_Opt_LUT(Opt& lut, const std::string& path);
	int 			Load_Att_LUT(Att& lut, const std::string& path);
	int 			Load_Sigma_LUT(Sigma& lut, const std::string& path);
	int 			Load_PixelPos_LUT(PixelPos& lut, const std::string& path);
};

#endif /* SRC_PATTERNCALIBMODULE_PATTERNCALIBMODULE_H_ */
