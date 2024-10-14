/*
 * CmdDecoder.cpp
 *
 *  Created on: Jan 19, 2023
 *      Author: mib_n
 */

#include <iostream>
#include <fstream>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdc++.h>		// uppercase or lowercase using STL.  std::transform()
#include <cstring>
#include <pthread.h>
#include <cmath>
//#include <sys/reboot.h>

#include "InterfaceModule/OCMTransfer.h"
#include "InterfaceModule/Dlog.h"
#include "CmdDecoder.h"

extern double g_direct_LCOS_Temp;
extern double g_direct_Hearter2_Temp;


std::string out;
using namespace std;

CmdDecoder::CmdDecoder() {
	//Initial Module definition, user can changed this
	eModule1 = TF;
//	eModule2 = FIXEDGRID;
	eModule2 = FIXEDGRID;
	arrModules[0].slotSize = "TF";
	arrModules[1].slotSize = "625";
//	arrModules[1].slotSize = "TF";

	strcpy(customerInfo, "BAIANTEK");

}

CmdDecoder::~CmdDecoder()
{
	delete[] TF_Channel_DS;
	delete[] TF_Channel_DS_For_Pattern;
	delete[] FG_Channel_DS;
	delete[] FG_Channel_DS_For_Pattern;

	delete actionSR;

}

void CmdDecoder::WaitPatternTransfer(void)
{
    time_t start_time = time(NULL);
	const int target_duration = 3;

	while(true)
	{
		if(GetPatternTransferFlag() == true)
		{
			break;
		}

		usleep(100);

        time_t current_time = time(NULL);

        double elapsed_time = difftime(current_time, start_time);

        if (elapsed_time >= target_duration) {
        	std::cout << "[WaitPatternTransfer] TIMEOUT...." << std::endl;
            break;
        }
	}
}

bool CmdDecoder::GetPatternTransferFlag(void)
{
	bool flag;

	if (pthread_mutex_lock(&global_mutex[LOCK_TEMP_CHANGED_FLAG]) != 0)
		std::cout << "global_mutex[LOCK_TEMP_CHANGED_FLAG] lock unsuccessful" << std::endl;
	else
	{
		flag = g_bTransferFinished;

		if (pthread_mutex_unlock(&global_mutex[LOCK_TEMP_CHANGED_FLAG]) != 0)
			std::cout << "global_mutex[LOCK_TEMP_CHANGED_FLAG] unlock unsuccessful" << std::endl;
	}

	return (flag);
}

void CmdDecoder::SetPatternTransferFlag(bool flag)
{
	if (pthread_mutex_lock(&global_mutex[LOCK_TEMP_CHANGED_FLAG]) != 0)
		std::cout << "global_mutex[LOCK_TEMP_CHANGED_FLAG] lock unsuccessful" << std::endl;
	else
	{
		g_bTransferFinished = flag;

		if (pthread_mutex_unlock(&global_mutex[LOCK_TEMP_CHANGED_FLAG]) != 0)
			std::cout << "global_mutex[LOCK_TEMP_CHANGED_FLAG] unlock unsuccessful" << std::endl;
	}
}

std::string& CmdDecoder::ReceiveCommand(const std::string& recvCommand)
{
	ResetGlobalVariables();

	int searchDone;											// If -ve then search fail
	int commandCount = 1;									// IMPORTANT: first command in concatenated commands must have verb, rest of the commands following can have no verb, is ok.

	vector<string> commandVector;
	cout << "Command Received: " << recvCommand <<endl;

	int status = getCommandFormat(commandVector, recvCommand);

	if (status == CommandFormat::SINGLE_CMD)
	{
	   //SINGLE COMMAND FOUND. SEND FOR SEARCH
		searchDone = ZTEDecodeCommand(commandVector, commandCount);	// Send single command for searching verbs, object, attributes...

		postProcessSingleCommandResult(&searchDone);
	}
	else if (status == CommandFormat::CONCAT_CMD)
	{
		//CONCAT COMMAND FOUND. EXTRACT SINGLE COMMANDS & SEND FOR SEARCH
		for (auto singleCommand: commandVector)					// Vector 'commandVector contains multiple commands here, use them one by one to look for attributes
		{
			vector<string> singleCommandVector;

			int status = preProcessConcatCommand(singleCommandVector, singleCommand, &commandCount);	//singleCommandVector is the vector where VERB,OBJECT, ATTRIBUTES are divided into different indexes

			if(status == PreProcessConcat::SUCCESS)
			{
				searchDone = ZTEDecodeCommand(singleCommandVector, commandCount);	// Send vector of attributes to search and load structure of each channel and so on

				postProcessSingleCommandResult(&searchDone);

				if (searchDone == -1)
				{
					break;		// Immediately break the loop and don't process other concat commands
				}
			}
			else
			{
				cout << "\01ERROR: Missing ':' in the command\04" << endl;
				searchDone = -1;
				break;			// Immediately break the loop and don't process any commands
			}

			commandCount++;
		}
	}
	else
	{
		//PrintResponse("\01WARNING: Invalid Command Format\04", ERROR_MSG);
		cout << "WARNING: Invalid Command Format" <<endl;
	}

	/************************************************************************/
	/*	    DO OVERLAP TEST -- AFTER TAKING ALL THE NEW DATA FROM USER      */
	/************************************************************************/


#ifdef _TEST_OVERLAP_

	if (searchDone != -1)	// We only do overlap test if previous searches were successful
	{
		searchDone = ChannelsOverlapTest();	// Return -1 means error and channels are overlapping
	}

#endif

	/************************************************************************/
	/*	    SEND USER THE OUTPUT FOR GET COMMANDS &OTHER RESPONSES         */
	/************************************************************************/

	if ((searchDone != -1) && b_SendString && (eVerb == GET))	// Place Delimiters and send the command if searchDone was successful in GET command
	{
		out = "\01";
		out += buff;
		out += "\04";

		return (out);
	}

	/*************************************************************************************/
	/*	    IF COMMAND SUCCESSFUL-> COPY PARAMETERS OF ATTRIBUTES TO NEW STRUCTURE       */
	/*************************************************************************************/

	if ((searchDone != -1) && ((eVerb == SET) || (eVerb == ADD) || (eVerb == DELETE)))	// command was all successful
	{
		CopyDataStructures();	// Call function and copy data or wait  until pattern finish reading data and then copy.

		PrintResponse("\01OK\04", NO_ERROR);

		WaitPatternTransfer();				// This wait is necessary because other modules can modify the PrintResponse if there is any error

	}
	else if((searchDone != -1) && (eVerb == ACTION && eObject == MODULE)) //drc added for store and restore
	{
		PrintResponse("\01OK\04", NO_ERROR);
	}
	else	// In case of command error or failure
	{
		ResetDataStructures();

		PrintResponse("\01INVALID_COMMAND_ACTION\04", ERROR_LOW_PRIORITY);			// Low priority because if no error from ZTE table happen we issue this one.
	}

	b_SendString = true;	// Set that we are about to send string

	return (out = buff);
}

int CmdDecoder::getCommandFormat(std::vector<std::string>& commandVector, const std::string& recvCommand)
{
	int status;

	commandVector = SplitCmd(recvCommand, ";");

	if(commandVector[0] == "-1")			// Means command don't have ';' means we use the command (recvCommand) to look for ':'  (not concatenated)
	{
		commandVector.clear();

		commandVector = SplitCmd(recvCommand, ":");

		if(commandVector[0] == "-1")
		{
			status = CommandFormat::INVALID_CMD;
		}
		else
		{
			status = CommandFormat::SINGLE_CMD;
		}

	}
	else
	{
		status = CommandFormat::CONCAT_CMD;
	}

	return (status);
}

int CmdDecoder::ZTEDecodeCommand(std::vector<std::string> &singleCommandVector, int commandCount)
{
	int commandItems = singleCommandVector.size();		// Include verb, object and attributes ---->	/**  ADD	 **/

	if(commandCount == 1)						// First 1 command
	{
		g_totalAttributes = commandItems -2;	// -2 to remove VERB AND OBJECT count

		if (commandItems <= 2)								// Only got verb and object and no attribute
			g_bNoAttribute = true;
		else												//Attributes are available
			g_bNoAttribute = false;
	}
	else if (commandCount > 1)					// Remainign Concat commands
	{
		g_totalAttributes = commandItems -1;	// -1 to remove OBJECT only. Not verb in concat other commands

		if (commandItems <= 1)								// Only got verb and object and no attribute
			g_bNoAttribute = true;
		else												//Attributes are available
			g_bNoAttribute = false;
	}


	for (int index = 0; index < commandItems; index++)
	{
		if(commandCount == 1)
		{
			if (index == 0)
			{
				/******************************/
				/*	    SEARCH ONLY VERB      */
				/******************************/

				int status = SearchVerb(singleCommandVector[index]);	// Returns search result as ENUM

				if (((status == VERB_NOTFOUND) || (status == VERB_WRONG)) && (commandCount == 1))
				{
					cout << "VERB NOT FOUND " << endl;
					return (-1);	// Break if verb not found and cmdType is SINGLE; Only for further if first Verb is correct, if first verb is wrong issue error and stop the whole string search
				}
			}
			else if (index == 1)
			{
				/******************************/
				/*	    SEARCH ONLY OBJECT    */
				/******************************/

				int status = SearchObject(singleCommandVector[index]);

				if (status == -1)			// Print_Search() function call here is only in case when no attribute exist, if attribute exists it will set the flag
					return (-1);			// Break if error in command

				if ((commandItems <= 2) && ((eVerb == SET) || (eVerb == ADD) || (eVerb == ACTION && eObject == MODULE) || (eVerb == ACTION && eObject == FWUPGRADE)))
				{
					std::cout << "ERROR: Command format is Invalid" << std::endl;
					PrintResponse("\01MISSING_ATTRIBUTE\04", ERROR_HI_PRIORITY);
					return (-1);	//Break
				}
				else if((commandItems > 2) && (eVerb == GET && (eObject == CH_TF || eObject == CH_FG)))
				{//get:ch.1:id:chaid:fc:bw:att

				}
			}
			else
			{
				/******************************/
				/*	  SEARCH ONLY ATTRIBUTES  */
				/******************************/

				g_currentAttributeCount = commandItems - index;		// How many attributes are there in once command
				g_bNoAttribute = false;	// Attribute exists

				// We will not call get attribute functions if usr has  *asterisk in object, because this function will be called from Object module. We only call this function when attribute is given by the user

				int status = SearchAttribute(singleCommandVector[index]);

				if(status == -1)
					return (-1);
			}
		}
		else if (commandCount > 1)
		{
			if (index == 0)
			{
				/******************************/
				/*	    SEARCH ONLY OBJECT    */
				/******************************/

				int status = SearchObject(singleCommandVector[index]);

				if (status == -1)		// Print_Search() function call here is only in case when no attribute exist, if attribute exists it will set the flag
					return (-1);		// Break if error in command

				if ((commandItems <= 1) && ((eVerb == SET) || (eVerb == ADD) || (eVerb == ACTION && eObject == MODULE) || (eVerb == ACTION && eObject == FWUPGRADE)))
				{
					std::cout << "ERROR: Command format is Invalid" << std::endl;
					PrintResponse("\01MISSING_ATTRIBUTE\04", ERROR_HI_PRIORITY);
					return (-1);	//Break
				}
			}
			else
			{
				/******************************/
				/*	  SEARCH ONLY ATTRIBUTES  */
				/******************************/

				g_currentAttributeCount = commandItems - index;		// How many attributes are there in once command
				g_bNoAttribute = false;	// Attribute exists

				// We will not call get attribute functions if usr has  *asterisk in object, because this function will be called from Object module. We only call this function when attribute is given by the user

				int status = SearchAttribute(singleCommandVector[index]);

				if(status == -1)
					return (-1);
			}
		}
	}

	return (0);	//SUCCESS
}

int CmdDecoder::preProcessConcatCommand(std::vector<std::string>& singleCommandVector, const std::string& singleCommand, int *commandCount)
{
	singleCommandVector = SplitCmd(singleCommand, ":");		// Divide singleCommand into vectors containing VERB,OBJECT,ATTRIBUTES...

	if(singleCommandVector[0] == "-1")
	{
		if(*commandCount == 1 || (*commandCount > 1 && (eVerb != GET) && (eVerb != DELETE)))	// First command must have : because VERB is always there. In Second and remaining commands : might not be present in case of GET & DELETE, such is possible-> GET:CH.1.1;CH.2.1  DELETE:CH.1.1;CH.2.1
		{
			return (PreProcessConcat::FAILED);
		}
		else if (*commandCount > 1 && ((eVerb == GET) || (eVerb == DELETE)))	// In case of GET & DELETE second concat command will not have VERB means no : so the error is ignored and -1 removed e.g. GET:CH.1.2:CH.1.3 <-----
		{
			singleCommandVector.erase(singleCommandVector.begin());	// Delete the first Index containing -1
			return (PreProcessConcat::SUCCESS);
		}

	}

	return (PreProcessConcat::SUCCESS);

}

void CmdDecoder::postProcessSingleCommandResult(int* searchDone)
{
	if ((*searchDone != -1) && (eObject == CH_FG) && g_edgeFreqDefined > 0)
	{
		int calculatedSlotsNumber = 0;
		double slotSize = 0;		// 6.25 or 12.5
		double newF1 = FG_Channel_DS[g_moduleNum][g_channelNum].F1;
		double newF2 = FG_Channel_DS[g_moduleNum][g_channelNum].F2;

		if (is_BWSlotSizeIntegral(&calculatedSlotsNumber, &newF1, &newF2, &slotSize) == 0)		// Success
		{
			int status = ModifySlotCountInChannel(&calculatedSlotsNumber, &newF1, &newF2, &slotSize);  //drc

			if (status == -1)
			{
				*searchDone = -1; // Error if not integral
			}
		}
		else
		{
			*searchDone = -1; // Error if not integral
		}
	}

#ifdef _DEVELOPMENT_MODE_
	if(structDevelopMode.developMode == 0)
	{
#endif
		if ((*searchDone != -1) && (eVerb == ADD))	// Test to see if all mandatory attributes are provided by user for ADD command, if not then error
		{
			int status = TestMandatoryAttributes();

			if(status == -1)
			{
				*searchDone = -1;
				printf("ADD -- Define all mandatory attributes\n");
			}
		}
#ifdef _DEVELOPMENT_MODE_
	}
#endif

#ifndef _TWIN_WSS_

	if ((*searchDone != -1) && (g_moduleNum == 2))	// If its not twin wss then we dont allow module number 2 command
	{
		*searchDone = -1;
		PrintResponse("\01Module 2 doesn't exist on Single WSS\04", ERROR_HI_PRIORITY);
	}
#endif

	changeF1 = false;		//reset
	changeF2 = false;		//reset
	g_edgeFreqDefined = 0; // reset
}

int CmdDecoder::TestMandatoryAttributes(void)
{
	// Test mandatory attributes of current global Module and global channel w.r.t global Object choosen i.e. TF or FG
	bool check = false;

	if(eObject == CH_TF)
	{
		check = TF_Channel_DS[g_moduleNum][g_channelNum].ADP &&
				TF_Channel_DS[g_moduleNum][g_channelNum].BW &&
				TF_Channel_DS[g_moduleNum][g_channelNum].FC &&
				TF_Channel_DS[g_moduleNum][g_channelNum].CMP;
	}
	else if (eObject == CH_FG)
	{
		check = FG_Channel_DS[g_moduleNum][g_channelNum].ADP &&
			    FG_Channel_DS[g_moduleNum][g_channelNum].BW &&
				FG_Channel_DS[g_moduleNum][g_channelNum].FC &&
				FG_Channel_DS[g_moduleNum][g_channelNum].F1 &&
				FG_Channel_DS[g_moduleNum][g_channelNum].F2 &&
				FG_Channel_DS[g_moduleNum][g_channelNum].CMP;
	}

	if(check != 0)
		return 0;		// SUCCESS
	else
		return -1;
}
vector<string> CmdDecoder::SplitCmd(string s, string delimiter)
{
	size_t pos_start = 0, pos_end, delim_len = delimiter.length();
	string token;
	vector<string> res;

	while ((pos_end = s.find(delimiter, pos_start)) != string::npos)
	{
		token = s.substr(pos_start, pos_end - pos_start);
		pos_start = pos_end + delim_len;
		res.push_back(token);
	}

	if (pos_start == 0)			// When no delimiter is available the pos_start = 0
	{
		if((delimiter == ".") && (pos_end = s.find("TEMP")) != string::npos)
		{
			token = s.substr(pos_end, 4);
			res.push_back(token);
			return(res);
		}
		else
			res.push_back("-1");	// Error code send at vector<string> 0th element
	}

	res.push_back(s.substr(pos_start));
	return (res);
}

int CmdDecoder::is_BWSlotSizeIntegral(int *calculatedSlotsNumber, const double *newF1, const double *newF2, double *slotSize)
{
	std::string str_slotSize = arrModules[g_moduleNum - 1].slotSize;	//Get the slot size of the module that is configured to fixed grid

	if(!(str_slotSize == "625" || str_slotSize == "125")){//drc to check if 3125 is needed to support
    	//ERROR
    	return(-1);
    }

	int m_slotSize = std::stoi(str_slotSize);

	if (m_slotSize != 0)
	{
		if (m_slotSize == 625)
		{
//			int Bandwidth = abs(*newF2 - *newF1)*1000;		// x1000 to avoid floating point error and do calculation in as integer
			double Bandwidth = *newF2 - *newF1;

//			if (((Bandwidth % m_slotSize) == 0))
			if(fmod(Bandwidth, 6.25) == 0)
			{
				// BW is integral number of slotsize
				*slotSize = 6.25;
				*calculatedSlotsNumber = abs(Bandwidth)/((*slotSize));	// divided 1000 to compensate for Bandwidth we multiplied by 1000
			}
			else
			{
				cout << "ERROR: The Bandwidth is not an integral number of Slotsize" << endl;
				return (-1);
			}
		}
		else if (m_slotSize == 125)
		{
//			int Bandwidth = abs(*newF2 - *newF1)*1000;		// x1000 to avoid floating point error and do calculation in as integer
			double Bandwidth = *newF2 - *newF1;

//			if (((Bandwidth % m_slotSize) == 0))
			if(fmod(Bandwidth, 12.5) == 0)
			{
				*slotSize = 12.5;
				*calculatedSlotsNumber = abs(Bandwidth)/ ((*slotSize));	// divided 1000 to compensate for Bandwidth we multiplied by 1000
			}
			else
			{
				cout << "ERROR: The Bandwidth is not an integral number of Slotsize" << endl;
				return (-1);
			}
		}
	}
	else
	{
    	return(-1);
	}

	if(*calculatedSlotsNumber == 0)
	{
		cout << "ERROR: Bandwidth of the channel < slotSize" << endl;
		return (-1);
	}

	return (0);
}

int CmdDecoder::ModifySlotCountInChannel(const int *calculatedSlotsNumber, const double *newF1, const double *newF2, const double *slotSize)
{
	if (eVerb == ADD)
	{
		FG_Channel_DS[g_moduleNum][g_channelNum].slotsATTEN.resize(*calculatedSlotsNumber);	//IMPORTANT: resize the vector to the NUMBER OF SLOTS in channel 0,1,2,3,4...
		FG_Channel_DS[g_moduleNum][g_channelNum].slotNum = *calculatedSlotsNumber;	// vector length is defined now. we can now add attentuation to any slot number
	}
	else if (eVerb == SET)
	{
		if (g_edgeFreqDefined == 1 && *newF2 != *newF1)	    // Only one edge frequency is modified only
		{
			if (changeF1 == true)	// User want to contract or expand the channel
			{
				prevF2 = *newF2;	// Since only F1 is changed then F2 is same as old one

				// CHANNEL EXPAND OR CONTRACT
					if (*newF1 > prevF1  && *newF1 < prevF2)
					{
						// Means f1 edge moved towards higher value but below F2 edge, means DELETE slots from FRONT
						int num_SlotToRemove = abs(FG_Channel_DS[g_moduleNum][g_channelNum].slotNum - *calculatedSlotsNumber);
						FG_Channel_DS[g_moduleNum][g_channelNum].slotsATTEN.erase(FG_Channel_DS[g_moduleNum][g_channelNum].slotsATTEN.begin(), FG_Channel_DS[g_moduleNum][g_channelNum].slotsATTEN.begin() + num_SlotToRemove);
					}
					else if (*newF1 < prevF1  && *newF1 < prevF2)
					{
						// Means f1 edge moved towards lower value, means INSERT slots from FRONT
						int num_SlotToInsert = abs(*calculatedSlotsNumber - FG_Channel_DS[g_moduleNum][g_channelNum].slotNum);

						vector<float>::iterator it = FG_Channel_DS[g_moduleNum][g_channelNum].slotsATTEN.begin();
						FG_Channel_DS[g_moduleNum][g_channelNum].slotsATTEN.insert(it, num_SlotToInsert, 0);	// Insert 0 at the location of new added slots
					}
					// CHANNEL MOVEMENT
					else if (*newF1 > prevF2  && *newF1 > prevF1)	// User is moving the channel
					{
						cout << "ERROR: F1 is beyond bandwidth" << endl;
						return (-1);
					}
					else if (*newF1 == prevF2)
					{
						cout << "ERROR: F1 & F2 can't be same" << endl;
						return (1);
					}
			}
			else if (changeF2 == true)
			{
				prevF1 = *newF1;	// Since only F1 is changed then F2 is same as old one

				if (*newF2 > prevF2 && *newF2 > prevF1)
				{
					// Means f2 edge moved towards higher value, means INSERT new slots from END
					int num_SlotToInsert = abs(*calculatedSlotsNumber - FG_Channel_DS[g_moduleNum][g_channelNum].slotNum);

					vector<float>::iterator it = FG_Channel_DS[g_moduleNum][g_channelNum].slotsATTEN.end();
					FG_Channel_DS[g_moduleNum][g_channelNum].slotsATTEN.insert(it, num_SlotToInsert, 0);	// Insert 0 at the location of new added slots
				}
				else if (*newF2 < prevF2 && *newF2 > prevF1)
				{
					// Means f2 edge moved towards lower value, means DELETE new slots from END
					int num_SlotToRemove = abs(FG_Channel_DS[g_moduleNum][g_channelNum].slotNum - *calculatedSlotsNumber);

					FG_Channel_DS[g_moduleNum][g_channelNum].slotsATTEN.erase(FG_Channel_DS[g_moduleNum][g_channelNum].slotsATTEN.end() - num_SlotToRemove, FG_Channel_DS[g_moduleNum][g_channelNum].slotsATTEN.end());
				}
				// CHANNEL MOVEMENT
				else if (*newF2 < prevF2  && *newF2 < prevF1)	// User is moving the channel
				{
					cout << "ERROR: F2 is beyond bandwidth" << endl;
					return (-1);
				}

				else if (*newF1 == prevF2)
				{
					cout << "ERROR: F1 & F2 can't be same" << endl;
					return (-1);
				}
			}

			FG_Channel_DS[g_moduleNum][g_channelNum].slotNum = *calculatedSlotsNumber;	// vector length is defined now. we can now add attentuation to any slot number
		}
		else if (g_edgeFreqDefined == 2	&& *newF2 != *newF1)	// Both edge frequencies are modified
		{
			if(FG_Channel_DS[g_moduleNum][g_channelNum].slotNum != *calculatedSlotsNumber)	// Only when bandwidth of prev channel is not same as new channel
				{
					if (*newF1 > prevF1  && *newF1 < prevF2)
					{
						// Means f1 edge moved towards higher value but below F2 edge, means DELETE slots from FRONT
						int num_SlotToRemove = (abs(*newF1 - prevF1))/(*slotSize);
						FG_Channel_DS[g_moduleNum][g_channelNum].slotsATTEN.erase(FG_Channel_DS[g_moduleNum][g_channelNum].slotsATTEN.begin(), FG_Channel_DS[g_moduleNum][g_channelNum].slotsATTEN.begin() + num_SlotToRemove);
					}
					else if (*newF1 < prevF1  && *newF1 < prevF2)
					{
						// Means f1 edge moved towards lower value, means INSERT slots from FRONT
						int num_SlotToInsert = (abs(prevF1 - *newF1))/(*slotSize);
						vector<float>::iterator it = FG_Channel_DS[g_moduleNum][g_channelNum].slotsATTEN.begin();
						FG_Channel_DS[g_moduleNum][g_channelNum].slotsATTEN.insert(it, num_SlotToInsert, 0);	// Insert 0 at the location of new added slots
					}


					if (*newF2 > prevF2 && *newF2 > prevF1)
					{
						// Means f2 edge moved towards higher value, means INSERT new slots from END
						int num_SlotToInsert = (abs(*newF2 - prevF2))/(*slotSize);
						vector<float>::iterator it = FG_Channel_DS[g_moduleNum][g_channelNum].slotsATTEN.end();
						FG_Channel_DS[g_moduleNum][g_channelNum].slotsATTEN.insert(it, num_SlotToInsert, 0);	// Insert 0 at the location of new added slots
					}
					else if (*newF2 < prevF2 && *newF2 > prevF1)
					{
						// Means f2 edge moved towards lower value, means DELETE new slots from END
						int num_SlotToRemove = (abs(prevF2 - *newF2))/(*slotSize);
						FG_Channel_DS[g_moduleNum][g_channelNum].slotsATTEN.erase(FG_Channel_DS[g_moduleNum][g_channelNum].slotsATTEN.end() - num_SlotToRemove, FG_Channel_DS[g_moduleNum][g_channelNum].slotsATTEN.end());
					}

					// CHANNEL MOVEMENT
					if (*newF1 > prevF2  && *newF1 > prevF1)	// User is moving the channel, move F1 to RIGHT
					{
						int num_SlotToRemove = (abs(prevF1 - *newF1))/(*slotSize);
						FG_Channel_DS[g_moduleNum][g_channelNum].slotsATTEN.erase(FG_Channel_DS[g_moduleNum][g_channelNum].slotsATTEN.begin(), FG_Channel_DS[g_moduleNum][g_channelNum].slotsATTEN.begin() + num_SlotToRemove);
					}
					// CHANNEL MOVEMENT
					else if (*newF2 < prevF1  && *newF2 < prevF2)	// User is moving the channel, move F2 to LEFT
					{
						int num_SlotToRemove = (abs(prevF2 - *newF2))/(*slotSize);	// BW/slotsize = slot numbers
						FG_Channel_DS[g_moduleNum][g_channelNum].slotsATTEN.erase(FG_Channel_DS[g_moduleNum][g_channelNum].slotsATTEN.end() - num_SlotToRemove, FG_Channel_DS[g_moduleNum][g_channelNum].slotsATTEN.end());
					}
					// CHANNEL MOVEMENT - SHIFT WHEN newF1 = PrevF2
					else if (*newF1 >= prevF2  && *newF2 > prevF2)	// User is moving the channel, move F2 to LEFT
					{
						int num_SlotToRemove = (abs(*newF1 - prevF1))/(*slotSize);	// BW/slotsize = slot numbers
						FG_Channel_DS[g_moduleNum][g_channelNum].slotsATTEN.erase(FG_Channel_DS[g_moduleNum][g_channelNum].slotsATTEN.begin(), FG_Channel_DS[g_moduleNum][g_channelNum].slotsATTEN.begin() + num_SlotToRemove);
					}
					// CHANNEL MOVEMENT - SHIFT WHEN newF2 = PrevF1
					else if (*newF2 <= prevF1  && *newF1 < prevF1)	// User is moving the channel, move F2 to LEFT
					{
						int num_SlotToRemove = (abs(prevF2 - *newF2))/(*slotSize);	// BW/slotsize = slot numbers
						FG_Channel_DS[g_moduleNum][g_channelNum].slotsATTEN.erase(FG_Channel_DS[g_moduleNum][g_channelNum].slotsATTEN.end() - num_SlotToRemove, FG_Channel_DS[g_moduleNum][g_channelNum].slotsATTEN.end());
					}
				}

			FG_Channel_DS[g_moduleNum][g_channelNum].slotNum = *calculatedSlotsNumber;
		}
		else
		{
			cout << "ERROR: F1 & F2 can't be same. Define both correctly" << endl;
			return (-1);
		}
	}
	else
	{
		cout << "ERROR: Wrong Verb ..." << endl;
		return (-1);
	}

	return (0);
}

bool CmdDecoder::PrintAllChannelsTF(int count)
{
	int c_channelActive = 0;
		// \01 delimiter added		//before--->>>> buffLenTemp += sprintf(&buff[buffLenTemp], "\01\nCH\t|\tADP\tATT\tCMP\tFc\tBW\n");		//Fill this string and get the length filled.
		if((g_moduleNum_prev != g_moduleNum)	|| g_bPrevAttrDisplayed)	// i.e GET:CH.1.2;CH.2.5	or if previously attributes were displayed. GET:CH.1.2:ATT:ADP;CH.1.5
		{
		//	buffLenTemp += sprintf(&buff[buffLenTemp], "\n");	//Fill this string and get the length filled.
		}

		for (int i = 1; i <= count; i++)
		{
			if (count == 1)
			{
				i = g_channelNum;
			}

			if (TF_Channel_DS[g_moduleNum][i].active)
			{
				c_channelActive++;

				if(c_channelActive != 1)
				{
					buffLenTemp += sprintf(&buff[buffLenTemp], ":\n");

				}
				//buffLenTemp += sprintf(&buff[buffLenTemp], "ID=%d\nMOD=%d\nADP=%d\nATT=%0.3f\nCMP=%d\nFC=%0.3f\nBW=%0.3f\n", i, g_moduleNum,TF_Channel_DS[g_moduleNum][i].ADP,
				//			TF_Channel_DS[g_moduleNum][i].ATT, TF_Channel_DS[g_moduleNum][i].CMP, TF_Channel_DS[g_moduleNum][i].FC, TF_Channel_DS[g_moduleNum][i].BW);
				buffLenTemp += sprintf(&buff[buffLenTemp], "ID=%d\nMOD=%d\nADP=%d\nCMP=%d\nF1=%0.3f\nF2=%0.3f\nATT=%0.3f\n", i, g_moduleNum,TF_Channel_DS[g_moduleNum][i].ADP,
											TF_Channel_DS[g_moduleNum][i].CMP, TF_Channel_DS[g_moduleNum][i].F1, TF_Channel_DS[g_moduleNum][i].F2, TF_Channel_DS[g_moduleNum][i].ATT);
			}
		}

		if (c_channelActive == 0)
		{
			memset(&buff, 0, sizeof(buff));
			sprintf(buff, "All Channels are INACTIVE");
		}

		return (0);
}

