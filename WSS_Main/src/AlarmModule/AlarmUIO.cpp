#include <poll.h>

#include "AlarmUIO.h"



const char *uiod0 = "/dev/uio0";    // LCos panel voltage exceeding: Optics failure
const char *uiod1 = "/dev/uio1";	// Lcos panel voltage exceeding: Optics failure
const char *uiod2 = "/dev/uio2";	// Grating component temperature exceeding: internal temperature
const char *uiod3 = "/dev/uio3";    // Thermal Failure or LCOS permanently damaged
const char *uiod4 = "/dev/uio4";    // Hard reset
const char *uiod5 = "/dev/uio5";    // Pattern receive finish
const char *uiod6 = "/dev/uio6";    // EEPROM load ready
const char *uiod7 = "/dev/uio7";    // Grid low temp
const char *uiod8 = "/dev/uio8";    // Lcos low temp

AlarmModule *AlarmModule::pinstance_{nullptr};

AlarmModule::AlarmModule()
{
	HisCon_OA = HisCon_OD = HisCon_GRIDTemp = HisCon_LCOSTemp = 0;
	DeOA_Flag = DeOD_Flag = DeGRID_Flag = DeLCOS_Flag = false;

	thread_id = 0;
	pthread_attr_init(&thread_attrb);	//Default initialize thread attributes

	mmapTEC = new MemoryMapping(MemoryMapping::TEC);
    mmapGPIO = new MemoryMapping(MemoryMapping::GPIO);

	UIO_DAC_OA = open(uiod0, O_RDWR | O_NONBLOCK);
    if (UIO_DAC_OA < 1)
    {
        printf("Invalid UIO device file : %s.\n",uiod0);
    }
    UIO_DAC_OD = open(uiod1, O_RDWR | O_NONBLOCK);
    if (UIO_DAC_OD < 1)
    {
        printf("Invalid UIO device file : %s.\n",uiod1);
    }
    UIO_GRID_Temp_H = open(uiod2, O_RDWR | O_NONBLOCK);
    if (UIO_GRID_Temp_H < 1)
    {
        printf("Invalid UIO device file : %s.\n",uiod2);
    }
    UIO_LCOS_Temp_H = open(uiod3, O_RDWR | O_NONBLOCK);
    if (UIO_LCOS_Temp_H < 1)
    {
        printf("Invalid UIO device file : %s.\n",uiod3);
    }
    UIO_Hard_Reset = open(uiod4, O_RDWR | O_NONBLOCK);
    if (UIO_Hard_Reset < 1)
    {
        printf("Invalid UIO device file : %s.\n",uiod4);
    }
    UIO_Pattern_Received = open(uiod5, O_RDWR | O_NONBLOCK);
    if (UIO_Pattern_Received < 1)
    {
        printf("Invalid UIO device file : %s.\n",uiod5);
    }
    UIO_EEPROM_Load_Ready = open(uiod6, O_RDWR | O_NONBLOCK);
    if (UIO_EEPROM_Load_Ready < 1)
    {
        printf("Invalid UIO device file : %s.\n",uiod6);
    }
    UIO_GRID_Temp_L = open(uiod7, O_RDWR | O_NONBLOCK);
    if (UIO_GRID_Temp_L < 1)
    {
        printf("Invalid UIO device file : %s.\n",uiod7);
    }
    UIO_LCOS_Temp_L = open(uiod8, O_RDWR | O_NONBLOCK);
    if (UIO_LCOS_Temp_L < 1)
    {
        printf("Invalid UIO device file : %s.\n",uiod8);
    }

    //For GPIO
    GPIO_exportfd = open("/sys/class/gpio/export", O_WRONLY);
    if (GPIO_exportfd < 0)
    {
        printf("Cannot open GPIO to export it\n");
        exit(1);
    }
/*
    write(GPIO_exportfd, "913", TEST_LEN);
    close(GPIO_exportfd);
    printf("GPIO exported successfully\n");

    // Update the direction of the GPIO to be an output
    GPIO_directionfd = open("/sys/class/gpio/gpio913/direction", O_RDWR);
    if (GPIO_directionfd < 0)
    {
        printf("Cannot open GPIO direction it\n");
        exit(1);
    }

    write(GPIO_directionfd, "out", TEST_LEN);
    close(GPIO_directionfd);
    printf("GPIO direction set as output successfully\n");

    // Get the GPIO value ready to be toggled
    GPIO_valuefd = open("/sys/class/gpio/gpio913/value", O_RDWR);
    if (GPIO_valuefd < 0)
    {
        printf("Cannot open GPIO value\n");
        exit(1);
    }

    printf("GPIO value opened, now toggling...\n");
*/
}

