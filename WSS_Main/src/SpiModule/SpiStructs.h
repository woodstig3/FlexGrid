/*
 * SPIStructs.h
 *
 *  Created on: Dec 17, 2024
 *      Author: Administrator
 */

#ifndef SRC_SPIMODULE_SPISTRUCTS_H_
#define SRC_SPIMODULE_SPISTRUCTS_H_

#include <vector>
#include "DataStructures.h"

// Define packet structures
struct SPICommandPacket {
    uint32_t spiMagic;
    uint32_t length;
    uint32_t seqNo;
    uint32_t opcode;
    std::vector<uint8_t> data;
    uint32_t crc1;
    uint32_t crc2;
};

struct SPIReplyPacket {
    uint32_t spiMagic;   //
    uint32_t length;
    uint32_t seqNo;
    uint32_t comres;
    uint32_t crc1;
    std::vector<uint8_t> data;
    uint32_t crc2;
};


#endif /* SRC_SPIMODULE_SPISTRUCTS_H_ */
