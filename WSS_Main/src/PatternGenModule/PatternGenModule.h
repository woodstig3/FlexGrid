/*
 * PatternGenModule.h
 *
 *  Created on: Jan 31, 2023
 *      Author: mib_n
 */

#ifndef SRC_PATTERNGENMODULE_PATTERNGENMODULE_H_
#define SRC_PATTERNGENMODULE_PATTERNGENMODULE_H_

#include <iostream>
#include <math.h>
#include <cmath>		//cmath.h for fmod()
#include <algorithm>
#include <pthread.h>
#include <fstream>
#include <cstring>	// for memset and strerror

#include "SerialModule.h"
#include "PatternCalibModule.h"
#include "GlobalVariables.h"
#include "TemperatureMonitor.h"
#include "InterfaceModule/OCMTransfer.h"

struct inputParameters{
	float 			ch_att;
	double 			ch_fc;
	double 			ch_f1;
	double 			ch_f2;
	int 			ch_adp;
};

struct outputParameters{
	float 			sigma;
	float 			Aopt;
	float 			Kopt;
	float 			Aatt;
	float 			Katt;

	float 			F1_PixelPos; //drc modified
	float 			F2_PixelPos;
	float 			FC_PixelPos;

};

struct Background_DS_For_Pattern{

	float SIGMA;
	float PD = 2.2;
	float A_OPP = 0.5;
	float K_OPP;
	float A_ATT;
	float K_ATT;
	float LAMDA = 1.55;
	double FC_PixelPos = 960.5;
	double F1_PixelPos = 191125.0;
	double F2_PixelPos = 196275.0;
};

class PatternGenModule {
protected:
					PatternGenModule();
	virtual 		~PatternGenModule();

public:
	/**
     * Singletons should not be cloneable.
     */
					PatternGenModule(PatternGenModule &other) = delete;

	/**
     * Singletons should not be assignable.
     */
	void 			operator=(const PatternGenModule &) = delete;

    /**
     * This is the static method that controls the access to the singleton
     * instance. On the first run, it creates a singleton object and places it
     * into the static field. On subsequent runs, it returns the client existing
     * object stored in the static field.
     */
    static PatternGenModule *GetInstance();

	SerialModule 	   *g_serialMod{nullptr};		// create an instance of CmdDecoder so that we can access its member and data via singleton method
	PatternCalibModule *g_patternCalib{nullptr};
	TemperatureMonitor *g_tempMonitor{nullptr};

	int 			MoveToThread();
	void 			StopThread();

	enum 			OperationMode {DEVELOPMENT, PRODUCTION};
	enum 			ErrorMessage {NO_ERROR, INTERPOLATION_FAILURE, PATTERN_CALC_FAILURE, PATTERN_RELOCATE_FAILURE, CALIB_FILES_NOTOK};
	enum 			PatternOutcome {SUCCESS = 0, FAILED, NO_OPERATION};
	ErrorMessage 	g_errorMsg = NO_ERROR;

	unsigned char 	*channelColumnData = new unsigned char[g_Total_Channels*g_LCOS_Height]();       // channelColumnData hold only 1 pixel width data value for each channel
	unsigned char 	*fullPatternData = new unsigned char[g_LCOS_Width*g_LCOS_Height]();		       // fullPatternData hold complete picture of all channels with their widths. This is used by OCM!
	unsigned char 	rotated[g_LCOS_Width*g_LCOS_Height];
	double 			rotationAngle{};
	unsigned char 	RotatedSquare[2160][4320]{}; // double the resolution
	unsigned char   BackgroundColumnData[g_LCOS_Height]; //drc added to store background grating gray scale value
	Background_DS_For_Pattern  Module_Background_DS_For_Pattern[3];  //drc added for 2 module background configuration, index start from 1 not 0


private:

	static PatternGenModule *pinstance_;

	std::vector<int> linearLUT{};									// The range depends on Phase_depth 2pi or 2.2pi etc
	unsigned int 	startOffsetLUT{0};								// These offsets can modify the range of LUT available to compare
	unsigned int 	endOffsetLUT{0};								// These offsets can modify the range of LUT available to compare



	bool 			m_bCalibDataOk = true;							// If calibration data has no issue then pattern will perform calculations otherwise no calculations

#ifdef _TWIN_WSS_
	int 			g_moduleNum{2};
#else
	int 			g_moduleNum{1};
#endif
	float 			g_LCOS_Temp{63};								// Default minimum
	bool 			g_bSigmaNegative{false};						// Flip pattern per period for -ve sigma