bool CmdDecoder::PrintAllChannelsFG(int count)
{
	int c_channelActive = 0;

	if((g_moduleNum_prev != g_moduleNum) || g_bPrevAttrDisplayed)	// i.e GET:CH.1.2;CH.2.5
	{
		buffLenTemp += sprintf(&buff[buffLenTemp], "\n");	//Fill this string and get the length filled.
	}
	for (int i = 1; i <= count; i++)
	{
		if (count == 1)
		{
			// if user send count =1 means he wants to get one certain channel values.. so we do i= objVec[2]	channel number
			i = g_channelNum;
		}

		if (FG_Channel_DS[g_moduleNum][i].active)
		{
			c_channelActive++;
			if(c_channelActive != 1){
				buffLenTemp += sprintf(&buff[buffLenTemp], ":\n");
			}
			buffLenTemp += sprintf(&buff[buffLenTemp], "ID=%d\nMOD=%d\nADP=%d\nATT=%0.3f\nCMP=%d\nF1=%0.3f\nF2=%0.3f\nSLOTNUM=%d\n", i, g_moduleNum,FG_Channel_DS[g_moduleNum][i].ADP,
								FG_Channel_DS[g_moduleNum][i].ATT, FG_Channel_DS[g_moduleNum][i].CMP, FG_Channel_DS[g_moduleNum][i].F1, FG_Channel_DS[g_moduleNum][i].F2, FG_Channel_DS[g_moduleNum][i].slotNum);
		}
	}


	if (c_channelActive == 0)
	{
		memset(&buff, 0, sizeof(buff));
		sprintf(buff, "All Channels are INACTIVE");
	}

	return (0);
}

void CmdDecoder::PrintAllSlotsFG(int SlotNum)
{
	for (int k = 0; k < SlotNum; k++)
	{

		buffLenTemp += sprintf(&buff[buffLenTemp],
								"id=%d "
								"\nmod=%d "
								"\nslot=%d "
								"\nadp=%d "
								"\ncmp=%d "
								"\nf1=%0.3f"
								"\nf2=%0.3f"
								"\nATT=%0.3f\n",  g_channelNum, g_moduleNum, k+1, FG_Channel_DS[g_moduleNum][g_channelNum].ADP, FG_Channel_DS[g_moduleNum][g_channelNum].CMP,
								FG_Channel_DS[g_moduleNum][g_channelNum].F1 + k*(FG_Channel_DS[g_moduleNum][g_channelNum].F2 - FG_Channel_DS[g_moduleNum][g_channelNum].F1)/FG_Channel_DS[g_moduleNum][g_channelNum].slotNum,
								FG_Channel_DS[g_moduleNum][g_channelNum].F1 + (k+1)*(FG_Channel_DS[g_moduleNum][g_channelNum].F2 - FG_Channel_DS[g_moduleNum][g_channelNum].F1)/FG_Channel_DS[g_moduleNum][g_channelNum].slotNum,
								FG_Channel_DS[g_moduleNum][g_channelNum].slotsATTEN[k]);
		if(k >= 0 && k < SlotNum-1)
		{
			buffLenTemp += sprintf(&buff[buffLenTemp], ":\n");
		}
	}

}

int CmdDecoder::ChannelsOverlapTest(void)
{
//	int ch = 1;

//	while (ch < g_Total_Channels)
	for(const auto& ch : activeChannels)
	{
		// last channel doesn't need to compare with anyone because 1st channel compare with 95 channels, 2nd with 94.. 3rd with 93... so on
		if (ch.moduleNo == g_moduleNum && TF_Channel_DS[g_moduleNum][ch.channelNo].active)
		{
			//Channel we are compare to others, we take that channel's f1,f2,fc first
			double ch_f1 = (TF_Channel_DS[g_moduleNum][ch.channelNo].FC - (TF_Channel_DS[g_moduleNum][ch.channelNo].BW / 2));
			double ch_f2 = (TF_Channel_DS[g_moduleNum][ch.channelNo].FC + (TF_Channel_DS[g_moduleNum][ch.channelNo].BW / 2));

//			int ch_compared_with = ch + 1;	// No need to compared channel to itself, always compare to other numbers
/*
			unsigned int couldbeSlotOf = 0;
			if(TF_Channel_DS[g_moduleNum][ch.channelNo].BW > 0 && TF_Channel_DS[g_moduleNum][ch.channelNo].BW < VENDOR_BW_RANGE_LOW)
			{
				couldbeSlotOf++;
			}
*/

//			while (ch_compared_with <= g_Total_Channels)
			for(const auto& ch_compared_with : activeChannels)
			{
				if (ch_compared_with.moduleNo == g_moduleNum && ch_compared_with.channelNo != ch.channelNo)
				{
					double f1 = (TF_Channel_DS[g_moduleNum][ch_compared_with.channelNo].FC - (TF_Channel_DS[g_moduleNum][ch_compared_with.channelNo].BW / 2));
					double f2 = (TF_Channel_DS[g_moduleNum][ch_compared_with.channelNo].FC + (TF_Channel_DS[g_moduleNum][ch_compared_with.channelNo].BW / 2));

					int status = Overlap_Logic(&ch_f1, &ch_f2, &f1, &f2);

					if(status == -1)
					{
						cout << "ERROR: The channel BW is overlapping with channel:" << ch_compared_with.channelNo << endl;
						return (-1);
					}
/*
					if(TF_Channel_DS[g_moduleNum][ch_compared_with.channelNo].BW > 0 && TF_Channel_DS[g_moduleNum][ch_compared_with.channelNo].BW < VENDOR_BW_RANGE_LOW)
					{
						couldbeSlotOf++;
					}

					if(couldbeSlotOf > 0 )
					{
						int status = CouldbeSlotOf_Logic(&ch_f1, &ch_f2, &f1, &f2);
						if(status != -1 && status != -2)
						{
							cout << "ERROR: The channel is not allowed to combine with other channel"<<endl;
							return (-1);
						}
					}
*/

				}

//				ch_compared_with++;
			}
		}

		//Fixed Grid test
		if (ch.moduleNo == g_moduleNum && FG_Channel_DS[g_moduleNum][ch.channelNo].active)
		{
			//Channel we are compare to others, we take that channel's f1,f2,fc first
			double ch_f1 = FG_Channel_DS[g_moduleNum][ch.channelNo].F1;
			double ch_f2 = FG_Channel_DS[g_moduleNum][ch.channelNo].F2;

//			int ch_compared_with = ch + 1;	// No need to compared channel to itself, always compare to other numbers
/*
			unsigned int couldbeSlotOf = 0;
			if(FG_Channel_DS[g_moduleNum][ch.channelNo].BW > 0 && FG_Channel_DS[g_moduleNum][ch.channelNo].BW < VENDOR_BW_RANGE_LOW)
			{
				couldbeSlotOf++;
			}
*/
//			while (ch_compared_with <= g_Total_Channels)
			for(const auto& ch_compared_with : activeChannels)
			{
				if (ch_compared_with.moduleNo == g_moduleNum && ch_compared_with.channelNo != ch.channelNo)
				{
					double f1 = FG_Channel_DS[g_moduleNum][ch_compared_with.channelNo].F1;
					double f2 = FG_Channel_DS[g_moduleNum][ch_compared_with.channelNo].F2;

					int status = Overlap_Logic(&ch_f1, &ch_f2, &f1, &f2);

					if(status == -1)
					{
						cout << "ERROR: The channel BW is overlapping with channel:" << ch_compared_with.channelNo << endl;
						return (-1);
					}
/*
					if(FG_Channel_DS[g_moduleNum][ch_compared_with.channelNo].BW > 0 && FG_Channel_DS[g_moduleNum][ch_compared_with.channelNo].BW < VENDOR_BW_RANGE_LOW)
					{
						couldbeSlotOf++;
					}

					if(couldbeSlotOf > 0 )
					{
						int status = CouldbeSlotOf_Logic(&ch_f1, &ch_f2, &f1, &f2);
						if(status != -1 && status != -2)
						{
							cout << "ERROR: The channel is not allowed to combine with other channel"<<endl;
							return (-1);
						}
					}
*/
				}

//				ch_compared_with++;
			}
		}
//		ch++;
	}

	return (0);
}

int CmdDecoder::Overlap_Logic(const double *ch_f1, const double *ch_f2, const double *other_ch_f1, const double *other_ch_f2)
{
	if (*ch_f1 > *other_ch_f1 && *ch_f1 < *other_ch_f2)	// Edge freqs same are OK
	{
		return (-1);
	}
	else if (*ch_f2 > *other_ch_f1 && *ch_f2 < *other_ch_f2)	// Edge freqs same are OK
	{
		return (-1);
	}
	else if ((*ch_f1 <= *other_ch_f1 && *ch_f1 <= *other_ch_f2) && (*ch_f2 >= *other_ch_f1 && *ch_f2 >= *other_ch_f2))		// when user try to add channal inside another channel
	{
		return (-1);
	}

	return 0;
}

int CmdDecoder::CouldbeSlotOf_Logic(const double *ch_f1, const double *ch_f2, const double *other_ch_f1, const double *other_ch_f2)
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

void CmdDecoder::CopyDataStructures(void)
{
	if (pthread_mutex_lock(&global_mutex[LOCK_CHANNEL_DS]) != 0)
		std::cout << "global_mutex[LOCK_CHANNEL_DS] lock unsuccessful" << std::endl;
	else
	{
		std::copy(&TF_Channel_DS[0][0], &TF_Channel_DS[0][0] + 3 * (g_Total_Channels), &TF_Channel_DS_For_Pattern[0][0]);	//3 * 97 is the size of array we defined
		std::copy(&FG_Channel_DS[0][0], &FG_Channel_DS[0][0] + 3 * (g_Total_Channels), &FG_Channel_DS_For_Pattern[0][0]);

		g_bNewCommandData = true;
		if (pthread_mutex_unlock(&global_mutex[LOCK_CHANNEL_DS]) != 0)
			std::cout << "global_mutex[LOCK_CHANNEL_DS] unlock unsuccessful" << std::endl;
	}

}

void CmdDecoder::ResetDataStructures(void)
{
	// Mutex is not needed because we are performing read operation from Pattern Data Structure & Pattern can't change DS
	// IMPORTANT: Revert back to last known correct data structure
	std::copy(&TF_Channel_DS_For_Pattern[0][0], &TF_Channel_DS_For_Pattern[0][0] + 3 * (g_Total_Channels), &TF_Channel_DS[0][0]);	//3 * 97 is the size of array we defined
	std::copy(&FG_Channel_DS_For_Pattern[0][0], &FG_Channel_DS_For_Pattern[0][0] + 3 * (g_Total_Channels), &FG_Channel_DS[0][0]);
}

void CmdDecoder::PrintResponse(const std::string &strSend, const enum PrintType &currentErrorType)
{
	if(currentErrorType == ERROR_HI_PRIORITY && ePrevErrorType != ERROR_HI_PRIORITY)		// Reset buffer because we only show High priority message
	{
		memset(&buff, 0, sizeof(buff));	// Fill Reset Buffer
		buffLenTemp = 0;	//  Reset Buffer length
	}
	/*
	 * High and High === BOTH
	 * Low and High == HIGH
	 * High and low == HIGH
	 * Low and Low == BOTH
	 */

	if((ePrevErrorType == currentErrorType) || (currentErrorType == NO_ERROR) ||					// If current is HI, we remove low and show high error only
			(ePrevErrorType == NO_ERROR && currentErrorType == ERROR_LOW_PRIORITY) ||
			(ePrevErrorType == ERROR_HI_PRIORITY && currentErrorType == ERROR_HI_PRIORITY) ||
			(ePrevErrorType == ERROR_LOW_PRIORITY && currentErrorType == ERROR_LOW_PRIORITY) ||
			(ePrevErrorType == NO_ERROR && currentErrorType == ERROR_HI_PRIORITY))
	{
		const char *str = &strSend[0];
		buffLenTemp += sprintf(&buff[buffLenTemp], str);	//Set Error Message
		buffLenTemp += sprintf(&buff[buffLenTemp], "\n");
	}

	ePrevErrorType = currentErrorType;
}

void CmdDecoder::ResetGlobalVariables(void)
{
	memset(&buff, 0, sizeof(buff));
	buffLenTemp = 0;

	// Reset Enum variables
	eVerb = NONE;
	eGet = SOME_ATTR;
	eObject = NONE_O;

	// Reset Channel variables
	g_moduleNum = 0;
	g_moduleNum_prev = 0;
	g_channelNum = 0;
	g_slotNum = 0;

	// Reset Channel Attribute variables
	g_currentAttributeCount = 0;
	g_bPrevAttrDisplayed = false;
	g_totalAttributes = 0;
	g_bNoAttribute = true;

	// Reset FG Freqs variables
	g_edgeFreqDefined = 0;
	changeF1 = false;
	changeF2 = false;

	// Reset std::string Response buffer & Signal to Write on Serial variable
	out.clear();
	b_SendString = false;
	ePrevErrorType = NO_ERROR;

	SetPatternTransferFlag(false);


}

int CmdDecoder::SearchVerb(std::string & verb)
{
	// Search verb and Set if correct verb is found

	std::transform(verb.begin(), verb.end(), verb.begin(), ::toupper);	// Convert to all UPPERCASE

	int chrlen = verb.length();

	if ((chrlen == 3) && ((verb[0] == 'S') || (verb[0] == 'G') || (verb[0] == 'A')))	//Pre-selecting S,G,A, if we don't chrlen=3 can be true for object as well, ch.1 or module.1, we need to avoid that.	// SET, ADD, GET
	{
		if (verb == "SET")
			eVerb = SET;
		else if (verb == "GET")
			eVerb = GET;
		else if (verb == "ADD")
			eVerb = ADD;
		else
		{
			cout << "ERROR: The command verb is wrong" << endl;
			return (VERB_WRONG);
		}
	}
	else if ((chrlen > 3) && ((verb[0] == 'D') || (verb[0] == 'A')))	//Pre-selecting D,A, if we don't chrlen>3 can be true for object as well, ch.1.2 or module.1 ETC,											// DELETE, ACTION
	{
		if (verb == "DELETE")
			eVerb = DELETE;
		else if (verb == "ACTION")
			eVerb = ACTION;
		else
		{
			cout << "ERROR: The command verb is wrong" << endl;
			return (VERB_WRONG);
		}
	}
	else	// Assume Verb doesn't exist, so we check for Object, i.e. ch.1.1 etc. if found means verb not found
	{
		cout << "ERROR: The command verb is wrong" << endl;
		return (VERB_WRONG);
	}

	return (VERB_FOUND);
}

int CmdDecoder::SearchObject(std::string &object)
{
	// Ch/Module/Restart/Fwupgrade/Panel/Idn/Calfile etc.

	std::transform(object.begin(), object.end(), object.begin(), ::toupper);	// Convert to all UPPERCASE

	objVec = SplitCmd(object, ".");			// Globally defined so that attributes can access the vector and know which channel,module or slots etc are under consideration

	if (objVec[0] == "-1")
	{
		cout << "ERROR: Invalid Object Format" << endl;
		return (-1);
	}
	else
	{
		// String is corrected formated and as numbers and . dots available

		int objVecLen = objVec[0].length();	// Length of the first element in Object i.e. ch., module, heatermonitor, etc.

		switch (eVerb)
		{
			// Move to the verb user set
			case ACTION:
			{
				switch(objVecLen)	// if object vector length is 2,6,7,...
				{
					case 7:
					{
						if (objVec[0] == "RESTART")
						{
							// DO SOMETHING
							if(g_bNoAttribute)	// ACTION:RESTART.1 Doesn't have any attribute
							{
								// RESTART APPLCIATION "LOGIC HERE"
								if (Sscanf<int>(objVec[1], g_moduleNum, 'i'))
								{
									if (g_moduleNum == 1)
									{
										eObject = RESTART;
										b_RestartNeeded = true;				// Get the module number use in attribute functions

//										sync();
//										reboot(RB_AUTOBOOT);
									}
									else
									{
										cout << "ERROR: The Module Number is wrong" << endl;
										return (-1);
									}
								}
								else
								{
									cout << "ERROR: The Module Number is not a numerical value" << endl;
									return (-1);
								}

							}
							else
							{
								cout << "ERROR: The command format is wrong" << endl;
								return (-1);
							}
						}
						else
						{
							cout << "ERROR: The command Object is wrong" << endl;
							return (-1);
						}

						break;
					}
					case 9:
					{
						if (objVec[0] == "FWUPGRADE")
						{
							// DO SOMETHING
							if (Sscanf(objVec[1], g_moduleNum, 'i'))
							{
								if (g_moduleNum == 1)
								{
									eObject = FWUPGRADE;
								}
								else
								{
									cout << "ERROR: The Module Number is wrong" << endl;
									return (-1);
								}
							}
							else
							{
								cout << "ERROR: The Module Number is not a numerical value" << endl;
								return (-1);
							}
						}
						else
						{
							cout << "ERROR: The command Object is wrong" << endl;
							return (-1);
						}
						break;
					}
					case 6:
					{
						if (objVec[0] == "MODULE")   //to add store and restore
						{
							// DO SOMETHING
							eObject = MODULE;

							if (objVec.size() == 2)		//MODULE.1 or MODULE.2 then its correct
							{
								if (Sscanf(objVec[1], g_moduleNum, 'i'))
								{
									if (g_moduleNum == 1 || g_moduleNum == 2)
									{
										// OK
										if(g_moduleNum == 2)
										{
#ifndef _TWIN_WSS_
											cout << "ERROR: The Module 2 Doesn't Exist!" << endl;
												return (-1);
#endif
										}
									}
									else
									{
										cout << "ERROR: The Module Number is wrong" << endl;
										return (-1);
									}
								}
								else
								{
									cout << "ERROR: The Module Number is not a numerical value" << endl;
									return (-1);
								}
							}
							else
							{
								cout << "ERROR: The Module format is invalid" << endl;
								return (-1);
							}
						}
						else
						{
							cout << "ERROR: The command Object is wrong" << endl;
							return (-1);
						}

						break;
					}
					default:
					{
						cout << "ERROR: The command Object is wrong" << endl;
						return (-1);
					}

					}
					break;	// main ACTION switch
				}

			case SET:
				{
					switch(objVecLen)
					{
					case 2:
					{
						if (objVec[0] == "CH")
						{
							if ((objVec.size() == 3))	// CH.M.N only 3 available in TF mode and Fixed Grid mode
							{
								if ((objVec[1] == "1" && eModule1 == TF)	|| (objVec[1] == "2" && eModule2 == TF))	// Read Module Number &Module Type/Slotsize
								{
									if (is_SetTFDone() == -1)	// Check if Channels are active and channel numbers are under 96. If not then error
										return (-1);
								}
								else if ((objVec[1] == "1" && eModule1 == FIXEDGRID) || (objVec[1] == "2" && eModule2 == FIXEDGRID))
								{
									if (is_SetNoSlotFGDone() == -1)	// Check if Channels are active,  channel numbers are under 96. If not then error
										return (-1);
								}
								else
								{
									cout << "ERROR: The command Object is wrong - Please check Module slotsize or command format" << endl;
									return (-1);
								}
							}
							else if ((objVec.size() == 4))		// CH.M.N.S  4 size only in Fixed Grid mode
							{
								if ((objVec[1] == "1" && eModule1 == FIXEDGRID) || (objVec[1] == "2" && eModule2 == FIXEDGRID))	// Read Module Number.
								{
									if (is_SetSlotFGDone() == -1)	// Check if Channels are active, Slot size is defined (and correct) and channel numbers are under 96. If not then error
										return (-1);
								}
								else
								{
									cout << "ERROR: The command Object is wrong - Please check Module Slotsize or command format" << endl;
									return (-1);
								}
							}
							else
							{
								cout << "ERROR: The command Object is wrong - Please check Module slotsize or command format" << endl;
								return (-1);
							}
						}
						else
						{
							cout << "ERROR: The command Object is wrong" << endl;
							return (-1);
						}
						break;
					}
					case 6:
					{
						if (objVec[0] == "MODULE") //drc to check
						{
							eObject = MODULE;
							if ((objVec.size() == 2))
							{
								//if (Sscanf(objVec[1], g_moduleNum, 'i'))
								if(objVec[1] == "1")
								{
									g_moduleNum = 1;//if (g_moduleNum == 1 || g_moduleNum == 2)
									/*{
										eObject = MODULE;
									}*/
								}
								else if(objVec[1] == "2")
								{
									g_moduleNum = 2;

								}
								else
								{
									cout << "ERROR: The Module Number is wrong" << endl;
									return (-1);
								}
							}
								/*else
								{
									cout << "ERROR: The Module Number is not a numerical value" << endl;
									return (-1);
								}*/
							/*}
							else
							{
								cout << "ERROR: The command Object is wrong" << endl;
								return (-1);
							}*/
						}
						else
						{
							cout << "ERROR: The command Object is wrong" << endl;
							return (-1);
						}
						break;
					}
					case 3:
					{
						if (objVec[0] == "IDN")
						{
							// DO SOMETHING
							if (Sscanf(objVec[1], g_moduleNum, 'i'))
							{
								if (g_moduleNum == 1)
								{
									eObject = IDN;
								}
								else
								{
									cout << "ERROR: The Module Number is wrong" << endl;
									return (-1);
								}
							}
							else
							{
								cout << "ERROR: The Module Number is not a numerical value" << endl;
								return (-1);
							}
						}
						else
						{
							cout << "ERROR: The command Object is wrong" << endl;
							return (-1);
						}
						break;
					}
					case 5:
					{
						if (objVec[0] == "PANEL")
						{
							// DO SOMETHING
							if (Sscanf(objVec[1], g_moduleNum, 'i'))
							{
								if (g_moduleNum == 1)
								{
									eObject = PANEL;
								}
								else if (g_moduleNum == 2 )
								{
									eObject = MODULE;  //drc added for twin wss
								}
								else
								{
									cout << "ERROR: The Module Number is wrong" << endl;
									return (-1);
								}
							}
							else
							{
								cout << "ERROR: The Module Number is not a numerical value" << endl;
								return (-1);
							}
						}
						else
						{
							cout << "ERROR: The command Object is wrong" << endl;
							return (-1);
						}
						break;
					}
					default:
					{
						cout << "ERROR: The command Object is wrong" << endl;
						return (-1);
					}

					}
					break;
				}

			case GET:	// Different scenarios. 1) GET:CH.1.1   2) GET:CH.1.*  3) GET:CH.1.1.1   4) GET:CH.1.1.*	 5) GET:CH.1.*.* (TF and FIXED GRID BOTH)
				{
					//cout << "Search Object GET" << endl;
					switch(objVecLen)
					{
					case 2:
					{
						if (objVec[0] == "CH")  //ch.*
						{
							if(objVec.size() == 2)
							{
								if ((objVec[1] == "1" && eModule1 == TF) || (objVec[1] == "2" && eModule2 == TF))	// Read Module Number &Module Type/Slotsize
								{
									if (Sscanf(objVec[1], g_moduleNum,'i')){

										eObject = CH_TF;
										if (g_bNoAttribute == true)
										{
											eGet =  ALL_CH_ALL_ATTR;	//include all channel info
											std::string chr_ask = "*";
											Print_SearchAttributes(chr_ask);
										}
										else
										{
											eGet = SOME_ATTR;
											std::string chr_ask = "*";
											Print_SearchAttributes(chr_ask);
										}
									}
									else
									{
										cout << "ERROR: Module Number is wrong" << endl;
										return (-1);
									}

								}
								else if ((objVec[1] == "1" && eModule1 == FIXEDGRID) || (objVec[1] == "2" && eModule2 == FIXEDGRID))
								{
									if (Sscanf(objVec[1], g_moduleNum,'i')){

										eObject = CH_FG;
										if (g_bNoAttribute == true)
										{
											eGet =  ALL_CH_ALL_ATTR;	//include all channel info
											std::string chr_ask = "*";
											Print_SearchAttributes(chr_ask);
										}
										else
										{
											eGet = SOME_ATTR;
											std::string chr_ask = "*";
											Print_SearchAttributes(chr_ask);
										}
									}
									else
									{
										cout << "ERROR: Module Number is wrong" << endl;
										return (-1);
									}
								}
								else if (objVec[1] == "*" && eModule1 == TF)	// Read Module Number
								{
									eObject = CH_TF;
									eGet = ALL_CH_ALL_ATTR;	//include all channel info
									std::string chr_null = " ";
									g_moduleNum = 1;
									Print_SearchAttributes(chr_null);
#ifdef _TWIN_WSS_
									if(eModule2 == TF)
									{
										eObject = CH_TF;
										eGet = ALL_CH_ALL_ATTR;	//include all channel info
										std::string chr_null = " ";
										g_moduleNum = 2;
										Print_SearchAttributes(chr_null);
									}
									else
									{
										eObject = CH_FG;
										eGet = ALL_CH_ALL_ATTR;	//include all channel info
										std::string chr_null = " ";
										g_moduleNum = 2;
										Print_SearchAttributes(chr_null);

									}
#endif
								}

								else if (objVec[1] == "*" && eModule1 == FIXEDGRID)
								{
									eObject = CH_FG;
									eGet = ALL_CH_ALL_ATTR;	//include all channel info
									std::string chr_null = " ";
									g_moduleNum = 1;
									Print_SearchAttributes(chr_null);
#ifdef _TWIN_WSS_
									if (objVec[1] == "*" &&  eModule2 == FIXEDGRID)
									{
										eObject = CH_FG;
										eGet = ALL_CH_ALL_ATTR;	//include all channel info
										std::string chr_null = " ";

										g_moduleNum = 2;
										Print_SearchAttributes(chr_null);
									}
									else
									{
										eObject = CH_TF;
										eGet = ALL_CH_ALL_ATTR;	//include all channel info
										std::string chr_null = " ";
										g_moduleNum = 2;
										Print_SearchAttributes(chr_null);
									}
#endif
								}
								else
								{
									cout << "ERROR: The command Object is wrong - Please check command format" << endl;
									return (-1);
								}
							}
							else if ((objVec.size() == 3))						// CH.M.N only 3 available in TF mode and Fixed Grid mode
							{
								if ((objVec[1] == "1" && eModule1 == TF) || (objVec[1] == "2" && eModule2 == TF))	// Read Module Number &Module Type/Slotsize
								{
									if (is_GetTFDone() == -1)	// Check if all channels need to send to user or just one channel all attributes
										return (-1);
								}
								else if ((objVec[1] == "1" && eModule1 == FIXEDGRID) || (objVec[1] == "2" && eModule2 == FIXEDGRID))
								{
									if (is_GetNoSlotFGDone() == -1)	// Check if Channels are active,  channel numbers are under 96. If not then error
										return (-1);
								}
								else
								{
									cout << "ERROR: The command Object is wrong - Please check Module slotsize or command format" << endl;
									return (-1);
								}
							}
							else if ((objVec.size() == 4))					// CH.M.N.S  4 size only in Fixed Grid mode
							{
								if ((objVec[1] == "1" && eModule1 == FIXEDGRID) || (objVec[1] == "2" && eModule2 == FIXEDGRID))	// Read Module Number.
								{
									if (is_GetSlotFGDone() == -1)	// Check if Channels are active, Slot size is defined (and correct) and channel numbers are under 96. If not then error
										return (-1);
								}
								else
								{
									cout << "ERROR: The command Object is wrong - Please check Module Slotsize or command format" << endl;
									return (-1);
								}
							}
							else
							{
								cout << "ERROR: The command Object is wrong - Please check Module slotsize or command format" << endl;
								return (-1);
							}
						}
						else
						{
							cout << "ERROR: The command Object is wrong" << endl;
							return (-1);
						}
						break;
					}
					case 6:
					{
						if (objVec[0] == "MODULE")
						{
							if((objVec.size() == 2))	// module.1 or module.2
							{
								if (objVec[1] == "1" || objVec[1] == "2")	// Read Module Number.
								{
									if (Sscanf(objVec[1], g_moduleNum,'i'))
									{
										eObject = MODULE;
										eGet = SOME_ATTR;		// default

										if (g_bNoAttribute == true)
										{
											// if no attribute is given
											eGet = ALL_ATTR_OF_CH;
											std::string chr_ask = "*";
											Print_SearchAttributes(chr_ask);	//send signal that all channel values are needed
										}
									}
									else
									{
										cout << "ERROR: Module Number is wrong" << endl;
										return (-1);
									}
								}
								else if(objVec[1] == "*" )
								{
									eObject = MODULE;
									eGet = SOME_ATTR;		// default

									if (g_bNoAttribute == true)
									{
										// if no attribute is given
										eGet = ALL_CH_ALL_ATTR;
										std::string chr_ask = "*";
										Print_SearchAttributes(chr_ask);	//send signal that all channel values are needed
									}
								}
								else
								{
									cout << "ERROR: Module Number is wrong" << endl;
									return (-1);
								}
							}
							else
							{
								cout << "ERROR: Module Object Format is wrong" << endl;
								return (-1);
							}
						}
						else
						{
							cout << "ERROR: The command Object is wrong" << endl;
							return (-1);
						}
						break;
					}
					case 3:
					{
						if (objVec[0] == "IDN")
						{
							if((objVec.size() == 2))	// heatermonitor.1 or heatermonitor.2
							{
								if (objVec[1] == "1")	// Read Module Number must be 1 only
								{
									if (Sscanf(objVec[1], g_moduleNum, 'i'))
									{
										eObject = IDN;
										eGet = SOME_ATTR;		// default

										if (g_bNoAttribute == true)
										{
											// if no attribute is given
											eGet = ALL_ATTR_OF_CH;
											std::string chr_ask = "*";
											Print_SearchAttributes(chr_ask);	//send signal that all channel values are needed
										}
									}
									else
									{
										cout << "ERROR: IDN Number is wrong" << endl;
										return (-1);
									}
								}
								else
								{
									cout << "ERROR: Module Number is wrong" << endl;
									return (-1);
								}
							}
							else
							{
								cout << "ERROR: Object Format is Wrong" << endl;
								return (-1);
							}
						}
						else
						{
							cout << "ERROR: The command Object is wrong" << endl;
							return (-1);
						}
						break;
					}
					case 4:
					{
						if (objVec[0] == "TEMP")
						{
							if((objVec.size() == 1))	// heatermonitor.1 or heatermonitor.2
							{
								eObject = TEMP;
								eGet = SOME_ATTR;		// default

								if (g_bNoAttribute == true)
								{
									// if no attribute is given
									eGet = ALL_ATTR_OF_CH;
									std::string chr_ask = "*";
									Print_SearchAttributes(chr_ask);	//send signal that all channel values are needed
								}
								else
								{
									cout << "ERROR:  is wrong" << endl;
									return (-1);
								}
							}
							else
							{
								cout << "ERROR: Object Format is Wrong" << endl;
								return (-1);
							}
						}
						else
						{
							cout << "ERROR: The command Object is wrong" << endl;
							return (-1);
						}
						break;
					}
					case 5:
					{
						if (objVec[0] == "PANEL")
						{
							if((objVec.size() == 2))	// PANEL.1
							{
								if (objVec[1] == "1")	// Read Module Number must be 1 only
								{
									if (Sscanf(objVec[1], g_moduleNum, 'i'))
									{
										eObject = PANEL;
										eGet = SOME_ATTR;		// default

										if (g_bNoAttribute == true)
										{
											// if no attribute is given
											eGet = ALL_ATTR_OF_CH;
											std::string chr_ask = "*";
											Print_SearchAttributes(chr_ask);	//send signal that all channel values are needed
										}
									}
									else
									{
										cout << "ERROR: PANEL Number is wrong" << endl;
										return (-1);
									}
								}
								else
								{
									cout << "ERROR: PANEL Number is wrong" << endl;
									return (-1);
								}
							}
							else
							{
								cout << "ERROR: Object Format is Wrong" << endl;
								return (-1);
							}
						}
						else if (objVec[0] == "FAULT")
						{
							if((objVec.size() == 2))	// FAULT.N
							{
								if (Sscanf(objVec[1], g_faultNum, 'i'))		// Here g_moduleNum = fault number N.
								{
									//if(search Fault# g_moduleNum exist in database?)
									//{
										eObject = FAULT;
										eGet = SOME_ATTR;		// default
                                        std::cout << "FAULT param2 was : " << g_faultNum <<endl;
										if (g_bNoAttribute == true)
										{
											// if no attribute is given
											eGet = ALL_ATTR_OF_CH;
											std::string chr_ask = "*";
											Print_SearchAttributes(chr_ask);	//send signal that all channel values are needed
										}
									//}
//										else
//										{
//											cout << "ERROR: FAULT Number doesn't exists" << endl;
//											return (-1);
//										}

								}
								else
								{
									cout << "ERROR: FAULT Number is wrong" << endl;
									return (-1);
								}
							}
							else
							{
								cout << "ERROR: Object Format is Wrong" << endl;
								return (-1);
							}
						}
						else
						{
							cout << "ERROR: The command Object is wrong" << endl;
							return (-1);
						}
						break;
					}
					case 13:
					{
						if (objVec[0] == "HEATERMONITOR")
						{
							if((objVec.size() == 2))	// heatermonitor.1 or heatermonitor.2
							{
								if (objVec[1] == "1" || objVec[1] == "2")	// Read Module Number.
								{
									if (Sscanf(objVec[1], g_heaterNum,'i'))
									{
										eObject = HEATERMONITOR;
										eGet = SOME_ATTR;		// default

										if (g_bNoAttribute == true)
										{
											// if no attribute is given
											eGet = ALL_ATTR_OF_CH;
											std::string chr_ask = "*";
											Print_SearchAttributes(chr_ask);	//send signal that all channel values are needed
										}
									}
									else
									{
										cout << "ERROR: Module Number is wrong" << endl;
										return (-1);
									}
								}
								else
								{
									cout << "ERROR: Module Number is wrong" << endl;
									return (-1);
								}
							}
							else
							{
								cout << "ERROR: Object Format is Wrong" << endl;
								return (-1);
							}

						}
						else
						{
							cout << "ERROR: The command Object is wrong" << endl;
							return (-1);
						}
						break;
					}
					case 10:
					{
						if (objVec[0] == "TECMONITOR")
						{
							if((objVec.size() == 2))	// heatermonitor.1 or heatermonitor.2
							{
								if (objVec[1] == "1")	// Read Module Number must be 1 only
								{
									if (Sscanf(objVec[1], g_moduleNum, 'i'))
									{
										eObject = TECMONITOR;
										eGet = SOME_ATTR;		// default

										if (g_bNoAttribute == true)
										{
											// if no attribute is given
											eGet = ALL_ATTR_OF_CH;
											std::string chr_ask = "*";
											Print_SearchAttributes(chr_ask);	//send signal that all channel values are needed
										}
									}
									else
									{
										cout << "ERROR: TEC Number is wrong" << endl;
										return (-1);
									}
								}
								else
								{
									cout << "ERROR: Module Number is wrong" << endl;
									return (-1);
								}
							}
							else
							{
								cout << "ERROR: Object Format is Wrong" << endl;
								return (-1);
							}
						}
						else
						{
							cout << "ERROR: The command Object is wrong" << endl;
							return (-1);
						}
						break;
					}
					case 7:
					{
						if (objVec[0] == "CALFILE")
						{
							if((objVec.size() == 2))	// FAULT.N
							{
								if (Sscanf(objVec[1], g_moduleNum, 'i'))		// Here g_moduleNum = CALFILE number N.
								{
									//if(search CALFILE# g_moduleNum exist in database?)
									//{
										eObject = CALFILE;
										eGet = SOME_ATTR;		// default

										if (g_bNoAttribute == true)
										{
											// if no attribute is given
											eGet = ALL_ATTR_OF_CH;
											std::string chr_ask = "*";
											Print_SearchAttributes(chr_ask);	//send signal that all channel values are needed
										}
									//}
//										else
//										{
//											cout << "ERROR: CALFILE Number doesn't exists" << endl;
//											return (-1);
//										}

								}
								else
								{
									cout << "ERROR: CALFILE Number is wrong" << endl;
									return (-1);
								}
							}
							else
							{
								cout << "ERROR: Object Format is Wrong" << endl;
								return (-1);
							}
						}
						else
						{
							cout << "ERROR: The command Object is wrong" << endl;
							return (-1);
						}
						break;
					}
					case 9:
					{
						if (objVec[0] == "FWUPGRADE")
						{
							if((objVec.size() == 2))	// FWUPGRADE.1
							{
								if (objVec[1] == "1")	// Read Module Number must be 1 only
								{
									if (Sscanf(objVec[1], g_moduleNum, 'i'))
									{
										eObject = FWUPGRADE;
										eGet = SOME_ATTR;		// default

										if (g_bNoAttribute == true)
										{
											// if no attribute is given
											eGet = ALL_ATTR_OF_CH;
											std::string chr_ask = "*";
											Print_SearchAttributes(chr_ask);	//send signal that all channel values are needed
										}
									}
									else
									{
										cout << "ERROR: FWUPGRADE Number is wrong" << endl;
										return (-1);
									}
								}
								else
								{
									cout << "ERROR: FWUPGRADE Number is wrong" << endl;
									return (-1);
								}
							}
							else
							{
								cout << "ERROR: Object Format is Wrong" << endl;
								return (-1);
							}
						}
						else
						{
							cout << "ERROR: The command Object is wrong" << endl;
							return (-1);
						}
						break;
					}
					default:
					{
						cout << "ERROR: The command Object is wrong" << endl;
						return (-1);
					}
					}

					break;
				}

			case ADD:	// Read if channel numbers and command format is correct for the Module slotsize used i.e TF or Fixed Grid. Further channel will be active after receiving correct Attributes
				{
					switch(objVecLen)
					{
					case 2:
					{
						if (objVec[0] == "CH")
						{
							if ((objVec.size() == 3))
							{
								// 3 because in both TF and fixed grid slot is not used so format is always CH.M.N
								if ((objVec[1] == "1" && eModule1 == TF) || (objVec[1] == "2" && eModule2 == TF))	// Read Module Number
								{
									if (is_AddTFDone() == -1)	// Check if Channel numbers, module number is correct and then set the FLAG for OBJECT
										return (-1);
								}
								else if ((objVec[1] == "1" && eModule1 == FIXEDGRID) || (objVec[1] == "2" && eModule2 == FIXEDGRID))
								{
									if (is_AddFGDone() == -1)	// Check if Channel numbers, module number is correct and then set the FLAG for OBJECT
										return (-1);
								}
								else
								{
									cout << "ERROR: The command Object is wrong - Please check Module slotsize or command format" << endl;
									return (-1);
								}
							}
							else
							{
								cout << "ERROR: The command Object is wrong" << endl;
								return (-1);
							}
						}
						else
						{
							cout << "ERROR: The command Object is wrong" << endl;
							return (-1);
						}
						break;
					}
					default:
					{
						cout << "ERROR: The command Object is wrong" << endl;
						return (-1);
					}
					}

					break;
				}

			case DELETE:
				{
					switch(objVecLen)
					{
					case 2:
					{
						if (objVec[0] == "CH")
						{
							if ((objVec[1] == "1" && eModule1 == TF) || (objVec[1] == "2" && eModule2 == TF))
							{
								// Read Module Number
								if (is_DeleteTFDone(objVec[1],objVec[2]) == -1)	// Check if Channel numbers, module number is correct and then set channel active to false
									return (-1);
							}
							else if ((objVec[1] == "1" && eModule1 == FIXEDGRID) || (objVec[1] == "2" && eModule2 == FIXEDGRID))
							{
								if (is_DeleteFGDone(objVec[1],objVec[2]) == -1)	// Check if Channel numbers, module number is correct and then set channel active to false
									return (-1);
							}
							else if(objVec[1] == "*" ) //drc added to delete all channels on both modules as required by running test at BAIAN
							{
								objVec.insert(objVec.begin() + 1, "1");
								objVec.insert(objVec.begin() + 2, "*");
								if(eModule1 == TF)
								{
									if (is_DeleteTFDone(objVec[1],objVec[2]) == -1)	// Check if Channel numbers, module number is correct and then set channel active to false
										return (-1);
								}
								else
								{
									if(is_DeleteFGDone(objVec[1],objVec[2]) == -1)
										return(-1);
								}
#ifdef _TWIN_WSS_				//module 2
								objVec.insert(objVec.begin() + 1, "2");
								objVec.insert(objVec.begin() + 2, "*");
								if(eModule2 == TF )
								{
									if (is_DeleteTFDone(objVec[1],objVec[2]) == -1)	// Check if Channel numbers, module number is correct and then set channel active to false
										return (-1);
								}
								else // eModule2 == FIXEDGRID
								{
									if (is_DeleteFGDone(objVec[1],objVec[2]) == -1)	// Check if Channel numbers, module number is correct and then set channel active to false
										return (-1);
								}
#endif
							}
							else
							{
								cout << "ERROR: The command Object is wrong - Please check Module slotsize or command format" << endl;
								return (-1);
							}
						}
						else
						{
							cout << "ERROR: The command Object is wrong" << endl;
							return (-1);
						}
						break;
					}
					default:
					{
						cout << "ERROR: The command Object is wrong" << endl;
						return (-1);
					}
					}

					break;
				}

			default:
			{
				cout << "ERROR: The command Verb is wrong" << endl;
				return (-1);
			}
		}
	}

	return (0);
}

