/*
 * PatternGenModule.cpp
 *
 *  Created on: Jan 31, 2023
 *      Author: mib_n
 */

#include <unistd.h>
#include <algorithm>
#include <string>
#include <map>
#include <ostream>

#include "PatternGenModule.h"
#include "DataStructures.h"

clock_t tstart;
PatternGenModule *PatternGenModule::pinstance_{nullptr};

PatternGenModule::PatternGenModule()
{
	g_serialMod = SerialModule::GetInstance();
	g_patternCalib = PatternCalibModule::GetInstance();
	g_tempMonitor = TemperatureMonitor::GetInstance();

	int status = PatternGen_Initialize();

	if(status != 0)
	{
		printf("Driver<Pattern>: Pattern Gen. Module Initialization Failed.\n");
		g_serialMod->Serial_WritePort("\01INTERNAL_ERROR\04\n");
		//PatternGen_Closure();
	}

	m_bCalibDataOk = g_patternCalib->Get_LUT_Load_Status();

#ifdef _TWIN_WSS_
	m_customLCOS_Height = g_LCOS_Height/2;
#endif


}

PatternGenModule::~PatternGenModule()
{
//	delete[] channelColumnData;
	delete[] fullPatternData;
}

PatternGenModule *PatternGenModule::GetInstance()
{
	if (pthread_mutex_lock(&global_mutex[LOCK_PATTERN_INSTANCE]) != 0)	// locking and checking the result, if lock was successful and no deadlock happened
		std::cout << "global_mutex[LOCK_PATTERN_INSTANCE] lock unsuccessful" << std::endl;
	else
	{
	    if (pinstance_ == nullptr)
	    {
	        pinstance_ = new PatternGenModule();
	    }

		if (pthread_mutex_unlock(&global_mutex[LOCK_PATTERN_INSTANCE]) != 0)	// Unlocking and checking the result, if lock was successful and no deadlock happened
			std::cout << "global_mutex[LOCK_PATTERN_INSTANCE] unlock unsuccessful" << std::endl;
	}

	return pinstance_;
}

int PatternGenModule::MoveToThread()
{
	if (thread_id == 0)
	{
		if (pthread_create(&thread_id, &thread_attrb, ThreadHandle, (void*) this) != 0) // 'this' is passed to pointer, so pointer dies as function dies
		{
			printf("Driver<PATTERN>: thread_id create fail.\n");
			return (-1);
		}
		else
		{
			printf("Driver<PATTERN>: thread_id create OK.\n");
		}
	}
	else
	{
		printf("Driver<PATTERN>: thread_id already exist.\n");
		return (-1);
	}

	return (0);
}

void *PatternGenModule::ThreadHandle(void *arg)
{
	PatternGenModule *recvPtr = (PatternGenModule*) arg;
	recvPtr->ProcessPatternGeneration();
	return (NULL);
}

void PatternGenModule::ProcessPatternGeneration(void)
{
	OCMTransfer ocmTrans;

	int is_bPatternDone;
	int status;
	enum bTrigger {NONE,TEMP_CHANGED, COMMAND_CAME};
	bTrigger etrigger = NONE;

	Load_Background_LUT(); //drc added for loading parameters for background pattern display
	loadBackgroundPattern();


	while(true)
	{
		usleep(100);

		is_bPatternDone = PatternOutcome::NO_OPERATION;

		if(BreakThreadLoop() == 0)
		{
			break;
		}


		if (pthread_mutex_lock(&global_mutex[LOCK_CHANNEL_DS]) != 0)	// locking and checking the result, if lock was successful and no deadlock happened
			std::cout << "global_mutex[LOCK_CHANNEL_DS] lock unsuccessful" << std::endl;
		else
		{
			if (pthread_mutex_lock(&global_mutex[LOCK_TEMP_CHANGED_FLAG]) != 0)	// locking and checking the result, if lock was successful and no deadlock happened
				std::cout << "global_mutex[LOCK_TEMP_CHANGED_FLAG] lock unsuccessful" << std::endl;
			else
			{
				if(g_bNewCommandData || g_bTempChanged)
				{
					status = Get_LCOS_Temperature();

					if(g_bTempChanged)
					{
						g_bTempChanged = false;
						etrigger = TEMP_CHANGED;
						std::cout << "Temperature Changed... " << std::endl;
					}
					else
					{
						etrigger = COMMAND_CAME;
						std::cout << "Command Came... " << std::endl;
					}

					if (pthread_mutex_unlock(&global_mutex[LOCK_TEMP_CHANGED_FLAG]) != 0)	// Unlocking and checking the result, if lock was successful and no deadlock happened
						std::cout << "global_mutex[LOCK_TEMP_CHANGED_FLAG] unlock unsuccessful" << std::endl;


					int operationMode; 		// Development or Production
					Find_OperationMode(&operationMode);

					if((m_bCalibDataOk && (operationMode == OperationMode::PRODUCTION)) || (operationMode == OperationMode::DEVELOPMENT))
					{
						status = Init_PatternGen_All_Modules(&operationMode);

						if(status == 0)
							is_bPatternDone = PatternOutcome::SUCCESS;
						else
							is_bPatternDone = PatternOutcome::FAILED;
					}
					else
					{
						is_bPatternDone = PatternOutcome::FAILED;
						g_errorMsg = CALIB_FILES_NOTOK;
					}


					g_bNewCommandData = false;
				}
				else	// If no new command or temp changed happened
				{
					if (pthread_mutex_unlock(&global_mutex[LOCK_TEMP_CHANGED_FLAG]) != 0)	// Unlocking and checking the result, if lock was successful and no deadlock happened
						std::cout << "global_mutex[LOCK_TEMP_CHANGED_FLAG] unlock unsuccessful" << std::endl;
				}
			}

			if (pthread_mutex_unlock(&global_mutex[LOCK_CHANNEL_DS]) != 0)	// Unlocking and checking the result, if lock was successful and no deadlock happened
				std::cout << "global_mutex[LOCK_CHANNEL_DS] unlock unsuccessful" << std::endl;
		}


		if(is_bPatternDone == PatternOutcome::SUCCESS)
		{
			if(rotationAngle != 0)
			{
				rotateArray(rotationAngle, g_LCOS_Width, g_LCOS_Height);
			}

#ifdef _FETCH_PATTERN_
			Save_Pattern_In_FileSysten();
			g_serialMod->Serial_WritePort("FF\n");	// Fetch File string send to PC software to start fetching
#endif
			// Perform OCM transfer
			if(ocmTrans.SendPatternData(fullPatternData) == 0)
				std::cerr << "Pattern Transfer Success!!\n";

			g_serialMod->cmd_decoder.SetPatternTransferFlag(true);
		}
		else if (is_bPatternDone == PatternOutcome::FAILED)
		{
			std::string msg;
			GetErrorMessage(msg);

			if(etrigger == TEMP_CHANGED)
			{
				g_serialMod->Serial_WritePort(msg);			// If temperature changed caused error then write message directly to Serial port
			}
			else if (etrigger == COMMAND_CAME)
			{
				g_serialMod->cmd_decoder.PrintResponse(msg, g_serialMod->cmd_decoder.PrintType::ERROR_HI_PRIORITY);	 // If Command came and caused error then write message to PrintResponse
			}

			g_serialMod->cmd_decoder.SetPatternTransferFlag(true);  //drc why still true here?
		}

	}

	pthread_exit(NULL);
}

int PatternGenModule::Get_LCOS_Temperature()
{
	float temp = g_tempMonitor->GetLCOSTemperature();

	if(temp > g_Max_Normal_Temperature)  //55
		temp = g_Max_Normal_Temperature;
	else if (temp < g_Min_Normal_Temperature)  //51
		temp = g_Min_Normal_Temperature;

	g_LCOS_Temp = temp;
	std::cout << "g_LCOS_Temp = " << g_LCOS_Temp << std::endl;

	return (0);
}

int PatternGenModule::Init_PatternGen_All_Modules(int *mode)
{
	/* Reset Full Pattern 2D Array */
	memset(fullPatternData, 0, sizeof(unsigned char)*g_LCOS_Width*g_LCOS_Height);
	loadBackgroundPattern();
	//memset(rotated, m_backColor, sizeof(unsigned char)*g_LCOS_Width*g_LCOS_Height);

	int status = 0;

	if(*mode == OperationMode::DEVELOPMENT)
	{
		status = Check_Need_For_GlobalParameterUpdate();

		if(status == -2)
			return 0;							// No further calculation of channels.
	}

	if (g_serialMod->cmd_decoder.arrModules[0].slotSize == "TF")
	{	//printf("Driver<PATTERN>: SLOT SIZE TF MODULE 1.\n");
		// if module 1 has slotsize TrueFlex
		g_moduleNum = 1;

		if(*mode == OperationMode::PRODUCTION)
			status = Calculate_Every_ChannelPattern('T');
		else
		{
			status = Calculate_Every_ChannelPattern_DevelopMode('T');
		}
	}

	if(status != 0)
		return (-1);

	if (g_serialMod->cmd_decoder.arrModules[1].slotSize == "TF")
	{	//printf("Driver<PATTERN>: SLOT SIZE TF MODULE 2.\n");
		// if module 2 has slotsize TrueFlex
		g_moduleNum = 2;

		if(*mode == OperationMode::PRODUCTION)
			status = Calculate_Every_ChannelPattern('T');
		else
		{
			status = Calculate_Every_ChannelPattern_DevelopMode('T');
		}
	}

	if(status != 0)
		return (-1);

	// Check and Do FIXED GRID MODULE Calculations
	if (g_serialMod->cmd_decoder.arrModules[0].slotSize == "625" || g_serialMod->cmd_decoder.arrModules[0].slotSize == "125")
	{	//printf("Driver<PATTERN>: SLOT SIZE FG MODULE 1.\n");
		// if module 1 has slotsize for Fixed Grid
		g_moduleNum = 1;

		if(*mode == OperationMode::PRODUCTION)
			status = Calculate_Every_ChannelPattern('F');
		else
		{
			status = Calculate_Every_ChannelPattern_DevelopMode('F');
		}
	}

	if(status != 0)
		return (-1);

	if (g_serialMod->cmd_decoder.arrModules[1].slotSize == "625" || g_serialMod->cmd_decoder.arrModules[1].slotSize == "125")
	{	//printf("Driver<PATTERN>: SLOT SIZE FG MODULE 2.\n");
		// if module 2 has slotsize for Fixed Grid
		g_moduleNum = 2;

		if(*mode == OperationMode::PRODUCTION)
			status = Calculate_Every_ChannelPattern('F');
		else
		{
			status = Calculate_Every_ChannelPattern_DevelopMode('F');
		}
	}

	if(status != 0)
		return (-1);

	return (0);
}

