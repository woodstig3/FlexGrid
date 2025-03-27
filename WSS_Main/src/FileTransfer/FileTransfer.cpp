/*
 * FileTransfer.cpp

 *
 *  Created on: Nov 15, 2024
 *      Author: Administrator
 */

#ifndef _FILE_TRANSFER_

extern "C" {
    #include <unistd.h>
    #include <cstdlib>
}
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm> // Include this header for std::copy
#include <cstring>
#include <sys/stat.h>
#include <sys/reboot.h>
#include "FileTransfer.h"
#include "Dlog.h"

#define  MAX_PACKET_SIZE  2048

extern ThreadSafeQueue<std::string> packetQueue;
//extern ThreadSafeQueue<uint8_t> fwpacketQueue;   //for file packets transfer between serial and file transfer thread

extern ThreadSafeQueue<bool> ackQueue;

// Function to convert two hexadecimal characters to an integer
uint16_t FileTransfer::hexCharToUint16(char high, char low) {
	return (hexToInt(high) << 8) | hexToInt(low);
}

// Helper function to convert a single hexadecimal character to an integer
uint8_t FileTransfer::hexToInt(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    } else if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    } else if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    } else {
        throw std::invalid_argument("Invalid hexadecimal character");
    }
}

#ifdef _TEST_DOWNLOAD_
// Helper function to convert a single hexadecimal digit to its ASCII representation
char FileTransfer::intToHexChar(uint8_t value) {
    value &= 0x0F; // Ensure we're only dealing with a single hexadecimal digit
    return value < 10 ? '0' + value : 'A' + (value - 10);
}

void FileTransfer::processFirmwarePackets(std::string& packet, int num_bytes)
{

	std::ofstream firmwareFile(m_newFirmwarePath, std::ios::binary | std::ios::ate);
    if (!firmwareFile) {
        std::cerr << "Failed to open firmware file for writing." << std::endl;
        return;
    }
    int totalPackets = (num_bytes + MAX_PACKET_SIZE - 1) / MAX_PACKET_SIZE;
    int currentPacketNumber = 0;
    bool firmwareComplete = false;
//    unsigned char firmware_bytes[MAX_PACKET_SIZE];

    if (packet.find('\03', 0) != std::string::npos)
    { //\0x03<packet_num><FW_bytes>\0x04

		// Extract the packet number
		uint16_t packet_num = hexCharToUint16(packet[1], packet[2]);
		if (packet_num != currentPacketNumber) {
			std::cerr << "Unexpected packet number received. Expected: " << currentPacketNumber
					  << ", Received: " << packet_num << std::endl;
			return;
		}

		// Extract the firmware bytes
		std::vector<uint8_t> firmware_bytes;
		firmware_bytes.reserve(packet.size() - 4); // Reserve space to avoid reallocations
		std::copy(packet.begin() + 3, packet.end() - 1, std::back_inserter(firmware_bytes));


		// Write the firmware bytes to the file
		firmwareFile.write(reinterpret_cast<const char*>(firmware_bytes.data()), firmware_bytes.size());

		// Increment the packet number for the next packet
		currentPacketNumber++;

		// Check if this is the last packet
		if (currentPacketNumber == totalPackets) {
			firmwareComplete = true;
			b_Start_Download = false;
		}
    }
    else
    {
            std::cerr << "Received an unexpected message that is not a firmware packet." << std::endl;
    }


    firmwareFile.close();
/*
    // Perform the integrity check and rename/reboot if successful
    int expectedHash = num_bytes; // Normally should be provided,but here only to compare file size before and after.
    if (check_integrity(m_newFirmwarePath, expectedHash)) {
//        rename_old_firmware();
//        reboot_system();
    } else {
        std::cerr << "Firmware integrity check failed." << std::endl;
    }
*/
}


#endif

bool FileTransfer::check_integrity(const std::string &path, const int expected_size) //const std::string &expected_hash)
{
    // Implement hashing to check firmware integrity (stub logic)
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;

    struct stat fileInfo;

    // Placeholder for hashing function. Replace with actual implementation
//    std::string hash = compute_hash(file); // Placeholder for actual hash function
    if (stat(path.c_str(), &fileInfo) == 0) {
           // Output the file size in bytes
        return expected_size == fileInfo.st_size;
       } else {
           return -1;
       }
//    return hash == expected_hash;
}

