/*
 * SerialModule.cpp
 *
 *  Created on: Jan 18, 2023
 *      Author: mib_n
 */

#include "SerialModule.h"

#include <stdio.h>
#include <iostream>
#include <sys/time.h>													// For time measurement

// Linux headers
#include <fcntl.h> 														// Contains file controls like O_RDWR
#include <errno.h> 														// Error integer and strerror() function
													// Contains POSIX terminal control definitions
#include <string>														// For memset and strerror
#include <unistd.h> 													// Write(), read(), close()
#include <sys/file.h>  													// Use of flock() to block other process accessing serial port
#include <algorithm>
#include <chrono>

#include "wdt.h"

SerialModule* SerialModule::pinstance_{nullptr};

extern ThreadSafeQueue<std::string> packetQueue;   //for file packets transfer between serial and file transfer thread
//extern ThreadSafeQueue<uint8_t> fwPacketQueue;
extern ThreadSafeQueue<bool> ackQueue;

SerialModule::SerialModule()
{
	int status = Serial_Initialize();

	if(status != 0)
	{
		printf("Driver<RS232>: Serial Module Initialization Failed.\n");
		// Mode drc checked this is not right when no serial at all
		std::ofstream enable_file("/mnt/enable_flag");
		if (enable_file) {
			enable_file << "SPI";
			enable_file.close();
		} else {
			std::cerr << "ERROR: Cannot write to enable_flag" << std::endl;
			return;
		}
		//
		std::cout << "SPI Mode" << std::endl;
		Serial_Closure();
	}

	mmapGPIO = new MemoryMapping(MemoryMapping::GPIO);
}

SerialModule::~SerialModule()
{
	printf("............APPLICATION IS CLOSED............\n\r");
	delete mmapGPIO;
}

/**
 * The first time we call GetInstance we will lock the storage location
 *      and then we make sure again that the variable is null and then we
 *      set the value. RU:
 */
SerialModule *SerialModule::GetInstance()
{
	if (pthread_mutex_lock(&global_mutex[LOCK_SERIAL_INSTANCE]) != 0)	// locking and checking the result, if lock was successful and no deadlock happened
		std::cout << "global_mutex[LOCK_SERIAL_INSTANCE] lock unsuccessful" << std::endl;
	else
	{
	    if (pinstance_ == nullptr)
	    {
	        pinstance_ = new SerialModule();
	    }

		if (pthread_mutex_unlock(&global_mutex[LOCK_SERIAL_INSTANCE]) != 0)	// Unlocking and checking the result, if lock was successful and no deadlock happened
			std::cout << "global_mutex[LOCK_SERIAL_INSTANCE] unlock unsuccessful" << std::endl;
	}

    return pinstance_;
}

int SerialModule::MoveToThread()
{
	if (thread_id == 0)
	{
		if (pthread_create(&thread_id, &thread_attrb, ThreadHandle, (void*) this) != 0) // 'this' is passed to pointer, so pointer dies as function dies
		{
			printf("Driver<RS232>: thread_id create fail.\n");
			return (-1);
		}
		else
		{
			printf("Driver<RS232>: thread_id create OK.\n");
		}
	}
	else
	{
		printf("Driver<RS232>: thread_id already exist.\n");
		return (-1);
	}

	return (0);
}

void *SerialModule::ThreadHandle(void *arg)
{
	SerialModule *recvPtr = (SerialModule*) arg;						// Thread handler, called function - No arguments passed, on this instances
	recvPtr->ProcessReadWrite();										// Do thread work here
	return (NULL);
}