AlarmModule::~AlarmModule()
{
    if (UIO_DAC_OA >= 0) close(UIO_DAC_OA);
    if (UIO_DAC_OD >= 0) close(UIO_DAC_OD);
    if (UIO_GRID_Temp_H >= 0) close(UIO_GRID_Temp_H);
    if (UIO_LCOS_Temp_H >= 0) close(UIO_LCOS_Temp_H);
    if (GPIO_valuefd >= 0) close(GPIO_valuefd);
    if (UIO_Hard_Reset >= 0) close(UIO_Hard_Reset);
    if (UIO_EEPROM_Load_Ready >= 0) close(UIO_EEPROM_Load_Ready);
    if (UIO_GRID_Temp_L >= 0) close(UIO_GRID_Temp_L);
    if (UIO_LCOS_Temp_L >= 0) close(UIO_LCOS_Temp_L);

    // Unexport the GPIO pin
    int GPIO_unexportfd = open("/sys/class/gpio/unexport", O_WRONLY);
    if (GPIO_unexportfd >= 0)
    {
        write(GPIO_unexportfd, "913", TEST_LEN);
        close(GPIO_unexportfd);
    }
    delete mmapTEC;
    delete mmapGPIO;
}

AlarmModule *AlarmModule::GetInstance()
{
	if (pthread_mutex_lock(&global_mutex[LOCK_UIO_ALARM]) != 0)	// locking and checking the result, if lock was successful and no deadlock happened
		std::cout << "global_mutex[LOCK_UIO_ALARM] lock unsuccessful" << std::endl;
	else
	{
	    if (pinstance_ == nullptr)
	    {
	        pinstance_ = new AlarmModule();
	    }

		if (pthread_mutex_unlock(&global_mutex[LOCK_UIO_ALARM]) != 0)	// Unlocking and checking the result, if lock was successful and no deadlock happened
			std::cout << "global_mutex[LOCK_UIO_ALARM] unlock unsuccessful" << std::endl;
	}

    return pinstance_;
}

int AlarmModule::MoveToThread()
{
	if (thread_id == 0)
	{
        if((UIO_DAC_OA < 1) || (UIO_DAC_OD < 1) || (UIO_GRID_Temp_H < 1) || (UIO_LCOS_Temp_H < 1) || (UIO_Hard_Reset < 1) || 
            (UIO_Pattern_Received < 1) || (UIO_EEPROM_Load_Ready < 1) || (UIO_GRID_Temp_L < 1) || (UIO_LCOS_Temp_L < 1))
        {
        	printf("Not all the file was open do not create pthread .\n");
        	return -1;
        }

		if (pthread_create(&thread_id, &thread_attrb, ThreadHandle, (void*) this) != 0) // 'this' is passed to pointer, so pointer dies as function dies
		{
			printf("Driver<AlarmModule>: thread_id create fail.\n");
			return (-1);
		}
		else
		{
			printf("Driver<AlarmModule>: thread_id create OK.\n");
		}
	}
	else
	{
		printf("Driver<AlarmModule>: thread_id already exist.\n");
		return (-1);
	}

	return (0);
}

void *AlarmModule::ThreadHandle(void *arg)
{
	AlarmModule *recvPtr = (AlarmModule*) arg;
	recvPtr->ProcessUIOAlarmMonitoring();
	return (NULL);
}

