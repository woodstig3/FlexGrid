/*
 * SPIProcess.cpp
 *
 *  Created on: Dec 11, 2024
 *      Author: Administrator
 */

#include "SpiProcess.h"
#include "wdt.h"

/** Global variables for thread synchronization
std::queue<Packet> spiPacketQueue;
pthread_mutex_t spiQueueMutex;
pthread_cond_t cv;
std::atomic<bool> running(true);
********************************/

std::queue<Packet> ThreadManager::spiPacketQueue;
pthread_mutex_t ThreadManager::spiQueueMutex;
pthread_cond_t ThreadManager::cv;
std::atomic<bool> ThreadManager::receiving{true};
std::atomic<bool> ThreadManager::busy{false};

// Initialize static member
std::unique_ptr<SpiCmdDecoder> ThreadManager::spiDec = nullptr;
std::unique_ptr<SPISlave> ThreadManager::spiSlave = nullptr;
struct spi_transfer_data ThreadManager::transfer{};

// Function to listen for SPI data
void* ThreadManager::spiListener(void* arg) {
//    SPISlave* spi = static_cast<SPISlave*>(arg);
    std::cout << "ThreadManager::spiListenner started." << std::endl;

// Prepare a packet as default reply when there was no previous command received
    std::vector<uint8_t> defReply = constructDefaultPacket();
//  std::memcpy(transfer.tx_buf, defReply.data(), defReply.size()*sizeof(uint8_t));
    std::copy(defReply.begin(), defReply.end(), transfer.tx_buf);

    while (receiving) {

    	// Wait for master command by detecting cs and sclk valid at the same time
/*    	if(!spiSlave->isReady()) {
    		std::cout << "Waiting for master command ...\n" << std::endl;
    		continue;
    	}
*/
#ifdef _WATCHDOG_SOFTRESET_
		watchdog_feed();
#endif
    	memset(transfer.rx_buf, 0, BUFFER_SIZE);
    	int ret = spiSlave->spi_transfer(transfer);
		if (ret >= 0 ) {
	    	std::cout << "Incoming data...\n" << std::endl;
/*			if(busy == true)
			{//previous command or query is not finished
				std::cerr << "Busy state: Discard incoming data." << std::endl;
				memset(transfer.rx_buf, 0, BUFFER_SIZE);
				continue;  //device pending on the previous processing, discard any incoming packets until pending is finished.
			}
*/
			pthread_mutex_lock(&spiQueueMutex);

			std::vector<uint8_t> buffer(std::begin(transfer.rx_buf), std::end(transfer.rx_buf));
			std::cout << "Read data from spi rx buffer of size: " << buffer.size() << std::endl;

			Packet packet{buffer};
			spiPacketQueue.push(packet);

			pthread_mutex_unlock(&spiQueueMutex);
			pthread_cond_signal(&cv); // Notify the processing thread

		}
        // Sleep for a short duration to prevent busy-waiting
        usleep(50000); // Sleep for 1ms
    }
    return nullptr;
}

