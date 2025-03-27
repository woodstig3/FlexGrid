/*
 * Main() is responsible to create instances of all objects.
 * It will initiate creation of thread for each instances in
 * a specific order. This order of creating instances will
 * be called backwards when main() will end.
 */

#include <unistd.h>
#include <time.h>
#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <assert.h>
#include <iomanip>

#include "GlobalVariables.h"
#include "SerialModule.h"
#include "SpiProcess.h"
#include "PatternCalibModule.h"
#include "TemperatureMonitor.h"
#include "PatternGenModule.h"
#include "AlarmUIO.h"
#include "MemoryMapping.h"
#include "I2CProtocol.h"
#include "SpiInterface.h"
#include "wdt.h"

pthread_mutex_t global_mutex[NUM_OF_MUTEXES];
pthread_mutexattr_t mutex_attribute;
pthread_cond_t cond, cond_result_ready;
bool g_bNewCommandData;
bool g_bTempChanged;
bool g_ready1, g_ready2, g_ready3, g_ready4;

void InitializeGlobalMutex(void);
void DestroyGlobalMutex(void);

/*
 * The databases exist in CommandDecoder Module, and Pattern Generation Module.
 * The database access is done through SerialModule. A user can look into database
 * to GET any Object parameters. The modules such as Temperature monitor, Pattern
 * Calibration, Pattern Generation must have access to CommandDecoder's database
 * so that they can modify the database accordingly. In order to give each module
 * access to database we must have the instance of serial module to each of them,
 * such that they can access database of command decoder and also send any
 * string output on serial terminal.
 *
 * Classical Singleton Design
 */

ThreadSafeQueue<std::string> packetQueue;  //the queue used for receiving file transfer packets from serial module to file-transfer module.
//ThreadSafeQueue<uint8_t> fwPacketQueue;
ThreadSafeQueue<bool> ackQueue;


int main(int argc, char* argv[])
{
	InitializeGlobalMutex();


	SerialModule *serialIns = SerialModule::GetInstance();
	if(serialIns->MoveToThread() != 0)
		printf("SerialModule: MoveToThread Failed!\n");

	TemperatureMonitor *tempIns = TemperatureMonitor::GetInstance();
	if(tempIns->MoveToThread() != 0)
	printf("TemperatureModule: MoveToThread Failed!\n");

	PatternGenModule *patternIns = PatternGenModule::GetInstance();			// Singleton approach has been used (passing instance to other class)
	if(patternIns->MoveToThread() != 0)
		printf("PatternGenModule: MoveToThread Failed!\n");

	PatternCalibModule *patternCalibIns = PatternCalibModule::GetInstance();
	if(patternCalibIns->MoveToThread() != 0)
		printf("PatternCalibModule: MoveToThread Failed!\n");



//	if(argc > 1)	// SEND COMMAND DIRECTLY FROM CONSOLE ARGUMENTS
//	{
//		for(int c=1; c< argc; ++c)
//		{
//			std::cout << "count "<< argc << " arg = " << argv[c];
//			std::string temp(argv[c]);
//			serialIns->Serial_InitiateCommandDecoding(temp);
//		}
//	}
	AlarmModule *InterUIO = AlarmModule::GetInstance();
	if(InterUIO->MoveToThread() != 0)
		printf("Alarm for UIO: MoveToThread Failed");

//	MemoryMapping MMAP(MemoryMapping::CLUT);
//
//	uint8_t val;
//	printf("\n\nReading CLUT from PL/FPGA\n\n");
//	for(int i=0; i<100; i++)
//	{
//		MMAP.ReadRegister_CLUT(i, &val);
//		printf("%02x ", val);
//
//		if(!((i+1)%20))
//			printf("\n");
//	}


#ifdef _SPI_INTERFACE_
//	SPISlave spi("/dev/spidev1.0");
//	if (!spi.init()) {
//		throw std::runtime_error("Failed to initialize SPI slave");
//	}


	ThreadManager& manager = ThreadManager::getInstance();
	ThreadManager::initializer();
	manager.startThreads();
#endif

#ifdef _WATCHDOG_SOFTRESET_
    const char *watchdog_device = "/dev/watchdog0";
    int timeout = 3; // Default timeout in seconds
    // Initialize the watchdog
	if (watchdog_init(watchdog_device, timeout) < 0) {
		fprintf(stderr, "Failed to initialize watchdog\n");
		return 1;
	}

	// Register signal handlers for graceful shutdown
	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);


