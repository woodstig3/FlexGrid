/*
 * spiCmdDecoder.cpp
 *
 *  Created on: Dec 27, 2024
 *      Author: Administrator
 */

#include <iostream>
#include <vector>
#include <cstdint>
#include <memory>
#include <fstream>
#include <sstream>
#include <map>
#include <string>
#include <bitset>
#include <cstdlib>    
#include <unistd.h>   
#include <sys/types.h>  
#include <signal.h>     

#include "SpiCmdDecoder.h"
#include "TemperatureMonitor.h"

extern double g_direct_LCOS_Temp;
extern double g_direct_Hearter2_Temp;
extern bool g_Calib_File_Failure;
extern bool g_OpticalControl_Failure;


Config_For_Prod SpiCmdDecoder::conf_spi{0};
Operational_Status SpiCmdDecoder:: oss{0};
Hardware_Status SpiCmdDecoder:: hss{0};
SPAConfigStruct SPASlicePortAttenuationCommand::spaConfStruct[];

SpiCmdDecoder::SpiCmdDecoder()
{
        oss.isHardwareError = oss.isLatchedError = oss.isOpticsDisabled = oss.isStartUpActive = oss.isPending = false;
        oss.isModuleRestarted = oss.isReady = oss.isOpticsNotReady = true;

		loadProdIniConf();

    	commandHandlers[0x0001] = [this]() { return std::make_unique<SpiNOPCmdCommand>(this); };  //No Operation
    	commandHandlers[0x0002] = [this]() { return std::make_unique<SpiResetCmdCommand>(this); };  //Soft Reset
		commandHandlers[0x0003] = [this]() { return std::make_unique<SpiStoreCmdCommand>(this); };  //Store
		commandHandlers[0x0004] = [this]() { return std::make_unique<SpiSUSQueryCommand>(this); };  //SUSQuery
		commandHandlers[0x0005] = [this]() { return std::make_unique<SpiSFDCmdCommand>(this); };  //SFD
		commandHandlers[0x0006] = [this]() { return std::make_unique<SpiSLSCmdCommand>(this); };  //SLS
		commandHandlers[0x0007] = [this]() { return std::make_unique<SpiHWRQueryCommand>(this); };  //HWRQuery
		commandHandlers[0x0008] = [this]() { return std::make_unique<SpiFWRQueryCommand>(this); };  //FWRQuery
		commandHandlers[0x0009] = [this]() { return std::make_unique<SpiSNOQueryCommand>(this); };  //SNOQuery
		commandHandlers[0x000A] = [this]() { return std::make_unique<SpiMFDQueryCommand>(this); };  //MFDQuery
		commandHandlers[0x000B] = [this]() { return std::make_unique<SpiLBLQueryCommand>(this); };  //LBLQuery
		commandHandlers[0x000C] = [this]() { return std::make_unique<SpiMIDAssignCmdCommand>(this); };  //MIDAssign
		commandHandlers[0x000D] = [this]() { return std::make_unique<SpiMIDQueryCommand>(this); };  //MIDQuery
		commandHandlers[0x000F] = [this]() { return std::make_unique<SpiOSSQueryCommand>(this); };  //OSSQuery
		commandHandlers[0x0010] = [this]() { return std::make_unique<SpiHSSQueryCommand>(this); };  //HSSQuery
		commandHandlers[0x0011] = [this]() { return std::make_unique<SpiLSSQueryCommand>(this); };  //LSSQuery
		commandHandlers[0x0012] = [this]() { return std::make_unique<SpiCLECmdCommand>(this); };  //CLE
		commandHandlers[0x0013] = [this]() { return std::make_unique<SpiCSSQueryCommand>(this); };  //CSSQuery
		commandHandlers[0x0014] = [this]() { return std::make_unique<SpiISSQueryCommand>(this); };  //ISSQuery
    	commandHandlers[0x0015] = [this]() { return std::make_unique<SPASlicePortAttenuationCommand>(this); }; //SPA
		commandHandlers[0x0016] = [this]() { return std::make_unique<SPAQuerySlicePortAttenuationCommand>(this); };  //SPA
		commandHandlers[0x0017] = [this]() { return std::make_unique<SpiFWTCmdCommand>(this); };  //FWT
		commandHandlers[0x0018] = [this]() { return std::make_unique<SpiFWSCmdCommand>(this); };  //FWS
		commandHandlers[0x0019] = [this]() { return std::make_unique<SpiFWLCmdCommand>(this); };  //FWL
		commandHandlers[0x001A] = [this]() { return std::make_unique<SpiFWPQueryCommand>(this); };  //FWPQuery
		commandHandlers[0x001B] = [this]() { return std::make_unique<SpiFWECmdCommand>(this); };  //FWE

        // Register other command handlers as needed
}

// Main method to process the incoming SPI command packet
std::vector<uint8_t> SpiCmdDecoder::processSPIPacket(const SPICommandPacket& commandPacket) {
	uint32_t opcode = commandPacket.opcode;
	uint32_t seqNo = commandPacket.seqNo;

	auto it = commandHandlers.find(opcode);
	if (it != commandHandlers.end()) {
		std::unique_ptr<BaseCommand> command = it->second(); // Create command instance

		// Parse the command packet
		if (command->parse(commandPacket)) {
			// Process the command
			return command->process(seqNo);
		} else {
			return constructErrorReply(0x02); // Command parsing error
		}
	} else {
		return constructErrorReply(0x01); // Unknown opcode error
	}
}

// Function to construct SPI reply packet
std::vector<uint8_t> SpiCmdDecoder:: constructSPIReplyPacket(const SPIReplyPacket& replyPacket) {
	std::vector<uint8_t> rawData;
    // 1. Serialize the header (0x00~0x0F)
    auto appendUint32 = [](std::vector<uint8_t>& buf, uint32_t value) {
        buf.push_back((value >> 24) & 0xFF);
        buf.push_back((value >> 16) & 0xFF);
        buf.push_back((value >> 8) & 0xFF);
        buf.push_back(value & 0xFF);
    };
    appendUint32(rawData, replyPacket.spiMagic); // 0x00-0x03
    appendUint32(rawData, replyPacket.length);   // 0x04-0x07
    appendUint32(rawData, replyPacket.seqNo);    // 0x08-0x0B
    appendUint32(rawData, replyPacket.comres);   // 0x0C-0x0F
	// 2. Calculate and append CRC1 (0x00~0x0F)
	uint32_t crc1 = calculateCRC1(rawData.data());
	appendUint32BigEndian(rawData, crc1); // ensure bigend
    // 3. Append data (if any)
    if (!replyPacket.data.empty() && replyPacket.length > 20) {
        rawData.insert(rawData.end(), replyPacket.data.begin(), replyPacket.data.end());
    }

    // 4. Calculate and append CRC2 (0x00~[LENGTH-5])
    uint32_t crc2 = 0;
    if (replyPacket.length > 20) {
        crc2 = calculateCRC2(rawData.data(), rawData.size());
        appendUint32BigEndian(rawData, crc2); // ensure bigend
    }
	// print packet
    std::cout << "SPI Reply Packet Content:" << std::endl;
    std::cout << "  SPI Magic: 0x" << std::hex << replyPacket.spiMagic << std::endl;
    std::cout << "  Length: " << std::dec << replyPacket.length << std::endl;
    std::cout << "  Sequence Number: " << replyPacket.seqNo << std::endl;
    std::cout << "  Command Result: " << replyPacket.comres << std::endl;
    std::cout << "  Data: ";
    for (const auto& byte : replyPacket.data) {
        std::cout << "0x" << std::hex << static_cast<int>(byte) << " ";
    }
    std::cout << std::endl;
    std::cout << "  CRC1: 0x" << std::hex << crc1 << std::endl;
    if (replyPacket.length > 20) {
        std::cout << "  CRC2: 0x" << std::hex << crc2 << std::endl;
    }
	// // Print rawData byte by byte
	// std::cout << "  Raw Data (hex): ";
	// for (const auto& byte : rawData) {
	// 	std::cout << "0x" << std::hex << static_cast<int>(byte) << " ";
	// }
	// std::cout << std::endl;
	std::cout << std::dec; // restore decimal output
    return rawData;
}
// Method to construct an error reply packet based on error codes
std::vector<uint8_t> SpiCmdDecoder::constructErrorReply(uint8_t errorCode) {
	// Error handling implementation...
	SPIReplyPacket replyPacket;
	replyPacket.spiMagic = SPIMAGIC;

	replyPacket.seqNo = 0; // No sequence number for errors
	replyPacket.comres = errorCode; // Error code
	replyPacket.data.clear(); // No data for errors
	replyPacket.crc1 = calculateCRC(reinterpret_cast<const uint8_t*>(&replyPacket), sizeof(replyPacket));
	//replyPacket.crc1 = calculateCRC(replyPacket); // Calculate CRC for error packet
	replyPacket.length = sizeof(replyPacket); // actual size of the reply packet

	return constructSPIReplyPacket(replyPacket);
}

// caculate CRC1
uint32_t SpiCmdDecoder::calculateCRC1(const uint8_t* data) {
	return calculateCRC(data, 16); // 包头固定16字节
}
// caculate CRC2（0x00~[LENGTH-5]）
uint32_t SpiCmdDecoder::calculateCRC2(const uint8_t* data, size_t length) {
	return calculateCRC(data, length - 4); // 排除最后4字节（CRC2自身）
}

// caculate CRC
uint32_t SpiCmdDecoder::calculateCRC(const uint8_t* data, size_t length) {
	uint32_t crc = 0xFFFFFFFF;
	const uint32_t polynomial = 0x82608EDB; // 反向多项式
	for (size_t i = 0; i < length; i++) {
		crc ^= data[i];
		for (int j = 0; j < 8; j++) {
			crc = (crc & 1) ? (crc >> 1) ^ polynomial : (crc >> 1);
		}
	}
	return ~crc;
}
    // Sample CRC calculation (to be implemented based on your specification)

