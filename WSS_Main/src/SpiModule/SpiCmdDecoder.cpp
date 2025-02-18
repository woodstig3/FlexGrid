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

#include "SpiCmdDecoder.h"


SpiCmdDecoder::SpiCmdDecoder()
{
        // First step: Register command handlers (need to be expanded)
//    	commandHandlers[0x0001] = &SpiCmdDecoder::handleNoOperation; // SPA command.
//    	commandHandlers[0x0015] = &SpiCmdDecoder::handleSPACmd; // SPA command.

    	commandHandlers[0x0001] = [this]() { return std::make_unique<SpiNOPCmdCommand>(this); };  //No Operation
    	commandHandlers[0x0002] = [this]() { return std::make_unique<SpiResetCmdCommand>(this); };  //Soft Reset
    	commandHandlers[0x0015] = [this]() { return std::make_unique<SPASlicePortAttenuationCommand>(this); }; //SPA
        // Register other command handlers as needed
}

// Main method to process the incoming SPI command packet
std::vector<uint8_t> SpiCmdDecoder::processSPIPacket(const SPICommandPacket& commandPacket) {
	uint32_t opcode = commandPacket.opcode;

	auto it = commandHandlers.find(opcode);
	if (it != commandHandlers.end()) {
		std::unique_ptr<BaseCommand> command = it->second(); // Create command instance

		// Parse the command packet
		if (command->parse(commandPacket)) {
			// Process the command
			return command->process();
		} else {
			return constructErrorReply(0x02); // Command parsing error
		}
	} else {
		return constructErrorReply(0x01); // Unknown opcode error
	}
}

// Function to construct SPI reply packet
std::vector<uint8_t> SpiCmdDecoder:: constructSPIReplyPacket(const SPIReplyPacket& replyPacket) {
	std::vector<uint8_t> packet;
	packet.resize(replyPacket.length);

	// Copy the fixed-size fields
	std::memcpy(packet.data(), &replyPacket.spiMagic, 4);
	std::memcpy(packet.data() + 4, &replyPacket.length, 4);
	std::memcpy(packet.data() + 8, &replyPacket.seqNo, 4);
	std::memcpy(packet.data() + 12, &replyPacket.comres, 4);
	std::memcpy(packet.data() + replyPacket.length - 2, &replyPacket.crc1, 2);

	// Copy the variable-size data field if present
	if (replyPacket.length > 16) {
		std::memcpy(packet.data() + 16, replyPacket.data.data(), replyPacket.data.size());
		std::memcpy(packet.data() + replyPacket.length - 4, &replyPacket.crc2, 2);
	}

	return packet;
}
// Method to construct an error reply packet based on error codes
std::vector<uint8_t> SpiCmdDecoder::constructErrorReply(uint8_t errorCode) {
	// Error handling implementation...
	SPIReplyPacket replyPacket;
	replyPacket.spiMagic = SPIMAGIC;

	replyPacket.seqNo = 0; // No sequence number for errors
	replyPacket.comres = errorCode; // Error code
	replyPacket.data.clear(); // No data for errors
	replyPacket.crc1 = calculateCRC(replyPacket); // Calculate CRC for error packet
	replyPacket.length = sizeof(replyPacket); // actual size of the reply packet

	return constructSPIReplyPacket(replyPacket);
}

    // Sample CRC calculation (to be implemented based on your specification)
