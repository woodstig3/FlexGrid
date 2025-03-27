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
#include "Dlog.h"
#include "LCOSDisplayTest.h"
#include "wdt.h"

clock_t tstart;
PatternGenModule *PatternGenModule::pinstance_{nullptr};

#define OCM_SCAN_CHANNEL 2048    //greater than normal to branch for OCM columdata calcu.

PatternGenModule::PatternGenModule()
{
	g_serialMod = SerialModule::GetInstance();
	g_patternCalib = PatternCalibModule::GetInstance();
	g_tempMonitor = TemperatureMonitor::GetInstance();
	g_spaCmd = new SPASlicePortAttenuationCommand(nullptr);

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
#else
	m_customLCOS_Height = g_LCOS_Height;
#endif

}

PatternGenModule::~PatternGenModule()
{
//	delete[] channelColumnData;
	delete[] fullPatternData;
	delete g_spaCmd;
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

	int is_bPatternDone, is_bRestarted = 0;
	int status;
	enum bTrigger {NONE,TEMP_CHANGED, COMMAND_CAME};
	bTrigger etrigger = NONE;

	//self-test lcos panel pin status
	if(LCOSDisplayTest::RunTest() != 0) {
		FaultsAttr attr = {0};
		attr.Raised = true;
		attr.RaisedCount += 1;
		attr.Degraded = false;
		attr.DegradedCount = attr.RaisedCount;
		FaultMonitor::logFault(WSS_ACCESS_FAILURE, attr);

	}

	Load_Background_LUT(); //drc added for loading parameters for background pattern display
	loadBackgroundPattern();


	while(true)
	{
		usleep(100);
#ifdef _WATCHDOG_SOFTRESET_
		watchdog_feed();
#endif

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

#ifdef _OCM_SCAN_
		CalculateOCMPattern();
#endif

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

		if(is_bRestarted == 0)
		{
			is_bRestarted = 1;
			if(g_serialMod->cmd_decoder.actionSR->RestoreModule(1) == false)
				std::cout << "No stored module 1 pattern" << std::endl;
#ifdef _TWIN_WSS_
			if(g_serialMod->cmd_decoder.actionSR->RestoreModule(2) == false)
				std::cout << "No stored module 2 pattern" << std::endl;
#endif
		}



		if(is_bPatternDone == PatternOutcome::SUCCESS)
		{
			if(rotationAngle != 0)
			{
				rotateArray(rotationAngle, g_LCOS_Width, g_LCOS_Height);
			}
			// Perform OCM transfer
			if(ocmTrans.SendPatternData(fullPatternData) == 0)
				std::cout << "Pattern Transfer Success!!\n";
			else {
				FaultsAttr attr = {0};
				attr.Raised = true;
				attr.RaisedCount += 1;
				attr.Degraded = false;
				attr.DegradedCount = attr.RaisedCount;
				FaultMonitor::logFault(TRANSFER_FAILURE, attr);
			}


			g_serialMod->cmd_decoder.SetPatternTransferFlag(true);

#ifdef _FETCH_PATTERN_
			Save_Pattern_In_FileSystem();
			g_serialMod->Serial_WritePort("FF\n");	// Fetch File string send to PC software to start fetching
#endif

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
				std::cerr << msg <<std::endl;
			}

			g_serialMod->cmd_decoder.SetPatternTransferFlag(true);  //drc why still true here WHILE FAILED OUTCOME?
		}

	}

	pthread_exit(NULL);
}

int PatternGenModule::Get_LCOS_Temperature()
{
	double temp = g_tempMonitor->GetLCOSTemperature();

	if(temp > g_Max_Normal_Temperature)  //55
		temp = g_Max_Normal_Temperature;
	else if (temp < g_Min_Normal_Temperature)  //51
		temp = g_Min_Normal_Temperature;

	g_LCOS_Temp = temp;
	std::cout << "g_LCOS_Temp = " << g_LCOS_Temp << std::endl;

	return (0);
}

/*
int PatternGenModule::Init_PatternGen_All_Modules(int *mode)
{
	// Reset Full Pattern 2D Array
//	memset(fullPatternData, 0, sizeof(unsigned char)*g_LCOS_Width*g_LCOS_Height);
//	loadBackgroundPattern();
	//memset(rotated, m_backColor, sizeof(unsigned char)*g_LCOS_Width*g_LCOS_Height);

	//load background pattern first
	//loadPatternFile_Bin("/mnt/SavedPattern.bin");
	refreshBackgroundPattern();

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
*/

int PatternGenModule::Init_PatternGen_All_Modules(int *mode)
{
	refreshBackgroundPattern();

	int status = 0;

	if(*mode == OperationMode::DEVELOPMENT)
	{
		status = Check_Need_For_GlobalParameterUpdate();

		if(status == -2)
			return 0;							// No further calculation of channels.
	}
	status = ChannelsContiguousTest();
	if(*mode == OperationMode::PRODUCTION)
	{
		status = Calculate_Every_ChannelPattern();
		std::cout << "Calculate_Every_ChannelPattern finished..." << std::endl;

	}
	else
	{
		status = Calculate_Every_ChannelPattern_DevelopMode();
	}
	if(status != 0)
		return (-1);

	return(0);


}



