/*
 * spiCmdDecoder.h
 *
 *  Created on: Dec 27, 2024
 *      Author: Administrator
 */

#ifndef SRC_SPIMODULE_SPICMDDECODER_H_
#define SRC_SPIMODULE_SPICMDDECODER_H_


#include <iostream>
#include <cstring>
#include <vector>
#include <sstream>
#include <cstdint>
#include <map>
#include <functional>
#include <memory>
#include "SpiStructs.h"


// Define constants for SPI interface packet start delimiter
const uint32_t SPIMAGIC = 0x0F1E2D3C;

class BaseCommand {
public:
    virtual ~BaseCommand() = default;
    virtual bool parse(const SPICommandPacket& commandPacket) = 0; // Modify to use SPICommandPacket
    virtual std::vector<uint8_t> process() = 0; // Command-specific processing

    uint16_t bytesToInt16BigEndian(const std::vector<uint8_t>& bytes, size_t offset) {
        // Ensure there are enough bytes
        if (bytes.size() < offset + 2) {
            throw std::out_of_range("Not enough bytes to read a 16-bit integer");
        }

        // Combine the bytes into a 16-bit integer (big-endian)
        uint16_t result = (static_cast<uint16_t>(bytes[offset]) << 8) |
                          (static_cast<uint16_t>(bytes[offset + 1]));

        return result;
    }
};

/*****************************************************************
 * define SPI commands and corresponding processes handlers class.
 *****************************************************************/
class SpiCmdDecoder {
public:
    // Constructor
    SpiCmdDecoder();

    // Main method to process the incoming SPI command packet
    std::vector<uint8_t> processSPIPacket(const SPICommandPacket& commandPacket);

    // Function to construct SPI reply packet
    std::vector<uint8_t> constructSPIReplyPacket(const SPIReplyPacket& replyPacket);
    // Method to construct an error reply packet based on error codes
    std::vector<uint8_t> constructErrorReply(uint8_t errorCode);

    // Sample CRC calculation (to be implemented based on your specification)
    uint32_t calculateCRC(const SPIReplyPacket& packet);        // Implement your CRC logic based on the IEEE 802.3 standard

    uint16_t bytesToInt16BigEndian(const std::vector<uint8_t>& bytes, size_t offset);

private:
//    using CommandHandler = std::vector<uint8_t> (SpiCmdDecoder::*)(const std::vector<uint8_t>&);
//    std::map<uint32_t, CommandHandler> commandHandlers; // Map of opcodes to handlers
	std::map<uint32_t, std::function<std::unique_ptr<BaseCommand>()>> commandHandlers;
/*
    // Function to decode the incoming packet and extract the opcode
    bool decodeOpCode(const std::vector<uint8_t>& packetData, uint32_t& opcode) {
        if (packetData.size() < 8) { // Example size check (adjust based on your protocol)
            return false; // Packet is too short
        }
        // Extract opcode (assuming it's at a specific offset, e.g., 12)
        opcode = *reinterpret_cast<const uint32_t*>(&packetData[12]); // Modify offset as needed
        return true;
    }

    //Function to parse NoOperation command for SPI
    bool parseNOPCmd(const uint8_t* packetData, size_t packetLength, NOPCommand& command) {
    	if (packetLength < 1) {
    		return false; // Packet is too short
    	}
    	//No Operation required, Rely OK immediately
    	// Build a successful reply packet
		SPIReplyPacket replyPacket;
		replyPacket.spiMagic = SPIMAGIC;
		replyPacket.length = 16; // Adjust based on response
		replyPacket.seqNo = command.w; // Example: use WSS module number as seq
		replyPacket.comres = 0; // No error
		replyPacket.data.clear(); // Clear or set necessary response data
		replyPacket.crc1 = calculateCRC(replyPacket); // Need to implement CRC calculation

		return constructSPIReplyPacket(replyPacket);

    }

    // Command handler for the SPA command
    std::vector<uint8_t> handleSPACmd(const std::vector<uint8_t>& packetData) {
        // Create and parse the SPA command structure
        SPASlicePortAttenuationCommand command;
        if (parseSPACmd(packetData.data(), packetData.size(), command)) {
            // Process the command
            return processCommand(command);
        } else {
            // Generate an error reply for invalid SPA command
            return constructErrorReply(0x03); // Command parsing error
        }
    }

    // Method to process specific command (e.g., SPA)
    std::vector<uint8_t> processCommand(const SPASlicePortAttenuationCommand& command) {
        // Implement your processing logic for SPA command here
        std::cout << "Processing SPA command, WSS Module: " << static_cast<int>(command.w) << std::endl;

        // Build a successful reply packet
        SPIReplyPacket replyPacket;
        replyPacket.spiMagic = SPIMAGIC;
        replyPacket.length = 16; // Adjust based on response
        replyPacket.seqNo = command.w; // Example: use WSS module number as seq
        replyPacket.comres = 0; // No error
        replyPacket.data.clear(); // Clear or set necessary response data
        replyPacket.crc1 = calculateCRC(replyPacket); // Need to implement CRC calculation

        return constructSPIReplyPacket(replyPacket);
    }
*/


};


struct SPACommandStruct {
    uint32_t opcode;                    // 0x0015
    uint8_t w;                          // WSS module number [1|2]
    std::vector<std::pair<uint16_t, uint16_t>> sliceRanges; // Slice ranges
    std::vector<uint8_t> commonPorts;  // Common ports
    std::vector<uint8_t> switchingPorts; // Switching ports
    std::vector<int16_t> attenuations; // Attenuation values
};


class SpiNOPCmdCommand: public BaseCommand {
public:

	SpiCmdDecoder* spiCmd;

    SpiNOPCmdCommand(SpiCmdDecoder* cmd) : spiCmd(cmd) {};

    // Function to parse SPA command data for SPI, e.g.: 0x0015 1, 1:8 1:1
    virtual bool parse(const SPICommandPacket& packetData) override;
    virtual std::vector<uint8_t> process() override;
};

//spi command SPA processing, e.g.:0x0015 1, 1:8 1:1
class SPASlicePortAttenuationCommand : public BaseCommand {
public:

	SpiCmdDecoder* spiCmd;
	uint8_t w; // WSS module number
    std::vector<std::pair<uint16_t, uint16_t>> sliceRanges;
    std::vector<uint8_t> commonPorts;
    std::vector<uint8_t> switchingPorts;
    std::vector<int16_t> attenuations;

    SPASlicePortAttenuationCommand(SpiCmdDecoder* cmd) : spiCmd(cmd) {};

    // Function to parse SPA command data for SPI, e.g.: 0x0015 1, 1:8 1:1
    virtual bool parse(const SPICommandPacket& packetData) override;
    virtual std::vector<uint8_t> process() override;
};



#endif /* SRC_SPIMODULE_SPICMDDECODER_H_ */
