/*
 * SerialModule.h
 *
 *  Created on: Jan 18, 2023
 *      Author: mib_n
 */

#ifndef SRC_SERIALMODULE_SERIALMODULE_H_
#define SRC_SERIALMODULE_SERIALMODULE_H_

#include <string>
#include <pthread.h>
#include <time.h>

#include "CmdDecoder.h"
#include "InterfaceModule/MemoryMapping.h"


class SerialModule
{
protected:

	SerialModule();
	virtual ~SerialModule();

public:

    /**
     * Singletons should not be cloneable.
     */
					SerialModule(SerialModule &other) = delete;

	/**
     * Singletons should not be assignable.
     */
    void 			operator=(const SerialModule &) = delete;

    /**
     * This is the static method that controls the access to the singleton
     * instance. On the first run, it creates a singleton object and places it
     * into the static field. On subsequent runs, it returns the client existing
     * object stored in the static field.
     */
    static SerialModule *GetInstance();


	enum 			DelimiterStatus {INVALID, MISSING, FOUND};
	enum 			OperationMode{NORMAL,RESTART};

	CmdDecoder 		cmd_decoder;
	MemoryMapping 	*mmapGPIO;

	int 			MoveToThread();
	void 			StopThread();

	int 			Serial_ReadPort(std::string&);
	int 			Serial_WritePort(const std::string&);
	std::string& 	Serial_InitiateCommandDecoding(const std::string& finalCommand);
	bool 			b_endMainSignal = false;								// To close main loop and end application when serial thread ends
	bool 			b_Restart = false;

private:

    static SerialModule *pinstance_;

	int 			iSerialFD;												// Serial file descriptor
	bool 			b_LoopOn;												// Loop running on thread

	pthread_t 		thread_id{0};								// Create Thread id
	pthread_attr_t 	thread_attrb;								// Create Attributes

	const unsigned int MAX_BUFFER_LENGTH = 50000;
	const unsigned int SERIAL_READ_LENGTH = 15000;

	const char 		START_DELIMITER = '\01';
	const char      START_DOWNLOAD_DELIMITER = '\03';
	const char 		END_DELIMITER = '\04';

	std::string 	sendMsg{""};
	bool 			b_DuplicateError= false;

private:

	int 			Serial_Initialize(void);
	void 			Serial_Closure(void);

	static void 	*ThreadHandle(void *);
	void 			ProcessReadWrite(void);								// Thread looping while in the function

	int 			SetSerialAttribs(int,int,int);
	void 			TrimString(std::string&, const char*);
	void 			RemoveStringSpaces(std::string& input);
	int 			CheckOperationFromCommandDecoder(void);				// Check if any Write or other operation needed from Command decoder

	int 			Serial_OpenFileDescriptor();
	int 			Serial_SetSerialAttributes();
	int 			Serial_LockFileDescriptor();

	int 			Serial_ExtractSingleCommand(std::string& dest, std::string& scr);
	int             Serial_ExtractFilePacket(std::string& dest, std::string& scr);  //drc added for file packets parsing

	//std::string& 	Serial_InitiateCommandDecoding(const std::string& finalCommand);
	//int Serial_ExtractSingleCommand2(std::string& dest, std::string& scr, std::string &temp_buff, bool *startDelimiter, bool *endDelimiter);

	void 			Serial_RefineCommand(std::string& finalCommand);		// Remove extra and trim additional ascii like \r\n etc

};

#endif /* SRC_SERIALMODULE_SERIALMODULE_H_ */
