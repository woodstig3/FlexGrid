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
    virtual std::vector<uint8_t> process(uint32_t seqNo) = 0; // Command-specific processing

};
/*****************************************************************
 * define SPI commands and corresponding processes handlers class.
 *****************************************************************/
struct WareVersion {
    int year1;            // Major version (1-99)
    int year2;            // Minor version (1-99)
    int month1;   // Implementation version (0-99)
    int month2; // Release Candidate version (0-99)
};

struct Config_For_Prod {

	int sus;
    WareVersion hwr; // Hardware version
	WareVersion fwr; // Firmware version
	std::string sno; // len: 8
	std::string mfd;  //len:11
	std::string lbl;  //len:255
	std::string mid;  //len:255

};

struct Operational_Status {
	 bool isReady;
	 bool isHardwareError;
	 bool isLatchedError;
	 bool isModuleRestarted;
	 bool isStartUpActive;
	 bool isOpticsNotReady;
	 bool isOpticsDisabled;
	 bool isPending;
 };

struct Hardware_Status {
	bool caseTempError;
	bool internalTempError;
	bool tempControlShutdown;
	bool thermalShutdown;
	bool opticalControlFailure;
	bool powerSupplyError;
	bool powerRailError;
	bool internalFailure;
	bool calibrationError;
	bool singleEventUpset;
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
    //(const SPIReplyPacket& packet);        // Implement your CRC logic based on the IEEE 802.3 standard
    // 计算包头 CRC1（0x00~0x0F）
    uint32_t calculateCRC1(const uint8_t* data) ;
    // 计算全包 CRC2（0x00~[LENGTH-5]）
    uint32_t calculateCRC2(const uint8_t* data, size_t length) ;
    uint32_t calculateCRC(const uint8_t* data, size_t length);
    std::vector<uint8_t> constructSPIReplyPacketHeader(const SPIReplyPacket& replyPacket);
    std::vector<uint8_t> constructSPIReplyPacketWithoutCRC2(const SPIReplyPacket& replyPacket);
    uint16_t bytesToInt16BigEndian(const std::vector<uint8_t>& bytes, size_t offset);

    void appendUint32BigEndian(std::vector<uint8_t>& buf, uint32_t value) ;
    static Config_For_Prod conf_spi;
    static Operational_Status oss;
    static Hardware_Status hss;

    int loadProdIniConf(void);
    int modifyIniValue(const std::string& section, const std::string& key, const std::string& newValue);
    uint16_t constructOSSReplyData();
    uint16_t constructHSSReplyData();

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
    virtual std::vector<uint8_t> process(uint32_t seqNo) override;
};

//0x0002 command: Reset command processing
class SpiResetCmdCommand: public BaseCommand {
public:
    SpiResetCmdCommand(SpiCmdDecoder* cmd) : spiCmd(cmd) {};
	SpiCmdDecoder* spiCmd;
    // Function to parse Reset command data for SPI, e.g.: 0x0002
    virtual bool parse(const SPICommandPacket& packetData) override;
    virtual std::vector<uint8_t> process(uint32_t seqNo) override;
};

//0x0003 command: Store command processing
class SpiStoreCmdCommand: public BaseCommand {
public:
    SpiStoreCmdCommand(SpiCmdDecoder* cmd) : spiCmd(cmd) {};
	SpiCmdDecoder* spiCmd;
    // Function to parse Store command data for SPI, e.g.: 0x0003
    virtual bool parse(const SPICommandPacket& packetData) override;
    virtual std::vector<uint8_t> process(uint32_t seqNo) override;

    static std::unique_ptr<CmdDecoder> g_cmdDecoder;
};

//0x0004 command: Start-up State command processing
class SpiSUSQueryCommand: public BaseCommand {
public:
    SpiSUSQueryCommand(SpiCmdDecoder* cmd) : spiCmd(cmd) {};
	SpiCmdDecoder* spiCmd;
    // Function to parse Store command data for SPI, e.g.: 0x0004
    virtual bool parse(const SPICommandPacket& packetData) override;
    virtual std::vector<uint8_t> process(uint32_t seqNo) override;

};