int CmdDecoder::SearchAttribute(std::string &attribute)
{
	if ((eVerb == GET))	// When user want to get something, we call function below, to retrieve values and send to user through a string
	{
		if(eGet == SOME_ATTR)	// if attribute exists and no *aasterisk mentioned
		{
			if (Print_SearchAttributes(attribute) == -1)
			{
				return (-1);
			}	// Break if error in command. SEND ATTRIBUTE TO READ/DISPLAY
		}
		else if (eGet != SOME_ATTR)	// This only happened if user mentioned *asterisk in command get:ch.1.*:adp
		{
			return (0);	// success already happened in searchObject.
		}

		eGet = SOME_ATTR;	// Reset the Flag once used
	}	// DELETE doesn't need attributes, nor they are provided by user
	else if ((eVerb == SET) || (eVerb == ADD) || (eVerb == ACTION))	// When we verbs are SET,ADD,ACTION, we call the function below to modify values
	{
		//Set All Attributes
		if (Set_SearchAttributes(attribute) == -1)
		{
			//std::cout << "ERROR: Set_SearchAttribute Failed- Have attribute" << std::endl;
			return (-1);
		}	// Break if error in command
	}
	else
	{
		std::cout << "ERROR: Command format is Invalid- LoadSingleCmd" << std::endl;
		return (-1);
	}

	return (0);		// Success
}