void FileTransfer::startFirmwareUpgrade(int numBytes, std::string filePath) {
    if (!m_firmwareUpgradeStarted) {

        this->m_newFirmwarePath = filePath;
        firmwareUpdaterThread = std::make_unique<std::thread>(&FileTransfer::processFirmwarePackets, this, numBytes);
        firmwareUpdaterThread->detach();

        m_firmwareUpgradeStarted = true;
    }
}

void FileTransfer::startLUTFileUpgrade(int numBytes, std::string filePath) {
    if (!m_firmwareUpgradeStarted) {

        this->m_newFirmwarePath = filePath;
        firmwareUpdaterThread = std::make_unique<std::thread>(&FileTransfer::processLUTFilePackets, this, numBytes);
        firmwareUpdaterThread->detach();

        m_firmwareUpgradeStarted = true;
    }
}

void FileTransfer::startHECFileRead(int numBytes, std::string filePath) {

    if (!m_HECFileReadStarted) {

        this->m_HECFilePath = filePath;
        HECFileReadThread = std::make_unique<std::thread>(&FileTransfer::sendHECFile, this, filePath);
        HECFileReadThread->detach();

        m_HECFileReadStarted = true;
    }
}

void FileTransfer::stopFirmwareUpgrade() {
    m_firmwareUpgradeStarted = false;
    // Signal the firmware updater thread to stop (e.g., by pushing a special command)
	b_Start_Download = false;
}

void FileTransfer::stopHECFileRead() {
    m_HECFileReadStarted = false;
    // Signal the firmware updater thread to stop (e.g., by pushing a special command)
	b_Start_Read = false;
}

// Function to replace the old file with the new file
bool FileTransfer::replaceFile(const std::string& oldFilename, const std::string& newFilename) {
    // Check if the new file exists
    if (access(newFilename.c_str(), F_OK) != 0) {
        std::cerr << "New file does not exist: " << newFilename << std::endl;
        return false;
    }

    // Check if the old file exists
    if (access(oldFilename.c_str(), F_OK) != 0) {
        std::cerr << "Old file does not exist: " << oldFilename << std::endl;
        return false;
    }

    // Create a backup of the old file
    std::string backupFilename = oldFilename + ".bak";
    if (rename(oldFilename.c_str(), backupFilename.c_str()) != 0) {
        std::cerr << "Failed to create backup of the old file: " << std::strerror(errno) << std::endl;
        return false;
    }

    // Rename the new file to the old filename
    if (rename(newFilename.c_str(), oldFilename.c_str()) != 0) {
        std::cerr << "Failed to replace old file with the new file: " << std::strerror(errno) << std::endl;
        // Clean up the backup file if cannot use backup to restore the old file
        if (rename(backupFilename.c_str(), oldFilename.c_str()) != 0) {
            std::cerr << "Failed to revert backup to the old file: " << std::strerror(errno) << std::endl;
        }
        remove(backupFilename.c_str()); // Clean up the backup file
        return false;
    }

    // Optionally, delete the backup file
    if (remove(backupFilename.c_str()) != 0) {
        std::cerr << "Warning: Failed to delete the old backup file: " << std::strerror(errno) << std::endl;
        // This is a warning, not an error, as the main operation (replacing the file) was successful
    }

    return true;
}