int PatternGenModule::Check_Need_For_GlobalParameterUpdate()
{
#ifdef _DEVELOPMENT_MODE_

	bool updatePhase = false;
	bool updateLUTRange = false;
	bool sendPatternFile = false;
	bool sendColor = false;
//	bool backColor = false;
	bool backSigma = false; // drc added for background grating init(e.g. SET:PANEL.1:BACK_COLOR=10 in DEV mode)
	bool backPD = false;
	bool backK_Opt = false;

	int  currentModule = std::stoi(g_serialMod->cmd_decoder.objVec[1]); //for background pattern setting in developmode

	std::string filePath;
	int color{0};
	int startGray{0};
	int endGray{0};

	if (pthread_mutex_lock(&global_mutex[LOCK_DEVMODE_VARS]) != 0)	// locking and checking the result, if lock was successful and no deadlock happened
		std::cout << "global_mutex[LOCK_DEVMODE_VARS] lock unsuccessful" << std::endl;
	else
	{
		if(g_serialMod->cmd_decoder.structDevelopMode.phaseDepth_changed == true)
		{
			g_phaseDepth = g_serialMod->cmd_decoder.structDevelopMode.phaseDepth;
			updatePhase = true;
			g_serialMod->cmd_decoder.structDevelopMode.phaseDepth_changed = false;
		}

		if(g_serialMod->cmd_decoder.structDevelopMode.rotateAngle_changed == true)
		{
			rotationAngle = g_serialMod->cmd_decoder.structDevelopMode.rotate;
		}

		if(g_serialMod->cmd_decoder.structDevelopMode.lutRange_changed == true)
		{
			updateLUTRange = true;
			startGray = g_serialMod->cmd_decoder.structDevelopMode.phaseStart;
			endGray = g_serialMod->cmd_decoder.structDevelopMode.phaseEnd;
			g_serialMod->cmd_decoder.structDevelopMode.lutRange_changed = false;
		}

		if(g_serialMod->cmd_decoder.structDevelopMode.sendPattern == true)
		{
			sendPatternFile = true;
			filePath = g_serialMod->cmd_decoder.structDevelopMode.path;
			g_serialMod->cmd_decoder.structDevelopMode.sendPattern = false;
		}

		if(g_serialMod->cmd_decoder.structDevelopMode.b_sendColor == true)
		{
			sendColor = true;
			color = g_serialMod->cmd_decoder.structDevelopMode.colorValue;
			g_serialMod->cmd_decoder.structDevelopMode.b_sendColor = false;
		}


		/*if(g_serialMod->cmd_decoder.structDevelopMode.b_backColor == true)
		{
			backColor = true;
			m_backColor = g_serialMod->cmd_decoder.structDevelopMode.backColorValue;
			g_serialMod->cmd_decoder.structDevelopMode.b_backColor = false;
			memset(fullPatternData, m_backColor, sizeof(unsigned char)*g_LCOS_Width*g_LCOS_Height);
		}*/

		if(g_serialMod->cmd_decoder.structDevelopMode.b_backSigma == true)
		{
			backSigma = true;
			g_serialMod->cmd_decoder.structDevelopMode.b_backSigma = false;
		}
		if(g_serialMod->cmd_decoder.structDevelopMode.b_backPD == true)
		{
			g_phaseDepth = g_serialMod->cmd_decoder.structDevelopMode.structBackgroundPara[currentModule].PD;
			backPD = true;
			g_serialMod->cmd_decoder.structDevelopMode.b_backPD = false;
		}
		if(g_serialMod->cmd_decoder.structDevelopMode.b_backK_Opt == true)
		{
			backK_Opt = true;
			g_serialMod->cmd_decoder.structDevelopMode.b_backK_Opt = false;
		}

		if (pthread_mutex_unlock(&global_mutex[LOCK_DEVMODE_VARS]) != 0)	// Unlocking and checking the result, if lock was successful and no deadlock happened
			std::cout << "global_mutex[LOCK_DEVMODE_VARS] unlock unsuccessful" << std::endl;
	}

	if(updatePhase == true)
	{
		std::cout << "\n\nGrayscale Maximum = " << linearLUT[static_cast<int>(g_phaseDepth*180)] << "\n\n"<< std::endl;	// Print for Yidan to see what grayscale last value is
	}

	if(updateLUTRange == true)
	{
		getStartEndOffset(startGray, endGray);
	}

	if(sendPatternFile == true)
	{
		loadPatternFile_Bin(filePath);
		return -2;					// Indicate that we are sending pattern file to OCM, no need to calculate pattern for channels.
	}

	if(sendColor == true)
	{
		loadOneColorPattern(color);
		return -2;				   // Indicate that we are sending one color to OCM, no need to calculate pattern for channels.
	}

	if(backSigma == true)
	{
		Module_Background_DS_For_Pattern[currentModule].SIGMA = g_serialMod->cmd_decoder.structDevelopMode.structBackgroundPara[currentModule].Sigma;
		loadBackgroundPattern();
		return 0;				   // Indicate that we are setting background pattern //drc modified
	}

	if(backPD == true)
	{
		Module_Background_DS_For_Pattern[currentModule].PD = g_serialMod->cmd_decoder.structDevelopMode.structBackgroundPara[currentModule].PD;
		loadBackgroundPattern();
		return 0;				   // Indicate that we are setting background pattern //drc modified
	}
	if(backK_Opt == true)
	{
		Module_Background_DS_For_Pattern[currentModule].K_OPP = g_serialMod->cmd_decoder.structDevelopMode.structBackgroundPara[currentModule].K_Opt;
		loadBackgroundPattern();
		return 0;				   // Indicate that we are setting background pattern //drc modified
	}
#endif
	return (0);


}

void PatternGenModule::getStartEndOffset(int startGrayVal, int endGrayVal)
{
	// DR.DU SAID WE DONT NEED TO SET start and end gray values

//	// Search the first occurrence of startGrayVal in LUT
//	auto pos = std::find(linearLUT.begin(), linearLUT.end(), startGrayVal);
//
//	if(pos != linearLUT.end())
//	{
//		startOffsetLUT = pos - linearLUT.begin();
//	}
//
//
//	// Search the first occurrence of endGrayVal in LUT in reverse from back
//	auto pos2 = std::find(linearLUT.rbegin(), linearLUT.rend(), endGrayVal);
//
//	if(pos2 != linearLUT.rend())
//	{
//		endOffsetLUT = pos2 - linearLUT.rbegin();
//	}
//
//	std::cout << "start offset = " << startOffsetLUT << "  end offset = " << endOffsetLUT
//			  << "   NEW PHASE DEPTH  =  "<< static_cast<double>(linearLUT.size()-1-endOffsetLUT-startOffsetLUT)/180 << "pi"<< std::endl;

}

int PatternGenModule::Calculate_Every_ChannelPattern(char slotSize)
{
	inputParameters inputs;
	outputParameters outputs;
	int status = 0;
	float edgeK_Att = 0.0;

	if (slotSize == 'T')		// Calculate for TrueFlex Module
	{
//		clock_t tstart = clock();
//		std::cout << "Interpolation timine T1  = " << tstart << std::endl;

		for (int ch = 0; ch < g_Total_Channels; ch++)
		{
			if (g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][ch + 1].active == true)
			{
				inputs.ch_att = g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][ch + 1].ATT;
				inputs.ch_fc = g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][ch + 1].FC;
				inputs.ch_adp = g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][ch + 1].ADP-1;			// -1 because Calib Module PORT[] array starts from 0 to 22, while user gives ADP from 1 to 23
				float ch_bw = g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][ch + 1].BW;
				inputs.ch_f1 = inputs.ch_fc - (ch_bw/2) + 2.0;
				inputs.ch_f2 = inputs.ch_fc + (ch_bw/2) - 2.0;

				status = Find_Parameters_By_Interpolation(inputs, outputs, true, true, true, true);		// Interpolate all parameters

				if(status != 0)
					return (-1);

				Calculate_Pattern_Formulas(ch, g_wavelength, g_pixelSize, outputs.sigma, outputs.Aopt, outputs.Kopt, outputs.Aatt, outputs.Katt);

				//to calculate for edge attenuated value
				edgeK_Att = outputs.Katt + abs(1- (outputs.F1_PixelPos - round(outputs.F1_PixelPos)));
//				std::cout<<outputs.F1_PixelPos<<round(outputs.F1_PixelPos)<< std::endl;
				Calculate_Optimization_And_Attenuation(outputs.Aopt, outputs.Kopt, 6.0, edgeK_Att, 0); //0:left edge
				Fill_Channel_ColumnData(ch);

				edgeK_Att = outputs.Katt + abs(outputs.F2_PixelPos- round(outputs.F2_PixelPos));