int CmdDecoder::Set_SearchAttributes(std::string &attributes)
{
	//cout << "attributes  " << attributes << endl;
	//Passing one attribute at a time
	// Search the eObject by using switch cases and then put if else statement on attribute, if attribute doesn't link to eObject used then we cause an error
	//int attrLen;
	int iValue;	// Integer converted value from user
	double fValue = 0.0;	// Float converted value from user
	std::string str_Value;

	std::transform(attributes.begin(), attributes.end(), attributes.begin(), ::toupper);

	vector<string> attr;

	/****************************************/
	/*	   IF NOT ACTION THEN SPLIT AT '='  */
	/****************************************/

	if (eVerb != ACTION)	// Action doesnt require '=' or value for attribute so we don't SplitCmd, same for fwupdate and restart, edit thissss
	{
		attr = SplitCmd(attributes, "=");

		if (attr[0] == "-1" || attr.size() > 2)
		{
			cout << "ERROR: Invalid Attribute Format"<< endl;
			return (-1);
		}

		if (attr.size() == 2 && attr[1] == "")				// When attribute value is not given i.e. ADD:CH.1.1:ADP=
		{
			PrintResponse("\01MISSING_ATTRIBUTE_DATA\04", ERROR_HI_PRIORITY);
			return (-1);
		}
	}
	else
	{
		attr.push_back(attributes);	// Move the attribute to 0th index of vector i.e STORE or RESTORE to attr[0]
		attr.push_back("0");	// EXTENDING VECTOR for safety and avoid segmentation error
	}


	switch (eObject)
	{
		// Switch to Object user wrote

		case CH_TF:	// If user command is for True Flex module channels
			{
				switch (attr[0][0])		// first character of string, i.e string= ADP = attr[0][0] = 'A'	switch only work on char
				{
				case 'A':
				{
					if (attr[0] == "ADP")
					{
						if (Sscanf(attr[1], iValue, 'i'))
						{
							if(iValue >=1 && iValue <=VENDOR_MAX_PORT)	//
							{
								TF_Channel_DS[g_moduleNum][g_channelNum].ADP = iValue;	// g_moduleNum and g_channelNums were found in SearchObject
							}
							else
							{
								PrintResponse("\01INVALID_ATTRIBUTE_DATA\04", ERROR_HI_PRIORITY);
								return (-1);
							}
						}
						else
						{
							cout << "ERROR: The command attribute ADP is not integer" << endl;
							PrintResponse("\01INVALID_ATTRIBUTE_DATA\04", ERROR_HI_PRIORITY);
							return (-1);
						}
					}
					else if (attr[0] == "ATT")
					{
						if(Sscanf(attr[1], fValue, 'f'))
						{
							if(fValue >= 0 && fValue < 35.01)		// Can't be negative  //drc modified to 35
							{
								// Get the float value of ATT
								TF_Channel_DS[g_moduleNum][g_channelNum].ATT = fValue;
							}
							else
							{
								cout << "ERROR: The attenuation is -ve" << endl;
								PrintResponse("\01INVALID_ATTRIBUTE_DATA\04", ERROR_HI_PRIORITY);
								return (-1);
							}

						}
						else
						{
							cout << "ERROR: The command attribute ATT is not numerical" << endl;
							PrintResponse("\01INVALID_ATTRIBUTE_DATA\04", ERROR_HI_PRIORITY);
							return (-1);
						}
					}
#ifdef _DEVELOPMENT_MODE_
					else if (attr[0] == "A_OP")
					{
						if (Sscanf(attr[1], fValue, 'f'))
						{
							TF_Channel_DS[g_moduleNum][g_channelNum].A_OPP = fValue;
						}
						else
						{
							cout << "ERROR: The command attribute a_op is not numerical" << endl;
							PrintResponse("\01INVALID_ATTRIBUTE_DATA\04", ERROR_HI_PRIORITY);
							return (-1);
						}
					}
					else if (attr[0] == "A_ATT")
					{
						if (Sscanf(attr[1], fValue,'f'))
						{
							// Get the float value of ATT
							TF_Channel_DS[g_moduleNum][g_channelNum].A_ATT = fValue;
						}
						else
						{
							cout << "ERROR: The command attribute ATT is not numerical" << endl;
							PrintResponse("\01INVALID_ATTRIBUTE_DATA\04", ERROR_HI_PRIORITY);
							return (-1);
						}
					}
#endif
					else
					{
						cout << "ERROR: The command attribute is wrong" << endl;
						PrintResponse("\01INVALID_ATTRIBUTE\04", ERROR_HI_PRIORITY);
						return (-1);
					}

					break;
				}
				case 'C':
				{
					if (attr[0] == "CMP")
					{
						if (Sscanf(attr[1], iValue, 'i'))
						{
							if(iValue == 1 || iValue == 2)
							{
								TF_Channel_DS[g_moduleNum][g_channelNum].CMP = iValue;
							}
							else
							{
								cout << "ERROR: Invalid CMP value" << endl;
								PrintResponse("\01INVALID_ATTRIBUTE_DATA\04", ERROR_HI_PRIORITY);
								return (-1);
							}

						}
						else
						{
							cout << "ERROR: The command attribute CMP is not numerical" << endl;
							PrintResponse("\01INVALID_ATTRIBUTE_DATA\04", ERROR_HI_PRIORITY);
							return (-1);
						}
					}
#ifdef _DEVELOPMENT_MODE_
					else if (attr[0] == "COLOR")
					{
						if (Sscanf(attr[1], iValue ,'i'))
						{
							// Get the float value of ATT
							if (iValue >= 0 && iValue <= 255)
							{
								TF_Channel_DS[g_moduleNum][g_channelNum].b_ColorSet = true;
								TF_Channel_DS[g_moduleNum][g_channelNum].COLOR = iValue;
							}
							else
							{
								cout << "ERROR: The colour range is incorrect (0-255 limit)" << endl;
								return (-1);
							}
						}
						else
						{
							cout << "ERROR: The command attribute ATT is not numerical" << endl;
							PrintResponse("\01INVALID_ATTRIBUTE_DATA\04", ERROR_HI_PRIORITY);
							return (-1);
						}
					}
#endif
					else
					{
						cout << "ERROR: The command attribute is wrong" << endl;
						PrintResponse("\01INVALID_ATTRIBUTE\04", ERROR_HI_PRIORITY);
						return (-1);
					}

					break;
				}
				case 'F':
				{
					if (attr[0] == "FC")
					{
						// Fc overlap test will be done later once all channels Fc is received
						if (Sscanf(attr[1], fValue,'f'))
						{
							// Get the float value of FC

							if (fValue > VENDOR_FREQ_RANGE_LOW && fValue < VENDOR_FREQ_RANGE_HIGH)
							{
								/*if(fmod(fValue,0.5) != 0)
								{
									cout << "ERROR: The FC is not multiples of 0.5 GHz" << endl;
									return (-1);
								}*/
								if(TF_Channel_DS[g_moduleNum][g_channelNum].BW != 0)
								{

									if((TF_Channel_DS[g_moduleNum][g_channelNum].BW/2 + fValue) > VENDOR_FREQ_RANGE_HIGH ||
											(fValue - TF_Channel_DS[g_moduleNum][g_channelNum].BW/2) < VENDOR_FREQ_RANGE_LOW)
									{
										cout << "ERROR: The channel is out of range" << endl;
										return (-1);
									}
									TF_Channel_DS[g_moduleNum][g_channelNum].FC = fValue;
									TF_Channel_DS[g_moduleNum][g_channelNum].F1 = TF_Channel_DS[g_moduleNum][g_channelNum].FC - TF_Channel_DS[g_moduleNum][g_channelNum].BW/2;
									TF_Channel_DS[g_moduleNum][g_channelNum].F2 = TF_Channel_DS[g_moduleNum][g_channelNum].FC + TF_Channel_DS[g_moduleNum][g_channelNum].BW/2;

								}
								else
								{//need to find a way to check bw is right configured
									// Make sure FC user gave is within the range of VENDOR_FREQ_RANGE_HIGH-VENDOR_FREQ_RANGE_LOW
									TF_Channel_DS[g_moduleNum][g_channelNum].FC = fValue;
								}

							}
							else
							{
								cout << "ERROR: The Fc is out of range" << endl;
								return (-1);
							}
						}
						else
						{
							cout << "ERROR: The command attribute FC is not numerical" << endl;
							PrintResponse("\01INVALID_ATTRIBUTE_DATA\04", ERROR_HI_PRIORITY);
							return (-1);
						}
					}
					else if(attr[0] == "F1")
					{
						if (Sscanf(attr[1], fValue,'f'))
						{
							// Get the float value of F1

							if (fValue > VENDOR_FREQ_RANGE_LOW && fValue < VENDOR_FREQ_RANGE_HIGH)
							{
								if(TF_Channel_DS[g_moduleNum][g_channelNum].F2 != 0)
								{
									if(fValue > TF_Channel_DS[g_moduleNum][g_channelNum].F2)
									{
										cout << "ERROR: The f1 is crossing of f2" << endl;
										return (-1);
									}
#ifndef _DEVELOPMENT_MODE_
									if(TF_Channel_DS[g_moduleNum][g_channelNum].F2 - fValue > VENDOR_BW_RANGE_HIGH)
#else
									if(TF_Channel_DS[g_moduleNum][g_channelNum].F2 - fValue > VENDOR_FREQ_RANGE_HIGH - VENDOR_FREQ_RANGE_LOW)
#endif
									{
										cout << "ERROR: The channel bandwidth is out of range" << endl;
										return (-1);
									}
									/*if(fmod((TF_Channel_DS[g_moduleNum][g_channelNum].F2 - fValue),0.5) != 0)
									{
										cout << "ERROR: The bandwidth is not multiples of 0.5 GHz" << endl;
										return (-1);
									}*/
									// Make sure FC user gave is within the range of VENDOR_FREQ_RANGE_HIGH-VENDOR_FREQ_RANGE_LOW
									TF_Channel_DS[g_moduleNum][g_channelNum].F1 = fValue;
									TF_Channel_DS[g_moduleNum][g_channelNum].BW = TF_Channel_DS[g_moduleNum][g_channelNum].F2 - TF_Channel_DS[g_moduleNum][g_channelNum].F1;
									TF_Channel_DS[g_moduleNum][g_channelNum].FC = (TF_Channel_DS[g_moduleNum][g_channelNum].F2 + TF_Channel_DS[g_moduleNum][g_channelNum].F1)/2;
								}
								else
								{
									TF_Channel_DS[g_moduleNum][g_channelNum].F1 = fValue;
								}
							}
							else
							{
								cout << "ERROR: The F1 is out of range" << endl;
								return (-1);
							}
						}
						else
						{
							cout << "ERROR: The command attribute F1 is not numerical" << endl;
							PrintResponse("\01INVALID_ATTRIBUTE_DATA\04", ERROR_HI_PRIORITY);
							return (-1);
						}

					}
					else if(attr[0] == "F2")
					{
						if (Sscanf(attr[1], fValue,'f'))
						{
							// Get the float value of F2

							if (fValue > VENDOR_FREQ_RANGE_LOW && fValue < VENDOR_FREQ_RANGE_HIGH)
							{
								if(TF_Channel_DS[g_moduleNum][g_channelNum].F1 != 0)
								{
									if(fValue < TF_Channel_DS[g_moduleNum][g_channelNum].F1)
									{
										cout << "ERROR: The f2 is crossing of f1" << endl;
										return (-1);
									}
#ifndef _DEVELOPMENT_MODE_
									if(fValue - TF_Channel_DS[g_moduleNum][g_channelNum].F1 > VENDOR_BW_RANGE_HIGH)
#else
									if(fValue - TF_Channel_DS[g_moduleNum][g_channelNum].F1 > VENDOR_BW_RANGE_HIGH - VENDOR_FREQ_RANGE_LOW)
#endif
									{
										cout << "ERROR: The channel bandwidth is out of range" << endl;
										return (-1);
									}
									/*if(fmod((fValue - TF_Channel_DS[g_moduleNum][g_channelNum].F1),0.5) != 0)
									{
										cout << "ERROR: The Bandwidth is not multiples of 0.5 GHz" << endl;
										return (-1);
									}*/
									// Make sure FC user gave is within the range of VENDOR_FREQ_RANGE_HIGH-VENDOR_FREQ_RANGE_LOW
									TF_Channel_DS[g_moduleNum][g_channelNum].F2 = fValue;
									TF_Channel_DS[g_moduleNum][g_channelNum].BW = fValue - TF_Channel_DS[g_moduleNum][g_channelNum].F1;
									TF_Channel_DS[g_moduleNum][g_channelNum].FC = (TF_Channel_DS[g_moduleNum][g_channelNum].F2 + TF_Channel_DS[g_moduleNum][g_channelNum].F1)/2;
								}
								else
								{
									TF_Channel_DS[g_moduleNum][g_channelNum].F2 = fValue;
								}
							}
							else
							{
								cout << "ERROR: The F2 is out of range" << endl;
								return (-1);
							}
						}
						else
						{
							cout << "ERROR: The command attribute F2 is not numerical" << endl;
							PrintResponse("\01INVALID_ATTRIBUTE_DATA\04", ERROR_HI_PRIORITY);
							return (-1);
						}

					}
					else
					{
						cout << "ERROR: The command attribute is wrong" << endl;
						PrintResponse("\01INVALID_ATTRIBUTE\04", ERROR_HI_PRIORITY);
						return (-1);
					}

					break;
				}
				case 'B':
				{
					if (attr[0] == "BW")
					{
						// BW overlap test will be done later once all channels BW is received
						if (Sscanf(attr[1], fValue, 'f'))
						{
#ifdef _DEVELOPMENT_MODE_
							if(fValue > 0)		// BW +ve
#else
							if(fValue > VENDOR_MIN_BW && fValue < VENDOR_BW_RANGE_HIGH) //drc modified to VENDOR_MIN_BW
#endif
							{
								/*if(fmod(fValue,0.5) != 0)
								{
									cout << "ERROR: The BW is not multiples of 0.5 GHz" << endl;
									return (-1);
								}*/
								if(TF_Channel_DS[g_moduleNum][g_channelNum].FC != 0)
								{
									if((TF_Channel_DS[g_moduleNum][g_channelNum].FC + fValue/2) > VENDOR_FREQ_RANGE_HIGH ||
											(TF_Channel_DS[g_moduleNum][g_channelNum].FC - fValue/2) < VENDOR_FREQ_RANGE_LOW)
									{
										cout << "ERROR: The BW Range is unacceptable" << endl;
										return (-1);
									}
									TF_Channel_DS[g_moduleNum][g_channelNum].BW = fValue;
									TF_Channel_DS[g_moduleNum][g_channelNum].F1 = TF_Channel_DS[g_moduleNum][g_channelNum].FC - TF_Channel_DS[g_moduleNum][g_channelNum].BW/2;
									TF_Channel_DS[g_moduleNum][g_channelNum].F2 = TF_Channel_DS[g_moduleNum][g_channelNum].FC + TF_Channel_DS[g_moduleNum][g_channelNum].BW/2;
								}
							else{
								// Get the float value of BW
								TF_Channel_DS[g_moduleNum][g_channelNum].BW = fValue;
								}
							}
							else
							{
								cout << "ERROR: The BW Range is unacceptable" << endl;
								return (-1);
							}

						}
						else
						{
							cout << "ERROR: The command attribute BW is not numerical" << endl;
							PrintResponse("\01INVALID_ATTRIBUTE_DATA\04", ERROR_HI_PRIORITY);
							return (-1);
						}
					}
					else
					{
						cout << "ERROR: The command attribute is wrong" << endl;
						PrintResponse("\01INVALID_ATTRIBUTE\04", ERROR_HI_PRIORITY);
						return (-1);
					}
					break;
				}
#ifdef _DEVELOPMENT_MODE_
				case 'L':
				{
					if (attr[0] == "LAMDA")
					{
						if (Sscanf(attr[1], fValue,'f'))
						{
							TF_Channel_DS[g_moduleNum][g_channelNum].LAMDA = fValue;	// g_moduleNum and g_channelNums were found in SearchObject
						}
						else
						{
							cout << "ERROR: The command attribute LAMDA is not numerical" << endl;
							PrintResponse("\01INVALID_ATTRIBUTE_DATA\04", ERROR_HI_PRIORITY);
							return (-1);
						}
					}
					else
					{
						cout << "ERROR: The command attribute is wrong" << endl;
						PrintResponse("\01INVALID_ATTRIBUTE\04", ERROR_HI_PRIORITY);
						return (-1);
					}
					break;
				}
				case 'S':
				{
					if (attr[0] == "SIGMA")
					{
						if (Sscanf(attr[1], fValue,'f'))
						{
							TF_Channel_DS[g_moduleNum][g_channelNum].SIGMA = fValue;
						}
						else
						{
							cout << "ERROR: The command attribute sigma is not numerical" << endl;
							PrintResponse("\01INVALID_ATTRIBUTE_DATA\04", ERROR_HI_PRIORITY);
							return (-1);
						}
					}
					else
					{
						cout << "ERROR: The command attribute is wrong" << endl;
						PrintResponse("\01INVALID_ATTRIBUTE\04", ERROR_HI_PRIORITY);
						return (-1);
					}
					break;
				}
				case 'K':
				{
					if (attr[0] == "K_OP")
					{
						if (Sscanf(attr[1], fValue,'f'))
						{
							// Get the float value of ATT
							TF_Channel_DS[g_moduleNum][g_channelNum].K_OPP = fValue;
						}
						else
						{
							cout << "ERROR: The command attribute K_OP is not numerical" << endl;
							PrintResponse("\01INVALID_ATTRIBUTE_DATA\04", ERROR_HI_PRIORITY);
							return (-1);
						}
					}
					else if (attr[0] == "K_ATT")
					{
						if (Sscanf(attr[1], fValue,'f'))
						{
							// Get the float value of ATT
							TF_Channel_DS[g_moduleNum][g_channelNum].K_ATT = fValue;
						}
						else
						{
							cout << "ERROR: The command attribute ATT is not numerical" << endl;
							PrintResponse("\01INVALID_ATTRIBUTE_DATA\04", ERROR_HI_PRIORITY);
							return (-1);
						}
					}
					else
					{
						cout << "ERROR: The command attribute is wrong" << endl;
						PrintResponse("\01INVALID_ATTRIBUTE\04", ERROR_HI_PRIORITY);
						return (-1);
					}
					break;
				}
#endif
				default:
				{
					cout << "ERROR: The command attribute is wrong" << endl;
					PrintResponse("\01INVALID_ATTRIBUTE\04", ERROR_HI_PRIORITY);
					return (-1);
				}

				}
			break;		// for case CH_TF top switch
			}

		case CH_FG:
			{
				switch(attr[0][0])
				{
				case 'A':
				{
					if (attr[0] == "ADP" && objVec.size() == 3)
					{
						if (Sscanf<int>(attr[1], iValue, 'i'))
						{
							if(iValue >=1 && iValue <=VENDOR_MAX_PORT)	// Port 1 to 12
							{
								FG_Channel_DS[g_moduleNum][g_channelNum].ADP = iValue;
							}
							else
							{
								PrintResponse("\01INVALID_ATTRIBUTE_DATA\04", ERROR_HI_PRIORITY);
								return (-1);
							}
						}
						else
						{
							cout << "ERROR: The command attribute ADP is not numerical" << endl;
							PrintResponse("\01INVALID_ATTRIBUTE_DATA\04", ERROR_HI_PRIORITY);
							return (-1);
						}
					}
					else if (attr[0] == "ATT")
					{
						/****************************************/
						/*	    Attenuation Belongs to Slot      */
						/****************************************/
						if (objVec.size() == 4)
						{
							// If Object Slot is defined. M.N.S. If 'S' is defined means attention value is for slot not for channel

							// Check if Bandwidth of channel is defined or not
							if ((FG_Channel_DS[g_moduleNum][g_channelNum].F1 != 0) && (FG_Channel_DS[g_moduleNum][g_channelNum].F2 != 0))
							{
								//If Channel BW is defined then calculate slot numbers and add attenuation to the slot index

								/************below part i added, remove and uncomment if anything go wrong***********/
								if (Sscanf(attr[1], fValue,'f'))
								{
									if((FG_Channel_DS[g_moduleNum][g_channelNum].ATT + fValue) >= 0 && (FG_Channel_DS[g_moduleNum][g_channelNum].ATT + fValue) < 35.01)		// Slot attenuation is relative and should not be less than 0 or more than 20dbm
									{
										// Get the float value of ATT
										FG_Channel_DS[g_moduleNum][g_channelNum].slotsATTEN[g_slotNum - 1] = fValue;
									}
									else
									{
										cout << "ERROR: The slot attenuation is violating channel relative attenuation rule" << endl;
										return (-1);
									}
								}
								else
								{
									cout << "ERROR: The Command Attribute ATT is not numerical" << endl;
									PrintResponse("\01INVALID_ATTRIBUTE_DATA\04", ERROR_HI_PRIORITY);
									return (-1);
								}

							}
							else
							{
								cout << "ERROR: The Channel Bandwidth (F1 & F2) not defined" << endl;
								return (-1);
							}
						}

						/****************************************/
						/*	   Attenuation Belongs to Channel    */
						/****************************************/

						else
						{
							// The Object doesn't have slot defined. The attenuation value belongs to channel
							if (Sscanf(attr[1], fValue,'f'))
							{
								if(fValue >=0 && fValue < 35.01)		// Can't be negative //drc confirmed with L and zte, should be 35.0
								{
									// Test all slots to make sure it doesnt violate relationship with channel attenuation
									for(int s=0; s< FG_Channel_DS[g_moduleNum][g_channelNum].slotNum; s++)
									{
										if((FG_Channel_DS[g_moduleNum][g_channelNum].slotsATTEN[s] + fValue) < 0 || (FG_Channel_DS[g_moduleNum][g_channelNum].slotsATTEN[s] + fValue) > 35.0)		// Slot attenuation is relative and should not be less than 0 or more than 20dbm
										{
											cout << "ERROR: The channel attenuation is violating slot relative attenuation rule" << endl;
											return (-1);
										}
									}
								}
								else
								{
									cout << "ERROR: The Channel Attenuation range is (0-35.0dbm +ve)" << endl;
									return (-1);
								}
								FG_Channel_DS[g_moduleNum][g_channelNum].ATT = fValue;
							}
							else
							{
								cout << "ERROR: The command attribute ATT is not numerical" << endl;
								PrintResponse("\01INVALID_ATTRIBUTE_DATA\04", ERROR_HI_PRIORITY);
								return (-1);
							}
						}
					}
#ifdef _DEVELOPMENT_MODE_
					else if (attr[0] == "A_OP")
					{
						if (Sscanf(attr[1], fValue,'f'))
						{
							FG_Channel_DS[g_moduleNum][g_channelNum].A_OPP = fValue;
						}
						else
						{
							cout << "ERROR: The command attribute a_op is not numerical" << endl;
							PrintResponse("\01INVALID_ATTRIBUTE_DATA\04", ERROR_HI_PRIORITY);
							return (-1);
						}
					}
					else if (attr[0] == "A_ATT")
					{
						if (Sscanf(attr[1], fValue,'f'))
						{
							// Get the float value of ATT
							FG_Channel_DS[g_moduleNum][g_channelNum].A_ATT = fValue;
						}
						else
						{
							cout << "ERROR: The command attribute A_ATT is not numerical" << endl;
							PrintResponse("\01INVALID_ATTRIBUTE_DATA\04", ERROR_HI_PRIORITY);
							return (-1);
						}
					}
#endif
					else
					{
						cout << "ERROR: The command attribute is wrong" << endl;
						PrintResponse("\01INVALID_ATTRIBUTE\04", ERROR_HI_PRIORITY);
						return (-1);
					}

					break;
				}
				case 'C':
				{
					if (attr[0] == "CMP" && objVec.size() == 3)
					{
						if (Sscanf(attr[1], iValue, 'i'))
						{
							if(iValue == 1 || iValue == 2)
							{
								FG_Channel_DS[g_moduleNum][g_channelNum].CMP = iValue;
							}
							else
							{
								cout << "ERROR: Invalid CMP value" << endl;
								PrintResponse("\01INVALID_ATTRIBUTE_DATA\04", ERROR_HI_PRIORITY);
								return (-1);
							}
						}
						else
						{
							cout << "ERROR: The command attribute CMP is not numerical" << endl;
							PrintResponse("\01INVALID_ATTRIBUTE_DATA\04", ERROR_HI_PRIORITY);
							return (-1);
						}
					}
					else
					{
						cout << "ERROR: The command attribute is wrong" << endl;
						PrintResponse("\01INVALID_ATTRIBUTE\04", ERROR_HI_PRIORITY);
						return (-1);
					}

					break;
				}
				case 'F':
				{
					if (attr[0] == "F1")
					{
						// We ignore if f2 or f1 is not defined at the moment, we only give bandwidth not integral error when both f1 and f2 are defined and then bandwidth not integral of slotsize
						double f1;
						if (Sscanf(attr[1], f1,'f'))
						{
							// If F1 is numerical
							if (f1 > VENDOR_FREQ_RANGE_LOW && f1 < VENDOR_FREQ_RANGE_HIGH)
							{
								if(g_edgeFreqDefined == 1 && f1 > FG_Channel_DS[g_moduleNum][g_channelNum].F2)
								{
									cout << "ERROR: The Edge Freqs F1 & F2 are crossed" << endl;
									return (-1);
								}

								changeF1 = true;	// Signal that F1 is changed
								prevF1 = FG_Channel_DS[g_moduleNum][g_channelNum].F1;	// save previous value of F1 to compare contract or expand later
								FG_Channel_DS[g_moduleNum][g_channelNum].F1 = f1;
								//std::cout << std::setprecision(10) << " F1 = " << FG_Channel_DS[g_moduleNum][g_channelNum].F1 <<std::endl;
								g_edgeFreqDefined++;	// whether user provided only F1 or F2, or both. for both == 2, for F1/F2 == 1

								if(FG_Channel_DS[g_moduleNum][g_channelNum].F2 != 0) //(changeF2 == true)	// Fill FC and BW of Fixed Grid channel
								{
									FG_Channel_DS[g_moduleNum][g_channelNum].BW = FG_Channel_DS[g_moduleNum][g_channelNum].F2 - FG_Channel_DS[g_moduleNum][g_channelNum].F1;
									FG_Channel_DS[g_moduleNum][g_channelNum].FC = (FG_Channel_DS[g_moduleNum][g_channelNum].F2 + FG_Channel_DS[g_moduleNum][g_channelNum].F1)/2;
									//std::cout << std::setprecision(10) << " FC = " << FG_Channel_DS[g_moduleNum][g_channelNum].FC <<std::endl;

									if(FG_Channel_DS[g_moduleNum][g_channelNum].BW < VENDOR_MIN_BW) //drc to check minimal BW
									{
										cout << "ERROR: BW range is not acceptable being less than 6.25GHz" << endl;
										PrintResponse("\01INVALID_ATTRIBUTE_DATA\04", ERROR_HI_PRIORITY);
										return (-1);
									}
									else if(FG_Channel_DS[g_moduleNum][g_channelNum].BW > WHOLE_BANDWIDTH)
									{
										cout << "ERROR: BW range is not acceptable being greater than full waveband" << endl;
										PrintResponse("\01INVALID_ATTRIBUTE_DATA\04", ERROR_HI_PRIORITY);
										return (-1);

									}
								}
							}
							else
							{
								cout << "ERROR: The F1 is not in desired range (i.e VENDOR_FREQ_RANGE_LOW - VENDOR_FREQ_RANGE_HIGH)" << endl;
								return (-1);
							}
						}
						else
						{
							cout << "ERROR: The command attribute F1 is not numerical" << endl;
							PrintResponse("\01INVALID_ATTRIBUTE_DATA\04", ERROR_HI_PRIORITY);
							return (-1);
						}
					}
					else if (attr[0] == "F2")
					{
						double f2;
						if (Sscanf(attr[1], f2,'f'))
						{
							// If F2 is numerical
							if (f2 > VENDOR_FREQ_RANGE_LOW && f2 < VENDOR_FREQ_RANGE_HIGH)
							{
								if(g_edgeFreqDefined == 1 && f2 < FG_Channel_DS[g_moduleNum][g_channelNum].F1)
								{
									cout << "ERROR: The Edge Freqs F1 & F2 are crossed" << endl;
									return (-1);
								}

								changeF2 = true;	// Signal that F2 is changed
								prevF2 = FG_Channel_DS[g_moduleNum][g_channelNum].F2;	// save previous value of F2 to compare contract or expand later
								FG_Channel_DS[g_moduleNum][g_channelNum].F2 = f2;
								g_edgeFreqDefined++;	// whether user provided only F1 or F2, or both. for both == 2, for F1/F2 == 1

								if(FG_Channel_DS[g_moduleNum][g_channelNum].F1 != 0) //(changeF1 == true)	// Fill FC and BW of Fixed Grid channel
								{
									FG_Channel_DS[g_moduleNum][g_channelNum].BW = FG_Channel_DS[g_moduleNum][g_channelNum].F2 - FG_Channel_DS[g_moduleNum][g_channelNum].F1;
									FG_Channel_DS[g_moduleNum][g_channelNum].FC = (FG_Channel_DS[g_moduleNum][g_channelNum].F2 + FG_Channel_DS[g_moduleNum][g_channelNum].F1)/2;

									if(FG_Channel_DS[g_moduleNum][g_channelNum].BW < VENDOR_MIN_BW)
									{
										PrintResponse("\01INVALID_ATTRIBUTE_DATA\04", ERROR_HI_PRIORITY);
										return (-1);
									}
									else if(FG_Channel_DS[g_moduleNum][g_channelNum].BW > WHOLE_BANDWIDTH)
									{
										cout << "ERROR: BW range is not acceptable being greater than full waveband" << endl;
										PrintResponse("\01INVALID_ATTRIBUTE_DATA\04", ERROR_HI_PRIORITY);
										return (-1);

									}

								}
							}
							else
							{
								cout << "ERROR: The F2 is not in desired range (i.e VENDOR_FREQ_RANGE_LOW - VENDOR_FREQ_RANGE_HIGH)" << endl;
								return (-1);
							}
						}
						else
						{
							cout << "ERROR: The command attribute F1 is not numerical" << endl;
							PrintResponse("\01INVALID_ATTRIBUTE_DATA\04", ERROR_HI_PRIORITY);
							return (-1);
						}
					}
					else if (attr[0] == "FC")
					{
						double fc;
						if (Sscanf(attr[1], fc,'f'))
						{
							// Get the float value of FC

							if (fc > VENDOR_FREQ_RANGE_LOW && fc < VENDOR_FREQ_RANGE_HIGH)
							{
								// Make sure FC user gave is within the range of VENDOR_FREQ_RANGE_HIGH-VENDOR_FREQ_RANGE_LOW
								FG_Channel_DS[g_moduleNum][g_channelNum].FC  = fc;
								++g_edgeFreqDefined;

								if(FG_Channel_DS[g_moduleNum][g_channelNum].BW != 0)
								{
									FG_Channel_DS[g_moduleNum][g_channelNum].F1 = FG_Channel_DS[g_moduleNum][g_channelNum].FC - FG_Channel_DS[g_moduleNum][g_channelNum].BW/2;
									FG_Channel_DS[g_moduleNum][g_channelNum].F2 = FG_Channel_DS[g_moduleNum][g_channelNum].FC + FG_Channel_DS[g_moduleNum][g_channelNum].BW/2;
									//cout << "F1" << FG_Channel_DS[g_moduleNum][g_channelNum].F1 << endl;
									//cout << "F2" << FG_Channel_DS[g_moduleNum][g_channelNum].F2 << endl;
									if(FG_Channel_DS[g_moduleNum][g_channelNum].F1 < VENDOR_FREQ_RANGE_LOW)
									{
										cout << "ERROR: The F1 is out of range" << endl;
										return (-1);
									}
									else if( FG_Channel_DS[g_moduleNum][g_channelNum].F2 > VENDOR_FREQ_RANGE_HIGH)
									{
										cout << "ERROR: The F2 is out of range" << endl;
										return (-1);
									}
								}
							}
							else
							{
								cout << "ERROR: The Fc is out of range" << endl;
								return (-1);
							}
						}
						else
						{
							cout << "ERROR: The command attribute FC is not numerical" << endl;
							PrintResponse("\01INVALID_ATTRIBUTE_DATA\04", ERROR_HI_PRIORITY);
							return (-1);
						}
					}
					else
					{
						cout << "ERROR: The command attribute is wrong" << endl;
						PrintResponse("\01INVALID_ATTRIBUTE\04", ERROR_HI_PRIORITY);
						return (-1);
					}

					break;
				}

				case 'B':
				{
					if (attr[0] == "BW")
					{
						double bw;
						if (Sscanf(attr[1], bw,'f'))
						{
							// Get the float value of FC
#ifdef _DEVELOPMENT_MODE_
							if(bw > 0)		// BW +ve
#else
							if(bw < WHOLE_BANDWIDTH && bw > VENDOR_MIN_BW) //drc modified to VENDOR_MIN_BW to WHOLE_BANDWIDTH
#endif
							{
								// Make sure FC user gave is within the range of VENDOR_FREQ_RANGE_HIGH-VENDOR_FREQ_RANGE_LOW
								FG_Channel_DS[g_moduleNum][g_channelNum].BW = bw;
								++g_edgeFreqDefined;

								if(FG_Channel_DS[g_moduleNum][g_channelNum].FC != 0)
								{
									FG_Channel_DS[g_moduleNum][g_channelNum].F1 = FG_Channel_DS[g_moduleNum][g_channelNum].FC - FG_Channel_DS[g_moduleNum][g_channelNum].BW/2;
									FG_Channel_DS[g_moduleNum][g_channelNum].F2 = FG_Channel_DS[g_moduleNum][g_channelNum].FC + FG_Channel_DS[g_moduleNum][g_channelNum].BW/2;

									//cout << "F1 BW" << FG_Channel_DS[g_moduleNum][g_channelNum].F1 << endl;
									//cout << "F2 BW" << FG_Channel_DS[g_moduleNum][g_channelNum].F2 << endl;
									if(FG_Channel_DS[g_moduleNum][g_channelNum].F1 < VENDOR_FREQ_RANGE_LOW)
									{
										cout << "ERROR: The F1 is out of range" << endl;
										return (-1);
									}
									else if( FG_Channel_DS[g_moduleNum][g_channelNum].F2 > VENDOR_FREQ_RANGE_HIGH)
									{
										cout << "ERROR: The F2 is out of range" << endl;
										return (-1);
									}
								}
							}
							else
							{
								cout << "ERROR: The BW is out of range" << endl;
								return (-1);
							}
						}
						else
						{
							cout << "ERROR: The command attribute FC is not numerical" << endl;
							PrintResponse("\01INVALID_ATTRIBUTE_DATA\04", ERROR_HI_PRIORITY);
							return (-1);
						}
					}
					else
					{
						cout << "ERROR: The command attribute is wrong" << endl;
						PrintResponse("\01INVALID_ATTRIBUTE\04", ERROR_HI_PRIORITY);
						return (-1);
					}

					break;
				}
#ifdef _DEVELOPMENT_MODE_

				case 'L':
				{
					if (attr[0] == "LAMDA")
					{
						if (Sscanf(attr[1], fValue,'f'))
						{
							FG_Channel_DS[g_moduleNum][g_channelNum].LAMDA = fValue;	// g_moduleNum and g_channelNums were found in SearchObject
						}
						else
						{
							cout << "ERROR: The command attribute LAMDA is not numerical" << endl;
							PrintResponse("\01INVALID_ATTRIBUTE_DATA\04", ERROR_HI_PRIORITY);
							return (-1);
						}
					}
					else
					{
						cout << "ERROR: The command attribute is wrong" << endl;
						PrintResponse("\01INVALID_ATTRIBUTE\04", ERROR_HI_PRIORITY);
						return (-1);
					}

					break;
				}
				case 'S':
				{
					if (attr[0] == "SIGMA")
					{
						if (Sscanf(attr[1], fValue,'f'))
						{
							FG_Channel_DS[g_moduleNum][g_channelNum].SIGMA = fValue;
						}
						else
						{
							cout << "ERROR: The command attribute Sigma is not numerical" << endl;
							PrintResponse("\01INVALID_ATTRIBUTE_DATA\04", ERROR_HI_PRIORITY);
							return (-1);
						}
					}
					else
					{
						cout << "ERROR: The command attribute is wrong" << endl;
						PrintResponse("\01INVALID_ATTRIBUTE\04", ERROR_HI_PRIORITY);
						return (-1);
					}

					break;
				}
				case 'K':
				{
					if (attr[0] == "K_OP")
					{
						if (Sscanf(attr[1], fValue,'f'))
						{
							// Get the float value of ATT
							FG_Channel_DS[g_moduleNum][g_channelNum].K_OPP = fValue;
						}
						else
						{
							cout << "ERROR: The command attribute K_OP is not numerical" << endl;
							PrintResponse("\01INVALID_ATTRIBUTE_DATA\04", ERROR_HI_PRIORITY);
							return (-1);
						}
					}
					else if (attr[0] == "K_ATT")
					{
						if (Sscanf(attr[1], fValue,'f'))
						{
							// Get the float value of ATT
							FG_Channel_DS[g_moduleNum][g_channelNum].K_ATT = fValue;
						}
						else
						{
							cout << "ERROR: The command attribute K_ATT is not numerical" << endl;
							PrintResponse("\01INVALID_ATTRIBUTE_DATA\04", ERROR_HI_PRIORITY);
							return (-1);
						}
					}
					else
					{
						cout << "ERROR: The command attribute is wrong" << endl;
						PrintResponse("\01INVALID_ATTRIBUTE\04", ERROR_HI_PRIORITY);
						return (-1);
					}

					break;
				}
#endif
				default:
				{
					cout << "ERROR: The command attribute is wrong" << endl;
					PrintResponse("\01INVALID_ATTRIBUTE\04", ERROR_HI_PRIORITY);
					return (-1);
				}
				}

			break;	// mains witch CH_FG
			}

		case MODULE:
		{
				//int value;
				switch(attr[0][0])
				{
#ifdef _DEVELOPMENT_MODE_
				case 'P':
				{
					if (attr[0] == "PT" && eVerb == SET)		// differentiating user verb because MODULE object is present in SET and ACTION both
					{
						// Point values for x,y phase and grayscale, so user can divide LUT into multiple linear LUTs

						int i_xValue, i_yValue;

						std::vector<std::string > xySplit = SplitCmd(attr[1], ",");
						if (Sscanf(xySplit[0], i_xValue, 'i') && Sscanf(xySplit[1], i_yValue, 'i'))
						{
//								pthread_mutex_lock(&myMutexModule);
//								arrModules[g_moduleNum - 1].xPhaseVal.push_back(i_xValue);
//								arrModules[g_moduleNum - 1].yGrayscaleVal.push_back(i_yValue);
//								arrModules[g_moduleNum - 1].b_NewValueSet = true;
//								pthread_mutex_unlock(&myMutexModule);
						}
						else
						{
							cout << "ERROR: The command attribute ADP is not numerical" << endl;
							PrintResponse("\01INVALID_ATTRIBUTE_DATA\04", ERROR_HI_PRIORITY);
							return (-1);
						}
					}
					else if (attr[0] == "PORT" && eVerb == SET)
					{
						int i_Value;
						if (Sscanf(attr[1], i_Value, 'i'))
						{
//								pthread_mutex_lock(&myMutexModule);
//								arrModules[g_moduleNum - 1].PortNo = i_Value;
//								pthread_mutex_unlock(&myMutexModule);
						}
						else
						{
							cout << "ERROR: The Port# is not numerical" << endl;
							PrintResponse("\01INVALID_ATTRIBUTE_DATA\04", ERROR_HI_PRIORITY);
							return (-1);
						}
					}
					else if (attr[0] == "PHASEDEPTH" && eVerb == SET)	// Dr.Du said we wont change phase depth, it stays 2Pi, we discuss with Yidan on 5/24/2023
					{
						float f_Value;
						if (Sscanf<float>(attr[1], f_Value, 'f'))
						{
							pthread_mutex_lock(&global_mutex[LOCK_DEVMODE_VARS]);
							structDevelopMode.phaseDepth_changed = true;
							structDevelopMode.phaseDepth = f_Value;
							pthread_mutex_unlock(&global_mutex[LOCK_DEVMODE_VARS]);
						}
						else
						{
							cout << "ERROR: The PHASEDEPTH is not numerical" << endl;
							PrintResponse("\01INVALID_ATTRIBUTE_DATA\04", ERROR_HI_PRIORITY);
							return (-1);
						}
					}
					else if (attr[0] == "PHASESTART" && eVerb == SET)	// Dr.Du said we wont change phase depth, it stays 2Pi, we discuss with Yidan on 5/24/2023
					{
						int i_Value;
						if (Sscanf(attr[1], i_Value, 'i'))
						{
							pthread_mutex_lock(&global_mutex[LOCK_DEVMODE_VARS]);
							structDevelopMode.lutRange_changed = true;
							structDevelopMode.phaseStart = i_Value;
							pthread_mutex_unlock(&global_mutex[LOCK_DEVMODE_VARS]);
						}
						else
						{
							cout << "ERROR: The phase start not numerical" << endl;
							PrintResponse("\01INVALID_ATTRIBUTE_DATA\04", ERROR_HI_PRIORITY);
							return (-1);
						}
					}
					else if (attr[0] == "PHASEEND" && eVerb == SET)	// Dr.Du said we wont change phase depth, it stays 2Pi, we discuss with Yidan on 5/24/2023
					{
						int i_Value;
						if (Sscanf(attr[1], i_Value, 'i'))
						{
							pthread_mutex_lock(&global_mutex[LOCK_DEVMODE_VARS]);
							structDevelopMode.lutRange_changed = true;
							structDevelopMode.phaseEnd = i_Value;
							pthread_mutex_unlock(&global_mutex[LOCK_DEVMODE_VARS]);
						}
						else
						{
							cout << "ERROR: The phase send not numerical" << endl;
							PrintResponse("\01INVALID_ATTRIBUTE_DATA\04", ERROR_HI_PRIORITY);
							return (-1);
						}
					}
					else
					{
						cout << "ERROR: The command attribute is wrong" << endl;
						PrintResponse("\01INVALID_ATTRIBUTE\04", ERROR_HI_PRIORITY);
						return (-1);
					}
					break;
				}
				case 'F':
				{
					if (attr[0] == "FC_LOW" && eVerb == SET)		// differentiating user verb because MODULE object is present in SET and ACTION both
					{
						// Point values for x,y phase and grayscale, so user can divide LUT into multiple linear LUTs

						double f_Value2;

						if (Sscanf(attr[1], f_Value2,'f'))
						{
//								pthread_mutex_lock(&myMutexModule);
//								arrModules[g_moduleNum - 1].fc_low = f_Value2;
//								arrModules[g_moduleNum - 1].b_NewFreqSet = true;
//								pthread_mutex_unlock(&myMutexModule);
						}
						else
						{
							cout << "ERROR: The command attribute FC_LOW is not numerical" << endl;
							PrintResponse("\01INVALID_ATTRIBUTE_DATA\04", ERROR_HI_PRIORITY);
							return (-1);
						}
					}
					else if (attr[0] == "FC_HIGH" && eVerb == SET)		// differentiating user verb because MODULE object is present in SET and ACTION both
					{
						// Point values for x,y phase and grayscale, so user can divide LUT into multiple linear LUTs

						double f_Value2;

						if (Sscanf(attr[1], f_Value2,'f'))
						{
//								pthread_mutex_lock(&myMutexModule);
//								arrModules[g_moduleNum - 1].fc_high = f_Value2;
//								arrModules[g_moduleNum - 1].b_NewFreqSet = true;
//								pthread_mutex_unlock(&myMutexModule);
						}
						else
						{
							cout << "ERROR: The command attribute FC_HIGH is not numerical" << endl;
							PrintResponse("\01INVALID_ATTRIBUTE_DATA\04", ERROR_HI_PRIORITY);
							return (-1);
						}
					}
					else
					{
						cout << "ERROR: The command attribute is wrong" << endl;
						PrintResponse("\01INVALID_ATTRIBUTE\04", ERROR_HI_PRIORITY);
						return (-1);
					}
					break;
				}
				case 'T':
				{
					if (attr[0] == "TEMPINTERPOLATION" && eVerb == SET)
					{
						int i_Value2;

						if (Sscanf(attr[1], i_Value2, 'i'))
						{
//								pthread_mutex_lock(&myMutexModule);
//								arrModules[g_moduleNum - 1].TempInterpolation = i_Value2;
//								pthread_mutex_unlock(&myMutexModule);
						}
						else
						{
							cout << "ERROR: The command attribute Temp Interpolation is not numerical" << endl;
							PrintResponse("\01INVALID_ATTRIBUTE_DATA\04", ERROR_HI_PRIORITY);
							return (-1);
						}
					}
					else if (attr[0] == "TEC_TV" && eVerb == SET)		// differentiating user verb because MODULE object is present in SET and ACTION both
					{
						// Point values for x,y phase and grayscale, so user can divide LUT into multiple linear LUTs

						unsigned int ui_Value;

						if (Sscanf(attr[1], ui_Value,'i'))
						{
							pthread_mutex_lock(&global_mutex[LOCK_DEVMODE_VARS]);
							structDevelopMode.PIDSet = true;
							structDevelopMode.TEC_tv = ui_Value;
							pthread_mutex_unlock(&global_mutex[LOCK_DEVMODE_VARS]);
						}
						else
						{
							cout << "ERROR: The command attribute TEC is not numerical" << endl;
							PrintResponse("\01INVALID_ATTRIBUTE_DATA\04", ERROR_HI_PRIORITY);
							return (-1);
						}
					}
					else if (attr[0] == "TEC1_KP" && eVerb == SET)		// differentiating user verb because MODULE object is present in SET and ACTION both
					{
							// Point values for x,y phase and grayscale, so user can divide LUT into multiple linear LUTs

							unsigned int ui_Value;

							if (Sscanf(attr[1], ui_Value,'i'))
							{
								pthread_mutex_lock(&global_mutex[LOCK_DEVMODE_VARS]);
								structDevelopMode.PIDSet = true;
								structDevelopMode.TEC1_kp = ui_Value;
								pthread_mutex_unlock(&global_mutex[LOCK_DEVMODE_VARS]);
							}
							else
							{
								cout << "ERROR: The command attribute TEC1 KP is not numerical" << endl;
								PrintResponse("\01INVALID_ATTRIBUTE_DATA\04", ERROR_HI_PRIORITY);
								return (-1);
							}
					}
					else if (attr[0] == "TEC1_KI" && eVerb == SET)		// differentiating user verb because MODULE object is present in SET and ACTION both
					{
							// Point values for x,y phase and grayscale, so user can divide LUT into multiple linear LUTs

							unsigned int ui_Value;

							if (Sscanf(attr[1], ui_Value,'i'))
							{
								pthread_mutex_lock(&global_mutex[LOCK_DEVMODE_VARS]);
								structDevelopMode.PIDSet = true;
								structDevelopMode.TEC1_ki = ui_Value;
								pthread_mutex_unlock(&global_mutex[LOCK_DEVMODE_VARS]);
							}
							else
							{
								cout << "ERROR: The command attribute TEC KI is not numerical" << endl;
								PrintResponse("\01INVALID_ATTRIBUTE_DATA\04", ERROR_HI_PRIORITY);
								return (-1);
							}
					}
					else if (attr[0] == "TEC1_KD" && eVerb == SET)		// differentiating user verb because MODULE object is present in SET and ACTION both
					{
							// Point values for x,y phase and grayscale, so user can divide LUT into multiple linear LUTs

							unsigned int ui_Value;

							if (Sscanf(attr[1], ui_Value,'i'))
							{
								pthread_mutex_lock(&global_mutex[LOCK_DEVMODE_VARS]);
								structDevelopMode.PIDSet = true;
								structDevelopMode.TEC1_kd = ui_Value;
								pthread_mutex_unlock(&global_mutex[LOCK_DEVMODE_VARS]);
							}
							else
							{
								cout << "ERROR: The command attribute TEC KD is not numerical" << endl;
								PrintResponse("\01INVALID_ATTRIBUTE_DATA\04", ERROR_HI_PRIORITY);
								return (-1);
							}
					}
					else if (attr[0] == "TEC2_TV" && eVerb == SET)		// differentiating user verb because MODULE object is present in SET and ACTION both
					{
						// Point values for x,y phase and grayscale, so user can divide LUT into multiple linear LUTs

						float f_Value2;

						if (Sscanf(attr[1], f_Value2,'f'))
						{
//								pthread_mutex_lock(&myMutexTEC);
//								if(copy_TECVars == false){
//									arrStructTEC.TEC2_tv = f_Value2;
//									TECSet = true;
//								}
//								pthread_mutex_unlock(&myMutexTEC);
						}
						else
						{
							cout << "ERROR: The command attribute TEC TV is not numerical" << endl;
							PrintResponse("\01INVALID_ATTRIBUTE_DATA\04", ERROR_HI_PRIORITY);
							return (-1);
						}
					}
					else if (attr[0] == "TEC2_KP" && eVerb == SET)		// differentiating user verb because MODULE object is present in SET and ACTION both
					{
							// Point values for x,y phase and grayscale, so user can divide LUT into multiple linear LUTs

							unsigned int ui_Value;

							if (Sscanf(attr[1], ui_Value,'i'))
							{
								pthread_mutex_lock(&global_mutex[LOCK_DEVMODE_VARS]);
								structDevelopMode.PIDSet = true;
								structDevelopMode.TEC2_kp = ui_Value;
								pthread_mutex_unlock(&global_mutex[LOCK_DEVMODE_VARS]);
							}
							else
							{
								cout << "ERROR: The command attribute TEC is not numerical" << endl;
								PrintResponse("\01INVALID_ATTRIBUTE_DATA\04", ERROR_HI_PRIORITY);
								return (-1);
							}
					}
					else if (attr[0] == "TEC2_KI" && eVerb == SET)		// differentiating user verb because MODULE object is present in SET and ACTION both
					{
							// Point values for x,y phase and grayscale, so user can divide LUT into multiple linear LUTs

							unsigned int ui_Value;

							if (Sscanf(attr[1], ui_Value,'i'))
							{
								pthread_mutex_lock(&global_mutex[LOCK_DEVMODE_VARS]);
								structDevelopMode.PIDSet = true;
								structDevelopMode.TEC2_ki = ui_Value;
								pthread_mutex_unlock(&global_mutex[LOCK_DEVMODE_VARS]);
							}
							else
							{
								cout << "ERROR: The command attribute TEC is not numerical" << endl;
								PrintResponse("\01INVALID_ATTRIBUTE_DATA\04", ERROR_HI_PRIORITY);
								return (-1);
							}
					}
					else if (attr[0] == "TEC2_KD" && eVerb == SET)		// differentiating user verb because MODULE object is present in SET and ACTION both
					{
							// Point values for x,y phase and grayscale, so user can divide LUT into multiple linear LUTs

							unsigned int ui_Value;

							if (Sscanf(attr[1], ui_Value,'i'))
							{
								pthread_mutex_lock(&global_mutex[LOCK_DEVMODE_VARS]);
								structDevelopMode.PIDSet = true;
								structDevelopMode.TEC2_kd = ui_Value;
								pthread_mutex_unlock(&global_mutex[LOCK_DEVMODE_VARS]);
							}
							else
							{
								cout << "ERROR: The command attribute TEC is not numerical" << endl;
								PrintResponse("\01INVALID_ATTRIBUTE_DATA\04", ERROR_HI_PRIORITY);
								return (-1);
							}
					}
					else if (attr[0] == "TEC1_PERIOD" && eVerb == SET)		// differentiating user verb because MODULE object is present in SET and ACTION both
					{
							// Point values for x,y phase and grayscale, so user can divide LUT into multiple linear LUTs

							float f_Value2;

							if (Sscanf(attr[1], f_Value2,'f'))
							{
//									pthread_mutex_lock(&myMutexTEC);
//									if(copy_TECVars == false){
//										arrStructTEC.TEC1_PERIOD = f_Value2;
//										TECSet = true;
//									}
//									pthread_mutex_unlock(&myMutexTEC);
							}
							else
							{
								cout << "ERROR: The command attribute TEC is not numerical" << endl;
								PrintResponse("\01INVALID_ATTRIBUTE_DATA\04", ERROR_HI_PRIORITY);
								return (-1);
							}
					}
					else if (attr[0] == "TEC2_PERIOD" && eVerb == SET)		// differentiating user verb because MODULE object is present in SET and ACTION both
					{
							// Point values for x,y phase and grayscale, so user can divide LUT into multiple linear LUTs

							float f_Value2;

							if (Sscanf(attr[1], f_Value2,'f'))
							{
//									pthread_mutex_lock(&myMutexTEC);
//									if(copy_TECVars == false){
//										arrStructTEC.TEC2_PERIOD = f_Value2;
//										TECSet = true;
//									}
//									pthread_mutex_unlock(&myMutexTEC);
							}
							else
							{
								cout << "ERROR: The command attribute TEC is not numerical" << endl;
								PrintResponse("\01INVALID_ATTRIBUTE_DATA\04", ERROR_HI_PRIORITY);
								return (-1);
							}
					}
					else if (attr[0] == "TECON" && eVerb == ACTION)
					{
						pthread_mutex_lock(&global_mutex[LOCK_DEVMODE_VARS]);
						structDevelopMode.startSendingTECData = true;
						pthread_mutex_unlock(&global_mutex[LOCK_DEVMODE_VARS]);
					}
					else if (attr[0] == "TECOFF" && eVerb == ACTION)
					{
						pthread_mutex_lock(&global_mutex[LOCK_DEVMODE_VARS]);
						structDevelopMode.startSendingTECData = false;
						pthread_mutex_unlock(&global_mutex[LOCK_DEVMODE_VARS]);
					}
					else
					{
						cout << "ERROR: The command attribute is wrong" << endl;
						PrintResponse("\01INVALID_ATTRIBUTE\04", ERROR_HI_PRIORITY);
						return (-1);
					}
					break;
				}
#endif
				case 'S':
				{
					if (attr[0] == "STORE" && eVerb == ACTION)	// differentiating user verb because MODULE object is present in SET and ACTION both
					{
						actionSR->StoreModule(g_moduleNum);

					}
					else if (attr[0] == "SLOTSIZE" && eVerb == SET)	// differentiating user verb because MODULE object is present in SET and ACTION both
					{
						if (attr[1] == "TF")
						{
							if (objVec[1] == "1")
							{
								// ACCESSING string vector of OBJECTs i.e. ch.module (previous search function) to know which module user selected
								// Test if prev and current mode of module is same or different, if different then make sure no channles on module before changing mode

								if (arrModules[g_moduleNum - 1].slotSize != "TF")	// current mode  different from prev one
								{
									if(TestChannelsNotActive(arrModules[g_moduleNum - 1].slotSize,g_moduleNum))	//check if current module has any channel?
									{
										eModule1 = TF;
										pthread_mutex_lock(&global_mutex[LOCK_CHANNEL_DS]);
										arrModules[0].slotSize = "TF";
										pthread_mutex_unlock(&global_mutex[LOCK_CHANNEL_DS]);
									}
									else	// Some channels are active and we must tell user can't change mode
									{
										cout << "ERROR: Can't Change Slotsize: Channels currently provisioned on the module" << endl;
										return (-1);
									}
								}

							}
							else if (objVec[1] == "2")
							{
								// Test if prev and current mode of module is same or different, if different then make sure no channles on module before changing mode

								if (arrModules[g_moduleNum - 1].slotSize != "TF")	// current mode  different from prev one
								{
									if(TestChannelsNotActive(arrModules[g_moduleNum - 1].slotSize,g_moduleNum))
									{
										eModule2 = TF;
										pthread_mutex_lock(&global_mutex[LOCK_CHANNEL_DS]);
										arrModules[1].slotSize = "TF";
										pthread_mutex_unlock(&global_mutex[LOCK_CHANNEL_DS]);
									}
									else	// Some channels are active and we must tell user can't change mode
									{
										cout << "ERROR: Can't Change Slotsize: Channels currently provisioned on the module" << endl;
										return (-1);
									}
								}
							}

						}
						else if (attr[1] == "625" || attr[1] == "125")	// It's fixed grid and get the slotsize 625 or 125
						{
							if (objVec[1] == "1")
							{
								// ACCESSING string vector of OBJECTs i.e. ch.module (previous search function) to know which module user selected
								// Test if prev and current mode of module is same or different, if different then make sure no channles on module before changing mode

								if (arrModules[g_moduleNum - 1].slotSize != attr[1])	// current mode  slotsize is different from prev one slotsize
								{
									if(TestChannelsNotActive(arrModules[g_moduleNum - 1].slotSize,g_moduleNum))
									{
										eModule1 = FIXEDGRID;
										pthread_mutex_lock(&global_mutex[LOCK_CHANNEL_DS]);
										arrModules[0].slotSize = attr[1];	// STRING set modules 1 slotsize... slotsize of arrModules input string
										pthread_mutex_unlock(&global_mutex[LOCK_CHANNEL_DS]);
									}
									else	// Some channels are active and we must tell user can't change mode
									{
										cout << "ERROR: Can't Change Slotsize: Channels currently provisioned on the module" << endl;
										return (-1);
									}
								}
							}
							else if (objVec[1] == "2")
							{
								if (arrModules[g_moduleNum - 1].slotSize != attr[1])	// current mode  slotsize is different from prev one slotsize
								{
									if(TestChannelsNotActive(arrModules[g_moduleNum - 1].slotSize,g_moduleNum))	//check if current module has any channel?
									{
										eModule2 = FIXEDGRID;
										pthread_mutex_lock(&global_mutex[LOCK_CHANNEL_DS]);
										arrModules[1].slotSize = attr[1];	// STRING set modules 1 slotsize... slotsize of arrModules input string
										pthread_mutex_unlock(&global_mutex[LOCK_CHANNEL_DS]);
									}
									else	// Some channels are active and we must tell user can't change mode
									{
										cout << "ERROR: Can't Change Slotsize: Channels currently provisioned on the module" << endl;
										return (-1);
									}
								}
							}

						}
						else
						{
							cout << "ERROR: The Module Slotsize is wrong" << endl;
							return (-1);
						}
					}
#ifdef _DEVELOPMENT_MODE_
					else if (attr[0] == "SIGMAS" && eVerb == SET)	// User send sigmas values for each port.. for development  mode, to save sigmas
					{
						std::vector<std::string > f_SigmaSplit = SplitCmd(attr[1], ","); // 0.00881,0.001155....
						float f_Value;

						if(f_SigmaSplit.size() <= 17)	// 17 different freqs 1544,1545,1546....1560nm  //drc: why?
						{
							for(unsigned int i=0; i< f_SigmaSplit.size() ; i++)
							{
								if(Sscanf(f_SigmaSplit[i],f_Value,'f'))
								{
//						    			pthread_mutex_lock(&myMutexModule);
//						    			arrModules[g_moduleNum - 1].v_SigmaVals.push_back(f_Value);	// convert string to float and push back to vector of sigmas
//										arrModules[g_moduleNum - 1].b_NewSigmasSet = true;
//						    			pthread_mutex_unlock(&myMutexModule);
								}
								else
								{
									cout << "ERROR: The PORT sigma is not numerical" << endl;
									return (-1);
								}

							}
						}
						else
						{
							cout << "ERROR: More than 17 sigma values are defined!" << endl;
							return (-1);
						}
					}
					else if (attr[0] == "SIGMAS_OFF" && eVerb == SET)	// User send sigmas values for each port.. for development  mode, to save sigmas
					{
						float f_Value;

						if(Sscanf(attr[1],f_Value,'f'))
						{
//								pthread_mutex_lock(&myMutexModule);
//								arrModules[g_moduleNum - 1].Sigma_offset = f_Value;	// convert string to float and push back to vector of sigmas
//								arrModules[g_moduleNum - 1].b_New_Sigma_offSet = true;
//								pthread_mutex_unlock(&myMutexModule);
						}
						else
						{
							cout << "ERROR: The PORT SIGMAS_OFF is not numerical" << endl;
							return (-1);
						}
					}
					else if (attr[0] == "S_K_ATT" && eVerb == SET)	// User set K_ATT values for each port.. for development  mode, to save k_ATT
					{
						std::vector<std::string > f_KattSplit = SplitCmd(attr[1], ","); // 0.00881,0.001155....
						float f_Value;

						if(f_KattSplit.size() <= 11)	// 11 different power attenuation ranges 0,2,4,6,8..20dbm
						{
							for(unsigned int i=0; i< f_KattSplit.size() ; i++)
							{
								if(Sscanf(f_KattSplit[i],f_Value,'f'))
								{
//										pthread_mutex_lock(&myMutexModule);
//										arrModules[g_moduleNum - 1].v_K_attVals.push_back(f_Value);	// convert string to float and push back to vector of sigmas
//										arrModules[g_moduleNum - 1].b_New_Katt_Set = true;
//										pthread_mutex_unlock(&myMutexModule);
								}
								else
								{
									cout << "ERROR: The PORT K_att is not numerical" << endl;
									return (-1);
								}

							}
						}
						else
						{
							cout << "ERROR: Less than 11 K_att values are defined!" << endl;
							return (-1);
						}
					}
					else if (attr[0] == "S_A_ATT" && eVerb == SET)	// User set A_ATT values for each port.. for development  mode, to save k_ATT
					{
						std::vector<std::string > f_AattSplit = SplitCmd(attr[1], ","); // 0.00881,0.001155....
						float f_Value;

						if(f_AattSplit.size() <= 11)	// 11 different power attenuation ranges 0,2,4,6,8..20dbm
						{
							for(unsigned int i=0; i< f_AattSplit.size() ; i++)
							{
								if(Sscanf(f_AattSplit[i],f_Value,'f'))
								{
//										pthread_mutex_lock(&myMutexModule);
//										arrModules[g_moduleNum - 1].v_A_attVals.push_back(f_Value);	// convert string to float and push back to vector of sigmas
//										arrModules[g_moduleNum - 1].b_New_Aatt_Set = true;
//										pthread_mutex_unlock(&myMutexModule);
								}
								else
								{
									cout << "ERROR: The PORT A_att is not numerical" << endl;
									return (-1);
								}

							}
						}
						else
						{
							cout << "ERROR: Less than 11 a_att values are defined!" << endl;
							return (-1);
						}
					}
					else if (attr[0] == "S_A_OFF" && eVerb == SET)	// User set A_ATT values for each port.. for development  mode, to save k_ATT
					{
						float f_Value;

						if(Sscanf(attr[1],f_Value,'f'))
						{
//								pthread_mutex_lock(&myMutexModule);
//								arrModules[g_moduleNum - 1].Aatt_offset = f_Value;	// convert string to float and push back to vector of sigmas
//								arrModules[g_moduleNum - 1].b_New_Aatt_offSet = true;
//								pthread_mutex_unlock(&myMutexModule);
						}
						else
						{
							cout << "ERROR: The PORT A_att is not numerical" << endl;
							return (-1);
						}
					}
					else if (attr[0] == "S_K_OFF" && eVerb == SET)	// User set A_ATT values for each port.. for development  mode, to save k_ATT
					{
						float f_Value;

						if(Sscanf(attr[1],f_Value,'f'))
						{
//								pthread_mutex_lock(&myMutexModule);
//								arrModules[g_moduleNum - 1].Katt_offset = f_Value;	// convert string to float and push back to vector of sigmas
//								arrModules[g_moduleNum - 1].b_New_Katt_offSet = true;
//								pthread_mutex_unlock(&myMutexModule);
						}
						else
						{
							cout << "ERROR: The PORT K_att is not numerical" << endl;
							return (-1);
						}
					}
					else if (attr[0] == "SEND_IMG" && eVerb == SET)	// User set A_ATT values for each port.. for development  mode, to save k_ATT
					{
						for(char& c : attr[1])			// make the path and filename to lowercase
						{
							c = std::tolower(c);
						}

						std::ifstream check(attr[1], std::ios::binary);

						if(!check.is_open())
						{
							cout << "ERROR: The file name is wrong\n**Make Sure File name is in lower letter**\n" << endl;
							return (-1);
						}

						pthread_mutex_lock(&global_mutex[LOCK_DEVMODE_VARS]);
						structDevelopMode.sendPattern = true;
						structDevelopMode.path = attr[1];
						pthread_mutex_unlock(&global_mutex[LOCK_DEVMODE_VARS]);

					}
					else if (attr[0] == "SEND_COLOR" && eVerb == SET)	// User set A_ATT values for each port.. for development  mode, to save k_ATT
					{
						int i_Value;
						if(Sscanf(attr[1],i_Value,'i'))
						{
							if(i_Value >=0 && i_Value <= 255)
							{
								pthread_mutex_lock(&global_mutex[LOCK_DEVMODE_VARS]);
								structDevelopMode.b_sendColor = true;
								structDevelopMode.colorValue = i_Value;
								pthread_mutex_unlock(&global_mutex[LOCK_DEVMODE_VARS]);
							}
							else
							{
								cout << "ERROR: The Color Value out of range (0-255)" << endl;
								return (-1);
							}
						}
						else
						{
							cout << "ERROR: The Color Value is not numerical" << endl;
							return (-1);
						}
					}
#endif
					else
					{
						cout << "ERROR: The command attribute is wrong" << endl;
						PrintResponse("\01INVALID_ATTRIBUTE\04", ERROR_HI_PRIORITY);
						return (-1);
					}

					break;
				}
				case 'R':
				{
					if (attr[0] == "RESTORE" && eVerb == ACTION)		// differentiating user verb because MODULE object is present in SET and ACTION both
					{
//						if(actionSR->bModuleConfigStored[g_moduleNum] == true)
	//					{
							actionSR->RestoreModule(g_moduleNum);
	//					}
		//				else
			//			{
				//			break;
					//	}
					}
#ifdef _DEVELOPMENT_MODE_
					else if (attr[0] == "ROTATE" && eVerb == SET)
					{
						float f_Value;
						if (Sscanf(attr[1], f_Value, 'f'))
						{
							pthread_mutex_lock(&global_mutex[LOCK_DEVMODE_VARS]);
							structDevelopMode.rotateAngle_changed = true;
							structDevelopMode.rotate = f_Value;
							pthread_mutex_unlock(&global_mutex[LOCK_DEVMODE_VARS]);
						}
						else
						{
							cout << "ERROR: The Port# is not numerical" << endl;
							PrintResponse("\01INVALID_ATTRIBUTE_DATA\04", ERROR_HI_PRIORITY);
							return (-1);
						}
					}
#endif
					else
					{
						cout << "ERROR: The command attribute is wrong" << endl;
						PrintResponse("\01INVALID_ATTRIBUTE\04", ERROR_HI_PRIORITY);
						return (-1);
					}

					break;
				}
#ifdef _DEVELOPMENT_MODE_
				case 'G':
				{
					if (attr[0] == "GRAYLOWRANGE")
					{
						if (Sscanf(attr[1], iValue, 'i'))
						{
//								pthread_mutex_lock(&myMutexModule);
//								arrModules[g_moduleNum - 1].grayValueLow = iValue;
//								arrModules[g_moduleNum - 1].b_NewValueSet = true;
//								pthread_mutex_unlock(&myMutexModule);
						}
						else
						{
							cout << "ERROR: The command attribute GRAYLOWRANGE is not numerical" << endl;
							return (-1);
						}
					}
					else if (attr[0] == "GRAYHIGHRANGE")
					{
						if (Sscanf(attr[1], iValue, 'i'))
						{
//								pthread_mutex_lock(&myMutexModule);
//								arrModules[g_moduleNum - 1].grayValueHigh = iValue;
//								arrModules[g_moduleNum - 1].b_NewValueSet = true;
//								pthread_mutex_unlock(&myMutexModule);
						}
						else
						{
							cout << "ERROR: The command attribute GRAYHIGHRANGE is not numerical" << endl;
							return (-1);
						}
					}
					else
					{
						cout << "ERROR: The command attribute is wrong" << endl;
						PrintResponse("\01INVALID_ATTRIBUTE\04", ERROR_HI_PRIORITY);
						return (-1);
					}

					break;
				}
				case 'D':
				{
					if (attr[0] == "DEVELOPMODE")
					{
						if (Sscanf(attr[1], iValue, 'i'))
						{
							if(iValue == 1 || iValue == 0)
							{
								pthread_mutex_lock(&global_mutex[LOCK_DEVMODE_VARS]);
								structDevelopMode.developMode = iValue;     //drc to check whether mode change for module or panel

								if(iValue == 0)
								{	// Reset all development mode features if development mode is turned off
									structDevelopMode.PIDSet = false;
									structDevelopMode.sendPattern = false;
									structDevelopMode.startSendingTECData = false;
								}
								pthread_mutex_unlock(&global_mutex[LOCK_DEVMODE_VARS]);

								// Each time developMode is switched we reset modules and channels
								std::string chNum = "*";
								std::string modNum = "2";   //drc to check if sth.need to be done here
//								if(g_moduleNum == 1)
								{
									is_DeleteTFDone(modNum,chNum);	// Delete module 1 all channels
									is_DeleteFGDone(modNum,chNum);	// Delete module 1 all channels
								}
								//else if (g_moduleNum == 2)
								{
									modNum = "1";
									is_DeleteTFDone(modNum,chNum);	// Delete module 2 all channels
									is_DeleteFGDone(modNum,chNum);	// Delete module 2 all channels
								}

								eVerb = DELETE;		// Modified action verb to DELETE, even though command was SET:MODULE.1:developMod=0/1

								if(iValue == 1)
									PrintResponse("\01DHS124-C\04", ERROR_HI_PRIORITY);			// Send special code to WSScalibre software for developmode initialization
								else
									PrintResponse("\01DHS124-NC\04", ERROR_HI_PRIORITY);
							}
							else
							{
								PrintResponse("\01INVALID_ATTRIBUTE\04", ERROR_HI_PRIORITY);
								return (-1);
							}

						}
						else
						{
							cout << "ERROR: The command attribute is not integer" << endl;
							PrintResponse("\01INVALID_ATTRIBUTE\04", ERROR_HI_PRIORITY);
							return (-1);
						}
					}
					else
					{
						cout << "ERROR: The command attribute is wrong" << endl;
						PrintResponse("\01INVALID_ATTRIBUTE\04", ERROR_HI_PRIORITY);
						return (-1);
					}

					break;
				}
				case 'B': //drc modified here for background test station configuration command set:module.1: sigma=: pd=:k_opp:
				{
					float f_Value;
					if (attr[0] == "BACK_SIGMA" && eVerb == SET)
					{
						if(Sscanf(attr[1],f_Value,'f'))
						{
							pthread_mutex_lock(&global_mutex[LOCK_DEVMODE_VARS]);
							structDevelopMode.b_backSigma = true;
								//structDevelopMode.backColorValue = i_Value;
							structDevelopMode.structBackgroundPara[g_moduleNum].Sigma = f_Value;
							pthread_mutex_unlock(&global_mutex[LOCK_DEVMODE_VARS]);
						}
					}
					else if (attr[0] == "BACK_PD" && eVerb == SET)
					{
						if(Sscanf(attr[1],f_Value,'f'))
						{
							pthread_mutex_lock(&global_mutex[LOCK_DEVMODE_VARS]);
							structDevelopMode.b_backPD = true;
							structDevelopMode.structBackgroundPara[g_moduleNum].PD = f_Value;
							pthread_mutex_unlock(&global_mutex[LOCK_DEVMODE_VARS]);
						}

					}
					else if (attr[0] == "BACK_K_OP" && eVerb == SET)
					{
						if(Sscanf(attr[1],f_Value,'f'))
						{
							pthread_mutex_lock(&global_mutex[LOCK_DEVMODE_VARS]);
							structDevelopMode.b_backK_Opt = true;
							structDevelopMode.structBackgroundPara[g_moduleNum].K_Opt = f_Value;
							pthread_mutex_unlock(&global_mutex[LOCK_DEVMODE_VARS]);
						}

					}
					else
					{
						cout << "ERROR: The command attribute BACK_* is not supported" << endl;
						PrintResponse("\01INVALID_ATTRIBUTE_DATA\04", ERROR_HI_PRIORITY);
						return (-1);
					}
					break;
				}
				case 'E':
				{
					int status;

					if (attr[0] == "EEPROMUPDATE" && eVerb == ACTION)
					{
						EEPROMUpdate update;
						status = update.InitiateUpdate();

						if(status == -1)
						{
							PrintResponse("\01EEPROM UPDATED FAILED. TRY AGAIN\04", ERROR_HI_PRIORITY);
							return (-1);
						}
					}
					else if (attr[0] == "EEPROMVERIFY" && eVerb == ACTION)
					{
						EEPROMUpdate verify;
						status = verify.VerifyWriteOperation();

						if(status == -1)
						{
							PrintResponse("\01EEPROM VERIFICATION FAILED. TRY AGAIN\04", ERROR_HI_PRIORITY);
							return (-1);
						}
					}
					else if (attr[0] == "EEPROMPRINT" && eVerb == ACTION)
					{
						EEPROMUpdate print;
						status = print.PrintEEPROM();

						if(status == -1)
						{
							PrintResponse("\01EEPROM PRINT FAILED. TRY AGAIN\04", ERROR_HI_PRIORITY);
							return (-1);
						}
					}
					else if(attr[0] == "EEPROMWRITE" && eVerb == SET)
					{
						std::vector<std::string > f_Split = SplitCmd(attr[1], ","); // file, address, size....

						int address;
						Sscanf(f_Split[1],address,'i');

						int size;
						Sscanf(f_Split[2],size,'i');

						EEPROMUpdate write;
						write.LoadAt(f_Split[0].c_str(), address, size);	// address in DECIMAL, to write at 0x3000 in EEPROM = 12288
					}
					else
					{
						cout << "ERROR: The command attribute is wrong" << endl;
						PrintResponse("\01INVALID_ATTRIBUTE\04", ERROR_HI_PRIORITY);
						return (-1);
					}
					break;
				}
#endif
				default:
				{
					cout << "ERROR: The command attribute is wrong" << endl;
					PrintResponse("\01INVALID_ATTRIBUTE\04", ERROR_HI_PRIORITY);
					return (-1);
				}

				}

			break;		// switch MODULE main one
			}


		case IDN:
		{
			std::string str_Value;
			if (attr[0] == "CUSTOMERINFO" && eVerb == SET)
			{
				strcpy(customerInfo, attr[1].c_str());
			}
			else
			{
				cout << "ERROR: The command attribute is wrong" << endl;
				PrintResponse("\01INVALID_ATTRIBUTE\04", ERROR_HI_PRIORITY);
				return (-1);
			}

			break;
		}
#ifdef _DEVELOPMENT_MODE_

		case PANEL:
		{
			if (attr[0] == "BGAP"	&& eVerb == SET)
			{
				unsigned int ui_Value;

				if (Sscanf(attr[1], ui_Value,'i'))
				{
					Panel p = GetPanelInfo();
					p.bottomGap = ui_Value;
					SetPanelInfo(p);
				}
				else
				{
					cout << "ERROR: The command attribute BGAP is not numerical" << endl;
					PrintResponse("\01INVALID_ATTRIBUTE_DATA\04", ERROR_HI_PRIORITY);
					return (-1);
				}
			}else if (attr[0] == "MGAP"	&& eVerb == SET){
				unsigned int ui_Value;

				if (Sscanf(attr[1], ui_Value,'i'))
				{
					Panel p = GetPanelInfo();
					p.middleGap = ui_Value;
					SetPanelInfo(p);
				}
				else
				{
					cout << "ERROR: The command attribute BGAP is not numerical" << endl;
					PrintResponse("\01INVALID_ATTRIBUTE_DATA\04", ERROR_HI_PRIORITY);
					return (-1);
				}
			}else if (attr[0] == "MGAP_POS"	&& eVerb == SET){
				unsigned int ui_Value;

				if (Sscanf(attr[1], ui_Value,'i'))
				{
					Panel p = GetPanelInfo();
					p.middleGapPosition = ui_Value;
					SetPanelInfo(p);
				}
				else
				{
					cout << "ERROR: The command attribute MGAP_POS is not numerical" << endl;
					PrintResponse("\01INVALID_ATTRIBUTE_DATA\04", ERROR_HI_PRIORITY);
					return (-1);
				}
			}else if (attr[0] == "TGAP"	&& eVerb == SET){
				unsigned int ui_Value;

				if (Sscanf(attr[1], ui_Value,'i'))
				{
					Panel p = GetPanelInfo();
					p.topGap = ui_Value;
					SetPanelInfo(p);
				}
				else
				{
					cout << "ERROR: The command attribute TGAP is not numerical" << endl;
					PrintResponse("\01INVALID_ATTRIBUTE_DATA\04", ERROR_HI_PRIORITY);
					return (-1);
				}
			}else if (attr[0] == "ENABLEGAP"	&& eVerb == SET){  //drc to check
				unsigned int ui_Value;

				if (Sscanf(attr[1], ui_Value,'i'))
				{
					Panel p = GetPanelInfo();
					p.b_gapSet = ui_Value;
					SetPanelInfo(p);
				}
				else
				{
					cout << "ERROR: The command attribute ENABLEGAP is not numerical" << endl;
					PrintResponse("\01INVALID_ATTRIBUTE_DATA\04", ERROR_HI_PRIORITY);
					return (-1);
				}
			}else if (attr[0] == "BACK_COLOR"	&& eVerb == SET){
				int i_Value;
				if(Sscanf(attr[1],i_Value,'i'))
				{
					if(i_Value >=0 && i_Value <= 255)
					{
						pthread_mutex_lock(&global_mutex[LOCK_DEVMODE_VARS]);
						structDevelopMode.b_backColor = true;
						structDevelopMode.backColorValue = i_Value;
						pthread_mutex_unlock(&global_mutex[LOCK_DEVMODE_VARS]);
					}
					else
					{
						cout << "ERROR: The Color Value out of range (0-255)" << endl;
						return (-1);
					}
				}
				else
				{
					cout << "ERROR: The command attribute ENABLEGAP is not numerical" << endl;
					PrintResponse("\01INVALID_ATTRIBUTE_DATA\04", ERROR_HI_PRIORITY);
					return (-1);
				}
			}else if (attr[0] == "TESTRIG_SWITCH" && eVerb == SET)
			{
				if (Sscanf(attr[1], iValue, 'i'))
				{
					structDevelopMode.m_switch = iValue;
					g_heaterNum = (iValue == 0? 1:2);

				}
				else
				{
					cout << "ERROR: The command attribute TESTRIG_SWITCH is not numerical" << endl;
					PrintResponse("\01INVALID_ATTRIBUTE_DATA\04", ERROR_HI_PRIORITY);
					return (-1);
				}
			}
			else
			{
				cout << "ERROR: The command attribute is wrong" << endl;
				PrintResponse("\01INVALID_ATTRIBUTE\04", ERROR_HI_PRIORITY);
				return (-1);
			}

			break;
		}
#endif
		case RESTART:		//ACTION Restart dont have attribute at all, if you come here it means user gave attribute so issue ERROR
		{
			if(eVerb == ACTION)
			{
				cout << "ERROR: The Action:Restart.1 doesn't need attribute" << endl;
				return (-1);
			}

			break;
		}
		case FWUPGRADE:
		{
			switch(attr[0][0])
			{
			case 'P':
			{
				if (attr[0] == "PREPARE")
				{
					// START PREPARING FOR FWUPGRADE

				}
				else
				{
					cout << "ERROR: The command attribute is wrong" << endl;
					PrintResponse("\01INVALID_ATTRIBUTE\04", ERROR_HI_PRIORITY);
					return (-1);
				}

				break;
			}
			case 'A':
			{
				if (attr[0] == "ACTIVATE")
				{
					// ACTIVATE FWUPGRADE

				}
				else
				{
					cout << "ERROR: The command attribute is wrong" << endl;
					PrintResponse("\01INVALID_ATTRIBUTE\04", ERROR_HI_PRIORITY);
					return (-1);
				}

				break;
			}
			case 'C':
			{
				if (attr[0] == "COMMIT")
				{
					// COMMIT FWUPGRADE

				}
				else
				{
					cout << "ERROR: The command attribute is wrong" << endl;
					PrintResponse("\01INVALID_ATTRIBUTE\04", ERROR_HI_PRIORITY);
					return (-1);
				}

				break;
			}
			case 'R':
			{
				if (attr[0] == "REVERT")
				{
					// REVERT FWUPGRADE

				}
				else
				{
					cout << "ERROR: The command attribute is wrong" << endl;
					PrintResponse("\01INVALID_ATTRIBUTE\04", ERROR_HI_PRIORITY);
					return (-1);
				}

				break;
			}
			default:
			{
				cout << "ERROR: The command attribute is wrong" << endl;
				PrintResponse("\01INVALID_ATTRIBUTE\04", ERROR_HI_PRIORITY);
				return (-1);
			}
			}

			break;
		}
		default:
		{
			cout << "ERROR: The command object is wrong" << endl;
			return (-1);
		}
	}

	return (0);
}


