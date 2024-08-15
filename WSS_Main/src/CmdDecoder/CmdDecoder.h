/*
 * CmdDecoder.h
 *
 *  Created on: Jan 19, 2023
 *      Author: mib_n
 */

#ifndef SRC_CMDDECODER_CMDDECODER_H_
#define SRC_CMDDECODER_CMDDECODER_H_

#include "GlobalVariables.h"
#include "DataStructures.h"		//All structures defined here
#include <vector>
#include <list>
#include <string>
#include <iostream>
#include <sstream>
#include <fstream>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>
#include "InterfaceModule/LCOSDisplayTest.h"
#include "InterfaceModule/EEPROMUpdate.h"


struct ChannelModules
{
	int moduleNo;
	int channelNo;
};

class ActionVrb {
public:
						ActionVrb(){

		TrueFlex 		(*TF_Channel_DS_For_Save)[g_Total_Channels] = new TrueFlex[3][g_Total_Channels]();		//array holding temporary data from arrStructTF that pattern class will read. This array of structure will be locked while pattern class is reading it

		FixedGrid 		(*FG_Channel_DS_For_Save)[g_Total_Channels] = new FixedGrid[3][g_Total_Channels]();		//array holding temporary data from arrStructTF that pattern class will read. This array of structure will be locked while pattern class is reading it

		ModulesInfo 	arrModules[2];
		};
		virtual 	  	~ActionVrb(){

			delete[]		TF_Channel_DS_For_Save;
			delete[]		FG_Channel_DS_For_Save;
		};


		bool            StoreModuleTF(int moduleNum, TrueFlex (*arrTFChannel_DS)[g_Total_Channels], ModulesInfo* arrModules);
		bool			StoreModuleFG(int moduleNum, FixedGrid (*arrFGChannel_DS)[g_Total_Channels], ModulesInfo* arrModules);

		bool 			RestoreModule1(int moduleNum, TrueFlex (*arrTFChannel_DS)[g_Total_Channels], FixedGrid (*arrFGChannel_DS)[g_Total_Channels], ModulesInfo* arrModules);
		bool 			RestoreModule2(int moduleNum, TrueFlex (*arrTFChannel_DS)[g_Total_Channels], FixedGrid (*arrFGChannel_DS)[g_Total_Channels], ModulesInfo* arrModules);

private:

		TrueFlex 		(*TF_Channel_DS_For_Save)[g_Total_Channels];
		FixedGrid 		(*FG_Channel_DS_For_Save)[g_Total_Channels];

		bool			bModuleConfigStored[2]{0,0};           //  to see if two modules config info stored or not when restart,stored = 1, not stored = 0

};



class CmdDecoder {
public:
					CmdDecoder();
	virtual 	  	~CmdDecoder();

	bool 			b_SendString = true;												//FLag that message to user needs to be send
	bool 			b_CmdDecError = false;
	bool 			b_RestartNeeded = false;

	enum 			PreProcessConcat {FAILED, SUCCESS};

	enum 			PrintType {NO_ERROR,ERROR_LOW_PRIORITY, ERROR_HI_PRIORITY};
	PrintType 		ePrevErrorType;

	enum 			CommandFormat {SINGLE_CMD,CONCAT_CMD, INVALID_CMD};

	enum 			Verbs { NONE, SET, GET, ADD, DELETE, ACTION};				//Global Verbs FLAG
	Verbs 			eVerb;

	enum 			Objects {NONE_O = 7, CH_TF, CH_FG , MODULE, RESTART, CALFILE, FAULT, PANEL, IDN, HEATERMONITOR, FWUPGRADE, TECMONITOR};	//Global Object FLAG
	Objects 		eObject;
																			// ALL_ATTR_OF_CH =  // user want certain channel info and their attributes and slots
	enum 			Getall {SOME_ATTR = 50, ALL_CH_ALL_ATTR, ALL_ATTR_OF_CH, ALL_SLOTS_OF_CH};				// When user want to GET attributes, we want to know if he requested all channels or all slots (all channles include slots info if FIXED GRID module)
	Getall 			eGet;

	enum 			VerbError {VERB_NOTFOUND = 60, VERB_FOUND, VERB_WRONG};
	VerbError 		eVerbErrors;

	enum 			Modules {NONE_M = 20, TF, FIXEDGRID};
	Modules 		eModule1, eModule2;												//Two modules defines,... user will decide if they are TF or Fixed grid


	TrueFlex 		(*TF_Channel_DS)[g_Total_Channels] = new TrueFlex[3][g_Total_Channels]();			//() brackets are very important. initialize them to default values	//array of structure for 96 channeles			[3] -- TWo Modules- both can have either TF or Fixed or both [0] 0th index for module is ignored
	TrueFlex 		(*TF_Channel_DS_For_Pattern)[g_Total_Channels] = new TrueFlex[3][g_Total_Channels]();		//array holding temporary data from arrStructTF that pattern class will read. This array of structure will be locked while pattern class is reading it

	FixedGrid 		(*FG_Channel_DS)[g_Total_Channels]= new FixedGrid[3][g_Total_Channels]();			//() brackets are very important. inialize them to default values
	FixedGrid 		(*FG_Channel_DS_For_Pattern)[g_Total_Channels] = new FixedGrid[3][g_Total_Channels]();	//array holding temporary data from arrStructTF that pattern class will read. This array of structure will be locked while pattern class is reading it