// Thread function to process packets from the queue
void* ThreadManager::spiPacketProcessor(void* arg) {
	std::vector<uint8_t> replyPacketData;
//    SPISlave* spi = static_cast<SPISlave*>(arg);
    while (receiving) {
#ifdef _WATCHDOG_SOFTRESET_
		watchdog_feed();
#endif
        pthread_mutex_lock(&spiQueueMutex);
        while (spiPacketQueue.empty()) {
            pthread_cond_wait(&cv, &spiQueueMutex); // Wait for a packet
        }

        while (!spiPacketQueue.empty()) {
            Packet packet = spiPacketQueue.front();
            spiPacketQueue.pop();
            pthread_mutex_unlock(&spiQueueMutex); // Unlock while processing

            // Process the packet
            // Print spi packet data
            std::cout << "Received spi packet: ";
            for (auto byte : packet.data) {
                std::cout << std::hex << static_cast<int>(byte) << " ";
            }
            std::cout << std::dec << std::endl;
            SPICommandPacket commandPacket;   //read out data into struct

    		int result = ThreadManager::parseSPICommandPacket(packet, commandPacket);
            if (result == 0) {
        		std::cout << "Command packet header parsed successfully." << std::endl;
    		    busy = true;
				//Send back PENDING reply packet first before further parsing and processing
				SPIReplyPacket replyPacket;
				replyPacket.spiMagic = SPIMAGIC;
				replyPacket.length = commandPacket.length; // Example length
				replyPacket.seqNo = commandPacket.seqNo;
				replyPacket.comres = -1; // PENDING COMRES
                // component header: 16 bytes
                std::vector<uint8_t> headerData = spiDec->constructSPIReplyPacketHeader(replyPacket);
                // caculate CRC1
                replyPacket.crc1 = spiDec->calculateCRC1(headerData.data());
                if (replyPacket.length > 0x0014 ) {
                    replyPacket.data.clear();
                    replyPacket.data.reserve(commandPacket.data.size());
                    for (signed char c : commandPacket.data) {
                        replyPacket.data.push_back(static_cast<uint8_t>(c));
                    }
                    // Calculate CRC2 based on the entire packet (excluding CRC2 itself)
                    std::vector<uint8_t> packetWithoutCRC2 = spiDec->constructSPIReplyPacketWithoutCRC2(replyPacket);
                    replyPacket.crc2 = spiDec->calculateCRC2(packetWithoutCRC2.data(), packetWithoutCRC2.size());
                }

				replyPacketData = constructSPIReplyPacket(replyPacket);
                
                if (replyPacketData.size() > BUFFER_SIZE) {
                    std::cerr << "Error: Reply packet size exceeds buffer capacity." << std::endl;
                    return nullptr;
                }
				// Formulate a reply based on the processed packet
				memset(transfer.tx_buf, 0, BUFFER_SIZE);
				memcpy(transfer.tx_buf, replyPacketData.data(), replyPacketData.size()*sizeof(uint8_t));

				spiDec->oss.isPending = true;

        		//then pass data content to spiCmdDecoder to extract opcode and arguments to calculate shape
        		if(spiDec) {
        		    replyPacketData = spiDec->processSPIPacket(commandPacket);
        		}
        		else {
        			std::cout << "Command decoder not in work." << std::endl;
        		}
				std::cout << "Reply packet constructed successfully." << std::endl;
			}
            else
			{
				std::cout << "Command packet format is not correct." << std::endl;
	            busy = false;
	            //Send back Default reply packet first before further parsing and processing
				SPIReplyPacket replyPacket;
				replyPacketData = constructDefaultPacket();
			}

            // Formulate a reply based on the processed packet
            memset(transfer.tx_buf, 0, BUFFER_SIZE);
            memcpy(transfer.tx_buf, replyPacketData.data(), replyPacketData.size());

            std::cout << "Content of transfer.tx_buf: ";
            for (int i = 0; i < replyPacketData.size(); i++) {
                std::cout << std::hex << static_cast<int>(transfer.tx_buf[i]) << " ";
            }
            std::cout << std::endl;

            pthread_mutex_lock(&spiQueueMutex); // Lock again before checking the queue
        }
        pthread_mutex_unlock(&spiQueueMutex); // Unlock after processing
    }
    return nullptr;
}

void ThreadManager::startThreads() {

    pthread_create(&spiListenerThread, nullptr, spiListener, nullptr);
    pthread_create(&spiProcessorThread, nullptr, spiPacketProcessor, nullptr);
}

void ThreadManager::stopThreads() {

    spiSlave->cleanup();
    pthread_cond_broadcast(&cv); // Unblock the processing thread if waiting
    pthread_join(spiListenerThread, nullptr);
    pthread_join(spiProcessorThread, nullptr);
}