int CmdDecoder::Print_SearchAttributes(std::string &attributes)
{
	// Some places we used \t\t two tabs to align the texxt. double tab is used mostly in text length smaller
	// Search the eObject by using switch cases and then put if else statement on attribute, if attribute doesnt link to eObject used then we cause an error
	std::transform(attributes.begin(), attributes.end(), attributes.begin(), ::toupper);

	cout << "g_bNoAttribute = " << g_bNoAttribute << "eGet = " << eGet << endl;
	switch (eObject)
	{
		// Switch to Object user wrote

		case CH_TF:	// If user command is for True Flex module channels
		{
			if (eGet == ALL_CH_ALL_ATTR)
			{
				PrintAllChannelsTF(g_Total_Channels);
				g_bNoAttribute = false;
			}
			else if (eGet == ALL_ATTR_OF_CH)
			{
				PrintAllChannelsTF(1);
				g_bNoAttribute = false;
			}
			else if ((eGet == SOME_ATTR) && (attributes != " "))    //drc modified below according to L
			{
				if (attributes == "ADP")
				{
					FillBuffer_ConcatAttributes("id=%d\nmod=%d\nadp=%d\n", "adp=%d\n", false, TF_Channel_DS[g_moduleNum][g_channelNum].ADP);
				}
				else if (attributes == "ATT")
				{
					FillBuffer_ConcatAttributes("id=%d\nmod=%d\natt=%.4f\n", "att=%.4f\n", false, TF_Channel_DS[g_moduleNum][g_channelNum].ATT);
				}
				else if (attributes == "CMP")
				{
					FillBuffer_ConcatAttributes("id=%d\nmod=%d\ncmp=%d\n", "cmp=%d\n", false, TF_Channel_DS[g_moduleNum][g_channelNum].CMP);

				}
				else if (attributes == "FC")
				{
					FillBuffer_ConcatAttributes("id=%d\nmod=%d\nfc=%.3f\n", "fc=%.3f\n", false, TF_Channel_DS[g_moduleNum][g_channelNum].FC);

				}
				else if (attributes == "BW")
				{
					FillBuffer_ConcatAttributes("id=%d\nmod=%d\nbw=%.3f\n", "bw=%.3f\n", false, TF_Channel_DS[g_moduleNum][g_channelNum].BW);
				}
				else if(attributes == "CHANID")
				{
					FillBuffer_ConcatAttributes("id=%d\nmod=%d\nchanid=%d\n", "chanid=%d\n", false, g_channelNum);
				}
				else if (attributes == "ID")
				{
					FillBuffer_ConcatAttributes("mod=%d\nID=%d\n", "ID=%d\n", true, g_channelNum);   //drc to check
				}
				else
				{
					cout << "ERROR: Invalid Attribute" << endl;
					PrintResponse("\01INVALID_ATTRIBUTE\04", ERROR_HI_PRIORITY);
					return (-1);
				}

				g_bPrevAttrDisplayed = true;		// to flag that previously attributes were printed
			}
			else
			{
				cout << "ERROR: Invalid Get Format" << endl;
				return (-1);
			}

			break;
		}

		case CH_FG:
		{
			if (eGet == ALL_CH_ALL_ATTR)
			{
				PrintAllChannelsFG(g_Total_Channels);
				g_bNoAttribute = false;
			}
			else if (eGet == ALL_ATTR_OF_CH)
			{
				if(objVec.size() == 3)	// get:ch.1.2	only all channel 2 info
				{
					PrintAllChannelsFG(1);
					g_bNoAttribute = false;
				}
				else if (objVec.size() == 4)	// get:ch.1.2.2 slot given
				{
					// GET:CH.2.1.2
					// \01 delimiter added
//					std::cout << FG_Channel_DS[g_moduleNum][g_channelNum].slotsATTEN[g_slotNum-1];

					buffLenTemp += sprintf(&buff[buffLenTemp],
							"id=%d "
							"\nmod=%d "
							"\nslot=%d "
							"\nadp=%d "
							"\ncmp=%d "
							"\nf1=%0.3f"
							"\nf2=%0.3f"
							"\nATT=%0.3f\n", g_channelNum, g_moduleNum, g_slotNum,
							FG_Channel_DS[g_moduleNum][g_channelNum].ADP,  FG_Channel_DS[g_moduleNum][g_channelNum].CMP,
							FG_Channel_DS[g_moduleNum][g_channelNum].F1 +  (g_slotNum-1)*((FG_Channel_DS[g_moduleNum][g_channelNum].F2 - FG_Channel_DS[g_moduleNum][g_channelNum].F1)/FG_Channel_DS[g_moduleNum][g_channelNum].slotNum),
							FG_Channel_DS[g_moduleNum][g_channelNum].F1 + g_slotNum*((FG_Channel_DS[g_moduleNum][g_channelNum].F2 - FG_Channel_DS[g_moduleNum][g_channelNum].F1)/FG_Channel_DS[g_moduleNum][g_channelNum].slotNum),
							FG_Channel_DS[g_moduleNum][g_channelNum].slotsATTEN[g_slotNum-1]);	//Fill this string and get the length filled.
				}
				else
				{
					cout << "ERROR: Invalid Get Format" << endl;
					return (-1);
				}
			}
			else if (eGet == ALL_SLOTS_OF_CH)
			{
				// ALL SLOTS INFO	GET:CH.2.1.*
				PrintAllSlotsFG(FG_Channel_DS[g_moduleNum][g_channelNum].slotNum);
			}
			else if ((eGet == SOME_ATTR) && (attributes != " ") && objVec.size() == 2)	// No slot defined get:ch.1:att
			{
				if (attributes == "ADP")
				{
					FillBuffer_ConcatAttributes("id=%d \nmod=%d \nadp=%d\n", "ADP=%d\n", false, FG_Channel_DS[g_moduleNum][g_channelNum].ADP);
				}
				else if (attributes == "ATT")
				{
					FillBuffer_ConcatAttributes("id=%d \nmod=%d \natt=%.3f\n", "ATT=%.3f\n", false, FG_Channel_DS[g_moduleNum][g_channelNum].ATT);
				}
				else if (attributes == "CMP")
				{
					FillBuffer_ConcatAttributes("id=%d \nmod=%d \ncmp=%d\n", "CMP=%d\n", false, FG_Channel_DS[g_moduleNum][g_channelNum].CMP);
				}
				else if (attributes == "F1")
				{
					FillBuffer_ConcatAttributes("id=%d \nmod=%d \nf1=%.3f\n", "F1=%0.3f\n", false, FG_Channel_DS[g_moduleNum][g_channelNum].F1);
				}
				else if (attributes == "F2")
				{
					FillBuffer_ConcatAttributes("id=%d \nmod=%d \nf2=%.3f\n", "F2=%0.3f\n", false, FG_Channel_DS[g_moduleNum][g_channelNum].F2);
				}
				else if (attributes == "FC")
				{
					FillBuffer_ConcatAttributes("id=%d \nmod=%d \nfc=%.3f\n", "fc=%0.3f\n", false, FG_Channel_DS[g_moduleNum][g_channelNum].FC);
				}
				else if (attributes == "BW")
				{
					FillBuffer_ConcatAttributes("id=%d \nmod=%d \nf1=%.3f\n", "bw=%0.3f\n", false, FG_Channel_DS[g_moduleNum][g_channelNum].BW);
				}
				else if (attributes == "CHANID")
				{
// drc to check how to add chanid
					FillBuffer_ConcatAttributes("id=%d \nmod=%d \nchanid=%d\n", "chanid=%d\n", false, g_channelNum);  //FG_Channel_DS[g_moduleNum][g_channelNum].slotNum);
				}
				else if(attributes == "ID")
				{
					//just leave it
				}
				else
				{
					cout << "ERROR: Invalid Attribute" << endl;
					PrintResponse("\01INVALID_ATTRIBUTE\04", ERROR_HI_PRIORITY);
					return (-1);
				}

				g_bPrevAttrDisplayed = true;		// to flag that previously attributes were printed
			}
			else if ((eGet == SOME_ATTR) && (attributes != " ") && objVec.size() == 4)	// slot is defined
			{

				if (attributes == "ADP")
				{
					FillBuffer_ConcatAttributes("id=%d \nmod=%d \nadp=%d\n", "ADP=%d\n", false, FG_Channel_DS[g_moduleNum][g_channelNum].ADP);
				}
				else if (attributes == "ATT")
				{// GET:CH.2.1.2:att
					FillBuffer_ConcatAttributes("id=%d \nmod=%d \natt=%.3f\n", "ATT=%.3f\n", false, FG_Channel_DS[g_moduleNum][g_channelNum].slotsATTEN[g_slotNum-1]);
				}
				else if (attributes == "CMP")
				{
					FillBuffer_ConcatAttributes("id=%d \nmod=%d \ncmp=%d\n", "CMP=%d\n", false, FG_Channel_DS[g_moduleNum][g_channelNum].CMP);
				}
				else if (attributes == "F1")
				{
					FillBuffer_ConcatAttributes("id=%d \nmod=%d \nf1=%.3f\n", "F1=%0.3f\n", false, FG_Channel_DS[g_moduleNum][g_channelNum].F1 +
							(g_slotNum-1)*(FG_Channel_DS[g_moduleNum][g_channelNum].F2 - FG_Channel_DS[g_moduleNum][g_channelNum].F1)/FG_Channel_DS[g_moduleNum][g_channelNum].slotNum);
				}
				else if (attributes == "F2")
				{
					FillBuffer_ConcatAttributes("id=%d \nmod=%d \nf2=%.3f\n", "F2=%0.3f\n", false, FG_Channel_DS[g_moduleNum][g_channelNum].F1 +
							g_slotNum*(FG_Channel_DS[g_moduleNum][g_channelNum].F2 - FG_Channel_DS[g_moduleNum][g_channelNum].F1)/FG_Channel_DS[g_moduleNum][g_channelNum].slotNum);
				}
				else if (attributes == "FC")
				{
					FillBuffer_ConcatAttributes("id=%d \nmod=%d \nfc=%.3f\n", "fc=%0.3f\n", false, FG_Channel_DS[g_moduleNum][g_channelNum].F1 +
							(g_slotNum-1)*FG_Channel_DS[g_moduleNum][g_channelNum].BW + FG_Channel_DS[g_moduleNum][g_channelNum].BW/2);
				}
				else if (attributes == "BW")
				{
					FillBuffer_ConcatAttributes("id=%d \nmod=%d \nf1=%.3f\n", "bw=%0.3f\n", false, (FG_Channel_DS[g_moduleNum][g_channelNum].F2 -
							FG_Channel_DS[g_moduleNum][g_channelNum].F1)/FG_Channel_DS[g_moduleNum][g_channelNum].slotNum);
				}
				else if (attributes == "CHANID")
				{
// drc to check how to add chanid
					FillBuffer_ConcatAttributes("id=%d \nmod=%d \nchanid=%d\n", "chanid=%d\n", false, g_channelNum);  //FG_Channel_DS[g_moduleNum][g_channelNum].slotNum);
				}
				else if(attributes == "ID")
				{
					//just leave it
				}
				else
				{
					cout << "ERROR: Invalid Attribute" << endl;
					PrintResponse("\01INVALID_ATTRIBUTE\04", ERROR_HI_PRIORITY);
					return (-1);
				}

				g_bPrevAttrDisplayed = true;		// to flag that previously attributes were printed
			}
			else
			{
				cout << "ERROR: Invalid Get Format" << endl;
				return (-1);
			}

			break;
		}
		case HEATERMONITOR:
		{

			if (eGet == ALL_ATTR_OF_CH)		// Print all attributes
			{
				// \01 delimiter added
//				buffLenTemp += sprintf(&buff[buffLenTemp], "mod:%d\n", g_moduleNum);	//Fill this string and get the length filled.
				if(g_heaterNum == 1)
					buffLenTemp += sprintf(&buff[buffLenTemp], "TEMPACTUAL=%.3f", g_direct_LCOS_Temp); //drc to check temperature readings
				else if(g_heaterNum == 2)
					buffLenTemp += sprintf(&buff[buffLenTemp], "TEMPACTUAL=%.3f", g_direct_Hearter2_Temp); //drc to check temperature readings
				else{
					cout << "ERROR: Invalid Attribute" << endl;
					PrintResponse("\01INVALID_ATTRIBUTE\04", ERROR_HI_PRIORITY);
					return (-1);
				}
				g_bNoAttribute = false;
			}
			else if ((eGet == SOME_ATTR) && (attributes != " "))	// Attributes are provided get:heatermonitor:tempactual
			{
				if (attributes == "TEMPACTUAL")
				{
//					buffLenTemp += sprintf(&buff[buffLenTemp], "mod:%d\n", g_moduleNum);	//Fill this string and get the length filled.
					if(g_heaterNum == 1)
						buffLenTemp += sprintf(&buff[buffLenTemp], "TEMPACTUAL=%.3f", g_direct_LCOS_Temp); //drc to check temperature readings
					else if(g_heaterNum == 2)
						buffLenTemp += sprintf(&buff[buffLenTemp], "TEMPACTUAL=%.3f", g_direct_Hearter2_Temp); //drc to check temperature readings
					else{
						cout << "ERROR: Invalid Attribute" << endl;
						PrintResponse("\01INVALID_ATTRIBUTE\04", ERROR_HI_PRIORITY);
						return (-1);
					}
				}
				else
				{
					cout << "ERROR: Invalid Attribute" << endl;
					PrintResponse("\01INVALID_ATTRIBUTE\04", ERROR_HI_PRIORITY);
					return (-1);
				}
			}
			break;
		}
		case MODULE:
		{
			if (eGet == ALL_ATTR_OF_CH)		// Print all attributes
			{
				// \01 delimiter added															// string to char *
				//buffLenTemp += sprintf(&buff[buffLenTemp], "MODULE\t|\tSLOTSIZE\tID\n");	//Fill this string and get the length filled.
#ifndef _TWIN_WSS_
				buffLenTemp += sprintf(&buff[buffLenTemp], "ID=%d\nslotSize=%s\nBWE=%d\nFLow=%.3f\nFHigh=%.3f\ntfslotSize=%d\nm=%d\nn=%d\nconfigsavail=%s\n",
						g_moduleNum, arrModules[g_moduleNum - 1].slotSize.c_str(), 2, 191125.000, 196275.000, 625, 2, 23, "2x23");
#else
				buffLenTemp += sprintf(&buff[buffLenTemp], "ID=%d\nslotSize=%s\nBWE=%d\nFLow=%.3f\nFHigh=%.3f\ntfslotSize=%d\nm=%d\nn=%d\nconfigsavail=%s\n",
						g_moduleNum, arrModules[g_moduleNum - 1].slotSize.c_str(), 2, 191125.000, 196275.000, 625, 2, 23, "2x23");
#endif

				g_bNoAttribute = false;
			}
			else if(eGet == ALL_CH_ALL_ATTR)
			{
#ifdef _TWIN_WSS_
					buffLenTemp += sprintf(&buff[buffLenTemp], "ID=%d\nslotSize=%s\nBWE=%d\nFLow=%.3f\nFHigh=%.3f\ntfslotSize=%d\nm=%d\nn=%d\nconfigsavail=%s\n:\n",
							1, arrModules[0].slotSize.c_str(), 2, 191125.000, 196275.000, 625, 2, 23, "2x23");
					buffLenTemp += sprintf(&buff[buffLenTemp], "ID=%d\nslotSize=%s\nBWE=%d\nFLow=%.3f\nFHigh=%.3f\ntfslotSize=%d\nm=%d\nn=%d\nconfigsavail=%s\n:\n",
												2, arrModules[1].slotSize.c_str(), 2, 191125.000, 196275.000, 625, 2, 23, "2x23");
#else
					buffLenTemp += sprintf(&buff[buffLenTemp], "ID=%d\nslotSize=%s\nBWE=%d\nFLow=%.3f\nFHigh=%.3f\ntfslotSize=%d\nm=%d\nn=%d\nconfigsavail=%s\n",
							1, arrModules[0].slotSize.c_str(), 2, 191125.000, 196275.000, 625, 2, 23, "2x23");
#endif
					g_bNoAttribute = false;
			}
			else if ((eGet == SOME_ATTR) && (attributes != " "))	// Attributes are provided get:heatermonitor:tempactual
			{
				if (attributes == "SLOTSIZE")
				{
					FillBuffer_ConcatAttributes("ID=%d\nSLOTSIZE=%s\n", "SLOTSIZE=%s\n", true, arrModules[g_moduleNum - 1].slotSize.c_str());
				}

#ifdef _DEVELOPMENT_MODE_
				else if (attributes == "LCOSCONNECTION")
				{
					std::string tempStr;

					LCOSDisplayTest dispTest;
					dispTest.RunTest();
					dispTest.GetResult(tempStr);
					FillBuffer_ConcatAttributes("MODULE.%d:  LCOS Pin Connection=%s\n", "LCOS Pin Connection=%s\n", true, tempStr.c_str());
				}
#endif
				else
				{
					cout << "ERROR: Invalid Attribute" << endl;
					PrintResponse("\01INVALID_ATTRIBUTE\04", ERROR_HI_PRIORITY);
					return (-1);
				}
			}

			break;
		}
		case TEMP:   //drc to check
		{
			if (eGet == ALL_ATTR_OF_CH)		// Print all attributes
			{
				// \01 delimiter added
				//buffLenTemp += sprintf(&buff[buffLenTemp], "\nTECMONITOR.%d:\n", g_moduleNum);	//Fill this string and get the length filled.
//				buffLenTemp += sprintf(&buff[buffLenTemp], "Temp=%.3f\n", g_direct_LCOS_Temp);
				buffLenTemp += sprintf(&buff[buffLenTemp], "Temp=%.3f", g_direct_Hearter2_Temp);

//				g_bNoAttribute = false;
			}
			else if ((eGet == SOME_ATTR) && (attributes != " "))	// Attributes are provided get:heatermonitor:tempactual
			{
				if (attributes == "TEMPACTUAL") //
				{
					FillBuffer_ConcatAttributes("\nmod=%d  \nTEMPACTUAL=%s", "TEMPACTUAL=%s", true, g_direct_Hearter2_Temp);  //"DUMMY 35C"); //drc to check
				}
				else
				{
					cout << "ERROR: Invalid Attribute" << endl;
					PrintResponse("\01INVALID_ATTRIBUTE\04", ERROR_HI_PRIORITY);
					return (-1);
				}
			}
			break;
		}
		case TECMONITOR:  //drc to check
		{
			if (eGet == ALL_ATTR_OF_CH)		// Print all attributes
			{
				// \01 delimiter added
				//buffLenTemp += sprintf(&buff[buffLenTemp], "\nTECMONITOR.%d:\n", g_moduleNum);	//Fill this string and get the length filled.
				buffLenTemp += sprintf(&buff[buffLenTemp], "Tempactual=%.3f\n", g_direct_LCOS_Temp);
//				buffLenTemp += sprintf(&buff[buffLenTemp], "Temp=%.3f\n", g_direct_Hearter2_Temp);

				g_bNoAttribute = false;
			}
			else if ((eGet == SOME_ATTR) && (attributes != " "))	// Attributes are provided get:heatermonitor:tempactual
			{
				if (attributes == "TEMPACTUAL") //
				{
					FillBuffer_ConcatAttributes("\nmod=%d  \nTEMPACTUAL=%s\n", "TEMPACTUAL=%s\n", true, g_direct_LCOS_Temp);// "DUMMY 35C"); //drc to check
				}
				else
				{
					cout << "ERROR: Invalid Attribute" << endl;
					PrintResponse("\01INVALID_ATTRIBUTE\04", ERROR_HI_PRIORITY);
					return (-1);
				}
			}
			break;
		}
		case IDN:	// Printing format is verticle
		{
			if (eGet == ALL_ATTR_OF_CH)		// Print all attributes
			{
				// \01 delimiter added
				//buffLenTemp += sprintf(&buff[buffLenTemp], "IDN.%d:\n", g_moduleNum);	//Fill this string and get the length filled.
				buffLenTemp += sprintf(&buff[buffLenTemp], "VendorName:%s\nVendorPartNumber:%d\nVendorSerialNumber:%d\nVendorRevision:%d\n"
															"ManufacturingDate:%s\nManufacturingVintage:%s\nCustomerPartNumber:%d\nCustomerSerialNumber:%d\n"
															"CustomerRevision:%d\nHardwarePartNumber:%d\nHardwareSerialNumber:%d\nHardwareRevision:%d\n"
															"LCOSPartNumber:%d\nLCOSSerialNumber:%d\nLCOSRevision:%d\nOpticsPartNumber:%d\nOpticsSerialNumber:%d\n"
															"OpticsRevision:%d\nFirmwareRelease:%d\nBootloaderRelease:%d\nFPGAVersion:%d\nDatabaseVersion:%d\n"
															"ModuleType:%d\nUnitSerialNumber:%d\nDateOfManufacture:%s\nCalibrationVersion:%d\nHardwareRelease:%d\n"
															"CustomerInfo:%s\n",
															"Glosine Tech", 01,202001,01,"2024-07-11","highVintage", 02, 200212, 02, 01, 101, 1001, 02,
															9001,901,01,01,1001,01,01,01,01,01,01,"2024-07-11",01,01,customerInfo);	//Fill this string and get the length filled.

				g_bNoAttribute = false;
			}
			else if ((eGet == SOME_ATTR) && (attributes != " "))	// Attributes are provided get:heatermonitor:tempactual
			{
				if (attributes == "VENDORNAME")
				{
					//FillBuffer_ConcatAttributes("VendorName=%s\n", "VendorName=%s\n", true, "Glosine Tech");
					buffLenTemp += sprintf(&buff[buffLenTemp], "VendorName:%s\n", "Glosine Tech");
				}
				else if (attributes == "VENDORPARTNUMBER")
				{
					FillBuffer_ConcatAttributes("VendorPartNumber:%d\n", "VendorPartNumber:%d\n", true, 01);
				}
				else if (attributes == "VENDORSERIALNUMBER")
				{
					FillBuffer_ConcatAttributes("VendorSerialNumber:%d\n", "VendorSerialNumber:%d\n", true, 202407);
				}
				else if (attributes == "VENDORREVISION")
				{
					FillBuffer_ConcatAttributes("VendorRevision:%d\n", "VendorRevision:%d\n", true, 01);
				}
				else if (attributes == "MANUFACTURINGDATE")
				{
					FillBuffer_ConcatAttributes("ManufacturingDate:%s\n", "VendorRevision:%d\n", true, "2021-07-11");
				}
				else if (attributes == "MANUFACTURINGVINTAGE")
				{
					FillBuffer_ConcatAttributes("ManufacturingVintage:%s\n", "ManufacturingVintage:%s", true, "highVintage");
				}
				else if (attributes == "CUSTOMERPARTNUMBER")
				{
					FillBuffer_ConcatAttributes("CustomerPartNumber:%d\n", "CustomerPartNumber:%d\n", true, 02);
				}
				else if (attributes == "CUSTOMERSERIALNUMBER")
				{
					FillBuffer_ConcatAttributes("CustomerSerialNumber:%d\n", "CustomerSerialNumber:%d\n", true, 200212);
				}
				else if (attributes == "CUSTOMERREVISION")
				{
					FillBuffer_ConcatAttributes("CustomerRevision:%d\n", "CustomerRevision:%d\n", true, 02);
				}
				else if (attributes == "HARDWAREPARTNUMBER")
				{
					FillBuffer_ConcatAttributes("HardwarePartNumber:%d\n", "HardwarePartNumber:%d\n", true, 01);
				}
				else if (attributes == "HARDWARESERIALNUMBER")
				{
					FillBuffer_ConcatAttributes("HardwareSerialNumber:%d\n", "HardwareSerialNumber:%d\n", true, 101);
				}
				else if (attributes == "HARDWAREREVISION")
				{
					FillBuffer_ConcatAttributes("HardwareRevision:%d\n", "HardwareRevision:%d\n", true, 101);
				}
				else if (attributes == "LCOSPARTNUMBER")
				{
					FillBuffer_ConcatAttributes("LCOSPartNumber:%d\n", "LCOSPartNumber:%d\n", true, 1001);
				}
				else if (attributes == "LCOSSERIALNUMBER")
				{
					FillBuffer_ConcatAttributes("LCOSSerialNumber:%d\n", "LCOSSerialNumber:%d\n", true, 02);
				}
				else if (attributes == "LCOSREVISION")
				{
					FillBuffer_ConcatAttributes("LCOSRevision:%d\n", "LCOSRevision:%d\n", true, 9001);
				}
				else if (attributes == "OPTICSPARTNUMBER")
				{
					FillBuffer_ConcatAttributes("OpticsPartNumber:%d\n", "OpticsPartNumber:%d\n", true, 901);
				}
				else if (attributes == "OPTICSSERIALNUMBER")
				{
					FillBuffer_ConcatAttributes("OpticsSerialNumber:%d\n", "OpticsSerialNumber:%d\n", true, 01);
				}
				else if (attributes == "OPTICSREVISION")
				{
					FillBuffer_ConcatAttributes("OpticsRevision:%d\n", "OpticsRevision:%d\n", true, 01);
				}
				else if (attributes == "FIRMWARERELEASE")
				{
					FillBuffer_ConcatAttributes("FirmwareRelease:%d\n", "FirmwareRelease:%d\n", true, 1001);
				}
				else if (attributes == "BOOTLOADERRELEASE")
				{
					FillBuffer_ConcatAttributes("BootloaderRelease:%d\n", "BootloaderRelease:%d\n", true, 1001);
				}
				else if (attributes == "FPGAVERSION")
				{
					FillBuffer_ConcatAttributes("FPGAVersion:%d\n", "FPGAVersion:%d\n", true, 01);
				}
				else if (attributes == "DATABASEVERSION")
				{
					FillBuffer_ConcatAttributes("DatabaseVersion:%d\n", "DatabaseVersion:%d\n", true, 01);
				}
				else if (attributes == "MODULETYPE")
				{
					FillBuffer_ConcatAttributes("ModuleType:%d\n", "ModuleType:%d\n", true, 01);
				}
				else if (attributes == "UNITSERIALNUMBER")
				{
					FillBuffer_ConcatAttributes("UnitSerialNumber:%d\n", "UnitSerialNumber:%d\n", true, 01);
				}
				else if (attributes == "DATEOFMANUFACTURE")
				{
					FillBuffer_ConcatAttributes("DateOfManufacture:%s\n", "DateOfManufacture:%s\n", true, "2024-07-11");
				}
				else if (attributes == "CALIBRATIONVERSION")
				{
					FillBuffer_ConcatAttributes("CalibrationVersion:%d\n", "CalibrationVersion:%d\n", true, 01);
				}
				else if (attributes == "HARDWARERELEASE")
				{
					FillBuffer_ConcatAttributes("HardwareRelease:%d\n", "HardwareRelease:%d\n", true, 01);
				}
				else if (attributes == "CUSTOMERINFO")
				{
//					FillBuffer_ConcatAttributes("CustomerInfo=%s\n", "CustomerInfo=%s\n", true, "BAIAN Tek");
					buffLenTemp += sprintf(&buff[buffLenTemp], "CustomerInfo:%s\n", customerInfo);
				}
				else
				{
					cout << "ERROR: Invalid Attribute" << endl;
					PrintResponse("\01INVALID_ATTRIBUTE\04", ERROR_HI_PRIORITY);
					return (-1);
				}
			}
			break;
		}
		case PANEL:
		{
			if (eGet == ALL_ATTR_OF_CH)		// Print all attributes
			{
				bool panelFlag {GetPanelInfo().readyFlag};

				// \01 delimiter added
				//buffLenTemp += sprintf(&buff[buffLenTemp], "\n");	//Fill this string and get the length filled.
				if(panelFlag == true)
				{
					buffLenTemp += sprintf(&buff[buffLenTemp], "READY:%s", " TRUE");
				}
				else
				{
					buffLenTemp += sprintf(&buff[buffLenTemp], "READY:%s", " FALSE");
				}
				g_bNoAttribute = false;
			}
			else if ((eGet == SOME_ATTR) && (attributes != " "))	// Attributes are provided get:heatermonitor:tempactual
			{
				if (attributes == "READY")
				{
					bool panelFlag {GetPanelInfo().readyFlag};

					//FillBuffer_ConcatAttributes("PANEL.%d:  READY\t=\t%d", "  READY\t=\t%s", true, panelFlag);
					//buffLenTemp += sprintf(&buff[buffLenTemp], "\n");	//Fill this string and get the length filled.
					if(panelFlag == true)
					{
						buffLenTemp += sprintf(&buff[buffLenTemp], "READY:%s", " TRUE");
					}
					else
					{
						buffLenTemp += sprintf(&buff[buffLenTemp], "READY:%s", " FALSE");
					}

				}
				else
				{
					cout << "ERROR: Invalid Attribute" << endl;
					PrintResponse("\01INVALID_ATTRIBUTE\04", ERROR_HI_PRIORITY);
					return (-1);
				}
			}
			break;
		}
		case CALFILE:		// some text have double tabs \t\t to align the text with less length
		{
			if (eGet == ALL_ATTR_OF_CH)		// Print all attributes
			{
				// \01 delimiter added
				buffLenTemp += sprintf(&buff[buffLenTemp], "CALFILE\t%d:\n", g_moduleNum);	//Fill this string and get the length filled.
				buffLenTemp += sprintf(&buff[buffLenTemp], "type\t\t=\t%s\nsequence\t\t=\t%s\nmajorVersion\t=\t%s\n"
															"minorVersion\t=\t%s\nserialNumber\t=\t%s\nproductCode\t=\t%s\n"
															"sequenceNumber\t=\t%s\ndate\t\t=\t%s\n",
															"channels", "1-2-3", "v3", "v1", "2020-01", "9999","3","6th June 2021");

				g_bNoAttribute = false;
			}
			else if ((eGet == SOME_ATTR) && (attributes != " "))	// Attributes are provided get:heatermonitor:tempactual
			{
				if (attributes == "TYPE")
				{
					FillBuffer_ConcatAttributes("CALFILE.%d:  Type\t\t=\t%s", "  Type\t\t=\t%s", true, "channels");
				}
				else if (attributes == "SEQUENCE")
				{
					FillBuffer_ConcatAttributes("CALFILE.%d:  Sequence\t\t=\t%s", "  Sequence\t\t=\t%s", true, "1-2-3");
				}
				else if (attributes == "MAJORVERSION")
				{
					FillBuffer_ConcatAttributes("CALFILE.%d:  nmajorVersion\t=\t%s", "  majorVersion\t=\t%s", true, "v3");
				}
				else if (attributes == "MINORVERSION")
				{
					FillBuffer_ConcatAttributes("CALFILE.%d:  minorVersion\t=\t%s", "  minorVersion\t=\t%s", true, "v1");
				}
				else if (attributes == "SERIALNUMBER")
				{
					FillBuffer_ConcatAttributes("CALFILE.%d:  serialNumber\t=\t%s", "  serialNumber\t=\t%s", true, "2020-01");
				}
				else if (attributes == "PRODUCTCODE")
				{
					FillBuffer_ConcatAttributes("CALFILE.%d:  productCode\t=\t%s", "  productCode\t=\t%s", true, "9999");
				}
				else if (attributes == "SEQUENCENUMBER")
				{
					FillBuffer_ConcatAttributes("CALFILE.%d:  sequenceNumber\t=\t%s", "  sequenceNumber\t=\t%s", true, "3");
				}
				else if (attributes == "DATE")
				{
					FillBuffer_ConcatAttributes("CALFILE.%d:  date\t\t=\t%s", "  date\t\t=\t%s", true, "6th June 2021");
				}
				else
				{
					cout << "ERROR: Invalid Attribute" << endl;
					PrintResponse("\01INVALID_ATTRIBUTE\04", ERROR_HI_PRIORITY);
					return (-1);
				}
			}
			break;
		}
		case FWUPGRADE:			// double tabs \t\t are used to align text with less length
		{
			if (eGet == ALL_ATTR_OF_CH)		// Print all attributes
			{
				// \01 delimiter added
				buffLenTemp += sprintf(&buff[buffLenTemp], "FWUPGRADE\t%d:\n", g_moduleNum);	//Fill this string and get the length filled.
				buffLenTemp += sprintf(&buff[buffLenTemp], "state\t\t=\t%s\nactiveBank\t\t=\t%s\npermanentFlag\t=\t%s\n"
															"temporaryFlag\t=\t%s\nfirmwareBankA\t=\t%s\nfirmwareBankB\t=\t%s\n",
															"Running", "BANK A", "TRUE", "FALSE", "2020-01", "2021-02");

				g_bNoAttribute = false;
			}
			else if ((eGet == SOME_ATTR) && (attributes != " "))	// Attributes are provided get:heatermonitor:tempactual
			{
				if (attributes == "STATE")
				{
					FillBuffer_ConcatAttributes("FWUPGRADE.%d:  state\t\t=\t%s", "  state\t\t=\t%s", true, "Running");
				}
				else if (attributes == "ACTIVEBANK")
				{
					FillBuffer_ConcatAttributes("FWUPGRADE.%d:  activeBank\t\t=\t%s", "  activeBank\t\t=\t%s", true, "BANK A");
				}
				else if (attributes == "PERMANENTFLAG")
				{
					FillBuffer_ConcatAttributes("FWUPGRADE.%d:  permanentFlag\t=\t%s", "  permanentFlag\t=\t%s", true, "TRUE");
				}
				else if (attributes == "TEMPORARYFLAG")
				{
					FillBuffer_ConcatAttributes("FWUPGRADE.%d:  temporaryFlag\t=\t%s", "  temporaryFlag\t=\t%s", true, "FALSE");
				}
				else if (attributes == "FIRMWAREBANKA")
				{
					FillBuffer_ConcatAttributes("FWUPGRADE.%d:  firmwareBankA\t=\t%s", "  firmwareBankA\t=\t%s", true, "2020-01");
				}
				else if (attributes == "FIRMWAREBANKB")
				{
					FillBuffer_ConcatAttributes("FWUPGRADE.%d:  firmwareBankB\t=\t%s", "  firmwareBankB\t=\t%s", true, "2021-02");
				}
				else
				{
					cout << "ERROR: Invalid Attribute" << endl;
					PrintResponse("\01INVALID_ATTRIBUTE\04", ERROR_HI_PRIORITY);
					return (-1);
				}
			}
			break;
		}
		case FAULT:		// used double tabs \t\t to align
		{
			if (eGet == ALL_ATTR_OF_CH)		// Print all attributes
			{
				MessDis Messtemp = {0};
				Get_logitem(g_faultNum,&Messtemp);
//				std::cout<< "g_faultNum FAULT Why:" << g_faultNum <<endl;
				// \01 delimiter added
				//buffLenTemp += sprintf(&buff[buffLenTemp], "FAULT\t%d:\n", g_faultNum);	//Fill this string and get the length filled.
				/*buffLenTemp += sprintf(&buff[buffLenTemp], "Name\t\t=\t%s\nTimestamp\t\t=\t%s\nDegraded\t\t=\t%s\n"
															"DegradedCount\t=\t%s\nRaised\t\t=\t%s\nRaisedCount\t=\t%s\n"
															"Debounce\t\t=\t%s\nFailCondition\t=\t%s\nDegradeCondition\t=\t%s\n",
															"Dummy ERROR DMA", "10:09:11 2nd July", "false", "01", "LOW", "0",
															"01","Dummy DMA failed","SEVERE");*/
				//buffLenTemp += sprintf(&buff[buffLenTemp], "\04\n");	// \04 delimiter added
				buffLenTemp += sprintf(&buff[buffLenTemp], "FAULT\t%d:\n", 4);
				buffLenTemp += sprintf(&buff[buffLenTemp], "Name\t\t=\t%s\nDegraded\t\t=\t%s\n"
														   "DegradedCount\t=\t%s\nRaised\t\t=\t%s\nRaisedCount\t=\t%s\n",
														   Messtemp.name, Messtemp.Degraded, Messtemp.DegradedCount,
														   Messtemp.Raised, Messtemp.RaisedCount );

				Get_logitem(g_faultNum+1,&Messtemp);
				buffLenTemp += sprintf(&buff[buffLenTemp], "Name\t\t=\t%s\nDegraded\t\t=\t%s\n"
														   "DegradedCount\t=\t%s\nRaised\t\t=\t%s\nRaisedCount\t=\t%s\n",
														   Messtemp.name, Messtemp.Degraded, Messtemp.DegradedCount,
														   Messtemp.Raised, Messtemp.RaisedCount );
				Get_logitem(g_faultNum+2,&Messtemp);
				buffLenTemp += sprintf(&buff[buffLenTemp], "Name\t\t=\t%s\nDegraded\t\t=\t%s\n"
														   "DegradedCount\t=\t%s\nRaised\t\t=\t%s\nRaisedCount\t=\t%s\n",
														   Messtemp.name, Messtemp.Degraded, Messtemp.DegradedCount,
														   Messtemp.Raised, Messtemp.RaisedCount );
				Get_logitem(g_faultNum+3,&Messtemp);
				buffLenTemp += sprintf(&buff[buffLenTemp], "Name\t\t=\t%s\nDegraded\t\t=\t%s\n"
														   "DegradedCount\t=\t%s\nRaised\t\t=\t%s\nRaisedCount\t=\t%s\n",
														   Messtemp.name, Messtemp.Degraded, Messtemp.DegradedCount,
														   Messtemp.Raised, Messtemp.RaisedCount );
				g_bNoAttribute = false;
			}
			else if ((eGet == SOME_ATTR) && (attributes != " "))	// Attributes are provided get:heatermonitor:tempactual
			{
				if (attributes == "NAME")
				{
					FillBuffer_ConcatAttributes("FAULT.%d:  Name\t\t=\t%s", "  Name\t\t=\t%s", true, "Dummy ERROR DMA");
				}
				else if (attributes == "TIMESTAMP")
				{
					FillBuffer_ConcatAttributes("FAULT.%d:  Timestamp\t\t=\t%s", "  Timestamp\t\t=\t%s", true, "10:09:11 2nd July");
				}
				else if (attributes == "DEGRADED")
				{
					FillBuffer_ConcatAttributes("FAULT.%d:  Degraded\t\t=\t%s", "  Degraded\t\t=\t%s", true, "false");
				}
				else if (attributes == "DEGRADEDCOUNT")
				{
					FillBuffer_ConcatAttributes("FAULT.%d:  DegradedCount\t=\t%s", "  DegradedCount\t=\t%s", true, "01");
				}
				else if (attributes == "RAISED")
				{
					FillBuffer_ConcatAttributes("FAULT.%d:  Raised\t\t=\t%s", "  Raised\t\t=\t%s", true, "LOW");
				}
				else if (attributes == "RAISEDCOUNT")
				{
					FillBuffer_ConcatAttributes("FAULT.%d:  RaisedCount\t=\t%s", "  RaisedCount\t=\t%s", true, "0");
				}
				else if (attributes == "DEBOUNCE")
				{
					FillBuffer_ConcatAttributes("FAULT.%d:  Debounce\t\t=\t%s", "  Debounce\t\t=\t%s", true, "01");
				}
				else if (attributes == "FAILCONDITION")
				{
					FillBuffer_ConcatAttributes("FAULT.%d:  FailCondition\t=\t%s", "  FailCondition\t=\t%s", true, "Dummy DMA failed");
				}
				else if (attributes == "DEGRADEDCONDITION")
				{
					FillBuffer_ConcatAttributes("FAULT.%d:  DegradeCondition\t=\t%s", "  DegradeCondition\t=\t%s", true, "Severe");
				}
				else
				{
					cout << "ERROR: Invalid Attribute" << endl;
					PrintResponse("\01INVALID_ATTRIBUTE\04", ERROR_HI_PRIORITY);
					return (-1);
				}
			}
			break;
		}
		default:
		{
			cout << "ERROR: Invalid Object" << endl;
			return (-1);
		}

	}

	b_SendString = true;

	g_moduleNum_prev = g_moduleNum;		// Save the previous g_moduleNum, can help us later in get concatenated command

	return (0);
}