std::vector<uint8_t> SpiCmdDecoder::constructSPIReplyPacketHeader(const SPIReplyPacket& replyPacket) {
	std::vector<uint8_t> header;
	header.reserve(16); 
	// Add SPIMAGIC (4 bytes)
	header.push_back((replyPacket.spiMagic >> 24) & 0xFF);
	header.push_back((replyPacket.spiMagic >> 16) & 0xFF);
	header.push_back((replyPacket.spiMagic >> 8) & 0xFF);
	header.push_back(replyPacket.spiMagic & 0xFF);
	// Add LENGTH (4 bytes)
	header.push_back((replyPacket.length >> 24) & 0xFF);
	header.push_back((replyPacket.length >> 16) & 0xFF);
	header.push_back((replyPacket.length >> 8) & 0xFF);
	header.push_back(replyPacket.length & 0xFF);
	// Add SEQNO (4 bytes)
	header.push_back((replyPacket.seqNo >> 24) & 0xFF);
	header.push_back((replyPacket.seqNo >> 16) & 0xFF);
	header.push_back((replyPacket.seqNo >> 8) & 0xFF);
	header.push_back(replyPacket.seqNo & 0xFF);
	// Add COMRES (4 bytes)
	header.push_back((replyPacket.comres >> 24) & 0xFF);
	header.push_back((replyPacket.comres >> 16) & 0xFF);
	header.push_back((replyPacket.comres >> 8) & 0xFF);
	header.push_back(replyPacket.comres & 0xFF);
	return header;
}

std::vector<uint8_t> SpiCmdDecoder::constructSPIReplyPacketWithoutCRC2(const SPIReplyPacket& replyPacket) {
	std::vector<uint8_t> packet;
    packet.reserve(replyPacket.length - 4);

    // Add SPIMAGIC (4 bytes)
    packet.push_back((replyPacket.spiMagic >> 24) & 0xFF);
    packet.push_back((replyPacket.spiMagic >> 16) & 0xFF);
    packet.push_back((replyPacket.spiMagic >> 8) & 0xFF);
    packet.push_back(replyPacket.spiMagic & 0xFF);

    // Add LENGTH (4 bytes)
    packet.push_back((replyPacket.length >> 24) & 0xFF);
    packet.push_back((replyPacket.length >> 16) & 0xFF);
    packet.push_back((replyPacket.length >> 8) & 0xFF);
    packet.push_back(replyPacket.length & 0xFF);

    // Add SEQNO (4 bytes)
    packet.push_back((replyPacket.seqNo >> 24) & 0xFF);
    packet.push_back((replyPacket.seqNo >> 16) & 0xFF);
    packet.push_back((replyPacket.seqNo >> 8) & 0xFF);
    packet.push_back(replyPacket.seqNo & 0xFF);

    // Add COMRES (4 bytes)
    packet.push_back((replyPacket.comres >> 24) & 0xFF);
    packet.push_back((replyPacket.comres >> 16) & 0xFF);
    packet.push_back((replyPacket.comres >> 8) & 0xFF);
    packet.push_back(replyPacket.comres & 0xFF);

    // Add CRC1 (4 bytes)
    packet.push_back((replyPacket.crc1 >> 24) & 0xFF);
    packet.push_back((replyPacket.crc1 >> 16) & 0xFF);
    packet.push_back((replyPacket.crc1 >> 8) & 0xFF);
    packet.push_back(replyPacket.crc1 & 0xFF);

    // Add DATA if present
    if (replyPacket.length > 0x0014) {
        // Add DATA
        packet.insert(packet.end(), replyPacket.data.begin(), replyPacket.data.end());
    }

    return packet;
}

uint16_t SpiCmdDecoder::bytesToInt16BigEndian(const std::vector<uint8_t>& bytes, size_t offset) {
	// Ensure there are enough bytes
	if (bytes.size() < offset + 2) {
		throw std::out_of_range("Not enough bytes to read a 16-bit integer");
	}

	// Combine the bytes into a 16-bit integer (big-endian)
	uint16_t result = (static_cast<uint16_t>(bytes[offset]) << 8) |
					  (static_cast<uint16_t>(bytes[offset + 1]));

	return result;
}

void SpiCmdDecoder::appendUint32BigEndian(std::vector<uint8_t>& buf, uint32_t value) {
    buf.push_back((value >> 24) & 0xFF);
    buf.push_back((value >> 16) & 0xFF);
    buf.push_back((value >> 8) & 0xFF);
    buf.push_back(value & 0xFF);

	// std::cout << " CRC Bytes appended: ";
    // std::cout << "0x" << std::hex << static_cast<int>((value >> 24) & 0xFF) << " ";
    // std::cout << "0x" << std::hex << static_cast<int>((value >> 16) & 0xFF) << " ";
    // std::cout << "0x" << std::hex << static_cast<int>((value >> 8) & 0xFF) << " ";
    // std::cout << "0x" << std::hex << static_cast<int>(value & 0xFF) << std::endl;
}

int SpiCmdDecoder::loadProdIniConf(void) {
	std::ifstream file("/mnt/SPI_LUT.ini");

	    if (file.is_open()) {
	 //       std::cout << "[Background_LUT] File has been opened" << std::endl;
	    }
	    else {
	        std::cout << "[SPI_LUT] File opening Error" << std::endl;
	        return (-1);
	    }

	    // The file reading is based on INI LUT file template.

	    std::map<std::string, std::map<std::string, std::string>> config;
	    unsigned int mod = 0;
	    std::string line, currentSection;
	    while (std::getline(file, line)) {
	    	 // Trim leading/trailing whitespace
			line.erase(0, line.find_first_not_of(" \t"));
			line.erase(line.find_last_not_of(" \t") + 1);
	    	if (line.empty() || line[0] == ';') {
	                continue; // Skip empty lines and comments
	    	}
	        if (line[0] == '[') {
	            currentSection = line.substr(1, line.size() - 2);
	            mod++;
	        }
	    	else
	    	{
	            size_t pos = line.find('=');
	            if (pos != std::string::npos) {
	            	std::string key = line.substr(0, pos);
	                std::string value = line.substr(pos + 1);
	                config[currentSection][key] = value;

	                // Trim key and value
					key.erase(0, key.find_first_not_of(" \t"));
					key.erase(key.find_last_not_of(" \t") + 1);
					value.erase(0, value.find_first_not_of(" \t"));
					value.erase(value.find_last_not_of(" \t") + 1);

	                // load parameters into data structure to be used by pattern generation
	                if(key == "SUS"){
						//std::cout<< "SUS (before process):" <<value << std::endl;
	                	conf_spi.sus = atoi(value.c_str());
						//std::cout << "SUS: "<<conf_spi.sus << std::endl;
	                }
	                else if(key == "HWR"){
						// Parse HWR as "Major.Minor.Implementation.ReleaseCandidate"
						//std::cout << "HWR value: " << value << std::endl;
						//std::cout << "FWR value size: " << value.size() << std::endl;
                        // Format: 2501
						conf_spi.hwr.year1 = (value[0] - '0');
	                    conf_spi.hwr.year2 = (value[1] - '0');
    	                conf_spi.hwr.month1 = (value[2] - '0');
        	            conf_spi.hwr.month2 = (value[3] - '0');
        	            // std::cout << "Parsed FWR: " << conf_spi.hwr.year1 << "."
            	        //           << conf_spi.hwr.year2 << "."
                	    //           << conf_spi.hwr.month1 << "."
                    	//           << conf_spi.hwr.month2 << std::endl;
	                }
	                else if(key == "FWR"){
	                	// Parse FWR as "Major.Minor.Implementation.ReleaseCandidate"
						// std::cout << "FWR value: " << value << std::endl;
						// std::cout << "FWR value size: " << value.size() << std::endl;
                    	conf_spi.fwr.year1 = (value[0] - '0');
	                    conf_spi.fwr.year2 = (value[1] - '0');
    	                conf_spi.fwr.month1 = (value[2] - '0');
        	            conf_spi.fwr.month2 = (value[3] - '0');
        	            // std::cout << "Parsed FWR: " << conf_spi.fwr.year1 << "."
            	        //           << conf_spi.fwr.year2 << "."
                	    //           << conf_spi.fwr.month1 << "."
                    	//           << conf_spi.fwr.month2 << std::endl;
					}
	                else if(key == "SNO"){
	                	conf_spi.sno = value;
						//std::cout << "SNO: " << conf_spi.sno << std::endl; 
	                }
	                else if(key == "MFD"){
	                	conf_spi.mfd = value;
						//std::cout << "MFD: " << conf_spi.mfd << std::endl;
	                }
	                else if(key == "LBL"){
	                	conf_spi.lbl = value;
						//std::cout << "LBL: " << conf_spi.lbl << std::endl;
	                }
	                else if(key == "MID"){
	                	conf_spi.mid = value;
						//std::cout << "MID: " << conf_spi.mid << std::endl; 
	                }
	                else{
	                	std::cout << "[SPI_LUT] File need more readings" << std::endl;
	                }
	            }
	        }
	     }
	     file.close();

	return (0);
}

int SpiCmdDecoder::modifyIniValue(const std::string& section, const std::string& key, const std::string& newValue) {
    // Open the INI file for reading
    std::ifstream inFile("/mnt/SPI_LUT.ini");
    if (!inFile.is_open()) {
        std::cout << "[SPI_LUT] File opening Error" << std::endl;
        return -1;
    }

    // Read the file into a string stream
    std::stringstream buffer;
    buffer << inFile.rdbuf();
    inFile.close();

    // Convert the string stream into a string for manipulation
    std::string fileContent = buffer.str();
    std::stringstream updatedContent;
    bool sectionFound = false;
    bool keyFound = false;

    // Split the file content into lines
    std::istringstream fileStream(fileContent);
    std::string line;
    while (std::getline(fileStream, line)) {
        // Trim leading/trailing whitespace
        line.erase(0, line.find_first_not_of(" \t"));
        line.erase(line.find_last_not_of(" \t") + 1);

        // Check if the current line is the target section
        if (line[0] == '[' && line.find(']') != std::string::npos) {
            std::string currentSection = line.substr(1, line.find(']') - 1);
            if (currentSection == section) {
                sectionFound = true;
            } else {
                sectionFound = false;
            }
        }

        // If we're in the target section, look for the key
        if (sectionFound && line.find('=') != std::string::npos) {
            std::string currentKey = line.substr(0, line.find('='));
            currentKey.erase(0, currentKey.find_first_not_of(" \t"));
            currentKey.erase(currentKey.find_last_not_of(" \t") + 1);

            if (currentKey == key) {
                // Replace the value with the new value
                updatedContent << key << "=" << newValue << std::endl;
                keyFound = true;
                continue;
            }
        }

        // Write the line to the updated content
        updatedContent << line << std::endl;
    }

    // If the key was not found, add it to the section
    if (!keyFound) {
        updatedContent << key << "=" << newValue << std::endl;
    }

    // Write the updated content back to the file
    std::ofstream outFile("/mnt/SPI_LUT.ini");
    if (!outFile.is_open()) {
        std::cout << "[SPI_LUT] File opening Error" << std::endl;
        return -1;
    }

    outFile << updatedContent.str();
    outFile.close();

    return 0;
}