//				std::cout<<outputs.F2_PixelPos<< round(outputs.F2_PixelPos)<< std::endl;
				Calculate_Optimization_And_Attenuation(outputs.Aopt, outputs.Kopt, 6.0, edgeK_Att, 2); //2: right edge
				Fill_Channel_ColumnData(ch);
//				AjustEdgePixelAttenuation(ch, outputs.F1_PixelPos, outputs.F2_PixelPos, outputs.FC_PixelPos);

				RelocateChannel(ch, outputs.F1_PixelPos, outputs.F2_PixelPos, outputs.FC_PixelPos);

			}
		}

//		clock_t tstart2 = clock();
//		std::cout << "Interpolation timine T2  = " << tstart << std::endl;
//		std::cout << "TOTAL  = " << static_cast<double>(tstart2 - tstart)/CLOCKS_PER_SEC << std::endl;
	}
	else						// Calculate for FixedGrid Module
	{
		for (int ch = 0; ch < g_Total_Channels; ch++)
		{
			// Go through each channel and set the colour user want to set.
			if (g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][ch + 1].active == true)
			{
				inputs.ch_att = g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][ch + 1].ATT;
				inputs.ch_fc = g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][ch + 1].FC;
				inputs.ch_adp = g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][ch + 1].ADP-1;
				inputs.ch_f1 = g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][ch + 1].F1 + 2.0;
				inputs.ch_f2 = g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][ch + 1].F2 - 2.0;

				status = Find_Parameters_By_Interpolation(inputs, outputs, true, true, true, true);		// Interpolate all parameters

				if(status != 0)
					return (-1);

				Calculate_Pattern_Formulas(ch, g_wavelength, g_pixelSize, outputs.sigma, outputs.Aopt, outputs.Kopt, outputs.Aatt, outputs.Katt);
                           // std::cout << "RelocateChannel  g_wavelength == 'T'" << std::endl;
				RelocateChannel(ch, outputs.F1_PixelPos, outputs.F2_PixelPos, outputs.FC_PixelPos);

				int total_Slots = g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][ch + 1].slotNum;
				for (int slot = 1; slot <= total_Slots; slot++)
				{
					// Go through all slot attenuation and if its not zero then calculate that slot attenuation and relocate that slot within the channel
					if (g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][ch + 1].slotsATTEN[slot-1] != 0)
					{
						float slot_ATT = g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][ch + 1].slotsATTEN[slot-1];
						float actual_slot_att = inputs.ch_att + slot_ATT;	// slot attenuation is relative to channel attenuation.(slot_ATT can be -ve)

						if(actual_slot_att < 0)
							actual_slot_att = 0;

						inputs.ch_att = actual_slot_att;

						status = Find_Parameters_By_Interpolation(inputs, outputs, false, false, true, false);		// Interpolate Attenuation only

						if(status != 0)
							return (-1);
						//std::cout << "\nSlot atten parameters: " <<" k_op= " << outputs.Kopt << " a_op= " << outputs.Aopt << " k_att= " << outputs.Aatt << " a_att= " << outputs.Katt << std::endl;
						Calculate_Pattern_Formulas(ch, g_wavelength, g_pixelSize, outputs.sigma, outputs.Aopt, outputs.Kopt, outputs.Aatt, outputs.Katt);

						RelocateSlot(ch, slot, total_Slots, outputs.F1_PixelPos, outputs.F2_PixelPos);
					}
				}
			}
		}
	}

	return (0);
}

void PatternGenModule::rotateArray(float angle, int width, int height)
{
	std::cout << "rotating .." << std::endl;
    float rad = angle * M_PI / 180.0;
    float cos_theta = cos(rad);
    float sin_theta = sin(rad);


    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int cx = width / 2;
            int cy = height / 2;

            int dx = x - cx;
            int dy = y - cy;

            int newx = (int)((dx * cos_theta - dy * sin_theta + cx));
            int newy = (int)((dx * sin_theta + dy * cos_theta + cy));

            if (newx >= 0 && newx < width && newy >= 0 && newy < height) {

            	// Loop through the destination image and find the source pixel positions by rotating in the opposite direction
            	rotated[1952*y + x] = fullPatternData[1952*newy + newx];

            }

        }
    }

    memcpy(fullPatternData, rotated, sizeof(rotated));

    std::cout << "rotating finished .." << std::endl;

}

int PatternGenModule::Calculate_Every_ChannelPattern_DevelopMode(char slotSize)
{
#ifdef _DEVELOPMENT_MODE_

	if (slotSize == 'T')		// Calculate for TrueFlex Module
	{
		for (int ch = 0; ch < g_Total_Channels; ch++)
		{
			if (g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][ch + 1].active == true)
			{
				float sigma =g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][ch + 1].SIGMA;
				float Aopt = g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][ch + 1].A_OPP;
				float Kopt = g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][ch + 1].K_OPP;
				float Aatt = g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][ch + 1].A_ATT;
				float Katt = g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][ch + 1].K_ATT;
				float wavelength = g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][ch + 1].LAMDA;
				float ch_fc = g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][ch + 1].FC;
				float ch_bw = g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][ch + 1].BW;
				float ch_f1 = ch_fc - (ch_bw/2);
				float ch_f2 = ch_fc + (ch_bw/2);

				float F1_PixelPos, F2_PixelPos, FC_PixelPos;
				float edgeK_Att = 0.0;

				if(Aatt == 0)
				{
					Aatt = 0.1;		// Aatt can't be zero otherwise ATT_factor calculation will have infinity
				}

				Calculate_Pattern_Formulas(ch, wavelength, g_pixelSize, sigma, Aopt, Kopt, Aatt, Katt);

				Find_LinearPixelPos_DevelopMode(ch_f1, F1_PixelPos);
				Find_LinearPixelPos_DevelopMode(ch_f2, F2_PixelPos);
				Find_LinearPixelPos_DevelopMode(ch_fc, FC_PixelPos);

				//to calculate for edge attenuated value

				edgeK_Att = Katt + F1_PixelPos - floor(F1_PixelPos);
//				std::cout<<outputs.F1_PixelPos<<round(outputs.F1_PixelPos)<< std::endl;
				Calculate_Optimization_And_Attenuation(Aopt, Kopt, 6.0, edgeK_Att, 0); //0:left edge
				Fill_Channel_ColumnData(ch);

				edgeK_Att = Katt + 1 - (F2_PixelPos - floor(F2_PixelPos));
//				std::cout<<outputs.F2_PixelPos<< round(outputs.F2_PixelPos)<< std::endl;
				Calculate_Optimization_And_Attenuation(Aopt, Kopt, 6.0, edgeK_Att, 2); //2: right edge
				Fill_Channel_ColumnData(ch);
//				AjustEdgePixelAttenuation(ch, outputs.F1_PixelPos, outputs.F2_PixelPos, outputs.FC_PixelPos);

//				RelocateChannel(ch, F1_PixelPos, F2_PixelPos, FC_PixelPos);

                //std::cout << "Calculate_Every_ChannelPattern_DevelopMode  slotSize == 'T'" << std::endl;
				//drc added for background pattern calculation command from test station where no relocate is needed
//				if(ch_bw != 5150.0) {
					RelocateChannel(ch, F1_PixelPos, F2_PixelPos, FC_PixelPos);
//

			}
		}
	}
	else	// Calculate for FixedGrid Module
	{
		for (int ch = 0; ch < g_Total_Channels; ch++)
		{
			// Go through each channel and set the colour user want to set.

			if (g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][ch + 1].active == true)
			{
				float sigma = g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][ch + 1].SIGMA;
				float Aopt = g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][ch + 1].A_OPP;
				float Kopt = g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][ch + 1].K_OPP;
				float Aatt = g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][ch + 1].A_ATT;
				float Katt = g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][ch + 1].K_ATT;
				float wavelength = g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][ch + 1].LAMDA;
				float ch_fc = g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][ch + 1].FC;
				float ch_f1 = g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][ch + 1].F1;
				float ch_f2 = g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][ch + 1].F2;

				float F1_PixelPos, F2_PixelPos, FC_PixelPos;

				if(Aatt == 0)
				{
					Aatt = 0.1;		// Aatt can't be zero otherwise ATT_factor calculation will have infinity
				}

				Calculate_Pattern_Formulas(ch, wavelength, g_pixelSize,sigma, Aopt, Kopt, Aatt, Katt);

				Find_LinearPixelPos_DevelopMode(ch_f1, F1_PixelPos);
				Find_LinearPixelPos_DevelopMode(ch_f2, F2_PixelPos);
				Find_LinearPixelPos_DevelopMode(ch_fc, FC_PixelPos);

                            //std::cout << "Calculate_Every_ChannelPattern_DevelopMode g_Total_Channels'" << std::endl;
				RelocateChannel(ch, F1_PixelPos, F2_PixelPos, FC_PixelPos);

				int total_Slots = g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][ch + 1].slotNum;
				for (int slot = 1; slot <= total_Slots; slot++)
				{
					// Go through all slot attenuation and if its not zero then calculate that slot attenuation and relocate that slot within the channel
					if (g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][ch + 1].slotsATTEN[slot-1] != 0)
					{
						float slot_ATT = g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][ch + 1].slotsATTEN[slot-1];
						float actual_slot_att = slot_ATT;	// slot attenuation is relative to channel attenuation.(slot_ATT can be -ve)
						float only_for_testing = Katt + 1;
						//std::cout << "\nSlot atten parameters: " <<" k_op= " << k_op << " a_op= " << a_op << " k_att= " << k_att << " a_att= " << a_att << std::endl;

						Calculate_Pattern_Formulas(ch, wavelength, g_pixelSize, sigma, Aopt, Kopt, actual_slot_att, only_for_testing);		// in develop mode, slot attenuation is currently not set by user

						RelocateSlot(ch, slot, total_Slots, F1_PixelPos, F2_PixelPos);
					}
				}
			}
		}
	}