/*
 *
#include <iostream>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>

// Mutex to synchronize file access
std::mutex fileMutex;

// Function to replace the old file with the new file with retries
bool replaceFileWithRetry(const std::string& oldFilename, const std::string& newFilename, int maxRetries, std::chrono::milliseconds retryDelay) {
    for (int attempt = 0; attempt < maxRetries; ++attempt) {
        // Attempt to lock the file mutex; will block until the lock is acquired
        std::lock_guard<std::mutex> lock(fileMutex);

        // Check if the old file exists
        if (access(oldFilename.c_str(), F_OK) != 0) {
            std::cerr << "Old file does not exist: " << oldFilename << std::endl;
            return false;
        }

        // Create a backup of the old file
        std::string backupFilename = oldFilename + ".bak";
        if (rename(oldFilename.c_str(), backupFilename.c_str()) != 0) {
            std::cerr << "Failed to create backup of the old file: " << strerror(errno) << std::endl;
            return false;
        }

        // Rename the new file to the old filename
        if (rename(newFilename.c_str(), oldFilename.c_str()) != 0) {
            std::cerr << "Failed to replace old file with the new file: " << strerror(errno) << std::endl;

            // If the rename fails, try to revert the backup
            if (rename(backupFilename.c_str(), oldFilename.c_str()) != 0) {
                std::cerr << "Failed to revert backup to the old file: " << strerror(errno) << std::endl;
            }
            // Clean up the backup file
            remove(backupFilename.c_str());

            // If the attempt failed, sleep for the specified delay before retrying
            std::this_thread::sleep_for(retryDelay);
            continue; // Retry
        }

        // Optionally, delete the backup file
        if (remove(backupFilename.c_str()) != 0) {
            std::cerr << "Warning: Failed to delete the old backup file: " << strerror(errno) << std::endl;
        }

        return true; // Success
    }

    // If all attempts fail, return false
    return false;
}

int main() {
    // Example usage of the replaceFileWithRetry function
    std::string oldFilename = "Att_LUT_M1.csv";
    std::string newFilename = "Att_LUT_M1_New.csv";
    int maxRetries = 5;
    std::chrono::milliseconds retryDelay(100); // 100ms delay between retries

    // Replace the old file with the new file
    bool success = replaceFileWithRetry(oldFilename, newFilename, maxRetries, retryDelay);

    if (success) {
        std::cout << "File successfully replaced: " << oldFilename << std::endl;
    } else {
        std::cout << "Failed to replace file after " << maxRetries << " attempts: " << oldFilename << std::endl;
    }

    return 0;
}

 *
 * */

