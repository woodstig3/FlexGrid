/*
 * GlobalMutexes.h
 *
 *  Created on: Jan 30, 2023
 *      Author: mib_n
 */

#ifndef SRC_GLOBALVARIABLES_H_
#define SRC_GLOBALVARIABLES_H_

#include <pthread.h>

#define _DEVELOPMENT_MODE_				// <---------- Uncomment it during production mode, Comment if when launching to customer
//#define _FETCH_PATTERN_
#define _TWIN_WSS_						// If not defined then only Module.1 will exist for user to set.
#define _XILINX_

//#define _OCM_SCAN_                     //added for OCM function blocks
//#define _SPI_INTERFACE_
//#define _WATCHDOG_SOFTRESET_

const double PI = 3.141592653589793238;

#ifdef _120_WL_
#define VENDOR_FREQ_RANGE_LOW  191124.999999
#define VENDOR_FREQ_RANGE_HIGH	 196275.000001
#define WHOLE_BANDWIDTH 5150
#else
#define VENDOR_FREQ_RANGE_LOW  190574.999999
#define VENDOR_FREQ_RANGE_HIGH	 196725.000001
#define WHOLE_BANDWIDTH 6150
#endif

#define VENDOR_BW_RANGE_LOW 37.49
#define VENDOR_BW_RANGE_HIGH 500.01

#define VENDOR_MAX_PORT 23
#define VENDOR_MIN_BW 6.249

#define MAX_ATT_BLOCK   15

constexpr int g_LCOS_Height{1080};	// compile-time constant 1080 (change HDMI resolution too)
constexpr int g_LCOS_Width{1952};	// compile-time constant 1952
constexpr int g_Total_Channels{1024};	// compile-time constant 96

constexpr double g_Max_Normal_Temperature{72.6}; //grating temperature
constexpr double g_Min_Normal_Temperature{51.6}; //grating temperature


// CmdDecoder Special
#define _TEST_OVERLAP_
#define _MANDATORY_ATTRIB_

// Pattern Gen. Special. If enabled the pattern will be draw from LEFT to RIGHT direction F2<---F1
//#define _FLIP_DISPLAY_

// Threads Special
enum mutexID{LOCK_SERIAL_WRITE, LOCK_CHANNEL_DS, LOCK_MODULE_DS, LOCK_PATTERN_DS, lOCK_CALIB_PARAMS, LOCK_DEVMODE_VARS, LOCK_CLOSE_SERIALLOOP
			, LOCK_CLOSE_CALIBLOOPS, LOCK_CLOSE_PATTGENLOOP, LOCK_CLOSE_TEMPLOOP, LOCK_REG2_REGISTERS, LOCK_OCM_REGISTERS, LOCK_SEQ_REGISTERS, LOCK_CLUT_REGISTERS
			, LOCK_TEC_REGISTERS, LOCK_TEMP_CHANGED_FLAG, LOCK_TRANSFER_FINISH_FLAG, LOCK_SERIAL_INSTANCE, LOCK_TEMP_INSTANCE, LOCK_CALIB_INSTANCE, LOCK_PATTERN_INSTANCE
			, LOCK_UIO_ALARM,NUM_OF_MUTEXES};

extern pthread_mutex_t global_mutex[NUM_OF_MUTEXES];
extern pthread_mutexattr_t mutex_attribute;
extern pthread_cond_t cond, cond_result_ready;

extern bool g_bNewCommandData;
extern bool g_bTempChanged;

extern bool g_ready1, g_ready2, g_ready3, g_ready4;



/*
class InitModule{

public:
	InitModule();
	virtual ~InitModule();
private:
	SerialModule 	   *g_serialMod{nullptr};  // create an instance of CmdDecoder so that we can access its member and data via singleton method
	PatternCalibModule *g_patternCalib{nullptr};
	TemperatureMonitor *g_tempMonitor{nullptr};
	CmdDecoder         *cmd_decoder{nullptr};
	PatternGenModule   *g_patternGen{nullptr};

	int HW_Initialization(void);
	int SW_Initialization(void);
	int CFG_Initialization(void);


}
*/

#endif /* SRC_GLOBALVARIABLES_H_ */