// Function to construct the Operational Status Register (OSS) reply data
uint16_t SpiCmdDecoder::constructOSSReplyData() {
    uint16_t ossReplyData = 0; // Initialize all bits to 0

	std::cout << "isReady: " << oss.isReady << std::endl;
    std::cout << "isHardwareError: " << oss.isHardwareError << std::endl;
    std::cout << "isLatchedError: " << oss.isLatchedError << std::endl;
    std::cout << "isModuleRestarted: " << oss.isModuleRestarted << std::endl;
    std::cout << "isStartUpActive: " << oss.isStartUpActive << std::endl;
    std::cout << "isOpticsNotReady: " << oss.isOpticsNotReady << std::endl;
    std::cout << "isOpticsDisabled: " << oss.isOpticsDisabled << std::endl;
    std::cout << "isPending: " << oss.isPending << std::endl;

    // Set Bit 0 (Ready) if the device is ready
        ossReplyData |= (oss.isReady << 0);

    // Set Bit 1 (Hardware Error) if there is a hardware error
        ossReplyData |= (oss.isHardwareError << 1);

    // Set Bit 2 (Hardware Error Latched) if there is a latched hardware error
        ossReplyData |= (oss.isLatchedError << 2);

    // Set Bit 3 (Module Restarted) if a soft reset occurred
        ossReplyData |= (oss.isModuleRestarted << 3);

    // Set Bit 4 (Start-Up Active) if the device is in start-up phase
        ossReplyData |= (oss.isStartUpActive << 4);

    // Set Bit 5 (Optics Not Ready) if optics temperature is not stable
        ossReplyData |= (oss.isOpticsNotReady << 5);

    // Set Bit 6 (Optics Disabled) if optics are disabled
        ossReplyData |= (oss.isOpticsDisabled << 6); //isOpticsDisabled not supported

    // Set Bit 7 (Pending) if slices' port or attenuation changes are in progress
        ossReplyData |= (oss.isPending << 7);

    // Bits 8-15 are reserved and remain 0
	std::cout << "ossReplyData: " << ossReplyData << std::endl;
	std::cout << "ossReplyData (dec): " << ossReplyData << std::endl;
    std::cout << "ossReplyData (hex): 0x" << std::hex << ossReplyData << std::endl;
    std::cout << "ossReplyData (bin): " << std::bitset<16>(ossReplyData) << std::endl;

    return ossReplyData;
}

// Function to construct the Operational Status Register (OSS) reply data
uint16_t SpiCmdDecoder::constructHSSReplyData() {
    uint16_t hssReplyData = 0; // Initialize all bits to 0

    // Set Bit 0 (caseTempError) if the device case temperature is not normal
    hssReplyData |= (hss.caseTempError << 0);

    // Set Bit 1 (internalTempError) if internal temperature is exceeds specification
    hssReplyData |= (hss.internalTempError << 1);

    // Set Bit 2 (tempControlShutdown) if temperature control is shutdown
    hssReplyData |= (hss.tempControlShutdown << 2);

    // Set Bit 3 (thermalShutdown) if a thermalShutdown occurred
    hssReplyData |= (hss.thermalShutdown << 3);

    // Set Bit 4 (opticalControlFailure) if the device optical Control Failed
    hss.opticalControlFailure = g_OpticalControl_Failure;
    hssReplyData |= (hss.opticalControlFailure << 4);

    // Set Bit 5 (powerSupplyError) if power supply is not stable
    hssReplyData |= (hss.powerSupplyError << 5);

    // Set Bit 6 (powerRailError) if powerRail are disabled
    hssReplyData |= (hss.powerRailError << 6); //isOpticsDisabled not supported

    // Set Bit 7 (internalFailure)
    hssReplyData |= (hss.internalFailure << 7);

    // Set Bit 8 (calibrationError)
    hss.calibrationError = g_Calib_File_Failure;
    hssReplyData |= (hss.calibrationError << 8);

    // Set Bit 9 (singleEventUpset)
    hssReplyData |= (hss.singleEventUpset << 9);

    // Bits 10-15 are reserved and remain 0

    return hssReplyData;
}

//Third step: Implement command object class member functions parse() and process()
/***********************************************************************************************
 * SPI Command processing functions definitions below: 0x0001
 * ********************************************************************************************/
bool SpiNOPCmdCommand::parse(const SPICommandPacket& packetData) {
	return true; // Successfully parsed command
}

std::vector<uint8_t> SpiNOPCmdCommand::process(uint32_t seqNo) {
        // Implement your command-specific processing logic here

		std::cout << "NOP Command Process" << std::endl;
        SPIReplyPacket replyPacket;
        replyPacket.spiMagic = SPIMAGIC;
        //replyPacket.length = 0x0014; // example length
        replyPacket.seqNo = seqNo;
        replyPacket.comres = 0; // Indicate success
        replyPacket.data.clear(); // Populate if necessary
        //replyPacket.crc1 = spiCmd->calculateCRC(reinterpret_cast<const uint8_t*>(&replyPacket), sizeof(replyPacket)); // Implement CRC calculation
		replyPacket.length = 20; // SPIMAGIC + LENGTH + SEQNO + COMRES + CRC1
    	if (!replyPacket.data.empty()) {
        	replyPacket.length += replyPacket.data.size() + 4; // DATA + CRC2
    	}
        
		spiCmd->oss.isReady = true;
        spiCmd->oss.isStartUpActive = false;

        return spiCmd->constructSPIReplyPacket(replyPacket);

}
/***********************************************************************************************
 * SPI Command processing functions definitions below: 0x0002
 * ********************************************************************************************/
bool SpiResetCmdCommand::parse(const SPICommandPacket& packetData) {
	return true; // Successfully parsed command
}

std::vector<uint8_t> SpiResetCmdCommand::process(uint32_t seqNo) {
    // Implement your command-specific processing logic here
	//sleep(10); //sleep for time more than watchdog timer to trigger soft reset

	SPIReplyPacket replyPacket;
	replyPacket.spiMagic = SPIMAGIC;
	//replyPacket.length = 0x0014; // example length
	replyPacket.seqNo = seqNo;
	replyPacket.comres = 0; // Indicate success
	replyPacket.data.clear(); // Populate if necessary
	replyPacket.length = 20; // SPIMAGIC + LENGTH + SEQNO + COMRES + CRC1
    if (!replyPacket.data.empty()) {
        replyPacket.length += replyPacket.data.size() + 4; // DATA + CRC2
    }

	spiCmd->oss.isModuleRestarted = true;

	try {
		const std::string mainAppPath = "/mnt/startwss.elf";
		
		// Step 1: Verify main application existence
		if (access(mainAppPath.c_str(), F_OK) != 0) {
			throw std::runtime_error("Main application not found");
		}
		// Step 2: Close non-standard file descriptors
		int max_fd = sysconf(_SC_OPEN_MAX);
		for (int fd = 3; fd < max_fd; ++fd) { 
			close(fd); // Ignore EBADF errors
		}
		// Step 3: Execute process replacement
		char* argv[] = {const_cast<char*>(mainAppPath.c_str()), nullptr};
		execv(mainAppPath.c_str(), argv);
		
		// If reached here, execv failed
		throw std::runtime_error("Exec failed: " + std::string(strerror(errno)));
		
	} catch (const std::runtime_error& e) {
		std::cerr << "Reset Error: " << e.what() << std::endl;
		replyPacket.comres = 0x02; 
	}
	// if (kill(getpid(), SIGTERM) == -1) { 
    //     perror("kill failed");
    // }      
	return spiCmd->constructSPIReplyPacket(replyPacket);

}

/***********************************************************************************************
 * SPI Command processing functions definitions below: 0x0003
 * ********************************************************************************************/
std::unique_ptr<CmdDecoder> SpiStoreCmdCommand::g_cmdDecoder = nullptr;
bool SpiStoreCmdCommand::parse(const SPICommandPacket& packetData) {
	return true; // Successfully parsed command
}

std::vector<uint8_t> SpiStoreCmdCommand::process(uint32_t seqNo) {
        // Implement your command-specific processing logic here
// fill  struct with arguments extracted from command

	if(!g_cmdDecoder) {
		g_cmdDecoder = std::make_unique<CmdDecoder>();
	}
	//Store configuration of slice plan
	g_cmdDecoder->actionSR->StoreModule(1);
	g_cmdDecoder->actionSR->StoreModule(2);
	//Reply packet constructed below
	SPIReplyPacket replyPacket;
	replyPacket.spiMagic = SPIMAGIC;
	replyPacket.seqNo = seqNo;
	replyPacket.comres = 0; // Indicate success
	replyPacket.data.clear(); // Populate if necessary
	replyPacket.length = 20; // SPIMAGIC + LENGTH + SEQNO + COMRES + CRC1
    if (!replyPacket.data.empty()) {
        replyPacket.length += replyPacket.data.size() + 4; // DATA + CRC2
    }
	return spiCmd->constructSPIReplyPacket(replyPacket);

}

