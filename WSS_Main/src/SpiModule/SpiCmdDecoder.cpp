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

#include "SpiCmdDecoder.h"

SpiCmdDecoder::SpiCmdDecoder()
{
        // Register command handlers (could be expanded)
//    	commandHandlers[0x0001] = &SpiCmdDecoder::handleNoOperation; // SPA command.
//    	commandHandlers[0x0015] = &SpiCmdDecoder::handleSPACmd; // SPA command.

    	commandHandlers[0x0001] = [this]() { return std::make_unique<SpiNOPCmdCommand>(this); };
    	commandHandlers[0x0015] = [this]() { return std::make_unique<SPASlicePortAttenuationCommand>(this); };
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
/***********************************************************************************************
 * SPI Command processing functions definitions below: 0x0001
 * ********************************************************************************************/
bool SpiNOPCmdCommand::parse(const SPICommandPacket& packetData) {

	return true; // Successfully parsed command
}

std::vector<uint8_t> SpiNOPCmdCommand::process() {
        // Implement your command-specific processing logic here
// fill patternGen struct with arguments extracted from command


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
 * SPI Command processing functions definitions below: 0x0015
 * ********************************************************************************************/

//spi command SPA processing, e.g.:0x0015 1 1:8 1:2 10 9:10 1:3 20
bool SPASlicePortAttenuationCommand::parse(const SPICommandPacket& packetData)
{
	// First byte is the WSS module number
	w = packetData.data[0];
	std::cout << "WSS number: " << packetData.data[0] << std::endl;

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
		std::cout << "COMMONPort: " << packetData.data[index] << " SWITCHPort: " << packetData.data[index+1] << std::endl;
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
        replyPacket.length = 16; // example length
        replyPacket.seqNo = sizeof(SPIReplyPacket);
        replyPacket.comres = 0; // Indicate success
        replyPacket.data.clear(); // Populate if necessary
        replyPacket.crc1 = spiCmd->calculateCRC(replyPacket); // Implement CRC calculation

        return spiCmd->constructSPIReplyPacket(replyPacket);

}