#endif
	return (0);


}

int PatternGenModule::Calculate_Module_BackgroundPattern_DevelopMode(unsigned char ModuleNum)
{
	//here for command configuring background. If using biggest bw value to configure is ok, then nothing needs to be done here.
	return 0;
}

int PatternGenModule::Calculate_Pattern_Formulas(const int ch,const float lamda, const int pixelSize, float sigmaRad, const float Aopt, const float Kopt, const float Aatt,const float Katt)
{
	/* Reset Channel Data Array */
//	memset(channelColumnData, 0, sizeof(unsigned char)*g_Total_Channels*m_customLCOS_Height);

	// LOGICS ARE ONLY SUITABLE FOR +VE SIGMA
	//printf("lamda = %f \t sigmaRad = %f \t Aopt = %f \t Kopt = %f \t Aatt = %f \t Katt = %f \n\r", lamda, sigmaRad, Aopt, Kopt,Aatt,Katt);

	if (sigmaRad < 0)
	{
		g_bSigmaNegative = true;
		sigmaRad = abs(sigmaRad);	// make it positive
	}
	else
	{
		g_bSigmaNegative = false;
	}

	Calculate_PhaseLine(pixelSize, sigmaRad, lamda);

	Calculate_Period(pixelSize, sigmaRad, lamda);

	Calculate_Mod_And_RebuildPeriod(&periodCount[0], &factorsForDiv[0]);

	Calculate_Optimization_And_Attenuation(Aopt, Kopt, Aatt, Katt, 1);

	Fill_Channel_ColumnData(ch);

	return (0);
}

int PatternGenModule::Calculate_PhaseLine(const float pixelSize, float sigmaRad, const float lamda)
{
	for (int Y = 1; Y <= m_customLCOS_Height; Y++)
	{
		phaseLine[Y-1] = (g_phaseDepth*Y*pixelSize*sigmaRad) / lamda;
		//printf("phaseLine[y] = %f \n\r", phaseLine[Y-1]);
	}

	return (0);
}

void PatternGenModule::Calculate_Mod_And_RebuildPeriod(unsigned int periodCount[], int factorsForDiv[])
{
	int factorForDiv_increament = 0;
	periods.clear();		// For tracking periods number in each channel 10,11,11,10,10 ....

	for (int i = 0; i < m_customLCOS_Height; i++)
	{
		phaseLine_MOD[i] = std::fmod(phaseLine[i], g_phaseDepth);	//mod of 2

		// Rebuild Period
		if(phaseLine_MOD[i] < (1/calculatedPeriod*g_phaseDepth/2))
		{
			rebuildPeriod[i] =	phaseLine_MOD[i] + g_phaseDepth;
		}
		else
		{
			rebuildPeriod[i] =	phaseLine_MOD[i];
		}

		// Do the period count from 1 to N...
		if(i == 0)
		{
			periodCount[i] = 1;
		}
		else
		{
			if(rebuildPeriod[i] > rebuildPeriod[i-1])					// Increment Period Count if current rebuild period is > previous rebuild period
			{
				periodCount[i] = periodCount[i-1] + 1;
				factorsForDiv[i] = factorForDiv_increament;
			}
			else
			{
				periods.push_back(periodCount[i-1]);
				periodCount[i] = 1;
				++factorForDiv_increament;
				factorsForDiv[i] = factorForDiv_increament;
			}
		}

		//printf("%d \t phaseLine_MOD = %f \t rebuildPeriod = %f \t periodCount = %d \t factorsForDiv = %d\n",i,phaseLine_MOD[i], rebuildPeriod[i], periodCount[i], factorsForDiv[i]);
	}

	// The last period will have nothing to compare itself to, so in loop periods,=.push_back will never happen
	// So we push the last periodCount to periods. It will also help when sigma is too small.
	// In case if sigma is too small that only one period exist which size is equal to whole height of LCOS

	periods.push_back(periodCount[m_customLCOS_Height-1]);
}

void PatternGenModule::Calculate_Period(const float pixelSize, float sigmaRad, const float lamda)
{
	if(sigmaRad == 0)
	{
		calculatedPeriod = m_customLCOS_Height;
	}
	else
	{
		calculatedPeriod = lamda/(pixelSize*sigmaRad);	 	// From EXCEL

		if (calculatedPeriod > m_customLCOS_Height)
		{
			// IMPORTANT: Make sure calculated Period height can never exceed the height of display i.e. g_LCOS_Height
			calculatedPeriod = m_customLCOS_Height;
		}
		else if (calculatedPeriod <= 0)
		{
			calculatedPeriod = 2;			// minimum period is 2. below 2 is not possible otherwise totalPeriodCount[i] - 1 will be infinity
		}
	}

	//printf("calculatedPeriod = %f \n", calculatedPeriod);
}

void PatternGenModule::Calculate_Optimization_And_Attenuation(const float Aopt, const float Kopt, const float Aatt,const float Katt, const int col)
{
	/* Based on EXCEL FILE:
	 * Opt Factor = Kopt*ABS(sin((Aopt*Y*Phase_depth*PI)/(cal_period)))
	 * Opt Pattern = (PhaseLine + Opt Factor) - (factorforDIV*Phase_depth)
	 * Att Factor = Katt*(sin(Y*Phase_depth*PI/(Aatt)))
	 * Att Pattern = Opt Pattern + Att Factor;
	 * Att_Border_Limited = if(Att Pattern < 0, 0, IF(Att Pattern > (Phase_depth + ((1/cal_period)*(Phase_depth/2))),
	 * 						Phase_depth + ((1/cal_period)*(Phase_depth/2)), Att Pattern))
	 */
	float optFactor{0.0}, attFactor{0.0};		// Holds intermediate values
	float minAtt = 0;
	float maxAtt = g_phaseDepth + (1/calculatedPeriod)*(g_phaseDepth/2);

	float temp[m_customLCOS_Height];	// Use to temporary store attenuated_limited data to flip all periods in case of -ve Sigma
	std::vector<float> flipArray;
	int periodIndex_track = 0;
	int jump=0;

	//std::cout << " Aopt = " << Aopt << " Kopt  " << Kopt <<" Aatt = " << Aatt << " Katt  " << Katt << " calculate Period = " << calculatedPeriod <<"\n";
	/*REMEMBER: Due to algorithm in excel sometimes is A_att is not unique it will cause no change in output data*/
	for (int Y = 0; Y < m_customLCOS_Height; Y++)
	{
		optFactor = Kopt*abs(sin((Aopt*(Y+1)*2*PI)/(calculatedPeriod)));		// (Y+1) because pixel number must starts from 1 to 1080

		optimizedPattern[Y] = (phaseLine[Y] + optFactor) - (factorsForDiv[Y]*g_phaseDepth);

		// Must do this to Optimize Algorithm !! Don't perform SINE calculation if SINE parameter is 2PI or PI, which results in ZERO anyway
		float theta = g_phaseDepth/Aatt;

		attFactor = Katt*sin(Aatt*(Y+1)*2*PI/calculatedPeriod);		// (Y+1) because pixel number must starts from 1 to 1080

		attenuatedPattern[col][Y] = optimizedPattern[Y] + attFactor;

		// For Boundary values which are below 0 or above 2PI we will restrict them

		if(attenuatedPattern[col][Y] < minAtt)
		{
			attenuatedPattern_limited[col][Y] = minAtt;
		}
		else if (attenuatedPattern[col][Y] > maxAtt)
		{
			attenuatedPattern_limited[col][Y] = maxAtt;
		}
		else
		{
			attenuatedPattern_limited[col][Y] = attenuatedPattern[col][Y];
		}

		if(g_bSigmaNegative == true)    //drc to check how to modify here for edge
		{
			// Need to flip each period data
			flipArray.push_back(attenuatedPattern_limited[col][Y]);

			if(periods[periodIndex_track] == periodCount[Y])
			{

				std::reverse(flipArray.begin(), flipArray.end());

//				for(float a:flipArray)
//				{
//					std::cout << a << std::endl;
//				}

				for(unsigned int j=0; j< flipArray.size(); j++)
				{
					temp[j + jump] = flipArray[j];
//					std::cout << temp[Y] << std::endl;
				}

				jump+=flipArray.size();
				flipArray.clear();
				periodIndex_track++;
			}
		}

//		printf("%d \t optFactor = %f \t optimizedPattern = %f \t attenuatedPattern = %f \t attenuatedPattern_limited = %f \n",
//				Y,optFactor, optimizedPattern[Y], attenuatedPattern[Y], attenuatedPattern_limited[Y]);

	}

	if(g_bSigmaNegative == true)
	{
		for(int y = 0; y < m_customLCOS_Height; y++)
		{
			attenuatedPattern_limited[col][y] = temp[y];
			//printf("attenuatedPattern_limited = %f \n", attenuatedPattern_limited[y]);
		}
	}
}


void  PatternGenModule::AjustEdgePixelAttenuation(unsigned int ch, float F1_PixelPos, float F2_PixelPos, float FC_PixelPos)
{
//	ch, outputs.F1_PixelPos, outputs.F2_PixelPos, outputs.FC_PixelPos
}