//0x0005 command: Start Factory Default command processing
class SpiSFDCmdCommand: public BaseCommand {
public:
    SpiSFDCmdCommand(SpiCmdDecoder* cmd) : spiCmd(cmd) {};
	SpiCmdDecoder* spiCmd;
    // Function to parse Store command data for SPI, e.g.: 0x0005
    virtual bool parse(const SPICommandPacket& packetData) override;
    virtual std::vector<uint8_t> process(uint32_t seqNo) override;

};

//0x0006 command: Start Factory Default command processing
class SpiSLSCmdCommand: public BaseCommand {
public:
    SpiSLSCmdCommand(SpiCmdDecoder* cmd) : spiCmd(cmd) {};
	SpiCmdDecoder* spiCmd;
    // Function to parse Store command data for SPI, e.g.: 0x0006
    virtual bool parse(const SPICommandPacket& packetData) override;
    virtual std::vector<uint8_t> process(uint32_t seqNo) override;

};

//0x0007 command: Hardware Release command processing
class SpiHWRQueryCommand: public BaseCommand {
public:
    SpiHWRQueryCommand(SpiCmdDecoder* cmd) : spiCmd(cmd) {};
	SpiCmdDecoder* spiCmd;
    // Function to parse Store command data for SPI, e.g.: 0x0007
    virtual bool parse(const SPICommandPacket& packetData) override;
    virtual std::vector<uint8_t> process(uint32_t seqNo) override;

};

//0x0008 command: Firmware Release command processing
class SpiFWRQueryCommand: public BaseCommand {
public:
    SpiFWRQueryCommand(SpiCmdDecoder* cmd) : spiCmd(cmd) {};
    SpiCmdDecoder* spiCmd;
    // Function to parse Store command data for SPI, e.g.: 0x0008
    virtual bool parse(const SPICommandPacket& packetData) override;
    virtual std::vector<uint8_t> process(uint32_t seqNo) override;

};

//0x0009 command: Serial Number command processing
class SpiSNOQueryCommand: public BaseCommand {
public:
    SpiSNOQueryCommand(SpiCmdDecoder* cmd) : spiCmd(cmd) {};
    SpiCmdDecoder* spiCmd;
    // Function to parse Store command data for SPI, e.g.: 0x0009
    virtual bool parse(const SPICommandPacket& packetData) override;
    virtual std::vector<uint8_t> process(uint32_t seqNo) override;

};

//0x000A command: ManuFacturing Date command processing
class SpiMFDQueryCommand: public BaseCommand {
public:
    SpiMFDQueryCommand(SpiCmdDecoder* cmd) : spiCmd(cmd) {};
    SpiCmdDecoder* spiCmd;
    // Function to parse Store command data for SPI, e.g.: 0x000A
    virtual bool parse(const SPICommandPacket& packetData) override;
    virtual std::vector<uint8_t> process(uint32_t seqNo) override;

};

//0x000B command: Label Query command processing
class SpiLBLQueryCommand: public BaseCommand {
public:
    SpiLBLQueryCommand(SpiCmdDecoder* cmd) : spiCmd(cmd) {};
    SpiCmdDecoder* spiCmd;
    // Function to parse Store command data for SPI, e.g.: 0x000B
    virtual bool parse(const SPICommandPacket& packetData) override;
    virtual std::vector<uint8_t> process(uint32_t seqNo) override;

};

//0x000C commands: Module Modification Set and Query command processing
class SpiMIDAssignCmdCommand: public BaseCommand {
public:
    SpiMIDAssignCmdCommand(SpiCmdDecoder* cmd) : spiCmd(cmd) {};
    SpiCmdDecoder* spiCmd;
    // Function to parse Store command data for SPI, e.g.: 0x000C
    virtual bool parse(const SPICommandPacket& packetData) override;
    virtual std::vector<uint8_t> process(uint32_t seqNo) override;

    std::string mid="CustomerDefinedString";  //255 characters in ascii code
};

//0x000D commands: Module Modification Query command processing
class SpiMIDQueryCommand: public BaseCommand {
public:
    SpiMIDQueryCommand(SpiCmdDecoder* cmd) : spiCmd(cmd) {};
    SpiCmdDecoder* spiCmd;
    // Function to parse Store command data for SPI, e.g.: 0x000C
    virtual bool parse(const SPICommandPacket& packetData) override;
    virtual std::vector<uint8_t> process(uint32_t seqNo) override;
};