int PatternGenModule::Check_Need_For_GlobalParameterUpdate()
{
#ifdef _DEVELOPMENT_MODE_

	bool updatePhase = false;
	bool updateLUTRange = false;
	bool sendPatternFile = false;
	bool sendColor = false;
	bool backColor = false;
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
			g_phaseDepth[currentModule] = g_serialMod->cmd_decoder.structDevelopMode.phaseDepth;
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


		if(g_serialMod->cmd_decoder.structDevelopMode.b_backColor == true)
		{
			backColor = true;
			m_backColor = g_serialMod->cmd_decoder.structDevelopMode.backColorValue;
			g_serialMod->cmd_decoder.structDevelopMode.b_backColor = false;
			memset(fullPatternData, m_backColor, sizeof(unsigned char)*g_LCOS_Width*g_LCOS_Height);
		}

		if(g_serialMod->cmd_decoder.structDevelopMode.b_backSigma == true)
		{
			backSigma = true;
			g_serialMod->cmd_decoder.structDevelopMode.b_backSigma = false;
		}
		if(g_serialMod->cmd_decoder.structDevelopMode.b_backPD == true)
		{
			g_phaseDepth[currentModule] = g_serialMod->cmd_decoder.structDevelopMode.structBackgroundPara[currentModule].PD;
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
//		std::cout << "\n\nGrayscale Maximum = " << linearLUT[static_cast<int>(g_phaseDepth*180)] << "\n\n"<< std::endl;	// Print for Yidan to see what grayscale last value is
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

	if(backPD == true) //although there is a set:module.1:phasedepth= command, drc add here for now and may omit it in case not needed later.
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

/*
 drc added for superchannel config: determine if several sub-channels are contiguous and form one super channel
 */
#ifndef _SPI_INTERFACE_
int PatternGenModule::ChannelsContiguousTest()
{
//	int ch = 1;
	int channelNo, moduleNo;
	int channelNo_compared_with, moduleNo_compared_with;

//	while (ch <= g_Total_Channels)
	for(const auto& ch: g_serialMod->cmd_decoder.activeChannels)
	{
		channelNo = ch.channelNo;
		moduleNo = ch.moduleNo;

		if (g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[moduleNo][channelNo].active && g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[moduleNo][channelNo].BW < VENDOR_BW_RANGE_LOW)
//		if(g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][ch].active && g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][ch].BW < VENDOR_BW_RANGE_LOW)
		{
			//Channel we are compare to others, we take that channel's f1,f2,fc first
			double ch_f1 = (g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[moduleNo][channelNo].FC - (g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[moduleNo][channelNo].BW / 2));
			double ch_f2 = (g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[moduleNo][channelNo].FC + (g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[moduleNo][channelNo].BW / 2));

//			int ch_compared_with = ch.channelNo + 1;	// No need to compared channel to itself, always compare to other numbers

//			while (ch_compared_with <= g_Total_Channels)
			for(const auto& ch_compared_with: g_serialMod->cmd_decoder.activeChannels)
			{
				channelNo_compared_with = ch_compared_with.channelNo;
				moduleNo_compared_with = ch_compared_with.moduleNo;
				if (moduleNo_compared_with == moduleNo && g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[moduleNo_compared_with][channelNo_compared_with].active == true)
//				if (g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][ch_compared_with].active == true)
				{
					double f1 = (g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[moduleNo_compared_with][ch_compared_with.channelNo].FC - (g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[moduleNo_compared_with][ch_compared_with.channelNo].BW / 2));
					double f2 = (g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[moduleNo_compared_with][ch_compared_with.channelNo].FC + (g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[moduleNo_compared_with][ch_compared_with.channelNo].BW / 2));

					int status = Contiguous_Logic(&ch_f1, &ch_f2, &f1, &f2);

					if(status == -1)
					{
						g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[moduleNo_compared_with][ch.channelNo].F1ContiguousOrNot = 1;
						g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[moduleNo_compared_with][ch_compared_with.channelNo].F2ContiguousOrNot = 1;

//						std::cout << "Contiguous at f1 to channel: "<< ch.channelNo << "The channel is contiguous at f2 to channel:" << ch_compared_with.channelNo << std::endl;
					}
					else if(status == -2)
					{
						g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[moduleNo_compared_with][ch.channelNo].F2ContiguousOrNot = 1;
						g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[moduleNo_compared_with][ch_compared_with.channelNo].F1ContiguousOrNot = 1;

//						std::cout << "Contiguous at f1: "<< ch.channelNo << "The channel is contiguous at f2 to channel:" << ch_compared_with.channelNo << std::endl;
					}
				}
//				ch_compared_with++;
			}
		}

		//Fixed Grid test
		if (g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[moduleNo][ch.channelNo].active && g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[moduleNo][ch.channelNo].BW < VENDOR_BW_RANGE_LOW)
//		if (g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][ch].active && g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][ch].BW < VENDOR_BW_RANGE_LOW)
		{
			//Channel we are compare to others, we take that channel's f1,f2,fc first
			double ch_f1 = g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[moduleNo][ch.channelNo].F1;
			double ch_f2 = g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[moduleNo][ch.channelNo].F2;

//			int ch_compared_with = ch + 1;	// No need to compared channel to itself, always compare to other numbers

//			while (ch_compared_with <= g_Total_Channels)
			for(const auto& ch_compared_with: g_serialMod->cmd_decoder.activeChannels)
			{
				if (ch_compared_with.moduleNo == ch.moduleNo && g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[ch_compared_with.moduleNo][ch_compared_with.channelNo].active)
				{
					double f1 = g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[ch_compared_with.moduleNo][ch_compared_with.channelNo].F1;
					double f2 = g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[ch_compared_with.moduleNo][ch_compared_with.channelNo].F2;

					int status = Contiguous_Logic(&ch_f1, &ch_f2, &f1, &f2);

					if(status == -1)
					{
						g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[ch_compared_with.moduleNo][ch.channelNo].F1ContiguousOrNot = 1;
						g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[ch_compared_with.moduleNo][ch_compared_with.channelNo].F2ContiguousOrNot = 1;
//						std::cout << "Contiguous at f1: "<< ch.channelNo << "The channel is contiguous at f2 to channel:" << ch_compared_with.channelNo << std::endl;

					}
					else if(status == -2)
					{
						g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[ch_compared_with.moduleNo][ch.channelNo].F2ContiguousOrNot = 1;
						g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[ch_compared_with.moduleNo][ch_compared_with.channelNo].F1ContiguousOrNot = 1;

//						std::cout << "Contiguous at f1: "<< ch.channelNo << "The channel is contiguous at f2 to channel:" << ch_compared_with.channelNo << std::endl;

					}
				}
//				ch_compared_with++;
			}
		}
//		ch++;
	}

	return (0);
}
#endif

#ifdef _SPI_INTERFACE_
int PatternGenModule::ChannelsContiguousTest()
{
	return (0);
}
#endif
int PatternGenModule::Contiguous_Logic(const double *ch_f1, const double *ch_f2, const double *other_ch_f1, const double *other_ch_f2)
{
	//if (*ch_f1 == *other_ch_f2 && *ch_f2 != *other_ch_f2)	// contiguous channel added to the right
	if((std::abs(*ch_f1 - *other_ch_f2) < 1e-6)	&& (std::abs(*ch_f2 - *other_ch_f2) > 1e-6))
	{
		return (-1);
	}
	else if ((std::abs(*ch_f2 - *other_ch_f1) < 1e-6) && (std::abs(*ch_f1 - *other_ch_f1) > 1e-6))	// left
	{
		return (-2);
	}

	return 0;
}


/*
int PatternGenModule::Calculate_Every_ChannelPattern(char slotSize)
{
	inputParameters inputs;
	outputParameters outputs;
	int status = 0;
	double edgeK_Att = 0.0;

	if (slotSize == 'T')		// Calculate for TrueFlex Module
	{
//		clock_t tstart = clock();
//		std::cout << "Interpolation timine T1  = " << tstart << std::endl;

//		for (int ch = 0; ch < g_Total_Channels; ch++)
		for(const auto& ch: g_serialMod->cmd_decoder.activeChannels)
		{
			if (ch.moduleNo == g_moduleNum && g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][ch.channelNo].active == true)
			{
				inputs.ch_att = g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][ch.channelNo].ATT;       //+1 because start from 1
				inputs.ch_fc = g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][ch.channelNo].FC;
				inputs.ch_adp = g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][ch.channelNo].ADP-1;			// -1 because Calib Module PORT[] array starts from 0 to 22, while user gives ADP from 1 to 23
				double ch_bw = g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][ch.channelNo].BW;
				//inputs.ch_f1 = inputs.ch_fc - (ch_bw/2) + 2.0; //drc
				inputs.ch_f1 = inputs.ch_fc - (ch_bw-10)/2;
				//inputs.ch_f2 = inputs.ch_fc + (ch_bw/2) - 2.0; //drc
				inputs.ch_f2 = inputs.ch_fc + (ch_bw-10)/2;

				status = Find_Parameters_By_Interpolation(inputs, outputs, true, true, true, true);		// Interpolate all parameters

				if(status != 0)
					return (-1);

				Calculate_Pattern_Formulas(ch.channelNo-1, g_wavelength, g_pixelSize, outputs.sigma, outputs.Aopt, outputs.Kopt, outputs.Aatt, outputs.Katt);

				//to calculate for edge attenuated value
				edgeK_Att = outputs.Katt + outputs.F1_PixelPos - floor(outputs.F1_PixelPos);

				Calculate_Optimization_And_Attenuation(outputs.Aopt, outputs.Kopt, 6.0, edgeK_Att, 0); //0:left edge
				Fill_Channel_ColumnData(ch.channelNo-1);

				edgeK_Att = outputs.Katt + 1- (outputs.F2_PixelPos - floor(outputs.F2_PixelPos));

				Calculate_Optimization_And_Attenuation(outputs.Aopt, outputs.Kopt, 6.0, edgeK_Att, 2); //2: right edge
				Fill_Channel_ColumnData(ch.channelNo-1);

				RelocateChannelTF(ch.channelNo-1, outputs.F1_PixelPos, outputs.F2_PixelPos, outputs.FC_PixelPos);

			}
		}
		//Adjust attenuation of edge columns and omit BW-10 for inner edge columns of slots within a superchannel
		status = ChannelsContiguousTest();
//		for (int ch = 0; ch < g_Total_Channels; ch++)
		for(const auto& ch: g_serialMod->cmd_decoder.activeChannels)
		{
			if (ch.moduleNo == g_moduleNum && g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][ch.channelNo].active == true)
			{
				if(g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][ch.channelNo].F1ContiguousOrNot == 0 &&
						g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][ch.channelNo].F2ContiguousOrNot == 0)
				{
					continue;
				}
				else if(g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][ch.channelNo].F1ContiguousOrNot == 1 &&
						g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][ch.channelNo].F2ContiguousOrNot == 1)
				{//inner slot, no need of edge attenuation and edge bw-10
					inputs.ch_att = g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][ch.channelNo].ATT;
					inputs.ch_fc = g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][ch.channelNo].FC;
					inputs.ch_adp = g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][ch.channelNo].ADP-1;			// -1 because Calib Module PORT[] array starts from 0 to 22, while user gives ADP from 1 to 23
					double ch_bw = g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][ch.channelNo].BW;

					inputs.ch_f1 = inputs.ch_fc - ch_bw/2;
					inputs.ch_f2 = inputs.ch_fc + ch_bw/2;

					status = Find_Parameters_By_Interpolation(inputs, outputs, true, true, true, true);		// Interpolate all parameters

					if(status != 0)
						return (-1);

					Calculate_Pattern_Formulas(ch.channelNo-1, g_wavelength, g_pixelSize, outputs.sigma, outputs.Aopt, outputs.Kopt, outputs.Aatt, outputs.Katt);

					RelocateChannelTF(ch.channelNo-1, outputs.F1_PixelPos, outputs.F2_PixelPos, outputs.FC_PixelPos);
				}
				else if(g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][ch.channelNo].F1ContiguousOrNot == 1 &&
						g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][ch.channelNo].F2ContiguousOrNot == 0)
				{//only right edge needs to be attenuated
					inputs.ch_att = g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][ch.channelNo].ATT;
					inputs.ch_fc = g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][ch.channelNo].FC;
					inputs.ch_adp = g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][ch.channelNo].ADP-1;			// -1 because Calib Module PORT[] array starts from 0 to 22, while user gives ADP from 1 to 23
					double ch_bw = g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][ch.channelNo].BW;

					inputs.ch_f1 = inputs.ch_fc - ch_bw/2;
					inputs.ch_f2 = inputs.ch_fc + (ch_bw-10)/2;

					status = Find_Parameters_By_Interpolation(inputs, outputs, true, true, true, true);		// Interpolate all parameters

					if(status != 0)
						return (-1);

					Calculate_Pattern_Formulas(ch.channelNo-1, g_wavelength, g_pixelSize, outputs.sigma, outputs.Aopt, outputs.Kopt, outputs.Aatt, outputs.Katt);

					edgeK_Att = outputs.Katt + 1- (outputs.F2_PixelPos - floor(outputs.F2_PixelPos));

					Calculate_Optimization_And_Attenuation(outputs.Aopt, outputs.Kopt, 6.0, edgeK_Att, 2); //2: right edge
					Fill_Channel_ColumnData(ch.channelNo-1);
					RelocateChannelTF(ch.channelNo-1, outputs.F1_PixelPos, outputs.F2_PixelPos, outputs.FC_PixelPos);
				}
				else if(g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][ch.channelNo].F1ContiguousOrNot == 0 &&
						g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][ch.channelNo].F2ContiguousOrNot == 1)
				{//only left edge needs to be attenuated

					inputs.ch_att = g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][ch.channelNo].ATT;
					inputs.ch_fc = g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][ch.channelNo].FC;
					inputs.ch_adp = g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][ch.channelNo].ADP-1;			// -1 because Calib Module PORT[] array starts from 0 to 22, while user gives ADP from 1 to 23
					double ch_bw = g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][ch.channelNo].BW;

					inputs.ch_f1 = inputs.ch_fc - (ch_bw-10)/2;
					inputs.ch_f2 = inputs.ch_fc + ch_bw/2;

					status = Find_Parameters_By_Interpolation(inputs, outputs, true, true, true, true);		// Interpolate all parameters

					if(status != 0)
						return (-1);

					Calculate_Pattern_Formulas(ch.channelNo-1, g_wavelength, g_pixelSize, outputs.sigma, outputs.Aopt, outputs.Kopt, outputs.Aatt, outputs.Katt);

					edgeK_Att = outputs.Katt + outputs.F1_PixelPos - floor(outputs.F1_PixelPos);

					Calculate_Optimization_And_Attenuation(outputs.Aopt, outputs.Kopt, 6.0, edgeK_Att, 0); //0:left edge
					Fill_Channel_ColumnData(ch.channelNo-1);

					RelocateChannelTF(ch.channelNo-1, outputs.F1_PixelPos, outputs.F2_PixelPos, outputs.FC_PixelPos);
				}
			}
		}

//		clock_t tstart2 = clock();
//		std::cout << "Interpolation timine T2  = " << tstart << std::endl;
//		std::cout << "TOTAL  = " << static_cast<double>(tstart2 - tstart)/CLOCKS_PER_SEC << std::endl;
	}
	else // Calculate for FixedGrid Module
	{
//		for (int ch = 0; ch < g_Total_Channels; ch++)
		for(const auto& ch: g_serialMod->cmd_decoder.activeChannels)
		{
			// Go through each channel and set the colour user want to set.
			if (ch.moduleNo == g_moduleNum && g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][ch.channelNo].active == true)
			{
				inputs.ch_att = g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][ch.channelNo].ATT;
				inputs.ch_fc = g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][ch.channelNo].FC;
				inputs.ch_adp = g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][ch.channelNo].ADP-1;
				inputs.ch_f1 = g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][ch.channelNo].F1;
				inputs.ch_f2 = g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][ch.channelNo].F2;
				double ch_bw = g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][ch.channelNo].BW;

				inputs.ch_f1 = inputs.ch_fc - (ch_bw-10)/2;
				inputs.ch_f2 = inputs.ch_fc + (ch_bw-10)/2;

				status = Find_Parameters_By_Interpolation(inputs, outputs, true, true, true, true);		// Interpolate all parameters

				if(status != 0)
					return (-1);

				Calculate_Pattern_Formulas(ch.channelNo-1, g_wavelength, g_pixelSize, outputs.sigma, outputs.Aopt, outputs.Kopt, outputs.Aatt, outputs.Katt);

				//to calculate for edge attenuated value
				edgeK_Att = outputs.Katt + outputs.F1_PixelPos - floor(outputs.F1_PixelPos);
//				std::cout<<outputs.F1_PixelPos<<round(outputs.F1_PixelPos)<< std::endl;
				Calculate_Optimization_And_Attenuation(outputs.Aopt, outputs.Kopt, 6.0, edgeK_Att, 0); //0:left edge
				Fill_Channel_ColumnData(ch.channelNo-1);

				edgeK_Att = outputs.Katt + 1 - (outputs.F2_PixelPos- floor(outputs.F2_PixelPos));
//				std::cout<<outputs.F2_PixelPos<< round(outputs.F2_PixelPos)<< std::endl;
				Calculate_Optimization_And_Attenuation(outputs.Aopt, outputs.Kopt, 6.0, edgeK_Att, 2); //2: right edge
				Fill_Channel_ColumnData(ch.channelNo-1);
//				AjustEdgePixelAttenuation(ch, outputs.F1_PixelPos, outputs.F2_PixelPos, outputs.FC_PixelPos);

				// std::cout << "RelocateChannel  g_wavelength == 'T'" << std::endl;
				RelocateChannelFG(ch.channelNo-1, outputs.F1_PixelPos, outputs.F2_PixelPos, outputs.FC_PixelPos);

				int total_Slots = g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][ch.channelNo].slotNum;
				for (int slot = 1; slot <= total_Slots; slot++)
				{
					// Go through all slot attenuation and if its not zero then calculate that slot attenuation and relocate that slot within the channel
					if (g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][ch.channelNo].slotsATTEN[slot-1] != 0)
					{
						double slot_ATT = g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][ch.channelNo].slotsATTEN[slot-1];
						double actual_slot_att = inputs.ch_att + slot_ATT;	// slot attenuation is relative to channel attenuation.(slot_ATT can be -ve)

						if(actual_slot_att < 0)
							actual_slot_att = 0;

						inputs.ch_att = actual_slot_att;

						status = Find_Parameters_By_Interpolation(inputs, outputs, false, false, true, false);		// Interpolate Attenuation only

						if(status != 0)
							return (-1);
						//std::cout << "\nSlot atten parameters: " <<" k_op= " << outputs.Kopt << " a_op= " << outputs.Aopt << " k_att= " << outputs.Aatt << " a_att= " << outputs.Katt << std::endl;
						Calculate_Pattern_Formulas(ch.channelNo-1, g_wavelength, g_pixelSize, outputs.sigma, outputs.Aopt, outputs.Kopt, outputs.Aatt, outputs.Katt);

						if(slot == 1) //left edge slot attenuation
						{
							//to calculate for edge attenuated value
							edgeK_Att = outputs.Katt + outputs.F1_PixelPos - floor(outputs.F1_PixelPos);
							Calculate_Optimization_And_Attenuation(outputs.Aopt, outputs.Kopt, 6.0, edgeK_Att, 0); //0:left edge
							Fill_Channel_ColumnData(ch.channelNo-1);
						}
						if(slot == total_Slots) //right edge slot attenuation
						{
							edgeK_Att = outputs.Katt + 1 - (outputs.F2_PixelPos- floor(outputs.F2_PixelPos));
							Calculate_Optimization_And_Attenuation(outputs.Aopt, outputs.Kopt, 6.0, edgeK_Att, 2); //2: right edge
							Fill_Channel_ColumnData(ch.channelNo-1);
						}
						RelocateSlot(ch.channelNo-1, slot, total_Slots, outputs.F1_PixelPos, outputs.F2_PixelPos);
					}
				}
			}
		}
		status = ChannelsContiguousTest();
//		for (int ch = 0; ch < g_Total_Channels; ch++)
		for(const auto& ch: g_serialMod->cmd_decoder.activeChannels)
		{
			if (ch.moduleNo == g_moduleNum && g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][ch.channelNo].active == true)
			{
				if(g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][ch.channelNo].F1ContiguousOrNot == 0 &&
						g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][ch.channelNo].F2ContiguousOrNot == 0)
				{
					continue;
				}
				else if(g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][ch.channelNo].F1ContiguousOrNot == 1 &&
						g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][ch.channelNo].F2ContiguousOrNot == 1)
				{//inner slot, no need of edge attenuation and edge bw-10
					inputs.ch_att = g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][ch.channelNo].ATT;
					inputs.ch_fc = g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][ch.channelNo].FC;
					inputs.ch_adp = g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][ch.channelNo].ADP-1;			// -1 because Calib Module PORT[] array starts from 0 to 22, while user gives ADP from 1 to 23
					double ch_bw = g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][ch.channelNo].BW;

					inputs.ch_f1 = inputs.ch_fc - ch_bw/2;
					inputs.ch_f2 = inputs.ch_fc + ch_bw/2;

					status = Find_Parameters_By_Interpolation(inputs, outputs, true, true, true, true);		// Interpolate all parameters

					if(status != 0)
						return (-1);

					Calculate_Pattern_Formulas(ch.channelNo-1, g_wavelength, g_pixelSize, outputs.sigma, outputs.Aopt, outputs.Kopt, outputs.Aatt, outputs.Katt);

					RelocateChannelFG(ch.channelNo-1, outputs.F1_PixelPos, outputs.F2_PixelPos, outputs.FC_PixelPos);
				}
				else if(g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][ch.channelNo].F1ContiguousOrNot == 1 &&
						g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][ch.channelNo].F2ContiguousOrNot == 0)
				{//only right edge needs to be attenuated
					inputs.ch_att = g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][ch.channelNo].ATT;
					inputs.ch_fc = g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][ch.channelNo].FC;
					inputs.ch_adp = g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][ch.channelNo].ADP-1;			// -1 because Calib Module PORT[] array starts from 0 to 22, while user gives ADP from 1 to 23
					double ch_bw = g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][ch.channelNo].BW;

					inputs.ch_f1 = inputs.ch_fc - ch_bw/2;
					inputs.ch_f2 = inputs.ch_fc + (ch_bw-10)/2;

					status = Find_Parameters_By_Interpolation(inputs, outputs, true, true, true, true);		// Interpolate all parameters

					if(status != 0)
						return (-1);

					Calculate_Pattern_Formulas(ch.channelNo-1, g_wavelength, g_pixelSize, outputs.sigma, outputs.Aopt, outputs.Kopt, outputs.Aatt, outputs.Katt);

					edgeK_Att = outputs.Katt + 1- (outputs.F2_PixelPos - floor(outputs.F2_PixelPos));

					Calculate_Optimization_And_Attenuation(outputs.Aopt, outputs.Kopt, 6.0, edgeK_Att, 2); //2: right edge
					Fill_Channel_ColumnData(ch.channelNo-1);
					RelocateChannelFG(ch.channelNo-1, outputs.F1_PixelPos, outputs.F2_PixelPos, outputs.FC_PixelPos);
				}
				else if(g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][ch.channelNo].F1ContiguousOrNot == 0 &&
						g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][ch.channelNo].F2ContiguousOrNot == 1)
				{//only left edge needs to be attenuated

					inputs.ch_att = g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][ch.channelNo].ATT;
					inputs.ch_fc = g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][ch.channelNo].FC;
					inputs.ch_adp = g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][ch.channelNo].ADP-1;			// -1 because Calib Module PORT[] array starts from 0 to 22, while user gives ADP from 1 to 23
					double ch_bw = g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][ch.channelNo].BW;

					inputs.ch_f1 = inputs.ch_fc - (ch_bw-10)/2;
					inputs.ch_f2 = inputs.ch_fc + ch_bw/2;

					status = Find_Parameters_By_Interpolation(inputs, outputs, true, true, true, true);		// Interpolate all parameters

					if(status != 0)
						return (-1);

					Calculate_Pattern_Formulas(ch.channelNo-1, g_wavelength, g_pixelSize, outputs.sigma, outputs.Aopt, outputs.Kopt, outputs.Aatt, outputs.Katt);

					edgeK_Att = outputs.Katt + outputs.F1_PixelPos - floor(outputs.F1_PixelPos);

					Calculate_Optimization_And_Attenuation(outputs.Aopt, outputs.Kopt, 6.0, edgeK_Att, 0); //0:left edge
					Fill_Channel_ColumnData(ch.channelNo-1);

					RelocateChannelFG(ch.channelNo-1, outputs.F1_PixelPos, outputs.F2_PixelPos, outputs.FC_PixelPos);
				}
			}
		}

	}

	return (0);
}
*/
#ifndef _SPI_INTERFACE_
int PatternGenModule::Calculate_Every_ChannelPattern()
{
	inputParameters inputs;
	outputParameters outputs;
	int status = 0;
	double edgeK_Att = 0.0;
	float edge_Att = 0.0, channel_Att = 0.0;

	unsigned int channelNo = 0;

	for(const auto& ch: g_serialMod->cmd_decoder.activeChannels)
	{
		channelNo = ch.channelNo;
		g_moduleNum = ch.moduleNo;

		if (g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][channelNo].active == true) //determine which data structure to use
		{
			inputs.ch_att = g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][channelNo].ATT;     //+1 because start from 1
			inputs.ch_fc = g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][channelNo].FC;
			inputs.ch_adp = g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][channelNo].ADP-1;	// -1 because Calib Module PORT[] array starts from 0 to 22, while user gives ADP from 1 to 23
			double ch_bw = g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][channelNo].BW;
			inputs.ch_cmp = g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][channelNo].CMP;

			double ch_bw_c = g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][channelNo].BW_C;  //added for 120 wl
			float  ch_att_c = g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][channelNo].ATT_C;  //added for 120 wl
			float  edg_factor = g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][channelNo].EDG_FACTOR;  //added for 120 wl

			if(inputs.ch_att > MAX_ATT_BLOCK)
				continue;      				// ATT exceeds max value, then channel is actually blocked so no need to configure channel shape
			if(g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][channelNo].F1ContiguousOrNot == 0 &&
				g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][channelNo].F2ContiguousOrNot == 0)
			{
				inputs.ch_f1 = inputs.ch_fc - (ch_bw-4)/2;
				inputs.ch_f2 = inputs.ch_fc + (ch_bw-4)/2;

				status = Find_Parameters_By_Interpolation(inputs, outputs, true, true, true, true);		// Interpolate all parameters for channel

				if(status != 0)
					return (-1);

				Calculate_Pattern_Formulas(channelNo-1, g_wavelength, g_pixelSize, outputs.sigma, outputs.Aopt, outputs.Kopt, outputs.Aatt, outputs.Katt);

				//to calculate for left edge attenuated value
				channel_Att = inputs.ch_att;

				edge_Att = 1- (outputs.F1_PixelPos - floor(outputs.F1_PixelPos)); //edge attenuation according to covered partial area of a pixel

				std::ostringstream oss;
				oss << "\01Channel:" << channelNo << " Left edge coverage:" << edge_Att <<"\04\n\r";
				std::string msg = oss.str();
				//edge_Att = 1- (outputs.F1_PixelPos - floor(outputs.F1_PixelPos));
				edge_Att = (channel_Att + abs(10*log10(edge_Att)) > MAX_ATT_BLOCK? MAX_ATT_BLOCK: channel_Att + abs(10*log10(edge_Att))); //10*log10() changes edge_Att from percentage to dB value
				inputs.ch_att = edge_Att*edg_factor;

				status = Find_Parameters_By_Interpolation(inputs, outputs, false, false, true, false);	// Interpolate K_Att parameters for edge
				edgeK_Att = outputs.Katt;

				Calculate_Optimization_And_Attenuation(outputs.Aopt, outputs.Kopt, outputs.Aatt, edgeK_Att, 0); //0:left edge


				//to calculate for right edge attenuated value
				edge_Att = outputs.F2_PixelPos - floor(outputs.F2_PixelPos);   //attenuation according to covered area

				oss << "\01Channel:" << channelNo << " Right edge coverage:" << edge_Att << "\04";
				msg = oss.str();
				g_serialMod->cmd_decoder.PrintResponse(msg, g_serialMod->cmd_decoder.PrintType::NO_ERROR);

				edge_Att = outputs.F2_PixelPos - floor(outputs.F2_PixelPos);
				edge_Att = (channel_Att + abs(10*log10(edge_Att)) > MAX_ATT_BLOCK? MAX_ATT_BLOCK: channel_Att + abs(10*log10(edge_Att)));
				inputs.ch_att = edge_Att*edg_factor;

				status = Find_Parameters_By_Interpolation(inputs, outputs, false, false, true, false);	// Interpolate K_Att parameters for edge
				edgeK_Att = outputs.Katt;

				Calculate_Optimization_And_Attenuation(outputs.Aopt, outputs.Kopt, outputs.Aatt, edgeK_Att, 2); //2: right edge



				Fill_Channel_ColumnData(channelNo-1);

				g_b120WL = false;
				RelocateChannelTF(channelNo-1, outputs.F1_PixelPos, outputs.F2_PixelPos, outputs.FC_PixelPos);

				//RelocateChannel is needed because extra attenuation is required within the closest area around fc for 120 wl
				if(ch_bw_c != 0 && ch_att_c != 0) {
					inputs.ch_f1 = inputs.ch_fc - ch_bw_c/2;
					inputs.ch_f2 = inputs.ch_fc + ch_bw_c/2;

					inputs.ch_att = channel_Att + ch_att_c;
					status = Find_Parameters_By_Interpolation(inputs, outputs, false, false, true, true);		// Interpolate att and pixelpos for extra attenuation area

					if(status != 0)
						return (-1);

					Calculate_Pattern_Formulas(channelNo-1, g_wavelength, g_pixelSize, outputs.sigma, outputs.Aopt, outputs.Kopt, outputs.Aatt, outputs.Katt);
					g_b120WL = true;
					RelocateChannelTF(channelNo-1, outputs.F1_PixelPos, outputs.F2_PixelPos, outputs.FC_PixelPos);
				}


			}
			else if(g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][channelNo].F1ContiguousOrNot == 1 &&
					g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][channelNo].F2ContiguousOrNot == 1)
			{
				inputs.ch_f1 = inputs.ch_fc - ch_bw/2;
				inputs.ch_f2 = inputs.ch_fc + ch_bw/2;

				status = Find_Parameters_By_Interpolation(inputs, outputs, true, true, true, true);		// Interpolate all parameters

				if(status != 0)
					return (-1);

				Calculate_Pattern_Formulas(channelNo-1, g_wavelength, g_pixelSize, outputs.sigma, outputs.Aopt, outputs.Kopt, outputs.Aatt, outputs.Katt);

				RelocateChannelTF(channelNo-1, outputs.F1_PixelPos, outputs.F2_PixelPos, outputs.FC_PixelPos);

			}
			else if(g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][channelNo].F1ContiguousOrNot == 1 &&
					g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][channelNo].F2ContiguousOrNot == 0)
			{
				inputs.ch_f1 = inputs.ch_fc - ch_bw/2;
				inputs.ch_f2 = inputs.ch_fc + (ch_bw-4)/2;

				status = Find_Parameters_By_Interpolation(inputs, outputs, true, true, true, true);		// Interpolate all parameters

				if(status != 0)
					return (-1);

				Calculate_Pattern_Formulas(channelNo-1, g_wavelength, g_pixelSize, outputs.sigma, outputs.Aopt, outputs.Kopt, outputs.Aatt, outputs.Katt);

				//to calculate for right edge attenuated value
				channel_Att = inputs.ch_att;
				edge_Att = outputs.F2_PixelPos - floor(outputs.F2_PixelPos);
				edge_Att = (channel_Att + abs(10*log10(edge_Att)) > MAX_ATT_BLOCK? MAX_ATT_BLOCK: channel_Att + abs(10*log10(edge_Att)));
				inputs.ch_att = edge_Att;
				status = Find_Parameters_By_Interpolation(inputs, outputs, false, false, true, false);		// Interpolate K_Att parameters for edge
				edgeK_Att = outputs.Katt;

				Calculate_Optimization_And_Attenuation(outputs.Aopt, outputs.Kopt, outputs.Aatt, edgeK_Att, 2); //2: right edge
				Fill_Channel_ColumnData(channelNo-1);
				RelocateChannelTF(channelNo-1, outputs.F1_PixelPos, outputs.F2_PixelPos, outputs.FC_PixelPos);

			}
			else if(g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][channelNo].F1ContiguousOrNot == 0 &&
					g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][channelNo].F2ContiguousOrNot == 1)
			{
				inputs.ch_f1 = inputs.ch_fc - (ch_bw-4)/2;
				inputs.ch_f2 = inputs.ch_fc + ch_bw/2;

				status = Find_Parameters_By_Interpolation(inputs, outputs, true, true, true, true);		// Interpolate all parameters

				if(status != 0)
					return (-1);

				Calculate_Pattern_Formulas(channelNo-1, g_wavelength, g_pixelSize, outputs.sigma, outputs.Aopt, outputs.Kopt, outputs.Aatt, outputs.Katt);

				//to calculate for left edge attenuated value
				channel_Att = inputs.ch_att;

				edge_Att = 1 - (outputs.F1_PixelPos - floor(outputs.F1_PixelPos));
				edge_Att = (channel_Att + abs(10*log10(edge_Att)) > MAX_ATT_BLOCK? MAX_ATT_BLOCK: channel_Att + abs(10*log10(edge_Att)));     //10*log10() changes edge_Att from percentage to dB value
				inputs.ch_att = edge_Att;
				status = Find_Parameters_By_Interpolation(inputs, outputs, false, false, true, false);		// Interpolate K_Att parameters for edge
				edgeK_Att = outputs.Katt;

				Calculate_Optimization_And_Attenuation(outputs.Aopt, outputs.Kopt, outputs.Aatt, edgeK_Att, 0); //0:left edge
				Fill_Channel_ColumnData(channelNo-1);

				RelocateChannelTF(channelNo-1, outputs.F1_PixelPos, outputs.F2_PixelPos, outputs.FC_PixelPos);
			}
			else
			{
				return (-1);
			}

		}
		else if (g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][channelNo].active == true)
		{
			inputs.ch_att = g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][channelNo].ATT;
			inputs.ch_fc = g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][channelNo].FC;
			inputs.ch_adp = g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][channelNo].ADP-1;
			inputs.ch_cmp = g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][channelNo].CMP;
			inputs.ch_f1 = g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][channelNo].F1;
			inputs.ch_f2 = g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][channelNo].F2;
			double ch_bw = g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][channelNo].BW;

			if(inputs.ch_att > MAX_ATT_BLOCK)
				continue;      			// ATT exceeds max value, then channel is actually blocked so no need to configure channel shape
			if(g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][channelNo].F1ContiguousOrNot == 0 &&
				g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][channelNo].F2ContiguousOrNot == 0)
			{
				inputs.ch_f1 = inputs.ch_fc - (ch_bw-4)/2;
				inputs.ch_f2 = inputs.ch_fc + (ch_bw-4)/2;

				status = Find_Parameters_By_Interpolation(inputs, outputs, true, true, true, true);		// Interpolate all parameters

				if(status != 0)
					return (-1);

				Calculate_Pattern_Formulas(channelNo-1, g_wavelength, g_pixelSize, outputs.sigma, outputs.Aopt, outputs.Kopt, outputs.Aatt, outputs.Katt);

				//to calculate for edge attenuated value

				//to calculate for left edge attenuated value
				channel_Att = inputs.ch_att;

				edge_Att = 1 - (outputs.F1_PixelPos - floor(outputs.F1_PixelPos));
				edge_Att = (channel_Att + abs(10*log10(edge_Att)) > MAX_ATT_BLOCK? MAX_ATT_BLOCK: channel_Att + abs(10*log10(edge_Att)));     //10*log10() changes edge_Att from percentage to dB value
				inputs.ch_att = edge_Att;
				status = Find_Parameters_By_Interpolation(inputs, outputs, false, false, true, false);		// Interpolate K_Att parameters for edge
				edgeK_Att = outputs.Katt;

				Calculate_Optimization_And_Attenuation(outputs.Aopt, outputs.Kopt, outputs.Aatt, edgeK_Att, 0); //0:left edge
//				Fill_Channel_ColumnData(channelNo-1);

				//to calculate for right edge attenuated value

				edge_Att = outputs.F2_PixelPos - floor(outputs.F2_PixelPos);
				edge_Att = (channel_Att + abs(10*log10(edge_Att)) > MAX_ATT_BLOCK? MAX_ATT_BLOCK: channel_Att + abs(10*log10(edge_Att)));
				inputs.ch_att = edge_Att;
				status = Find_Parameters_By_Interpolation(inputs, outputs, false, false, true, false);		// Interpolate K_Att parameters for edge
				edgeK_Att = outputs.Katt;

				Calculate_Optimization_And_Attenuation(outputs.Aopt, outputs.Kopt, outputs.Aatt, edgeK_Att, 2); //2: right edge
				Fill_Channel_ColumnData(channelNo-1);

				RelocateChannelFG(channelNo-1, outputs.F1_PixelPos, outputs.F2_PixelPos, outputs.FC_PixelPos);
			}
			else if(g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][channelNo].F1ContiguousOrNot == 1 &&
					g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][channelNo].F2ContiguousOrNot == 1)
			{//inner slot, no need of edge attenuation and edge bw-10

				inputs.ch_f1 = inputs.ch_fc - ch_bw/2;
				inputs.ch_f2 = inputs.ch_fc + ch_bw/2;

				status = Find_Parameters_By_Interpolation(inputs, outputs, true, true, true, true);		// Interpolate all parameters

				if(status != 0)
					return (-1);

				Calculate_Pattern_Formulas(channelNo-1, g_wavelength, g_pixelSize, outputs.sigma, outputs.Aopt, outputs.Kopt, outputs.Aatt, outputs.Katt);

				RelocateChannelFG(channelNo-1, outputs.F1_PixelPos, outputs.F2_PixelPos, outputs.FC_PixelPos);
			}
			else if(g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][channelNo].F1ContiguousOrNot == 1 &&
					g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][channelNo].F2ContiguousOrNot == 0)
			{//only right edge needs to be attenuated

				inputs.ch_f1 = inputs.ch_fc - ch_bw/2;
				inputs.ch_f2 = inputs.ch_fc + (ch_bw-4)/2;

				status = Find_Parameters_By_Interpolation(inputs, outputs, true, true, true, true);		// Interpolate all parameters

				if(status != 0)
					return (-1);

				Calculate_Pattern_Formulas(channelNo-1, g_wavelength, g_pixelSize, outputs.sigma, outputs.Aopt, outputs.Kopt, outputs.Aatt, outputs.Katt);

				//to calculate for right edge attenuated value
				channel_Att = inputs.ch_att;

				edge_Att = outputs.F2_PixelPos - floor(outputs.F2_PixelPos);
				edge_Att = (channel_Att + abs(10*log10(edge_Att)) > MAX_ATT_BLOCK? MAX_ATT_BLOCK : channel_Att + abs(10*log10(edge_Att)));
				inputs.ch_att = edge_Att;
				status = Find_Parameters_By_Interpolation(inputs, outputs, false, false, true, false);		// Interpolate K_Att parameters for edge
				edgeK_Att = outputs.Katt;

				Calculate_Optimization_And_Attenuation(outputs.Aopt, outputs.Kopt, outputs.Aatt, edgeK_Att, 2); //2: right edge
				Fill_Channel_ColumnData(channelNo-1);
				RelocateChannelFG(channelNo-1, outputs.F1_PixelPos, outputs.F2_PixelPos, outputs.FC_PixelPos);
			}
			else if(g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][channelNo].F1ContiguousOrNot == 0 &&
					g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][channelNo].F2ContiguousOrNot == 1)
			{//only left edge needs to be attenuated

				inputs.ch_f1 = inputs.ch_fc - (ch_bw-4)/2;
				inputs.ch_f2 = inputs.ch_fc + ch_bw/2;

				status = Find_Parameters_By_Interpolation(inputs, outputs, true, true, true, true);		// Interpolate all parameters

				if(status != 0)
					return (-1);

				Calculate_Pattern_Formulas(channelNo-1, g_wavelength, g_pixelSize, outputs.sigma, outputs.Aopt, outputs.Kopt, outputs.Aatt, outputs.Katt);

				//to calculate for left edge attenuated value
				channel_Att = inputs.ch_att;

				edge_Att = 1-(outputs.F1_PixelPos - floor(outputs.F1_PixelPos));
				edge_Att = (channel_Att + abs(10*log10(edge_Att)) > 15? MAX_ATT_BLOCK: channel_Att + abs(10*log10(edge_Att)));     //10*log10() changes edge_Att from percentage to dB value
				inputs.ch_att = edge_Att;
				status = Find_Parameters_By_Interpolation(inputs, outputs, false, false, true, false);		// Interpolate K_Att parameters for edge
				edgeK_Att = outputs.Katt;

				Calculate_Optimization_And_Attenuation(outputs.Aopt, outputs.Kopt, outputs.Aatt, edgeK_Att, 0); //0:left edge
				Fill_Channel_ColumnData(channelNo-1);

				RelocateChannelFG(channelNo-1, outputs.F1_PixelPos, outputs.F2_PixelPos, outputs.FC_PixelPos);
			}

			int total_Slots = g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][channelNo].slotNum;
			for (int slot = 1; slot <= total_Slots; slot++)
			{
				// Go through all slot attenuation and if its not zero then calculate that slot attenuation and relocate that slot within the channel
				if (g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][channelNo].slotsATTEN[slot-1] != 0)
				{
					double slot_ATT = g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][channelNo].slotsATTEN[slot-1];
					double actual_slot_att = channel_Att + slot_ATT;	// slot attenuation is relative to channel attenuation.(slot_ATT can be -ve)

					if(actual_slot_att < 0)
						actual_slot_att = 0;
					if(actual_slot_att > MAX_ATT_BLOCK)
					{
						g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][channelNo].slotBlockedOrNot.resize(total_Slots);
						std::vector<int>::iterator it = g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][channelNo].slotBlockedOrNot.begin();
						g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][channelNo].slotBlockedOrNot.insert(it+slot, 1);
						RelocateSlot(channelNo-1, slot, total_Slots, outputs.F1_PixelPos, outputs.F2_PixelPos);

						continue;
					}

					inputs.ch_att = actual_slot_att;

					status = Find_Parameters_By_Interpolation(inputs, outputs, false, false, true, false);		// Interpolate Attenuation only

					if(status != 0)
						return (-1);

					Calculate_Pattern_Formulas(channelNo-1, g_wavelength, g_pixelSize, outputs.sigma, outputs.Aopt, outputs.Kopt, outputs.Aatt, outputs.Katt);

					if(slot == 1) //left edge slot attenuation
					{
						//to calculate for edge attenuated value
						//to calculate for left edge attenuated value
						channel_Att = actual_slot_att;

						edge_Att =1 - (outputs.F1_PixelPos - floor(outputs.F1_PixelPos));
						edge_Att = (channel_Att + abs(10*log10(edge_Att)) > 15? 15: channel_Att + abs(10*log10(edge_Att)));     //10*log10() changes edge_Att from percentage to dB value
						inputs.ch_att = edge_Att;
						status = Find_Parameters_By_Interpolation(inputs, outputs, false, false, true, false);		// Interpolate K_Att parameters for edge
						edgeK_Att = outputs.Katt;
						Calculate_Optimization_And_Attenuation(outputs.Aopt, outputs.Kopt, outputs.Aatt, edgeK_Att, 0); //0:left edge
						Fill_Channel_ColumnData(channelNo-1);
					}
					if(slot == total_Slots) //right edge slot attenuation
					{
						channel_Att = actual_slot_att;
						edge_Att = outputs.F2_PixelPos - floor(outputs.F2_PixelPos);
						edge_Att = (channel_Att + abs(10*log10(edge_Att)) > 15? 15: channel_Att + abs(10*log10(edge_Att)));
						inputs.ch_att = edge_Att;
						status = Find_Parameters_By_Interpolation(inputs, outputs, false, false, true, false);		// Interpolate K_Att parameters for edge
						edgeK_Att = outputs.Katt;
						Calculate_Optimization_And_Attenuation(outputs.Aopt, outputs.Kopt, outputs.Aatt, edgeK_Att, 2); //2: right edge
						Fill_Channel_ColumnData(channelNo-1);
					}
					RelocateSlot(channelNo-1, slot, total_Slots, outputs.F1_PixelPos, outputs.F2_PixelPos);
				}
			}
		}
	}

	return (0);
}
#endif