template <typename T> bool CmdDecoder::Sscanf(std::string &str, T &Value, char type)
{
	bool b_error = false;
	int dot_cnt = 0;
	int minux_cnt = 0;
	bool b_NumNotFound = true;

	for (unsigned int i = 0; i < str.length(); i++)
	{
		if (str[i] >= 48 && str[i] <= 57)
		{
			//ok
			b_NumNotFound = false;
		}
		else if((str[i] >= 65 && str[i] <= 90) || (str[i] >= 97 && str[i] <= 122)) //drc added for characters
		{
			b_NumNotFound = false;
		}
		else if ((str[i] == 46 && dot_cnt == 0))		// only if type is double or float
			dot_cnt++;
		else if ((str[i] == 45 && minux_cnt == 0 && i == 0))
			minux_cnt++;
		else
		{
			//error
			b_error = true;
			break;
		}
	}

	if (b_error || b_NumNotFound || (type == 'i' && dot_cnt>0))
	{
		return (false);
	}
	else
	{
		if(type == 'f')
			Value = atof(str.c_str());
		else if(type == 'i')
			Value = atoi(str.c_str());
//		else // 's'
//			Value = str.c_str();
		return (true);
	}
}

template <typename T> void CmdDecoder::FillBuffer_ConcatAttributes(const char* firstTimePrintString , const char* secondTimePrintString, bool b_onlyModuleArg, T arg)
{
	if(g_currentAttributeCount == g_totalAttributes)	// first attribute of single or multiple attribute get:ch.1.3:att or get:ch.1.3:att:adp
	{
		if(b_onlyModuleArg)	// No channel argument in string
		{
			buffLenTemp += sprintf(&buff[buffLenTemp], firstTimePrintString, g_moduleNum, arg);	//Fill this string and get the length filled.
		}
		else
		{
			buffLenTemp += sprintf(&buff[buffLenTemp], firstTimePrintString, g_channelNum, g_moduleNum, arg);	//Fill this string and get the length filled.
		}
	}
	else if (g_currentAttributeCount >= 1)	// multiple attributes after first attribute get:ch.1.3:att:adp
	{
		buffLenTemp += sprintf(&buff[buffLenTemp], secondTimePrintString, arg);	//Fill this string and get the length filled.
	}
	if(g_currentAttributeCount == 1)
	{
//		buffLenTemp += sprintf(&buff[buffLenTemp], "\n");	// \04 delimiter added
	}
}