//0x000F command: Operational Status command processing
class SpiOSSQueryCommand: public BaseCommand {
public:
    SpiOSSQueryCommand(SpiCmdDecoder* cmd) : spiCmd(cmd) {};
    SpiCmdDecoder* spiCmd;
    // Function to parse Store command data for SPI, e.g.: 0x000F
    virtual bool parse(const SPICommandPacket& packetData) override;
    virtual std::vector<uint8_t> process(uint32_t seqNo) override;
};

//0x0010 command: Hardware Status command processing
class SpiHSSQueryCommand: public BaseCommand {
public:
    SpiHSSQueryCommand(SpiCmdDecoder* cmd) : spiCmd(cmd) {};
    SpiCmdDecoder* spiCmd;
    // Function to parse Store command data for SPI, e.g.: 0x0010
    virtual bool parse(const SPICommandPacket& packetData) override;
    virtual std::vector<uint8_t> process(uint32_t seqNo) override;
};

//0x0011 command: Hardware LAtched Status command processing
class SpiLSSQueryCommand: public BaseCommand {
public:
    SpiLSSQueryCommand(SpiCmdDecoder* cmd) : spiCmd(cmd) {};
    SpiCmdDecoder* spiCmd;
    // Function to parse Store command data for SPI, e.g.: 0x0011
    virtual bool parse(const SPICommandPacket& packetData) override;
    virtual std::vector<uint8_t> process(uint32_t seqNo) override;
};

//0x0012 command: Clear Latched Error(oss? and lss?) command processing
class SpiCLECmdCommand: public BaseCommand {
public:
    SpiCLECmdCommand(SpiCmdDecoder* cmd) : spiCmd(cmd) {};
    SpiCmdDecoder* spiCmd;
    // Function to parse Store command data for SPI, e.g.: 0x0012
    virtual bool parse(const SPICommandPacket& packetData) override;
    virtual std::vector<uint8_t> process(uint32_t seqNo) override;

    uint16_t hss = 0xAABB;

};

//0x0013 command: Case Temperature Status command processing
class SpiCSSQueryCommand: public BaseCommand {
public:
    SpiCSSQueryCommand(SpiCmdDecoder* cmd) : spiCmd(cmd) {};
    SpiCmdDecoder* spiCmd;
    // Function to parse Store command data for SPI, e.g.: 0x0013
    virtual bool parse(const SPICommandPacket& packetData) override;
    virtual std::vector<uint8_t> process(uint32_t seqNo) override;
};

//0x0014 command: Internal Temperature Status command processing
class SpiISSQueryCommand: public BaseCommand {
public:
    SpiISSQueryCommand(SpiCmdDecoder* cmd) : spiCmd(cmd) {};
    SpiCmdDecoder* spiCmd;
    // Function to parse Store command data for SPI, e.g.: 0x0014
    virtual bool parse(const SPICommandPacket& packetData) override;
    virtual std::vector<uint8_t> process(uint32_t seqNo) override;

};

//0x0015 command SPA processing, e.g.:0x0015 1, 1:8 1:1
struct SPAConfigStruct {
//    uint8_t w;                          // WSS module number [1|2]
    std::vector<std::pair<uint16_t, uint16_t>> sliceRanges; // Slice ranges
    std::vector<uint8_t> commonPorts;  // Common ports
    std::vector<uint8_t> switchingPorts; // Switching ports
    std::vector<int16_t> attenuations; // Attenuation values
};

class SPASlicePortAttenuationCommand : public BaseCommand {
public:
    SPASlicePortAttenuationCommand(SpiCmdDecoder* cmd) : spiCmd(cmd) {
    	if(!g_cmdDecoder) {
    		g_cmdDecoder = std::make_unique<CmdDecoder>();
    	}
    };

	SpiCmdDecoder* spiCmd;
    static std::unique_ptr<CmdDecoder> g_cmdDecoder;

    // Function to parse SPA command data for SPI, e.g.: 0x0015 1, 1:8 1:1
    virtual bool parse(const SPICommandPacket& packetData) override;
    virtual std::vector<uint8_t> process(uint32_t seqNo) override;

    static SPAConfigStruct spaConfStruct[3]; //store slice configuration for each module

private:
    float sliceSize = 3.125; //6.25/12.5GHz
    uint8_t w; // WSS module number
	std::vector<std::pair<uint16_t, uint16_t>> sliceRanges;
	std::vector<uint8_t> commonPorts;
	std::vector<uint8_t> switchingPorts;
	std::vector<int16_t> attenuations;

