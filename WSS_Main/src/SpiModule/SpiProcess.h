/*
 * SPIProcess.h
 *
 *  Created on: Dec 11, 2024
 *      Author: Administrator
 */


#ifndef SRC_SPIMODULE_SPIPROCESS_H_
#define SRC_SPIMODULE_SPIPROCESS_H_

#include <iostream>
#include <pthread.h>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <cstring>

#include "SPIInterface.h"
#include "SpiCmdDecoder.h"

struct Packet {
	std::vector<uint8_t> data;
	 // Constructor to initialize the struct with a vector
	Packet(const std::vector<uint8_t>& buffer) : data(buffer) {}
};

class ThreadManager {
public:
    static ThreadManager& getInstance() {
        static ThreadManager instance; // Ensure a single instance
        return instance;
    }
    static std::unique_ptr<SpiCmdDecoder> spiDec;
    static std::unique_ptr<SPISlave> spiSlave;
    static void initializer();


    void startThreads();
    void stopThreads();

private:
    ThreadManager() {
        pthread_mutex_init(&spiQueueMutex, nullptr);
        pthread_cond_init(&cv, nullptr);
    }

    ~ThreadManager() {
        stopThreads();
        pthread_mutex_destroy(&spiQueueMutex);
        pthread_cond_destroy(&cv);
    }

    static void* spiListener(void* arg);
    static void* spiPacketProcessor(void* arg);

    static std::queue<Packet> spiPacketQueue;
    static pthread_mutex_t spiQueueMutex;
    static pthread_cond_t cv;
    static std::atomic<bool> receiving;
    static std::atomic<bool> busy;

	static struct spi_transfer_data transfer;

    pthread_t spiListenerThread;
    pthread_t spiProcessorThread;

    static int parseSPICommandPacket(const Packet& packet, SPICommandPacket& commandPacket);
    static std::vector<uint8_t> constructSPIReplyPacket(const SPIReplyPacket& replyPacket);
    static std::vector<uint8_t> constructDefaultPacket();

    static uint32_t bytesToInt32BigEndian(const std::vector<uint8_t>& bytes, size_t offset);
    static uint16_t bytesToInt16BigEndian(const std::vector<uint8_t>& bytes, size_t offset);

    static uint32_t calculateCRC(const std::vector<uint8_t>& packet);        // Implement your CRC logic based on the IEEE 802.3 standard
};


#endif /* SRC_SPIMODULE_SPIPROCESS_H_ */