// Function to parse SPI command/query packet
int ThreadManager::parseSPICommandPacket(const Packet& packet, SPICommandPacket& commandPacket) {
    if (packet.data.size() < 0x001C) {
    	std::cout << "packet size too short:" << packet.data.size() <<std::endl;
    	return 2; // Packet is too short, invalid packet
    }

    // Copy the fixed-size fields
    commandPacket.spiMagic = bytesToInt32BigEndian(packet.data, 0);
    std::cout << "SPI MAGIC: 0x" << std::hex << commandPacket.spiMagic <<std::endl;
    // Validate SPIMAGIC
    if (commandPacket.spiMagic != SPIMAGIC) {
    	std::cout << "Wrong SPI MAGIC" << commandPacket.spiMagic <<std::endl;
    	return 2; // Invalid packet
    }

    commandPacket.length = bytesToInt32BigEndian(packet.data, 4);
    std::cout << "Packet Length:" << std::dec << commandPacket.length <<std::endl;
    // Check if the entire packet size is valid against the specified length
   if (packet.data.size() < commandPacket.length) {
	   std::cout << "Packet length mismatch: expected " << commandPacket.length
				 << ", actual " << packet.data.size() << std::endl;
	   return 2; // Length mismatch, invalid packet
   }
    commandPacket.seqNo = bytesToInt32BigEndian(packet.data, 8);
    std::cout << "Packet SeqNo:" << commandPacket.seqNo <<std::endl;
    commandPacket.opcode = bytesToInt32BigEndian(packet.data, 12);
    std::cout << "Command OpCode:" << commandPacket.opcode <<std::endl;
    // Corrected OPCODE range check
    if (commandPacket.opcode < 0x0001 || commandPacket.opcode > 0x001B) {
    	std::cout << "Wrong OPcode:" << commandPacket.opcode <<std::endl;
    	return 1; // OPCODE ERROR
    }

    commandPacket.crc1 = bytesToInt32BigEndian(packet.data, 16);
    std::cout << "CRC1: 0x" << std::hex << commandPacket.crc1 << std::endl;
    //check if crc1 is correct or return 3 and set seqno =0
    // Check if CRC1 is correct. Implementation for CRC1 validation should be added here.
    // if (!isCRC1Valid(commandPacket.crc1)) {
    //     return 3; // CRC1 Error
    // }

    // Copy the variable-size data field if present
    if (commandPacket.length > 20) {
        // Ensure length includes DATA and CRC2
        if (commandPacket.length < 24) {
            std::cout << "Invalid length for DATA and CRC2: " << commandPacket.length << std::endl;
            return 2; // Length too short for DATA and CRC2
        }
        // Extract DATA field (from byte 20 to LENGTH - 5)
        size_t dataLength = commandPacket.length - 24; // Subtract header (20 bytes) and CRC2 (4 bytes)
        commandPacket.data.assign(packet.data.begin() + 20, packet.data.begin() + 20 + dataLength);
        // Parse CRC2 (last 4 bytes)
        commandPacket.crc2 = bytesToInt32BigEndian(packet.data, commandPacket.length - 4);
        std::cout << "CRC2: 0x" << std::hex << commandPacket.crc2 << std::endl;
    } else {
        // No DATA field, clear data and set CRC2 to 0
        commandPacket.data.clear();
        commandPacket.crc2 = 0;
    }

    return 0; // Successfully parsed
}