#ifdef _SPI_INTERFACE_
int PatternGenModule::Calculate_Every_ChannelPattern()
{
	inputParameters inputs;
	outputParameters outputs;
	int status = 0;
	double edgeK_Att = 0.0;
	float edge_Att = 0.0, channel_Att = 0.0;

	unsigned int channelNo = 0;

	for(const auto& ch: g_spaCmd->g_cmdDecoder->activeChannels)
	{
		channelNo = ch.channelNo;
		g_moduleNum = ch.moduleNo;

		if (g_spaCmd->g_cmdDecoder->TF_Channel_DS_For_Pattern[g_moduleNum][channelNo].active == true) //determine which data structure to use
		{
			inputs.ch_att = g_spaCmd->g_cmdDecoder->TF_Channel_DS_For_Pattern[g_moduleNum][channelNo].ATT;       //+1 because start from 1
			inputs.ch_fc = g_spaCmd->g_cmdDecoder->TF_Channel_DS_For_Pattern[g_moduleNum][channelNo].FC;
			inputs.ch_adp = g_spaCmd->g_cmdDecoder->TF_Channel_DS_For_Pattern[g_moduleNum][channelNo].ADP-1;			// -1 because Calib Module PORT[] array starts from 0 to 22, while user gives ADP from 1 to 23
			double ch_bw = g_spaCmd->g_cmdDecoder->TF_Channel_DS_For_Pattern[g_moduleNum][channelNo].BW;
			inputs.ch_cmp = g_spaCmd->g_cmdDecoder->TF_Channel_DS_For_Pattern[g_moduleNum][channelNo].CMP;

			if(inputs.ch_att > MAX_ATT_BLOCK)
				continue;      				// ATT exceeds max value, then channel is actually blocked so no need to configure channel shape
			if(g_spaCmd->g_cmdDecoder->TF_Channel_DS_For_Pattern[g_moduleNum][channelNo].F1ContiguousOrNot == 0 &&
				g_spaCmd->g_cmdDecoder->TF_Channel_DS_For_Pattern[g_moduleNum][channelNo].F2ContiguousOrNot == 0)
			{
				inputs.ch_f1 = inputs.ch_fc - (ch_bw-4)/2;
				inputs.ch_f2 = inputs.ch_fc + (ch_bw-4)/2;

				status = Find_Parameters_By_Interpolation(inputs, outputs, true, true, true, true);		// Interpolate all parameters for channel

				if(status != 0)
					return (-1);

				Calculate_Pattern_Formulas(channelNo-1, g_wavelength, g_pixelSize, outputs.sigma, outputs.Aopt, outputs.Kopt, outputs.Aatt, outputs.Katt);
	//				std::cout << "***Product Mode Channel Sigma:"<< outputs.sigma << " K_Att:" << outputs.Katt << std::endl;

				//to calculate for left edge attenuated value
				channel_Att = inputs.ch_att;

				edge_Att = 1- (outputs.F1_PixelPos - floor(outputs.F1_PixelPos)); //edge attenuation according to covered area
	//				std::cout << "Left Edge Position:" << outputs.F1_PixelPos << std::endl;
				edge_Att = 1- (outputs.F1_PixelPos - floor(outputs.F1_PixelPos));
				edge_Att = (channel_Att + abs(10*log10(edge_Att)) > MAX_ATT_BLOCK? MAX_ATT_BLOCK: channel_Att + abs(10*log10(edge_Att)));     //10*log10() changes edge_Att from percentage to dB value
				inputs.ch_att = edge_Att;

				status = Find_Parameters_By_Interpolation(inputs, outputs, false, false, true, false);		// Interpolate K_Att parameters for edge
				edgeK_Att = outputs.Katt;
	//				std::cout << "Product Mode Left Edge K_Att:" << outputs.Katt << std::endl;
				Calculate_Optimization_And_Attenuation(outputs.Aopt, outputs.Kopt,  outputs.Aatt, edgeK_Att, 0); //0:left edge //2* is because experiments show this will help improve fc accuracy
	//				Fill_Channel_ColumnData(channelNo-1);

				//to calculate for right edge attenuated value
				edge_Att = outputs.F2_PixelPos - floor(outputs.F2_PixelPos);   //attenuation according to covered area
	//				std::cout << "Right Edge Position:" << outputs.F2_PixelPos << std::endl;
				edge_Att = outputs.F2_PixelPos - floor(outputs.F2_PixelPos);
				edge_Att = (channel_Att + abs(10*log10(edge_Att)) > MAX_ATT_BLOCK? MAX_ATT_BLOCK: channel_Att + abs(10*log10(edge_Att)));
				inputs.ch_att = edge_Att;
				status = Find_Parameters_By_Interpolation(inputs, outputs, false, false, true, false);		// Interpolate K_Att parameters for edge
				edgeK_Att = outputs.Katt;
	//				std::cout << "Product Mode Right Edge K_Att:" << outputs.Katt << std::endl;
				Calculate_Optimization_And_Attenuation(outputs.Aopt, outputs.Kopt,  outputs.Aatt, edgeK_Att, 2); //2: right edge
				Fill_Channel_ColumnData(channelNo-1);

				RelocateChannelTF(channelNo-1, outputs.F1_PixelPos, outputs.F2_PixelPos, outputs.FC_PixelPos);
			}
			else if(g_spaCmd->g_cmdDecoder->TF_Channel_DS_For_Pattern[g_moduleNum][channelNo].F1ContiguousOrNot == 1 &&
					g_spaCmd->g_cmdDecoder->TF_Channel_DS_For_Pattern[g_moduleNum][channelNo].F2ContiguousOrNot == 1)
			{
				inputs.ch_f1 = inputs.ch_fc - ch_bw/2;
				inputs.ch_f2 = inputs.ch_fc + ch_bw/2;

				status = Find_Parameters_By_Interpolation(inputs, outputs, true, true, true, true);		// Interpolate all parameters

				if(status != 0)
					return (-1);

				Calculate_Pattern_Formulas(channelNo-1, g_wavelength, g_pixelSize, outputs.sigma, outputs.Aopt, outputs.Kopt, outputs.Aatt, outputs.Katt);

				RelocateChannelTF(channelNo-1, outputs.F1_PixelPos, outputs.F2_PixelPos, outputs.FC_PixelPos);

			}
			else if(g_spaCmd->g_cmdDecoder->TF_Channel_DS_For_Pattern[g_moduleNum][channelNo].F1ContiguousOrNot == 1 &&
					g_spaCmd->g_cmdDecoder->TF_Channel_DS_For_Pattern[g_moduleNum][channelNo].F2ContiguousOrNot == 0)
			{
				inputs.ch_f1 = inputs.ch_fc - ch_bw/2;
				inputs.ch_f2 = inputs.ch_fc + (ch_bw-4)/2;

				status = Find_Parameters_By_Interpolation(inputs, outputs, true, true, true, true);		// Interpolate all parameters

				if(status != 0)
					return (-1);

				Calculate_Pattern_Formulas(channelNo-1, g_wavelength, g_pixelSize, outputs.sigma, outputs.Aopt, outputs.Kopt, outputs.Aatt, outputs.Katt);

				//to calculate for right edge attenuated value
				channel_Att = inputs.ch_att;
				edge_Att = outputs.F2_PixelPos - floor(outputs.F2_PixelPos);
				edge_Att = (channel_Att + abs(10*log10(edge_Att)) > MAX_ATT_BLOCK? MAX_ATT_BLOCK: channel_Att + abs(10*log10(edge_Att)));
				inputs.ch_att = edge_Att;
				status = Find_Parameters_By_Interpolation(inputs, outputs, false, false, true, false);		// Interpolate K_Att parameters for edge
				edgeK_Att = outputs.Katt;

				Calculate_Optimization_And_Attenuation(outputs.Aopt, outputs.Kopt,  outputs.Aatt, edgeK_Att, 2); //2: right edge
				Fill_Channel_ColumnData(channelNo-1);
				RelocateChannelTF(channelNo-1, outputs.F1_PixelPos, outputs.F2_PixelPos, outputs.FC_PixelPos);

			}
			else if(g_spaCmd->g_cmdDecoder->TF_Channel_DS_For_Pattern[g_moduleNum][channelNo].F1ContiguousOrNot == 0 &&
					g_spaCmd->g_cmdDecoder->TF_Channel_DS_For_Pattern[g_moduleNum][channelNo].F2ContiguousOrNot == 1)
			{
				inputs.ch_f1 = inputs.ch_fc - (ch_bw-4)/2;
				inputs.ch_f2 = inputs.ch_fc + ch_bw/2;

				status = Find_Parameters_By_Interpolation(inputs, outputs, true, true, true, true);		// Interpolate all parameters

				if(status != 0)
					return (-1);

				Calculate_Pattern_Formulas(channelNo-1, g_wavelength, g_pixelSize, outputs.sigma, outputs.Aopt, outputs.Kopt, outputs.Aatt, outputs.Katt);

				//to calculate for left edge attenuated value
				channel_Att = inputs.ch_att;

				edge_Att = 1 - (outputs.F1_PixelPos - floor(outputs.F1_PixelPos));
				edge_Att = (channel_Att + abs(10*log10(edge_Att)) > MAX_ATT_BLOCK? MAX_ATT_BLOCK: channel_Att + abs(10*log10(edge_Att)));     //10*log10() changes edge_Att from percentage to dB value
				inputs.ch_att = edge_Att;
				status = Find_Parameters_By_Interpolation(inputs, outputs, false, false, true, false);		// Interpolate K_Att parameters for edge
				edgeK_Att = outputs.Katt;

				Calculate_Optimization_And_Attenuation(outputs.Aopt, outputs.Kopt,  outputs.Aatt, edgeK_Att, 0); //0:left edge
				Fill_Channel_ColumnData(channelNo-1);

				RelocateChannelTF(channelNo-1, outputs.F1_PixelPos, outputs.F2_PixelPos, outputs.FC_PixelPos);
			}
			else
			{
				return (-1);
			}

		}
		else if (g_spaCmd->g_cmdDecoder->FG_Channel_DS_For_Pattern[g_moduleNum][channelNo].active == true)
		{
			std::cout << "SPI command pattern generation started." << std::endl;
			inputs.ch_att = g_spaCmd->g_cmdDecoder->FG_Channel_DS_For_Pattern[g_moduleNum][channelNo].ATT;
			inputs.ch_fc = g_spaCmd->g_cmdDecoder->FG_Channel_DS_For_Pattern[g_moduleNum][channelNo].FC;
			inputs.ch_adp = g_spaCmd->g_cmdDecoder->FG_Channel_DS_For_Pattern[g_moduleNum][channelNo].ADP-1;
			inputs.ch_cmp = g_spaCmd->g_cmdDecoder->FG_Channel_DS_For_Pattern[g_moduleNum][channelNo].CMP;
			inputs.ch_f1 = g_spaCmd->g_cmdDecoder->FG_Channel_DS_For_Pattern[g_moduleNum][channelNo].F1;
			inputs.ch_f2 = g_spaCmd->g_cmdDecoder->FG_Channel_DS_For_Pattern[g_moduleNum][channelNo].F2;
			double ch_bw = g_spaCmd->g_cmdDecoder->FG_Channel_DS_For_Pattern[g_moduleNum][channelNo].BW;

			if(inputs.ch_att > MAX_ATT_BLOCK)
				continue;      			// ATT exceeds max value, then channel is actually blocked so no need to configure channel shape
			if(g_spaCmd->g_cmdDecoder->FG_Channel_DS_For_Pattern[g_moduleNum][channelNo].F1ContiguousOrNot == 0 &&
				g_spaCmd->g_cmdDecoder->FG_Channel_DS_For_Pattern[g_moduleNum][channelNo].F2ContiguousOrNot == 0)
			{
				inputs.ch_f1 = inputs.ch_fc - (ch_bw-4)/2;
				inputs.ch_f2 = inputs.ch_fc + (ch_bw-4)/2;

				status = Find_Parameters_By_Interpolation(inputs, outputs, true, true, true, true);		// Interpolate all parameters

				if(status != 0) {
					std::cerr << "Find_Parameters_By_Interpolation Failed..." << std::endl;
					return (-1);
				}

				Calculate_Pattern_Formulas(channelNo-1, g_wavelength, g_pixelSize, outputs.sigma, outputs.Aopt, outputs.Kopt, outputs.Aatt, outputs.Katt);

				//to calculate for edge attenuated value

				//to calculate for left edge attenuated value
				channel_Att = inputs.ch_att;

				edge_Att = 1 - (outputs.F1_PixelPos - floor(outputs.F1_PixelPos));
				edge_Att = (channel_Att + abs(10*log10(edge_Att)) > MAX_ATT_BLOCK? MAX_ATT_BLOCK: channel_Att + abs(10*log10(edge_Att)));     //10*log10() changes edge_Att from percentage to dB value
				inputs.ch_att = edge_Att;
				status = Find_Parameters_By_Interpolation(inputs, outputs, false, false, true, false);		// Interpolate K_Att parameters for edge
				edgeK_Att = outputs.Katt;

				Calculate_Optimization_And_Attenuation(outputs.Aopt, outputs.Kopt,  outputs.Aatt, edgeK_Att, 0); //0:left edge
	//				Fill_Channel_ColumnData(channelNo-1);

				//to calculate for right edge attenuated value

				edge_Att = outputs.F2_PixelPos - floor(outputs.F2_PixelPos);
				edge_Att = (channel_Att + abs(10*log10(edge_Att)) > MAX_ATT_BLOCK? MAX_ATT_BLOCK: channel_Att + abs(10*log10(edge_Att)));
				inputs.ch_att = edge_Att;
				status = Find_Parameters_By_Interpolation(inputs, outputs, false, false, true, false);		// Interpolate K_Att parameters for edge
				edgeK_Att = outputs.Katt;

				Calculate_Optimization_And_Attenuation(outputs.Aopt, outputs.Kopt,  outputs.Aatt, edgeK_Att, 2); //2: right edge
				Fill_Channel_ColumnData(channelNo-1);

				RelocateChannelFG_SPI(channelNo-1, outputs.F1_PixelPos, outputs.F2_PixelPos, outputs.FC_PixelPos);
			}
			else if(g_spaCmd->g_cmdDecoder->FG_Channel_DS_For_Pattern[g_moduleNum][channelNo].F1ContiguousOrNot == 1 &&
					g_spaCmd->g_cmdDecoder->FG_Channel_DS_For_Pattern[g_moduleNum][channelNo].F2ContiguousOrNot == 1)
			{//inner slot, no need of edge attenuation and edge bw-10

				inputs.ch_f1 = inputs.ch_fc - ch_bw/2;
				inputs.ch_f2 = inputs.ch_fc + ch_bw/2;

				status = Find_Parameters_By_Interpolation(inputs, outputs, true, true, true, true);		// Interpolate all parameters

				if(status != 0)
					return (-1);

				Calculate_Pattern_Formulas(channelNo-1, g_wavelength, g_pixelSize, outputs.sigma, outputs.Aopt, outputs.Kopt, outputs.Aatt, outputs.Katt);

				RelocateChannelFG_SPI(channelNo-1, outputs.F1_PixelPos, outputs.F2_PixelPos, outputs.FC_PixelPos);
			}
			else if(g_spaCmd->g_cmdDecoder->FG_Channel_DS_For_Pattern[g_moduleNum][channelNo].F1ContiguousOrNot == 1 &&
					g_spaCmd->g_cmdDecoder->FG_Channel_DS_For_Pattern[g_moduleNum][channelNo].F2ContiguousOrNot == 0)
			{//only right edge needs to be attenuated

				inputs.ch_f1 = inputs.ch_fc - ch_bw/2;
				inputs.ch_f2 = inputs.ch_fc + (ch_bw-4)/2;

				status = Find_Parameters_By_Interpolation(inputs, outputs, true, true, true, true);		// Interpolate all parameters

				if(status != 0)
					return (-1);

				Calculate_Pattern_Formulas(channelNo-1, g_wavelength, g_pixelSize, outputs.sigma, outputs.Aopt, outputs.Kopt, outputs.Aatt, outputs.Katt);

				//to calculate for right edge attenuated value
				channel_Att = inputs.ch_att;

				edge_Att = outputs.F2_PixelPos - floor(outputs.F2_PixelPos);
				edge_Att = (channel_Att + abs(10*log10(edge_Att)) > MAX_ATT_BLOCK? MAX_ATT_BLOCK : channel_Att + abs(10*log10(edge_Att)));
				inputs.ch_att = edge_Att;
				status = Find_Parameters_By_Interpolation(inputs, outputs, false, false, true, false);		// Interpolate K_Att parameters for edge
				edgeK_Att = outputs.Katt;

				Calculate_Optimization_And_Attenuation(outputs.Aopt, outputs.Kopt,  outputs.Aatt, edgeK_Att, 2); //2: right edge
				Fill_Channel_ColumnData(channelNo-1);
				RelocateChannelFG_SPI(channelNo-1, outputs.F1_PixelPos, outputs.F2_PixelPos, outputs.FC_PixelPos);
			}
			else if(g_spaCmd->g_cmdDecoder->FG_Channel_DS_For_Pattern[g_moduleNum][channelNo].F1ContiguousOrNot == 0 &&
					g_spaCmd->g_cmdDecoder->FG_Channel_DS_For_Pattern[g_moduleNum][channelNo].F2ContiguousOrNot == 1)
			{//only left edge needs to be attenuated

				inputs.ch_f1 = inputs.ch_fc - (ch_bw-4)/2;
				inputs.ch_f2 = inputs.ch_fc + ch_bw/2;

				status = Find_Parameters_By_Interpolation(inputs, outputs, true, true, true, true);		// Interpolate all parameters

				if(status != 0)
					return (-1);

				Calculate_Pattern_Formulas(channelNo-1, g_wavelength, g_pixelSize, outputs.sigma, outputs.Aopt, outputs.Kopt, outputs.Aatt, outputs.Katt);

				//to calculate for left edge attenuated value
				channel_Att = inputs.ch_att;

				edge_Att = 1-(outputs.F1_PixelPos - floor(outputs.F1_PixelPos));
				edge_Att = (channel_Att + abs(10*log10(edge_Att)) > 15? MAX_ATT_BLOCK: channel_Att + abs(10*log10(edge_Att)));     //10*log10() changes edge_Att from percentage to dB value
				inputs.ch_att = edge_Att;
				status = Find_Parameters_By_Interpolation(inputs, outputs, false, false, true, false);		// Interpolate K_Att parameters for edge
				edgeK_Att = outputs.Katt;

				Calculate_Optimization_And_Attenuation(outputs.Aopt, outputs.Kopt,  outputs.Aatt, edgeK_Att, 0); //0:left edge
				Fill_Channel_ColumnData(channelNo-1);

				RelocateChannelFG_SPI(channelNo-1, outputs.F1_PixelPos, outputs.F2_PixelPos, outputs.FC_PixelPos);
			}

			int total_Slots = g_spaCmd->g_cmdDecoder->FG_Channel_DS_For_Pattern[g_moduleNum][channelNo].slotNum;
			/*for (int slot = 1; slot <= total_Slots; slot++)
			{
				// Go through all slot attenuation and if its not zero then calculate that slot attenuation and relocate that slot within the channel
				if (g_spaCmd->g_cmdDecoder->FG_Channel_DS_For_Pattern[g_moduleNum][channelNo].slotsATTEN[slot-1] != 0)
				{
					double slot_ATT = g_spaCmd->g_cmdDecoder->FG_Channel_DS_For_Pattern[g_moduleNum][channelNo].slotsATTEN[slot-1];
					double actual_slot_att = channel_Att + slot_ATT;	// slot attenuation is relative to channel attenuation.(slot_ATT can be -ve)

					if(actual_slot_att < 0)
						actual_slot_att = 0;
					if(actual_slot_att > MAX_ATT_BLOCK)
					{
						g_spaCmd->g_cmdDecoder->FG_Channel_DS_For_Pattern[g_moduleNum][channelNo].slotBlockedOrNot.resize(total_Slots);
						std::vector<int>::iterator it = g_spaCmd->g_cmdDecoder->FG_Channel_DS_For_Pattern[g_moduleNum][channelNo].slotBlockedOrNot.begin();
						g_spaCmd->g_cmdDecoder->FG_Channel_DS_For_Pattern[g_moduleNum][channelNo].slotBlockedOrNot.insert(it+slot, 1);
						RelocateSlot(channelNo-1, slot, total_Slots, outputs.F1_PixelPos, outputs.F2_PixelPos);

						continue;
					}

					inputs.ch_att = actual_slot_att;

					status = Find_Parameters_By_Interpolation(inputs, outputs, false, false, true, false);		// Interpolate Attenuation only

					if(status != 0)
						return (-1);

					Calculate_Pattern_Formulas(channelNo-1, g_wavelength, g_pixelSize, outputs.sigma, outputs.Aopt, outputs.Kopt, outputs.Aatt, outputs.Katt);

					if(slot == 1) //left edge slot attenuation
					{
						//to calculate for edge attenuated value
						//to calculate for left edge attenuated value
						channel_Att = actual_slot_att;

						edge_Att =1 - (outputs.F1_PixelPos - floor(outputs.F1_PixelPos));
						edge_Att = (channel_Att + abs(10*log10(edge_Att)) > 15? 15: channel_Att + abs(10*log10(edge_Att)));     //10*log10() changes edge_Att from percentage to dB value
						inputs.ch_att = edge_Att;
						status = Find_Parameters_By_Interpolation(inputs, outputs, false, false, true, false);		// Interpolate K_Att parameters for edge
						edgeK_Att = outputs.Katt;
						Calculate_Optimization_And_Attenuation(outputs.Aopt, outputs.Kopt, 6.0, edgeK_Att, 0); //0:left edge
						Fill_Channel_ColumnData(channelNo-1);
					}
					if(slot == total_Slots) //right edge slot attenuation
					{
						channel_Att = actual_slot_att;
						edge_Att = outputs.F2_PixelPos - floor(outputs.F2_PixelPos);
						edge_Att = (channel_Att + abs(10*log10(edge_Att)) > 15? 15: channel_Att + abs(10*log10(edge_Att)));
						inputs.ch_att = edge_Att;
						status = Find_Parameters_By_Interpolation(inputs, outputs, false, false, true, false);		// Interpolate K_Att parameters for edge
						edgeK_Att = outputs.Katt;
						Calculate_Optimization_And_Attenuation(outputs.Aopt, outputs.Kopt, 6.0, edgeK_Att, 2); //2: right edge
						Fill_Channel_ColumnData(channelNo-1);
					}
					RelocateSlot(channelNo-1, slot, total_Slots, outputs.F1_PixelPos, outputs.F2_PixelPos);
				}
			}*/
		}
}

    return (0);
}
#endif