void PatternGenModule::Fill_Channel_ColumnData(unsigned int ch)
{
	// For every attenuated value in degree find the graylevel from LUT
	for(int col = 0; col < 3; col++) {//added by drc for : 0 left edge; 1:channel;2 right edge
		for(int i =0 ; i<m_customLCOS_Height; i++)
		{
			unsigned int degree = round(attenuatedPattern_limited[col][i]*180);		// IMPORTANT: NOT DIVIDE BY PI, because attenuatedPattern values have unit PI, so the value doesnt include PI itself

			if(degree < (0 + startOffsetLUT))										// if startOffset is more than 0, i.e. 2, then any value of degree below 2 will get value of 2 from LUT
				degree = 0 + startOffsetLUT;
			else if (degree > (linearLUT.size()-1-endOffsetLUT))				// if endOffset is more than 0, i.e. 5, then any value of degree above max range of LUT available will get max value from LUT, max = linearLUT.size()-1-endOffsetLUT
				degree = (linearLUT.size()-1-endOffsetLUT);

			channelColumnData[col][i + m_customLCOS_Height*ch] = linearLUT[degree];
		}
	}
}

void PatternGenModule::RelocateChannel(unsigned int chNum, float f1_PixelPos, float f2_PixelPos, float fc_PixelPos)
{
	int i = 0;

	int ch_start_pixelLocation = round(f1_PixelPos);
	int ch_end_pixelLocation = round(f2_PixelPos);

	int ch_width_inPixels = ch_end_pixelLocation - ch_start_pixelLocation + 1; //drc modified starting from 0 end with 1919/1951, width should be 1920/1952

	if ((ch_start_pixelLocation + ch_width_inPixels) > g_LCOS_Width)
	{
		//channel getting out of screeen from right side
		ch_width_inPixels = g_LCOS_Width - ch_start_pixelLocation;	// give us the remaining pixel space available  and we set it to ch_bandwidth
	}
#ifdef _DEVELOPMENT_MODE_
//	 std::cout << "wjh start pixel location: " << ch_start_pixelLocation << "channel " << chNum +1<<std::endl;
//	 std::cout << "wjh end pixel location: " << ch_end_pixelLocation << std::endl;
//	 std::cout << "ch width in Pixel: " << ch_width_inPixels << std::endl;

#endif

	bool rotate = false;

	if(rotate)
	{	std::cout << "Rotating channel \n" << std::endl;
		RotateChannel(0, ch_start_pixelLocation, 0, ch_end_pixelLocation, 1079, ch_end_pixelLocation, 1079, ch_start_pixelLocation, 0.1, 540, ch_end_pixelLocation-ch_start_pixelLocation);
	}
	else
	{
		while (i < m_customLCOS_Height)
		{
			unsigned char value = m_backColor;

			if(g_moduleNum == 1)		// Top side of LCOS
			{
				if(g_serialMod->cmd_decoder.GetPanelInfo().b_gapSet){ //drc to check if gap is needed
#ifdef _TWIN_WSS_
					if(i > g_serialMod->cmd_decoder.GetPanelInfo().topGap){
						value = channelColumnData[1][i + m_customLCOS_Height *chNum];
					}

					int startMidGap = g_serialMod->cmd_decoder.GetPanelInfo().middleGapPosition - g_serialMod->cmd_decoder.GetPanelInfo().middleGap/2;
					int endMidGap = g_serialMod->cmd_decoder.GetPanelInfo().middleGapPosition + g_serialMod->cmd_decoder.GetPanelInfo().middleGap/2;

					if(startMidGap < m_customLCOS_Height && endMidGap < m_customLCOS_Height){
						if(i>=startMidGap && i <= endMidGap){
							//value = m_backColor;
							value = BackgroundColumnData[i];
						}
					}else if (startMidGap < m_customLCOS_Height && endMidGap > m_customLCOS_Height){
						if(i > startMidGap){
							//value = m_backColor;
							value = BackgroundColumnData[i];
						}
					}
#else
					if(i > g_serialMod->cmd_decoder.GetPanelInfo().topGap && i < m_customLCOS_Height - g_serialMod->cmd_decoder.GetPanelInfo().bottomGap){
							value = channelColumnData[1][i + m_customLCOS_Height *chNum];
					}
#endif
				}else{
					value = channelColumnData[1][i + m_customLCOS_Height *chNum];
				}

#ifndef _FLIP_DISPLAY_
				memset((fullPatternData + (i *g_LCOS_Width) + ch_start_pixelLocation), channelColumnData[0][i + m_customLCOS_Height *chNum], sizeof(char));
				memset((fullPatternData + (i *g_LCOS_Width) + ch_start_pixelLocation + 1), value, (ch_width_inPixels-2)* sizeof(char));
				memset((fullPatternData + (i *g_LCOS_Width) + ch_start_pixelLocation+(ch_width_inPixels-1)), channelColumnData[2][i + m_customLCOS_Height *chNum], sizeof(char));
#else
				memset((fullPatternData + ((m_customLCOS_Height -i) *g_LCOS_Width) - ch_start_pixelLocation - ch_width_inPixels), value, ch_width_inPixels* sizeof(char));
#endif
			}
			else if (g_moduleNum == 2)	// Bottom side of LCOS
			{
				if(g_serialMod->cmd_decoder.GetPanelInfo().b_gapSet){
					if(i < (m_customLCOS_Height - g_serialMod->cmd_decoder.GetPanelInfo().bottomGap)){
						value = channelColumnData[1][i + m_customLCOS_Height *chNum];
					}

					int startMidGap = g_serialMod->cmd_decoder.GetPanelInfo().middleGapPosition - g_serialMod->cmd_decoder.GetPanelInfo().middleGap/2;
					int endMidGap = g_serialMod->cmd_decoder.GetPanelInfo().middleGapPosition + g_serialMod->cmd_decoder.GetPanelInfo().middleGap/2;

					if(startMidGap > m_customLCOS_Height && endMidGap > m_customLCOS_Height){
						if(i>=(startMidGap%m_customLCOS_Height) && i <= (endMidGap%m_customLCOS_Height)){
							//value = m_backColor;
							value = BackgroundColumnData[i + m_customLCOS_Height];
						}
					}else if (startMidGap < m_customLCOS_Height && endMidGap > m_customLCOS_Height){
						if(i < (endMidGap%m_customLCOS_Height)){
							//value = m_backColor;
							value = BackgroundColumnData[i + m_customLCOS_Height];
						}
					}
				}else{
					value = channelColumnData[1][i + m_customLCOS_Height *chNum];
				}

#ifndef _FLIP_DISPLAY_
				memset((fullPatternData + ((i+m_customLCOS_Height) *g_LCOS_Width) + ch_start_pixelLocation), channelColumnData[0][i + m_customLCOS_Height *chNum],  sizeof(char));
				memset((fullPatternData + ((i+m_customLCOS_Height) *g_LCOS_Width) + ch_start_pixelLocation+1), value, (ch_width_inPixels-2)* sizeof(char));
				memset((fullPatternData + ((i+m_customLCOS_Height) *g_LCOS_Width) + ch_start_pixelLocation+(ch_width_inPixels-1)),  channelColumnData[2][i + m_customLCOS_Height *chNum], sizeof(char));

#else
				memset((fullPatternData + ((i+m_customLCOS_Height) *g_LCOS_Width) - ch_start_pixelLocation - ch_width_inPixels), value, ch_width_inPixels* sizeof(char));	// FLIP= F1 starts from RHS goes to LHS. 196275-191125  F2 <------ F1
#endif
			}

			i++;
		}
	}

}
void PatternGenModule::RotateChannel(int x1, int y1, int x2, int y2, int x3, int y3, int x4, int y4, float angleRad, int centerX, int centerY)
{
	float sin_theta = sin(angleRad);
	float cos_theta = cos(angleRad);

	// Vertex 1
	//float x1_new = abs(round((x1 - centerX) * cos_theta - (y1 - centerY) * sin_theta + centerX));
	float x1_new = 0;
	float y1_new = abs(round((x1 - centerX) * sin_theta + (y1 - centerY) * cos_theta + centerY));

	// Vertex 2
	//double x2_new = abs(round((x2 - centerX) * cos_theta - (y2 - centerY) * sin_theta + centerX));
	float x2_new = 0;
	float y2_new = abs(round((x2 - centerX) * sin_theta + (y2 - centerY) * cos_theta + centerY));

	// Vertex 3
	//double x3_new = abs(round((x3 - centerX) * cos_theta - (y3 - centerY) * sin_theta + centerX));
	float x3_new = 1079;
	float y3_new = abs(round((x3 - centerX) * sin_theta + (y3 - centerY) * cos_theta + centerY));

	// Vertex 4
	//double x4_new = abs(round((x4 - centerX) * cos_theta - (y4 - centerY) * sin_theta + centerX));
	float x4_new = 1079;
	float y4_new = abs(round((x4 - centerX) * sin_theta + (y4 - centerY) * cos_theta + centerY));
	std::cout << "New coordinates: \n" << std::endl;
	std::cout << x1_new << "  " <<y1_new << "\n"
			 << x2_new << "  " <<y2_new << "\n"
			 << x3_new << "  " <<y3_new << "\n"
			 << x4_new << "  " <<y4_new << std::endl;

	for (int row = std::min(y1_new, std::min(y2_new, std::min(y3_new, y4_new))); row <= std::max(y1_new, std::max(y2_new, std::max(y3_new, y4_new))); row++) {
	    for (int col = std::min(x1_new, std::min(x2_new, std::min(x3_new, x4_new))); col <= std::max(x1_new, std::max(x2_new, std::max(x3_new, x4_new))); col++) {
	        if (isInsideRectangle(col, row, x1_new, y1_new, x2_new, y2_new, x3_new, y3_new, x4_new, y4_new)) {

	        	fullPatternData[1952*col + row] = 255;

	        }
	        //std::cout << col << " ";
	    }
		//std::cout << "\n";
	}
}

