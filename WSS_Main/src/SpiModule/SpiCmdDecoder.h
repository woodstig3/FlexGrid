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
#include "CmdDecoder.h"

// Define constants for SPI interface packet start delimiter
const uint32_t SPIMAGIC = 0x0F1E2D3C;


class BaseCommand {
public:
    virtual ~BaseCommand() = default;
    virtual bool parse(const SPICommandPacket& commandPacket) = 0; // Modify to use SPICommandPacket
    virtual std::vector<uint8_t> process() = 0; // Command-specific processing

};
/*****************************************************************
 * define SPI commands and corresponding processes handlers class.
 *****************************************************************/
struct Config_For_Prod {

	uint8_t sus;
	uint32_t hwr;
	uint32_t fwr;
	std::string sno;
	std::string mfd;
	std::string lbl;
	std::string mid;

};
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

    Config_For_Prod conf_spi;
    int loadProdIniConf(void);
    int modifyIniValue(const std::string& section, const std::string& key, const std::string& newValue);


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

//Second step: Define class for each command object
//0x0001 command: No Operation command processing
class SpiNOPCmdCommand: public BaseCommand {
public:
    SpiNOPCmdCommand(SpiCmdDecoder* cmd) : spiCmd(cmd) {};
	SpiCmdDecoder* spiCmd;
    // Function to parse No Operation command data for SPI, e.g.: 0x0001
    virtual bool parse(const SPICommandPacket& packetData) override;
    virtual std::vector<uint8_t> process() override;
};

//0x0002 command: Reset command processing
class SpiResetCmdCommand: public BaseCommand {
public:
    SpiResetCmdCommand(SpiCmdDecoder* cmd) : spiCmd(cmd) {};
	SpiCmdDecoder* spiCmd;
    // Function to parse Reset command data for SPI, e.g.: 0x0002
    virtual bool parse(const SPICommandPacket& packetData) override;
    virtual std::vector<uint8_t> process() override;
};

//0x0003 command: Store command processing
class SpiStoreCmdCommand: public BaseCommand {
public:
    SpiStoreCmdCommand(SpiCmdDecoder* cmd) : spiCmd(cmd) {};
	SpiCmdDecoder* spiCmd;
    // Function to parse Store command data for SPI, e.g.: 0x0003
    virtual bool parse(const SPICommandPacket& packetData) override;
    virtual std::vector<uint8_t> process() override;

    static std::unique_ptr<CmdDecoder> g_cmdDecoder;
};

//0x0004 command: Start-up State command processing
class SpiSUSCmdCommand: public BaseCommand {
public:
    SpiSUSCmdCommand(SpiCmdDecoder* cmd) : spiCmd(cmd) {};
	SpiCmdDecoder* spiCmd;
    // Function to parse Store command data for SPI, e.g.: 0x0004
    virtual bool parse(const SPICommandPacket& packetData) override;
    virtual std::vector<uint8_t> process() override;

};

//0x0005 command: Start Factory Default command processing
class SpiSFDCmdCommand: public BaseCommand {
public:
    SpiSFDCmdCommand(SpiCmdDecoder* cmd) : spiCmd(cmd) {};
	SpiCmdDecoder* spiCmd;
    // Function to parse Store command data for SPI, e.g.: 0x0005
    virtual bool parse(const SPICommandPacket& packetData) override;
    virtual std::vector<uint8_t> process() override;

};

//0x0006 command: Start Factory Default command processing
class SpiSLSCmdCommand: public BaseCommand {
public:
    SpiSLSCmdCommand(SpiCmdDecoder* cmd) : spiCmd(cmd) {};
	SpiCmdDecoder* spiCmd;
    // Function to parse Store command data for SPI, e.g.: 0x0006
    virtual bool parse(const SPICommandPacket& packetData) override;
    virtual std::vector<uint8_t> process() override;

};

//0x0007 command: Hardware Release command processing
class SpiHWRCmdCommand: public BaseCommand {
public:
    SpiHWRCmdCommand(SpiCmdDecoder* cmd) : spiCmd(cmd) {};
	SpiCmdDecoder* spiCmd;
    // Function to parse Store command data for SPI, e.g.: 0x0007
    virtual bool parse(const SPICommandPacket& packetData) override;
    virtual std::vector<uint8_t> process() override;

    uint32_t hwr=00000000;

};

//0x0008 command: Firmware Release command processing
class SpiFWRCmdCommand: public BaseCommand {
public:
    SpiFWRCmdCommand(SpiCmdDecoder* cmd) : spiCmd(cmd) {};
    SpiCmdDecoder* spiCmd;
    // Function to parse Store command data for SPI, e.g.: 0x0008
    virtual bool parse(const SPICommandPacket& packetData) override;
    virtual std::vector<uint8_t> process() override;

