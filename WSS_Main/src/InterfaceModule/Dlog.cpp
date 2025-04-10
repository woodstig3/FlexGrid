
#include <string>
#include <chrono>
#include <fstream>
#include <cstring>
#include <sstream>
#include <iostream>
#include <sys/stat.h>
#include "Dlog.h"

std::string FNmask[] =
{
	"HEATER_1_TEMP",
	"HEATER_2_TEMP",
	"TEC_TEMP",
	"ADC_AD7689_ACCESS_FAILURE",
	"DAC_AD5624_ACCESS_FAILURE",
	"DAC_LTC2620_ACCESS_FAILURE",
	"TRANSFER_FAILURE",
	"WATCH_DOG_EVENT",
	"FIRMWARE_DOWNLOAD_FAILURE",
	"WSS_ACCESS_FAILURE",
	"FPGA_ACCESS_FAILURE",
	"FLASH_ACCESS_FAILURE",
	"FLASH_PROGRAMMING_ERROR",
	"EEPROM_ACCESS_FAILURE",
	"EEPROM_CHECKSUM_FAILURE",
	"CALIB_FILE_MISMATCH",
	"CALIB_FILE_MISSING",
	"CALIB_FILE_CHECKSUM_ERROR",
	"FW_FILE_CHECKSUM_ERROR",
	"FPGA1_DOWNLOAD_FAILURE",
	"FPGA2_DOWNLOAD_FAILURE"
};

Fault::Fault(const std::string& name,
             const std::string& timestamp,
             bool degraded,
             int degraded_count,
             bool raised,
             int raised_count
//             const std::string& cdebounce,
//             const std::string& cfail_condition,
//             const std::string& cdegrade_condition
			 )
    : name(name),
      timestamp(timestamp),
      degraded(degraded),
      degraded_count(degraded_count),
      raised(raised),
      raised_count(raised_count) {
    if (name == "HEATER_1_TEMP" || name == "HEATER_2_TEMP") {
        setupHeaterConditions(name);
    } else if (name == "TEC_TEMP") {
        setupTECConditions();
    } //else if (...)   to add as required by ZTE serial commands
}

void Fault::setupHeaterConditions(const std::string& name) {
    std::ostringstream oss;

    // Build debounce string
    oss << "Debounce: Raise=" << 3 << " Clear=" << 3 << std::endl;
    debounce = oss.str(); // Assign to debounce
    oss.str(""); // Clear the stream for reuse

    // Build fail_condition string
    oss << "FailCondition: HighThresh=" << 65 << " HighHyst=" << 1
        << " LowThresh=" << 40 << " LowHyst=" << 1 << std::endl;
    fail_condition = oss.str(); // Assign to fail_condition
    oss.str(""); // Clear the stream for reuse

    // Build degrade_condition string
    oss << "DegradeCondition: HighThresh=" << 50 << " HighHyst=" << 1
        << " LowThresh=" << 45 << " LowHyst=" << 1 << std::endl;
    degrade_condition = oss.str(); // Assign to degrade_condition
}

void Fault::setupTECConditions() {
    std::ostringstream oss;

    // Build debounce string
    oss << "Debounce: Raise=" << 3 << " Clear=" << 3 << std::endl;
    debounce = oss.str(); // Assign to debounce
    oss.str(""); // Clear the stream for reuse

    // Build fail_condition string
    oss << "FailCondition: TEC temperature exceeds target temperature by " << 10 << std::endl;
    fail_condition = oss.str(); // Assign to fail_condition
    oss.str(""); // Clear the stream for reuse

    // Build degrade_condition string
    oss << "DegradeCondition: TEC temperature exceeds target temperature by " << 5 << std::endl;
    degrade_condition = oss.str(); // Assign to degrade_condition
}

void Fault::setupADCConditions() {
    std::ostringstream oss;

    // Build debounce string
    oss << "Debounce: Raise=" << 3 << " Clear=" << 3 << std::endl;
    debounce = oss.str(); // Assign to debounce
    oss.str(""); // Clear the stream for reuse

    // Build fail_condition string
    oss << "FailCondition: Failed access to the ADC " << 7490 << std::endl;
    fail_condition = oss.str(); // Assign to fail_condition
    oss.str(""); // Clear the stream for reuse

    // Build degrade_condition string
    oss << "DegradeCondition: Unstable access to the ADC " << 7490 << std::endl;
    degrade_condition = oss.str(); // Assign to degrade_condition
}

//continue to add Conditions descriptions below:


void FaultMonitor::logFault(FaultsName faultNumber, FaultsAttr& attr) {

    std::time_t now = std::time(nullptr);
    char buffer[80];
    std::strftime(buffer, sizeof(buffer), "%a %b %d %H:%M:%S %Y", std::localtime(&now));
	// std::time_t now = std::time(nullptr); // Get current time in seconds
	// Create Fault object

	Fault fault(
        FNmask[faultNumber],                    //faultInfo["Name"] =
        buffer,                                 //faultInfo["Timestamp"]
		//std::ctime(&now),                       //faultInfo["Timestamp"]
        attr.Degraded,                          //faultInfo["Degraded"] == "TRUE",
        attr.DegradedCount,                     //std::stoi(faultInfo["DegradedCount"]),
        attr.Raised,                            //faultInfo["Raised"] == "TRUE",
        attr.RaisedCount                       //std::stoi(faultInfo["RaisedCount"]),
    );

    // Log the fault
    logToFile(fault);
}