void SerialModule::ProcessReadWrite(void)
{
	int num_bytes{0};
//	unsigned int readData{0};
	std::string temp_search_Str{""};									// Take data from temp_read_buf and perform delimiter searching. It also separate commands if they come all back to back \01 \04\01 \04
	std::string finalCommand{""};
	std::string strPacket{""};										// One complete command without delimiters


	while(b_LoopOn)														// Serial loop running on a thread.
	{
		usleep(100);
#ifdef _WATCHDOG_SOFTRESET_
		watchdog_feed();
#endif
		do																// Read from user until all data from serial line are received...
		{
#ifndef _SPI_INTERFACE_
			num_bytes = Serial_ReadPort(temp_search_Str);				// Keep reading serial port until command stop arriving
#endif
			if(num_bytes < 0)
			{
				printf("Error reading: %s", strerror(errno));	// If board is not logged in
			}
			else if(num_bytes > 0)
			{

//				std::cout << "Cmd Received = " << temp_search_Str<< std::endl;
				if(temp_search_Str.size() > MAX_BUFFER_LENGTH)	// If data received is more than 50,000 bytes we reset and give overflow error
				{
					temp_search_Str.clear();
					Serial_WritePort("\01MESSAGE_QUEUE_FULL\04\n");
					sleep(2); //required to make flush work, for some reason
				    tcflush(iSerialFD, TCIFLUSH);
					break;
				}
			}
			else
			{
				//nothing happened

			}

			usleep(6000);		// 5000 > delay is necessary otherwise serial read wont have much time to add all data to the buffer and you might miss \04 delimiter10

		}while(num_bytes != 0 && b_LoopOn);

		while(temp_search_Str.size() > 0)
		{
		   // Start the clock
			auto start = std::chrono::high_resolution_clock::now();
#ifdef _WATCHDOG_SOFTRESET_
		watchdog_feed();
#endif

//			clock_t tstart = clock();
//			std::cout << "First clock = " << tstart << std::endl;

			//extract download file packets into queue

			int status = Serial_ExtractFilePacket(strPacket, temp_search_Str);

			if (status == DelimiterStatus::FOUND)
			{
				Serial_WritePort("\01OK\04");

#ifdef  _TEST_DOWNLOAD_
			    // Create a test packet with a packet number and firmware bytes
			    uint16_t packetNumber = 1; // Packet number 1
			    const int packetSize = 2048; // Size of firmware data per packet
			    std::string testPacket = "\x03"; // Start delimiter
			    testPacket += cmd_decoder.file_transfer->intToHexChar((packetNumber >> 8) & 0xFF); // High byte of packet number
			    testPacket += cmd_decoder.file_transfer->intToHexChar(packetNumber & 0xFF);      // Low byte of packet number

			    // Append firmware data (for brevity, using a pattern of 0xAA and 0xBB)
			    for (int i = 0; i < packetSize; ++i) {
			        testPacket += (i % 2 == 0) ? "\xAA" : "\xBB";
			    }

			    // Append the end delimiter
			    testPacket += "\x04";

//			    cmd_decoder.file_transfer->processFirmwarePackets(testPacket, packetSize+4);

				cmd_decoder.file_transfer->processFirmwarePackets(strPacket,cmd_decoder.file_transfer->file_num_bytes);
#endif
				num_bytes = 0;
				break;
			}
			status = Serial_ExtractSingleCommand(finalCommand, temp_search_Str);	// Extract commands one after another if multiple commands are there with delimiters \01..\04\01...\04\01...\04.

			if (status == DelimiterStatus::INVALID)
			{
				Serial_WritePort("\01INVALID_DELIMITER\04\n");
				continue;
			}
			else if (status == DelimiterStatus::MISSING)
			{
				Serial_WritePort("\01MISSING_DELIMITER\04\n");
				continue;
			}
			else if(status == DelimiterStatus::OK)
			{//ack to upload gamma file packets
		        continue;
			}

			//std::cout << "FINAL COMMAND = " << finalCommand << "Size = " << finalCommand.size() << std::endl;

			//Done
			//mmapGPIO->WriteRegister_GPIO(0x000c/0x4, 0x0);usleep(1000);//Done PIN
            //mmapGPIO->WriteRegister_GPIO(0x0008/0x4, 0x0);usleep(1000);//Done PIN

            //Error
            //mmapGPIO->WriteRegister_GPIO(0x0004/0x4, 0x0);usleep(1000);//Err PIN
            //mmapGPIO->WriteRegister_GPIO(0x0000/0x4, 0x0);usleep(1000);//Err PIN

            //mmapGPIO->WriteRegister_GPIO(0x0004/0x4, 0x1);usleep(1000);//Err PIN
            //mmapGPIO->ReadRegister_GPIO(0x0000/0x4, &readData);usleep(1000);
	        //std::cout << "ErrorPin 0 = " << readData <<std::endl;
			
	        //mmapGPIO->WriteRegister_GPIO(0x0000/0x4, 0x1);usleep(1000);
	        //mmapGPIO->ReadRegister_GPIO(0x0000/0x4, &readData);usleep(1000);

	        //std::cout << "ErrorPin 1 = " << readData <<std::endl;

			if (finalCommand.size() > 0)
			{
				Serial_RefineCommand(finalCommand);		// Remove spaces and other extract characters \r\n \t etc.

#ifdef _DEVELOPMENT_MODE_
				if (finalCommand == "q")				//Make sure the data is not 'q' quit call
				{
					Serial_Closure();
					b_Restart = false;
					printf("\nQuitting Application...\n");
					break;
				}
#endif
				 //std::cout << "finalCommand: "  << finalCommand << std::endl;

				 sendMsg = Serial_InitiateCommandDecoding(finalCommand.data());

				 int status = CheckOperationFromCommandDecoder();

				 // Stop the clock
				 auto stop = std::chrono::high_resolution_clock::now();

				 // Calculate the duration
				 auto duration = std::chrono::duration_cast<std::chrono::microseconds>(stop - start);

				 // Print the execution time
				 std::cout << "Total execution time: " << duration.count() << " microseconds" << std::endl;

				 //CheckOperationFromOtherModules();
				 if(status == OperationMode::RESTART)
				 {
					Serial_Closure();
					b_Restart = true;
					//ahbInsMain->write_ahbRegister(0x01b8, 0);	// Writing 1 to DMA before restart. so DMA knows we are restarting
					usleep(1000);
					Serial_WritePort("\01Restarting Application...\04\n");
					std::cout << "Restarting Application... " << std::endl;
//					sync();
//					reboot(RB_AUTOBOOT);

					const char* processName = "WSS_Main.elf"; // Replace with the name of your application

					// Kill the application
					std::string killCommand = std::string("pkill ") + processName; // This will send SIGTERM
					system(killCommand.c_str());

					 // Wait for the application to terminate
					sleep(1); // Optional: Wait for 1 second

					// Relaunch the application
					std::string launchCommand = std::string("./") + processName; // Assuming the app is in the current directory
					system(launchCommand.c_str());

					break;
				 }
			}

			finalCommand.clear();
		}

		num_bytes = 0;
		temp_search_Str.clear();

		/* I commented, because sendMsg has no message if user have no command.
		 * Change logic to fetch message directly from command decoder and
		 * not rely on sendMsg*/
		//CheckOperationFromCommandDecoder();						// Check operation from command decoder even if no command is send

	}

	pthread_exit(NULL);
}

