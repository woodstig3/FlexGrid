#include<sys/mman.h>
#include<sys/types.h>
#include<sys/stat.h>
#include<sys/file.h>
#include<fcntl.h>
#include<unistd.h>
#include<stdio.h>
#include<string.h>
#include<error.h>
#include<stdlib.h>
#include <iostream>
using namespace std;

#define MaxLine 200  //limitation line number of log file
#define VariousF 4 //category for error
#if 0
typedef enum FaultName
     {
	    HEATER_1_TEMP,
	    HEATER_2_TEMP,
	    TEC_TEMP,
	    ADC_AD7689_ACCESS_FAILURE,
	    DAC_AD5624_ACCESS_FAILURE,
	    DAC_LTC2620_ACCESS_FAILURE,
	    TRANSFER_FAILURE,
	    WATCH_DOG_EVENT,
	    FIRMWARE_DOWNLOAD_FAILURE,
	    WSS611_ACCESS_FAILURE,
	    QDMA_FPGA_ACCESS_FAILURE,
	    QDMA_FLASH_ACCESS_FAILURE,
	    FLASH_PROGRAMMING_ERROR,
	    EEPROM_ACCESS_FAILURE,
	    EEPROM_CHECKSUM_FAILURE,
	    CALIB_FILE_MISMATCH,
	    CALIB_FILE_MISSING,
	    CALIB_FILE_CHECKSUM_ERROR,
	    FW_FILE_CHECKSUM_ERROR,
	    FPGA1_DOWNLOAD_FAILURE,
	    FPGA2_DOWNLOAD_FAILURE
     };
#endif

typedef enum FaultName
     {
	    HEATER_1_TEMP,
	    HEATER_2_TEMP,
	    ADC_AD7689_ACCESS_FAILURE,
	    WATCH_DOG_EVENT
     };

#if 0
typedef struct MessRes
	{
	    char name[30];
	    char timestamp[20];
	    bool Degraded;
	    int  DegradedCount;
	    bool Raised;
	    int  RaisedCount;
	    char Debounce[20];
	    char FailCondition[20];
	    char DegradeCondition[20];
	};
#endif
typedef struct MessRes
	{
	    char name[30];
	    bool Degraded;
	    int  DegradedCount;
	    bool Raised;
	    int  RaisedCount;
	};

typedef struct MessDis
   {
    char name[30];
    char Degraded[2];
    char DegradedCount[10];
    char Raised[2];
    char RaisedCount[10];
   };
int Debug_logfile(FaultName nameIndex,char *buf);
int Fault_logcompress(FaultName nameIndex,MessRes *mess);
int Extend_log(char *buf,FaultName lineIndex);
int Get_logitem(int faultNo, MessDis *mess);
int Replace_log(char *buf,FaultName lineIndex);
string CharToStr(char * contentChar);
