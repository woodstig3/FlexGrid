#ifndef SPI_SLAVE_H
#define SPI_SLAVE_H

#include <vector>
#include <string>
#include <functional>
#include <unistd.h>
#include <linux/spi/spidev.h>
#include "SpiStructs.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#define  BUFFER_SIZE  64

// Structure to hold SPI transfer data
struct spi_transfer_data {
    char tx_buf[BUFFER_SIZE] = {
    		0x0F, 0x1E, 0x2C, 0x3D, 0x00, 0x00,
    		0x40, 0x00, 0x00, 0x00, 0x00, 0x05,
    		0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    		0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF,
    		0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    		0xF0, 0x0D,
    	};
    char rx_buf[BUFFER_SIZE] = {0,};
    size_t len = BUFFER_SIZE;
};

/*
struct spi_ioc_transfer {
	uint8_t tx_buf[BUFFER_SIZE];
	uint8_t rx_buf[BUFFER_SIZE];
    size_t len;
    size_t speed_hz = 1000000; // 1MHz - adjust as needed
    size_t bits_per_word = 8;
    size_t delay_usecs = 0;
};
*/

class SPISlave {
public:
    SPISlave(const std::string& device = "/dev/spidev1.0");
    ~SPISlave();

    // Delete copy constructor and assignment operator
    SPISlave(const SPISlave&) = delete;
    SPISlave& operator=(const SPISlave&) = delete;

    bool init();
    void cleanup();

    // Main read function that waits for master commands
    bool isReady() const;
    int spi_transfer(struct spi_transfer_data& transfer);
    // Function to prepare response for next master read
    bool prepareResponse(const std::vector<uint8_t>& response);
    void processData(unsigned char* rx_buf, unsigned char* tx_buf, size_t len);
    void handleCommand(unsigned char* rx_buf, unsigned char* tx_buf, size_t& len);

private:
    bool configureSPIDevice();

    int m_fd;
    std::string m_devicePath;
    std::vector<uint8_t> m_responseBuffer;

    size_t current_mode=0;
    size_t bits=8;
    size_t speed=1000000;
};

#endif