void SerialModule::StopThread()
{
	Serial_Closure();

	if(iSerialFD != -1)
	{
		tcflush(iSerialFD, TCIOFLUSH);	// Clear write buffer
		flock(iSerialFD, LOCK_UN);	// UNLOCK THE FILE
		close(iSerialFD);
	}

     // Wait for thread to exit normally
	if (thread_id != 0)
	{
		pthread_join(thread_id, NULL);
		thread_id = 0;
	}

	printf("Driver<RS232>: Thread terminated\n");

	if(pinstance_ != nullptr)
	{
		delete pinstance_;
		pinstance_ = nullptr;
	}

}


int SerialModule::Serial_ExtractFilePacket(std::string& strPacket, std::string& temp_search_Str)
{
	std::size_t delimiter04_pos = 0;
	std::size_t delimiter03_pos = 0;

	bool b_missingDelim = false;
	bool b_invalidDelim = false;
	bool issue = false;

	if(cmd_decoder.file_transfer->b_Start_Download == true)
	{
		delimiter03_pos = temp_search_Str.find(START_DOWNLOAD_DELIMITER, 0);
		if( delimiter03_pos == std::string::npos)	// Not Found at all-
		{
			// file packets delimited by '\03\' Not found at all
			b_missingDelim = true;
			issue = true;
		}
		else
		{
			delimiter04_pos = temp_search_Str.rfind(END_DELIMITER); //last occurrance of END_DELIMITER
			if( delimiter04_pos == std::string::npos)	// END_DELIMITER Not Found at all
			{
				b_missingDelim = true;
				issue = true;
			}
			else
			{
				//For case \03
				if(delimiter04_pos < delimiter03_pos)
				{// For case \04....\03..
					b_invalidDelim = true;
					issue = true;
				}
				else if(delimiter04_pos - delimiter03_pos +1 > 2052)
				{
					std::cout << strPacket << " wrong file packet ending: " << delimiter04_pos <<std::endl;
					delimiter04_pos = temp_search_Str.rfind(END_DELIMITER,delimiter04_pos-1);
					if( delimiter04_pos == std::string::npos)	// END_DELIMITER Not Found at all
					{
						b_missingDelim = true;
						issue = true;
					}
				}
				else if(delimiter04_pos - delimiter03_pos +1 < 2052)
				{
					if(delimiter04_pos - delimiter03_pos +1 != temp_search_Str.length())
					{
						b_invalidDelim = true;
						issue = true;
					}
					else
					{
						//last packet or small files less than 2048 bytes
					}
				}
				else
				{
					// All Good
				}
			}
		}

		//Extract file packets
		if(issue == false)
		{
			strPacket = temp_search_Str.substr(delimiter03_pos,delimiter04_pos+1); // '\03' and '\04' should be extracted too
			//push packet into queue
			packetQueue.push(strPacket);
			temp_search_Str.erase(delimiter03_pos, delimiter04_pos+1);
			std::cout << strPacket << " file packet size = "<< strPacket.size() <<std::endl;
			return (DelimiterStatus::FOUND);
		}
		else
		{
			if(b_invalidDelim)
			{
				return (DelimiterStatus::INVALID);
			}
			else if(b_missingDelim)
			{
				return (DelimiterStatus::MISSING);
			}
			else
			{
				//Not file packet, go to next step: command format parsing
			}
		}

	}
	else
	{
		//if no packet download, nothing else happened
	}
	return (0);
}