#endif


#if 0   //just for spi interface testing
	spi_transfer_data buff;
    memset(&buff.rx_buf,0, BUFFER_SIZE);
	memset(&buff.tx_buf,0, BUFFER_SIZE);
	buff.len = ARRAY_SIZE(&buff.tx_buf);

	try {
		// Create SPI slave instance
		SPISlave spiSlave("/dev/spidev1.0");
//		g_spiSlave = &spiSlave;

		// Set up signal handling
//	        signal(SIGINT, signalHandler);
//	        signal(SIGTERM, signalHandler);

		if (!spiSlave.init()) {
			throw std::runtime_error("Failed to initialize SPI slave");
		}
		printf("SPI Slave initialized. Waiting for data...\n");

		spiSlave.spi_transfer(&buff);


//		ThreadManager& manager = ThreadManager::getInstance();
//		ThreadManager::initializer();
//		manager.startThreads(spiSlave);

		} catch (const std::exception& e) {
			std::cerr << "Error: " << e.what() << std::endl;
			return 1;
	}
#endif

	while(!serialIns->b_endMainSignal) 			// To close main loop and end application when serial thread ends
	{
		sleep(2);
	}

	patternIns->StopThread();
	patternCalibIns->StopThread();
	tempIns->StopThread();
	serialIns->StopThread();
	InterUIO->StopThread();

	DestroyGlobalMutex();

#ifdef _SPI_INTERFACE_
	manager.stopThreads();  	// Shutdown spi interface module
#endif

#ifdef _WATCHDOG_SOFTRESET_
	// Disable the watchdog before exiting
	watchdog_disable();
#endif
	return 0;
}

void InitializeGlobalMutex(void)
{
	if (pthread_mutexattr_init(&mutex_attribute) != 0)
		std::cout << "pthread_mutexattr_init unsuccessful" << std::endl;

	if (pthread_mutexattr_settype(&mutex_attribute, PTHREAD_MUTEX_ERRORCHECK) != 0)
		std::cout << "pthread_mutexattr_settype unsuccessful" << std::endl;

	for(int i =0; i< NUM_OF_MUTEXES; i++)
	{
		if (pthread_mutex_init(&global_mutex[i], &mutex_attribute) != 0)
			std::cout << "pthread_mutex_init unsuccessful" << std::endl;
	}

	cond_result_ready= cond = PTHREAD_COND_INITIALIZER;
	pthread_cond_init(&cond, NULL);
	pthread_cond_init(&cond_result_ready, NULL);

	g_ready1 = g_ready2 = g_ready3= g_ready4 = false;

	g_bNewCommandData = g_bTempChanged = false;
}

void DestroyGlobalMutex(void)
{
	for(int i =0; i< NUM_OF_MUTEXES; i++)
	{
		if (pthread_mutex_destroy(&global_mutex[i]) != 0)
			std::cout << "pthread_mutex_destroy unsuccessful" << std::endl;
	}
}

/*
InitModule::InitModule(void)
{
	g_serialMod = SerialModule::GetInstance();
	g_patternCalib = PatternCalibModule::GetInstance();
	g_tempMonitor = TemperatureMonitor::GetInstance();
	g_patternGen = PatternGenModule::GetInstance();

	int status = HW_Initialize();
	int status = SW_Initialize();
	int status = CFG_Initialize();

	if(status != 0)
	{
		printf(" Initialization Failed. \n");
		g_serialMod->Serial_WritePort("\01INTERNAL_ERROR\04\n");
	}
}

InitModule::~InitModule()
{
	printf("............APPLICATION IS CLOSED............\n\r");
}

*/


