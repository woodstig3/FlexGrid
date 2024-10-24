#include "AlarmUIO.h"
#include "InterfaceModule/Dlog.h"


const char *uiod0 = "/dev/uio0";
const char *uiod1 = "/dev/uio1";
const char *uiod2 = "/dev/uio2";
const char *uiod3 = "/dev/uio3";
const char *uiod4 = "/dev/uio4";

AlarmModule *AlarmModule::pinstance_{nullptr};

AlarmModule::AlarmModule()
{
	HisCon_OA = HisCon_OD = HisCon_GRIDTemp = HisCon_LCOSTemp = 0;
	DeOA_Flag = DeOD_Flag = DeGRID_Flag = DeLCOS_Flag = true;

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
	close(UIO_DAC_OA);
	close(UIO_DAC_OD);
	close(UIO_GRID_Temp);
	close(UIO_LCOS_Temp);
	close(GPIO_valuefd);
	close(UIO_LCOS_Ready);
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
        if((UIO_DAC_OA < 1) || (UIO_DAC_OD < 1) || (UIO_GRID_Temp < 1) || (UIO_LCOS_Temp < 1))
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
	int irq_on = 1;
	int icount,jcount,kcount,lconut;
	int err;
	MessRes UIOMess = {0};

	icount = jcount = kcount = lconut = 0;
	while(true)
	{
		sleep(5);

		write(UIO_LCOS_Temp, &irq_on, sizeof(irq_on));
		err = read(UIO_LCOS_Temp, &icount, TEST_LEN);
		/*printf("Con_LCOSTemp : %d icount : %d\n",err,icount);
        if (err != TEST_LEN)
        {
            perror("UIO_LCOS_Temp ERR\n");
        }*/

        if(icount != HisCon_LCOSTemp)
        {
            HisCon_LCOSTemp = icount;
            //TEC log
            UIOMess.Raised = true;
            UIOMess.RaisedCount = icount;
            UIOMess.Degraded = false;
            UIOMess.DegradedCount = HisCon_LCOSTemp;
            Fault_logcompress(HEATER_1_TEMP,&UIOMess);

            DeLCOS_Flag = true;
        }
        else
        {
            UIOMess.Raised = false;
            UIOMess.RaisedCount = icount;
            UIOMess.Degraded = true;
            UIOMess.DegradedCount = HisCon_LCOSTemp;
            if(DeLCOS_Flag == true)
            {
                Fault_logcompress(HEATER_1_TEMP,&UIOMess);
                DeLCOS_Flag = false;
            }
        }

        memset(&UIOMess, 0, sizeof(UIOMess));
        write(UIO_GRID_Temp, &irq_on, sizeof(irq_on));
        err = read(UIO_GRID_Temp, &jcount, TEST_LEN);
        /*printf("Con_GRIDTemp : %d jcount : %d\n",err,jcount);
        if (err != TEST_LEN)
        {
            perror("UIO_GRID_Temp ERR\n");
        }*/

        if(jcount != HisCon_GRIDTemp)
        {
        	HisCon_GRIDTemp = jcount;
        	//HEATER_2_TEMP log
            UIOMess.Raised = true;
            UIOMess.RaisedCount = jcount;
            UIOMess.Degraded = false;
            UIOMess.DegradedCount = HisCon_GRIDTemp;
            Fault_logcompress(HEATER_2_TEMP,&UIOMess);

            DeGRID_Flag = true;
        }
        else
        {
            UIOMess.Raised = false;
            UIOMess.RaisedCount = jcount;
            UIOMess.Degraded = true;
            UIOMess.DegradedCount = HisCon_GRIDTemp;
            if(DeGRID_Flag == true)
            {
                Fault_logcompress(HEATER_2_TEMP,&UIOMess);
                DeGRID_Flag = false;
            }
        }

        memset(&UIOMess, 0, sizeof(UIOMess));
        write(UIO_DAC_OD, &irq_on, sizeof(irq_on));
        err = read(UIO_DAC_OD, &kcount, TEST_LEN);
        /*printf("con_DAC_ODTemp : %d kconut : %d\n",err,kcount);
        if (err != TEST_LEN)
        {
            perror("UIO_DAC_OD ERR \n");
        }*/

        if(kcount != HisCon_OD)
        {
        	HisCon_OD = kcount;
        	//ADC_AD7689_ACCESS_FAILURE log 2?
            UIOMess.Raised = true;
            UIOMess.RaisedCount = kcount;
            UIOMess.Degraded = false;
            UIOMess.DegradedCount = HisCon_OD;
            Fault_logcompress(ADC_AD7689_ACCESS_FAILURE,&UIOMess);

            DeOD_Flag = true;
        }
        else
        {
            UIOMess.Raised = false;
            UIOMess.RaisedCount = kcount;
            UIOMess.Degraded = true;
            UIOMess.DegradedCount = HisCon_OD;
            if(DeOD_Flag == true)
            {
                Fault_logcompress(ADC_AD7689_ACCESS_FAILURE,&UIOMess);
                DeOD_Flag = false;
            }
        }

        memset(&UIOMess, 0, sizeof(UIOMess));
        write(UIO_DAC_OA, &irq_on, sizeof(irq_on));
        err = read(UIO_DAC_OA, &lconut, TEST_LEN);
        /*printf("con_DAC_OATemp : %d lconut : %d\n",err,lconut);
        if (err != TEST_LEN)
        {
            perror("UIO_DAC_OA ERR \n");
        }*/
        if(lconut != HisCon_OA)
        {
        	HisCon_OA = lconut;
        	//ADC_AD7689_ACCESS_FAILURE log 2?
            UIOMess.Raised = true;
            UIOMess.RaisedCount = lconut;
            UIOMess.Degraded = false;
            UIOMess.DegradedCount = HisCon_OA;
            Fault_logcompress(WATCH_DOG_EVENT,&UIOMess);

            DeOA_Flag = true;
        }
        else
        {
            UIOMess.Raised = false;
            UIOMess.RaisedCount = lconut;
            UIOMess.Degraded = true;
            UIOMess.DegradedCount = HisCon_OA;
            if(DeOA_Flag == true)
            {
                Fault_logcompress(WATCH_DOG_EVENT,&UIOMess);
                DeOA_Flag = false;
            }
        }


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