void AlarmModule::ProcessUIOAlarmMonitoring(void)
{

    while (thread_id != 0) // Add a flag for graceful termination
    {
        // Use poll() to wait for interrupts on all UIO devices
        //printf("[DEBUG] Polling UIO devices... (timeout=5s)\n");
        struct pollfd fds[9] = {
            {UIO_DAC_OA, POLLIN, 0},
            {UIO_DAC_OD, POLLIN, 0},
            {UIO_GRID_Temp_H, POLLIN, 0},
            {UIO_LCOS_Temp_H, POLLIN, 0},
            {UIO_Hard_Reset, POLLIN, 0},
            {UIO_Pattern_Received, POLLIN, 0},
            {UIO_EEPROM_Load_Ready, POLLIN, 0},
            {UIO_GRID_Temp_L, POLLIN, 0},
            {UIO_LCOS_Temp_L, POLLIN, 0}
        };
        int ret = poll(fds, 9, 5000); // Wait for 5 seconds
        if (ret < 0)
        {
            perror("poll() failed");
            continue;
        } else if (ret == 0){
            //printf("[DEBUG] poll() timed out, no interrupts detected.\n");
            continue;
        } else {
            printf("[DEBUG] poll() returned %d events.\n", ret);
        }

        // Prints the trigger status of each device
        for (int i = 0; i < 9; i++)
        {
            printf("[DEBUG] Device fd=%d, revents=0x%X (%s)\n",
                   fds[i].fd,
                   fds[i].revents,
                   (fds[i].revents & POLLIN) ? "Interrupt!" : "No event");
        }

        // Process each UIO device
        if (fds[0].revents & POLLIN)
        {
#ifdef _SPI_INTERFACE_
            SpiCmdDecoder::hss.opticalControlFailure = 1; // Bit 4
            SpiCmdDecoder::hss.internalFailure = 1;       // Bit 7
#else
            std::lock_guard<std::mutex> lock(m_wssAccessFailure.mtx);
            m_wssAccessFailure.Raised = true;
            m_wssAccessFailure.RaisedCount += 1;
            m_wssAccessFailure.Degraded = true;
            m_wssAccessFailure.RaisedCount += 1;
            FaultMonitor::logFault(WSS_ACCESS_FAILURE, m_wssAccessFailure);
            //ProcessUIODevice(UIO_DAC_OA, HisCon_OA, DeOA_Flag, WATCH_DOG_EVENT);
            //WSS_ACCESS
#endif
            //mmapGPIO->WriteRegister_GPIO(0x0000/0x4, 0x1);usleep(1000);
            // Re-enable interrupt if needed:
            uint32_t enable = 1;
            write(fds[0].fd, &enable, sizeof(enable));
        }
        if (fds[1].revents & POLLIN)
        {
#ifdef _SPI_INTERFACE_
            SpiCmdDecoder::hss.powerSupplyError = 1;      // Bit 5
            SpiCmdDecoder::hss.powerRailError = 1;        // Bit 6
#else 
            std::lock_guard<std::mutex> lock(m_wssAccessFailure.mtx);
            m_wssAccessFailure.Raised = true;
            m_wssAccessFailure.RaisedCount += 1;
            m_wssAccessFailure.Degraded = true;
            m_wssAccessFailure.RaisedCount += 1;
            FaultMonitor::logFault(WSS_ACCESS_FAILURE, m_wssAccessFailure);
            //ProcessUIODevice(UIO_DAC_OD, HisCon_OD, DeOD_Flag, ADC_AD7689_ACCESS_FAILURE);
            //WSS_ACCESS
#endif
            //mmapGPIO->WriteRegister_GPIO(0x0000/0x4, 0x1);usleep(1000);
            // Re-enable interrupt if needed:
            uint32_t enable = 1;
            write(fds[1].fd, &enable, sizeof(enable));
        }
        if (fds[2].revents & POLLIN)
        {
#ifdef _SPI_INTERFACE_
            //SpiCmdDecoder::hss.tempControlShutdown = DeGRID_Flag; // Bit 2
            SpiCmdDecoder::hss.internalTempError = 1;     // Bit 1
            SpiCmdDecoder::hss.thermalShutdown = 1;       // Bit 3
#else    
            unsigned int hexValue = 0;
            int status = mmapTEC->ReadRegister_TEC32(0x007C / 0x4, &hexValue);
            if (status != 0) {
                printf("Error: Failed to read INTR_STATE_REG register\n");
            } else {
                //printf("[DEBUG] Raw INTR_STATE_REG value: 0x%04X | DEC: %u\n", hexValue, hexValue);
                const uint8_t byteValue = hexValue & 0xFF; 
                const uint8_t bit3 = (byteValue >> 3) & 0x1; 
                printf("[DEBUG] 8-bit Register Value: 0x%02X\n", byteValue);
                printf("[DEBUG] Binary: 0b");
                for (int i = 7; i >= 0; i--) {
                    printf("%d", (byteValue >> i) & 0x1); 
                }
                printf(", bit3=%d\n", bit3);
                if (bit3 == 0) {
                    printf("[WARNING] Normal alert: bit3 is 0\n");
                    std::lock_guard<std::mutex> lock(m_heater2Temp.mtx);
                    m_heater2Temp.Raised = false;
                    m_heater2Temp.Degraded = true;
                    m_heater2Temp.DegradedCount += 1;
                    FaultMonitor::logFault(HEATER_2_TEMP, m_heater2Temp);
                } else if (bit3 == 1) {
                    printf("[CRITICAL] Severe alert: bit3 is 1\n");
                    std::lock_guard<std::mutex> lock(m_heater2Temp.mtx);
                    m_heater2Temp.Raised = true;
                    m_heater2Temp.RaisedCount += 1;
                    m_heater2Temp.Degraded = false;
                    FaultMonitor::logFault(HEATER_2_TEMP, m_heater2Temp);
                }
                /*
                if (hexValue == 0) {
                    printf("[WARNING] Normal alert: Register value is zero\n");
                    std::lock_guard<std::mutex> lock(m_heater2Temp.mtx);
                    m_heater2Temp.Raised = false;
                    m_heater2Temp.Degraded = true;
                    m_heater2Temp.DegradedCount += 1;
                    FaultMonitor::logFault(HEATER_2_TEMP, m_heater2Temp);
                } else if (hexValue == 1) {
                    printf("[CRITICAL] Severe alert: Register value is one\n");
                    std::lock_guard<std::mutex> lock(m_heater2Temp.mtx);
                    m_heater2Temp.Raised = true;
                    m_heater2Temp.RaisedCount += 1;
                    m_heater2Temp.Degraded = false;
                    FaultMonitor::logFault(HEATER_2_TEMP, m_heater2Temp);
                    */
            } 
            //ProcessUIODevice(UIO_GRID_Temp_H, HisCon_GRIDTemp, DeGRID_Flag, HEATER_2_TEMP);
#endif
            // Re-enable interrupt if needed:
            uint32_t enable = 1;
            write(fds[2].fd, &enable, sizeof(enable));
        }
        if (fds[3].revents & POLLIN)
        {
#ifdef _SPI_INTERFACE_
            SpiCmdDecoder::hss.tempControlShutdown = 1;   // Bit 2
            SpiCmdDecoder::hss.internalFailure = 1;       // Bit 7
            //SpiCmdDecoder::hss.thermalShutdown = DeLCOS_Flag;     // Bit 3
#else
            unsigned int hexValue = 0;
            int status = mmapTEC->ReadRegister_TEC32(0x007C / 0x4, &hexValue);
            if (status != 0) {
                printf("Error: Failed to read INTR_STATE_REG register\n");
            } else {
                //printf("[DEBUG] Raw INTR_STATE_REG value: 0x%04X | DEC: %u\n", hexValue, hexValue);
                const uint8_t byteValue = hexValue & 0xFF; 
                const uint8_t bit1 = (byteValue >> 1) & 0x1; 
                printf("[DEBUG] 8-bit Register Value: 0x%02X\n", byteValue);
                printf("[DEBUG] Binary: 0b");
                for (int i = 7; i >= 0; i--) {
                    printf("%d", (byteValue >> i) & 0x1); 
                }
                printf(", bit1=%d\n", bit1);
                if (bit1 == 0) {
                    printf("[WARNING] Normal alert: bit1 is 0\n");
                    std::lock_guard<std::mutex> lock(m_tecTemp.mtx);
                    m_tecTemp.Raised = false;
                    m_tecTemp.Degraded = true;
                    m_tecTemp.DegradedCount += 1;
                    FaultMonitor::logFault(TEC_TEMP, m_tecTemp);
                } else if (bit1 == 1) {
                    printf("[CRITICAL] Severe alert: bit1 is 1\n");
                    std::lock_guard<std::mutex> lock(m_tecTemp.mtx);
                    m_tecTemp.Raised = true;
                    m_tecTemp.RaisedCount += 1;
                    m_tecTemp.Degraded = false;
                    FaultMonitor::logFault(TEC_TEMP, m_tecTemp);
                }
				} else {
                    printf("[UNKNOWN] Unexpected register value: %u\n", hexValue);
                }
            }
            //ProcessUIODevice(UIO_LCOS_Temp_H, HisCon_LCOSTemp, DeLCOS_Flag, HEATER_1_TEMP);
#endif
            // Re-enable interrupt if needed:
            uint32_t enable = 1;
            write(fds[3].fd, &enable, sizeof(enable));
        }
        if (fds[4].revents & POLLIN)
        {
            HardReset();    
            // Re-enable interrupt if needed:
            uint32_t enable = 1;
            write(fds[4].fd, &enable, sizeof(enable));
        }
        if (fds[5].revents & POLLIN)
        {
            // printf("[DEBUG] UIO_Pattern_Received\n");
            // int count;
            // if (read(fds[5].fd, &count, sizeof(count)) == sizeof(count)) {
            //     printf("[DEBUG] UIO_Pattern_Received (fd=%d): Interrupt triggered, count=%d\n", 
            //            fds[5].fd, count);
            //     // Re-enable interrupt if needed:
            //     uint32_t enable = 1;
            //     write(fds[5].fd, &enable, sizeof(enable));
            // } else {
            //     perror("Failed to read UIO_Pattern_Received");
            // }
        }
        if (fds[6].revents & POLLIN)
        {
            // printf("[DEBUG] UIO_EEPROM_Load_Ready\n");
            // int count;
            // if (read(fds[6].fd, &count, sizeof(count)) == sizeof(count)) {
            //     printf("[DEBUG] UIO_EEPROM_Load_Ready (fd=%d): Interrupt triggered, count=%d\n", 
            //         fds[6].fd, count);
            //     // Re-enable interrupt
            //     uint32_t enable = 1;
            //     write(fds[6].fd, &enable, sizeof(enable));
            // } else {
            //     perror("Failed to read UIO_EEPROM_Load_Ready");
            // }   
        }
        if (fds[7].revents & POLLIN)
        {
#ifdef _SPI_INTERFACE_
            SpiCmdDecoder::hss.tempControlShutdown = 1;   // Bit 2
            SpiCmdDecoder::hss.internalFailure = 1;       // Bit 7
            //SpiCmdDecoder::hss.thermalShutdown = DeLCOS_Flag;     // Bit 3
#else
            unsigned int hexValue = 0;
            int status = mmapTEC->ReadRegister_TEC32(0x007C / 0x4, &hexValue);
            if (status != 0) {
                printf("Error: Failed to read INTR_STATE_REG register\n");
            } else {
                //printf("[DEBUG] Raw INTR_STATE_REG value: 0x%04X | DEC: %u\n", hexValue, hexValue);
                const uint8_t byteValue = hexValue & 0xFF; 
                const uint8_t bit2 = (byteValue >> 2) & 0x1; 
                printf("[DEBUG] 8-bit Register Value: 0x%02X\n", byteValue);
                printf("[DEBUG] Binary: 0b");
                for (int i = 7; i >= 0; i--) {
                    printf("%d", (byteValue >> i) & 0x1); 
                }
                printf(", bit2=%d\n", bit2);
                if (bit2 == 0) {
                    printf("[WARNING] Normal alert: bit2 is 0\n");
                    std::lock_guard<std::mutex> lock(m_heater2Temp.mtx);
                    m_heater2Temp.Raised = false;
                    m_heater2Temp.Degraded = true;
                    m_heater2Temp.DegradedCount += 1;
                    FaultMonitor::logFault(HEATER_2_TEMP, m_heater2Temp);
                } else if (bit2 == 1) {
                    printf("[CRITICAL] Severe alert: bit2 is 1\n");
                    std::lock_guard<std::mutex> lock(m_heater2Temp.mtx);
                    m_heater2Temp.Raised = true;
                    m_heater2Temp.RaisedCount += 1;
                    m_heater2Temp.Degraded = false;
                    FaultMonitor::logFault(HEATER_2_TEMP, m_heater2Temp);
                } else {
                    printf("[UNKNOWN] Unexpected register value: %u\n", hexValue);
                }
            }
            //ProcessUIODevice(UIO_GRID_Temp_L, HisCon_LCOSTemp, DeLCOS_Flag, HEATER_1_TEMP);
#endif
            // Re-enable interrupt if needed:
            uint32_t enable = 1;
            write(fds[7].fd, &enable, sizeof(enable));
        }
        if (fds[8].revents & POLLIN)
        {
#ifdef _SPI_INTERFACE_
            SpiCmdDecoder::hss.tempControlShutdown = 1;   // Bit 2
            SpiCmdDecoder::hss.internalFailure = 1;       // Bit 7
            //SpiCmdDecoder::hss.thermalShutdown = DeLCOS_Flag;     // Bit 3
#else
            unsigned int hexValue = 0;
            int status = mmapTEC->ReadRegister_TEC32(0x007C / 0x4, &hexValue);
            if (status != 0) {
                printf("Error: Failed to read INTR_STATE_REG register\n");
            } else {
                //printf("[DEBUG] Raw INTR_STATE_REG value: 0x%04X | DEC: %u\n", hexValue, hexValue);
                const uint8_t byteValue = hexValue & 0xFF; 
                const uint8_t bit0 = (byteValue >> 0) & 0x1; 
                printf("[DEBUG] 8-bit Register Value: 0x%02X\n", byteValue);
                printf("[DEBUG] Binary: 0b");
                for (int i = 7; i >= 0; i--) {
                    printf("%d", (byteValue >> i) & 0x1); 
                }
                printf(", bit0=%d\n", bit0);
                if (bit0 == 0) {
                    printf("[WARNING] Normal alert: bit0 is 0\n");
                    std::lock_guard<std::mutex> lock(m_tecTemp.mtx);
                    m_tecTemp.Raised = false;
                    m_tecTemp.Degraded = true;
                    m_tecTemp.DegradedCount += 1;
                    FaultMonitor::logFault(TEC_TEMP, m_tecTemp);
                } else if (bit0 == 1) {
                    printf("[CRITICAL] Severe alert: bit0 is 1\n");
                    std::lock_guard<std::mutex> lock(m_tecTemp.mtx);
                    m_tecTemp.Raised = true;
                    m_tecTemp.RaisedCount += 1;
                    m_tecTemp.Degraded = false;
                    FaultMonitor::logFault(TEC_TEMP, m_tecTemp);
                } else {
                    printf("[UNKNOWN] Unexpected register value: %u\n", hexValue);
                }
            }
            //ProcessUIODevice(UIO_LCOS_Temp_L, HisCon_LCOSTemp, DeLCOS_Flag, HEATER_1_TEMP);
#endif
            // Re-enable interrupt if needed:
            uint32_t enable = 1;
            write(fds[8].fd, &enable, sizeof(enable));
        }     
        //SpiCmdDecoder::hss.caseTempError = DeCase_Flag; //currently not available because no case tempsensor yet.
        //other hardware status polling below:
        //ADC/DAC Access Error
        CheckADCPowerSupply();
		CheckDACPowerSupply();
        //TRANSFER_FAILURE

        //Watch_Dog_Event
        //Firmware_Download_Failure
        //...


    }
    pthread_exit(NULL);
}