std::vector<uint8_t> ThreadManager::constructSPIReplyPacket(const SPIReplyPacket& replyPacket) {
    std::vector<uint8_t> packet;
    packet.reserve(replyPacket.length);
/*
    // Print reply packet fields
    std::cout << "Constructing SPI Reply Packet:" << std::endl;
    std::cout << "  SPIMAGIC: 0x" << std::hex << replyPacket.spiMagic << std::endl;
    std::cout << "  LENGTH: " << std::dec << replyPacket.length << std::endl;
    std::cout << "  SEQNO: " << replyPacket.seqNo << std::endl;
    std::cout << "  COMRES: " << replyPacket.comres << std::endl;
    std::cout << "  CRC1: 0x" << std::hex << replyPacket.crc1 << std::endl;
    if (replyPacket.length > 0x0014) {
        std::cout << "  DATA: ";
        for (auto byte : replyPacket.data) {
            std::cout << std::hex << static_cast<int>(byte) << " ";
        }
        std::cout << std::endl;
        std::cout << "  CRC2: 0x" << std::hex << replyPacket.crc2 << std::endl;
    }
    */

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

    // Add DATA and CRC2 if present
    if (replyPacket.length > 0x0014) {
        // Add DATA
        packet.insert(packet.end(), replyPacket.data.begin(), replyPacket.data.end());
        // Add CRC2 (4 bytes)
    	packet.push_back((replyPacket.crc2 >> 24) & 0xFF);
    	packet.push_back((replyPacket.crc2 >> 16) & 0xFF);
    	packet.push_back((replyPacket.crc2 >> 8) & 0xFF);
        packet.push_back(replyPacket.crc2 & 0xFF);
    }

    if (packet.size() != replyPacket.length) {
        std::cerr << "Error: Packet size does not match the specified length." << std::endl;
        std::cerr << "  Packet size: " << std::dec << packet.size() << std::endl;
        for (auto byte : packet) {
            std::cerr << std::hex << static_cast<int>(byte) << " ";
        }
        std::cerr << std::endl;
        // Print replyPacket content
        std::cerr << "  ReplyPacket content (hex): ";
        std::cerr << "SPIMAGIC: 0x" << std::hex << replyPacket.spiMagic << " ";
        std::cerr << "LENGTH: 0x" << replyPacket.length << " ";
        std::cerr << "SEQNO: 0x" << replyPacket.seqNo << " ";
        std::cerr << "COMRES: 0x" << replyPacket.comres << " ";
        std::cerr << "CRC1: 0x" << replyPacket.crc1 << " ";
        if (replyPacket.length > 0x0014) {
            std::cerr << "DATA: ";
            for (auto byte : replyPacket.data) {
                std::cerr << std::hex << static_cast<int>(byte) << " ";
            }
            std::cerr << "CRC2: 0x" << replyPacket.crc2 << " ";
        }
        std::cerr << std::endl;
    }
    // Print the constructed packet
    std::cout << "Constructed SPI Reply Packet (raw bytes): ";
    for (auto byte : packet) {
        std::cout << std::hex << static_cast<int>(byte) << " ";
    }
    std::cout << std::endl;
    return packet;
}


std::vector<uint8_t> ThreadManager::constructDefaultPacket() {
    // Fixed field values
    const uint32_t HWR = 1000;  //spiDec
    const uint32_t FWR = 1000;
    const char* SNO = "SN000000";
    const char* MFD = "20241231";
    const char* LBL = "TWIN_STANDARD";
    uint32_t seqno = 0;
    uint32_t comres = 0;

    // Create DATA payload
    std::vector<uint8_t> data;

    // Insert HWR (4 bytes)
    data.push_back((HWR >> 24) & 0xFF);
    data.push_back((HWR >> 16) & 0xFF);
    data.push_back((HWR >> 8) & 0xFF);
    data.push_back(HWR & 0xFF);

    // Insert FWR (4 bytes)
    data.push_back((FWR >> 24) & 0xFF);
    data.push_back((FWR >> 16) & 0xFF);
    data.push_back((FWR >> 8) & 0xFF);
    data.push_back(FWR & 0xFF);

    // Insert SNO (fixed 8 bytes)
    for(size_t i = 0; i < 8; i++) {
        data.push_back(i < strlen(SNO) ? SNO[i] : 0);
    }

    // Insert MFD (fixed 8 bytes)
    for(size_t i = 0; i < 8; i++) {
        data.push_back(i < strlen(MFD) ? MFD[i] : 0);
    }

    // Insert LBL (fixed 16 bytes)
    for(size_t i = 0; i < 16; i++) {
        data.push_back(i < strlen(LBL) ? LBL[i] : 0);
    }

    // Construct full packet
    std::vector<uint8_t> packet;

    // SPIMAGIC (4 bytes) - MSB first
    packet.push_back((SPIMAGIC >> 24) & 0xFF);  // 0x0F
    packet.push_back((SPIMAGIC >> 16) & 0xFF);  // 0x1E
    packet.push_back((SPIMAGIC >> 8) & 0xFF);   // 0x2D
    packet.push_back(SPIMAGIC & 0xFF);          // 0x3C

    // LENGTH (4 bytes) - calculated after DATA construction
    uint32_t length = 16 + data.size() + 4; // SPIMAGIC+LENGTH+SEQNO+COMRES+DATA
    packet.push_back((length >> 24) & 0xFF);
    packet.push_back((length >> 16) & 0xFF);
    packet.push_back((length >> 8) & 0xFF);
    packet.push_back(length & 0xFF);

    // SEQNO (4 bytes)
    packet.push_back((seqno >> 24) & 0xFF);
    packet.push_back((seqno >> 16) & 0xFF);
    packet.push_back((seqno >> 8) & 0xFF);
    packet.push_back(seqno & 0xFF);

    // COMRES (4 bytes)
    packet.push_back((comres >> 24) & 0xFF);
    packet.push_back((comres >> 16) & 0xFF);
    packet.push_back((comres >> 8) & 0xFF);
    packet.push_back(comres & 0xFF);

    // Append DATA
    packet.insert(packet.end(), data.begin(), data.end());

    // Calculate CRC1 (from 0x00 to 0x0F)
    uint32_t crc1 = spiDec->calculateCRC1(packet.data());
    // Add CRC1 (4 bytes)
    packet.push_back((crc1 >> 24) & 0xFF);
    packet.push_back((crc1 >> 16) & 0xFF);
    packet.push_back((crc1 >> 8) & 0xFF);
    packet.push_back(crc1 & 0xFF);
    // Calculate CRC2 (from 0x00 to (LENGTH-0x05))
    uint32_t crc2 = spiDec->calculateCRC2(packet.data(), packet.size());
    // Add CRC2 (4 bytes)
    packet.push_back((crc2 >> 24) & 0xFF);
    packet.push_back((crc2 >> 16) & 0xFF);
    packet.push_back((crc2 >> 8) & 0xFF);
    packet.push_back(crc2 & 0xFF);
    // Print the constructed packet
    std::cout << "Constructed Default SPI Reply Packet (raw bytes): ";
    for (auto byte : packet) {
        std::cout << std::hex << static_cast<int>(byte) << " ";
    }
    std::cout << std::endl;

    return packet;
}