	const double 	g_wavelength{1.55};
	const int 		g_pixelSize{8};
	double 			g_phaseDepth{2.2};
	int				m_backColor{0};
	int				m_customLCOS_Height{g_LCOS_Height};

	float 			calculatedPeriod{0};
	float 			phaseLine[g_LCOS_Height]{0};
	float 			phaseLine_MOD[g_LCOS_Height]{0};
	float 			rebuildPeriod[g_LCOS_Height]{0};
	float 			attenuatedPattern[g_LCOS_Height]{0};
	float 			attenuatedPattern_limited[g_LCOS_Height]{0};	// The max and min values are limited, the border values are cut off
	float 			optimizedPattern[g_LCOS_Height]{0};
	unsigned int 	periodCount[g_LCOS_Height]{0};
	std::vector<unsigned int> periods{};
	int 			factorsForDiv[g_LCOS_Height]{0};

	pthread_t 		thread_id{0};									// Create Thread id
	pthread_attr_t 	thread_attrb{};									// Create Attributes

	bool 			b_LoopOn{};										// Loop running on thread

private:

	int 			BreakThreadLoop(void);							// Control when to end all thread loops, usually when class dies
	void 			GetErrorMessage(std::string &);

	int 			PatternGen_Initialize(void);
	void 			Create_Linear_LUT(float phaseDepth);
	void 			PatternGen_Closure(void);
	static void 	*ThreadHandle(void *);
	void 			ProcessPatternGeneration(void);					// Thread looping while in the function

	int 			Init_PatternGen_All_Modules(int *mode);			// Needs to know which mode to initiate pattern generation for
	void 			Save_Pattern_In_FileSysten(void);
	void 			Find_OperationMode(int *mode);
	int 			Get_LCOS_Temperature(void);
	int 			Find_Parameters_By_Interpolation(inputParameters &, outputParameters &, bool interpolateSigma, bool interpolateOpt, bool interpolateAtt, bool interpolatePixelPos);

	int 			Check_Need_For_GlobalParameterUpdate();
	int 			Calculate_Every_ChannelPattern(char);
	int 			Calculate_Every_ChannelPattern_DevelopMode(char);
	void 			Find_LinearPixelPos_DevelopMode(double &freq, int &pixelPos);

	int 			Calculate_Pattern_Formulas(const int ch, const float lamda, const int pixelSize, float sigmaRad, const float Aopt, const float Kopt, const float Aatt,const float Katt);
	int 			Calculate_PhaseLine(const float pixelSize, float sigmaRad, const float lamda);
	void 			Calculate_Period(const float pixelSize, float sigmaRad, const float lamda);
	void 			Calculate_Mod_And_RebuildPeriod(unsigned int periodCount[], int factorsForDiv[]);
	void 			Calculate_Optimization_And_Attenuation(const float Aopt, const float Kopt, const float Aatt,const float Katt);
	void 			Fill_Channel_ColumnData(unsigned int ch);
	void 			RelocateChannel(unsigned int chNum, unsigned int f1_PixelPos, unsigned int f2_PixelPos, unsigned int fc_PixelPos);
	void 			RelocateSlot(unsigned int chNum, unsigned int slotNum, unsigned int totalSlots, unsigned int f1_PixelPos, unsigned int f2_PixelPos);

	void 			RotateChannel(int x1, int y1, int x2, int y2, int x3, int y3, int x4, int y4, double angleRad, int centerX, int centerY);
	bool 			isInsideRectangle(double x, double y, double x1, double y1, double x2, double y2, double x3, double y3, double x4, double y4);
	void 			rotateArray(double angle, int width, int height);

	void 			getStartEndOffset(int startGrayVal, int endGrayVal);
	void			loadPatternFile_Bin(std::string path);				// send pattern file .bin to ocm
	void			loadOneColorPattern(unsigned int colorVal);
	void			loadBackgroundPattern();    //drc added for background pattern generation
	int 			Calculate_Module_BackgroundPattern_DevelopMode(unsigned char ModuleNum);
	int 			Load_Background_LUT(void);
	void            AjustEdgePixelAttenuation(unsigned int ch, float F1_PixelPos, float F2_PixelPos, float FC_PixelPos);
};

#endif /* SRC_PATTERNGENMODULE_PATTERNGENMODULE_H_ */