/*
int SerialModule::Serial_ExtractFilePacket(std::string& strPacket, std::string& temp_search_Str)
{

	bool b_missingDelim = false;
	bool b_invalidDelim = false;
	bool issue = false;

	if(cmd_decoder.file_transfer->b_Start_Download == true) {
		std::vector<size_t> delimiter04_pos; // Vector to store positions of char1
		std::vector<size_t> delimiter03_pos; // Vector to store positions of char2

		// First loop to find positions of both characters
		for (size_t i = 0; i < temp_search_Str.length(); ++i) {
			if (temp_search_Str[i] == END_DELIMITER) {
				delimiter04_pos.push_back(i); // Store position of char1
			} else if (temp_search_Str[i] == START_DOWNLOAD_DELIMITER) {
				delimiter03_pos.push_back(i); // Store position of char2
			}
		}
		if(delimiter04_pos.size() == 0 || delimiter03_pos.size() == 0)
		{
			b_missingDelim = true;
			issue = true;
//			temp_search_Str.clear();

		}
		// Check for pairs of positions
		int count = 0;
		for (size_t pos1 : delimiter04_pos) {
			for (size_t pos2 : delimiter03_pos) {
				if (std::abs(static_cast<int>(pos1 - pos2 + 1)) != 2052) {
//					count++; // Increment count for each valid pair
					b_invalidDelim = true;
					issue = true;
					continue;
				}
				else
				{
					b_missingDelim = false;
					b_invalidDelim = false;
					issue = false;
					//Extract
					strPacket = temp_search_Str.substr(pos2,pos1+1); // '\03' and '\04' should be extracted too
					//push packet into queue
					packetQueue.push(strPacket);
	//				temp_search_Str.erase(pos2, pos1+1);
					temp_search_Str.clear();
					std::cout << strPacket << " file packet size = "<< strPacket.size() <<std::endl;
					return (DelimiterStatus::FOUND);
				}
			}
		}
		if(b_invalidDelim)
		{
//			temp_search_Str.clear();
			return (DelimiterStatus::INVALID);
		}
		else if(b_missingDelim)
		{
			return (DelimiterStatus::MISSING);
		}
	}

	return (0);

}
*/
int SerialModule::Serial_ExtractSingleCommand(std::string& finalCommand, std::string& temp_search_Str)
{

	/*Following commands were tested and the following results were found:
	 * Hello					- Missing delimiter
	 * \01 Hellow 				- Missing delimiter
	 * Hellow\04				- Missing delimiter
	 * \04Hellow\01				- Invalid delimiter
	 * \01Hello\04				- Correct
	 * \01Hello\04\01Maybee\04	- Correct
	 * Hello\01Maybe\04			- Missing delimiter
	 * \01Hello\01maybe\04		- Missing delimiter
	 * \04Hellow\01Maybe\04		- Second command ok, first delimiter missing
	 */

	std::size_t delimiter04_pos = 0;
	std::size_t delimiter01_pos = 0;

	bool b_missingDelim = false;
	bool b_invalidDelim = false;
	bool issue = false;

	delimiter01_pos = temp_search_Str.find(START_DELIMITER, 0);
	if( delimiter01_pos == std::string::npos)	// Not Found at all-
	{
		temp_search_Str.clear();
		b_missingDelim = true;
		issue = true;
	}
	else
	{
		delimiter04_pos = temp_search_Str.find(END_DELIMITER, 0);

		if( delimiter04_pos == std::string::npos)	// Not Found at all-
		{
			b_missingDelim = true;
			temp_search_Str.clear();
			issue = true;
		}
		else
		{
			std::size_t temp_delimiter_01 = 0;

			temp_delimiter_01 = temp_search_Str.find(START_DELIMITER, delimiter01_pos+1);	// Find \01 other than previous one
			if( temp_delimiter_01 == std::string::npos)	// Not Found at all-
			{
				if(delimiter04_pos < delimiter01_pos)
				{// For case \04....\01..
					b_invalidDelim = true;
					temp_search_Str.clear();
					issue = true;
				}
				else
				{
					// All Good
				}
			}
			else
			{
				// There maybe \01 between other... \01... \01....\04

				if(temp_delimiter_01 > delimiter04_pos && temp_delimiter_01 > delimiter01_pos)
				{
					// ALL GOOD
				}
				else
				{
					b_missingDelim = true;
					temp_search_Str.clear();
					issue = true;
				}
			}

		}
	}

	//Extract
	if(issue == false)
	{
		finalCommand = temp_search_Str.substr(delimiter01_pos+1, delimiter04_pos-1);
//		temp_search_Str.erase(delimiter01_pos, delimiter04_pos+1);
		temp_search_Str.clear();
		std::cout << finalCommand << " size = "<< finalCommand.size() <<std::endl;
		std::transform(finalCommand.begin(), finalCommand.end(), finalCommand.begin(), ::toupper);
		if(finalCommand == "OK") {//ack to gamma file upload
			ackQueue.push(true);
			return (DelimiterStatus::OK);
		}

	}
	else
	{
		if(b_invalidDelim)
		{
			return (DelimiterStatus::INVALID);
		}
		else if(b_missingDelim)
		{
			return (DelimiterStatus::MISSING);
		}
	}

//	issue = false;
	temp_search_Str.clear();

	return (DelimiterStatus::FOUND);

}