void PatternGenModule::rotateArray(double angle, int width, int height)
{
	std::cout << "rotating .." << std::endl;
    double rad = angle * M_PI / 180.0;
    double cos_theta = cos(rad);
    double sin_theta = sin(rad);


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
            	rotated[g_LCOS_Width*y + x] = fullPatternData[g_LCOS_Width*newy + newx];

            }

        }
    }

    memcpy(fullPatternData, rotated, sizeof(rotated));

    std::cout << "rotating finished .." << std::endl;

}

int PatternGenModule::Calculate_Every_ChannelPattern_DevelopMode()
{
#ifdef _DEVELOPMENT_MODE_

//	double edgeK_Att;

	int ch = 0;
//		for (int ch = 0; ch < g_Total_Channels; ch++)
	for(const auto& channel : g_serialMod->cmd_decoder.activeChannels)
	{
		g_moduleNum = channel.moduleNo;
		ch = channel.channelNo;
		if (g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][ch].active == true)
		{
			double sigma =g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][ch].SIGMA;
			double Aopt = g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][ch].A_OPP;
			double Kopt = g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][ch].K_OPP;
			double Aatt = g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][ch].A_ATT;
			double Katt = g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][ch].K_ATT;
			double wavelength = g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][ch].LAMDA;
			double ch_fc = g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][ch].FC;
			double ch_bw = g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][ch].BW;
			double ch_f1 = ch_fc - (ch_bw-4)/2;
			double ch_f2 = ch_fc + (ch_bw-4)/2;

			double F1_PixelPos, F2_PixelPos, FC_PixelPos;

			Calculate_Pattern_Formulas(ch-1, wavelength, g_pixelSize, sigma, Aopt, Kopt, Aatt, Katt);

			Find_LinearPixelPos_DevelopMode(ch_f1, F1_PixelPos);
			Find_LinearPixelPos_DevelopMode(ch_f2, F2_PixelPos);
			Find_LinearPixelPos_DevelopMode(ch_fc, FC_PixelPos);

			//to calculate for edge attenuated value for test
			//No need anymore because Katt cannot equal to pixel coverage in percentage.

//			edgeK_Att = Katt + 1- (F1_PixelPos - floor(F1_PixelPos));
//			std::cout << "Develop Mode Left Edge K_Att:" << edgeK_Att << std::endl;
//			std::ostringstream oss;
//			oss << "\01Channel:" << ch << "Left edge coverage:" << edgeK_Att- Katt <<"\04\n\r";
//			std::string msg = oss.str();

//			Aatt = g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][ch].A_ATT_L;
//			Katt = g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][ch].K_ATT_L;
//			if(Katt == 0)
//				Calculate_Optimization_And_Attenuation(Aopt, Kopt, 6.0, 2*edgeK_Att, 0); //0:left edge
//			else
//				Calculate_Optimization_And_Attenuation(Aopt, Kopt, Aatt, Katt, 0); //0:left edge attenuation for test in Trueflex only

//			Aatt = g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][ch].A_ATT_R;
//			Katt = g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][ch].K_ATT_R;