void FaultMonitor::logToFile(const Fault& fault) {
    // std::ofstream logFile(LOG_FILE, std::ios::out | std::ios::app);
    // if (!logFile) {
    //     std::cerr << "Error opening log file" << std::endl;
    //     return;
    // }

    const size_t MAX_LOG_SIZE = 1024 * 1024; // 1MB limit
    std::ios_base::openmode mode = std::ios::app; // Default append mode
    // Check existing file size
    struct stat file_stat;
    if (stat(LOG_FILE, &file_stat) == 0) {
        if (file_stat.st_size >= MAX_LOG_SIZE) {
            mode = std::ios::trunc; // Switch to truncate mode when over limit
            std::cout << "Log reached " << file_stat.st_size 
                    << " bytes, truncating..." << std::endl;
        }
    } else if (errno != ENOENT) { // Ignore file-not-exist errors
        std::cerr << "Log size check failed: " << strerror(errno) << std::endl;
    }
    // Open file (append or truncate mode)
    std::ofstream logFile(LOG_FILE, std::ios::out | mode);
    if (!logFile) {
        std::cerr << "Error opening log file" << std::endl;
        return;
    }

    logFile << "Fault Name: " << fault.getName() << "\n"
            << "Timestamp: " << fault.getTimestamp() << "\n"
            << "Degraded: " << (fault.isDegraded() ? "TRUE" : "FALSE") << "\n"
            << "Degraded Count: " << fault.getDegradedCount() << "\n"
            << "Raised: " << (fault.isRaised() ? "TRUE" : "FALSE") << "\n"
            << "Raised Count: " << fault.getRaisedCount() << "\n"
            << "Debounce: " << fault.getDebounce() << "\n"
            << "Fail Condition: " << fault.getFailCondition() << "\n"
            << "Degrade Condition: " << fault.getDegradeCondition() << "\n"
            << std::string(40, '-') << "\n";
}

std::string FaultMonitor::getFaultInfo(int faultNumber) {
    // Check if faultNumber is valid
    if (faultNumber < 1 || faultNumber > sizeof(FNmask)/sizeof(FNmask[0])) {
        return "Error: Invalid fault number";
    }
    std::ifstream logFile(LOG_FILE);
    if (!logFile) {
        return "Error: Could not open log file";
    }
    // Get target fault name from FNmask array
    std::string targetFaultName = "Fault Name: " + FNmask[faultNumber - 1];
    std::vector<std::string> faultEntry;
    // Search for the last occurrence of the target fault entry
    if (!findLastFaultEntry(logFile, targetFaultName, faultEntry)) {
        return "Error: Fault not found";
    }

    return formatFaultEntry(faultEntry);
}

bool FaultMonitor::findLastFaultEntry(std::ifstream& file, const std::string& targetFaultName, std::vector<std::string>& entry) {
    std::string line;
    std::vector<std::string> currentEntry;
    bool isTargetEntry = false;
    bool found = false;
    while (std::getline(file, line)) {
        // Check if the line is a "Fault Name:" line
        if (line.find("Fault Name:") != std::string::npos) {
            // If it matches the target fault name, start new entry
            if (line == targetFaultName) {
                currentEntry.clear();
                isTargetEntry = true;
                found = true;  // Mark that we've found at least one occurrence
            } else {
                isTargetEntry = false;
            }
        }
        // Record content if it's the target entry
        if (isTargetEntry) {
            currentEntry.push_back(line);
        }
        // Check for entry end marker (40 '-')
        if (line.find("----------------------------------------") != std::string::npos) {
            // Save current entry if it's the target and properly closed
            if (isTargetEntry) {
                entry = currentEntry;
            }
            isTargetEntry = false;
        }
    }
    // Handle case where entry is not properly closed at EOF
    if (isTargetEntry) {
        entry = currentEntry;
    }
    return found;
}

std::string FaultMonitor::formatFaultEntry(const std::vector<std::string>& entry) {
    std::map<std::string, std::string> faultInfo;

    // Parse the entry lines into key-value pairs
    for (const auto& line : entry) {
        size_t colonPos = line.find(':');
        if (colonPos != std::string::npos) {
            std::string key = line.substr(0, colonPos);
            std::string value = line.substr(colonPos + 2);  // Skip ": "

            // Remove "Fault " from "Fault Name"
            if (key == "Fault Name") {
                key = "Name";
            }

            faultInfo[key] = value;
        }
    }

    // Format the output in the same format as the command response
    std::ostringstream output;
    output << "Name:" << faultInfo["Name"] << " "
           << "Timestamp:" << faultInfo["Timestamp"] << " "
           << "Degraded:" << faultInfo["Degraded"] << " "
           << "DegradedCount:" << faultInfo["Degraded Count"] << " "
           << "Raised:" << faultInfo["Raised"] << " "
           << "RaisedCount:" << faultInfo["Raised Count"] << " "
           << "Debounce:" << faultInfo["Debounce"] << " "
           << "FailCondition:" << faultInfo["Fail Condition"] << " "
           << "DegradeCondition:" << faultInfo["Degrade Condition"];

    return output.str();
}