/***********************************************************************************************
 * SPI Command processing functions definitions below: 0x0004
 * ********************************************************************************************/

bool SpiSUSQueryCommand::parse(const SPICommandPacket& packetData) {
	return true; // Successfully parsed command
}

std::vector<uint8_t> SpiSUSQueryCommand::process(uint32_t seqNo) {
        // Implement your command-specific processing logic here
// fill  struct with arguments extracted from command

	//Reply packet constructed below
	SPIReplyPacket replyPacket;
	replyPacket.spiMagic = SPIMAGIC;
	//replyPacket.length = 0x0014; // example length
	replyPacket.seqNo = seqNo;
	replyPacket.comres = 0; // Indicate success
	replyPacket.data.resize(1);
	replyPacket.data[0] = spiCmd->conf_spi.sus; // return data: 0: start factory default; 1: start last saved
	replyPacket.length = 20; // SPIMAGIC + LENGTH + SEQNO + COMRES + CRC1
    if (!replyPacket.data.empty()) {
        replyPacket.length += replyPacket.data.size() + 4; // DATA + CRC2
    }

	return spiCmd->constructSPIReplyPacket(replyPacket);

}

/***********************************************************************************************
 * SPI Command processing functions definitions below: 0x0005
 * ********************************************************************************************/
bool SpiSFDCmdCommand::parse(const SPICommandPacket& packetData) {
	return true; // Successfully parsed command
}

std::vector<uint8_t> SpiSFDCmdCommand::process(uint32_t seqNo) {
        // Implement your command-specific processing logic here
// fill  struct with arguments extracted from command
//write sus= start factory default:0
	spiCmd->modifyIniValue("SPI CONF", "SUS", std::to_string(0));
	//Reply packet constructed below
	SPIReplyPacket replyPacket;
	replyPacket.spiMagic = SPIMAGIC;
	replyPacket.seqNo = seqNo;
	replyPacket.comres = 0; // Indicate success
	replyPacket.data.clear();
	replyPacket.length = 20; // SPIMAGIC + LENGTH + SEQNO + COMRES + CRC1
    if (!replyPacket.data.empty()) {
        replyPacket.length += replyPacket.data.size() + 4; // DATA + CRC2
    }

	return spiCmd->constructSPIReplyPacket(replyPacket);

}

/***********************************************************************************************
 * SPI Command processing functions definitions below: 0x0006
 * ********************************************************************************************/
bool SpiSLSCmdCommand::parse(const SPICommandPacket& packetData) {
	return true; // Successfully parsed command
}

std::vector<uint8_t> SpiSLSCmdCommand::process(uint32_t seqNo) {
        // Implement your command-specific processing logic here
// fill  struct with arguments extracted from command
//write sls = start last saved:1
	spiCmd->modifyIniValue("SPI CONF", "SLS", std::to_string(1));
	//Reply packet constructed below
	SPIReplyPacket replyPacket;
	replyPacket.spiMagic = SPIMAGIC;
	replyPacket.seqNo = seqNo;
	replyPacket.comres = 0; // Indicate success
	replyPacket.data.clear();
	replyPacket.length = 20; // SPIMAGIC + LENGTH + SEQNO + COMRES + CRC1
    if (!replyPacket.data.empty()) {
        replyPacket.length += replyPacket.data.size() + 4; // DATA + CRC2
    }

	return spiCmd->constructSPIReplyPacket(replyPacket);

}

/***********************************************************************************************
 * SPI Command processing functions definitions below: 0x0007
 * ********************************************************************************************/
bool SpiHWRQueryCommand::parse(const SPICommandPacket& packetData) {
	return true; // Successfully parsed command
}

std::vector<uint8_t> SpiHWRQueryCommand::process(uint32_t seqNo) {
        // Implement your command-specific processing logic here
// fill  struct with arguments extracted from command

	//Reply packet constructed below
	SPIReplyPacket replyPacket;
	replyPacket.spiMagic = SPIMAGIC;
	replyPacket.seqNo = seqNo;
	replyPacket.comres = 0; // Indicate success
	// Assign hardware version data (4 bytes)
    replyPacket.data = {
    	spiCmd->conf_spi.hwr.year1,
    	spiCmd->conf_spi.hwr.year2,
    	spiCmd->conf_spi.hwr.month1,
		spiCmd->conf_spi.hwr.month2
    };
	replyPacket.length = 20; // SPIMAGIC + LENGTH + SEQNO + COMRES + CRC1
    if (!replyPacket.data.empty()) {
        replyPacket.length += replyPacket.data.size() + 4; // DATA + CRC2
    }

	return spiCmd->constructSPIReplyPacket(replyPacket);

}

/***********************************************************************************************
 * SPI Command processing functions definitions below: 0x0008
 * ********************************************************************************************/
bool SpiFWRQueryCommand::parse(const SPICommandPacket& packetData) {
	return true; // Successfully parsed command
}

std::vector<uint8_t> SpiFWRQueryCommand::process(uint32_t seqNo) {
        // Implement your command-specific processing logic here
// fill  struct with arguments extracted from command


	//Reply packet constructed below
	SPIReplyPacket replyPacket;
	replyPacket.spiMagic = SPIMAGIC;
	//replyPacket.length = 0x0014; // example length
	replyPacket.seqNo = seqNo;
	replyPacket.comres = 0; // Indicate success
	// Assign firmware version data (4 bytes)
    replyPacket.data = {
    	spiCmd->conf_spi.fwr.year1,
    	spiCmd->conf_spi.fwr.year2,
    	spiCmd->conf_spi.fwr.month1,
    	spiCmd->conf_spi.fwr.month2
    };
	replyPacket.length = 20; // SPIMAGIC + LENGTH + SEQNO + COMRES + CRC1
    if (!replyPacket.data.empty()) {
        replyPacket.length += replyPacket.data.size() + 4; // DATA + CRC2
    }

	return spiCmd->constructSPIReplyPacket(replyPacket);

}

/***********************************************************************************************
 * SPI Command processing functions definitions below: 0x0009
 * ********************************************************************************************/
bool SpiSNOQueryCommand::parse(const SPICommandPacket& packetData) {
	return true; // Successfully parsed command
}

std::vector<uint8_t> SpiSNOQueryCommand::process(uint32_t seqNo) {
        // Implement your command-specific processing logic here
// fill  struct with arguments extracted from command

	//Reply packet constructed below
	SPIReplyPacket replyPacket;
	replyPacket.spiMagic = SPIMAGIC;
	//replyPacket.length = 0x0014; // example length
	replyPacket.seqNo = seqNo;
	replyPacket.comres = 0; // Indicate success

    // Assign the serial number to the data field
	if (!spiCmd->conf_spi.sno.empty() && spiCmd->conf_spi.sno.back() == '\r') {
		replyPacket.data.assign(spiCmd->conf_spi.sno.begin(), spiCmd->conf_spi.sno.end() - 1);
	} else {
		replyPacket.data.assign(spiCmd->conf_spi.sno.begin(), spiCmd->conf_spi.sno.end());
	}
	replyPacket.length = 20; // SPIMAGIC + LENGTH + SEQNO + COMRES + CRC1
    if (!replyPacket.data.empty()) {
        replyPacket.length += replyPacket.data.size() + 4; // DATA + CRC2
    }

	return spiCmd->constructSPIReplyPacket(replyPacket);

}

/***********************************************************************************************
 * SPI Command processing functions definitions below: 0x000A
 * ********************************************************************************************/
bool SpiMFDQueryCommand::parse(const SPICommandPacket& packetData) {
	return true; // Successfully parsed command
}

std::vector<uint8_t> SpiMFDQueryCommand::process(uint32_t seqNo) {
        // Implement your command-specific processing logic here
// fill  struct with arguments extracted from command

	//Reply packet constructed below
	SPIReplyPacket replyPacket;
	replyPacket.spiMagic = SPIMAGIC;
	replyPacket.seqNo = seqNo;
	replyPacket.comres = 0; // Indicate success
	if (!spiCmd->conf_spi.mfd.empty() && spiCmd->conf_spi.mfd.back() == '\r') {
		replyPacket.data.assign(spiCmd->conf_spi.mfd.begin(), spiCmd->conf_spi.mfd.end() - 1);
	} else {
		replyPacket.data.assign(spiCmd->conf_spi.mfd.begin(), spiCmd->conf_spi.mfd.end());
	}
	//replyPacket.data.assign(spiCmd->conf_spi.mfd.begin(), spiCmd->conf_spi.mfd.end());
	replyPacket.length = 20; // SPIMAGIC + LENGTH + SEQNO + COMRES + CRC1
    if (!replyPacket.data.empty()) {
        replyPacket.length += replyPacket.data.size() + 4; // DATA + CRC2
    }

	return spiCmd->constructSPIReplyPacket(replyPacket);

}

/***********************************************************************************************
 * SPI Command processing functions definitions below: 0x000B
 * ********************************************************************************************/

bool SpiLBLQueryCommand::parse(const SPICommandPacket& packetData) {
	return true; // Successfully parsed command
}

std::vector<uint8_t> SpiLBLQueryCommand::process(uint32_t seqNo) {
        // Implement your command-specific processing logic here
// fill  struct with arguments extracted from command

	//Reply packet constructed below
	SPIReplyPacket replyPacket;
	replyPacket.spiMagic = SPIMAGIC;
	replyPacket.seqNo = seqNo;
	replyPacket.comres = 0; // Indicate success
	if (!spiCmd->conf_spi.lbl.empty() && spiCmd->conf_spi.lbl.back() == '\r') {
		replyPacket.data.assign(spiCmd->conf_spi.lbl.begin(), spiCmd->conf_spi.lbl.end() - 1);
	} else {
		replyPacket.data.assign(spiCmd->conf_spi.lbl.begin(), spiCmd->conf_spi.lbl.end());
	}
	replyPacket.length = 20; // SPIMAGIC + LENGTH + SEQNO + COMRES + CRC1
    if (!replyPacket.data.empty()) {
        replyPacket.length += replyPacket.data.size() + 4; // DATA + CRC2
    }

	return spiCmd->constructSPIReplyPacket(replyPacket);

}
/***********************************************************************************************
 * SPI Command processing functions definitions below: 0x000C
 * ********************************************************************************************/