bool PatternGenModule::isInsideRectangle(float x, float y, float x1, float y1, float x2, float y2, float x3, float y3, float x4, float y4)
{
    // Compute the area of the rectangle
    float area = 0.5 * abs(x2*y1 - x1*y2 + x3*y2 - x2*y3 + x4*y3 - x3*y4 + x1*y4 - x4*y1);
    //std::cout << "area of rectangle = " << area << std::endl;
    // Compute the areas of the four triangles formed by the point (x,y) and the vertices of the rectangle
    float area1 = 0.5 * abs(x1*y - x*y1 + x2*y1 - x1*y2 + x*y2 - x2*y);
    float area2 = 0.5 * abs(x2*y - x*y2 + x3*y2 - x2*y3 + x*y3 - x3*y);
    float area3 = 0.5 * abs(x3*y - x*y3 + x4*y3 - x3*y4 + x*y4 - x4*y);
    float area4 = 0.5 * abs(x4*y - x*y4 + x1*y4 - x4*y1 + x*y1 - x1*y);

    // If the sum of the areas of the four triangles is equal to the area of the rectangle, the point is inside the rectangle
    return (area1 + area2 + area3 + area4) == area;
}

void PatternGenModule::RelocateSlot(unsigned int chNum, unsigned int slotNum, unsigned int totalSlots, float f1_PixelPos, float f2_PixelPos)
{
	int i = 0;

	int slot_Start_Location = 0;
	int slot_Width_inPixels = 0;

	int ch_start_pixelLocation = round(f1_PixelPos);
	int ch_end_pixelLocation = round(f2_PixelPos);

	int ch_width_inPixels = ch_end_pixelLocation - ch_start_pixelLocation;

	if ((ch_start_pixelLocation + ch_width_inPixels) > g_LCOS_Width)
	{
		//channel getting out of screeen from right side
		ch_width_inPixels = g_LCOS_Width - ch_start_pixelLocation;	// give us the remaining pixel space available  and we set it to ch_bandwidth
	}

	int prev_slot_width_inPixels = 0;

	if (slotNum > 1)
	{
		prev_slot_width_inPixels = round((slotNum-1)*(1.0*ch_width_inPixels/(totalSlots)));
	}

	slot_Width_inPixels = round(slotNum*(1.0*ch_width_inPixels/totalSlots)) - prev_slot_width_inPixels;

	if (slot_Width_inPixels < 0)
	{
		// Just for safe and avoiding segmentation error
		slot_Width_inPixels = 0;	// Just for safe and avoiding segmentation error
	}

	slot_Start_Location = ch_start_pixelLocation + (prev_slot_width_inPixels);

	while (i < m_customLCOS_Height)
	{
		unsigned char value = m_backColor;

		if(g_moduleNum == 1)		// Top side of LCOS
		{
			if(g_serialMod->cmd_decoder.GetPanelInfo().b_gapSet){
#ifdef _TWIN_WSS_
				if(i > g_serialMod->cmd_decoder.GetPanelInfo().topGap){
					value = channelColumnData[1][i + m_customLCOS_Height *chNum];
				}

				int startMidGap = g_serialMod->cmd_decoder.GetPanelInfo().middleGapPosition - g_serialMod->cmd_decoder.GetPanelInfo().middleGap/2;
				int endMidGap = g_serialMod->cmd_decoder.GetPanelInfo().middleGapPosition + g_serialMod->cmd_decoder.GetPanelInfo().middleGap/2;

				if(startMidGap < m_customLCOS_Height && endMidGap < m_customLCOS_Height){
					if(i>=startMidGap && i <= endMidGap){
						//value = m_backColor;
						value = BackgroundColumnData[i];
					}
				}else if (startMidGap < m_customLCOS_Height && endMidGap > m_customLCOS_Height){
						if(i > startMidGap){
							//value = m_backColor;
							value = BackgroundColumnData[i];
						}
					}
#else
					if(i > g_serialMod->cmd_decoder.GetPanelInfo().topGap && i < m_customLCOS_Height - g_serialMod->cmd_decoder.GetPanelInfo().bottomGap){
							value = channelColumnData[1][i + m_customLCOS_Height *chNum];
					}
#endif
			}else{
				value = channelColumnData[1][i + m_customLCOS_Height *chNum];
			}

#ifndef _FLIP_DISPLAY_
			memset((fullPatternData + (i *g_LCOS_Width) + slot_Start_Location), value, slot_Width_inPixels* sizeof(char));
#else
			memset((fullPatternData + ((m_customLCOS_Height -i) *g_LCOS_Width) - slot_Start_Location - slot_Width_inPixels), value, slot_Width_inPixels* sizeof(char));	// FLIP= F1 starts from RHS goes to LHS. 196275-191125  F2 <------ F1
#endif
		}
		else if (g_moduleNum == 2)	// Bottom side of LCOS
		{
			if(g_serialMod->cmd_decoder.GetPanelInfo().b_gapSet){
				if(i < (m_customLCOS_Height - g_serialMod->cmd_decoder.GetPanelInfo().bottomGap)){
					value = channelColumnData[1][i + m_customLCOS_Height *chNum];
				}

				int startMidGap = g_serialMod->cmd_decoder.GetPanelInfo().middleGapPosition - g_serialMod->cmd_decoder.GetPanelInfo().middleGap/2;
				int endMidGap = g_serialMod->cmd_decoder.GetPanelInfo().middleGapPosition + g_serialMod->cmd_decoder.GetPanelInfo().middleGap/2;

				if(startMidGap > m_customLCOS_Height && endMidGap > m_customLCOS_Height){
					if(i>=(startMidGap%m_customLCOS_Height) && i <= (endMidGap%m_customLCOS_Height)){
						//value = m_backColor;
						value = BackgroundColumnData[i];
					}
				}else if (startMidGap < m_customLCOS_Height && endMidGap > m_customLCOS_Height){
					if(i < (endMidGap%m_customLCOS_Height)){
						//value = m_backColor;
						value = BackgroundColumnData[i];
					}
				}
			}else{
				value = channelColumnData[1][i + m_customLCOS_Height *chNum];
			}

#ifndef _FLIP_DISPLAY_
			memset((fullPatternData + ((i+m_customLCOS_Height) *g_LCOS_Width) + slot_Start_Location), value, slot_Width_inPixels* sizeof(char));
#else
			memset((fullPatternData + ((i+m_customLCOS_Height) *g_LCOS_Width) - slot_Start_Location - slot_Width_inPixels), value, slot_Width_inPixels* sizeof(char));	// FLIP= F1 starts from RHS goes to LHS. 196275-191125  F2 <------ F1
#endif
		}
		i++;
	}

}

int PatternGenModule::Find_Parameters_By_Interpolation(inputParameters &ins, outputParameters &outs, bool interpolateSigma, bool interpolateOpt, bool interpolateAtt, bool interpolatePixelPos)
{
	int status = 0;

	// Send ready signal to calib thread
	if (pthread_mutex_lock(&global_mutex[lOCK_CALIB_PARAMS]) != 0)	// locking and checking the result, if lock was successful and no deadlock happened
		std::cout << "global_mutex[LOCK_CALIB_PARAMS] lock unsuccessful" << std::endl;
	else
	{
		g_ready1 = g_ready2 = g_ready3 = g_ready4 = false;			// Initialize all to No-Interpolation

		g_patternCalib->Set_Current_Module(g_moduleNum);

		if(interpolateSigma)
		{
			g_patternCalib->Set_Sigma_Args(ins.ch_adp,ins.ch_fc,g_LCOS_Temp);
			g_ready3 = true;					// Setting it to false will cause no calculation for that thread
		}

		if(interpolateOpt)
		{
			g_patternCalib->Set_Aopt_Kopt_Args(ins.ch_adp, ins.ch_fc);
			g_ready1 = true;					// Setting it to false will cause no calculation for that thread
		}

		if(interpolateAtt)
		{
			g_patternCalib->Set_Aatt_Katt_Args(ins.ch_adp,ins.ch_fc, ins.ch_att);
			g_ready2 = true;					// Setting it to false will cause no calculation for that thread
		}

		if(interpolatePixelPos)
		{
			g_patternCalib->Set_Pixel_Pos_Args(ins.ch_f1, ins.ch_f2, ins.ch_fc, g_LCOS_Temp);
			g_ready4 = true;					// Setting it to false will cause no calculation for that thread
		}

		pthread_cond_broadcast(&cond);		// broadcast signal to all threads which are waiting on condition

		if (pthread_mutex_unlock(&global_mutex[lOCK_CALIB_PARAMS]) != 0)	// Unlocking and checking the result, if lock was successful and no deadlock happened
			std::cout << "global_mutex[LOCK_CALIB_PARAMS] unlock unsuccessful" << std::endl;
	}

	usleep(80);	// Not necessary but just to ensure caibration threads have mutex access so they can signal result ready

	if (pthread_mutex_lock(&global_mutex[lOCK_CALIB_PARAMS]) != 0)	// locking and checking the result, if lock was successful and no deadlock happened
		std::cout << "global_mutex[LOCK_CALIB_PARAMS] lock unsuccessful" << std::endl;
	else
	{
		while(g_ready1 || g_ready2 || g_ready3 || g_ready4)		// wait until all results are ready from calibration module
		{
			pthread_cond_wait(&cond_result_ready, &global_mutex[lOCK_CALIB_PARAMS]);	// wait until condition result_ready signal is triggered
		};

		status = g_patternCalib->Get_Interpolation_Status();

		if(status == 0)
		{
			if(interpolateSigma)
				outs.sigma = g_patternCalib->Sigma_params.result_Sigma;

			if(interpolateOpt)
			{
				outs.Aopt = g_patternCalib->Aopt_Kopt_params.result_Aopt;
				outs.Kopt = g_patternCalib->Aopt_Kopt_params.result_Kopt;
			}

			if(interpolateAtt)
			{
				outs.Aatt = g_patternCalib->Aatt_Katt_params.result_Aatt;
				outs.Katt = g_patternCalib->Aatt_Katt_params.result_Katt;
			}

			if(interpolatePixelPos)   //drc to check how to 
			{
				outs.F1_PixelPos = g_patternCalib->Pixel_Pos_params.result_F1_PixelPos;
				outs.F2_PixelPos = g_patternCalib->Pixel_Pos_params.result_F2_PixelPos;
				outs.FC_PixelPos = g_patternCalib->Pixel_Pos_params.result_FC_PixelPos;
			}
		}
		else
		{
			// INTERNAL ERROR
		}

		if (pthread_mutex_unlock(&global_mutex[lOCK_CALIB_PARAMS]) != 0)	// Unlocking and checking the result, if lock was successful and no deadlock happened
			std::cout << "global_mutex[LOCK_CALIB_PARAMS] unlock unsuccessful" << std::endl;
	}

	if(status != 0)
	{
		g_errorMsg = INTERPOLATION_FAILURE;
		return (-1);
	}

	return (0);
}