void AlarmModule::StopThread()
{

	// Wait for thread to exit normally
	if (thread_id != 0)
	{
		pthread_join(thread_id, NULL);
		thread_id = 0;
	}

	printf("Driver<AlarmModule> Thread terminated\n");

	if(pinstance_ != nullptr)
	{
		delete pinstance_;
		pinstance_ = nullptr;
	}
}

void AlarmModule::GpioWrite(int fd, char level)
{
	if(level == '0')
	{
	    write(fd,"0", 2);
	}

	if(level == '1')
	{
		write(fd,"1", 2);
	}
}

void AlarmModule::CheckADCPowerSupply(){
	// Added: Check if the 5V ADC power supply voltage is normal
	unsigned int hexAdcVoltage = 0;
	int adcStatus = mmapTEC->ReadRegister_TEC32(ADC_REG_ADDR / 0x4, &hexAdcVoltage);
	if (adcStatus != 0) {
		printf("Error: Failed to read 5V ADC power supply register\n");
	}
	//printf("[DEBUG] Raw ADC value: 0x%04X | DEC: %u\n", hexAdcVoltage, hexAdcVoltage);

	double hexAdcVoltage_adjusted = (hexAdcVoltage / 33.2) * (33.2 + 91);
	double vadc_out = (hexAdcVoltage_adjusted * ADC_REF_VOLTAGE) / 4096;
	if (vadc_out < 4.5 || vadc_out > 5.5) {
		printf("[ERROR] ADC not enabled! 5V power supply abnormal: current voltage=%.2fV\n", vadc_out);
		std::lock_guard<std::mutex> lock(m_adcAccessFailure.mtx);
		m_adcAccessFailure.Raised = true;
		m_adcAccessFailure.RaisedCount += 1;
		m_adcAccessFailure.Degraded = true;
		m_adcAccessFailure.DegradedCount = m_adcAccessFailure.RaisedCount;
        FaultMonitor::logFault(ADC_AD7689_ACCESS_FAILURE,m_adcAccessFailure);
	}
}