	ModulesInfo 	arrModules[3];										// Two modules info, 0 not used

	Panel 			panelInfo;												// Keep Panel Records

	ActionVrb       actionSR;  //drc added for store and restore action process

#ifdef _DEVELOPMENT_MODE_

	bool 			TECSet = false;
	TECinfo 		arrStructTEC;

	DevelopModeVar 	structDevelopMode;

#endif

	std::string& 	ReceiveCommand(const std::string&);							//Called when user send new command, return 1 when command is processed well

	bool 			g_bTransferFinished = false;								// True when pattern generation finish transfering
	bool 			GetPatternTransferFlag(void);
	void 			SetPatternTransferFlag(bool);

	void 			PrintResponse(const std::string &, const enum PrintType &);

	Panel 			GetPanelInfo();
	void 			SetPanelInfo(bool);
	void 			SetPanelInfo(Panel&);
	std::vector<std::string> objVec;											// chMOdule vector that contains SplitCmdted strings of OBJECT, CH.M.N.S , MODULE.1 etc
	std::list<ChannelModules> activeChannels;

private:

	int 			g_edgeFreqDefined = 0;
	bool 			changeF1 = false;
	bool 			changeF2 = false;
	double 			prevF1 = 0;
	double 			prevF2 = 0;

	char 			buff[10000]{0};												//Buffer to add data that user can read- data send to user
	int 			buffLenTemp = 0;											//Temporary integer for shifting/jumping in buffer to another index

	int 			g_moduleNum = 0;												//Module number parsed from cmd
	int 			g_moduleNum_prev = 0;											// Holds the last state Module #, it helps us in GET command, get:ch.1.2;ch.2.3.. we can print differently knowing last known state
	int 			g_channelNum = 0;												//Channel number
	int 			g_slotNum = 0;													//Slot number
    int             g_faultNum = 0;

	bool 			g_bNoAttribute;												//Flag if no attribute is present

	int 			g_currentAttributeCount = 0;
	int 			g_totalAttributes = 0;
	bool 			g_bPrevAttrDisplayed = false;

private:

	void 			CopyDataStructures(void);									//Function to copy structures data from Command Decoder to Pattern Generator
	void 			ResetDataStructures(void);									// Reset to last known correct values
	void 			ResetGlobalVariables(void);
	void 			WaitPatternTransfer(void);									// This wait is needed to acquire errors from other modules if any happens

	int 			SearchVerb(std::string &);									// return enum flag number
	int 			SearchObject(std::string &);
	int 			SearchAttribute(std::string &);
	int 			Set_SearchAttributes(std::string &);
	int 			Print_SearchAttributes(std::string &);


	std::vector<std::string> SplitCmd(std::string s, std::string delimiter);

	int 			ZTEDecodeCommand (std::vector<std::string> &, int commandCount);
	int 			getCommandFormat(std::vector<std::string>& dest, const std::string& src);
	int 			preProcessConcatCommand(std::vector<std::string>& singleCommandVector, const std::string& singleCommand, int *commandCount);
	void 			postProcessSingleCommandResult(int* searchDone);
	int 			TestMandatoryAttributes(void);								// Test mandatory attributes of current global Module and global channel w.r.t global Object choosen i.e. TF or FG

	int 			is_BWSlotSizeIntegral(int *calculatedSlotsNumber, const double *newF1, const double *newF2, double *slotSize);
	int 			ModifySlotCountInChannel(const int *calculatedSlotsNumber, const double *newF1, const double *newF2, const double *slotSize);
	int 			ChannelsOverlapTest(void);
	int 			Overlap_Logic(const double *ch_f1, const double *ch_f2, const double *other_ch_f1, const double *other_ch_f2);
	int				CouldbeSlotOf_Logic(const double *ch_f1, const double *ch_f2, const double *other_ch_f1, const double *other_ch_f2);

	int 			is_SetTFDone();												// When CH.M.N only given TF
	int 			is_SetNoSlotFGDone();										// When CH.M.N only given FG
	int 			is_SetSlotFGDone();											// When CH.M.N.S given FG

	int 			is_GetTFDone();												// When CH.M.N only give TF
	int 			is_GetNoSlotFGDone();										// When CH.M.N only given FG
	int 			is_GetSlotFGDone();											// When CH.M.N.S given FG

	int 			is_AddTFDone();												// When CH.M.N only given TF
	int 			is_AddFGDone();												// When CH.M.N only given FG

	int 			is_DeleteTFDone(std::string &moduleNum, std::string &chNum);				// When CH.M.N only given TF
	int 			is_DeleteFGDone(std::string &moduleNum, std::string &chNum);				// When CH.M.N only given FG

	template <typename T> bool Sscanf(std::string &, T &, char);				// For scanning integer and double from string command

	template <typename T> void FillBuffer_ConcatAttributes(const char* firstTimePrintString , const char* secondTimePrintString, bool b_onlyModuleArg, T arg);								// For GET command, attributes have to be concats in one command one buffer..-> get:ch.1.1:fc:bw;ch.1.2:att:adp:ch.1.3:bw

	bool 			TestChannelsNotActive(std::string &, int &);				// String TF/FG and Int module #

	bool 			PrintAllChannelsTF(int);									// receive count how many channels need print. channel number is globally retrieve
	bool 			PrintAllChannelsFG(int);

	void            PrintAllSlotsFG(int SlotNum);

};


#endif /* SRC_CMDDECODER_CMDDECODER_H_ */
