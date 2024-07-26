/*
 * StructDefined.h
 *
 *  Created on: Dec 6, 2020
 *      Author: nas
 */

#ifndef SRC_CMDDECODER_DATASTRUCTURES_H_
#define SRC_CMDDECODER_DATASTRUCTURES_H_

#include "GlobalVariables.h"
#include <string>
#include <vector>


#ifdef _DEVELOPMENT_MODE_

	// Struct same as class no need to nest it inside another class
	struct TrueFlex
	{
		bool 			active = false;	   //default all channels are active	//1 means channels are active and 0 means deleted
		int 			ADP = 0;			//NOT IN USE IN PRODUCTION MODE
		float 			ATT =0;			  //NOT IN USE IN PRODUCTION MODE
		int 			CMP =0;				//NOT IN USE IN PRODUCTION MODE

		double 			FC =0;				// for position
		double 			BW =0;				// for channel width

		float           PD = 2;             //drc added for when phasedepth can be configured in test procedure to find a optimal for background and channels.
		float 			LAMDA=0;
		float 			SIGMA=0;
		float 			K_OPP=0;
		float 			A_OPP=0;
		float 			K_ATT=0;
		float 			A_ATT=0;

		bool 			b_ColorSet= false;		//full screen on colour
		int 			COLOR=0;
	};

	struct FixedGrid
	{
		bool 			active = false;	//default all channels are active  //1 means channels are active and 0 means deleted
		int 			ADP = 0;
		float 			ATT =0;
		int 			CMP =0;
		double 			F1 = 0;
		double 			F2 = 0;

		double 			FC = 0;
		double 			BW =0;			// for channel width

		int 			slotNum = 0;

		std::vector<float> slotsATTEN; 	//holds every slot attentuation within a channel	//dynamic vector allocation

		float           PD = 2;             //drc added for when phasedepth can be configured in test procedure to find a optimal for background and channels.
		float 			LAMDA=0;
		float 			SIGMA=0;
		float 			K_OPP=0;
		float 			A_OPP=0;
		float 			K_ATT=0;
		float 			A_ATT=0;

		bool 			b_ColorSet= false;		//full screen on colour
		int 			COLOR=0;

		int 			n_ch_1 = 0;		// Each gap pattern carries information of 2 channel numbers which give its position
		int 			n_ch_2 = 0;		// Each gap pattern carries information of 2 channel numbers which give its position
	};


	struct ModulesInfo{
		std::string 	slotSize = "0";			// for TF and numerical
		int 			ID;
		enum 			action {STORE, RESTORE};

		// for setting grayscales low and high range
		bool 			b_NewValueSet= false;	//when user set new low and high range of grayscale
		int 			grayValueLow= 0;
		int 			grayValueHigh= 0;

		std::vector<int> xPhaseVal;
		std::vector<int> yGrayscaleVal;

		int 			PortNo = 0;			// User tell in development mode by command which port are we setting sigmas for
		std::vector<float> v_SigmaVals;	// value of sigma for each port
		std::vector<float> v_K_attVals;	// different values of K_att for each power range (11)per port
		std::vector<float> v_A_attVals;	// different values of A_att for each power range (11)per port
		bool 			b_New_Katt_Set= false;
		bool 			b_New_Aatt_Set= false;
		bool 			b_New_Katt_offSet= false;
		float 			Katt_offset=0;
		bool 			b_New_Aatt_offSet= false;
		float 			Aatt_offset=0;
		bool 			b_NewSigmasSet= false;

		bool 			b_New_Sigma_offSet= false;
		float 			Sigma_offset=0;


		//For updating Frequency ranges in Pattern, fc_low/fc_high etc
		bool 			b_NewFreqSet= false;
		double 			fc_low = 191125;	//default
		double 			fc_high = 196275;	//default
		float 			range=0;
		float 			unit_mul=0;
		int 			TempInterpolation = 0;
	};

	struct TECinfo{
		//PID controller
		float 			TEC1_kp=0, TEC2_kp=0;//p
		float 			TEC1_ki=0, TEC2_ki=0;//i
		float 			TEC1_kd=0, TEC2_kd=0;//d

		float 			TEC1_tv=0, TEC2_tv=0;//target value

		int 			TEC1_PERIOD=0, TEC2_PERIOD=0;
	};


	struct BackgroundCalibPara{
		//drc added for background pattern parameters
		int				Sigma = 0.025;
		float			PD = 2.2;
		float			A_Opt = 0.5;
		float			K_Opt = 0.03;

	};

	struct DevelopModeVar{

		int 			developMode = 0;
		bool 			phaseDepth_changed = false;
		float 			phaseDepth = 2;		// default 2PI

		float 			rotate = 0;
		bool 			rotateAngle_changed = false;

		bool 			startSendingTECData = false;

		int 			phaseStart=0;
		int 			phaseEnd=255;
		bool 			lutRange_changed = false;

		//PID controller
		bool			PIDSet = false;
		unsigned int 	TEC1_kp=0, TEC2_kp=0;//p
		unsigned int 	TEC1_ki=0, TEC2_ki=0;//i
		unsigned int 	TEC1_kd=0, TEC2_kd=0;//d
		unsigned int 	TEC_tv=0;//target value

		//Send pattern image from file in binary format. patterb.bin
		bool			sendPattern = false;
		std::string		path;

		//Send grayscale single color to LCOS
		bool 			b_sendColor = false;
		int				colorValue = 0;

		//Background pattern
		int				backColorValue =0;
		bool			b_backColor = false;

		BackgroundCalibPara structBackgroundPara; //drc added

		//Switch Temperature on TestRig Display LCOS vs Grating
		bool			m_switch = 0;
	};

	struct Panel
	{
		uint64_t 		current{};			// 64 byte value ZTE
		bool 			readyFlag{0};		// When desired temperature is reached this flag will go high 1.

		//For Panel Gaps
		bool			b_gapSet = false;
		int				topGap = 0;
		int				bottomGap = 0;
		int 			middleGap = 0;
		int				middleGapPosition = 0;
	};

#else
// Struct same as class no need to nest it inside another class
	struct TrueFlex
	{
		bool 			active = false;	//default all channels are active	//1 means channels are active and 0 means deleted
		int 			ADP = 0;
		float			ATT =0;
		int 			CMP =0;
		double 			FC =0;
		double 			BW =0;
	};

	struct ModulesInfo
	{
		std::string 	slotSize = "0";			// for TF and numerical
		int 			ID;
		enum 			action {STORE, RESTORE};
	};


	/**********************************************************************************************************************************************************************************************************/

	struct FixedGrid
	{
		bool 			active = false;	//default all channels are active  //1 means channels are active and 0 means deleted
		int 			ADP = 0;
		float 			ATT =0;
		int 			CMP =0;
		double 			F1 = 0;
		double 			F2 = 0;

		double 			FC = 0;
		double 			BW =0;			// for channel width

		int 			slotNum = 0;

		std::vector<float> slotsATTEN; 	//holds every slot attentuation within a channel	//dynamic vector allocation
	};

	struct Panel
	{
		uint64_t 		current{};		// 64 byte value ZTE
		bool 			readyFlag{0};		// When desired temperature is reached this flag will go high 1.
	};
#endif




#endif /* SRC_CMDDECODER_DATASTRUCTURES_H_ */