void ThreadManager::initializer()
{
	if(!spiDec)
		spiDec = std::make_unique<SpiCmdDecoder>();
	if(!spiSlave)
		spiSlave = std::make_unique<SPISlave>();

	if(spiSlave->init()) {
		std::cout << "ThreadManager Initialized." << std::endl;
	} else {
		std::cout << "SPI Interface Failed to Initialize." << std::endl;
	}
}

//Big-Endian to 32-bit Integer
uint32_t ThreadManager::bytesToInt32BigEndian(const std::vector<uint8_t>& bytes, size_t offset) {
    // Ensure there are enough bytes
    if (bytes.size() < offset + 4) {
        throw std::out_of_range("Not enough bytes to read a 32-bit integer from offset " + std::to_string(offset));
    }

    // Combine the bytes into a 32-bit integer (big-endian)
    uint32_t result = (static_cast<uint32_t>(bytes[offset]) << 24) |
                      (static_cast<uint32_t>(bytes[offset + 1]) << 16) |
                      (static_cast<uint32_t>(bytes[offset + 2]) << 8) |
                      (static_cast<uint32_t>(bytes[offset + 3]));

    return result;
}

uint16_t ThreadManager::bytesToInt16BigEndian(const std::vector<uint8_t>& bytes, size_t offset) {
    // Ensure there are enough bytes
    if (bytes.size() < offset + 2) {
        throw std::out_of_range("Not enough bytes to read a 16-bit integer");
    }

    // Combine the bytes into a 16-bit integer (big-endian)
    uint16_t result = (static_cast<uint16_t>(bytes[offset]) << 8) |
                      (static_cast<uint16_t>(bytes[offset + 1]));

    return result;
}

/*  Little-Endian to 32-bit Integer
uint32_t ThreadManager::bytesToInt32LittleEndian(const std::vector<uint8_t>& bytes, size_t offset) {
    // Ensure there are enough bytes
    if (bytes.size() < offset + 4) {
        throw std::out_of_range("Not enough bytes to read a 32-bit integer");
    }

    // Combine the bytes into a 32-bit integer (little-endian)
    uint32_t result = (static_cast<uint32_t>(bytes[offset]) << 0) |
                      (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
                      (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
                      (static_cast<uint32_t>(bytes[offset + 3]) << 24);

    return result;
}
*/
std::vector<uint8_t> ThreadManager::constructSPIReplyPacketHeader(const SPIReplyPacket& replyPacket) {
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