    uint32_t fwr=00000000;

};

//0x0009 command: Serial Number command processing
class SpiSNOCmdCommand: public BaseCommand {
public:
    SpiSNOCmdCommand(SpiCmdDecoder* cmd) : spiCmd(cmd) {};
    SpiCmdDecoder* spiCmd;
    // Function to parse Store command data for SPI, e.g.: 0x0009
    virtual bool parse(const SPICommandPacket& packetData) override;
    virtual std::vector<uint8_t> process() override;

    std::string sno="SN000001";

};

//0x000A command: ManuFacturing Date command processing
class SpiMFDCmdCommand: public BaseCommand {
public:
    SpiMFDCmdCommand(SpiCmdDecoder* cmd) : spiCmd(cmd) {};
    SpiCmdDecoder* spiCmd;
    // Function to parse Store command data for SPI, e.g.: 0x000A
    virtual bool parse(const SPICommandPacket& packetData) override;
    virtual std::vector<uint8_t> process() override;

    std::string mfd="31122024";

};

//0x000B command: Label Query command processing
class SpiLBLCmdCommand: public BaseCommand {
public:
    SpiLBLCmdCommand(SpiCmdDecoder* cmd) : spiCmd(cmd) {};
    SpiCmdDecoder* spiCmd;
    // Function to parse Store command data for SPI, e.g.: 0x000B
    virtual bool parse(const SPICommandPacket& packetData) override;
    virtual std::vector<uint8_t> process() override;

    std::string lbl="Device's Part Number";

};

//0x000C commands: Module Modification Set and Query command processing
class SpiMIDACmdCommand: public BaseCommand {
public:
    SpiMIDACmdCommand(SpiCmdDecoder* cmd) : spiCmd(cmd) {};
    SpiCmdDecoder* spiCmd;
    // Function to parse Store command data for SPI, e.g.: 0x000C
    virtual bool parse(const SPICommandPacket& packetData) override;
    virtual std::vector<uint8_t> process() override;

    std::string lbl="CustomerDefinedString";  //255 characters in ascii code

};

//0x000D commands: Module Modification Query command processing
class SpiMIDQCmdCommand: public BaseCommand {
public:
    SpiMIDQCmdCommand(SpiCmdDecoder* cmd) : spiCmd(cmd) {};
    SpiCmdDecoder* spiCmd;
    // Function to parse Store command data for SPI, e.g.: 0x000C
    virtual bool parse(const SPICommandPacket& packetData) override;
    virtual std::vector<uint8_t> process() override;

    std::string lbl="CustomerDefinedString";

};

//0x000F command: Operational Status command processing
class SpiOSSCmdCommand: public BaseCommand {
public:
    SpiOSSCmdCommand(SpiCmdDecoder* cmd) : spiCmd(cmd) {};
    SpiCmdDecoder* spiCmd;
    // Function to parse Store command data for SPI, e.g.: 0x000F
    virtual bool parse(const SPICommandPacket& packetData) override;
    virtual std::vector<uint8_t> process() override;

    uint16_t oss = 0xAABB;

};

//0x0010 command: Hardware Status command processing
class SpiHSSCmdCommand: public BaseCommand {
public:
    SpiHSSCmdCommand(SpiCmdDecoder* cmd) : spiCmd(cmd) {};
    SpiCmdDecoder* spiCmd;
    // Function to parse Store command data for SPI, e.g.: 0x0010
    virtual bool parse(const SPICommandPacket& packetData) override;
    virtual std::vector<uint8_t> process() override;

    uint16_t hss = 0xAABB;

};

//0x0011 command: Hardware LAtched Status command processing
class SpiLSSCmdCommand: public BaseCommand {
public:
    SpiLSSCmdCommand(SpiCmdDecoder* cmd) : spiCmd(cmd) {};
    SpiCmdDecoder* spiCmd;
    // Function to parse Store command data for SPI, e.g.: 0x0011
    virtual bool parse(const SPICommandPacket& packetData) override;
    virtual std::vector<uint8_t> process() override;

    uint16_t hss = 0xAABB;

};

//0x0012 command: Clear Latched Error(oss? and lss?) command processing
class SpiCLECmdCommand: public BaseCommand {
public:
    SpiCLECmdCommand(SpiCmdDecoder* cmd) : spiCmd(cmd) {};
    SpiCmdDecoder* spiCmd;
    // Function to parse Store command data for SPI, e.g.: 0x0012
    virtual bool parse(const SPICommandPacket& packetData) override;
    virtual std::vector<uint8_t> process() override;

    uint16_t hss = 0xAABB;

};