bool CmdDecoder::TestChannelsNotActive(std::string &Mode, int &ModuleNum)
{
	if(Mode == "TF")
	{
		for (int i = 1; i <= g_Total_Channels; i++)
		{
			if(TF_Channel_DS[ModuleNum][i].active != false)
			{
				return false;	// means some channels are active
			}
		}
	}
	else
	{
		for (int i = 1; i <= g_Total_Channels; i++)
		{
			if(FG_Channel_DS[ModuleNum][i].active != false)
			{
				return false;	// means some channels are active
			}
		}
	}

	return true;
}

int CmdDecoder::is_SetTFDone(void)
{
	g_moduleNum = std::stoi(objVec[1]);
	if (Sscanf(objVec[2], g_channelNum, 'i'))
	{
		//Check if user provided channel number
		if (g_channelNum <= g_Total_Channels)
		{
			eObject = CH_TF;	//Set flag that user is using channels of TF mode.
			if (TF_Channel_DS[g_moduleNum][g_channelNum].active == false)
			{
				// If channel of TF wasn't active, we can't set it.
				cout << "ERROR: The Channel is Inactive-(ADD channel first)" << endl;
				return (-1);
			}
		}
		else
		{
			cout << "ERROR: The Channel numbers are exceeding" << endl;
			return (-1);
		}
	}
	else
	{
		cout << "ERROR: The Channel number is Incorrect" << endl;
		return (-1);
	}

	return (0);
}

int CmdDecoder::is_SetNoSlotFGDone(void)
{
	g_moduleNum = std::stoi(objVec[1]);
	if (Sscanf(objVec[2], g_channelNum, 'i'))
	{
		// (CH.M.N) when N is digits

		if (g_channelNum <= g_Total_Channels)
		{
			if (FG_Channel_DS[g_moduleNum][g_channelNum].active == false)
			{
				cout << "ERROR: The Channel is Inactive-(ADD channel first)" << endl;
				return (-1);
			}
			else
			{
				if (arrModules[g_moduleNum - 1].slotSize == "0")
				{
					// If Fixed Grid module size is not defined then we give error
					cout << "ERROR: The Module Slotsize is not defined" << endl;
					return (-1);
				}
				else
				{
					eObject = CH_FG;
				}

			}
		}
		else
		{
			cout << "ERROR: The Channel numbers are exceeding" << endl;
			return (-1);
		}
	}
	else
	{
		cout << "ERROR: The Channel number is Incorrect" << endl;
		return (-1);
	}

	return (0);
}

int CmdDecoder::is_SetSlotFGDone(void)
{
	g_moduleNum = std::stoi(objVec[1]);
	if (Sscanf(objVec[2], g_channelNum, 'i') && Sscanf(objVec[3], g_slotNum, 'i'))
	{
		// when N.S are digits
		if (g_channelNum <= g_Total_Channels)
		{
			eObject = CH_FG;
			if (FG_Channel_DS[g_moduleNum][g_channelNum].active == false)
			{
				cout << "ERROR: The Channel is Inactive-(ADD channel first)" << endl;
				return (-1);
			}
			else
			{
				if (arrModules[g_moduleNum - 1].slotSize == "0")
				{
					// if fixed grid module size is not defined then we give error
					cout << "ERROR: The Module Slotsize is not defined" << endl;
					return (-1);
				}

				if (g_slotNum <= FG_Channel_DS[g_moduleNum][g_channelNum].slotNum && g_slotNum != 0)
				{
					eObject = CH_FG;
				}
				else
				{
					cout << "ERROR: Slot number is out of Channel's Bandwidth!" << endl;
					return (-1);
				}
			}
		}
		else
		{
			cout << "ERROR: The Channel numbers are exceeding" << endl;
			return (-1);
		}
	}
	else
	{
		cout << "ERROR: The Channel/Slot number is Incorrect" << endl;
		return (-1);
	}

	return (0);
}

int CmdDecoder::is_GetTFDone(void)
{
	g_moduleNum = std::stoi(objVec[1]);
	if (Sscanf(objVec[2], g_channelNum, 'i'))
	{
		//Check if user provided channel number
		if (g_channelNum <= g_Total_Channels)
		{
			if (TF_Channel_DS[g_moduleNum][g_channelNum].active)
			{
				eObject = CH_TF;
				eGet = SOME_ATTR;

				if (g_bNoAttribute == true)
				{
					// if no attribute is given
					eGet = ALL_ATTR_OF_CH;
					std::string chr_ask = "*";
					Print_SearchAttributes(chr_ask);	//send signal that all channel values are needed
				}
			}
			else
			{
				cout << "ERROR: The Channel numbers doesn't exist. Please ADD channel first" << endl;
				return (-1);
			}
		}
		else
		{
			cout << "ERROR: The Channel numbers are exceeding" << endl;
			return (-1);
		}
	}
	else
	{
		// User didn't provide channel numbers
		if (objVec[2] == "*")
		{
			eObject = CH_TF;
			eGet = ALL_CH_ALL_ATTR;	//set a flag that user want to get all channels belong to TrueFlex
			std::string chr_ask = "*";
			Print_SearchAttributes(chr_ask);	//send signal that all channel values are needed
		}
		else
		{
			cout << "ERROR: The Channel number or *not defined" << endl;

			return (-1);
		}
	}

	return (0);
}

int CmdDecoder::is_GetNoSlotFGDone(void)
{
	g_moduleNum = std::stoi(objVec[1]);
	if (Sscanf(objVec[2], g_channelNum, 'i'))
	{
		// when N are digits
		if (g_channelNum <= g_Total_Channels)
		{
			if (arrModules[g_moduleNum - 1].slotSize == "0")  //drc to check
			{
				// if fixed grid module size is not defined then we give error
				cout << "ERROR: The Slot Size is not defined" << endl;
				return (-1);
			}

				if (FG_Channel_DS[g_moduleNum][g_channelNum].active)
				{
					eObject = CH_FG;
					eGet = SOME_ATTR;

					if (g_bNoAttribute == true)
					{
						// if no attribute is given
						eGet = ALL_ATTR_OF_CH;
						std::string chr_ask = "*";
						Print_SearchAttributes(chr_ask);	//send signal that all channel values are needed
					}
				}
				else
				{
					cout << "ERROR: The Channel numbers doesn't exist. Please ADD channel first" << endl;
					return (-1);
				}

		}
		else
		{
			cout << "ERROR: The Channel numbers are exceeding" << endl;
			return (-1);
		}
	}
	else if (objVec[2] == "*")
	{
		// when N = *
		eObject = CH_FG;
		eGet = ALL_CH_ALL_ATTR;	//include all channel info and their slots
		std::string chr_null = " ";
		Print_SearchAttributes(chr_null);
	}
	else
	{
		cout << "ERROR: Invalid Channel Parameter" << endl;
		return (-1);
	}

	return (0);

}

int CmdDecoder::is_GetSlotFGDone(void)
{
	g_moduleNum = std::stoi(objVec[1]);
	if (Sscanf(objVec[2], g_channelNum, 'i') && Sscanf(objVec[3], g_slotNum, 'i'))		//When slot number is give by user,1,2,3...
	{
		// when N.S are digits
		if (g_channelNum <= g_Total_Channels)
		{
			if (arrModules[g_moduleNum - 1].slotSize == "0")
			{
				// if fixed grid module size is not defined then we give error
				cout << "ERROR: The Module Size is not defined" << endl;
				return (-1);
			}

			if (g_slotNum <= FG_Channel_DS[g_moduleNum][g_channelNum].slotNum)
			{
				if (FG_Channel_DS[g_moduleNum][g_channelNum].active)
				{
					eObject = CH_FG;
					eGet = SOME_ATTR;		//default

					if (g_bNoAttribute == true)
					{
						// if no attribute is given
						eGet = ALL_ATTR_OF_CH;		// SINCE slot# is given it means user want all slot attributes, but slot only has ATT
						std::string chr_ask = " ";			// Slot number is give
						Print_SearchAttributes(chr_ask);	//send signal that all channel values are needed
					}
/*					else if (g_bNoAttribute == false && g_totalAttributes > 1)
					{
						cout << "ERROR: The Slot Requires one attribute only" << endl;
						return (-1);
					}
*/
				}
				else
				{
					cout << "ERROR: The Channel numbers doesn't exist. Please ADD channel first" << endl;
					return (-1);
				}
			}
			else
			{
				cout << "ERROR: Slot size is not defined or available slot numbers are exceeding" << endl;
				return (-1);
			}
		}
		else
		{
			cout << "ERROR: The Channel numbers are exceeding" << endl;
			return (-1);
		}
	}
	else if ((Sscanf(objVec[2], g_channelNum, 'i')) && objVec[3] == "*")
	{
		eObject = CH_FG;
		eGet = SOME_ATTR;		//default

		if (g_bNoAttribute == true)
		{
			// if no attribute is given
			eGet = ALL_SLOTS_OF_CH;		// SINCE slot is *asterik user want all slot attenuations in a channel
			std::string chr_ask = " ";			// Slot number is give
			Print_SearchAttributes(chr_ask);	//send signal that all channel values are needed
		}

	}
	else if (objVec[2] == "*")		//
	{
		// when N = *
		eObject = CH_FG;
		eGet = ALL_CH_ALL_ATTR;	//include all channel info EXCEPT SLOTS, slots are subset - user doesnt want subsets
		std::string chr_null = " ";
		Print_SearchAttributes(chr_null);
	}
	else
	{
		cout << "ERROR: Invalid Object Parameter" << endl;
		return (-1);
	}

	return (0);

}

int CmdDecoder::is_AddTFDone(void)
{
	g_moduleNum = std::stoi(objVec[1]);
	if (Sscanf(objVec[2], g_channelNum, 'i'))
	{
		// when N.S are digits
		if (g_channelNum >= 1 && g_channelNum <= g_Total_Channels)
		{
			if (TF_Channel_DS[g_moduleNum][g_channelNum].active == false)
			{
				// Channel doesn't exist yet
				eObject = CH_TF;
				TF_Channel_DS[g_moduleNum][g_channelNum].active = true;
			}
			else
			{
				// Channel already exist, give error to use SET
				cout << "ERROR: The Channel Already Exists; Use 'SET' " << endl;
				return (-1);
			}
		}
		else
		{
			cout << "ERROR: The Channel number is out-of-range" << endl;
			return (-1);
		}
		activeChannels.push_back({g_moduleNum, g_channelNum});
	}
	else
	{
		cout << "ERROR: The Channel numbers is not numeric" << endl;
		return (-1);
	}


	return (0);
}

int CmdDecoder::is_AddFGDone(void)
{
	g_moduleNum = std::stoi(objVec[1]);
	if (Sscanf(objVec[2], g_channelNum, 'i'))
	{
		// when N.S are digits
		if (g_channelNum >= 1 && g_channelNum <= g_Total_Channels)
		{
			if (FG_Channel_DS[g_moduleNum][g_channelNum].active == false)
			{
				// Channel doesn't exist yet
				eObject = CH_FG;
				FG_Channel_DS[g_moduleNum][g_channelNum].active = true;
			}
			else
			{
				// Channel already exist, give error to use SET
				cout << "ERROR: The Channel Already Exists; Use 'SET' " << endl;
				return (-1);
			}
			activeChannels.push_back({g_moduleNum, g_channelNum});
		}
		else
		{
			cout << "ERROR: The Channel numbers are exceeding" << endl;
			return (-1);
		}
	}
	else
	{
		cout << "ERROR: The Channel numbers is not numeric" << endl;
		return (-1);
	}


	return (0);
}

int CmdDecoder::is_DeleteTFDone(std::string &moduleNum, std::string &chNum)
{
	g_moduleNum = std::stoi(moduleNum);
	if (Sscanf(chNum, g_channelNum, 'i'))
	{
		// when N.S are digits
		if (g_channelNum >= 1 && g_channelNum <= g_Total_Channels)
		{
			eObject = CH_TF;
			if(TF_Channel_DS[g_moduleNum][g_channelNum].active == true)
			{
				TF_Channel_DS[g_moduleNum][g_channelNum].active = false;	// Channel is deleted or inactive

				// Reset elements
				TF_Channel_DS[g_moduleNum][g_channelNum].ADP = 0;
				TF_Channel_DS[g_moduleNum][g_channelNum].ATT = 35;  //drc set default 35 according to L
				TF_Channel_DS[g_moduleNum][g_channelNum].CMP = 1;  //drc modified
				TF_Channel_DS[g_moduleNum][g_channelNum].FC = 0;
				TF_Channel_DS[g_moduleNum][g_channelNum].BW = 0;
				TF_Channel_DS[g_moduleNum][g_channelNum].F1ContiguousOrNot = 0;
				TF_Channel_DS[g_moduleNum][g_channelNum].F2ContiguousOrNot = 0;

#ifdef _DEVELOPMENT_MODE_
				TF_Channel_DS[g_moduleNum][g_channelNum].LAMDA =0;
				TF_Channel_DS[g_moduleNum][g_channelNum].SIGMA =0;
				TF_Channel_DS[g_moduleNum][g_channelNum].K_OPP =0;
				TF_Channel_DS[g_moduleNum][g_channelNum].A_OPP =0;
				TF_Channel_DS[g_moduleNum][g_channelNum].K_ATT =0;
				TF_Channel_DS[g_moduleNum][g_channelNum].A_ATT =0;
				TF_Channel_DS[g_moduleNum][g_channelNum].b_ColorSet = false;
				TF_Channel_DS[g_moduleNum][g_channelNum].COLOR = 0;
#endif
				int targetChan = g_channelNum;
				int targetMod = g_moduleNum;
				activeChannels.remove_if([targetMod, targetChan](const ChannelModules& point) {
			        return point.moduleNo == targetMod && point.channelNo == targetChan;
			    });
			}
			else
			{
				cout << "ERROR: The Channel numbers is not active" << endl;
				return (-1);
			}

		}
		else
		{
			cout << "ERROR: The Channel numbers are exceeding" << endl;
			return (-1);
		}
	}
	else if (chNum == "*")
	{
		// when N = *	// delete all channels
		eObject = CH_TF;
		for (int i = 1; i <= g_Total_Channels; i++)
		{
			if(TF_Channel_DS[g_moduleNum][i].active == true)
			{

				TF_Channel_DS[g_moduleNum][i].active = false;	// Channel is deleted or inactive
	// Reset elements
				TF_Channel_DS[g_moduleNum][i].ADP = 0;
				TF_Channel_DS[g_moduleNum][i].ATT = 35;   //drc set to default according to L
				TF_Channel_DS[g_moduleNum][i].CMP = 1;
				TF_Channel_DS[g_moduleNum][i].FC = 0;
				TF_Channel_DS[g_moduleNum][i].BW = 0;
				TF_Channel_DS[g_moduleNum][i].F1ContiguousOrNot = 0;
				TF_Channel_DS[g_moduleNum][i].F2ContiguousOrNot = 0;

#ifdef _DEVELOPMENT_MODE_
				TF_Channel_DS[g_moduleNum][i].LAMDA =0;
				TF_Channel_DS[g_moduleNum][i].SIGMA =0;
				TF_Channel_DS[g_moduleNum][i].K_OPP =0;
				TF_Channel_DS[g_moduleNum][i].A_OPP =0;
				TF_Channel_DS[g_moduleNum][i].K_ATT =0;
				TF_Channel_DS[g_moduleNum][i].A_ATT =0;
				TF_Channel_DS[g_moduleNum][i].b_ColorSet = false;
				TF_Channel_DS[g_moduleNum][i].COLOR = 0;

#endif

				int targetChan = i;
				int targetMod = g_moduleNum;
				activeChannels.remove_if([targetMod, targetChan](const ChannelModules& point) {
					return point.moduleNo == targetMod && point.channelNo == targetChan;
				});
			}


		}
	}

	else
	{
		cout << "ERROR: The Channel numbers is not numeric" << endl;
		return (-1);
	}

	return (0);
}