void AlarmModule::CheckDACPowerSupply(){
	// Added: DAC output check section
	unsigned int hexAdcInput = 0;
	// const uint16_t ADC_DATA_MASK = 0x0FFF;  // Define 12-bit data mask
	int dacStatus = mmapTEC->ReadRegister_TEC32(DAC_REG_ADDR / 0x4, &hexAdcInput);
	if (dacStatus != 0) {
		printf("Error: Failed to read portD(ADC input/DAC output) register\n");
	}
	// Extract valid 12-bit data (assuming right-aligned)
	uint16_t adcInputData = hexAdcInput & ADC_DATA_MASK;
	// printf("[DEBUG] Raw DAC value: 0x%04X | DEC: %u\n", hexAdcInput, hexAdcInput);
	//printf("[DEBUG] Raw DAC value: 0x%04X | Valid 12-bit: 0x%03X\n", hexAdcInput, adcInputData);
	// DAC value validation (example conditions)
	double vadc_tempout = (adcInputData * ADC_REF_VOLTAGE) / 4096;
	// Calculate DAC output voltage with gain compensation (1 + 33/33)
	double vdac_out = (vadc_tempout * (1 + 33 + 33)) / 33;
	//printf("[DEBUG] DAC current output voltage=%.2fV\n", vdac_out);
	if (vdac_out < 1 || vdac_out > 1.25) {
		printf("[ERROR] DAC output abnormal: current voltage=%.2fV\n", vdac_out);
		std::lock_guard<std::mutex> lock(m_dacAccessFailure.mtx);
		m_dacAccessFailure.Raised = true;
		m_dacAccessFailure.RaisedCount += 1;
		m_dacAccessFailure.Degraded = true;
		m_dacAccessFailure.DegradedCount = m_dacAccessFailure.RaisedCount;
        FaultMonitor::logFault(DAC_AD5624_ACCESS_FAILURE,m_dacAccessFailure);
	}
}

void AlarmModule::HardReset(){
    try {
        const std::string firmwarePath = "/mnt/startwss.elf";
        
        // Step 1: Verify firmware existence
        if (access(firmwarePath.c_str(), F_OK) != 0) {
            throw std::runtime_error("Main firmware not found");
        }
        // Step 2: Set executable permissions
        if (chmod(firmwarePath.c_str(), 0777) != 0) {  // Note: 0777 is octal format
            throw std::runtime_error("Permission setting failed: " + std::string(strerror(errno)));
        }
        // Step 3: Close all non-standard file descriptors
        int max_fd = sysconf(_SC_OPEN_MAX);
        for (int fd = 3; fd < max_fd; ++fd) {
            close(fd);  // Errors are ignored (invalid fds return EBADF)
        }
        // Step 4: Perform process replacement
        char* argv[] = {const_cast<char*>(firmwarePath.c_str()), nullptr};
        execv(firmwarePath.c_str(), argv);
        // If execution reaches here, execv failed
        throw std::runtime_error("Execution failed: " + std::string(strerror(errno)));
    } catch (const std::runtime_error& e) {
        std::cerr << "HardReset Error: " << e.what() << std::endl;
    }
}