bool SpiMIDAssignCmdCommand::parse(const SPICommandPacket& packetData) {

	// Check if the data is empty
    if (packetData.data.empty()) {
        std::cerr << "Error: MID command requires <IDString> as data" << std::endl;
        return false; // Return false to indicate error
    }
    // Validate the data (ensure it is a valid ASCII string)
    for (uint8_t c : packetData.data) {
        if (c < 0x20 || c > 0x7E) {
            std::cerr << "Error: Invalid character in <IDString> (0x" << std::hex << static_cast<int>(c) << ")" << std::endl;
            return false; // Return false to indicate error
        }
    }
    // Assign the data to mid (limit to 255 characters)
    mid.assign(packetData.data.begin(), packetData.data.begin() + std::min(packetData.data.size(), static_cast<size_t>(255)));
    std::cout << "MID String: " << mid << std::endl;
    return true; // Successfully parsed command
}

std::vector<uint8_t> SpiMIDAssignCmdCommand::process(uint32_t seqNo) {
        // Implement your command-specific processing logic here
// fill  struct with arguments extracted from command
//write mid as customer wants it to be
	spiCmd->modifyIniValue("SPI CONF", "MID", mid);
	//Reply packet constructed below
	SPIReplyPacket replyPacket;
	replyPacket.spiMagic = SPIMAGIC;
	replyPacket.seqNo = seqNo;
	replyPacket.comres = 0; // Indicate success
	replyPacket.data.clear();
	replyPacket.length = 20; // SPIMAGIC + LENGTH + SEQNO + COMRES + CRC1
    if (!replyPacket.data.empty()) {
        replyPacket.length += replyPacket.data.size() + 4; // DATA + CRC2
    }

	return spiCmd->constructSPIReplyPacket(replyPacket);

}

/***********************************************************************************************
 * SPI Command processing functions definitions below: 0x000D
 * ********************************************************************************************/
bool SpiMIDQueryCommand::parse(const SPICommandPacket& packetData) {
	return true; // Successfully parsed command
}

std::vector<uint8_t> SpiMIDQueryCommand::process(uint32_t seqNo) {
        // Implement your command-specific processing logic here
// fill  struct with arguments extracted from command
	//Reply packet constructed below
	SPIReplyPacket replyPacket;
	replyPacket.spiMagic = SPIMAGIC;
	replyPacket.seqNo = seqNo;
	replyPacket.comres = 0; // Indicate success
	if (!spiCmd->conf_spi.mid.empty() && spiCmd->conf_spi.mid.back() == '\r') {
		replyPacket.data.assign(spiCmd->conf_spi.mid.begin(), spiCmd->conf_spi.mid.end() - 1);
	} else {
		replyPacket.data.assign(spiCmd->conf_spi.mid.begin(), spiCmd->conf_spi.mid.end());
	}
//	replyPacket.data.assign(spiCmd->conf_spi.mid.begin(), spiCmd->conf_spi.mid.end());
	replyPacket.length = 20; // SPIMAGIC + LENGTH + SEQNO + COMRES + CRC1
    if (!replyPacket.data.empty()) {
        replyPacket.length += replyPacket.data.size() + 4; // DATA + CRC2
    }

	return spiCmd->constructSPIReplyPacket(replyPacket);

}

/***********************************************************************************************
 * SPI Command processing functions definitions below: 0x000F
 * ********************************************************************************************/
bool SpiOSSQueryCommand::parse(const SPICommandPacket& packetData) {
	return true; // Successfully parsed command
}

std::vector<uint8_t> SpiOSSQueryCommand::process(uint32_t seqNo) {
        // Implement your command-specific processing logic here
// fill  struct with arguments extracted from command

	//Reply packet constructed below
	SPIReplyPacket replyPacket;
	replyPacket.spiMagic = SPIMAGIC;
	replyPacket.seqNo = seqNo;
	replyPacket.comres = 0; // Indicate success
	replyPacket.data.resize(2); // Resize to 2 bytes to store the 16-bit value
	
	uint16_t ossReplyData = spiCmd->constructOSSReplyData();
	std::cout << "oss data: " << ossReplyData << std::endl;
	replyPacket.data[0] = ((ossReplyData >> 8) & 0xFF); //16 bitwise value that needs to get from actual operational status
	std::cout << "high: " << ((ossReplyData >> 8) & 0xFF) << std::endl;
	std::cout << "high: " << replyPacket.data[0] << std::endl;
	replyPacket.data[1] = (ossReplyData & 0xFF); //16 bitwise value that needs to get from actual operational status
	std::cout << "low: " << (ossReplyData & 0xFF) << std::endl;
	std::cout << "low: " << replyPacket.data[1] << std::endl;
	replyPacket.length = 20; // SPIMAGIC + LENGTH + SEQNO + COMRES + CRC1
    if (!replyPacket.data.empty()) {
        replyPacket.length += replyPacket.data.size() + 4; // DATA + CRC2
    }

	return spiCmd->constructSPIReplyPacket(replyPacket);

}

/***********************************************************************************************
 * SPI Command processing functions definitions below: 0x0010
 * ********************************************************************************************/
bool SpiHSSQueryCommand::parse(const SPICommandPacket& packetData) {
	return true; // Successfully parsed command
}

std::vector<uint8_t> SpiHSSQueryCommand::process(uint32_t seqNo) {
// Implement your command-specific processing logic here
// fill  struct with arguments extracted from command

	//Reply packet constructed below
	SPIReplyPacket replyPacket;
	replyPacket.spiMagic = SPIMAGIC;
	replyPacket.seqNo = seqNo;
	replyPacket.comres = 0; // Indicate success
	replyPacket.data.resize(2); // Resize to 2 bytes to store the 16-bit value
	replyPacket.data[0] = (spiCmd->constructHSSReplyData() >> 8) & 0xFF; //16 bitwise value that needs to get from actual operational status
	replyPacket.data[1] = spiCmd->constructHSSReplyData() & 0xFF; //16 bitwise value that needs to get from actual operational status
	replyPacket.length = 20; // SPIMAGIC + LENGTH + SEQNO + COMRES + CRC1
    if (!replyPacket.data.empty()) {
        replyPacket.length += replyPacket.data.size() + 4; // DATA + CRC2
    }

	return spiCmd->constructSPIReplyPacket(replyPacket);

}

/***********************************************************************************************
 * SPI Command processing functions definitions below: 0x0011
 * ********************************************************************************************/
bool SpiLSSQueryCommand::parse(const SPICommandPacket& packetData) {
	return true; // Successfully parsed command
}

std::vector<uint8_t> SpiLSSQueryCommand::process(uint32_t seqNo) {
// Implement your command-specific processing logic here
// fill  struct with arguments extracted from command

	//Reply packet constructed below
	SPIReplyPacket replyPacket;
	replyPacket.spiMagic = SPIMAGIC;
	//replyPacket.length = 0x0014; // example length
	replyPacket.seqNo = seqNo;
	replyPacket.comres = 0; // Indicate success
	replyPacket.data.resize(2); // Resize to 2 bytes to store the 16-bit value
	replyPacket.data[0] = (spiCmd->constructHSSReplyData() >> 8) & 0xFF; //16 bitwise value that needs to get from actual operational status
	replyPacket.data[1] = spiCmd->constructHSSReplyData() & 0xFF; //16 bitwise value that needs to get from actual operational status
	replyPacket.length = 20; // SPIMAGIC + LENGTH + SEQNO + COMRES + CRC1
    if (!replyPacket.data.empty()) {
        replyPacket.length += replyPacket.data.size() + 4; // DATA + CRC2
    }

	return spiCmd->constructSPIReplyPacket(replyPacket);

}

/***********************************************************************************************
 * SPI Command processing functions definitions below: 0x0012
 * ********************************************************************************************/
bool SpiCLECmdCommand::parse(const SPICommandPacket& packetData) {
	return true; // Successfully parsed command
}

std::vector<uint8_t> SpiCLECmdCommand::process(uint32_t seqNo) {
// Implement your command-specific processing logic here
// fill  struct with arguments extracted from command


	//Reply packet constructed below
	SPIReplyPacket replyPacket;
	replyPacket.spiMagic = SPIMAGIC;
	//replyPacket.length = 0x0014; // example length
	replyPacket.seqNo = seqNo;
	replyPacket.comres = 0; // Indicate success
	replyPacket.data.clear(); //16 bitwise value that needs to get from actual operational status
	//replyPacket.crc1 = spiCmd->calculateCRC(reinterpret_cast<const uint8_t*>(&replyPacket), sizeof(replyPacket)); // Implement CRC calculation
	replyPacket.length = 20; // SPIMAGIC + LENGTH + SEQNO + COMRES + CRC1
    if (!replyPacket.data.empty()) {
        replyPacket.length += replyPacket.data.size() + 4; // DATA + CRC2
    }

	return spiCmd->constructSPIReplyPacket(replyPacket);

}

/***********************************************************************************************
 * SPI Command processing functions definitions below: 0x0013
 * ********************************************************************************************/
bool SpiCSSQueryCommand::parse(const SPICommandPacket& packetData) {
	return true; // Successfully parsed command
}