//			edgeK_Att = Katt + (F2_PixelPos - floor(F2_PixelPos));
//			std::cout << "Develop Mode Right Edge K_Att:" << edgeK_Att << std::endl;
//			oss << "\01Channel:" << ch << "Right edge coverage:" << edgeK_Att-Katt << "\04";
//			msg = oss.str();
//			g_serialMod->cmd_decoder.PrintResponse(msg, g_serialMod->cmd_decoder.PrintType::NO_ERROR);
//			if(Katt == 0)
//				Calculate_Optimization_And_Attenuation(Aopt, Kopt, 6.0, 2*edgeK_Att, 2); //2: right edge
//			else
//				Calculate_Optimization_And_Attenuation(Aopt, Kopt, Aatt, Katt, 2); //2: right edge

			Fill_Channel_ColumnData(ch-1);
			RelocateChannelTF(ch-1, F1_PixelPos, F2_PixelPos, FC_PixelPos);
		}
		else if (g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][ch].active == true)
		{
			double sigma = g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][ch].SIGMA;
			double Aopt = g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][ch].A_OPP;
			double Kopt = g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][ch].K_OPP;
			double Aatt = g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][ch].A_ATT;
			double Katt = g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][ch].K_ATT;
			double wavelength = g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][ch].LAMDA;
			double ch_fc = g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][ch].FC;
			double ch_bw = g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][ch].BW;
			double ch_f1 = g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][ch].F1;
			double ch_f2 = g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][ch].F2;

			ch_f1 = ch_fc - (ch_bw-4)/2;
			ch_f2 = ch_fc + (ch_bw-4)/2;

			double F1_PixelPos, F2_PixelPos, FC_PixelPos;

			Calculate_Pattern_Formulas(ch-1, wavelength, g_pixelSize,sigma, Aopt, Kopt, Aatt, Katt); //1: channel btw edges

			Find_LinearPixelPos_DevelopMode(ch_f1, F1_PixelPos);
			Find_LinearPixelPos_DevelopMode(ch_f2, F2_PixelPos);
			Find_LinearPixelPos_DevelopMode(ch_fc, FC_PixelPos);

//			edgeK_Att = Katt + 1 - (F1_PixelPos - floor(F1_PixelPos));

//			Calculate_Optimization_And_Attenuation(Aopt, Kopt, 6.0, 2*edgeK_Att, 0); //0:left edge

//			edgeK_Att = Katt + (F2_PixelPos - floor(F2_PixelPos));

//			Calculate_Optimization_And_Attenuation(Aopt, Kopt, 6.0, 2*edgeK_Att, 2); //2: right edge

			Fill_Channel_ColumnData(ch-1);
			RelocateChannelFG(ch-1, F1_PixelPos, F2_PixelPos, FC_PixelPos);

			int total_Slots = g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][ch].slotNum;
			for (int slot = 1; slot <= total_Slots; slot++)
			{
				// Go through all slot attenuation and if its not zero then calculate that slot attenuation and relocate that slot within the channel
				if (g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][ch].slotsATTEN[slot-1] != 0)
				{
					double slot_ATT = g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][ch].slotsATTEN[slot-1];
					double actual_slot_att = slot_ATT;	// slot attenuation is relative to channel attenuation.(slot_ATT can be -ve)
					double only_for_testing = Katt + 1;

					Calculate_Pattern_Formulas(ch-1, wavelength, g_pixelSize, sigma, Aopt, Kopt, actual_slot_att, only_for_testing);		// in develop mode, slot attenuation is currently not set by user

					RelocateSlot(ch-1, slot, total_Slots, F1_PixelPos, F2_PixelPos);
				}
			}
		}
	}

#endif
	return (0);


}

int PatternGenModule::Calculate_Channel_EdgePattern_DevelopMode(unsigned char channelNum)
{
	//here for command configuring left and right edge pattern for fc calib.
	//a_op,k_op values can be set independently for two edges to calculate each edge pattern.
	return 0;
}

int PatternGenModule::Calculate_Pattern_Formulas(const int ch,const double lamda, const int pixelSize, double sigmaRad, const double Aopt, const double Kopt, const double Aatt,const double Katt)
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

#ifndef _OCM_SCAN_
	Fill_Channel_ColumnData(ch);
#else
	if(g_serialMod->cmd_decoder.GetPanelInfo().b_OCMSet && ch == OCM_SCAN_CHANNEL)
	{
		Fill_Channel_ColumnData(ch+g_Total_Channels);
	}
	else
	{
		Fill_Channel_ColumnData(ch);
	}
#endif
	return (0);
}

int PatternGenModule::Calculate_PhaseLine(const int pixelSize, double sigmaRad, const double lamda)
{
	for (int Y = 1; Y <= m_customLCOS_Height; Y++)
	{
		phaseLine[Y-1] = (g_phaseDepth[g_moduleNum]*Y*pixelSize*sigmaRad) / lamda;
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
		phaseLine_MOD[i] = std::fmod(phaseLine[i], g_phaseDepth[g_moduleNum]);	//mod of 2

		// Rebuild Period
		if(phaseLine_MOD[i] < (1/calculatedPeriod*g_phaseDepth[g_moduleNum]/2))
		{
			rebuildPeriod[i] =	phaseLine_MOD[i] + g_phaseDepth[g_moduleNum];
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
			if(rebuildPeriod[i] > rebuildPeriod[i-1])	// Increment Period Count if current rebuild period is > previous rebuild period
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

//		printf("%d \t phaseLine_MOD = %f \t rebuildPeriod = %f \t periodCount = %d \t factorsForDiv = %d \t\n",i,phaseLine_MOD[i], rebuildPeriod[i], periodCount[i], factorsForDiv[i]);
	}

	// The last period will have nothing to compare itself to, so in loop periods,=.push_back will never happen
	// So we push the last periodCount to periods. It will also help when sigma is too small.
	// In case if sigma is too small that only one period exist which size is equal to whole height of LCOS

	periods.push_back(periodCount[m_customLCOS_Height-1]);
}

void PatternGenModule::Calculate_Period(const int pixelSize, double sigmaRad, const double lamda)
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

void PatternGenModule::Calculate_Optimization_And_Attenuation(const double Aopt, const double Kopt, const double Aatt,const double Katt, const int col)
{
	/* Based on EXCEL FILE:
	 * Opt Factor = Kopt*ABS(sin((Aopt*Y*Phase_depth*PI)/(cal_period)))
	 * Opt Pattern = (PhaseLine + Opt Factor) - (factorforDIV*Phase_depth)
	 * Att Factor = Katt*(sin(Y*Phase_depth*PI/(Aatt)))
	 * Att Pattern = Opt Pattern + Att Factor;
	 * Att_Border_Limited = if(Att Pattern < 0, 0, IF(Att Pattern > (Phase_depth + ((1/cal_period)*(Phase_depth/2))),
	 * 						Phase_depth + ((1/cal_period)*(Phase_depth/2)), Att Pattern))
	 */
	double optFactor{0.0}, attFactor{0.0};		// Holds intermediate values
	double minAtt = 0;
	double maxAtt = g_phaseDepth[g_moduleNum] + (1/calculatedPeriod)*(g_phaseDepth[g_moduleNum]/2);

	double temp[m_customLCOS_Height];	// Use to temporary store attenuated_limited data to flip all periods in case of -ve Sigma
	std::vector<double> flipArray;
	int periodIndex_track = 0;
	int jump=0;

	//std::cout << " Aopt = " << Aopt << " Kopt  " << Kopt <<" Aatt = " << Aatt << " Katt  " << Katt << " calculate Period = " << calculatedPeriod <<"\n";
	/*REMEMBER: Due to algorithm in excel sometimes is A_att is not unique it will cause no change in output data*/
	for (int Y = 0; Y < m_customLCOS_Height; Y++)
	{
		//2*PI not 2.2*PI because
		optFactor = Kopt*abs(sin((Aopt*(Y+1)*2*PI)/(calculatedPeriod)));		// drc modified, always 2*PI, not 2.2*PI

		optimizedPattern[Y] = (phaseLine[Y] + optFactor) - (factorsForDiv[Y]*g_phaseDepth[g_moduleNum]);

		attFactor = Katt*sin(Aatt*(Y+1)*2*PI/calculatedPeriod);		// always 2*PI, not 2.2*PI because diffraction efficiency is most optimal by it according to Dr. Du

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

//				for(double a:flipArray)
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


void  PatternGenModule::AjustEdgePixelAttenuation(unsigned int ch, double F1_PixelPos, double F2_PixelPos, double FC_PixelPos)
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
#ifndef _OCM_SCAN_
			channelColumnData[col][i + m_customLCOS_Height*ch] = linearLUT[degree];
#else
			if(ch >= g_Total_Channels) //for ocm scan channel
			{
				OCMColumnData[col][i] = linearLUT[degree];
			}
			else
			{
				channelColumnData[col][i + m_customLCOS_Height*ch] = linearLUT[degree];
			}
#endif
		}
	}
}

void PatternGenModule::RelocateChannelTF(unsigned int chNum, double f1_PixelPos, double f2_PixelPos, double fc_PixelPos)
{
	int i = 0;
	unsigned char value = m_backColor, tgapped = 0, mgapped = 0, bgapped = 0, ocmscanned = 0;

	int ch_start_pixelLocation = floor(f1_PixelPos); //left side should be floored to be included in coverage by calculated pattern
//	std::cout << "f1_PixelPos: " << f1_PixelPos <<std::endl;
	int ch_end_pixelLocation = floor(f2_PixelPos);   //right side should be ceilinged to be covered by calculated pattern
//	std::cout << "f2_PixelPos: " << f2_PixelPos <<std::endl;

	int ch_width_inPixels = ch_end_pixelLocation - ch_start_pixelLocation + 1; //drc modified starting from 0 because width max should be 1920/1952

	if ((ch_start_pixelLocation + ch_width_inPixels) > g_LCOS_Width)
	{
		//channel getting out of screen from right side
		ch_width_inPixels = g_LCOS_Width - ch_start_pixelLocation;	// give us the remaining pixel space available and we set it to ch_bandwidth
	}

	bool rotate = false;

	int operationMode; 		// Development or Production
	Find_OperationMode(&operationMode);

	if(rotate)
	{	std::cout << "Rotating channel \n" << std::endl;
		RotateChannel(0, ch_start_pixelLocation, 0, ch_end_pixelLocation, 1079, ch_end_pixelLocation, 1079, ch_start_pixelLocation, 0.1, 540, ch_end_pixelLocation-ch_start_pixelLocation);
	}
	else
	{
		while (i < m_customLCOS_Height)
		{
			if(g_moduleNum == 1)		// Top side of LCOS
			{
#ifdef _TWIN_WSS_
				if(g_serialMod->cmd_decoder.GetPanelInfo().b_OCMSet)
				{
#ifdef _OCM_SCAN_
					ocmscanned = bgapped = 0;

					if(i >= g_serialMod->cmd_decoder.GetPanelInfo().ocm_top_M1 && i <= g_serialMod->cmd_decoder.GetPanelInfo().ocm_bottom_M1) // OCM region
					{
						value = OCMColumnData[1][i];
						ocmscanned++;
					}
					else if(i < g_serialMod->cmd_decoder.GetPanelInfo().ocm_top_M1)
					{
						if(chNum < g_Total_Channels) // worker channels not ocm channels
							value = channelColumnData[1][i + m_customLCOS_Height *chNum];
						else
							value = BackgroundColumnData[i];
							bgapped++;
					}
					else
					{
						value = BackgroundColumnData[i];
						bgapped++;
					}
#endif
				}
				if(g_serialMod->cmd_decoder.GetPanelInfo().b_gapSet)
				{ //drc to check if gap is needed

					tgapped = mgapped = 0;
					if(i >= g_serialMod->cmd_decoder.GetPanelInfo().topGap){
						value = channelColumnData[1][i + m_customLCOS_Height *chNum];
					}
					else{
						value = BackgroundColumnData[i];
						tgapped++;
					}

					int startMidGap = g_serialMod->cmd_decoder.GetPanelInfo().middleGapPosition - g_serialMod->cmd_decoder.GetPanelInfo().middleGap/2;
					int endMidGap = g_serialMod->cmd_decoder.GetPanelInfo().middleGapPosition + g_serialMod->cmd_decoder.GetPanelInfo().middleGap/2;

					if(startMidGap < m_customLCOS_Height && endMidGap < m_customLCOS_Height)
					{
						if(i>=startMidGap && startMidGap != 0)
						{
							//value = background;
							value = BackgroundColumnData[i];
							mgapped++;
						}
					}
					else if (startMidGap < m_customLCOS_Height && endMidGap > m_customLCOS_Height)
					{
						if(i >= startMidGap)
						{
							//value = m_background;
							value = BackgroundColumnData[i];
							mgapped++;
						}
					}
					if(i > g_serialMod->cmd_decoder.GetPanelInfo().topGap && i < startMidGap)
					{
						value = channelColumnData[1][i + m_customLCOS_Height *chNum];
					}
				}
				if(ocmscanned != 0 || bgapped != 0 || mgapped != 0 || tgapped != 0)
				{
					//do nothing
				}
				else
#endif
				{
					if(chNum < g_Total_Channels) // worker channels not ocm channels
						value = channelColumnData[1][i + m_customLCOS_Height *chNum];
				}

#ifndef _FLIP_DISPLAY_
				if(g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][chNum+1].F1ContiguousOrNot == 0 && g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][chNum+1].F2ContiguousOrNot == 0)
				{
					if(tgapped != 0 || mgapped != 0 || ocmscanned != 0 || bgapped != 0 || operationMode == OperationMode::DEVELOPMENT || g_b120WL == true)  //added for 120 wl
					{//developmode for calibration of FC pixelpos
						memset((fullPatternData + (i *g_LCOS_Width) + ch_start_pixelLocation), value, ch_width_inPixels* sizeof(char));
					}
					else{
						memset((fullPatternData + (i *g_LCOS_Width) + ch_start_pixelLocation), channelColumnData[0][i + m_customLCOS_Height *chNum], sizeof(char));
						memset((fullPatternData + (i *g_LCOS_Width) + ch_start_pixelLocation + 1), value, (ch_width_inPixels-2)* sizeof(char));
						memset((fullPatternData + (i *g_LCOS_Width) + ch_start_pixelLocation+ch_width_inPixels-1), channelColumnData[2][i + m_customLCOS_Height *chNum], sizeof(char));
					}
				}
				else if((g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][chNum+1].F1ContiguousOrNot == 1 && g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][chNum+1].F2ContiguousOrNot == 0))
				{
					if(tgapped != 0 || mgapped != 0 || ocmscanned != 0){
						memset((fullPatternData + (i *g_LCOS_Width) + ch_start_pixelLocation), value, ch_width_inPixels*sizeof(char));
					}
					else
					{
						memset((fullPatternData + (i *g_LCOS_Width) + ch_start_pixelLocation), value, (ch_width_inPixels-1)* sizeof(char));
						memset((fullPatternData + (i *g_LCOS_Width) + ch_start_pixelLocation+ch_width_inPixels), channelColumnData[2][i + m_customLCOS_Height *chNum], sizeof(char));
					}
				}
				else if((g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][chNum+1].F1ContiguousOrNot == 0 && g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][chNum+1].F2ContiguousOrNot == 1))
				{
					if(tgapped != 0 || mgapped != 0 || ocmscanned != 0){
						memset((fullPatternData + (i *g_LCOS_Width) + ch_start_pixelLocation), value, ch_width_inPixels*sizeof(char));
					}
					else
					{
						memset((fullPatternData + (i *g_LCOS_Width) + ch_start_pixelLocation), channelColumnData[0][i + m_customLCOS_Height *chNum], sizeof(char));
						memset((fullPatternData + (i *g_LCOS_Width) + ch_start_pixelLocation + 1), value, (ch_width_inPixels-1)* sizeof(char));
					}
					//memset((fullPatternData + (i *g_LCOS_Width) + ch_start_pixelLocation+(ch_width_inPixels-1)), value, sizeof(char));

				}
				else if((g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][chNum+1].F1ContiguousOrNot == 1 && g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][chNum+1].F2ContiguousOrNot == 1))
				{
					memset((fullPatternData + (i *g_LCOS_Width) + ch_start_pixelLocation), value, ch_width_inPixels* sizeof(char));
				}
#else

				memset((fullPatternData + ((m_customLCOS_Height -i) *g_LCOS_Width) - ch_start_pixelLocation - ch_width_inPixels), value, ch_width_inPixels* sizeof(char));