void SerialModule::Serial_RefineCommand(std::string& finalCommand)
{
	TrimString(finalCommand, " \t\n\r\f\v");	// remove newline and whitespacess from start and end of string only

	RemoveStringSpaces(finalCommand);	// Remove any spaces that exist between characters

}

std::string& SerialModule::Serial_InitiateCommandDecoding(const std::string& finalCommand)
{
	//return (cmd_decoder.ReceiveMsg(finalCommand));		// Send command to decoder  cmdDec.
	return (cmd_decoder.ReceiveCommand(finalCommand));

}

int SerialModule::Serial_Initialize()
{
	b_endMainSignal = false;
	b_LoopOn = true;

	pthread_attr_init(&thread_attrb);									// Default initialize thread attributes

	int status = 0;

	status = Serial_OpenFileDescriptor();

	status = Serial_SetSerialAttributes();

	status = Serial_LockFileDescriptor();

	if(status != 0)
	{
		return (-1);
	}

	return (0);
}

void SerialModule::Serial_Closure(void)
{
	b_LoopOn = false;			// No need MUTEX LOCK because Serial Closure mainly happen from within the while loop
	b_endMainSignal = true;
}

int SerialModule::Serial_OpenFileDescriptor(void)
{
	/*****Change device path as needed (currently set to an standard FTDI USB-UART cable type device)*****/

	iSerialFD = open("/dev/ttyPS1", O_RDWR | O_NOCTTY);

	if(iSerialFD == -1)													// File descriptor opening error
	{
		return (-1);
	}

	return (0);
}