std::vector<uint8_t> SpiCSSQueryCommand::process(uint32_t seqNo) {
// Implement your command-specific processing logic here
// fill  struct with arguments extracted from command


	//Reply packet constructed below
	SPIReplyPacket replyPacket;
	replyPacket.spiMagic = SPIMAGIC;
	//replyPacket.length = 0x0014; // example length
	replyPacket.seqNo = seqNo;
	replyPacket.comres = 0; // Indicate success
	replyPacket.data.resize(2); // Resize to 2 bytes to store the 16-bit value
	replyPacket.data[0] = (static_cast<int16_t>(g_direct_Hearter2_Temp*10) >> 8) & 0xFF; //16 bit signed value of case temperature
	replyPacket.data[1] = (static_cast<int16_t>(g_direct_Hearter2_Temp*10)) & 0xFF; //16 bit signed value of case temperature
	std::cout <<"g_direct_Hearter2_Temp:"<< g_direct_Hearter2_Temp << std::endl;
	std::cout <<"g_direct_Hearter2_Temp*10:"<< g_direct_Hearter2_Temp*10 << std::endl;
	std::cout <<"(static_cast<int16_t>(g_direct_Hearter2_Temp*10) >> 8) & 0xFF:"<< ((static_cast<int16_t>(g_direct_Hearter2_Temp*10) >> 8) & 0xFF) << std::endl;
	std::cout <<"(static_cast<int16_t>(g_direct_Hearter2_Temp*10)) & 0xFF:"<< ((static_cast<int16_t>(g_direct_Hearter2_Temp*10)) & 0xFF) << std::endl;

	replyPacket.length = 20; // SPIMAGIC + LENGTH + SEQNO + COMRES + CRC1
    if (!replyPacket.data.empty()) {
        replyPacket.length += replyPacket.data.size() + 4; // DATA + CRC2
    }

	return spiCmd->constructSPIReplyPacket(replyPacket);

}

/***********************************************************************************************
 * SPI Command processing functions definitions below: 0x0014
 * ********************************************************************************************/
bool SpiISSQueryCommand::parse(const SPICommandPacket& packetData) {
	return true; // Successfully parsed command
}

std::vector<uint8_t> SpiISSQueryCommand::process(uint32_t seqNo) {
// Implement your command-specific processing logic here
// fill  struct with arguments extracted from command


	//Reply packet constructed below
	SPIReplyPacket replyPacket;
	replyPacket.spiMagic = SPIMAGIC;
	replyPacket.seqNo = seqNo;
	replyPacket.comres = 0; // Indicate success
	replyPacket.data.resize(2); // Resize to 2 bytes to store the 16-bit value
	replyPacket.data[0] = (static_cast<int16_t>(g_direct_LCOS_Temp*10) >> 8) & 0xFF; //16 bit signed value of internal temperature
	replyPacket.data[1] = (static_cast<int16_t>(g_direct_LCOS_Temp)) & 0xFF; //16 bit signed value of internal temperature
	replyPacket.length = 20; // SPIMAGIC + LENGTH + SEQNO + COMRES + CRC1
    if (!replyPacket.data.empty()) {
        replyPacket.length += replyPacket.data.size() + 4; // DATA + CRC2
    }

	return spiCmd->constructSPIReplyPacket(replyPacket);

}

/***********************************************************************************************
 * SPI Command processing functions definitions below: 0x0015
 * ********************************************************************************************/
std::unique_ptr<CmdDecoder> SPASlicePortAttenuationCommand::g_cmdDecoder = nullptr;
//spi command SPA processing, e.g.:0x0015 1 1:8 1:2 10 9:10 1:3 20
bool SPASlicePortAttenuationCommand::parse(const SPICommandPacket& packetData)
{
	// First byte is the WSS module number
	w = packetData.data[0];
	std::cout << "WSS number: " <<  static_cast<int>(w) << std::endl;

	if (w < 1 || w > 2) {
		std::cout << "Invalid WSS number: " << w << std::endl;
		return false; // Invalid WSS number
	}

	// Parse the rest of the command data
	size_t index = 1; // Start after the WSS module number
	while (index < packetData.data.size()) {
		// Ensure there are enough bytes to read the slice range and ports
		if (index + 5 > packetData.data.size()) {
			std::cout << "Not enough data for the next section: " << packetData.data.size() << std::endl;
			return false; // Not enough data for the next section
		}
		// Get slice range S:S
		uint16_t startSlice = spiCmd->bytesToInt16BigEndian(packetData.data, index);
		uint16_t endSlice = spiCmd->bytesToInt16BigEndian(packetData.data, index+2);
		sliceRanges.emplace_back(startSlice, endSlice);
		SPASlicePortAttenuationCommand::spaConfStruct[w].sliceRanges.emplace_back(startSlice, endSlice);
		std::cout << "StartSlice: " << startSlice << " EndSlice: " << endSlice << std::endl;
		index += 4;
		// Get common and switching ports
		uint8_t commonPort = packetData.data[index];
		uint8_t switchingPort = packetData.data[index+1];
		std::cout << "COMMONPort: " <<  static_cast<int>(commonPort) << " SWITCHPort: " <<  static_cast<int>(switchingPort) << std::endl;
		if(commonPort < 1 || commonPort > 2){
			std::cout << "Invalid Common Port: " << commonPort << std::endl;
			return false; // Invalid
		}
		if(switchingPort < 1 || switchingPort > 23){
			std::cout << "Invalid Switching Port: " << switchingPort << std::endl;
			return false; // Invalid
		}
		commonPorts.push_back(commonPort);
		switchingPorts.push_back(switchingPort);
		SPASlicePortAttenuationCommand::spaConfStruct[w].commonPorts.push_back(commonPort);
		SPASlicePortAttenuationCommand::spaConfStruct[w].switchingPorts.push_back(switchingPort);
		index += 2;
		// Get attenuation
		int16_t attenuation = spiCmd->bytesToInt16BigEndian(packetData.data,index);
		attenuations.push_back(attenuation);
		SPASlicePortAttenuationCommand::spaConfStruct[w].attenuations.push_back(attenuation);
		std::cout << "Attenuation: " << attenuation << std::endl;
		index += 2;
	}
	return true; // Successfully parsed command
}

std::vector<uint8_t> SPASlicePortAttenuationCommand::process(uint32_t seqNo) {
// Implement your command-specific processing logic here
// fill patternGen struct with arguments extracted from command
	SPIReplyPacket replyPacket;
	replyPacket.spiMagic = SPIMAGIC;
	//replyPacket.length = sizeof(SPIReplyPacket); //
	replyPacket.data.clear(); // No data attached in reply
	replyPacket.seqNo = seqNo;



	// Lock the channel data structures for thread safety
	if (pthread_mutex_lock(&global_mutex[LOCK_CHANNEL_DS]) != 0) {
		std::cout << "global_mutex[LOCK_CHANNEL_DS] lock unsuccessful" << std::endl;
		replyPacket.comres = 1;
		//replyPacket.crc1 = spiCmd->calculateCRC(reinterpret_cast<const uint8_t*>(&replyPacket), sizeof(replyPacket)); // Implement CRC calculation
		return spiCmd->constructSPIReplyPacket(replyPacket);
	}

	try {
		// Get reference to the FG_Channel_DS for the specified WSS module
		// Update channel parameters for each slice range
		for (size_t i = 0; i < sliceRanges.size(); i++) {
			uint16_t channelNum = i + 1; // Adjust based on your channel numbering scheme

			// Update channel parameters using existing structure
			g_cmdDecoder->FG_Channel_DS_For_Pattern[w][channelNum].active = true;
			g_cmdDecoder->FG_Channel_DS_For_Pattern[w][channelNum].ATT = attenuations[i]/10.0;  // devided by 10 because it's cB not dB
			g_cmdDecoder->FG_Channel_DS_For_Pattern[w][channelNum].F1 = freqBySlice(sliceRanges[i].first);
			g_cmdDecoder->FG_Channel_DS_For_Pattern[w][channelNum].F2 = freqBySlice(sliceRanges[i].second);
			g_cmdDecoder->FG_Channel_DS_For_Pattern[w][channelNum].FC = (g_cmdDecoder->FG_Channel_DS_For_Pattern[w][channelNum].F1 + g_cmdDecoder->FG_Channel_DS_For_Pattern[w][channelNum].F2) / 2;
			g_cmdDecoder->FG_Channel_DS_For_Pattern[w][channelNum].BW = g_cmdDecoder->FG_Channel_DS_For_Pattern[w][channelNum].F2 - g_cmdDecoder->FG_Channel_DS_For_Pattern[w][channelNum].F1;
			g_cmdDecoder->FG_Channel_DS_For_Pattern[w][channelNum].slotNum = sliceRanges[i].second - sliceRanges[i].first + 1;
			// Set attenuation if available
			if (i < attenuations.size()) {
				//g_cmdDecoder->FG_Channel_DS_For_Pattern[w][channelNum].slotsATTEN[i] = attenuations[i]/10;  // devided by 10 because it's cB not dB
			}

			// Set ports
			if (i < commonPorts.size() && i < switchingPorts.size()) {
				// Update port configuration using existing structure fields
				// (Assuming these fields exist in the FixedGrid structure)
				g_cmdDecoder->FG_Channel_DS_For_Pattern[w][channelNum].CMP = commonPorts[i];
				g_cmdDecoder->FG_Channel_DS_For_Pattern[w][channelNum].ADP = switchingPorts[i];
			}
			g_cmdDecoder->activeChannels.push_back({w, channelNum});
		}

		// Set flag to trigger pattern generation using existing mechanism
		g_bNewCommandData = true;
		replyPacket.comres = 0; // Success
	}
	catch (const std::exception& e) {
		std::cerr << "Error processing SPI command: " << e.what() << std::endl;
		replyPacket.comres = 1;
	}

	pthread_mutex_unlock(&global_mutex[LOCK_CHANNEL_DS]);

	std::cout << "SPI command is ready for pattern generation" << std::endl;
	g_cmdDecoder->WaitPatternTransfer();

	replyPacket.length = 20; // SPIMAGIC + LENGTH + SEQNO + COMRES + CRC1
    if (!replyPacket.data.empty()) {
        replyPacket.length += replyPacket.data.size() + 4; // DATA + CRC2
    }


	return spiCmd->constructSPIReplyPacket(replyPacket);

}

/***********************************************************************************************
 * SPI Command processing functions definitions below: 0x0016 1/2
 * ********************************************************************************************/
bool SPAQuerySlicePortAttenuationCommand::parse(const SPICommandPacket& packetData) {

	// First byte is the WSS module number
	w = packetData.data[0];
	std::cout << "WSS number: " <<  static_cast<int>(w) << std::endl;

	if (w < 1 || w > 2) {
		std::cout << "Invalid WSS number: " << w << std::endl;
		return false; // Invalid WSS number
	}

	return true; // Successfully parsed command
}