#endif
			}
			else if (g_moduleNum == 2)	// Bottom side of LCOS
			{
				bgapped = mgapped = ocmscanned = 0;

				if(g_serialMod->cmd_decoder.GetPanelInfo().b_OCMSet)
				{
#ifdef _OCM_SCAN_

					if(i >= g_serialMod->cmd_decoder.GetPanelInfo().ocm_top_M2 && i <= g_serialMod->cmd_decoder.GetPanelInfo().ocm_bottom_M2) // OCM region
					{
						value = OCMColumnData[1][i];
						ocmscanned++;
					}
					else if(i > g_serialMod->cmd_decoder.GetPanelInfo().ocm_bottom_M2)
					{
						if(chNum < g_Total_Channels) // worker channels not ocm channels
							value = channelColumnData[1][i + m_customLCOS_Height *chNum];
						else
							value = BackgroundColumnData[i+ m_customLCOS_Height];
							bgapped++;
					}
					else
					{
						value = BackgroundColumnData[i + m_customLCOS_Height];
						bgapped++;
					}
#endif
				}
				if(g_serialMod->cmd_decoder.GetPanelInfo().b_gapSet)
				{

					if(i <= (m_customLCOS_Height - g_serialMod->cmd_decoder.GetPanelInfo().bottomGap)){
						value = channelColumnData[1][i + m_customLCOS_Height *chNum];
					}
					else
					{
						value = BackgroundColumnData[i + m_customLCOS_Height];
						bgapped++;
					}

					int startMidGap = g_serialMod->cmd_decoder.GetPanelInfo().middleGapPosition - g_serialMod->cmd_decoder.GetPanelInfo().middleGap/2;
					int endMidGap = g_serialMod->cmd_decoder.GetPanelInfo().middleGapPosition + g_serialMod->cmd_decoder.GetPanelInfo().middleGap/2;

					if(startMidGap > m_customLCOS_Height && endMidGap > m_customLCOS_Height){
						if(i <= (endMidGap%m_customLCOS_Height)){
							//value = m_backColor;
							value = BackgroundColumnData[i + m_customLCOS_Height];
							mgapped++;
						}
					}else if (startMidGap < m_customLCOS_Height && endMidGap > m_customLCOS_Height){
						if(i <= (endMidGap%m_customLCOS_Height)){
							//value = m_backColor;
							value = BackgroundColumnData[i + m_customLCOS_Height];
							mgapped++;
						}
					}
				}
				if(ocmscanned != 0 || bgapped != 0 || mgapped != 0)
				{
					//do nothing
				}
				else
				{
					if(chNum < g_Total_Channels) // worker channels not ocm channels
						value = channelColumnData[1][i + m_customLCOS_Height *chNum];
				}

#ifndef _FLIP_DISPLAY_
				if(g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][chNum+1].F1ContiguousOrNot == 0 && g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][chNum+1].F2ContiguousOrNot == 0)
				{
					if(bgapped != 0 || mgapped != 0 || ocmscanned != 0 || operationMode == OperationMode::DEVELOPMENT)
					{
						memset((fullPatternData + (i+m_customLCOS_Height) *g_LCOS_Width + ch_start_pixelLocation), value, ch_width_inPixels* sizeof(char));
					}
					else
					{
						memset((fullPatternData + ((i+m_customLCOS_Height) *g_LCOS_Width) + ch_start_pixelLocation), channelColumnData[0][i + m_customLCOS_Height *chNum], sizeof(char));
						memset((fullPatternData + ((i+m_customLCOS_Height) *g_LCOS_Width) + ch_start_pixelLocation + 1), value, (ch_width_inPixels-2)* sizeof(char));
						memset((fullPatternData + ((i+m_customLCOS_Height) *g_LCOS_Width) + ch_start_pixelLocation+ch_width_inPixels-1), channelColumnData[2][i + m_customLCOS_Height *chNum], sizeof(char));
					}
				}
				else if((g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][chNum+1].F1ContiguousOrNot == 1 && g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][chNum+1].F2ContiguousOrNot == 0))
				{
					if(bgapped != 0 || mgapped != 0 || ocmscanned != 0 || operationMode == OperationMode::DEVELOPMENT)
					{
						memset((fullPatternData + (i+m_customLCOS_Height) *g_LCOS_Width + ch_start_pixelLocation), value, ch_width_inPixels* sizeof(char));
					}
					else
					{
						memset((fullPatternData + ((i+m_customLCOS_Height) *g_LCOS_Width) + ch_start_pixelLocation), value, (ch_width_inPixels-1)* sizeof(char));
						memset((fullPatternData + ((i+m_customLCOS_Height) *g_LCOS_Width) + ch_start_pixelLocation+ch_width_inPixels-1), channelColumnData[2][i + m_customLCOS_Height *chNum], sizeof(char));
					}
				}
				else if((g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][chNum+1].F1ContiguousOrNot == 0 && g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][chNum+1].F2ContiguousOrNot == 1))
				{
					if(bgapped != 0 || mgapped != 0 || ocmscanned != 0 || operationMode == OperationMode::DEVELOPMENT)
					{
						memset((fullPatternData + (i+m_customLCOS_Height) *g_LCOS_Width + ch_start_pixelLocation), value, ch_width_inPixels* sizeof(char));
					}
					else
					{
						memset((fullPatternData + ((i+m_customLCOS_Height) *g_LCOS_Width) + ch_start_pixelLocation), channelColumnData[0][i + m_customLCOS_Height *chNum], sizeof(char));
						memset((fullPatternData + ((i+m_customLCOS_Height) *g_LCOS_Width) + ch_start_pixelLocation + 1), value, (ch_width_inPixels-1)* sizeof(char));
					}

				}
				else if((g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][chNum+1].F1ContiguousOrNot == 1 && g_serialMod->cmd_decoder.TF_Channel_DS_For_Pattern[g_moduleNum][chNum+1].F2ContiguousOrNot == 1))
				{
					memset((fullPatternData + ((i+m_customLCOS_Height) *g_LCOS_Width) + ch_start_pixelLocation), value, (ch_width_inPixels)* sizeof(char));
				}
#else
				memset((fullPatternData + ((i+m_customLCOS_Height) *g_LCOS_Width) - ch_start_pixelLocation - ch_width_inPixels), value, ch_width_inPixels* sizeof(char));	// FLIP= F1 starts from RHS goes to LHS. 196275-191125  F2 <------ F1
#endif
			}
			i++;
		}
	}

}

void PatternGenModule::RelocateChannelFG(unsigned int chNum, double f1_PixelPos, double f2_PixelPos, double fc_PixelPos)
{
	int i = 0;
	unsigned char value = m_backColor, tgapped = 0, mgapped = 0, bgapped = 0;
	int ch_start_pixelLocation = floor(f1_PixelPos);
//	std::cout << "f1_PixelPos: " << f1_PixelPos <<std::endl;
	int ch_end_pixelLocation = floor(f2_PixelPos);
//	std::cout << "f2_PixelPos: " << f2_PixelPos <<std::endl;

	int ch_width_inPixels = ch_end_pixelLocation - ch_start_pixelLocation + 1; //drc modified starting from 0 end with 1919/1951, width should be 1920/1952

	if ((ch_start_pixelLocation + ch_width_inPixels) > g_LCOS_Width)
	{
		//channel getting out of screeen from right side
		ch_width_inPixels = g_LCOS_Width - ch_start_pixelLocation;	// give us the remaining pixel space available  and we set it to ch_bandwidth
	}

	bool rotate = false;
	int operationMode; 		// Development or Production
	Find_OperationMode(&operationMode);

	if(rotate)
	{	std::cout << "Rotating channel \n" << std::endl;
		RotateChannel(0, ch_start_pixelLocation, 0, ch_end_pixelLocation, 1079, ch_end_pixelLocation, 1079, ch_start_pixelLocation, 0.1, 540, ch_end_pixelLocation-ch_start_pixelLocation);
	}
	else
	{
		while (i < m_customLCOS_Height)
		{
			if(g_moduleNum == 1)		// Top side of LCOS
			{
#ifdef _TWIN_WSS_
				if(g_serialMod->cmd_decoder.GetPanelInfo().b_gapSet){ //drc to check if gap is needed

					if(i >= g_serialMod->cmd_decoder.GetPanelInfo().topGap){
						value = channelColumnData[1][i + m_customLCOS_Height *chNum];
					}
					else
					{
						value = BackgroundColumnData[i];
						tgapped++;
					}

					int startMidGap = g_serialMod->cmd_decoder.GetPanelInfo().middleGapPosition - g_serialMod->cmd_decoder.GetPanelInfo().middleGap/2;
					int endMidGap = g_serialMod->cmd_decoder.GetPanelInfo().middleGapPosition + g_serialMod->cmd_decoder.GetPanelInfo().middleGap/2;

					if(startMidGap < m_customLCOS_Height && endMidGap < m_customLCOS_Height){
						if(i>=startMidGap && startMidGap != 0){
							//value = background;
							value = BackgroundColumnData[i];
							mgapped++;
						}
					}
					else if (startMidGap < m_customLCOS_Height && endMidGap > m_customLCOS_Height){
						if(i >= startMidGap){
							//value = background;
							value = BackgroundColumnData[i];
							mgapped++;
						}
					}

					if(i >= g_serialMod->cmd_decoder.GetPanelInfo().topGap && i <= m_customLCOS_Height - g_serialMod->cmd_decoder.GetPanelInfo().bottomGap){
							value = channelColumnData[1][i + m_customLCOS_Height *chNum];
					}

		}
		else
#endif
		{
			value = channelColumnData[1][i + m_customLCOS_Height *chNum];
		}

#ifndef _FLIP_DISPLAY_
				if(g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][chNum+1].F1ContiguousOrNot == 0 && g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][chNum+1].F2ContiguousOrNot == 0)
				{
					if(tgapped != 0 || mgapped != 0 || operationMode == OperationMode::DEVELOPMENT){
						memset((fullPatternData + (i *g_LCOS_Width) + ch_start_pixelLocation), value, ch_width_inPixels* sizeof(char));
					}
					else{
						memset((fullPatternData + (i *g_LCOS_Width) + ch_start_pixelLocation), channelColumnData[0][i + m_customLCOS_Height *chNum], sizeof(char));
						memset((fullPatternData + (i *g_LCOS_Width) + ch_start_pixelLocation + 1), value, (ch_width_inPixels-2)* sizeof(char));
						memset((fullPatternData + (i *g_LCOS_Width) + ch_start_pixelLocation+ch_width_inPixels-1), channelColumnData[2][i + m_customLCOS_Height *chNum], sizeof(char));
					}
				}
				else if((g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][chNum+1].F1ContiguousOrNot == 1 && g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][chNum+1].F2ContiguousOrNot == 0))
				{
					if(tgapped != 0 || mgapped != 0 || operationMode == OperationMode::DEVELOPMENT){
						memset((fullPatternData + (i *g_LCOS_Width) + ch_start_pixelLocation), value, ch_width_inPixels* sizeof(char));
					}
					else{
						memset((fullPatternData + (i *g_LCOS_Width) + ch_start_pixelLocation), value, (ch_width_inPixels-1)* sizeof(char));
						memset((fullPatternData + (i *g_LCOS_Width) + ch_start_pixelLocation+ch_width_inPixels-1), channelColumnData[2][i + m_customLCOS_Height *chNum], sizeof(char));
					}
				}
				else if((g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][chNum+1].F1ContiguousOrNot == 0 && g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][chNum+1].F2ContiguousOrNot == 1))
				{
					if(tgapped != 0 || mgapped != 0 || operationMode == OperationMode::DEVELOPMENT){
						memset((fullPatternData + (i *g_LCOS_Width) + ch_start_pixelLocation), value, ch_width_inPixels* sizeof(char));
					}
					else{
						memset((fullPatternData + (i *g_LCOS_Width) + ch_start_pixelLocation), channelColumnData[0][i + m_customLCOS_Height *chNum], sizeof(char));
						memset((fullPatternData + (i *g_LCOS_Width) + ch_start_pixelLocation + 1), value, (ch_width_inPixels-1)* sizeof(char));
					}
				}
				else if((g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][chNum+1].F1ContiguousOrNot == 1 && g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][chNum+1].F2ContiguousOrNot == 1))
				{
					memset((fullPatternData + (i *g_LCOS_Width) + ch_start_pixelLocation), value, ch_width_inPixels* sizeof(char));
				}
#else
				memset((fullPatternData + ((m_customLCOS_Height -i) *g_LCOS_Width) - ch_start_pixelLocation - ch_width_inPixels), value, ch_width_inPixels* sizeof(char));
#endif
			}
			else if (g_moduleNum == 2)	// Bottom side of LCOS
			{
				if(g_serialMod->cmd_decoder.GetPanelInfo().b_gapSet){
					bgapped = mgapped = 0;
					if(i < (m_customLCOS_Height - g_serialMod->cmd_decoder.GetPanelInfo().bottomGap)){
						value = channelColumnData[1][i + m_customLCOS_Height *chNum];
					}
					else
					{
						value = BackgroundColumnData[i + m_customLCOS_Height];
						bgapped++;
					}

					int startMidGap = g_serialMod->cmd_decoder.GetPanelInfo().middleGapPosition - g_serialMod->cmd_decoder.GetPanelInfo().middleGap/2;
					int endMidGap = g_serialMod->cmd_decoder.GetPanelInfo().middleGapPosition + g_serialMod->cmd_decoder.GetPanelInfo().middleGap/2;

					if(startMidGap > m_customLCOS_Height && endMidGap > m_customLCOS_Height){
						if(i < (endMidGap%m_customLCOS_Height)){
							//value = m_backColor;
							value = BackgroundColumnData[i + m_customLCOS_Height];
							mgapped++;
						}
					}else if (startMidGap < m_customLCOS_Height && endMidGap > m_customLCOS_Height){
						if(i < (endMidGap%m_customLCOS_Height)){
							//value = m_backColor;
							value = BackgroundColumnData[i + m_customLCOS_Height];
							mgapped++;
						}
					}
				}else{
					value = channelColumnData[1][i + m_customLCOS_Height *chNum];
				}

#ifndef _FLIP_DISPLAY_
				if(g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][chNum+1].F1ContiguousOrNot == 0 && g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][chNum+1].F2ContiguousOrNot == 0)
				{
					if(bgapped != 0 || mgapped != 0 || operationMode == OperationMode::DEVELOPMENT)
					{
						memset((fullPatternData + (i+m_customLCOS_Height) *g_LCOS_Width + ch_start_pixelLocation), value, ch_width_inPixels* sizeof(char));
					}
					else
					{
						memset((fullPatternData + ((i+m_customLCOS_Height) *g_LCOS_Width) + ch_start_pixelLocation), channelColumnData[0][i + m_customLCOS_Height *chNum], sizeof(char));
						memset((fullPatternData + ((i+m_customLCOS_Height) *g_LCOS_Width) + ch_start_pixelLocation + 1), value, (ch_width_inPixels-2)* sizeof(char));
						memset((fullPatternData + ((i+m_customLCOS_Height) *g_LCOS_Width) + ch_start_pixelLocation+ch_width_inPixels-1), channelColumnData[2][i + m_customLCOS_Height *chNum], sizeof(char));
					}
				}
				else if((g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][chNum+1].F1ContiguousOrNot == 1 && g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][chNum+1].F2ContiguousOrNot == 0))
				{
					if(bgapped != 0 || mgapped != 0 || operationMode == OperationMode::DEVELOPMENT)
					{
						memset((fullPatternData + (i+m_customLCOS_Height) *g_LCOS_Width + ch_start_pixelLocation), value, ch_width_inPixels* sizeof(char));
					}
					else{
						memset((fullPatternData + ((i+m_customLCOS_Height) *g_LCOS_Width) + ch_start_pixelLocation), value, (ch_width_inPixels-1)* sizeof(char));
						memset((fullPatternData + ((i+m_customLCOS_Height) *g_LCOS_Width) + ch_start_pixelLocation+ch_width_inPixels-1), channelColumnData[2][i + m_customLCOS_Height *chNum], sizeof(char));
					}
				}
				else if((g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][chNum+1].F1ContiguousOrNot == 0 && g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][chNum+1].F2ContiguousOrNot == 1))
				{
					if(bgapped != 0 || mgapped != 0 || operationMode == OperationMode::DEVELOPMENT)
					{
						memset((fullPatternData + (i+m_customLCOS_Height) *g_LCOS_Width + ch_start_pixelLocation), value, ch_width_inPixels* sizeof(char));
					}
					else{
						memset((fullPatternData + ((i+m_customLCOS_Height) *g_LCOS_Width) + ch_start_pixelLocation), channelColumnData[0][i + m_customLCOS_Height *chNum], sizeof(char));
						memset((fullPatternData + ((i+m_customLCOS_Height) *g_LCOS_Width) + ch_start_pixelLocation + 1), value, (ch_width_inPixels-1)* sizeof(char));
					}
				}
				else if((g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][chNum+1].F1ContiguousOrNot == 1 && g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][chNum+1].F2ContiguousOrNot == 1))
				{
					memset((fullPatternData + ((i+m_customLCOS_Height) *g_LCOS_Width) + ch_start_pixelLocation), value, ch_width_inPixels* sizeof(char));
				}
#else
				memset((fullPatternData + ((i+m_customLCOS_Height) *g_LCOS_Width) - ch_start_pixelLocation - ch_width_inPixels), value, ch_width_inPixels* sizeof(char));	// FLIP= F1 starts from RHS goes to LHS. 196275-191125  F2 <------ F1
#endif
			}
			i++;
		}
	}

}