int SerialModule::Serial_SetSerialAttributes(void)
{
	if(SetSerialAttribs(iSerialFD, B115200, 0) != 0)					// Set speed to 115,200 bps, 8n1 (no parity)
	{
		return (-1);
	}

	return (0);
}

int SerialModule::Serial_LockFileDescriptor(void)
{
	fcntl(iSerialFD, F_SETFL, FNDELAY);									// Return 0 if no character available in buffer upon read. (No blocking)

	if (flock(iSerialFD, LOCK_EX | LOCK_NB) == -1)						// Acquire non-blocking exclusive lock
	{
		throw std::runtime_error("Serial port with file descriptor " + std::to_string(iSerialFD) + " is already locked by another process.");
		return (-1);
	}

	return (0);
}


int SerialModule::Serial_ReadPort(std::string& temp_search_Str)
{
	int num_bytes{0};
	bool bBin = false;
	char temp_read_buf1 [SERIAL_READ_LENGTH]{0};
	std::vector<uint8_t>temp_read_buf2(2052);


	if(cmd_decoder.file_transfer->b_Start_Download == true) {
		num_bytes = read(iSerialFD, temp_read_buf2.data(), temp_read_buf2.size());
		bBin = true;

	} else {
		bBin = false;
		// This buffer hold temporary data upon each read function.
		memset(&temp_read_buf1, '\0', sizeof(temp_read_buf1));				// Reset temp_read_buf all values with \0
		num_bytes = read(iSerialFD, &temp_read_buf1, sizeof(temp_read_buf1)-1);
	}

	if(num_bytes > 0)
	{
		if (bBin == false)
			temp_search_Str += temp_read_buf1;
		else
//			memcpy(&temp_search_Str[0],temp_read_buf2.data(),temp_read_buf2.size());
			temp_search_Str.append(reinterpret_cast<char*>(temp_read_buf2.data()),num_bytes);

//			temp_search_Str.append(temp_read_buf2.begin(),temp_read_buf2.end());
	}

	return (num_bytes);
}




int SerialModule::Serial_WritePort(const std::string& sendMsg)
{
	// MUTEX LOCK, in case other module try to access Serial_WritePort function
	if (pthread_mutex_lock(&global_mutex[LOCK_SERIAL_WRITE]) != 0)	// locking and checking the result, if lock was successful and no deadlock happened
		std::cout << "global_mutex[LOCK_SERIAL_WRITE] lock unsuccessful" << std::endl;
	else
	{
		const char *pSendBuff;
		int num_bytes = 0;

		//writeMsg 1 when command is received, b_SendString 1 when command is successfully decoded
		pSendBuff = &sendMsg[0];

		do
		{	// Jump to remaining pointer position "pSendBuff+s_num_bytes" and send the remaining bytes "sendMsg.length()-s_num_bytes"
			num_bytes += write(iSerialFD, pSendBuff+num_bytes, sendMsg.length()-num_bytes);	// if byte more than 4096 buffer will get full, and then we loop and send the remaining bytes after clearing buffer

			tcdrain(iSerialFD);	// Wait until transmission ends. Make sure first half is sent and then clear the buffer
			tcflush(iSerialFD, TCOFLUSH);	// Clear write buffer

		}while(num_bytes != (int)sendMsg.length());


		if (pthread_mutex_unlock(&global_mutex[LOCK_SERIAL_WRITE]) != 0)	// Unlocking and checking the result, if lock was successful and no deadlock happened
			std::cout << "global_mutex[LOCK_SERIAL_WRITE] unlock unsuccessful" << std::endl;
	}
	//Done
	mmapGPIO->WriteRegister_GPIO(0x0008/0x4, 0x1);usleep(1000);
	return (0);
}

int SerialModule::CheckOperationFromCommandDecoder(void)
{
	/**************************************************/
	/*		   CHECK IF 'WRITE' IS NEEDED			  */
	/**************************************************/

	if (cmd_decoder.b_SendString)	// If command decoder wants to write something
	{
		cmd_decoder.b_SendString = false;
		Serial_WritePort(sendMsg);
		sendMsg.clear();

		if(cmd_decoder.b_RestartNeeded)
		{
			return (OperationMode::RESTART);
		}
	}

	return (OperationMode::NORMAL);
}