std::vector<uint8_t> SPAQuerySlicePortAttenuationCommand::process(uint32_t seqNo) {
// Implement your command-specific processing logic here
// fill  struct with arguments extracted from command

	//Reply packet constructed below
	SPIReplyPacket replyPacket;
	replyPacket.spiMagic = SPIMAGIC;
	//replyPacket.length = 0x0014; // example length
	replyPacket.seqNo = seqNo;
	replyPacket.comres = 0; // Indicate success

	size_t dataSize = 1; // Start with 1 byte for <W>
    for (size_t i = 0; i < SPASlicePortAttenuationCommand::spaConfStruct[w].sliceRanges.size(); i++) {
        for (size_t j = SPASlicePortAttenuationCommand::spaConfStruct[w].sliceRanges[i].first;
             j <= SPASlicePortAttenuationCommand::spaConfStruct[w].sliceRanges[i].second; j++) {
            dataSize += 6; // Each slice adds 6 bytes: <S><P><P><A>
        }
    }
    // Resize data to ensure it has enough space
    replyPacket.data.resize(dataSize);
    // Fill the data
    size_t offset = 0;
    replyPacket.data[offset++] = w; // <W>
    for (size_t i = 0; i < SPASlicePortAttenuationCommand::spaConfStruct[w].sliceRanges.size(); i++) {
        for (size_t j = SPASlicePortAttenuationCommand::spaConfStruct[w].sliceRanges[i].first;
             j <= SPASlicePortAttenuationCommand::spaConfStruct[w].sliceRanges[i].second; j++) {
            replyPacket.data[offset++] = (j >> 8) & 0xFF; // <S> high byte
            replyPacket.data[offset++] = j & 0xFF;        // <S> low byte
            replyPacket.data[offset++] = SPASlicePortAttenuationCommand::spaConfStruct[w].commonPorts[i]; // <P>
            replyPacket.data[offset++] = SPASlicePortAttenuationCommand::spaConfStruct[w].switchingPorts[i]; // <P>
            replyPacket.data[offset++] = (SPASlicePortAttenuationCommand::spaConfStruct[w].attenuations[i] >> 8) & 0xFF; // <A> high byte
            replyPacket.data[offset++] = SPASlicePortAttenuationCommand::spaConfStruct[w].attenuations[i] & 0xFF; // <A> low byte
        }
    }
	// //<W>{<S><P><P><A>}*(1:Smax)
	// replyPacket.data[0] = w;
	// for (size_t i = 0; i < SPASlicePortAttenuationCommand::spaConfStruct[w].sliceRanges.size(); i++) {
	// 	for(size_t j = SPASlicePortAttenuationCommand::spaConfStruct[w].sliceRanges[i].first;
	// 			j <= SPASlicePortAttenuationCommand::spaConfStruct[w].sliceRanges[i].second; j++) {
	// 		replyPacket.data[1] = (j >> 8) & 0xFF;
	// 		replyPacket.data[2] = j & 0xFF;
	// 		replyPacket.data[3] = SPASlicePortAttenuationCommand::spaConfStruct[w].commonPorts[i];;
	// 		replyPacket.data[4] = SPASlicePortAttenuationCommand::spaConfStruct[w].switchingPorts[i];
	// 		replyPacket.data[5] = (SPASlicePortAttenuationCommand::spaConfStruct[w].attenuations[i] >> 8) & 0xFF;
	// 		replyPacket.data[6] = SPASlicePortAttenuationCommand::spaConfStruct[w].attenuations[i] & 0xFF;
	// 	}
	// }

	replyPacket.length = 20; // SPIMAGIC + LENGTH + SEQNO + COMRES + CRC1
    if (!replyPacket.data.empty()) {
        replyPacket.length += replyPacket.data.size() + 4; // DATA + CRC2
    }

	return spiCmd->constructSPIReplyPacket(replyPacket);

}

/***********************************************************************************************
 * SPI Command processing functions definitions below: 0x0017
 * ********************************************************************************************/
uint32_t SpiFWTCmdCommand::totalReceivedBytes = 0;
std::fstream SpiFWTCmdCommand::elfFile;              // Keep file stream open
bool SpiFWTCmdCommand::transferActive = false;
bool SpiFWTCmdCommand::parse(const SPICommandPacket& packetData) {
    // Protocol format: [4-byte offset][N-byte data block]
    constexpr size_t OFFSET_SIZE = sizeof(uint32_t);
    if (packetData.data.size() < OFFSET_SIZE) {
        std::cerr << "FWT: Packet too small (requires at least 4-byte offset)" << std::endl;
        return false;
    }
    
    // Parse offset in big-endian format
    const auto* p = packetData.data.data();
    offset = (static_cast<uint32_t>(p[0]) << 24) |
             (static_cast<uint32_t>(p[1]) << 16) |
             (static_cast<uint32_t>(p[2]) << 8)  |
             static_cast<uint32_t>(p[3]);
    
    // Extract data block (bytes after offset)
    blockData.assign(packetData.data.begin() + OFFSET_SIZE, 
                    packetData.data.end());
	// Print received offset and data block
	std::cout << "FWT: Received offset = 0x" << std::hex << offset << std::endl;
	std::cout << "FWT: Received data block size = " << std::dec << blockData.size() << " bytes" << std::endl;
	std::cout << "FWT: Data block content (hex): ";
	for (const auto& byte : blockData) {
		std::cout << "0x" << std::hex << static_cast<int>(byte) << " ";
	}
	std::cout << std::endl;

    // Protocol validation
    constexpr uint32_t Fmax = 0x01000000; // 16MB
    if (offset >= Fmax) {
        std::cerr << "FWT: Invalid offset 0x" << std::hex << offset << std::endl;
        return false;
    }
    
    if (blockData.empty() || blockData.size() > Fmax) {
        std::cerr << "FWT: Invalid block size " << blockData.size() << std::endl;
        return false;
    }
    
    return true;
}
std::vector<uint8_t> SpiFWTCmdCommand::process(uint32_t seqNo) {
    // Reply packet constructed below
    SPIReplyPacket replyPacket;
    replyPacket.spiMagic = SPIMAGIC;
    replyPacket.seqNo = seqNo;
    replyPacket.comres = 0; // Indicate success
    try {
        // Transfer continuity check (first transfer or offset must be sequential)
        if (transferActive && offset != totalReceivedBytes) {
            throw std::runtime_error("Offset discontinuity, expected: 0x" + 
                                    std::to_string(totalReceivedBytes));
        }
        // Initialize file operations (first transfer)
        if (!transferActive) {
            initFileTransfer();
            transferActive = true;
        }
        // Write data to file
        writeToFile(offset, blockData);
		totalReceivedBytes += blockData.size();
        // Final transfer verification
        // finalizeTransfer();
	} catch (const std::exception& e) {
        std::cerr << "FWT Error: " << e.what() << std::endl;
        replyPacket.comres = 2;  // Generic error code
        resetTransfer();
    }
	replyPacket.data.clear();
    replyPacket.length = 20; // SPIMAGIC + LENGTH + SEQNO + COMRES + CRC1
    if (!replyPacket.data.empty()) {
        replyPacket.length += replyPacket.data.size() + 4; // DATA + CRC2
    }
    return spiCmd->constructSPIReplyPacket(replyPacket);
}
// Private method implementations
void SpiFWTCmdCommand::initFileTransfer() {
    constexpr const char* FILE_PATH = "/mnt/WSS_Backup.elf";
    // Clean up residual files
    if (access(FILE_PATH, F_OK) == 0) {
        if (remove(FILE_PATH) != 0) {
            throw std::runtime_error("Failed to clean up old file");
        }
    }
    // Create new file and set permissions
    std::ofstream tmp(FILE_PATH, std::ios::binary);
    if (!tmp) throw std::runtime_error("File creation failed");
    tmp.close();
    chmod(FILE_PATH, S_IRUSR | S_IWUSR);
    // Open file and maintain write state
    elfFile.open(FILE_PATH, std::ios::in | std::ios::out | std::ios::binary);
    if (!elfFile) throw std::runtime_error("File open failed");

}
void SpiFWTCmdCommand::writeToFile(uint32_t offset, const std::vector<uint8_t>& data) {
    elfFile.seekp(offset);
    if (elfFile.fail()) {
        throw std::runtime_error("Seek to 0x" + std::to_string(offset) + " failed");
    }
    elfFile.write(reinterpret_cast<const char*>(data.data()), data.size());
    if (elfFile.fail()) {
        throw std::runtime_error("Data write failed");
    }
    elfFile.flush();  // Ensure data persistence
}
void SpiFWTCmdCommand::finalizeTransfer() {
    // Close file and reset state
    if (elfFile.is_open()) {
        elfFile.close();
    }
    transferActive = false;
    totalReceivedBytes = 0;
}
void SpiFWTCmdCommand::resetTransfer() {
	if (elfFile.is_open()) {
        elfFile.close();
    }
    transferActive = false;
    totalReceivedBytes = 0;
    // Clean up temporary file
    constexpr const char* FILE_PATH = "/mnt/WSS_Backup.elf";
    if (access(FILE_PATH, F_OK) == 0) {
        remove(FILE_PATH);
    }
}

/***********************************************************************************************
 * SPI Command processing functions definitions below: 0x0018
 * ********************************************************************************************/
