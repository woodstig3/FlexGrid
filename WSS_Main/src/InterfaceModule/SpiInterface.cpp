#include "SPIInterface.h"
#include <fcntl.h>

#include <sys/ioctl.h>
#include <poll.h>
#include <system_error>
#include <iostream>
#include <cstring>


SPISlave::SPISlave(const std::string& device)
    : m_fd(-1)
    , m_devicePath(device)
{
    m_responseBuffer.resize(BUFFER_SIZE);

}

SPISlave::~SPISlave()
{
    cleanup();
}

bool SPISlave::init()
{
    m_fd = open(m_devicePath.c_str(), O_RDWR);
    if (m_fd < 0) {
        std::cerr << "Error opening SPI device: " << strerror(errno) << std::endl;
        return false;
    }

	if (ioctl(m_fd, SPI_IOC_RD_MODE32, &current_mode) < 0) {
	    perror("Failed to read SPI mode");
	    cleanup();
	    return false;
	}
	printf("Current SPI mode: %d\n", current_mode);

	if (ioctl(m_fd, SPI_IOC_RD_BITS_PER_WORD, &bits) < 0) {
        std::cerr << "Error setting bits per word: " << strerror(errno) << std::endl;
        cleanup();
        return false;
    }
    printf("Current Bits_Per_Word: %d\n", bits);

    if (ioctl(m_fd, SPI_IOC_RD_MAX_SPEED_HZ, &speed) < 0) {
        std::cerr << "Error setting speed: " << strerror(errno) << std::endl;
        cleanup();
        return false;
    }
	printf("Current SPI speed in Hz: %d\n", speed);

    return (true);
}

void SPISlave::cleanup()
{
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
}

bool SPISlave::configureSPIDevice()
{

    if (ioctl(m_fd, SPI_IOC_WR_MODE32, &current_mode) < 0) {
        std::cerr << "Error setting SPI mode: " << strerror(errno) << std::endl;
        return false;
    }

    if (ioctl(m_fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0) {
        std::cerr << "Error setting bits per word: " << strerror(errno) << std::endl;
        return false;
    }

    if (ioctl(m_fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0) {
		std::cerr << "Error setting speed: " << strerror(errno) << std::endl;
		return false;
	}

    return true;
}


bool SPISlave::isReady() const {
    pollfd pfd;

    pfd    .fd = m_fd;
    pfd    .events = POLLIN | POLLOUT; // Monitor for both read and write readiness
    pfd    .revents = 0;

    int ret = poll(&pfd, 1, 100); // 100ms timeout
    return ret > 0 && (pfd.revents & (POLLIN | POLLOUT));
}


// Function to handle SPI data transfer
int SPISlave::spi_transfer(struct spi_transfer_data &transfer) {
    spi_ioc_transfer tr;
	memset(&tr, 0 ,sizeof(spi_ioc_transfer));

    tr.tx_buf = (unsigned long)transfer.tx_buf;
    tr.rx_buf = (unsigned long)transfer.rx_buf;
	tr.len = transfer.len;
	tr.speed_hz = 20000000; // 1MHz - adjust as needed
	tr.bits_per_word = bits;
	tr.delay_usecs = 0;

    int ret = ioctl(m_fd, SPI_IOC_MESSAGE(1), &tr);
    if (ret < 0) {
    	std::cerr << "Error during SPI transfer: " << strerror(errno) << std::endl;
        return -1;
    }
    return ret;
}

void SPISlave::processData(unsigned char* rx_buf, unsigned char* tx_buf, size_t len)
{
    // Default implementation: echo received data
    std::memcpy(tx_buf, rx_buf, len);

    std::cout << "Received data: ";
    for (size_t i = 0; i < len; i++) {
        printf("0x%02X ", rx_buf[i]);
    }

    // Process any specific commands
    handleCommand(rx_buf, tx_buf, len);
}

void SPISlave::handleCommand(unsigned char* rx_buf, unsigned char* tx_buf, size_t& len)
{
    // Default command handling implementation
    switch(rx_buf[0]) {
        case 0x01: // Example command
            tx_buf[0] = 0xAA; // Response
            len = 1;
            break;
        default:
            tx_buf[0] = 0xFF;
            len = 1;
            break;
    }
}

bool SPISlave::prepareResponse(const std::vector<uint8_t>& response)
{
    if (m_fd < 0) return false;

    // Copy response to internal buffer
    m_responseBuffer = response;
    for(const auto& data : m_responseBuffer)
    	printf("Read data: %02X", data);

    // Write response to device (will be sent when master initiates next transfer)
//    ssize_t bytesWritten = write(m_fd, m_responseBuffer.data(), m_responseBuffer.size());
    return true; //bytesWritten == static_cast<ssize_t>(m_responseBuffer.size());
}