void PatternGenModule::RelocateChannelFG_SPI(unsigned int chNum, double f1_PixelPos, double f2_PixelPos, double fc_PixelPos)
{
	int i = 0;
	unsigned char value = m_backColor, tgapped = 0, mgapped = 0, bgapped = 0;
	int ch_start_pixelLocation = floor(f1_PixelPos);
	std::cout << "f1_PixelPos: " << f1_PixelPos <<std::endl;
	int ch_end_pixelLocation = floor(f2_PixelPos);
	std::cout << "f2_PixelPos: " << f2_PixelPos <<std::endl;

	int ch_width_inPixels = ch_end_pixelLocation - ch_start_pixelLocation + 1; //drc modified starting from 0 end with 1919/1951, width should be 1920/1952

	if ((ch_start_pixelLocation + ch_width_inPixels) > g_LCOS_Width)
	{
		//channel getting out of screeen from right side
		ch_width_inPixels = g_LCOS_Width - ch_start_pixelLocation;	// give us the remaining pixel space available  and we set it to ch_bandwidth
	}

	bool rotate = false;
	int operationMode; 		// Development or Production
	Find_OperationMode(&operationMode);

	if(rotate)
	{	std::cout << "Rotating channel \n" << std::endl;
		RotateChannel(0, ch_start_pixelLocation, 0, ch_end_pixelLocation, 1079, ch_end_pixelLocation, 1079, ch_start_pixelLocation, 0.1, 540, ch_end_pixelLocation-ch_start_pixelLocation);
	}
	else
	{
		while (i < m_customLCOS_Height)
		{
			if(g_moduleNum == 1)		// Top side of LCOS
			{
#ifdef _TWIN_WSS_
				if(g_spaCmd->g_cmdDecoder->GetPanelInfo().b_gapSet)
				{ //drc to check if gap is needed

					if(i >= g_spaCmd->g_cmdDecoder->GetPanelInfo().topGap){
						value = channelColumnData[1][i + m_customLCOS_Height *chNum];
					}
					else
					{
						value = BackgroundColumnData[i];
						tgapped++;
					}

					int startMidGap = g_spaCmd->g_cmdDecoder->GetPanelInfo().middleGapPosition - g_spaCmd->g_cmdDecoder->GetPanelInfo().middleGap/2;
					int endMidGap = g_spaCmd->g_cmdDecoder->GetPanelInfo().middleGapPosition + g_spaCmd->g_cmdDecoder->GetPanelInfo().middleGap/2;

					if(startMidGap < m_customLCOS_Height && endMidGap < m_customLCOS_Height){
						if(i>=startMidGap && startMidGap != 0){
							//value = background;
							value = BackgroundColumnData[i];
							mgapped++;
						}
					}
					else if (startMidGap < m_customLCOS_Height && endMidGap > m_customLCOS_Height){
						if(i >= startMidGap){
							//value = background;
							value = BackgroundColumnData[i];
							mgapped++;
						}
					}

					if(i >= g_spaCmd->g_cmdDecoder->GetPanelInfo().topGap && i <= m_customLCOS_Height - g_spaCmd->g_cmdDecoder->GetPanelInfo().bottomGap){
							value = channelColumnData[1][i + m_customLCOS_Height *chNum];
					}

		}
		else
#endif
		{
			value = channelColumnData[1][i + m_customLCOS_Height *chNum];
		}

#ifndef _FLIP_DISPLAY_
				if(g_spaCmd->g_cmdDecoder->FG_Channel_DS_For_Pattern[g_moduleNum][chNum+1].F1ContiguousOrNot == 0 && g_spaCmd->g_cmdDecoder->FG_Channel_DS_For_Pattern[g_moduleNum][chNum+1].F2ContiguousOrNot == 0)
				{
					if(tgapped != 0 || mgapped != 0 || operationMode == OperationMode::DEVELOPMENT){
						memset((fullPatternData + (i *g_LCOS_Width) + ch_start_pixelLocation), value, ch_width_inPixels* sizeof(char));
					}
					else{
						memset((fullPatternData + (i *g_LCOS_Width) + ch_start_pixelLocation), channelColumnData[0][i + m_customLCOS_Height *chNum], sizeof(char));
						memset((fullPatternData + (i *g_LCOS_Width) + ch_start_pixelLocation + 1), value, (ch_width_inPixels-2)* sizeof(char));
						memset((fullPatternData + (i *g_LCOS_Width) + ch_start_pixelLocation+ch_width_inPixels-1), channelColumnData[2][i + m_customLCOS_Height *chNum], sizeof(char));
					}
				}
				else if((g_spaCmd->g_cmdDecoder->FG_Channel_DS_For_Pattern[g_moduleNum][chNum+1].F1ContiguousOrNot == 1 && g_spaCmd->g_cmdDecoder->FG_Channel_DS_For_Pattern[g_moduleNum][chNum+1].F2ContiguousOrNot == 0))
				{
					if(tgapped != 0 || mgapped != 0 || operationMode == OperationMode::DEVELOPMENT){
						memset((fullPatternData + (i *g_LCOS_Width) + ch_start_pixelLocation), value, ch_width_inPixels* sizeof(char));
					}
					else{
						memset((fullPatternData + (i *g_LCOS_Width) + ch_start_pixelLocation), value, (ch_width_inPixels-1)* sizeof(char));
						memset((fullPatternData + (i *g_LCOS_Width) + ch_start_pixelLocation+ch_width_inPixels-1), channelColumnData[2][i + m_customLCOS_Height *chNum], sizeof(char));
					}
				}
				else if((g_spaCmd->g_cmdDecoder->FG_Channel_DS_For_Pattern[g_moduleNum][chNum+1].F1ContiguousOrNot == 0 && g_spaCmd->g_cmdDecoder->FG_Channel_DS_For_Pattern[g_moduleNum][chNum+1].F2ContiguousOrNot == 1))
				{
					if(tgapped != 0 || mgapped != 0 || operationMode == OperationMode::DEVELOPMENT){
						memset((fullPatternData + (i *g_LCOS_Width) + ch_start_pixelLocation), value, ch_width_inPixels* sizeof(char));
					}
					else{
						memset((fullPatternData + (i *g_LCOS_Width) + ch_start_pixelLocation), channelColumnData[0][i + m_customLCOS_Height *chNum], sizeof(char));
						memset((fullPatternData + (i *g_LCOS_Width) + ch_start_pixelLocation + 1), value, (ch_width_inPixels-1)* sizeof(char));
					}
				}
				else if((g_spaCmd->g_cmdDecoder->FG_Channel_DS_For_Pattern[g_moduleNum][chNum+1].F1ContiguousOrNot == 1 && g_spaCmd->g_cmdDecoder->FG_Channel_DS_For_Pattern[g_moduleNum][chNum+1].F2ContiguousOrNot == 1))
				{
					memset((fullPatternData + (i *g_LCOS_Width) + ch_start_pixelLocation), value, ch_width_inPixels* sizeof(char));
				}
#else
				memset((fullPatternData + ((m_customLCOS_Height -i) *g_LCOS_Width) - ch_start_pixelLocation - ch_width_inPixels), value, ch_width_inPixels* sizeof(char));
#endif
			}
			else if (g_moduleNum == 2)	// Bottom side of LCOS
			{
				if(g_spaCmd->g_cmdDecoder->GetPanelInfo().b_gapSet){
					bgapped = mgapped = 0;
					if(i < (m_customLCOS_Height - g_spaCmd->g_cmdDecoder->GetPanelInfo().bottomGap)){
						value = channelColumnData[1][i + m_customLCOS_Height *chNum];
					}
					else
					{
						value = BackgroundColumnData[i + m_customLCOS_Height];
						bgapped++;
					}

					int startMidGap = g_spaCmd->g_cmdDecoder->GetPanelInfo().middleGapPosition - g_spaCmd->g_cmdDecoder->GetPanelInfo().middleGap/2;
					int endMidGap = g_spaCmd->g_cmdDecoder->GetPanelInfo().middleGapPosition + g_spaCmd->g_cmdDecoder->GetPanelInfo().middleGap/2;

					if(startMidGap > m_customLCOS_Height && endMidGap > m_customLCOS_Height){
						if(i < (endMidGap%m_customLCOS_Height)){
							//value = m_backColor;
							value = BackgroundColumnData[i + m_customLCOS_Height];
							mgapped++;
						}
					}else if (startMidGap < m_customLCOS_Height && endMidGap > m_customLCOS_Height){
						if(i < (endMidGap%m_customLCOS_Height)){
							//value = m_backColor;
							value = BackgroundColumnData[i + m_customLCOS_Height];
							mgapped++;
						}
					}
				}else{
					value = channelColumnData[1][i + m_customLCOS_Height *chNum];
				}

#ifndef _FLIP_DISPLAY_
				if(g_spaCmd->g_cmdDecoder->FG_Channel_DS_For_Pattern[g_moduleNum][chNum+1].F1ContiguousOrNot == 0 && g_spaCmd->g_cmdDecoder->FG_Channel_DS_For_Pattern[g_moduleNum][chNum+1].F2ContiguousOrNot == 0)
				{
					if(bgapped != 0 || mgapped != 0 || operationMode == OperationMode::DEVELOPMENT)
					{
						memset((fullPatternData + (i+m_customLCOS_Height) *g_LCOS_Width + ch_start_pixelLocation), value, ch_width_inPixels* sizeof(char));
					}
					else
					{
						memset((fullPatternData + ((i+m_customLCOS_Height) *g_LCOS_Width) + ch_start_pixelLocation), channelColumnData[0][i + m_customLCOS_Height *chNum], sizeof(char));
						memset((fullPatternData + ((i+m_customLCOS_Height) *g_LCOS_Width) + ch_start_pixelLocation + 1), value, (ch_width_inPixels-2)* sizeof(char));
						memset((fullPatternData + ((i+m_customLCOS_Height) *g_LCOS_Width) + ch_start_pixelLocation+ch_width_inPixels-1), channelColumnData[2][i + m_customLCOS_Height *chNum], sizeof(char));
					}
				}
				else if((g_spaCmd->g_cmdDecoder->FG_Channel_DS_For_Pattern[g_moduleNum][chNum+1].F1ContiguousOrNot == 1 && g_spaCmd->g_cmdDecoder->FG_Channel_DS_For_Pattern[g_moduleNum][chNum+1].F2ContiguousOrNot == 0))
				{
					if(bgapped != 0 || mgapped != 0 || operationMode == OperationMode::DEVELOPMENT)
					{
						memset((fullPatternData + (i+m_customLCOS_Height) *g_LCOS_Width + ch_start_pixelLocation), value, ch_width_inPixels* sizeof(char));
					}
					else{
						memset((fullPatternData + ((i+m_customLCOS_Height) *g_LCOS_Width) + ch_start_pixelLocation), value, (ch_width_inPixels-1)* sizeof(char));
						memset((fullPatternData + ((i+m_customLCOS_Height) *g_LCOS_Width) + ch_start_pixelLocation+ch_width_inPixels-1), channelColumnData[2][i + m_customLCOS_Height *chNum], sizeof(char));
					}
				}
				else if((g_spaCmd->g_cmdDecoder->FG_Channel_DS_For_Pattern[g_moduleNum][chNum+1].F1ContiguousOrNot == 0 && g_spaCmd->g_cmdDecoder->FG_Channel_DS_For_Pattern[g_moduleNum][chNum+1].F2ContiguousOrNot == 1))
				{
					if(bgapped != 0 || mgapped != 0 || operationMode == OperationMode::DEVELOPMENT)
					{
						memset((fullPatternData + (i+m_customLCOS_Height) *g_LCOS_Width + ch_start_pixelLocation), value, ch_width_inPixels* sizeof(char));
					}
					else{
						memset((fullPatternData + ((i+m_customLCOS_Height) *g_LCOS_Width) + ch_start_pixelLocation), channelColumnData[0][i + m_customLCOS_Height *chNum], sizeof(char));
						memset((fullPatternData + ((i+m_customLCOS_Height) *g_LCOS_Width) + ch_start_pixelLocation + 1), value, (ch_width_inPixels-1)* sizeof(char));
					}
				}
				else if((g_spaCmd->g_cmdDecoder->FG_Channel_DS_For_Pattern[g_moduleNum][chNum+1].F1ContiguousOrNot == 1 && g_spaCmd->g_cmdDecoder->FG_Channel_DS_For_Pattern[g_moduleNum][chNum+1].F2ContiguousOrNot == 1))
				{
					memset((fullPatternData + ((i+m_customLCOS_Height) *g_LCOS_Width) + ch_start_pixelLocation), value, ch_width_inPixels* sizeof(char));
				}
#else
				memset((fullPatternData + ((i+m_customLCOS_Height) *g_LCOS_Width) - ch_start_pixelLocation - ch_width_inPixels), value, ch_width_inPixels* sizeof(char));	// FLIP= F1 starts from RHS goes to LHS. 196275-191125  F2 <------ F1
#endif
			}
			i++;
		}
	}

}

void PatternGenModule::RotateChannel(int x1, int y1, int x2, int y2, int x3, int y3, int x4, int y4, double angleRad, int centerX, int centerY)
{
	double sin_theta = sin(angleRad);
	double cos_theta = cos(angleRad);

	// Vertex 1
	//double x1_new = abs(round((x1 - centerX) * cos_theta - (y1 - centerY) * sin_theta + centerX));
	double x1_new = 0;
	double y1_new = abs(round((x1 - centerX) * sin_theta + (y1 - centerY) * cos_theta + centerY));

	// Vertex 2
	//double x2_new = abs(round((x2 - centerX) * cos_theta - (y2 - centerY) * sin_theta + centerX));
	double x2_new = 0;
	double y2_new = abs(round((x2 - centerX) * sin_theta + (y2 - centerY) * cos_theta + centerY));

	// Vertex 3
	//double x3_new = abs(round((x3 - centerX) * cos_theta - (y3 - centerY) * sin_theta + centerX));
	double x3_new = 1079;
	double y3_new = abs(round((x3 - centerX) * sin_theta + (y3 - centerY) * cos_theta + centerY));

	// Vertex 4
	//double x4_new = abs(round((x4 - centerX) * cos_theta - (y4 - centerY) * sin_theta + centerX));
	double x4_new = 1079;
	double y4_new = abs(round((x4 - centerX) * sin_theta + (y4 - centerY) * cos_theta + centerY));
	std::cout << "New coordinates: \n" << std::endl;
	std::cout << x1_new << "  " <<y1_new << "\n"
			 << x2_new << "  " <<y2_new << "\n"
			 << x3_new << "  " <<y3_new << "\n"
			 << x4_new << "  " <<y4_new << std::endl;

	for (int row = std::min(y1_new, std::min(y2_new, std::min(y3_new, y4_new))); row <= std::max(y1_new, std::max(y2_new, std::max(y3_new, y4_new))); row++) {
	    for (int col = std::min(x1_new, std::min(x2_new, std::min(x3_new, x4_new))); col <= std::max(x1_new, std::max(x2_new, std::max(x3_new, x4_new))); col++) {
	        if (isInsideRectangle(col, row, x1_new, y1_new, x2_new, y2_new, x3_new, y3_new, x4_new, y4_new)) {

	        	fullPatternData[g_LCOS_Width*col + row] = 255;

	        }
	        //std::cout << col << " ";
	    }
		//std::cout << "\n";
	}
}

bool PatternGenModule::isInsideRectangle(double x, double y, double x1, double y1, double x2, double y2, double x3, double y3, double x4, double y4)
{
    // Compute the area of the rectangle
    double area = 0.5 * abs(x2*y1 - x1*y2 + x3*y2 - x2*y3 + x4*y3 - x3*y4 + x1*y4 - x4*y1);
    //std::cout << "area of rectangle = " << area << std::endl;
    // Compute the areas of the four triangles formed by the point (x,y) and the vertices of the rectangle
    double area1 = 0.5 * abs(x1*y - x*y1 + x2*y1 - x1*y2 + x*y2 - x2*y);
    double area2 = 0.5 * abs(x2*y - x*y2 + x3*y2 - x2*y3 + x*y3 - x3*y);
    double area3 = 0.5 * abs(x3*y - x*y3 + x4*y3 - x3*y4 + x*y4 - x4*y);
    double area4 = 0.5 * abs(x4*y - x*y4 + x1*y4 - x4*y1 + x*y1 - x1*y);

    // If the sum of the areas of the four triangles is equal to the area of the rectangle, the point is inside the rectangle
    return (area1 + area2 + area3 + area4) == area;
}

void PatternGenModule::RelocateSlot(unsigned int chNum, unsigned int slotNum, unsigned int totalSlots, double f1_PixelPos, double f2_PixelPos)
{
	int i = 0;

	int startMidGap = 0;
	int endMidGap = 0;

	int slot_Start_Location = 0;
	int slot_Width_inPixels = 0;

	int ch_start_pixelLocation = round(f1_PixelPos);
	int ch_end_pixelLocation = round(f2_PixelPos);

	int ch_width_inPixels = ch_end_pixelLocation - ch_start_pixelLocation + 1; // drc modified: start pixel no. start from 0, so need to +1

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
			if(g_serialMod->cmd_decoder.GetPanelInfo().b_gapSet)
			{
#ifdef _TWIN_WSS_
				if(i >= g_serialMod->cmd_decoder.GetPanelInfo().topGap)
				{
					value = channelColumnData[1][i + m_customLCOS_Height *chNum];
				}

				startMidGap = g_serialMod->cmd_decoder.GetPanelInfo().middleGapPosition - g_serialMod->cmd_decoder.GetPanelInfo().middleGap/2;
				endMidGap = g_serialMod->cmd_decoder.GetPanelInfo().middleGapPosition + g_serialMod->cmd_decoder.GetPanelInfo().middleGap/2;

				if(startMidGap < m_customLCOS_Height && endMidGap < m_customLCOS_Height)
				{
					if(i>=startMidGap && i <= endMidGap)
					{
						//value = m_backColor;
						value = BackgroundColumnData[i];
					}
				}
				else if (startMidGap < m_customLCOS_Height && endMidGap > m_customLCOS_Height)
				{
					if(i >= startMidGap)
					{
							//value = m_backColor;
						value = BackgroundColumnData[i];
					}
				}
#else
				if(i >= g_serialMod->cmd_decoder.GetPanelInfo().topGap)
				{
					value = channelColumnData[1][i + m_customLCOS_Height *chNum];
				}
#endif
			}
			else
			{
				value = channelColumnData[1][i + m_customLCOS_Height *chNum];
			}

#ifndef _FLIP_DISPLAY_
			if(g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][chNum+1].slotBlockedOrNot[slotNum-1] == 1)
			{
				memset(fullPatternData + (i *g_LCOS_Width) + slot_Start_Location, BackgroundColumnData[i], slot_Width_inPixels* sizeof(char));
			}
			else
			{
				memset((fullPatternData + (i *g_LCOS_Width) + slot_Start_Location), channelColumnData[0][i + m_customLCOS_Height *chNum], sizeof(char));
				memset((fullPatternData + (i *g_LCOS_Width) + slot_Start_Location + 1), value, (slot_Width_inPixels-2)* sizeof(char));
				memset((fullPatternData + (i *g_LCOS_Width) + slot_Start_Location+(slot_Width_inPixels-1)), channelColumnData[2][i + m_customLCOS_Height *chNum], sizeof(char));
			}
#else
			memset((fullPatternData + ((m_customLCOS_Height -i) *g_LCOS_Width) - slot_Start_Location - slot_Width_inPixels), value, slot_Width_inPixels* sizeof(char));	// FLIP= F1 starts from RHS goes to LHS. 196275-191125  F2 <------ F1
#endif
		}
		else if (g_moduleNum == 2)	// Bottom side of LCOS
		{
			if(g_serialMod->cmd_decoder.GetPanelInfo().b_gapSet){
				if(i <= (m_customLCOS_Height - g_serialMod->cmd_decoder.GetPanelInfo().bottomGap)){
					value = channelColumnData[1][i + m_customLCOS_Height *chNum];
				}

				int startMidGap = g_serialMod->cmd_decoder.GetPanelInfo().middleGapPosition - g_serialMod->cmd_decoder.GetPanelInfo().middleGap/2;
				int endMidGap = g_serialMod->cmd_decoder.GetPanelInfo().middleGapPosition + g_serialMod->cmd_decoder.GetPanelInfo().middleGap/2;

				if(startMidGap > m_customLCOS_Height && endMidGap > m_customLCOS_Height){
					if(i>=(startMidGap%m_customLCOS_Height) && i <= (endMidGap%m_customLCOS_Height)){
						//value = m_backColor;
						value = BackgroundColumnData[i+m_customLCOS_Height];
					}
				}else if (startMidGap < m_customLCOS_Height && endMidGap > m_customLCOS_Height){
					if(i <= (endMidGap%m_customLCOS_Height)){
						//value = m_backColor;
						value = BackgroundColumnData[i+m_customLCOS_Height];
					}
				}
			}else{
				value = channelColumnData[1][i + m_customLCOS_Height *chNum];
			}

#ifndef _FLIP_DISPLAY_
			if(g_serialMod->cmd_decoder.FG_Channel_DS_For_Pattern[g_moduleNum][chNum+1].slotBlockedOrNot[slotNum-1] == 1)
			{
				memset(fullPatternData + ((i + m_customLCOS_Height) *g_LCOS_Width) + slot_Start_Location, BackgroundColumnData[i+m_customLCOS_Height], slot_Width_inPixels* sizeof(char));
			}
			else
			{
				memset((fullPatternData + ((i + m_customLCOS_Height) *g_LCOS_Width) + slot_Start_Location), channelColumnData[0][i + m_customLCOS_Height *chNum], sizeof(char));
				memset((fullPatternData + ((i + m_customLCOS_Height) *g_LCOS_Width) + slot_Start_Location + 1), value, (slot_Width_inPixels-2)* sizeof(char));
				memset((fullPatternData + ((i + m_customLCOS_Height) *g_LCOS_Width) + slot_Start_Location+(slot_Width_inPixels-1)), channelColumnData[2][i + m_customLCOS_Height *chNum], sizeof(char));
			}
#else
			memset((fullPatternData + ((i+m_customLCOS_Height) *g_LCOS_Width) - slot_Start_Location - slot_Width_inPixels), value, slot_Width_inPixels* sizeof(char));	// FLIP= F1 starts from RHS goes to LHS. 196275-191125  F2 <------ F1
#endif
		}
		i++;
	}

}

