
#include<string.h>
#include <map>
#include <vector>
#include <iostream>


enum FaultsName
 {
	HEATER_1_TEMP=1,
	HEATER_2_TEMP,
	TEC_TEMP,
	ADC_AD7689_ACCESS_FAILURE,
	DAC_AD5624_ACCESS_FAILURE,
	DAC_LTC2620_ACCESS_FAILURE,
	TRANSFER_FAILURE,
	WATCH_DOG_EVENT,
	FIRMWARE_DOWNLOAD_FAILURE,
	WSS_ACCESS_FAILURE,
	FPGA_ACCESS_FAILURE,
	FLASH_ACCESS_FAILURE,
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

struct FaultsAttr
{
	char name[30];
	bool Degraded = false;
	int  DegradedCount = 0;
	bool Raised = false;
	int  RaisedCount = 0;
};

class Fault {
public:
    Fault(const std::string& name,
          const std::string& timestamp,
          bool degraded,
          int degraded_count,
          bool raised,
          int raised_count
//          const std::string& debounce,
//          const std::string& fail_condition,
//          const std::string& degrade_condition
		  );

    // Getters
    std::string getName() const { return name; }
    std::string getTimestamp() const { return timestamp; }
    bool isDegraded() const { return degraded; }
    int getDegradedCount() const { return degraded_count; }
    bool isRaised() const { return raised; }
    int getRaisedCount() const { return raised_count; }
    std::string getDebounce() const { return debounce; }
    std::string getFailCondition() const { return fail_condition; }
    std::string getDegradeCondition() const { return degrade_condition; }

private:
    std::string name;
    std::string timestamp;
    bool degraded = false;
    int degraded_count = 0;
    bool raised = false;
    int raised_count = 0;
    std::string debounce;
    std::string fail_condition;
    std::string degrade_condition;

    void setupHeaterConditions(const std::string& name);
    void setupTECConditions();
    void setupADCConditions();
};

class FaultMonitor {
public:
    static void logFault(FaultsName faultNumber, FaultsAttr& attr);
    static std::string getFaultInfo(int faultNumber);

private:
    static std::map<std::string, std::string> ConstructFaultData(const std::string& faultData);

    static void logToFile(const Fault& fault);
    static bool findFaultEntry(std::ifstream& file, int faultNumber, std::vector<std::string>& entry);
    static std::string formatFaultEntry(const std::vector<std::string>& entry);

    static constexpr size_t MAX_LOG_SIZE = 2 * 1024;  // 2KB limit
    static constexpr const char* LOG_FILE = "/mnt/log.txt";
    static constexpr const char* TEMP_LOG_FILE = "/mnt/log.txt.tmp";

    // ... existing private members ...
    static void rotateLogFile();



};