//0x0013 command: Case Temperature Status command processing
class SpiCSSCmdCommand: public BaseCommand {
public:
    SpiCSSCmdCommand(SpiCmdDecoder* cmd) : spiCmd(cmd) {};
    SpiCmdDecoder* spiCmd;
    // Function to parse Store command data for SPI, e.g.: 0x0013
    virtual bool parse(const SPICommandPacket& packetData) override;
    virtual std::vector<uint8_t> process() override;

    int16_t hss;

};

//0x0014 command: Internal Temperature Status command processing
class SpiISSCmdCommand: public BaseCommand {
public:
    SpiISSCmdCommand(SpiCmdDecoder* cmd) : spiCmd(cmd) {};
    SpiCmdDecoder* spiCmd;
    // Function to parse Store command data for SPI, e.g.: 0x0014
    virtual bool parse(const SPICommandPacket& packetData) override;
    virtual std::vector<uint8_t> process() override;

    int16_t hss;

};

//0x0015 command SPA processing, e.g.:0x0015 1, 1:8 1:1
struct SPACommandStruct {
    uint32_t opcode;                    // 0x0015
    uint8_t w;                          // WSS module number [1|2]
    std::vector<std::pair<uint16_t, uint16_t>> sliceRanges; // Slice ranges
    std::vector<uint8_t> commonPorts;  // Common ports
    std::vector<uint8_t> switchingPorts; // Switching ports
    std::vector<int16_t> attenuations; // Attenuation values
};

class SPASlicePortAttenuationCommand : public BaseCommand {
public:
    SPASlicePortAttenuationCommand(SpiCmdDecoder* cmd) : spiCmd(cmd) {};
	SpiCmdDecoder* spiCmd;
    static std::unique_ptr<CmdDecoder> g_cmdDecoder;
	uint8_t w; // WSS module number
    std::vector<std::pair<uint16_t, uint16_t>> sliceRanges;
    std::vector<uint8_t> commonPorts;
    std::vector<uint8_t> switchingPorts;
    std::vector<int16_t> attenuations;

    // Function to parse SPA command data for SPI, e.g.: 0x0015 1, 1:8 1:1
    virtual bool parse(const SPICommandPacket& packetData) override;
    virtual std::vector<uint8_t> process() override;

private:
    float sliceSize = 3.125; //6.25/12.5GHz
    double freqBySlice(uint16_t sliceNo) {
    	return((sliceNo-1)*sliceSize);
    };
};

//0x0017 command: Firmware Transfer command processing
class SpiFWTCmdCommand: public BaseCommand {
public:
    SpiFWTCmdCommand(SpiCmdDecoder* cmd) : spiCmd(cmd) {};
    SpiCmdDecoder* spiCmd;
    // Function to parse Store command data for SPI, e.g.: 0x0014
    virtual bool parse(const SPICommandPacket& packetData) override;
    virtual std::vector<uint8_t> process() override;

};

//0x0018 command: Firmware Store command processing
class SpiFWSCmdCommand: public BaseCommand {
public:
    SpiFWSCmdCommand(SpiCmdDecoder* cmd) : spiCmd(cmd) {};
    SpiCmdDecoder* spiCmd;
    // Function to parse Store command data for SPI, e.g.: 0x0018<O><File>
    virtual bool parse(const SPICommandPacket& packetData) override;
    virtual std::vector<uint8_t> process() override;

    uint8_t fwPosition = 0; // 0/1/2

};

//0x0019 command: Firmware Load command processing
class SpiFWLCmdCommand: public BaseCommand {
public:
    SpiFWLCmdCommand(SpiCmdDecoder* cmd) : spiCmd(cmd) {};
    SpiCmdDecoder* spiCmd;
    // Function to parse Store command data for SPI, e.g.: 0x0018<O><File>
    virtual bool parse(const SPICommandPacket& packetData) override;
    virtual std::vector<uint8_t> process() override;

    uint8_t fwPosition = 0; // 0/1/2

};

//0x001A command: Firmware Postion Query command processing
class SpiFWPCmdCommand: public BaseCommand {
public:
    SpiFWPCmdCommand(SpiCmdDecoder* cmd) : spiCmd(cmd) {};
    SpiCmdDecoder* spiCmd;
    // Function to parse Store command data for SPI, e.g.: 0x0018
    virtual bool parse(const SPICommandPacket& packetData) override;
    virtual std::vector<uint8_t> process() override;

};


//0x001B command: Firmware Execute command processing
class SpiFWECmdCommand: public BaseCommand {
public:
    SpiFWECmdCommand(SpiCmdDecoder* cmd) : spiCmd(cmd) {};
    SpiCmdDecoder* spiCmd;
    // Function to parse Store command data for SPI, e.g.: 0x0018
    virtual bool parse(const SPICommandPacket& packetData) override;
    virtual std::vector<uint8_t> process() override;

};











#endif /* SRC_SPIMODULE_SPICMDDECODER_H_ */