uint32_t SpiCmdDecoder::calculateCRC(const SPIReplyPacket& packet) {
        // Implement your CRC logic based on the IEEE 802.3 standard
        return 0; // Placeholder for actual CRC calculation
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
	                	conf_spi.sus = stoi(value);
						std::cout << "SUS: "<<conf_spi.sus << std::endl;
	                }
	                else if(key == "HWR"){
	                	conf_spi.hwr = stoi(value);
	                }
	                else if(key == "FWR"){
	                	conf_spi.fwr = stoi(value);

	                }
	                else if(key == "SNO"){
	                	conf_spi.sno = value;

	                }
	                else if(key == "MFD"){
	                	conf_spi.mfd = value;

	                }
	                else if(key == "LBL"){
	                	conf_spi.lbl = value;

	                }
	                else if(key == "MID"){
	                	conf_spi.mid = value;

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
    if (sectionFound && !keyFound) {
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

//Third step: Implement command object class member functions parse() and process()
/***********************************************************************************************
 * SPI Command processing functions definitions below: 0x0001
 * ********************************************************************************************/
bool SpiNOPCmdCommand::parse(const SPICommandPacket& packetData) {
	return true; // Successfully parsed command
}

std::vector<uint8_t> SpiNOPCmdCommand::process() {
        // Implement your command-specific processing logic here

        SPIReplyPacket replyPacket;
        replyPacket.spiMagic = SPIMAGIC;
        replyPacket.length = 0x0014; // example length
        replyPacket.seqNo = sizeof(SPIReplyPacket);
        replyPacket.comres = 0; // Indicate success
        replyPacket.data.clear(); // Populate if necessary
        replyPacket.crc1 = spiCmd->calculateCRC(replyPacket); // Implement CRC calculation

        return spiCmd->constructSPIReplyPacket(replyPacket);

}
/***********************************************************************************************
 * SPI Command processing functions definitions below: 0x0002
 * ********************************************************************************************/
bool SpiResetCmdCommand::parse(const SPICommandPacket& packetData) {
	return true; // Successfully parsed command
}

std::vector<uint8_t> SpiResetCmdCommand::process() {
    // Implement your command-specific processing logic here
	sleep(10); //sleep for time more than watchdog timer to trigger soft reset

	SPIReplyPacket replyPacket;
	replyPacket.spiMagic = SPIMAGIC;
	replyPacket.length = 0x0014; // example length
	replyPacket.seqNo = sizeof(SPIReplyPacket);
	replyPacket.comres = 0; // Indicate success
	replyPacket.data.clear(); // Populate if necessary
	replyPacket.crc1 = spiCmd->calculateCRC(replyPacket); // Implement CRC calculation

	return spiCmd->constructSPIReplyPacket(replyPacket);

}

/***********************************************************************************************
 * SPI Command processing functions definitions below: 0x0003
 * ********************************************************************************************/
std::unique_ptr<CmdDecoder> SpiStoreCmdCommand::g_cmdDecoder = nullptr;
bool SpiStoreCmdCommand::parse(const SPICommandPacket& packetData) {
	return true; // Successfully parsed command
}

std::vector<uint8_t> SpiStoreCmdCommand::process() {
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
	replyPacket.length = 0x0014; // example length
	replyPacket.seqNo = sizeof(SPIReplyPacket);
	replyPacket.comres = 0; // Indicate success
	replyPacket.data.clear(); // Populate if necessary
	replyPacket.crc1 = spiCmd->calculateCRC(replyPacket); // Implement CRC calculation

	return spiCmd->constructSPIReplyPacket(replyPacket);

}

/***********************************************************************************************
 * SPI Command processing functions definitions below: 0x0004
 * ********************************************************************************************/

bool SpiSUSCmdCommand::parse(const SPICommandPacket& packetData) {
	return true; // Successfully parsed command
}

std::vector<uint8_t> SpiSUSCmdCommand::process() {
        // Implement your command-specific processing logic here
// fill  struct with arguments extracted from command


	//Reply packet constructed below
	SPIReplyPacket replyPacket;
	replyPacket.spiMagic = SPIMAGIC;
	replyPacket.length = 0x0014; // example length
	replyPacket.seqNo = sizeof(SPIReplyPacket);
	replyPacket.comres = 0; // Indicate success
	replyPacket.data[0] = spiCmd->conf_spi.sus; // return data: 0: start factory default; 1: start last saved
	replyPacket.crc1 = spiCmd->calculateCRC(replyPacket); // Implement CRC calculation

	return spiCmd->constructSPIReplyPacket(replyPacket);

}

/***********************************************************************************************
 * SPI Command processing functions definitions below: 0x0005
 * ********************************************************************************************/
bool SpiSFDCmdCommand::parse(const SPICommandPacket& packetData) {
	return true; // Successfully parsed command
}

std::vector<uint8_t> SpiSFDCmdCommand::process() {
        // Implement your command-specific processing logic here
// fill  struct with arguments extracted from command
//write sus= start factory default:0

	//Reply packet constructed below
	SPIReplyPacket replyPacket;
	replyPacket.spiMagic = SPIMAGIC;
	replyPacket.length = 0x0014; // example length
	replyPacket.seqNo = sizeof(SPIReplyPacket);
	replyPacket.comres = 0; // Indicate success
	replyPacket.data.clear();
	replyPacket.crc1 = spiCmd->calculateCRC(replyPacket); // Implement CRC calculation

	return spiCmd->constructSPIReplyPacket(replyPacket);

}

/***********************************************************************************************
 * SPI Command processing functions definitions below: 0x0006
 * ********************************************************************************************/
bool SpiSLSCmdCommand::parse(const SPICommandPacket& packetData) {
	return true; // Successfully parsed command
}

std::vector<uint8_t> SpiSLSCmdCommand::process() {
        // Implement your command-specific processing logic here
// fill  struct with arguments extracted from command
//write sls = start last saved:1

	//Reply packet constructed below
	SPIReplyPacket replyPacket;
	replyPacket.spiMagic = SPIMAGIC;
	replyPacket.length = 0x0014; // example length
	replyPacket.seqNo = sizeof(SPIReplyPacket);
	replyPacket.comres = 0; // Indicate success
	replyPacket.data.clear();
	replyPacket.crc1 = spiCmd->calculateCRC(replyPacket); // Implement CRC calculation

	return spiCmd->constructSPIReplyPacket(replyPacket);

}

/***********************************************************************************************
 * SPI Command processing functions definitions below: 0x0007
 * ********************************************************************************************/
bool SpiHWRCmdCommand::parse(const SPICommandPacket& packetData) {
	return true; // Successfully parsed command
}

std::vector<uint8_t> SpiHWRCmdCommand::process() {
        // Implement your command-specific processing logic here
// fill  struct with arguments extracted from command


	//Reply packet constructed below
	SPIReplyPacket replyPacket;
	replyPacket.spiMagic = SPIMAGIC;
	replyPacket.length = 0x0014; // example length
	replyPacket.seqNo = sizeof(SPIReplyPacket);
	replyPacket.comres = 0; // Indicate success
	std::string hwr = std::to_string(spiCmd->conf_spi.hwr);
	replyPacket.data.assign(hwr.begin(),hwr.end());
	replyPacket.crc1 = spiCmd->calculateCRC(replyPacket); // Implement CRC calculation

	return spiCmd->constructSPIReplyPacket(replyPacket);

}

/***********************************************************************************************
 * SPI Command processing functions definitions below: 0x0008
 * ********************************************************************************************/
bool SpiFWRCmdCommand::parse(const SPICommandPacket& packetData) {
	return true; // Successfully parsed command
}

std::vector<uint8_t> SpiFWRCmdCommand::process() {
        // Implement your command-specific processing logic here
// fill  struct with arguments extracted from command


	//Reply packet constructed below
	SPIReplyPacket replyPacket;
	replyPacket.spiMagic = SPIMAGIC;
	replyPacket.length = 0x0014; // example length
	replyPacket.seqNo = sizeof(SPIReplyPacket);
	replyPacket.comres = 0; // Indicate success
	std::string fwr = std::to_string(spiCmd->conf_spi.fwr);
	replyPacket.data.assign(fwr.begin(),fwr.end());
	replyPacket.crc1 = spiCmd->calculateCRC(replyPacket); // Implement CRC calculation

	return spiCmd->constructSPIReplyPacket(replyPacket);

}

/***********************************************************************************************
 * SPI Command processing functions definitions below: 0x0009
 * ********************************************************************************************/
bool SpiSNOCmdCommand::parse(const SPICommandPacket& packetData) {
	return true; // Successfully parsed command
}

std::vector<uint8_t> SpiSNOCmdCommand::process() {
        // Implement your command-specific processing logic here
// fill  struct with arguments extracted from command


	//Reply packet constructed below
	SPIReplyPacket replyPacket;
	replyPacket.spiMagic = SPIMAGIC;
	replyPacket.length = 0x0014; // example length
	replyPacket.seqNo = sizeof(SPIReplyPacket);
	replyPacket.comres = 0; // Indicate success
	replyPacket.data.assign(spiCmd->conf_spi.sno.begin(), spiCmd->conf_spi.sno.end());
	replyPacket.crc1 = spiCmd->calculateCRC(replyPacket); // Implement CRC calculation

	return spiCmd->constructSPIReplyPacket(replyPacket);

}

/***********************************************************************************************
 * SPI Command processing functions definitions below: 0x000A
 * ********************************************************************************************/
bool SpiMFDCmdCommand::parse(const SPICommandPacket& packetData) {
	return true; // Successfully parsed command
}

std::vector<uint8_t> SpiMFDCmdCommand::process() {
        // Implement your command-specific processing logic here
// fill  struct with arguments extracted from command


	//Reply packet constructed below
	SPIReplyPacket replyPacket;
	replyPacket.spiMagic = SPIMAGIC;
	replyPacket.length = 0x0014; // example length
	replyPacket.seqNo = sizeof(SPIReplyPacket);
	replyPacket.comres = 0; // Indicate success
	replyPacket.data.assign(spiCmd->conf_spi.mfd.begin(), spiCmd->conf_spi.mfd.end());
	replyPacket.crc1 = spiCmd->calculateCRC(replyPacket); // Implement CRC calculation

	return spiCmd->constructSPIReplyPacket(replyPacket);

}

/***********************************************************************************************
 * SPI Command processing functions definitions below: 0x000B
 * ********************************************************************************************/

bool SpiLBLCmdCommand::parse(const SPICommandPacket& packetData) {
	return true; // Successfully parsed command
}

std::vector<uint8_t> SpiLBLCmdCommand::process() {
        // Implement your command-specific processing logic here
// fill  struct with arguments extracted from command


	//Reply packet constructed below
	SPIReplyPacket replyPacket;
	replyPacket.spiMagic = SPIMAGIC;
	replyPacket.length = 0x0014; // example length
	replyPacket.seqNo = sizeof(SPIReplyPacket);
	replyPacket.comres = 0; // Indicate success
	replyPacket.data.assign(spiCmd->conf_spi.lbl.begin(), spiCmd->conf_spi.lbl.end());
	replyPacket.crc1 = spiCmd->calculateCRC(replyPacket); // Implement CRC calculation

	return spiCmd->constructSPIReplyPacket(replyPacket);

}
/***********************************************************************************************
 * SPI Command processing functions definitions below: 0x000C
 * ********************************************************************************************/
bool SpiMIDACmdCommand::parse(const SPICommandPacket& packetData) {
	return true; // Successfully parsed command
}

std::vector<uint8_t> SpiMIDACmdCommand::process() {
        // Implement your command-specific processing logic here
// fill  struct with arguments extracted from command


	//Reply packet constructed below
	SPIReplyPacket replyPacket;
	replyPacket.spiMagic = SPIMAGIC;
	replyPacket.length = 0x0014; // example length
	replyPacket.seqNo = sizeof(SPIReplyPacket);
	replyPacket.comres = 0; // Indicate success
	replyPacket.data.clear();
	replyPacket.crc1 = spiCmd->calculateCRC(replyPacket); // Implement CRC calculation

	return spiCmd->constructSPIReplyPacket(replyPacket);

}

/***********************************************************************************************
 * SPI Command processing functions definitions below: 0x000D
 * ********************************************************************************************/
bool SpiMIDQCmdCommand::parse(const SPICommandPacket& packetData) {
	return true; // Successfully parsed command
}

std::vector<uint8_t> SpiMIDQCmdCommand::process() {
        // Implement your command-specific processing logic here
// fill  struct with arguments extracted from command


	//Reply packet constructed below
	SPIReplyPacket replyPacket;
	replyPacket.spiMagic = SPIMAGIC;
	replyPacket.length = 0x0014; // example length
	replyPacket.seqNo = sizeof(SPIReplyPacket);
	replyPacket.comres = 0; // Indicate success
	replyPacket.data.assign(spiCmd->conf_spi.mid.begin(), spiCmd->conf_spi.mid.end());
	replyPacket.crc1 = spiCmd->calculateCRC(replyPacket); // Implement CRC calculation

	return spiCmd->constructSPIReplyPacket(replyPacket);

}

/***********************************************************************************************
 * SPI Command processing functions definitions below: 0x000F
 * ********************************************************************************************/
bool SpiOSSCmdCommand::parse(const SPICommandPacket& packetData) {
	return true; // Successfully parsed command
}

std::vector<uint8_t> SpiOSSCmdCommand::process() {
        // Implement your command-specific processing logic here
// fill  struct with arguments extracted from command


	//Reply packet constructed below
	SPIReplyPacket replyPacket;
	replyPacket.spiMagic = SPIMAGIC;
	replyPacket.length = 0x0014; // example length
	replyPacket.seqNo = sizeof(SPIReplyPacket);
	replyPacket.comres = 0; // Indicate success
	replyPacket.data[0] = oss; //need to get from actual operational status
	replyPacket.crc1 = spiCmd->calculateCRC(replyPacket); // Implement CRC calculation

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
		index += 2;
		// Get attenuation
		int16_t attenuation = spiCmd->bytesToInt16BigEndian(packetData.data,index);
		attenuations.push_back(attenuation);
		std::cout << "Attenuation: " << attenuation << std::endl;
		index += 2;
	}
	return true; // Successfully parsed command
}

std::vector<uint8_t> SPASlicePortAttenuationCommand::process() {
// Implement your command-specific processing logic here
// fill patternGen struct with arguments extracted from command
	SPIReplyPacket replyPacket;
	replyPacket.spiMagic = SPIMAGIC;
	replyPacket.length = sizeof(SPIReplyPacket); //
	replyPacket.data.clear(); // No data attached in reply
	replyPacket.seqNo = 1;

	if(!g_cmdDecoder) {
		g_cmdDecoder = std::make_unique<CmdDecoder>();
	}

	// Lock the channel data structures for thread safety
	if (pthread_mutex_lock(&global_mutex[LOCK_CHANNEL_DS]) != 0) {
		std::cout << "global_mutex[LOCK_CHANNEL_DS] lock unsuccessful" << std::endl;
		replyPacket.comres = 1;
		replyPacket.crc1 = spiCmd->calculateCRC(replyPacket); // Implement CRC calculation
		return spiCmd->constructSPIReplyPacket(replyPacket);
	}

	try {
		// Get reference to the FG_Channel_DS for the specified WSS module
		// Update channel parameters for each slice range
		for (size_t i = 0; i < sliceRanges.size(); i++) {
			uint16_t channelNum = i + 1; // Adjust based on your channel numbering scheme

			// Update channel parameters using existing structure
			g_cmdDecoder->FG_Channel_DS_For_Pattern[w][channelNum].active = true;
			g_cmdDecoder->FG_Channel_DS_For_Pattern[w][channelNum].F1 = freqBySlice(sliceRanges[i].first);
			g_cmdDecoder->FG_Channel_DS_For_Pattern[w][channelNum].F2 = freqBySlice(sliceRanges[i].second);
			g_cmdDecoder->FG_Channel_DS_For_Pattern[w][channelNum].FC = (g_cmdDecoder->FG_Channel_DS_For_Pattern[w][channelNum].F1 + g_cmdDecoder->FG_Channel_DS_For_Pattern[w][channelNum].F2) / 2;
			g_cmdDecoder->FG_Channel_DS_For_Pattern[w][channelNum].BW = g_cmdDecoder->FG_Channel_DS_For_Pattern[w][channelNum].F2 - g_cmdDecoder->FG_Channel_DS_For_Pattern[w][channelNum].F1;
			g_cmdDecoder->FG_Channel_DS_For_Pattern[w][channelNum].slotNum = sliceRanges[i].second - sliceRanges[i].first + 1;
			// Set attenuation if available
			if (i < attenuations.size()) {
				g_cmdDecoder->FG_Channel_DS_For_Pattern[w][channelNum].slotsATTEN[i] = attenuations[i];
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

	replyPacket.crc1 = spiCmd->calculateCRC(replyPacket); // Implement CRC calculation
	return spiCmd->constructSPIReplyPacket(replyPacket);

}