bool SpiFWSCmdCommand::parse(const SPICommandPacket& packetData) {
    if (packetData.data.size() < 1) return false; // At least 1 byte of storage position parameter is required
    storagePosition = packetData.data[0]; // Get the storage position (0, 1, or 2)
    if (storagePosition < 0 || storagePosition > 2) return false; // Parameter validation
    return true; // Successfully parsed command
}
std::vector<uint8_t> SpiFWSCmdCommand::process(uint32_t seqNo) {
    // Implement your command-specific processing logic here
    SPIReplyPacket replyPacket;
    replyPacket.spiMagic = SPIMAGIC;
    replyPacket.comres = 0; // Default success
    try {
		if (SpiFWTCmdCommand::transferActive) {
            SpiFWTCmdCommand::finalizeTransfer();
            std::cout << "FWT: Transfer finalized by FWE command" << std::endl;
        }
        // Define the firmware file paths
        const std::string sourcePath = "/mnt/WSS_Backup.elf";
        const std::string targetPath = "/mnt/WSS_Backup" + std::to_string(storagePosition) + ".elf";
        // Check if the source file exists
        if (access(sourcePath.c_str(), F_OK) != 0) {
            throw std::runtime_error("Source firmware file does not exist");
        }
        // Rename the file
        if (rename(sourcePath.c_str(), targetPath.c_str()) != 0) {
            throw std::runtime_error("Failed to rename firmware file");
        }
        // Set file permissions
        chmod(targetPath.c_str(), S_IRUSR | S_IWUSR);
        std::cout << "Firmware stored successfully as " << targetPath << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "FWS Error: " << e.what() << std::endl;
        replyPacket.comres = 0x02; // Error code
    }
    replyPacket.seqNo = seqNo;
	replyPacket.length = 20; // SPIMAGIC + LENGTH + SEQNO + COMRES + CRC1
    if (!replyPacket.data.empty()) {
        replyPacket.length += replyPacket.data.size() + 4; // DATA + CRC2
    }
    return spiCmd->constructSPIReplyPacket(replyPacket);
}

/***********************************************************************************************
 * SPI Command processing functions definitions below: 0x0019
 * ********************************************************************************************/
bool SpiFWLCmdCommand::parse(const SPICommandPacket& packetData) {
    if (packetData.data.size() < 1) return false; // At least 1 byte of load position parameter is required
    loadPosition = packetData.data[0]; // Get the load position (0, 1, or 2)
    if (loadPosition < 0 || loadPosition > 2) return false; // Parameter validation
    return true; // Successfully parsed command
}
std::vector<uint8_t> SpiFWLCmdCommand::process(uint32_t seqNo) {
    // Implement your command-specific processing logic here
    SPIReplyPacket replyPacket;
    replyPacket.spiMagic = SPIMAGIC;
    replyPacket.comres = 0; // Default success
    try {
        // Define the firmware file path
        const std::string firmwarePath = "/mnt/WSS_Backup" + std::to_string(loadPosition) + ".elf";
        // Check if the firmware file exists
        if (access(firmwarePath.c_str(), F_OK) != 0) {
            throw std::runtime_error("Firmware file does not exist");
        }
        // Write to the configuration file to record the currently loaded firmware version
        std::ofstream configFile("/mnt/firmware.conf");
        if (!configFile) {
            throw std::runtime_error("Failed to open firmware config file");
        }
        configFile << "FIRMWARE=" << firmwarePath << std::endl;
        configFile.close();
        std::cout << "Firmware load configuration updated successfully" << std::endl;
		// Delete firmware_flag 
		const std::string flagPath = "/mnt/firmware_flag";
		if (access(flagPath.c_str(), F_OK) == 0) {
			std::cout << "Deleting firmware_flag..." << std::endl;
			if (remove(flagPath.c_str()) != 0) {
				std::cerr << "Warning: Delete failed: " << strerror(errno) << std::endl;
			}
		}
		// Reboot with startwss.elf
		const std::string mainAppPath = "/mnt/startwss.elf";
		// Step 1: Verify main application existence
		if (access(mainAppPath.c_str(), F_OK) != 0) {
			throw std::runtime_error("Main application not found");
		}
		// Step 2: Close non-standard file descriptors
		int max_fd = sysconf(_SC_OPEN_MAX);
		for (int fd = 3; fd < max_fd; ++fd) { 
			close(fd); // Ignore EBADF errors
		}
		// Step 3: Execute process replacement
		char* argv[] = {const_cast<char*>(mainAppPath.c_str()), nullptr};
		execv(mainAppPath.c_str(), argv);
		// If reached here, execv failed
		throw std::runtime_error("Exec failed: " + std::string(strerror(errno)));
		/*
        // Trigger a reboot
        std::cout << "Rebooting system to load firmware..." << std::endl;
		sync();
        system("reboot");
		*/
    } catch (const std::exception& e) {
        std::cerr << "FWL Error: " << e.what() << std::endl;
        replyPacket.comres = 0x02; // Error code
    }
    replyPacket.seqNo = seqNo;
	replyPacket.length = 20; // SPIMAGIC + LENGTH + SEQNO + COMRES + CRC1
    if (!replyPacket.data.empty()) {
        replyPacket.length += replyPacket.data.size() + 4; // DATA + CRC2
    }
    return spiCmd->constructSPIReplyPacket(replyPacket);
}
/***********************************************************************************************
 * SPI Command processing functions definitions below: 0x001A
 * ********************************************************************************************/
bool SpiFWPQueryCommand::parse(const SPICommandPacket& packetData) {
	return true; // Successfully parsed command
}

std::vector<uint8_t> SpiFWPQueryCommand::process(uint32_t seqNo) {
    // Implement your command-specific processing logic here
    SPIReplyPacket replyPacket;
    replyPacket.spiMagic = SPIMAGIC;
    replyPacket.comres = 0; // Default success
    uint8_t firmwarePosition = 0; // Default position is 0
    try {
        // Open the configuration file
        std::ifstream configFile("/mnt/firmware.conf");
        if (!configFile) {
            throw std::runtime_error("Failed to open firmware config file");
        }
        // Read the configuration file content
        std::string line;
        while (std::getline(configFile, line)) {
            // Look for the line starting with "FIRMWARE="
			if (line.find("FIRMWARE=") == 0) {
				std::cout << "FIRMWARE line content: " << line << std::endl;  
				// Extract the firmware path
				std::string firmwarePath = line.substr(9); // Skip "FIRMWARE="
				size_t lastSlash = firmwarePath.find_last_of('/');
				std::string filename;
				// Extract filename from path
				if (lastSlash != std::string::npos) {
					filename = firmwarePath.substr(lastSlash + 1); // "/mnt/WSS_Backup2.elf" → "WSS_Backup2.elf"
				} else {
					filename = firmwarePath;
				}
				// Remove file extension (.elf)
				size_t dotPos = filename.find_last_of('.');
				std::string basename = (dotPos != std::string::npos) ? 
									filename.substr(0, dotPos) : 
									filename; // "WSS_Backup2.elf" → "WSS_Backup2"
				// Find position of "Backup" keyword
				size_t backupPos = basename.find("Backup");
				if (backupPos != std::string::npos) {
					// Extract position number after "Backup"
					size_t numPos = backupPos + 6; // "Backup" is 6 characters long
					if (numPos < basename.size()) {
						char posChar = basename[numPos]; // "WSS_Backup2" → '2'
						if (posChar >= '0' && posChar <= '2') {
							firmwarePosition = static_cast<uint8_t>(posChar - '0');
							std::cout << "Current firmware position: " << static_cast<int>(firmwarePosition) << std::endl;
						} else {
							throw std::runtime_error("Invalid firmware position in filename");
						}
					} else {
						throw std::runtime_error("No position number after 'Backup'");
					}
				} else {
					throw std::runtime_error("'Backup' keyword not found in filename");
				}
				break;
            }
        }
        configFile.close();
    } catch (const std::exception& e) {
        std::cerr << "FWP? Error: " << e.what() << std::endl;
        replyPacket.comres = 0x02; // Error code
    }
    // Add the firmware position to the response data
    replyPacket.data.push_back(firmwarePosition);
    replyPacket.seqNo = seqNo;
	replyPacket.length = 20; // SPIMAGIC + LENGTH + SEQNO + COMRES + CRC1
    if (!replyPacket.data.empty()) {
        replyPacket.length += replyPacket.data.size() + 4; // DATA + CRC2
    }
    return spiCmd->constructSPIReplyPacket(replyPacket);
}

/***********************************************************************************************
 * SPI Command processing functions definitions below: 0x001B
 * ********************************************************************************************/
bool SpiFWECmdCommand::parse(const SPICommandPacket& packetData) {
	return true; // Successfully parsed command
}

std::vector<uint8_t> SpiFWECmdCommand::process(uint32_t seqNo) {
    // Implement your command-specific processing logic here
	SPIReplyPacket replyPacket;
	replyPacket.spiMagic = SPIMAGIC;
	replyPacket.comres = 0; // Default success
	try {
		if (SpiFWTCmdCommand::transferActive) {
            SpiFWTCmdCommand::finalizeTransfer();
            std::cout << "FWT: Transfer finalized by FWE command" << std::endl;
        }

		const std::string firmwarePath = "/mnt/WSS_Backup.elf";
		
		// Step 1: Verify firmware existence
		if (access(firmwarePath.c_str(), F_OK) != 0) {
			throw std::runtime_error("Backup firmware not found");
		}
		// Step 2: Set executable permissions
		if (chmod(firmwarePath.c_str(), 0777) != 0) {  // Note: 0777 is octal format
			throw std::runtime_error("Permission setting failed: " + std::string(strerror(errno)));
		}
		// Step 3: Close all non-standard file descriptors
		int max_fd = sysconf(_SC_OPEN_MAX);
		for (int fd = 3; fd < max_fd; ++fd) {
			close(fd);  // Errors are ignored (invalid fds return EBADF)
		}
		// Step 4: Perform process replacement
		char* argv[] = {const_cast<char*>(firmwarePath.c_str()), nullptr};
		execv(firmwarePath.c_str(), argv);
		// If execution reaches here, execv failed
		throw std::runtime_error("Execution failed: " + std::string(strerror(errno)));
	} catch (const std::runtime_error& e) {
		std::cerr << "FWE Error: " << e.what() << std::endl;
		replyPacket.comres = 0x02;  // Unified error code
	}
	replyPacket.seqNo = seqNo;
	replyPacket.length = 20; // SPIMAGIC + LENGTH + SEQNO + COMRES + CRC1
    if (!replyPacket.data.empty()) {
        replyPacket.length += replyPacket.data.size() + 4; // DATA + CRC2
    }
	return spiCmd->constructSPIReplyPacket(replyPacket);

}