    double freqBySlice(uint16_t sliceNo) {
    	return((sliceNo-1)*sliceSize);
    };
};

//0x0016 command SPA processing, e.g.:0x0016 1/2
class SPAQuerySlicePortAttenuationCommand : public BaseCommand {
public:
    SPAQuerySlicePortAttenuationCommand(SpiCmdDecoder* cmd) : spiCmd(cmd) {};
	SpiCmdDecoder* spiCmd;

	uint8_t w; // WSS module number

    // Function to parse SPA command data for SPI, e.g.: 0x0015 1, 1:8 1:1
    virtual bool parse(const SPICommandPacket& packetData) override;
    virtual std::vector<uint8_t> process(uint32_t seqNo) override;

    std::vector<uint8_t> fwData; // 
    bool isFullTransfer = false;

private:
    float sliceSize = 6.25; //6.25/12.5GHz
    double freqBySlice(uint16_t sliceNo) {
    	return((sliceNo-1)*sliceSize);
    };
};

//0x0017 command: Firmware Transfer command processing
class SpiFWTCmdCommand: public BaseCommand {
    public:
        SpiFWTCmdCommand(SpiCmdDecoder* cmd) : spiCmd(cmd) {};
        SpiCmdDecoder* spiCmd;
        
        // Parses Store command data for SPI (e.g., command 0x0014)
        virtual bool parse(const SPICommandPacket& packetData) override;
        virtual std::vector<uint8_t> process(uint32_t seqNo) override;

        // Static member variables
        static uint32_t totalReceivedBytes;   // Total received bytes
        static uint32_t firmwareTotalSize;    // Firmware total size (to be obtained from firmware header)
        static std::fstream elfFile;          // Firmware file stream
        static bool transferActive;           // Transfer active flag
        
        // Non-static member variables
        uint32_t offset;                      // Offset of current data block
        std::vector<uint8_t> blockData;       // Content of current data block
        
        // methods
        static void initFileTransfer();       // Initialize file transfer
        static void writeToFile(uint32_t offset, const std::vector<uint8_t>& data); // Write data to file
        static void finalizeTransfer();       // Finalize transfer and cleanup
        static void resetTransfer();          // Reset transfer state

};

//0x0018 command: Firmware Store command processing
class SpiFWSCmdCommand: public BaseCommand {
public:
    SpiFWSCmdCommand(SpiCmdDecoder* cmd) : spiCmd(cmd) {};
    SpiCmdDecoder* spiCmd;
    // Function to parse Store command data for SPI, e.g.: 0x0018<O><File>
    virtual bool parse(const SPICommandPacket& packetData) override;
    virtual std::vector<uint8_t> process(uint32_t seqNo) override;

    uint8_t storagePosition = 0; // 0/1/2

};

//0x0019 command: Firmware Load command processing
class SpiFWLCmdCommand: public BaseCommand {
public:
    SpiFWLCmdCommand(SpiCmdDecoder* cmd) : spiCmd(cmd) {};
    SpiCmdDecoder* spiCmd;
    // Function to parse Store command data for SPI, e.g.: 0x0018<O><File>
    virtual bool parse(const SPICommandPacket& packetData) override;
    virtual std::vector<uint8_t> process(uint32_t seqNo) override;

    uint8_t loadPosition = 0; // 0/1/2

};

//0x001A command: Firmware Postion Query command processing
class SpiFWPQueryCommand: public BaseCommand {
public:
    SpiFWPQueryCommand(SpiCmdDecoder* cmd) : spiCmd(cmd) {};
    SpiCmdDecoder* spiCmd;
    // Function to parse Store command data for SPI, e.g.: 0x0018
    virtual bool parse(const SPICommandPacket& packetData) override;
    virtual std::vector<uint8_t> process(uint32_t seqNo) override;

};


//0x001B command: Firmware Execute command processing
class SpiFWECmdCommand: public BaseCommand {
public:
    SpiFWECmdCommand(SpiCmdDecoder* cmd) : spiCmd(cmd) {};
    SpiCmdDecoder* spiCmd;
    // Function to parse Store command data for SPI, e.g.: 0x0018
    virtual bool parse(const SPICommandPacket& packetData) override;
    virtual std::vector<uint8_t> process(uint32_t seqNo) override;
};











#endif /* SRC_SPIMODULE_SPICMDDECODER_H_ */