void PatternGenModule::RelocateSlot_SPI(unsigned int chNum, unsigned int slotNum, unsigned int totalSlots, double f1_PixelPos, double f2_PixelPos)
{
	int i = 0;

	int startMidGap = 0;
	int endMidGap = 0;

	int slot_Start_Location = 0;
	int slot_Width_inPixels = 0;

	int ch_start_pixelLocation = round(f1_PixelPos);
	int ch_end_pixelLocation = round(f2_PixelPos);

	int ch_width_inPixels = ch_end_pixelLocation - ch_start_pixelLocation + 1; // drc modified: start pixel no. start from 0, so need to +1

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
			if(g_spaCmd->g_cmdDecoder->GetPanelInfo().b_gapSet)
			{
#ifdef _TWIN_WSS_
				if(i >= g_spaCmd->g_cmdDecoder->GetPanelInfo().topGap)
				{
					value = channelColumnData[1][i + m_customLCOS_Height *chNum];
				}

				startMidGap = g_spaCmd->g_cmdDecoder->GetPanelInfo().middleGapPosition - g_spaCmd->g_cmdDecoder->GetPanelInfo().middleGap/2;
				endMidGap = g_spaCmd->g_cmdDecoder->GetPanelInfo().middleGapPosition + g_spaCmd->g_cmdDecoder->GetPanelInfo().middleGap/2;

				if(startMidGap < m_customLCOS_Height && endMidGap < m_customLCOS_Height)
				{
					if(i>=startMidGap && i <= endMidGap)
					{
						//value = m_backColor;
						value = BackgroundColumnData[i];
					}
				}
				else if (startMidGap < m_customLCOS_Height && endMidGap > m_customLCOS_Height)
				{
					if(i >= startMidGap)
					{
							//value = m_backColor;
						value = BackgroundColumnData[i];
					}
				}
#else
				if(i >= g_spaCmd->g_cmdDecoder->GetPanelInfo().topGap)
				{
					value = channelColumnData[1][i + m_customLCOS_Height *chNum];
				}
#endif
			}
			else
			{
				value = channelColumnData[1][i + m_customLCOS_Height *chNum];
			}

#ifndef _FLIP_DISPLAY_
			if(g_spaCmd->g_cmdDecoder->FG_Channel_DS_For_Pattern[g_moduleNum][chNum+1].slotBlockedOrNot[slotNum-1] == 1)
			{
				memset(fullPatternData + (i *g_LCOS_Width) + slot_Start_Location, BackgroundColumnData[i], slot_Width_inPixels* sizeof(char));
			}
			else
			{
				memset((fullPatternData + (i *g_LCOS_Width) + slot_Start_Location), channelColumnData[0][i + m_customLCOS_Height *chNum], sizeof(char));
				memset((fullPatternData + (i *g_LCOS_Width) + slot_Start_Location + 1), value, (slot_Width_inPixels-2)* sizeof(char));
				memset((fullPatternData + (i *g_LCOS_Width) + slot_Start_Location+(slot_Width_inPixels-1)), channelColumnData[2][i + m_customLCOS_Height *chNum], sizeof(char));
			}
#else
			memset((fullPatternData + ((m_customLCOS_Height -i) *g_LCOS_Width) - slot_Start_Location - slot_Width_inPixels), value, slot_Width_inPixels* sizeof(char));	// FLIP= F1 starts from RHS goes to LHS. 196275-191125  F2 <------ F1
#endif
		}
		else if (g_moduleNum == 2)	// Bottom side of LCOS
		{
			if(g_spaCmd->g_cmdDecoder->GetPanelInfo().b_gapSet){
				if(i <= (m_customLCOS_Height - g_spaCmd->g_cmdDecoder->GetPanelInfo().bottomGap)){
					value = channelColumnData[1][i + m_customLCOS_Height *chNum];
				}

				int startMidGap = g_spaCmd->g_cmdDecoder->GetPanelInfo().middleGapPosition - g_spaCmd->g_cmdDecoder->GetPanelInfo().middleGap/2;
				int endMidGap = g_spaCmd->g_cmdDecoder->GetPanelInfo().middleGapPosition + g_spaCmd->g_cmdDecoder->GetPanelInfo().middleGap/2;

				if(startMidGap > m_customLCOS_Height && endMidGap > m_customLCOS_Height){
					if(i>=(startMidGap%m_customLCOS_Height) && i <= (endMidGap%m_customLCOS_Height)){
						//value = m_backColor;
						value = BackgroundColumnData[i+m_customLCOS_Height];
					}
				}else if (startMidGap < m_customLCOS_Height && endMidGap > m_customLCOS_Height){
					if(i <= (endMidGap%m_customLCOS_Height)){
						//value = m_backColor;
						value = BackgroundColumnData[i+m_customLCOS_Height];
					}
				}
			}else{
				value = channelColumnData[1][i + m_customLCOS_Height *chNum];
			}

#ifndef _FLIP_DISPLAY_
			if(g_spaCmd->g_cmdDecoder->FG_Channel_DS_For_Pattern[g_moduleNum][chNum+1].slotBlockedOrNot[slotNum-1] == 1)
			{
				memset(fullPatternData + ((i + m_customLCOS_Height) *g_LCOS_Width) + slot_Start_Location, BackgroundColumnData[i+m_customLCOS_Height], slot_Width_inPixels* sizeof(char));
			}
			else
			{
				memset((fullPatternData + ((i + m_customLCOS_Height) *g_LCOS_Width) + slot_Start_Location), channelColumnData[0][i + m_customLCOS_Height *chNum], sizeof(char));
				memset((fullPatternData + ((i + m_customLCOS_Height) *g_LCOS_Width) + slot_Start_Location + 1), value, (slot_Width_inPixels-2)* sizeof(char));
				memset((fullPatternData + ((i + m_customLCOS_Height) *g_LCOS_Width) + slot_Start_Location+(slot_Width_inPixels-1)), channelColumnData[2][i + m_customLCOS_Height *chNum], sizeof(char));
			}
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
			g_patternCalib->Set_Sigma_Args(ins.ch_adp,ins.ch_fc,g_LCOS_Temp, ins.ch_cmp);
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
//			std::cout << "INPUT ATT: " << ins.ch_att << std::endl;
		}

		if(interpolatePixelPos)
		{
			g_patternCalib->Set_Pixel_Pos_Args(ins.ch_f1, ins.ch_f2, ins.ch_fc, g_LCOS_Temp);
//			std::cout << "INPUT F1: " << ins.ch_f1 << std::endl;
//			std::cout << "INPUT F2: " << ins.ch_f2 << std::endl;
//			std::cout << "INPUT FC: " << ins.ch_fc << std::endl;
//			std::cout << "INPUT Temp: " << g_LCOS_Temp << std::endl;
			g_ready4 = true;					// Setting it to false will cause no calculation for that thread
		}

		pthread_cond_broadcast(&cond);		// broadcast signal to all threads which are waiting on condition

		if (pthread_mutex_unlock(&global_mutex[lOCK_CALIB_PARAMS]) != 0)	// Unlocking and checking the result, if lock was successful and no deadlock happened
			std::cout << "global_mutex[LOCK_CALIB_PARAMS] unlock unsuccessful" << std::endl;
	}

	usleep(80);	// Not necessary but just to ensure calibration threads have mutex access so they can signal result ready

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
//				std::cout << "OUTPUT Katt " << outs.Katt << std::endl;
			}

			if(interpolatePixelPos)   //drc to check how to 
			{
				outs.F1_PixelPos = g_patternCalib->Pixel_Pos_params.result_F1_PixelPos;
				outs.F2_PixelPos = g_patternCalib->Pixel_Pos_params.result_F2_PixelPos;
				outs.FC_PixelPos = g_patternCalib->Pixel_Pos_params.result_FC_PixelPos = (g_patternCalib->Pixel_Pos_params.result_F1_PixelPos + g_patternCalib->Pixel_Pos_params.result_F2_PixelPos)/2;
//				std::cout << "OUTPUT P1 " << outs.F1_PixelPos << std::endl;
//				std::cout << "OUTPUT P2 " << outs.F2_PixelPos << std::endl;
//				std::cout << "OUTPUT PC " << outs.FC_PixelPos << std::endl;

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

	constexpr double maxPhase = 2.2;
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

void PatternGenModule::Create_Linear_LUT(double phaseDepth)
{
	int lutSize = round(phaseDepth*(180));					// Not divide by PI because phaseDepth has no PI in it
	//int lutSize = round(g_phaseDepth*(180));					// Dr.Du said always create LUT for 2.2 PI and we can change range of LUT using phaseDepth
	//std::cout << "lutSize " <<lutSize <<std::endl;

	int phaseLow = 0;
	int phaseHigh = lutSize;
	int grayLow = 0;
	int grayHigh = 255;

	// y = mx + b

	double m = (double)(grayHigh - grayLow)/(phaseHigh - phaseLow);

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
 //       std::cout << "[Background_LUT] File has been opened" << std::endl;
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
    	 // Trim leading/trailing whitespace
		line.erase(0, line.find_first_not_of(" \t"));
		line.erase(line.find_last_not_of(" \t") + 1);
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

                // Trim key and value
				key.erase(0, key.find_first_not_of(" \t"));
				key.erase(key.find_last_not_of(" \t") + 1);
				value.erase(0, value.find_first_not_of(" \t"));
				value.erase(value.find_last_not_of(" \t") + 1);

                // load parameters into data structure to be used by pattern generation
                if(key == "SIGMA"){
					Module_Background_DS_For_Pattern[mod].SIGMA = stof(value);
//					std::cout << "Module: "<< mod <<"SIGMA:" << Module_Background_DS_For_Pattern[mod].SIGMA << std::endl;
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
//                	std::cout << "Module:" << mod <<" F1_PixelPos:" << Module_Background_DS_For_Pattern[mod].F1_PixelPos << std::endl;
                }
                else if(key == "F2_PixelPos"){
                	Module_Background_DS_For_Pattern[mod].F2_PixelPos = stof(value);
                }
                // if more parameters are needed to be adjusted for optimal pattern, add here for more parameters loading
                else if(key == "CH_PD"){
                	g_phaseDepth[mod] = stof(value);
//                	std::cout << "[Background_LUT] PD for Channel patterns" << std::endl;
                }
                else{
                	std::cout << "[Background_LUT] File need more readings" << std::endl;
                }
            }
        }
     }
     file.close();
     return(0);
}


void PatternGenModule::Save_Pattern_In_FileSystem(void)
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

void PatternGenModule::Find_LinearPixelPos_DevelopMode(double &freq, double &pixelPos)
{
	double fc_range_low = VENDOR_FREQ_RANGE_LOW;
	double fc_range_high = VENDOR_FREQ_RANGE_HIGH;

	//double slope = (fc_range_high-fc_range_low)/(g_LCOS_Width-1);		// Linear relationship between frequency distribution vs pixel number
//	double slope = (fc_range_high-fc_range_low)/g_LCOS_Width;    //drc modified

	  // y = fc_range_low + m(x)
	  // freq- fc_range_low/m

//	pixelPos = (freq-VENDOR_FREQ_RANGE_LOW)/slope; //drc modified to no round for edge attenuation

	pixelPos = (freq-VENDOR_FREQ_RANGE_LOW)*g_LCOS_Width/WHOLE_BANDWIDTH;    // avoid floating point calculation loss of precision

	if(pixelPos < 0)
		pixelPos = 0;
	else if (pixelPos > g_LCOS_Width)  //drc modified to no round for edge attenuation
		pixelPos = g_LCOS_Width;
}

//drc added for calculate pattern for background when startup for two modules
void PatternGenModule::loadBackgroundPattern()
{
	OCMTransfer ocmTrans;
	unsigned char mod = 1;

	double sigma = Module_Background_DS_For_Pattern[mod].SIGMA;
	double Aopt = Module_Background_DS_For_Pattern[mod].A_OPP;
	double Kopt = Module_Background_DS_For_Pattern[mod].K_OPP;
	double Aatt = Module_Background_DS_For_Pattern[mod].A_ATT;
	double Katt = Module_Background_DS_For_Pattern[mod].K_ATT;
	double wavelength = Module_Background_DS_For_Pattern[mod].LAMDA;
	double FC_PixelPos = Module_Background_DS_For_Pattern[mod].FC_PixelPos;
	double F1_PixelPos = Module_Background_DS_For_Pattern[mod].F1_PixelPos;
	double F2_PixelPos = Module_Background_DS_For_Pattern[mod].F2_PixelPos;

	g_moduleNum = mod;
	g_phaseDepth[mod] = Module_Background_DS_For_Pattern[mod].PD;

	Calculate_Pattern_Formulas(0, wavelength, g_pixelSize, sigma, Aopt, Kopt, Aatt, Katt);
	//std::cout << "Calculate_Every_ChannelPattern_DevelopMode  slotSize == 'T'" << std::endl;
	memcpy(BackgroundColumnData, &channelColumnData[1], m_customLCOS_Height); // obtain background column grayvalue
	RelocateChannelTF(0, F1_PixelPos, F2_PixelPos, FC_PixelPos);

#ifdef _TWIN_WSS_
	mod++;
	g_moduleNum = mod;
	g_phaseDepth[mod] = Module_Background_DS_For_Pattern[mod].PD;    // background pattern's
	sigma = Module_Background_DS_For_Pattern[mod].SIGMA;
	Aopt = Module_Background_DS_For_Pattern[mod].A_OPP;
	Kopt = Module_Background_DS_For_Pattern[mod].K_OPP;
	Aatt = Module_Background_DS_For_Pattern[mod].A_ATT;
	Katt = Module_Background_DS_For_Pattern[mod].K_ATT;
	wavelength = Module_Background_DS_For_Pattern[mod].LAMDA;
	FC_PixelPos = Module_Background_DS_For_Pattern[mod].FC_PixelPos;
	F1_PixelPos = Module_Background_DS_For_Pattern[mod].F1_PixelPos;
	F2_PixelPos = Module_Background_DS_For_Pattern[mod].F2_PixelPos;

	Calculate_Pattern_Formulas(0, wavelength, g_pixelSize, sigma, Aopt, Kopt, Aatt, Katt);
	//std::cout << "Calculate_Every_ChannelPattern_DevelopMode  slotSize == 'T'" << std::endl;
	memcpy(BackgroundColumnData + m_customLCOS_Height, &channelColumnData[1], m_customLCOS_Height);
	RelocateChannelTF(0, F1_PixelPos, F2_PixelPos, FC_PixelPos);

#endif
	if(ocmTrans.SendPatternData(fullPatternData) == 0)
		std::cerr << "Background Pattern Transfer Success!!\n";
	else {
		FaultsAttr attr = {0};
		attr.Raised = true;
		attr.RaisedCount += 1;
		attr.Degraded = false;
		attr.DegradedCount = attr.RaisedCount;
		FaultMonitor::logFault(TRANSFER_FAILURE, attr);
	}
#ifdef _FETCH_PATTERN_
	Save_Pattern_In_FileSystem();
#endif
	Load_Background_LUT(); //recover PD for worker channel patterns's
}

#ifdef _OCM_SCAN_

void PatternGenModule::CalculateOCMPattern(void)
{
	static int i = 0;
//	OCMTransfer ocmTrans;
	unsigned char mod = 0;
	double sigma =0;
	double Aopt = 0;
	double Kopt = 0;
	double Aatt = 0;
	double Katt = 0;
	double wavelength = 0;

	double F1_PixelPos = 0;
	double F2_PixelPos = 0;

	double FC_PixelPos = 0;

//	do
	if(g_serialMod->cmd_decoder.GetPanelInfo().b_OCMSet)
	{
		refreshBackgroundPattern();

		mod = 1;
		g_moduleNum = mod;

		sigma = g_serialMod->cmd_decoder.TF_Channel_DS_For_OCM[0][0].OCM_SIGMA;
		Aopt = g_serialMod->cmd_decoder.TF_Channel_DS_For_OCM[0][0].OCM_A_OPP;
		Kopt = g_serialMod->cmd_decoder.TF_Channel_DS_For_OCM[0][0].OCM_K_OPP;
		Aatt = g_serialMod->cmd_decoder.TF_Channel_DS_For_OCM[0][0].A_ATT;
		Katt = g_serialMod->cmd_decoder.TF_Channel_DS_For_OCM[0][0].K_ATT;
		wavelength = g_serialMod->cmd_decoder.TF_Channel_DS_For_OCM[0][0].LAMDA;

		F1_PixelPos = g_serialMod->cmd_decoder.TF_Channel_DS_For_OCM[0][0].OCM_P1 + i;

		F2_PixelPos = F1_PixelPos + OCMPattern_Para[mod].bw;

		FC_PixelPos = F1_PixelPos + OCMPattern_Para[mod].bw/2;
		std::cout << "!!!FC:" << FC_PixelPos << "!!!" <<std::endl;
		Calculate_Pattern_Formulas(mod, OCM_SCAN_CHANNEL, wavelength, g_pixelSize, sigma, Aopt, Kopt, Aatt, Katt);
		RelocateChannelTF(OCM_SCAN_CHANNEL, F1_PixelPos, F2_PixelPos, FC_PixelPos);

		sleep(1);

		mod++;
		g_moduleNum = mod;
		sigma = g_serialMod->cmd_decoder.TF_Channel_DS_For_OCM[0][0].OCM_SIGMA;
		Aopt = g_serialMod->cmd_decoder.TF_Channel_DS_For_OCM[0][0].OCM_A_OPP;
		Kopt = g_serialMod->cmd_decoder.TF_Channel_DS_For_OCM[0][0].OCM_K_OPP;
		Aatt = g_serialMod->cmd_decoder.TF_Channel_DS_For_OCM[0][0].A_ATT;
		Katt = g_serialMod->cmd_decoder.TF_Channel_DS_For_OCM[0][0].K_ATT;
		wavelength = g_serialMod->cmd_decoder.TF_Channel_DS_For_OCM[0][0].LAMDA;
/*
		F1_PixelPos = OCMPattern_Para][mod].f1_startpixelpos + i;
		F2_PixelPos = F1_PixelPos + OCMPattern_Para[mod].bw;
		FC_PixelPos = F1_PixelPos + bw/2;
*/
		Calculate_Pattern_Formulas(mod, OCM_SCAN_CHANNEL, wavelength, g_pixelSize, sigma, Aopt, Kopt, Aatt, Katt);
		RelocateChannelTF(OCM_SCAN_CHANNEL, F1_PixelPos, F2_PixelPos, FC_PixelPos);

//		if(ocmTrans.SendPatternData(fullPatternData) == 0)
//			std::cerr << "OCM Pattern Transfer Success!!\n";
//		g_serialMod->cmd_decoder.SetPatternTransferFlag(true);

#ifdef _FETCH_PATTERN_
		Save_Pattern_In_FileSystem();
		g_serialMod->Serial_WritePort("FF\n");	// Fetch File string send to PC software to start fetching

#endif

		i++;
//		if(F2_PixelPos >= g_serialMod->cmd_decoder.TF_Channel_DS_For_OCM[0][0].OCM_P2)
		if(g_serialMod->cmd_decoder.GetPanelInfo().b_OCMSet == 2)
			i = 0;
	}
//	}while(++i && F2_PixelPos <= OCMPattern_Para[mod].f2_endpixelpos);


}

#endif



void PatternGenModule::loadOneColorPattern(unsigned int colorVal)
{
//	if(colorVal == 255 || colorVal == 0) //single color(grayscale for whole panel) as background
//	{
	memset(fullPatternData, static_cast<unsigned char>(colorVal), sizeof(unsigned char)*g_LCOS_Width*g_LCOS_Height);
//	}
}

void PatternGenModule::refreshBackgroundPattern(void)
{
	for(int i = 0; i < g_LCOS_Height; i++)
	{
		memset(fullPatternData + (i *g_LCOS_Width), BackgroundColumnData[i], g_LCOS_Width*sizeof(char)); // obtain background column grayvalue
	}
}
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

//		std::cout << "Pattern File Is Loaded by " << __func__ << " at line " << __LINE__ << " on " << __DATE__ << std::endl;
		in.close();
	}

}