int CmdDecoder::is_DeleteFGDone(std::string &moduleNum, std::string &chNum)
{
	g_moduleNum = std::stoi(moduleNum);
	if (Sscanf(chNum, g_channelNum, 'i'))
	{
		// when N.S are digits
		if (g_channelNum >= 1 && g_channelNum <= g_Total_Channels)
		{
			eObject = CH_FG;

			if(FG_Channel_DS[g_moduleNum][g_channelNum].active == true)
			{
				FG_Channel_DS[g_moduleNum][g_channelNum].active = false;	// Channel is deleted or inactive

				// Reset elements
				FG_Channel_DS[g_moduleNum][g_channelNum].ADP = 0;
				FG_Channel_DS[g_moduleNum][g_channelNum].ATT = 35;
				FG_Channel_DS[g_moduleNum][g_channelNum].CMP = 1;  //default CMP=1
				FG_Channel_DS[g_moduleNum][g_channelNum].F1 = 0;
				FG_Channel_DS[g_moduleNum][g_channelNum].F2 = 0;
				FG_Channel_DS[g_moduleNum][g_channelNum].slotNum = 0;
				FG_Channel_DS[g_moduleNum][g_channelNum].slotsATTEN.clear();

				FG_Channel_DS[g_moduleNum][g_channelNum].F1ContiguousOrNot = 0;
				FG_Channel_DS[g_moduleNum][g_channelNum].F2ContiguousOrNot = 0;

#ifdef _DEVELOPMENT_MODE_
				FG_Channel_DS[g_moduleNum][g_channelNum].LAMDA =0;
				FG_Channel_DS[g_moduleNum][g_channelNum].SIGMA =0;
				FG_Channel_DS[g_moduleNum][g_channelNum].K_OPP =0;
				FG_Channel_DS[g_moduleNum][g_channelNum].A_OPP =0;
				FG_Channel_DS[g_moduleNum][g_channelNum].K_ATT =0;
				FG_Channel_DS[g_moduleNum][g_channelNum].A_ATT =0;
				FG_Channel_DS[g_moduleNum][g_channelNum].b_ColorSet = false;
				FG_Channel_DS[g_moduleNum][g_channelNum].COLOR = 0;
				FG_Channel_DS[g_moduleNum][g_channelNum].n_ch_1 = 0;
				FG_Channel_DS[g_moduleNum][g_channelNum].n_ch_2 = 0;

#endif

				int targetChan = g_channelNum;
				int targetMod = g_moduleNum;
				activeChannels.remove_if([targetMod, targetChan](const ChannelModules& point) {
					return point.moduleNo == targetMod && point.channelNo == targetChan;
				});
			}
			else
			{
				cout << "ERROR: The Channel numbers is not active" << endl;
				return (-1);
			}


		}
		else
		{
			cout << "ERROR: The Channel numbers are exceeding" << endl;
			return (-1);
		}
	}
	else if (chNum == "*")
	{
		// when N = *	// delete all channels
		eObject = CH_FG;
		for (int i = 1; i <= g_Total_Channels; i++)
		{

			if(FG_Channel_DS[g_moduleNum][i].active == true)
			{

				FG_Channel_DS[g_moduleNum][i].active = false;	// Channel is deleted or inactive

				// Reset elements
				FG_Channel_DS[g_moduleNum][i].ADP = 0;
				FG_Channel_DS[g_moduleNum][i].ATT = 35;
				FG_Channel_DS[g_moduleNum][i].CMP = 1;
				FG_Channel_DS[g_moduleNum][i].F1 = 0;
				FG_Channel_DS[g_moduleNum][i].F2 = 0;
				FG_Channel_DS[g_moduleNum][i].slotNum = 0;
				FG_Channel_DS[g_moduleNum][i].slotsATTEN.clear();

				FG_Channel_DS[g_moduleNum][i].F1ContiguousOrNot = 0;
				FG_Channel_DS[g_moduleNum][i].F2ContiguousOrNot = 0;
#ifdef _DEVELOPMENT_MODE_
				FG_Channel_DS[g_moduleNum][i].LAMDA =0;
				FG_Channel_DS[g_moduleNum][i].SIGMA =0;
				FG_Channel_DS[g_moduleNum][i].K_OPP =0;
				FG_Channel_DS[g_moduleNum][i].A_OPP =0;
				FG_Channel_DS[g_moduleNum][i].K_ATT =0;
				FG_Channel_DS[g_moduleNum][i].A_ATT =0;
				FG_Channel_DS[g_moduleNum][i].b_ColorSet = false;
				FG_Channel_DS[g_moduleNum][i].COLOR = 0;
				FG_Channel_DS[g_moduleNum][i].n_ch_1 = 0;
				FG_Channel_DS[g_moduleNum][i].n_ch_2 = 0;

#endif
				int targetChan = i;
				int targetMod = g_moduleNum;
				activeChannels.remove_if([targetMod, targetChan](const ChannelModules& point) {
					return point.moduleNo == targetMod && point.channelNo == targetChan;
				});
			}

		}
	}
	else
	{
		cout << "ERROR: The Channel numbers is not numeric" << endl;
		return (-1);
	}

	return (0);
}

void CmdDecoder::SetPanelInfo(bool flag)
{
	if (pthread_mutex_lock(&global_mutex[LOCK_MODULE_DS]) != 0)
		std::cout << "global_mutex[LOCK_MODULE_DS] lock unsuccessful" << std::endl;
	else
	{
		panelInfo.readyFlag = flag;

		if (pthread_mutex_unlock(&global_mutex[LOCK_MODULE_DS]) != 0)
			std::cout << "global_mutex[LOCK_MODULE_DS] unlock unsuccessful" << std::endl;
	}
}

void CmdDecoder::SetPanelInfo(Panel& panel)
{
	if (pthread_mutex_lock(&global_mutex[LOCK_MODULE_DS]) != 0)
		std::cout << "global_mutex[LOCK_MODULE_DS] lock unsuccessful" << std::endl;
	else
	{
		panelInfo = panel;

		if (pthread_mutex_unlock(&global_mutex[LOCK_MODULE_DS]) != 0)
			std::cout << "global_mutex[LOCK_MODULE_DS] unlock unsuccessful" << std::endl;
	}
}

Panel CmdDecoder::GetPanelInfo()
{
	Panel p;

	if (pthread_mutex_lock(&global_mutex[LOCK_MODULE_DS]) != 0)
		cout << "global_mutex[LOCK_MODULE_DS] lock unsuccessful" << std::endl;
	else
	{
		p = panelInfo;

		if (pthread_mutex_unlock(&global_mutex[LOCK_MODULE_DS]) != 0)
			cout << "global_mutex[LOCK_MODULE_DS] unlock unsuccessful" << std::endl;
	}
	return p;
}


//drc added for store to and restore from channel config data files
bool CmdDecoder::ActionVrb::StoreModule(int moduleNum)
{

	if(outerRef.arrModules[moduleNum-1].slotSize == "TF")
	{
		std::copy(&outerRef.TF_Channel_DS_For_Pattern[moduleNum][0], &outerRef.TF_Channel_DS_For_Pattern[moduleNum][0]+g_Total_Channels, &TF_Channel_DS_For_Save[moduleNum][0]);
	}
	else
	{
		std::copy(&outerRef.FG_Channel_DS_For_Pattern[moduleNum][0], &outerRef.FG_Channel_DS_For_Pattern[moduleNum][0]+g_Total_Channels, &FG_Channel_DS_For_Save[moduleNum][0]);
	}

	if(moduleNum == 2)
	{
		std::ofstream outfile("/mnt/SavedModule2ConfigInfo.cfg", std::ios::trunc | std::ios::binary);
		if(outfile.is_open())
		{
			// Write the struct data into the file
			//write arrModules info first
			int strLen = outerRef.arrModules[moduleNum-1].slotSize.size();
			outfile.write(reinterpret_cast<char*>(&strLen), sizeof(strLen));
			outfile.write(outerRef.arrModules[moduleNum-1].slotSize.c_str(), strLen);

			if(outerRef.arrModules[moduleNum-1].slotSize == "TF")
			{
				outfile.write(reinterpret_cast<char*>(&TF_Channel_DS_For_Save[moduleNum][0]), sizeof(TrueFlex)*g_Total_Channels);
			}
			else if (outerRef.arrModules[moduleNum-1].slotSize == "625")
			{
				for(int i = 0; i < g_Total_Channels;i++)
				{
					outfile.write(reinterpret_cast<char*>(&FG_Channel_DS_For_Save[moduleNum][i].active), sizeof(FG_Channel_DS_For_Save[moduleNum][i].active));
					outfile.write(reinterpret_cast<char*>(&FG_Channel_DS_For_Save[moduleNum][i].ADP), sizeof(FG_Channel_DS_For_Save[moduleNum][i].ADP));
					outfile.write(reinterpret_cast<char*>(&FG_Channel_DS_For_Save[moduleNum][i].ATT), sizeof(FG_Channel_DS_For_Save[moduleNum][i].ATT));
					outfile.write(reinterpret_cast<char*>(&FG_Channel_DS_For_Save[moduleNum][i].CMP), sizeof(FG_Channel_DS_For_Save[moduleNum][i].CMP));
					outfile.write(reinterpret_cast<char*>(&FG_Channel_DS_For_Save[moduleNum][i].F1), sizeof(FG_Channel_DS_For_Save[moduleNum][i].F1));
					outfile.write(reinterpret_cast<char*>(&FG_Channel_DS_For_Save[moduleNum][i].F2), sizeof(FG_Channel_DS_For_Save[moduleNum][i].F2));
					outfile.write(reinterpret_cast<char*>(&FG_Channel_DS_For_Save[moduleNum][i].FC), sizeof(FG_Channel_DS_For_Save[moduleNum][i].FC));
					outfile.write(reinterpret_cast<char*>(&FG_Channel_DS_For_Save[moduleNum][i].BW), sizeof(FG_Channel_DS_For_Save[moduleNum][i].BW));
					outfile.write(reinterpret_cast<char*>(&FG_Channel_DS_For_Save[moduleNum][i].slotNum), sizeof(FG_Channel_DS_For_Save[moduleNum][i].slotNum));
					outfile.write(reinterpret_cast<char*>(&FG_Channel_DS_For_Save[moduleNum][i].F1ContiguousOrNot), sizeof(FG_Channel_DS_For_Save[moduleNum][i].F1ContiguousOrNot));
					outfile.write(reinterpret_cast<char*>(&FG_Channel_DS_For_Save[moduleNum][i].F2ContiguousOrNot), sizeof(FG_Channel_DS_For_Save[moduleNum][i].F2ContiguousOrNot));

					int vecLen = FG_Channel_DS_For_Save[moduleNum][i].slotsATTEN.size();
					outfile.write(reinterpret_cast<char*>(&vecLen), sizeof(vecLen));
					outfile.write(reinterpret_cast<char*>(FG_Channel_DS_For_Save[moduleNum][i].slotsATTEN.data()), sizeof(float)*vecLen);

					vecLen = FG_Channel_DS_For_Save[moduleNum][i].slotBlockedOrNot.size();
					outfile.write(reinterpret_cast<char*>(&vecLen), sizeof(vecLen));
					outfile.write(reinterpret_cast<char*>(FG_Channel_DS_For_Save[moduleNum][i].slotBlockedOrNot.data()), sizeof(int)*vecLen);

				}
			}
			else
			{
				std::cout << "Wrong Slotsize" << std::endl;
				outfile.close();
				return false;
			}
			for (auto& ch : outerRef.activeChannels)
			{
				if(ch.moduleNo == moduleNum)
					outfile.write(reinterpret_cast<char*>(&ch), sizeof(ChannelModules));
			}
			// Close the file
			outfile.close();
			bModuleConfigStored[moduleNum] = true;
		}
		else
		{
			std::cerr << "Error opening the file SavedModule2ConfigInfo.cfg for writing." << std::endl;
			return false;
		}
	}
	else   //Module 1
	{
		std::ofstream outfile("/mnt/SavedModule1ConfigInfo.cfg", std::ios::trunc | std::ios::binary);
		if(outfile.is_open())
		{
			// Write the struct data into the file
			//arrModules info first
			int strLen = outerRef.arrModules[moduleNum-1].slotSize.size();
			outfile.write(reinterpret_cast<char*>(&strLen), sizeof(strLen));
			outfile.write(outerRef.arrModules[moduleNum-1].slotSize.c_str(), strLen);

			if(outerRef.arrModules[moduleNum-1].slotSize == "TF")
			{
				outfile.write(reinterpret_cast<char*>(&TF_Channel_DS_For_Save[moduleNum][0]), sizeof(TrueFlex)*g_Total_Channels);
			}
			else if(outerRef.arrModules[moduleNum-1].slotSize == "625")
			{
				for(int i = 0; i < g_Total_Channels;i++)
				{

					outfile.write(reinterpret_cast<char*>(&FG_Channel_DS_For_Save[moduleNum][i].active), sizeof(FG_Channel_DS_For_Save[moduleNum][i].active));
					outfile.write(reinterpret_cast<char*>(&FG_Channel_DS_For_Save[moduleNum][i].ADP), sizeof(FG_Channel_DS_For_Save[moduleNum][i].ADP));
					outfile.write(reinterpret_cast<char*>(&FG_Channel_DS_For_Save[moduleNum][i].ATT), sizeof(FG_Channel_DS_For_Save[moduleNum][i].ATT));
					outfile.write(reinterpret_cast<char*>(&FG_Channel_DS_For_Save[moduleNum][i].CMP), sizeof(FG_Channel_DS_For_Save[moduleNum][i].CMP));
					outfile.write(reinterpret_cast<char*>(&FG_Channel_DS_For_Save[moduleNum][i].F1), sizeof(FG_Channel_DS_For_Save[moduleNum][i].F1));
					outfile.write(reinterpret_cast<char*>(&FG_Channel_DS_For_Save[moduleNum][i].F2), sizeof(FG_Channel_DS_For_Save[moduleNum][i].F2));
					outfile.write(reinterpret_cast<char*>(&FG_Channel_DS_For_Save[moduleNum][i].FC), sizeof(FG_Channel_DS_For_Save[moduleNum][i].FC));
					outfile.write(reinterpret_cast<char*>(&FG_Channel_DS_For_Save[moduleNum][i].BW), sizeof(FG_Channel_DS_For_Save[moduleNum][i].BW));
					outfile.write(reinterpret_cast<char*>(&FG_Channel_DS_For_Save[moduleNum][i].slotNum), sizeof(FG_Channel_DS_For_Save[moduleNum][i].slotNum));
					outfile.write(reinterpret_cast<char*>(&FG_Channel_DS_For_Save[moduleNum][i].F1ContiguousOrNot), sizeof(FG_Channel_DS_For_Save[moduleNum][i].F1ContiguousOrNot));
					outfile.write(reinterpret_cast<char*>(&FG_Channel_DS_For_Save[moduleNum][i].F2ContiguousOrNot), sizeof(FG_Channel_DS_For_Save[moduleNum][i].F2ContiguousOrNot));

					int vecLen = FG_Channel_DS_For_Save[moduleNum][i].slotsATTEN.size();
					outfile.write(reinterpret_cast<char*>(&vecLen), sizeof(vecLen));
					outfile.write(reinterpret_cast<char*>(FG_Channel_DS_For_Save[moduleNum][i].slotsATTEN.data()), sizeof(float)*vecLen);

					vecLen = FG_Channel_DS_For_Save[moduleNum][i].slotBlockedOrNot.size();
					outfile.write(reinterpret_cast<char*>(&vecLen), sizeof(vecLen));
					outfile.write(reinterpret_cast<char*>(FG_Channel_DS_For_Save[moduleNum][i].slotBlockedOrNot.data()), sizeof(int)*vecLen);
				}
			}
			else
			{
				std::cout << "Wrong Slotsize" << std::endl;
				outfile.close();
				return false;
			}
			for (auto& ch : outerRef.activeChannels)
			{
				if(ch.moduleNo == moduleNum)
					outfile.write(reinterpret_cast<char*>(&ch), sizeof(ChannelModules));
			}
			// Close the file
			outfile.close();
			bModuleConfigStored[moduleNum] = true;
		}
		else
		{
			std::cerr << "Error opening the file SavedModule1ConfigInfo.cfg for writing." << std::endl;
			return false;
		}
	}

	return true;
}

bool CmdDecoder::ActionVrb::RestoreModule(int moduleNum)
{

	if(moduleNum == 2)
	{
		std::ifstream infile("/mnt/SavedModule2ConfigInfo.cfg", std::ios::binary);
		if (infile.is_open())
		{
			/*arrModules info first*/
			int strLen = 0;
			infile.read(reinterpret_cast<char*>(&strLen), sizeof(strLen));
			outerRef.arrModules[moduleNum-1].slotSize.resize(strLen);
			infile.read(reinterpret_cast<char*>(&outerRef.arrModules[moduleNum-1].slotSize[0]), strLen);


			std::cout << "Slotsize:" << outerRef.arrModules[moduleNum-1].slotSize <<std::endl;

			if(outerRef.arrModules[moduleNum-1].slotSize == "TF")
			{
				outerRef.eModule2 = TF;
				for(int i = 0; i < g_Total_Channels;i++)
				{
					infile.read(reinterpret_cast<char*>(&TF_Channel_DS_For_Save[moduleNum][i]), sizeof(TrueFlex));
				}
				//delete old channel in the list first
				outerRef.activeChannels.remove_if([moduleNum](const ChannelModules& point){return point.moduleNo == moduleNum;});
				ChannelModules temp;
				while (infile.read(reinterpret_cast<char*>(&temp), sizeof(ChannelModules)))
				{ // Read each channel from binary
					outerRef.activeChannels.push_back(temp); // Then add the read channel into the list
				}
				if (pthread_mutex_lock(&global_mutex[LOCK_CHANNEL_DS]) != 0)
						std::cout << "global_mutex[LOCK_CHANNEL_DS] lock unsuccessful" << std::endl;
				else
				{
					std::copy(&TF_Channel_DS_For_Save[moduleNum][0], &TF_Channel_DS_For_Save[moduleNum][0]+g_Total_Channels, &outerRef.TF_Channel_DS_For_Pattern[moduleNum][0]);
					std::copy(&TF_Channel_DS_For_Save[moduleNum][0], &TF_Channel_DS_For_Save[moduleNum][0]+g_Total_Channels, &outerRef.TF_Channel_DS[moduleNum][0]);
					g_bNewCommandData = true;
				}
				if (pthread_mutex_unlock(&global_mutex[LOCK_CHANNEL_DS]) != 0)
						std::cout << "global_mutex[LOCK_CHANNEL_DS] unlock unsuccessful" << std::endl;
			}
			else if(outerRef.arrModules[moduleNum-1].slotSize == "625")  // FG channels
			{
				outerRef.eModule2 = FIXEDGRID;
				for(int i = 0; i < g_Total_Channels;i++)
				{
					infile.read(reinterpret_cast<char*>(&FG_Channel_DS_For_Save[moduleNum][i].active), sizeof(FG_Channel_DS_For_Save[moduleNum][i].active));
					infile.read(reinterpret_cast<char*>(&FG_Channel_DS_For_Save[moduleNum][i].ADP), sizeof(FG_Channel_DS_For_Save[moduleNum][i].ADP));
					infile.read(reinterpret_cast<char*>(&FG_Channel_DS_For_Save[moduleNum][i].ATT), sizeof(FG_Channel_DS_For_Save[moduleNum][i].ATT));
					infile.read(reinterpret_cast<char*>(&FG_Channel_DS_For_Save[moduleNum][i].CMP), sizeof(FG_Channel_DS_For_Save[moduleNum][i].CMP));
					infile.read(reinterpret_cast<char*>(&FG_Channel_DS_For_Save[moduleNum][i].F1), sizeof(FG_Channel_DS_For_Save[moduleNum][i].F1));
					infile.read(reinterpret_cast<char*>(&FG_Channel_DS_For_Save[moduleNum][i].F2), sizeof(FG_Channel_DS_For_Save[moduleNum][i].F2));
					infile.read(reinterpret_cast<char*>(&FG_Channel_DS_For_Save[moduleNum][i].FC), sizeof(FG_Channel_DS_For_Save[moduleNum][i].FC));
					infile.read(reinterpret_cast<char*>(&FG_Channel_DS_For_Save[moduleNum][i].BW), sizeof(FG_Channel_DS_For_Save[moduleNum][i].BW));
					infile.read(reinterpret_cast<char*>(&FG_Channel_DS_For_Save[moduleNum][i].slotNum), sizeof(FG_Channel_DS_For_Save[moduleNum][i].slotNum));
					infile.read(reinterpret_cast<char*>(&FG_Channel_DS_For_Save[moduleNum][i].F1ContiguousOrNot), sizeof(FG_Channel_DS_For_Save[moduleNum][i].F1ContiguousOrNot));
					infile.read(reinterpret_cast<char*>(&FG_Channel_DS_For_Save[moduleNum][i].F2ContiguousOrNot), sizeof(FG_Channel_DS_For_Save[moduleNum][i].F2ContiguousOrNot));

					int vecLen = 0;
					infile.read(reinterpret_cast<char*>(&vecLen), sizeof(vecLen));
					FG_Channel_DS_For_Save[moduleNum][i].slotsATTEN.resize(vecLen);
					infile.read(reinterpret_cast<char*>(FG_Channel_DS_For_Save[moduleNum][i].slotsATTEN.data()), sizeof(float)*vecLen);


					vecLen = 0;
					infile.read(reinterpret_cast<char*>(&vecLen), sizeof(vecLen));
					FG_Channel_DS_For_Save[moduleNum][i].slotBlockedOrNot.resize(vecLen);
					infile.read(reinterpret_cast<char*>(FG_Channel_DS_For_Save[moduleNum][i].slotBlockedOrNot.data()), sizeof(int)*vecLen);
				}

				 //delete old channel in the list first
				outerRef.activeChannels.remove_if([moduleNum](const ChannelModules& point){return point.moduleNo == moduleNum;});
				ChannelModules temp;
				while (infile.read(reinterpret_cast<char*>(&temp), sizeof(ChannelModules)))
				{ // Read each channel from binary
					outerRef.activeChannels.push_back(temp); // Then add the read channel into the list
				}

				if (pthread_mutex_lock(&global_mutex[LOCK_CHANNEL_DS]) != 0)
						std::cout << "global_mutex[LOCK_CHANNEL_DS] lock unsuccessful" << std::endl;
				else
				{
					std::copy(&FG_Channel_DS_For_Save[moduleNum][0], &FG_Channel_DS_For_Save[moduleNum][0]+g_Total_Channels, &outerRef.FG_Channel_DS_For_Pattern[moduleNum][0]);
					std::copy(&FG_Channel_DS_For_Save[moduleNum][0], &FG_Channel_DS_For_Save[moduleNum][0]+g_Total_Channels, &outerRef.FG_Channel_DS[moduleNum][0]);
					g_bNewCommandData = true;
				}
				if (pthread_mutex_unlock(&global_mutex[LOCK_CHANNEL_DS]) != 0)
					std::cout << "global_mutex[LOCK_CHANNEL_DS] unlock unsuccessful" << std::endl;
			}
			else
			{
				std::cerr << "Error Slotsize." << std::endl;
				infile.close();
				return false;
			}
			infile.close();
		}
		else
		{
			std::cerr << "Error opening the SavedModule2Config file for reading." << std::endl;
			return false;
		}
	}
	else  //module 1
	{
		std::ifstream infile("/mnt/SavedModule1ConfigInfo.cfg", std::ios::binary);
		if (infile.is_open())
		{
			/*Read arrModules info first*/
			int strLen = 0;
			infile.read(reinterpret_cast<char*>(&strLen), sizeof(strLen));
			outerRef.arrModules[moduleNum-1].slotSize.resize(strLen);
			infile.read(reinterpret_cast<char*>(&outerRef.arrModules[moduleNum-1].slotSize[0]), strLen);

			std::cout << "Slotsize:" << outerRef.arrModules[moduleNum-1].slotSize <<std::endl;
			if(outerRef.arrModules[moduleNum-1].slotSize == "TF")
			{
				outerRef.eModule1 = TF;
				for(int i = 0; i < g_Total_Channels;i++)
				{
					infile.read(reinterpret_cast<char*>(&TF_Channel_DS_For_Save[moduleNum][i]), sizeof(TrueFlex));
				}

				//delete old channel in the list first
				outerRef.activeChannels.remove_if([moduleNum](const ChannelModules& point){return point.moduleNo == moduleNum;});
				ChannelModules temp;
				while (infile.read(reinterpret_cast<char*>(&temp), sizeof(ChannelModules)))
				{ // Read each channel from binary
					outerRef.activeChannels.push_back(temp); // Then add the read channel into the list
				}

				if (pthread_mutex_lock(&global_mutex[LOCK_CHANNEL_DS]) != 0)
					std::cout << "global_mutex[LOCK_CHANNEL_DS] lock unsuccessful" << std::endl;
				else
				{
					std::copy(&TF_Channel_DS_For_Save[moduleNum][0], &TF_Channel_DS_For_Save[moduleNum][0]+g_Total_Channels, &outerRef.TF_Channel_DS_For_Pattern[moduleNum][0]);
					std::copy(&TF_Channel_DS_For_Save[moduleNum][0], &TF_Channel_DS_For_Save[moduleNum][0]+g_Total_Channels, &outerRef.TF_Channel_DS[moduleNum][0]);
					g_bNewCommandData = true;
				}
				if (pthread_mutex_unlock(&global_mutex[LOCK_CHANNEL_DS]) != 0)
					std::cout << "global_mutex[LOCK_CHANNEL_DS] unlock unsuccessful" << std::endl;
			}
			else if(outerRef.arrModules[moduleNum-1].slotSize == "625")
			{
				outerRef.eModule1 = FIXEDGRID;
				for(int i = 0; i < g_Total_Channels;i++)
				{
					infile.read(reinterpret_cast<char*>(&FG_Channel_DS_For_Save[moduleNum][i].active), sizeof(FG_Channel_DS_For_Save[moduleNum][i].active));
					infile.read(reinterpret_cast<char*>(&FG_Channel_DS_For_Save[moduleNum][i].ADP), sizeof(FG_Channel_DS_For_Save[moduleNum][i].ADP));
					infile.read(reinterpret_cast<char*>(&FG_Channel_DS_For_Save[moduleNum][i].ATT), sizeof(FG_Channel_DS_For_Save[moduleNum][i].ATT));
					infile.read(reinterpret_cast<char*>(&FG_Channel_DS_For_Save[moduleNum][i].CMP), sizeof(FG_Channel_DS_For_Save[moduleNum][i].CMP));
					infile.read(reinterpret_cast<char*>(&FG_Channel_DS_For_Save[moduleNum][i].F1), sizeof(FG_Channel_DS_For_Save[moduleNum][i].F1));
					infile.read(reinterpret_cast<char*>(&FG_Channel_DS_For_Save[moduleNum][i].F2), sizeof(FG_Channel_DS_For_Save[moduleNum][i].F2));
					infile.read(reinterpret_cast<char*>(&FG_Channel_DS_For_Save[moduleNum][i].FC), sizeof(FG_Channel_DS_For_Save[moduleNum][i].FC));
					infile.read(reinterpret_cast<char*>(&FG_Channel_DS_For_Save[moduleNum][i].BW), sizeof(FG_Channel_DS_For_Save[moduleNum][i].BW));
					infile.read(reinterpret_cast<char*>(&FG_Channel_DS_For_Save[moduleNum][i].slotNum), sizeof(FG_Channel_DS_For_Save[moduleNum][i].slotNum));
					infile.read(reinterpret_cast<char*>(&FG_Channel_DS_For_Save[moduleNum][i].F1ContiguousOrNot), sizeof(FG_Channel_DS_For_Save[moduleNum][i].F1ContiguousOrNot));
					infile.read(reinterpret_cast<char*>(&FG_Channel_DS_For_Save[moduleNum][i].F2ContiguousOrNot), sizeof(FG_Channel_DS_For_Save[moduleNum][i].F2ContiguousOrNot));

					int vecLen = 0;
					infile.read(reinterpret_cast<char*>(&vecLen), sizeof(vecLen));
					FG_Channel_DS_For_Save[moduleNum][i].slotsATTEN.resize(vecLen);
					infile.read(reinterpret_cast<char*>(FG_Channel_DS_For_Save[moduleNum][i].slotsATTEN.data()), sizeof(float)*vecLen);


					vecLen = 0;
					infile.read(reinterpret_cast<char*>(&vecLen), sizeof(vecLen));
					FG_Channel_DS_For_Save[moduleNum][i].slotBlockedOrNot.resize(vecLen);
					infile.read(reinterpret_cast<char*>(FG_Channel_DS_For_Save[moduleNum][i].slotBlockedOrNot.data()), sizeof(int)*vecLen);
				}

				 //delete old channel in the list first
				outerRef.activeChannels.remove_if([moduleNum](const ChannelModules& point){return point.moduleNo == moduleNum;});
				ChannelModules temp;
				while (infile.read(reinterpret_cast<char*>(&temp), sizeof(ChannelModules)))
				{ // Read each channel from binary
					outerRef.activeChannels.push_back(temp); // Then add the read channel into the list
				}

				if (pthread_mutex_lock(&global_mutex[LOCK_CHANNEL_DS]) != 0)
						std::cout << "global_mutex[LOCK_CHANNEL_DS] lock unsuccessful" << std::endl;
				else
				{
					outerRef.FG_Channel_DS_For_Pattern[moduleNum][0].slotBlockedOrNot.resize(160);
					outerRef.FG_Channel_DS_For_Pattern[moduleNum][0].slotsATTEN.resize(160);
					std::copy(&FG_Channel_DS_For_Save[moduleNum][0], &FG_Channel_DS_For_Save[moduleNum][0]+g_Total_Channels, &outerRef.FG_Channel_DS_For_Pattern[moduleNum][0]);
					std::copy(&FG_Channel_DS_For_Save[moduleNum][0], &FG_Channel_DS_For_Save[moduleNum][0]+g_Total_Channels, &outerRef.FG_Channel_DS[moduleNum][0]);
					g_bNewCommandData = true;
				}
				if (pthread_mutex_unlock(&global_mutex[LOCK_CHANNEL_DS]) != 0)
						std::cout << "global_mutex[LOCK_CHANNEL_DS] unlock unsuccessful" << std::endl;
			}
			else
			{
				std::cerr << "Error Slotsize." << std::endl;
				infile.close();
				return false;
			}
			infile.close();
		}
		else
		{
			std::cerr << "Error opening the SavedModule1Config file for reading." << std::endl;
			return false;
		}
	}

//	    std::cout << "fc: " << TF_Channel_DS_For_Save[moduleNum][1].FC << ", bw: " << TF_Channel_DS_For_Save[moduleNum][1].BW << std::endl;

	return true;
}