// trim from left &right
void SerialModule::TrimString(std::string &s, const char *delimiters)
{
	//Old but working:
	//s.erase(s.find_last_not_of(delimiters) + 1).erase(0, s.erase(s.find_last_not_of(delimiters) + 1).find_first_not_of(delimiters));

	//New I did:
	 s.erase(s.find_last_not_of(delimiters) + 1, std::string::npos).erase(0,s.find_first_not_of(delimiters));

}

void SerialModule::RemoveStringSpaces(std::string& input)
{
	input.erase(remove_if(input.begin(), input.end(), isspace), input.end());
}

int SerialModule::SetSerialAttribs(int iSerialFD, int speed, int parity)
{
	struct termios tty;													// Create new termios struc, we call it 'tty' for convention

	if (tcgetattr(iSerialFD, &tty) != 0)								// Read in existing settings, and handle any error
	{
		printf("Error %i from tcgetattr: %s\n", errno, strerror(errno));
		return (-1);
	}

	cfsetispeed(&tty, speed);											// Set In baudrate
	cfsetospeed(&tty, speed);											// Set Out baudrate

	tty.c_cflag &= ~PARENB;												// Clear parity bit, disabling parity (most common)
	tty.c_cflag &= ~CSTOPB;												// Clear stop field, only one stop bit used in communication (most common)
	tty.c_cflag &= ~CSIZE;												// Clear all bits that set the data size
	tty.c_cflag |= CS8;													// 8 bits per byte (most common)
	tty.c_cflag &= ~CRTSCTS;											// Disable RTS/CTS hardware flow control (most common)
	tty.c_cflag |= CREAD | CLOCAL;										// Turn on READ &ignore ctrl lines (CLOCAL = 1)+
	tty.c_cflag |= parity;

	tty.c_lflag &= ~ICANON;
	tty.c_lflag &= ~ECHO;												// Disable echo
	tty.c_lflag &= ~ECHOE;												// Disable erasure
	tty.c_lflag &= ~ECHONL;												// Disable new-line echo
	tty.c_lflag &= ~ISIG;												// Disable interpretation of INTR, QUIT and SUSP
	tty.c_iflag &= ~(IXON | IXOFF | IXANY);								// Turn off s/w flow ctrl
	tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR
					| IGNCR | ICRNL);									// Disable any special handling of received bytes

	tty.c_oflag &= ~OPOST;												// Prevent special interpretation of output bytes (e.g. newline chars)
//	tty.c_oflag &= ~ONLCR;												// Prevent conversion of newline to carriage return/line feed

	cfmakeraw(&tty);

	/* NO USING THEM so initialize to 0
	 * http://www.iitk.ac.in/LDP/HOWTO/text/Serial-Programming-HOWTO */

	tty.c_cc[VTIME] = 0;												// Wait for up to 1s (10 deciseconds), returning as soon as any data is received. If > 0 you get read error due to set FCNTL
	tty.c_cc[VMIN] = 0;
	tty.c_cc[VINTR] = 0;												/*Ctrl-c */
	tty.c_cc[VQUIT] = 0; 												/*Ctrl-\ */
	tty.c_cc[VERASE] = 0; 												/*del */
	tty.c_cc[VKILL] = 0; 												/*@ */

	if (tcsetattr(iSerialFD, TCSANOW, &tty) != 0)						// Save tty settings, also checking for error
	{
		printf("Error %i from tcsetattr: %s\n", errno, strerror(errno));
		return (-1);
	}
	savetty = tty;
	return (0);
}

/*
int SerialModule::SetSerialAttribsForBinaryData()// raw mode for binary data transfer
{
	struct termios tty;													// Create new termios struc, we call it 'tty' for convention

	if (tcgetattr(iSerialFD, &tty) != 0)								// Read in existing settings, and handle any error
	{
		printf("Error %i from tcgetattr: %s\n", errno, strerror(errno));
		return (-1);
	}

	cfmakeraw(&tty);

	tty.c_cc[VMIN] = 0; //03 00 00 01 04
	tty.c_cc[VTIME] = 1;

	if (tcsetattr(iSerialFD, TCSANOW, &tty) != 0)						// Save tty settings, also checking for error
	{
		printf("Error %i from tcsetattr: %s\n", errno, strerror(errno));
		return (-1);
	}

	return (0);
}
*/