void PatternGenModule::Find_OperationMode(int *mode)
{
	*mode = OperationMode::PRODUCTION;

#ifdef _DEVELOPMENT_MODE_
	pthread_mutex_lock(&global_mutex[LOCK_DEVMODE_VARS]);
		if(g_serialMod->cmd_decoder.structDevelopMode.developMode == 1)
			*mode = OperationMode::DEVELOPMENT;
	pthread_mutex_unlock(&global_mutex[LOCK_DEVMODE_VARS]);
#endif
}

int PatternGenModule::PatternGen_Initialize(void)
{
	b_LoopOn = true;

	thread_id = 0;
	pthread_attr_init(&thread_attrb);	//Default initialize thread attributes

	constexpr float maxPhase = 2.2;
	Create_Linear_LUT(maxPhase);		// We have fixed LUT for 2.2 PI


	return (0);
}

void PatternGenModule::PatternGen_Closure(void)
{
	if (pthread_mutex_lock(&global_mutex[LOCK_CLOSE_PATTGENLOOP]) != 0)	// locking and checking the result, if lock was successful and no deadlock happened
		std::cout << "[569]global_mutex[LOCK_CLOSE_PATTGENLOOP] lock unsuccessful" << std::endl;
	else
	{
		b_LoopOn = false;

		if (pthread_mutex_unlock(&global_mutex[LOCK_CLOSE_PATTGENLOOP]) != 0)	// Unlocking and checking the result, if lock was successful and no deadlock happened
			std::cout << "global_mutex[LOCK_CLOSE_PATTGENLOOP] unlock unsuccessful" << std::endl;
	}
}

void PatternGenModule::StopThread()
{
	// Break loop
	PatternGen_Closure();

	// Wait for thread to exit normally
	if (thread_id != 0)
	{
		pthread_join(thread_id, NULL);
		thread_id = 0;
	}

	printf("Driver<PATTERN> Thread terminated\n");

	if(pinstance_ != nullptr)
	{
		delete pinstance_;
		pinstance_ = nullptr;
	}
}

int PatternGenModule::BreakThreadLoop()
{
	if (pthread_mutex_lock(&global_mutex[LOCK_CLOSE_PATTGENLOOP]) != 0)	// locking and checking the result, if lock was successful and no deadlock happened
		std::cout << "[597]global_mutex[LOCK_CLOSE_PATTGENLOOP] lock unsuccessful" << std::endl;
	else
	{
		if(!b_LoopOn)
		{
			if (pthread_mutex_unlock(&global_mutex[LOCK_CLOSE_PATTGENLOOP]) != 0)	// Unlocking and checking the result, if lock was successful and no deadlock happened
				std::cout << "global_mutex[LOCK_CLOSE_PATTGENLOOP] unlock unsuccessful" << std::endl;

			return (0);		// break
		}

		if (pthread_mutex_unlock(&global_mutex[LOCK_CLOSE_PATTGENLOOP]) != 0)	// Unlocking and checking the result, if lock was successful and no deadlock happened
			std::cout << "global_mutex[LOCK_CLOSE_PATTGENLOOP] unlock unsuccessful" << std::endl;
	}

	return (-1);			// dont break loop
}

void PatternGenModule::Create_Linear_LUT(float phaseDepth)
{
	int lutSize = round(phaseDepth*(180));					// Not divide by PI because phaseDepth has no PI in it
	//int lutSize = round(g_phaseDepth*(180));					// Dr.Du said always create LUT for 2.2 PI and we can change range of LUT using phaseDepth
	//std::cout << "lutSize " <<lutSize <<std::endl;

	int phaseLow = 0;
	int phaseHigh = lutSize;
	int grayLow = 0;
	int grayHigh = 255;

	// y = mx + b

	float m = (float)(grayHigh - grayLow)/(phaseHigh - phaseLow);

	// y - y1 = m(x-x1)
	// y - 0 = m(x-0)
	// y = mx;

	for (int j = 0; j <= lutSize; j++)
	{
		 int values = std::min(static_cast<int>(std::round(grayLow + (m * j))), grayHigh);	// before j was replaced with i and i was overshadowing
		linearLUT.push_back(static_cast< unsigned char > (values));
		//std::cout << j << " "<<values <<std::endl;
	}

	//std::cout << "lutSize " <<lutSize <<std::endl;

}

//drc added for print map elements in cout below
template <typename T>std::ostream& operator<<(std::ostream& os, const std::map<std::string, T>& m) {
     os << "{";
     bool first = true;
     for (const auto& pair : m) {
         if (!first) {
             os << ", ";
         }
         os << "\"" << pair.first << "\": \"" << pair.second << "\"";
         first = false;
     }
     os << "}";
     return os;
}

//drc added for background pattern para read and load into data structure to initialize background display when startup

int PatternGenModule::Load_Background_LUT(void)
{
	std::ifstream file("/mnt/Background_LUT.ini");

    if (file.is_open()) {
        std::cout << "[Background_LUT] File has been opened" << std::endl;
    }
    else {
        std::cout << "[Background_LUT] File opening Error" << std::endl;
        return (-1);
    }


    // The file reading is based on INI LUT file template.

    std::map<std::string, std::map<std::string, std::string>> config;
    unsigned int mod = 0;
    std::string line, currentSection;
    while (std::getline(file, line)) {
    	if (line.empty() || line[0] == ';') {
                continue; // Skip empty lines and comments
    	}
        if (line[0] == '[') {
            currentSection = line.substr(1, line.size() - 2);
            mod++;
        }
    	else
    	{
            size_t pos = line.find('=');
            if (pos != std::string::npos) {
            	std::string key = line.substr(0, pos);
                std::string value = line.substr(pos + 1);
                config[currentSection][key] = value;
                // load parameters into data structure to be used by pattern generation
                if(key == "SIGMA"){
					Module_Background_DS_For_Pattern[mod].SIGMA = stof(value);
					std::cout << "Module: "<< mod <<"SIGMA:" << Module_Background_DS_For_Pattern[mod].SIGMA << std::endl;
                }
                else if(key == "PD"){
					Module_Background_DS_For_Pattern[mod].PD = stof(value);

                }
                else if(key == "A_OPP"){
					Module_Background_DS_For_Pattern[mod].A_OPP = stof(value);

                }
                else if(key == "K_OPP"){
					Module_Background_DS_For_Pattern[mod].K_OPP = stof(value);

                }
                else if(key == "A_ATT"){
                	Module_Background_DS_For_Pattern[mod].A_ATT = stof(value);

                }
                else if(key == "K_ATT"){
                	Module_Background_DS_For_Pattern[mod].K_ATT = stof(value);

                }
                else if(key == "LAMDA"){
                	Module_Background_DS_For_Pattern[mod].LAMDA = stof(value);

                }
                else if(key == "FC_PixelPos"){
                	Module_Background_DS_For_Pattern[mod].FC_PixelPos = stof(value);
                }
                else if(key == "F1_PixelPos"){
                	Module_Background_DS_For_Pattern[mod].F1_PixelPos = stof(value);
                	std::cout << "Module:" << mod <<" F1_PixelPos:" << Module_Background_DS_For_Pattern[mod].F1_PixelPos << std::endl;
                }
                else if(key == "F2_PixelPos"){
                	Module_Background_DS_For_Pattern[mod].F2_PixelPos = stof(value);
                }
                // if more parameters are needed to be adjusted for optimal pattern, add here for more parameters loading
                else{
                	std::cout << "[Background_LUT] File reading" << std::endl;
                }
            }
        }
     }
     file.close();
     return(0);
}


void PatternGenModule::Save_Pattern_In_FileSysten(void)
{
	/* Save file as a binary data, only pure data is saved and no whitespace or newlines
	*  Save as binary file to reduce file size and speed up transmission via SSH
	*/

	std::ofstream myFile("/mnt/SavedPattern.bin", std::ios::trunc | std::ios::binary);

	if(!myFile)
	{
		std::cout << "SavedPattern.bin can't open !!!" << std::endl;
	}
	else
	{
		for(unsigned int i = 0; i<(g_LCOS_Width*g_LCOS_Height); i++)
		{
			unsigned char mBytes = fullPatternData[i];
			myFile.write(reinterpret_cast<char*>(&mBytes), sizeof(mBytes));
		}
	}

	myFile.close();

//	std::ofstream myFile("/mnt/SavedPattern.txt", std::ios::trunc);
//
//	for(unsigned int i = 0; i<(g_LCOS_Width*g_LCOS_Height); i++)
//	{
//		myFile << (unsigned int)fullPatternData[i];
//		myFile << " ";
//
//		if(!(i%1919) && i != 0)
//		{
//			myFile << "\n";
//		}
//	}
//
//	myFile.close();
}

