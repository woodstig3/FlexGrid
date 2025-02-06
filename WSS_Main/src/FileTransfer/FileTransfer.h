/*
 * FileTransfer.h
 *
 *  Created on: Nov 15, 2024
 *      Author: Administrator
 */

#ifndef SRC_FILETRANSFER_FILETRANSFER_H_
#define SRC_FILETRANSFER_FILETRANSFER_H_

#include <string>
#include <queue>
#include <thread>
//#include <pthread.h>
//#include <stdexcept>
//#include <memory>
//#include <mutex>
#include <condition_variable>
#include <vector>


template<typename T>
class ThreadSafeQueue {
public:
    ThreadSafeQueue() {}

    void push(const T &value) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.push(value);
        m_condition.notify_one();
    }

    void pop(T &value) {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_condition.wait(lock, [this] { return !m_queue.empty(); });
        value = m_queue.front();
        m_queue.pop();
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_queue.empty();
    }

private:
    std::queue<T> m_queue;
    mutable std::mutex m_mutex;
    std::condition_variable m_condition;
};


class FileTransfer {
public:

	FileTransfer(const std::string &old_firmware_path, const std::string &base_firmware_dir)
	        : m_oldFirmwarePath(old_firmware_path), m_newFirmwarePath(base_firmware_dir) {}//, firmwareUpdaterThread(nullptr) {}

    ~FileTransfer() {
//         Properly join the thread if it's still running before destroying it
        if (firmwareUpdaterThread && firmwareUpdaterThread->joinable()) {
        	stopFirmwareUpgrade();
            firmwareUpdaterThread->join();
        }
    }

//    ThreadSafeQueue<std::string>& getPacketQueue() {
//            return *packetQueue;
//    }
//    void processFirmwarePackets(std::string& strPacket, int num_bytes);
    void handlePrepareCommand(int numBytesToReceive, std::string strOldPath, std::string strNewPath);
    void handleFWPrepareCommand(int numBytesToReceive, std::string strOldPath, std::string strNewPath);
    void handleSwitchCommand();
    void handleCommitCommand();
    void handleRevertCommand();
    void handlePrepareToRead(int numBytesToRead, std::string strPath);
    int  file_num_bytes;
	bool b_Start_Download = false;
	bool b_Start_Read = false;
//    char intToHexChar(uint8_t value);

private:

    // ... [rest of the member variables]
//    void processFirmwarePackets(int num_bytes);

    void processFirmwarePackets(int num_bytes);
    void processLUTFilePackets(int num_bytes);
//    std::unique_ptr<ThreadSafeQueue<std::string>> packetQueue;
    std::string m_oldFirmwarePath;
    std::string m_newFirmwarePath;
    std::string m_HECFilePath;

    std::unique_ptr<std::thread> firmwareUpdaterThread;
    bool m_firmwareUpgradeStarted;


//    void sendAcknowledgement(const std::string& ackMessage);
    //    void* firmwareUpdaterThreadFunc(void* arg);
    void startFirmwareUpgrade(int num_bytes, std::string path);
    void startLUTFileUpgrade(int num_bytes, std::string path);
    void stopFirmwareUpgrade();

    std::unique_ptr<std::thread> HECFileReadThread;
    void startHECFileRead(int numBytes, std::string fielPath);
    void stopHECFileRead();
    bool m_HECFileReadStarted = false;
    void sendHECFile(const std::string& filename);

    uint16_t hexCharToUint16(char high, char low);

//    void handleDataPacket(const std::string& packet);
    uint8_t hexToInt(char c);
    // ... [rest of the member functions]
    bool check_integrity(const std::string &path, const int expected_size); //const std::string &expected_hash);
    bool replaceFile(const std::string& oldFilename, const std::string& newFilename);
};


/*
template<typename T>
class ThreadSafeQueue {
public:
    ThreadSafeQueue() {
        if (pthread_mutex_init(&m_mutex, nullptr)) {
            throw std::runtime_error("Failed to initialize mutex");
        }
        if (pthread_cond_init(&m_condition, nullptr)) {
            pthread_mutex_destroy(&m_mutex);
            throw std::runtime_error("Failed to initialize condition variable");
        }
    }

    ~ThreadSafeQueue() {
        pthread_mutex_destroy(&m_mutex);
        pthread_cond_destroy(&m_condition);
    }

    void push(const T &value) {
        pthread_mutex_lock(&m_mutex);
        m_queue.push(value);
        pthread_cond_signal(&m_condition);
        pthread_mutex_unlock(&m_mutex);
    }

    void pop(T &value) {
        pthread_mutex_lock(&m_mutex);
        while (m_queue.empty()) {
            pthread_cond_wait(&m_condition, &m_mutex);
        }
        value = m_queue.front();
        m_queue.pop();
        pthread_mutex_unlock(&m_mutex);
    }

    bool empty() const {
        pthread_mutex_lock(&m_mutex);
        bool result = m_queue.empty();
        pthread_mutex_unlock(&m_mutex);
        return result;
    }

private:
    std::queue<T> m_queue;
    pthread_mutex_t m_mutex;
    pthread_cond_t m_condition;
};
*/
#endif /* SRC_FILETRANSFER_FILETRANSFER_H_ */
