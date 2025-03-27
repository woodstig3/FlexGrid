#include <poll.h>

#include "AlarmUIO.h"
#include "SpiCmdDecoder.h"


const char *uiod0 = "/dev/uio0";    // LCos panel voltage exceeding: Optics failure
const char *uiod1 = "/dev/uio1";	// Lcos panel voltage exceeding: Optics failure
const char *uiod2 = "/dev/uio2";	// Grating component temperature exceeding: internal temperature
const char *uiod3 = "/dev/uio3";    // Thermal Failure or LCOS permanently damaged
const char *uiod4 = "/dev/uio4";  // LCOS display panel access error

AlarmModule *AlarmModule::pinstance_{nullptr};

AlarmModule::AlarmModule()
{
	HisCon_OA = HisCon_OD = HisCon_GRIDTemp = HisCon_LCOSTemp = 0;
	DeOA_Flag = DeOD_Flag = DeGRID_Flag = DeLCOS_Flag = false;

	thread_id = 0;
	pthread_attr_init(&thread_attrb);	//Default initialize thread attributes

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
    UIO_GRID_Temp = open(uiod2, O_RDWR | O_NONBLOCK);
    if (UIO_GRID_Temp < 1)
    {
        printf("Invalid UIO device file : %s.\n",uiod2);
    }
    UIO_LCOS_Temp = open(uiod3, O_RDWR | O_NONBLOCK);
    if (UIO_LCOS_Temp < 1)
    {
        printf("Invalid UIO device file : %s.\n",uiod3);
    }
    UIO_LCOS_Ready = open(uiod4, O_RDWR | O_NONBLOCK);
    if (UIO_LCOS_Ready < 1)
    {
        printf("Invalid UIO device file : %s.\n",uiod4);
    }

    //For GPIO
    GPIO_exportfd = open("/sys/class/gpio/export", O_WRONLY);
    if (GPIO_exportfd < 0)
    {
        printf("Cannot open GPIO to export it\n");
        exit(1);
    }

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
}

AlarmModule::~AlarmModule()
{
    if (UIO_DAC_OA >= 0) close(UIO_DAC_OA);
    if (UIO_DAC_OD >= 0) close(UIO_DAC_OD);
    if (UIO_GRID_Temp >= 0) close(UIO_GRID_Temp);
    if (UIO_LCOS_Temp >= 0) close(UIO_LCOS_Temp);
    if (GPIO_valuefd >= 0) close(GPIO_valuefd);
    if (UIO_LCOS_Ready >= 0) close(UIO_LCOS_Ready);

    // Unexport the GPIO pin
    int GPIO_unexportfd = open("/sys/class/gpio/unexport", O_WRONLY);
    if (GPIO_unexportfd >= 0)
    {
        write(GPIO_unexportfd, "913", TEST_LEN);
        close(GPIO_unexportfd);
    }
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
        if((UIO_DAC_OA < 1) || (UIO_DAC_OD < 1) || (UIO_GRID_Temp < 1) || (UIO_LCOS_Temp < 1) || (UIO_LCOS_Ready < 1))
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

void AlarmModule::ProcessUIODevice(int fd, int& hisCon, bool& deFlag, FaultsName logName)
{

    int count;
    int err = read(fd, &count, TEST_LEN);
    if (err != TEST_LEN)
    {
        perror("UIO device read error");
        return;
    }

    FaultsAttr UIOMess = {0};
    if (count != hisCon)
    {
        hisCon = count;
        UIOMess.Raised = true;
        UIOMess.RaisedCount = count;
        UIOMess.Degraded = false;
        UIOMess.DegradedCount = hisCon;
        FaultMonitor::logFault(logName, UIOMess);
        deFlag = true;

    }
    else
    {
        UIOMess.Raised = false;
        UIOMess.RaisedCount = count;
        UIOMess.Degraded = true;
        UIOMess.DegradedCount = hisCon;
        if (deFlag)
        {
        	FaultMonitor::logFault(logName, UIOMess);
            deFlag = false;
        }
    }
}

void AlarmModule::ProcessUIOAlarmMonitoring(void)
{

    while (thread_id != 0) // Add a flag for graceful termination
    {
        // Use poll() to wait for interrupts on all UIO devices
        struct pollfd fds[4] = {
            {UIO_LCOS_Temp, POLLIN, 0},
            {UIO_GRID_Temp, POLLIN, 0},
            {UIO_DAC_OD, POLLIN, 0},
            {UIO_DAC_OA, POLLIN, 0}
        };
        int ret = poll(fds, 4, 5000); // Wait for 5 seconds
        if (ret < 0)
        {
            perror("poll() failed");
            continue;
        }

        // Process each UIO device
        if (fds[0].revents & POLLIN)
        {
            ProcessUIODevice(UIO_LCOS_Temp, HisCon_LCOSTemp, DeLCOS_Flag, HEATER_1_TEMP);
            SpiCmdDecoder::hss.tempControlShutdown = DeLCOS_Flag;
            SpiCmdDecoder::hss.internalFailure = DeOA_Flag;
        }
        if (fds[1].revents & POLLIN)
        {
            ProcessUIODevice(UIO_GRID_Temp, HisCon_GRIDTemp, DeGRID_Flag, HEATER_2_TEMP);
            SpiCmdDecoder::hss.internalTempError = DeLCOS_Flag;
            SpiCmdDecoder::hss.thermalShutdown = DeLCOS_Flag;
        }
        if (fds[2].revents & POLLIN)
        {
            ProcessUIODevice(UIO_DAC_OD, HisCon_OD, DeOD_Flag, ADC_AD7689_ACCESS_FAILURE);
            SpiCmdDecoder::hss.powerSupplyError = DeOD_Flag;
            SpiCmdDecoder::hss.powerRailError = DeOD_Flag;
        }
        if (fds[3].revents & POLLIN)
        {
            ProcessUIODevice(UIO_DAC_OA, HisCon_OA, DeOA_Flag, WATCH_DOG_EVENT);
            SpiCmdDecoder::hss.opticalControlFailure = DeOA_Flag;
            SpiCmdDecoder::hss.internalFailure = DeOA_Flag;
        }
        //SpiCmdDecoder::hss.caseTempError = DeCase_Flag;
        //other hardware status polling below:
        //ADC/DAC Access Error

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