void PatternGenModule::GetErrorMessage(std::string &msg)
{
	switch(g_errorMsg)
	{
		case (INTERPOLATION_FAILURE):
		{
			msg = "\01Driver<PATTERN> Parameter Interpolation Failed\04\n";
			break;
		}
		case (PATTERN_CALC_FAILURE):
		{
			msg = "\01Driver<PATTERN> Pattern Calculation Failed\04\n";
			break;
		}
		case (PATTERN_RELOCATE_FAILURE):
		{
			msg = "\01Driver<PATTERN> Pattern Relocation Failed\04\n";
			break;
		}
		case (CALIB_FILES_NOTOK):
		{
			msg = "\01Driver<PATTERN> No Pattern Generation. Calib File issue\04\n";
			break;
		}
		default:
		{
			msg = "\01Driver<PATTERN> UNKNOWN ERROR\04\n";
		}
	}
}

void PatternGenModule::Find_LinearPixelPos_DevelopMode(float &freq, float &pixelPos)
{
	float fc_range_low = VENDOR_FREQ_RANGE_LOW;
	float fc_range_high = VENDOR_FREQ_RANGE_HIGH;

	float slope = (fc_range_high-fc_range_low)/(g_LCOS_Width-1);		// Linear relationship between frequency distribution vs pixel number

	  // y = fc_range_low + m(x)
	  // freq- fc_range_low/m

	   pixelPos = (freq-fc_range_low)/slope; //drc modified to no round for edge attenuation

	   if(pixelPos < 0)
		   pixelPos = 0;
	   else if (pixelPos > g_LCOS_Width-1)
		   pixelPos = g_LCOS_Width-1;
}

//drc added for calculate pattern for background when startup for two modules
void PatternGenModule::loadBackgroundPattern()
{
	OCMTransfer ocmTrans;
	unsigned char mod = 1;

	float sigma = Module_Background_DS_For_Pattern[mod].SIGMA;
	float Aopt = Module_Background_DS_For_Pattern[mod].A_OPP;
	float Kopt = Module_Background_DS_For_Pattern[mod].K_OPP;
	float Aatt = Module_Background_DS_For_Pattern[mod].A_ATT;
	float Katt = Module_Background_DS_For_Pattern[mod].K_ATT;
	float wavelength = Module_Background_DS_For_Pattern[mod].LAMDA;
	float FC_PixelPos = Module_Background_DS_For_Pattern[mod].FC_PixelPos;
	float F1_PixelPos = Module_Background_DS_For_Pattern[mod].F1_PixelPos;
	float F2_PixelPos = Module_Background_DS_For_Pattern[mod].F2_PixelPos;
	if(Aatt == 0){
		Aatt = 0.1;		// Aatt can't be zero otherwise ATT_factor calculation will have infinity
	}
	g_moduleNum = mod;
	Calculate_Pattern_Formulas(1, wavelength, g_pixelSize, sigma, Aopt, Kopt, Aatt, Katt);
	//std::cout << "Calculate_Every_ChannelPattern_DevelopMode  slotSize == 'T'" << std::endl;
	memcpy(BackgroundColumnData, &channelColumnData[1], m_customLCOS_Height); // obtain background column grayvalue
	RelocateChannel(1, F1_PixelPos, F2_PixelPos, FC_PixelPos);

	if(ocmTrans.SendPatternData(fullPatternData) == 0)
	std::cerr << "Background Pattern Transfer Success!!\n";
	//usleep(15000); //transfer pattern data


#ifdef _TWIN_WSS_
	mod++;
	g_moduleNum = mod;
	sigma = Module_Background_DS_For_Pattern[mod].SIGMA;
	Aopt = Module_Background_DS_For_Pattern[mod].A_OPP;
	Kopt = Module_Background_DS_For_Pattern[mod].K_OPP;
	Aatt = Module_Background_DS_For_Pattern[mod].A_ATT;
	Katt = Module_Background_DS_For_Pattern[mod].K_ATT;
	wavelength = Module_Background_DS_For_Pattern[mod].LAMDA;
	FC_PixelPos = Module_Background_DS_For_Pattern[mod].FC_PixelPos;
	F1_PixelPos = Module_Background_DS_For_Pattern[mod].F1_PixelPos;
	F2_PixelPos = Module_Background_DS_For_Pattern[mod].F2_PixelPos;
	if(Aatt == 0){
		Aatt = 0.1;		// Aatt can't be zero otherwise ATT_factor calculation will have infinity
	}
	Calculate_Pattern_Formulas(1, wavelength, g_pixelSize, sigma, Aopt, Kopt, Aatt, Katt);
	//std::cout << "Calculate_Every_ChannelPattern_DevelopMode  slotSize == 'T'" << std::endl;
	memcpy(BackgroundColumnData + m_customLCOS_Height, &channelColumnData[1], m_customLCOS_Height);
	RelocateChannel(1, F1_PixelPos, F2_PixelPos, FC_PixelPos);

	if(ocmTrans.SendPatternData(fullPatternData) == 0)
	std::cerr << "Background Pattern Transfer Success!!\n";
#endif
	Save_Pattern_In_FileSysten();

}


void PatternGenModule::loadOneColorPattern(unsigned int colorVal)
{
//	if(colorVal == 255 || colorVal == 0) //single color(grayscale for whole panel) as background
//	{
		memset(fullPatternData, static_cast<unsigned char>(colorVal), sizeof(unsigned char)*g_LCOS_Width*g_LCOS_Height);
//	}

	/*
#ifndef _TWIN_WSS_

	else //single grating pattern for whole panel as background
	{
		//drc added initial pattern of one single grating with period of value at colorVal
		unsigned char cPixelBlocks = 0, cPixelEndingLines = 0;
		unsigned char grayVal = 0;
		cPixelBlocks = round(g_LCOS_Height/colorVal); //pixel lines per grating period, need to set grayscale from 255 to 0 in each line.
		cPixelEndingLines = g_LCOS_Height%colorVal;


		unsigned char k = 0;
		while(k < cPixelBlocks) //how many blocks
		{
			grayVal = 0;
			for(unsigned int j = 0; j<colorVal; j++) //how many lines per block
			{
				grayVal = round(j*255/colorVal); //calculate grayscale value on slop
				for(unsigned int i = 0; i<g_LCOS_Width; i++)
				{
					memset(fullPatternData + (i + j*g_LCOS_Width) + k*colorVal*g_LCOS_Width, grayVal, sizeof(char));//copy gray value on width
				}
			}
			k++;
		}
		while(cPixelEndingLines != 0)
		{
			grayVal = 0;
			for(unsigned int j = 0; j<cPixelEndingLines; j++) //how many lines left unconfigured
			{
				grayVal = round(j*255/colorVal); //calculate grayscale value on slop
				for(unsigned int i = 0; i<g_LCOS_Width; i++)
				{
					memset(fullPatternData + (i + j*g_LCOS_Width) + cPixelBlocks*colorVal*g_LCOS_Width, grayVal, sizeof(char));//copy gray value on width
				}
			}
			break;
		}
	}
#endif //m_customLCOS_Height   g_moduleNum
	else //two grating pattern for two modules in panel as different background
	{
		unsigned char cPixelBlocks = 0, cPixelEndingLines = 0;
		unsigned char grayVal = 0;

		cPixelBlocks = round(m_customLCOS_Height/colorVal); //pixel lines per grating period, need to set grayscale from 255 to 0 in each line.
		cPixelEndingLines = m_customLCOS_Height%colorVal;

		for(unsigned char m = 0; m < g_moduleNum; m++)
		{
			unsigned char k = 0;
			while(k < cPixelBlocks) //how many stripes
			{
				grayVal = 0;
				for(unsigned int j = 0; j<colorVal; j++) //how many lines per block
				{
					grayVal = (m == 0 ? (round(j*255/colorVal)):(255 - (round(j*255/colorVal)))); //calculate grayscale value on slop
					for(unsigned int i = 0; i<g_LCOS_Width; i++)
					{
						memset(fullPatternData + m*m_customLCOS_Height*g_LCOS_Width + (i + j*g_LCOS_Width) + k*colorVal*g_LCOS_Width, grayVal, sizeof(char));//copy gray value on width
					}
				}
				k++;
			}
			while(cPixelEndingLines != 0) // for incomplete stripes
			{
				grayVal = 0;
				for(unsigned int j = 0; j<cPixelEndingLines; j++) //how many lines left
				{
					grayVal = (m == 0 ? (round(j*255/colorVal)):(255 - (round(j*255/colorVal)))); //calculate grayscale value on slop
					for(unsigned int i = 0; i<g_LCOS_Width; i++)
					{
						memset(fullPatternData + m*m_customLCOS_Height*g_LCOS_Width + (i + j*g_LCOS_Width) + cPixelBlocks*colorVal*g_LCOS_Width, grayVal, sizeof(char));//copy gray value on width
					}
				}
				break;
			}
		}

	}*/

}


void PatternGenModule::loadPatternFile_Bin(std::string path)
{
	std::ifstream in(path, std::ios::binary);

	if(in.is_open())
	{
		memset(fullPatternData, 0, sizeof(unsigned char)*g_LCOS_Width*g_LCOS_Height);

		unsigned char mBytes;

		for(unsigned int i = 0; i<(g_LCOS_Width*g_LCOS_Height); i++)
		{
			in.read(reinterpret_cast<char*>(&mBytes), sizeof(mBytes));
			fullPatternData[i] = mBytes;
		}

		std::cout << "Pattern File Is Loaded by " << __func__ << " at line " << __LINE__ << " on " << __DATE__ << std::endl;
		in.close();
	}

}