void FileTransfer::processFirmwarePackets(int num_bytes ) {
	//create new file if a new download in binary and append mode
	std::ofstream firmwareFile;
	firmwareFile.open(m_newFirmwarePath, std::ios::trunc | std::ios::binary);
	if (!firmwareFile) {
		std::cerr << "Failed to open new firmware file for writing." << std::endl;
		return;
	}

    int totalPackets = (num_bytes + MAX_PACKET_SIZE - 1) / MAX_PACKET_SIZE;
    int currentPacketNumber = 0;
    bool firmwareComplete = false;

    while (!firmwareComplete && m_firmwareUpgradeStarted == true) {
#ifdef _WATCHDOG_SOFTRESET_
		watchdog_feed();
#endif
    	std::string packet;
        if (packetQueue.empty()) {
            // Optionally sleep for a short period to avoid busy-waiting
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        packetQueue.pop(packet);
        std::cerr << "Firmware Packet received: " << packet.size() << std::endl;


        if (packet.find('\03', 0) != std::string::npos)
		{ //\0x03<packet_num><FW_bytes>\0x04

			// Extract the packet number
            uint16_t packet_num = (static_cast<uint8_t>(packet[1]) << 8) | static_cast<uint8_t>(packet[2]);
			//uint16_t packet_num = hexCharToUint16(packet[1], packet[2]);
			if (packet_num != currentPacketNumber) {
				std::cerr << "Unexpected packet number received. Expected: " << currentPacketNumber
						  << ", Received: " << packet_num << std::endl;
				continue;
			}

			// Extract the firmware bytes
			std::vector<char> firmware_bytes;
			firmware_bytes.reserve(packet.size() - 4); // Reserve space to avoid reallocations
			std::copy(packet.begin() + 3, packet.end() - 1, std::back_inserter(firmware_bytes));


			// Write the firmware bytes to the file
//		    uint8_t uint8_array[firmware_bytes.size()];

/*		    for (size_t i = 0; i < firmware_bytes.size(); ++i) {
		        uint8_array[i] = static_cast<uint8_t>(firmware_bytes[i]);
		        firmwareFile.write(reinterpret_cast<const char*>(&uint8_array[i]), sizeof(uint8_t));
		    }*/
			firmwareFile.write(reinterpret_cast<const char*>(firmware_bytes.data()), firmware_bytes.size());

			//
			if (firmwareFile.bad() || !firmwareFile.good()) {
			    std::cerr << "Error writing to file: " << m_newFirmwarePath << std::endl;
			    continue;
			}
			// Check if this is the last packet
			else if (currentPacketNumber == totalPackets-1) {
				firmwareComplete = true;
				std::cerr << "File download successfully completed." << std::endl;
			}
			else {
				// Increment the packet number for the next packet
				currentPacketNumber++;
			}

		} else {
            std::cerr << "Received an unexpected message that is not a firmware packet." << std::endl;
        }
    }

	stopFirmwareUpgrade();
    firmwareFile.close();

    // Perform the integrity check and rename/reboot if successful
//    int expectedHash = num_bytes; // Normally should be provided,but here only to compare file size before and after.
//    if (check_integrity(m_newFirmwarePath, expectedHash)) {
//    	if(!replaceFile(m_oldFirmwarePath, m_newFirmwarePath))
//    		std::cerr << "Error replacing file: " << m_oldFirmwarePath << std::endl;
//        reboot_system();
//    } else {
//        std::cerr << "Firmware integrity check failed." << std::endl;
//    }
}

void FileTransfer::processLUTFilePackets(int num_bytes ) {
	//create new file if a new download in binary and append mode
	std::ofstream firmwareFile;
	firmwareFile.open(m_newFirmwarePath, std::ios::trunc | std::ios::binary);
	if (!firmwareFile) {
		std::cerr << "Failed to open new lut file for writing." << std::endl;
		return;
	}

    int totalPackets = (num_bytes + MAX_PACKET_SIZE - 1) / MAX_PACKET_SIZE;
    int currentPacketNumber = 0;
    bool firmwareComplete = false;

    while (!firmwareComplete && m_firmwareUpgradeStarted == true) {
#ifdef _WATCHDOG_SOFTRESET_
		watchdog_feed();
#endif
    	std::string packet;
        if (packetQueue.empty()) {
            // Optionally sleep for a short period to avoid busy-waiting
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        packetQueue.pop(packet);
        std::cerr << "LUT File Packet received: " << packet.size() << std::endl;


        if (packet.find('\03', 0) != std::string::npos)
		{ //\0x03<packet_num><FW_bytes>\0x04

			// Extract the packet number
            uint16_t packet_num = (static_cast<uint8_t>(packet[1]) << 8) | static_cast<uint8_t>(packet[2]);
			//uint16_t packet_num = hexCharToUint16(packet[1], packet[2]);
//        	uint16_t packet_num = hexCharToUint16(packet.substr(1,4));
			if (packet_num != currentPacketNumber) {
				std::cerr << "Unexpected packet number received. Expected: " << currentPacketNumber
						  << ", Received: " << packet_num << std::endl;
	            FaultsAttr attr = {0};
	            attr.Raised = true;
	            attr.RaisedCount += 1;
	            attr.Degraded = false;
	            attr.DegradedCount = attr.RaisedCount;
	            FaultMonitor::logFault(FIRMWARE_DOWNLOAD_FAILURE, attr);
				continue;
			}

			// Extract the lut file ascii code bytes
			std::vector<uint8_t> firmware_bytes;
			firmware_bytes.reserve(packet.size() - 4); // Reserve space to avoid reallocations
			std::copy(packet.begin() + 3, packet.end() - 1, std::back_inserter(firmware_bytes));


			// Write the firmware bytes to the file
			firmwareFile.write(reinterpret_cast<const char*>(firmware_bytes.data()), firmware_bytes.size());

			//
			if (firmwareFile.bad() || !firmwareFile.good()) {
			    std::cerr << "Error writing to file: " << m_newFirmwarePath << std::endl;
			    FaultsAttr attr = {0};
				attr.Raised = true;
				attr.RaisedCount += 1;
				attr.Degraded = false;
				attr.DegradedCount = attr.RaisedCount;
				FaultMonitor::logFault(FLASH_ACCESS_FAILURE, attr);
			    continue;
			}
			// Check if this is the last packet
			else if (currentPacketNumber == totalPackets-1) {
				firmwareComplete = true;
				std::cerr << "LUT File download successfully completed in " << totalPackets << " packets" << std::endl;
			}
			else {
				// Increment the packet number for the next packet
				currentPacketNumber++;
			}

		} else {
            std::cerr << "Received an unexpected message that is not a lut file packet." << std::endl;
            FaultsAttr attr = {0};
            attr.Raised = true;
            attr.RaisedCount += 1;
            attr.Degraded = false;
            attr.DegradedCount = attr.RaisedCount;
            FaultMonitor::logFault(FIRMWARE_DOWNLOAD_FAILURE, attr);
        }
    }

	stopFirmwareUpgrade();
    firmwareFile.close();

    // Perform the integrity check and rename/reboot if successful
//    int expectedHash = num_bytes; // Normally should be provided,but here only to compare file size before and after.
//    if (check_integrity(m_newFirmwarePath, expectedHash)) {
//    	if(!replaceFile(m_oldFirmwarePath, m_newFirmwarePath))
//    		std::cerr << "Error replacing file: " << m_oldFirmwarePath << std::endl;
//        reboot_system();
//    } else {
//        std::cerr << "Firmware integrity check failed." << std::endl;
//    }
}

// Function to read a HEC file and send it through UART
void FileTransfer::sendHECFile(const std::string& filename) {
    std::ifstream hecFile(filename, std::ios::binary);
    if (!hecFile) {
        throw std::runtime_error("Unable to open HEC file: " + filename);
    }

    // Read the entire file into a vector
    std::vector<uint8_t> fileData((std::istreambuf_iterator<char>(hecFile)), std::istreambuf_iterator<char>());
    size_t totalSize = fileData.size();

    // Packet parameters
    const size_t payloadSize = 2048; // Max payload size
    uint16_t seqNo = 0;

    // Sending packets
    for (size_t offset = 0; offset < totalSize; offset += payloadSize) {
        size_t bytesToSend = std::min(payloadSize, totalSize - offset);

        // Create packet
        std::vector<char> packet;
        packet.push_back(0x03); // Start marker
        packet.push_back((seqNo >> 8) & 0xFF); // Sequence number (high byte)
        packet.push_back(seqNo & 0xFF); // Sequence number (low byte)
        packet.insert(packet.end(), fileData.begin() + offset, fileData.begin() + offset + bytesToSend);
        packet.push_back(0x04); // End marker

 //       std::string s(packet.begin(),packet.end());
 //       packetQueue.push(s);
        packetQueue.push(std::string(packet.data()));

        // Wait for ACK
        bool ackReceived = false;
        ackQueue.pop(ackReceived);
        if (!ackReceived) {
            std::cerr << "ACK not received, resending packet." << std::endl;
            offset -= payloadSize; // Resend the current packet
            continue;
        }

        seqNo++;
    }
    stopHECFileRead();
    std::cout << "[READ] HEC file transfer complete." << std::endl;
}


void FileTransfer::handlePrepareCommand(int numBytesToReceive, std::string strOldPath, std::string strNewPath)
{
   // Extract the firmware size
//	packetQueue = std::make_unique<ThreadSafeQueue<std::string>>();

	file_num_bytes = numBytesToReceive;
	m_oldFirmwarePath = strOldPath;
	m_newFirmwarePath = strNewPath;

	b_Start_Download = true;
	b_Bin_Download = false;

	startLUTFileUpgrade(numBytesToReceive,strNewPath);
}

void FileTransfer::handleBinPrepareCommand(int numBytesToReceive, std::string strOldPath, std::string strNewPath)
{
   // Extract the firmware size
//	packetQueue = std::make_unique<ThreadSafeQueue<std::string>>();

	file_num_bytes = numBytesToReceive;
	m_oldFirmwarePath = strOldPath;
	m_newFirmwarePath = strNewPath;

	b_Start_Download = true;
	b_Bin_Download = true;

	startFirmwareUpgrade(numBytesToReceive,strNewPath);
}

void FileTransfer::handlePrepareToRead(int numBytesToRead, std::string strPath)
{
	file_num_bytes = numBytesToRead;
	m_HECFilePath = strPath;
	b_Start_Read = true;
	startHECFileRead(numBytesToRead,strPath);
}

void FileTransfer::handleSwitchCommand()
{
    // SWITCH
    std::ofstream flag_file("/mnt/firmware_flag");
    if (flag_file) {
        flag_file << "SWITCH";
        flag_file.close();
    } else {
        std::cerr << "ERROR: Cannot write to firmware_flag" << std::endl;
        return;
    }
    std::cout << "SWITCH flag set. System will reboot." << std::endl;
}

void FileTransfer::handleCommitCommand()
{
	//handle fw new version taking into effect by restart
    try {
        // reboot
        reboot_system();
    } catch (const std::exception& e) {
        std::cerr << "Commit error: " << e.what() << std::endl;
    }
}

void FileTransfer::handleRevertCommand() {
    // REVERT
    std::ofstream flag_file("/mnt/firmware_flag");
    if (flag_file) {
        flag_file << "REVERT";
        flag_file.close();
    } else {
        std::cerr << "ERROR: Cannot write to firmware_flag" << std::endl;
        return;
    }
    //
    std::cout << "REVERT flag set. System will reboot." << std::endl;
}

void FileTransfer::reboot_system() {

	sync(); // Ensure that data is written to disk
	if (reboot(RB_AUTOBOOT) == -1) {
		throw std::runtime_error("Linux reboot failed: " + std::string(strerror(errno)));
	}

}

#endif
