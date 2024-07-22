/*
 * TemperatureMonitor.cpp
 *
 *  Created on: Mar 9, 2023
 *      Author: Administrator
 */

#include "TemperatureMonitor.h"
#include <cassert>
long PRIMARK = 0.0;
#define TEC_STRCHEECK_LOW 50.0
#define TEC_STRCHEECK_HIGH 58.0
/*
 * When delta Temp change is more than +- 0.2 we calculate pattern
 * TEC working range is 65+-2C... 63-67C ,if temperature goes
 * beyond this range we alter user
 *
 *
 * USE FIR- Moving average filter - CHATGPT
 *
 * 1- Read AHB register
 * 2- Check LUT to get Temperature value
 * 3- Check if temp is within the operating range (63-65)
 * 4- If temp within operating range:
 * 			Apply moving average filter and check if temperature changed or not to 0.2 C
 * 			If temperature changed 0.2C then update flag tempChanged and keep reading AHB register
 * 			If temperature didnt change, keep reading AHB register
 * 5-if temp is not in operating range:
 * 			If temperature is low then wait for FPGA to fix TEC, set PANEL:ready false
 * 			If temperature is high then alert user for high temp
 */
TemperatureMonitor *TemperatureMonitor::pinstance_{nullptr};

TemperatureMonitor::TemperatureMonitor()
{
	g_serialMod = SerialModule::GetInstance();

	mmapTEC = new MemoryMapping(MemoryMapping::TEC);

	int status = TemperatureMon_Initialize();

	if(status != 0)
	{
		printf("Driver<TempMonitor>: Temperature Mon. Module Initialization Failed.\n");
		g_serialMod->Serial_WritePort("\01INTERNAL_ERROR\04\n");
		m_bTempLUTOk = false;
		//TemperatureMonitor_Closure();
	}
	else
	{
		m_bTempLUTOk = true;
	}

}

TemperatureMonitor::~TemperatureMonitor()
{
	delete mmapTEC;
}

/**
 * The first time we call GetInstance we will lock the storage location
 *      and then we make sure again that the variable is null and then we
 *      set the value. RU:
 */
TemperatureMonitor *TemperatureMonitor::GetInstance()
{
	if (pthread_mutex_lock(&global_mutex[LOCK_TEMP_INSTANCE]) != 0)	// locking and checking the result, if lock was successful and no deadlock happened
		std::cout << "global_mutex[LOCK_TEMP_INSTANCE] lock unsuccessful" << std::endl;
	else
	{
	    if (pinstance_ == nullptr)
	    {
	        pinstance_ = new TemperatureMonitor();
	    }

		if (pthread_mutex_unlock(&global_mutex[LOCK_TEMP_INSTANCE]) != 0)	// Unlocking and checking the result, if lock was successful and no deadlock happened
			std::cout << "global_mutex[LOCK_TEMP_INSTANCE] unlock unsuccessful" << std::endl;
	}

    return pinstance_;
}

int TemperatureMonitor::TemperatureMon_Initialize(void)
{
	b_LoopOn = true;

	thread_id = 0;
	pthread_attr_init(&thread_attrb);	//Default initialize thread attributes

	if(Load_TempSensor_LUT() == -1)
	{
		printf("Driver<TempMonitor>: LUT loading failed.\n");
		return (-1);
	}

	if(Load_OldTempSensor_LUT() == -1)
	{
		printf("Driver<TempMonitor>: LUT loading failed.\n");
		return (-1);
	}

	if(Config_FPGA_TEC() == -1)
	{
		printf("Driver<TempMonitor>: FPGA_TEC settings failed\n");
		return (-1);
	}

	GetZYNQTempVars();

	return (0);
}

int TemperatureMonitor::MoveToThread()
{
	if (thread_id == 0)
	{
		if (pthread_create(&thread_id, &thread_attrb, ThreadHandle, (void*) this) != 0) // 'this' is passed to pointer, so pointer dies as function dies
		{
			printf("Driver<TempMonitor>: thread_id create fail.\n");
			return (-1);
		}
		else
		{
			printf("Driver<TempMonitor>: thread_id create OK.\n");
		}
	}
	else
	{
		printf("Driver<TempMonitor>: thread_id already exist.\n");
		return (-1);
	}

	return (0);
}

void *TemperatureMonitor::ThreadHandle(void *arg)
{
	TemperatureMonitor *recvPtr = (TemperatureMonitor*) arg;
	recvPtr->ProcessTemperatureMonitoring();
	return (NULL);
}

void TemperatureMonitor::ProcessTemperatureMonitoring(void)
{
    double m_direct_LCOS_Temp{0}, m_direct_Heater1_Temp{0}, m_direct_Heater2_Temp{0};

    double temp_buffer_LCOS[WINDOW_SIZE], temp_buffer_Heater1[WINDOW_SIZE], temp_buffer_Heater2[WINDOW_SIZE];

    int buffer_index_LCOS{0};
    int buffer_index_Heater1{0};
    int buffer_index_Heater2{0};

    double filteredTemp_LCOS{0};
    double filteredTemp_Heater1{0};
    double filteredTemp_Heater2{0};

	while(true)
	{
		usleep(500000);

		if(BreakThreadLoop() == 0)
		{
			break;
		}

        if(b_LoopOn && m_bTempLUTOk)	// if and only if the main loop is running we will perform calculations
        {
        	if(1)			// if FPGA signaled stable TEC
        	{
        		ProcessLCOS(filteredTemp_LCOS, temp_buffer_LCOS, &buffer_index_LCOS, m_direct_LCOS_Temp);
        		/* Heaters Sensor LUT is different from LCOS. Currently I closed the heater process */
        		//ProcessHeater1(filteredTemp_Heater1, temp_buffer_Heater1, &buffer_index_Heater1, m_direct_Heater1_Temp);
        		ProcessHeater2(filteredTemp_Heater2, temp_buffer_Heater2, &buffer_index_Heater2, m_direct_Heater2_Temp);

//        		printf("LCOS = %f  \tHEATER1 = %f  \tHEATER2 = %f\n",
//        				m_direct_LCOS_Temp, m_direct_Heater1_Temp, m_direct_Heater2_Temp);

#ifdef _DEVELOPMENT_MODE_

        		if(Check_Need_For_TEC_Data_Transfer_To_PC())
        		{
            		char buff[100];
    				//sprintf(buff, "\x24:%0.2f:%0.2f:%0.2f:%0.2f:%0.2f:\x23", filteredTemp_Heater1, filteredTemp_Heater2, previousTemp_LCOS, filteredTemp_LCOS, 0.0);
            		// For sake of saving data to files
            		//sprintf(buff, "\x24:%0.2f:%0.2f:%0.2f:%0.2f:%0.2f:\x23", filteredTemp_Heater1, filteredTemp_Heater2, m_direct_LCOS_Temp, filteredTemp_LCOS, CpuTemp());
            		// For sake of TestRig only showing LCOS temperature
            		if(m_testRigTemperatureSwitch)
            		{
            			// Grating temperaure to sned
            			sprintf(buff, "\x24%0.3f\x23", m_direct_LCOS_Temp);
            		}
            		else
            		{
            			sprintf(buff, "\x24%0.3f\x23", m_direct_Heater2_Temp);
            		}

            		g_serialMod->Serial_WritePort(buff);
        		}

#endif
        	}
#if 0
//delete by jihongwang on 20240722
        	else
        	{

//        		if(isTECStableInFPGA())
//        		{
        			b_isTECStable = true;
//        		}
//        		else
//        		{
//            		b_isTECStable = ReturnWhenTECStable();
//        		}

        		std::cout << "TEC STABLE = " << b_isTECStable << std::endl;

        		if(b_isTECStable)
        			g_serialMod->cmd_decoder.SetPanelInfo(true);
        	}
#endif
//begin add by jihongwang on 20240722
   		    if((m_direct_Heater2_Temp > TEC_STRCHEECK_LOW)  && ( m_direct_Heater2_Temp < TEC_STRCHEECK_HIGH))
   		    {
   		           if(isTECStableInFPGA())
   		           {
   		                b_isTECStable = true;
   		           }
   		           else
   		           {
   		                b_isTECStable = false;
   		           }

                   g_serialMod->cmd_decoder.SetPanelInfo(b_isTECStable);
   		           //std::cout << "TEC STABLE = " << b_isTECStable << std::endl;
   		     }
   	         else
   		     {
   			      b_isTECStable = false;
   			      g_serialMod->cmd_decoder.SetPanelInfo(b_isTECStable);
   		     }
//end add by jihongwang on 20240722
        }

		//std::cout << "LOOPING... " << std::endl;
	}

	pthread_exit(NULL);
}

void TemperatureMonitor::ProcessLCOS(double& filteredTemp_LCOS, double temp_buffer_LCOS[], int *buffer_index_LCOS, double& m_direct_LCOS_Temp)
{
	int status;

	// Read LCOS Temperature from FPGA
	status = ReadTemperature(&m_direct_LCOS_Temp, Sensors::lCOS);

	if(status == ConversionStatus::BELOW_LUT_RANGE)
	{
		std::cout << "[Error] LCOS Temperature Read is below LUT range\n";
		return;
	}

	// Check status if its above LUT or not.
	filteredTemp_LCOS = SMA_Filter(temp_buffer_LCOS, buffer_index_LCOS, m_direct_LCOS_Temp);

	if(!initial_cond_reached_LCOS)
	{
		if(filteredTemp_LCOS >= LCOS_OPERATING_TEMP_MIN)
			initial_cond_reached_LCOS = true;
	}

	if(initial_cond_reached_LCOS)
	{
		if(filteredTemp_LCOS >= LCOS_OPERATING_TEMP_MIN && filteredTemp_LCOS <= LCOS_OPERATING_TEMP_MAX)
		{
			 double delta_temp = abs(filteredTemp_LCOS - previousTemp_LCOS);

			 if(delta_temp >= 0.2)
			 {
				// update the system with the new temperature value
				std::cout << "\n[NOTICE] LCOS Temperature updated to: " << filteredTemp_LCOS << std::endl;

				if (pthread_mutex_lock(&global_mutex[LOCK_TEMP_CHANGED_FLAG]) != 0)	// locking and checking the result, if lock was successful and no deadlock happened
					std::cout << "global_mutex[LOCK_TEMP_CHANGED_FLAG] lock unsuccessful" << std::endl;
				else
				{
					g_bTempChanged = true;

					// set the previous temperature to the new filtered temperature
					previousTemp_LCOS = filteredTemp_LCOS;

					if (pthread_mutex_unlock(&global_mutex[LOCK_TEMP_CHANGED_FLAG]) != 0)	// Unlocking and checking the result, if lock was successful and no deadlock happened
						std::cout << "global_mutex[LOCK_TEMP_CHANGED_FLAG] unlock unsuccessful" << std::endl;
				}

			 }
		}
		else if (filteredTemp_LCOS < LCOS_OPERATING_TEMP_MIN)
		{
			std::cout << "[WARNING]: LCOS Temperature Below Target " << filteredTemp_LCOS << std::endl;
		}
		else if (filteredTemp_LCOS > LCOS_OPERATING_TEMP_MAX)
		{
			std::cout << "[WARNING]: LCOS Temperature Exceeding Target!!!! " << filteredTemp_LCOS << std::endl;
		}

	}
}

void TemperatureMonitor::ProcessHeater1(double &filteredTemp_Heater1, double temp_buffer_Heater1[], int *buffer_index_Heater1, double& m_direct_Heater1_Temp)
{
	int status;

	// Read HEATER1 Temperature from FPGA
	status = ReadTemperature(&m_direct_Heater1_Temp, Sensors::HEATER1);

	if(status == ConversionStatus::BELOW_LUT_RANGE)
	{
		std::cout << "[Error] Heater1 Temperature Read is below LUT range\n";
		return;
	}

	filteredTemp_Heater1 = SMA_Filter(temp_buffer_Heater1, buffer_index_Heater1, m_direct_Heater1_Temp);

	if(!initial_cond_reached_Heater1)
	{
		if(filteredTemp_Heater1 >= HEATER_OPERATING_TEMP_MIN)
			initial_cond_reached_Heater1 = true;
	}

	if(initial_cond_reached_Heater1)
	{
		if(filteredTemp_Heater1 >= HEATER_OPERATING_TEMP_MIN && filteredTemp_Heater1 <= HEATER_OPERATING_TEMP_MAX)
		{

		}
		else if (filteredTemp_Heater1 < HEATER_OPERATING_TEMP_MIN)
		{
			//std::cout << "[WARNING]: Heater1 Temperature Below Target " << filteredTemp_Heater1 << std::endl;
		}
		else if (filteredTemp_Heater1 > HEATER_OPERATING_TEMP_MAX)
		{
			//std::cout << "[WARNING]: Heater1 Temperature Exceeding Target!!!! " << filteredTemp_Heater1 << std::endl;
		}
	}
}

void TemperatureMonitor::ProcessHeater2(double &filteredTemp_Heater2,  double temp_buffer_Heater2[], int *buffer_index_Heater2, double& m_direct_Heater2_Temp)
{
	int status;

	// Read HEATER2 Temperature from FPGA
	status = ReadTemperature(&m_direct_Heater2_Temp, Sensors::HEATER2);
	/*if((PRIMARK % 10) == 0)
	{
	    std::cout << "Heater2 : "<< m_direct_Heater2_Temp <<"\n";
	}*/

	if(status == ConversionStatus::BELOW_LUT_RANGE)
	{
		std::cout << "[Error] Heater2 Temperature Read is below LUT range\n";
		return;
	}

	filteredTemp_Heater2 = SMA_Filter(temp_buffer_Heater2, buffer_index_Heater2, m_direct_Heater2_Temp);

	if(!initial_cond_reached_Heater2)
	{
		if(filteredTemp_Heater2 >= HEATER_OPERATING_TEMP_MIN)
			initial_cond_reached_Heater2 = true;
	}

	if(initial_cond_reached_Heater2)
	{
		if(filteredTemp_Heater2 >= HEATER_OPERATING_TEMP_MIN && filteredTemp_Heater2 <= HEATER_OPERATING_TEMP_MAX)
		{
			 double delta_temp = abs(filteredTemp_Heater2 - previousTemp_GRID);

			 if(delta_temp >= 0.2)
			 {
				// update the system with the new temperature value
				std::cout << "\n[NOTICE] GRID Temperature updated to: " << filteredTemp_Heater2 << std::endl;

				if (pthread_mutex_lock(&global_mutex[LOCK_TEMP_CHANGED_FLAG]) != 0)	// locking and checking the result, if lock was successful and no deadlock happened
					std::cout << "global_mutex[LOCK_TEMP_CHANGED_FLAG] lock unsuccessful" << std::endl;
				else
				{
					g_bTempChanged = true;

					// set the previous temperature to the new filtered temperature
					previousTemp_GRID = filteredTemp_Heater2;

					if (pthread_mutex_unlock(&global_mutex[LOCK_TEMP_CHANGED_FLAG]) != 0)	// Unlocking and checking the result, if lock was successful and no deadlock happened
						std::cout << "global_mutex[LOCK_TEMP_CHANGED_FLAG] unlock unsuccessful" << std::endl;
				}

			 }
		}
		else if (filteredTemp_Heater2 < HEATER_OPERATING_TEMP_MIN)
		{
			std::cout << "[WARNING]: Heater2 Temperature Below Target " << filteredTemp_Heater2 << std::endl;
		}
		else if (filteredTemp_Heater2 > HEATER_OPERATING_TEMP_MAX)
		{
			std::cout << "[WARNING]: Heater2 Temperature Exceeding Target!!!! " << filteredTemp_Heater2 << std::endl;
		}
	}
}

bool TemperatureMonitor::ReturnWhenTECStable()
{
	if(EWMA_Filter() == 0)
		return true;
}

bool TemperatureMonitor::isTECStableInFPGA()
{
	unsigned int hexValue = 0x0;

	int status = mmapTEC->ReadRegister_TEC32(FPGA_TEC_STABLE_SIGNAL/0x4, &hexValue);

	if(status != 0 || (hexValue&0x01) != 0x1)		// 1 means stable, 0 means not stable yet
	{
		return false;
	}

	return true;
}

double TemperatureMonitor::SMA_Filter(double buf[], int *buf_index, double currentTemp)
{
	double filtered_temp = 0;

	// Apply the moving average filter
	buf[*buf_index] = currentTemp;
	*buf_index = (*buf_index + 1) % WINDOW_SIZE;

	filtered_temp = 0;

	for (int i = 0; i < WINDOW_SIZE; i++)
	{
		filtered_temp += buf[i];
	}

	filtered_temp /= WINDOW_SIZE;

	return (filtered_temp);
}

int TemperatureMonitor::EWMA_Filter()
{	// 0.02
	double alpha = 0.03;  			// Smoothing factor- 0 to 1. Higher number means more weight/importance on new data.
	double currentTempEst = 0;     // Initialize EWMA
	double targetTemp = LCOS_TEMP_TARGET;
	double tempSteadyStateError = 1.0;
	double tolerance = 0.45;  		   // Allowable tolerance around target temperature. In stable state of PID algorithm the +- swing range of temperature decide this tolerance


	// Initialize variables
	double prevTempEst{0};  // Initial temperature estimate

	bool tempStable = false;
	double tempTrend = 0;

	double m_direct_LCOS_Temp = 0x0;

	double stableSignalAtTemp_FPGA = 0.0;
	double stableSignalAtTemp_ARM = 0.0;

	static int track1 = 0;
	static int track2 = 0;

	//double windowVerify[10]{0};		// 20 sample points based verification
	std::vector<double> windowVerify;

	int index{0};

	double prevRateOfChange = 0;
	//double rateOfChangeTolerance = 0.0005; 		// GOOD SETTING - Set the tolerance for the rate of change
	//double peakToPeak = 0.09;						// GOOD SETTING

	/************************************/
	// Strict low Oscillation case:
	// Diff = 0.1 peak to peak
	// rateOfChangeTolerance = 0.0009
	// trendTolerance = 0.008;
	// ---------------------------------
	// High Oscillation case:
	// Diff = 0.40 peak to peak
	// rateOfChangeTolerance = 0.002
	// trendTolerance = 0.012;
	/************************************/
	double trendTolerance = 0.008;
	double rateOfChangeTolerance = 0.002;			// Try
	double peakToPeak = 0.15;						// Try

	while (!tempStable)
	{
		usleep(1000000);

		if(BreakThreadLoop() == 0)
		{
			break;
		}
		// Read temperature value from sensor
	    //int status = mmapTEC->ReadRegister_TEC32(LCOS_TEMP_ADDR/0x4, &hexValue);usleep(1000);
		int status = ReadTemperature(&m_direct_LCOS_Temp, Sensors::lCOS);

		if(status == ConversionStatus::BELOW_LUT_RANGE)
		{
			std::cout << "[Error] LCOS Temperature Read is below LUT range\n";
			continue;
		}

		//std::cout << "m_direct_LCOS_Temp = " << m_direct_LCOS_Temp << std::endl;
	    // Calculate current temperature estimate using EWMA filter
	    currentTempEst = alpha * m_direct_LCOS_Temp + (1 - alpha) * prevTempEst;

	    // Calculate temperature trend
	    tempTrend = currentTempEst - prevTempEst;

	    // Check if temperature is stable around target temperature
	    //if (currentTempEst >= (targetTemp - 0.5) && currentTempEst <= (targetTemp + 0.5) && abs(tempTrend) <= tolerance)
//	    if (abs(currentTempEst - m_direct_LCOS_Temp) < tolerance && abs(tempTrend) < trendTolerance
//	    		&& (currentTempEst >= targetTemp - 2.0) && (currentTempEst <= targetTemp + 2.0))
	    if (abs(currentTempEst - m_direct_LCOS_Temp) < tolerance && abs(tempTrend) < trendTolerance)
	    {
	        //tempStable = true;

	        // Verify Result:
	        //windowVerify[index] = currentTempEst;

	    	if(windowVerify.size() != 50)
	    	{
	    		windowVerify.push_back(currentTempEst);
	    	}
	    	else
	    	{
	    		double max = *std::max_element(std::begin(windowVerify), std::end(windowVerify));
	    		double min = *std::min_element(std::begin(windowVerify), std::end(windowVerify));

	    		double diff = max - min;

	            // Calculate the average rate of change of the temperature readings over the windowVerify array
	            double sum = 0;
	            for (unsigned int i = 1; i < windowVerify.size(); i++)
	            {
	                sum += abs(windowVerify[i] - windowVerify[i-1]);
	            }

	            double avgRateOfChange = sum / (windowVerify.size() - 1);

	            std::cerr << " " <<abs(avgRateOfChange - prevRateOfChange) << " DIFF =  " << diff << "\n";

	    		//if(diff < 0.09)
	            if (abs(avgRateOfChange - prevRateOfChange) < rateOfChangeTolerance  && diff < peakToPeak)
	    		{
					//std::cerr << "ARM Stable Signal = 1 --- ";
					if(track1 == 0)
					{
						stableSignalAtTemp_ARM = m_direct_LCOS_Temp;
						tempStable = true;		// break the loop once stable
					}

					track1++;
	    		}
	            else
	            {
	   	    	 //std::cerr << "ARM Stable Signal = 0 --- ";
	   	    	 stableSignalAtTemp_ARM = 0.0;
	   	    	 track1 = 0;
	            }

				prevRateOfChange = avgRateOfChange;
		        windowVerify.clear();
	    	}

	    }
	    else
	    {
	    	 prevRateOfChange = 0;

	    	 //std::cerr << "ARM Stable Signal = 0 --- ";
	    	 stableSignalAtTemp_ARM = 0.0;
	    	 track1 = 0;

	    	 windowVerify.clear();
	    }

	    bool FPGAstablecheck = isTECStableInFPGA();
	    if(FPGAstablecheck)
	    {
	    	if(track2 == 0)
	    		stableSignalAtTemp_FPGA = m_direct_LCOS_Temp;

	    	track2++;
	    	//std::cerr << "FPGA Stable Signal = 1 \n";
	    }
	    else
	    {
	    	//std::cerr << "FPGA Stable Signal = 0 \n";
	    	stableSignalAtTemp_FPGA = 0.0;
	    	track2 = 0;
	    }

	    // Update previous temperature estimate
	    prevTempEst = currentTempEst;

		if(Check_Need_For_TEC_Data_Transfer_To_PC())
		{
    		char buff[100];
			sprintf(buff, "\x24:%0.3f:%0.3f:%0.3f:%0.3f:%0.3f:\x23", currentTempEst, tempTrend, stableSignalAtTemp_FPGA, m_direct_LCOS_Temp, stableSignalAtTemp_ARM);
    		g_serialMod->Serial_WritePort(buff);
		}
	}

	return 0;
}

void TemperatureMonitor::StopThread()
{
	// Break loop
	TemperatureMonitor_Closure();

	// Wait for thread to exit normally
	if (thread_id != 0)
	{
		pthread_join(thread_id, NULL);
		thread_id = 0;
	}

	printf("Driver<TempMonitor> Thread terminated\n");

	if(pinstance_ != nullptr)
	{
		delete pinstance_;
		pinstance_ = nullptr;
	}
}

int TemperatureMonitor::ReadTemperature(double *temp, int sensor)
{
	int status{0};
	unsigned int hexValue = 0x0;

	switch(sensor)
	{
		case(Sensors::lCOS):
		{
			status |= mmapTEC->ReadRegister_TEC32(LCOS_TEMP_ADDR/0x4, &hexValue);usleep(1000);
			break;
		}
		case(Sensors::HEATER1):
		{
			status |= mmapTEC->ReadRegister_TEC32(HEATER1_TEMP_ADDR/0x4, &hexValue);usleep(1000);
			break;
		}
		case(Sensors::HEATER2):
		{
			status |= mmapTEC->ReadRegister_TEC32(HEATER2_TEMP_ADDR/0x4, &hexValue);usleep(1000);
			//std::cerr << " status = " << status << " hexValue : " << (hexValue & 0x0FFF) <<std::endl;
			/*if(PRIMARK % 10 == 0)
			{
			    printf("hexvalue 0x%x\n",(hexValue & 0x0FFF));
			}*/
			*temp = ConvertToOldCelsius(hexValue & 0x0FFF);
			
			//PRIMARK ++;
			//*temp = 0;
			return(ConversionStatus::NORMAL_LUT_RANGE);
			break;
		}
	}

	// hexValue is in format 0xABCD. We need to extract 0xBCD that is actual temperature value from ADC,
	// ignore 4th Byte.

	hexValue &= 0xFFF;
	//std::cerr << " temp hexValue = " << hexValue;

	// convert hex to celsius and test the range is within LUT range or not, otherwise fix at boundary values
	if(hexValue >= LUT_MIN_HEX && hexValue <= LUT_MAX_HEX)
	{
		*temp = ConvertToCelsius(hexValue);
		return(ConversionStatus::NORMAL_LUT_RANGE);
	}
	else if (hexValue < LUT_MIN_HEX)
	{
		*temp = ConvertToCelsius(LUT_MIN_HEX);
		return(ConversionStatus::BELOW_LUT_RANGE);
	}
	else if (hexValue > LUT_MAX_HEX)
	{
		*temp = ConvertToCelsius(LUT_MAX_HEX);
		return(ConversionStatus::ABOVE_LUT_RANGE);
	}



}

double TemperatureMonitor::ConvertToCelsius(unsigned int hexTemp)
{
	assert(hexTemp >= g_startHexValue && "****[Out of LUT range] Temperature reading in Hex is invalid****");

	unsigned int index = hexTemp - g_startHexValue;

	return(LUT[index]);
}


double TemperatureMonitor::ConvertToOldCelsius(unsigned int hexTemp)
{
	assert(hexTemp >= g_oldstartHexValue && "****[Out of LUT range] Temperature reading in Hex is invalid****");

	unsigned int index = hexTemp - g_oldstartHexValue;

	return(OLDLUT[index]);
}
int TemperatureMonitor::Load_OldTempSensor_LUT(void)
{
    std::ifstream file("/mnt/OLDTempSensorLUT.csv");

    if (file.is_open()) {
        std::cout << "[OLDTempSensor_LUT] File has been opened" << std::endl;
    }
    else {
        std::cout << "[OLDTempSensor_LUT] File opening Error" << std::endl;
        return (-1);
    }


    // The file reading is based on EXCEL/CSV LUT file template.

    std::string row;
    int rowNumber = 1;

    while (std::getline(file, row)) {			// Fetch one entire row

       std::istringstream iss(row);
       std::string column;

       int columnNumber = 1;
       int hexValue;
       float tempValue;

       while (std::getline(iss, column, ',')) 		// Within one row get each column
       {
           if(rowNumber == 3 && columnNumber == 1)
           {
               std::istringstream(column) >> std::hex >> hexValue;
               g_oldstartHexValue = hexValue;
           }
           else if (rowNumber >= 3 && columnNumber == 2)
           {
			   std::istringstream(column) >> tempValue;
			   //std::cout << column << std::endl;
			   OLDLUT.push_back(tempValue);
           }

           columnNumber++;
       }

       rowNumber++;
     }

    file.close();

#ifdef _DEBUG_
    // Verify values...

    printf("start value in hex: %x \n", g_startHexValue);
    for(unsigned int i =0; i < LUT.size(); i++)
    {
    	printf("%f \n", LUT[i]);
    }

#endif

    return (0);
}

int TemperatureMonitor::Load_TempSensor_LUT(void)
{
    std::ifstream file("/mnt/TempSensorLUT.csv");

    if (file.is_open()) {
        std::cout << "[TempSensor_LUT] File has been opened" << std::endl;
    }
    else {
        std::cout << "[TempSensor_LUT] File opening Error" << std::endl;
        return (-1);
    }


    // The file reading is based on EXCEL/CSV LUT file template.

    std::string row;
    int rowNumber = 1;

    while (std::getline(file, row)) {			// Fetch one entire row

       std::istringstream iss(row);
       std::string column;

       int columnNumber = 1;
       int hexValue;
       float tempValue;

       while (std::getline(iss, column, ',')) 		// Within one row get each column
       {
           if(rowNumber == 3 && columnNumber == 1)
           {
               std::istringstream(column) >> std::hex >> hexValue;
               g_startHexValue = hexValue;
           }
           else if (rowNumber >= 3 && columnNumber == 2)
           {
			   std::istringstream(column) >> tempValue;
			   //std::cout << column << std::endl;
			   LUT.push_back(tempValue);
           }

           columnNumber++;
       }

       rowNumber++;
     }

    file.close();

#ifdef _DEBUG_
    // Verify values...

    printf("start value in hex: %x \n", g_startHexValue);
    for(unsigned int i =0; i < LUT.size(); i++)
    {
    	printf("%f \n", LUT[i]);
    }

#endif

    return (0);
}

int TemperatureMonitor::Config_FPGA_TEC(void)
{
	// Divided by 0x4 to get Index for accessing 32bit Address. Currently direct address of register is given
	// we need to convert address to index for accessing 32 bit array of int.

	int status{0};

	// Set Period for both TECs
	status |= mmapTEC->WriteRegister_TEC32(TEC1_PERIOD_ADDR/0x4, 0xFF);usleep(1000);				// tec period set to 0xFF 390Hz
	status |= mmapTEC->WriteRegister_TEC32(TEC2_PERIOD_ADDR/0x4, 0xFF);usleep(1000);				// tec period set to 0xFF 390Hz

	// Active Heating on both heaters
	status |= SetHeaters(true);

	// Set the sampling time of ADDR
	status |= mmapTEC->WriteRegister_TEC32(SAMPLING_TIMER_ADDR/0x4, 0x001E);usleep(1000);			// ADDR_SAMPLING_TIMER 30ms 0x001E

	// Set Target Temperature
	status |= SetTargetTemp(LCOS_TEMP_TARGET);			//1770														// ADDR_PID1_TARGET Default 65.43c; 0x199C = 6556

	// Set PID1
	status |= SetPID1_Kp(KP_PID1_VAL);
	status |= SetPID1_Ki(KI_PID1_VAL);
	status |= SetPID1_Kd(KD_PID1_VAL);

	// Set PID2
	status |= SetPID2_Kp(KP_PID2_VAL);
	status |= SetPID2_Ki(KI_PID2_VAL);
	status |= SetPID2_Kd(KD_PID2_VAL);


	// Flush write operation, disable WEN - write enable signal in FPGA before any read operation
	// This is a bug in FPGA that afte write operation if you read, it can modify read register sometimes
	// So we forced write operation to stop, by reading at 0th address
	unsigned int readData{0};
	mmapTEC->ReadRegister_TEC32(0x0, &readData);usleep(1000);

#if 1

	mmapTEC->ReadRegister_TEC32(HEATER1_START_ADDR/0x4, &readData);usleep(1000);
	std::cout << "TEC1 Mode = " << readData <<std::endl;
	mmapTEC->ReadRegister_TEC32(HEATER2_START_ADDR/0x4, &readData);usleep(1000);
	std::cout << "TEC2 Mode = " << readData <<std::endl;

	mmapTEC->ReadRegister_TEC32(SAMPLING_TIMER_ADDR/0x4, &readData);usleep(1000);
	std::cout << "sampling time = " << readData <<std::endl;
	mmapTEC->ReadRegister_TEC32(TARGET_TEMP_ADDR/0x4, &readData);usleep(1000);
	//std::cout << "Target Temperature = " << readData <<std::endl;

	//FOR INTERRUPT ALARM LIMITATION
	mmapTEC->WriteRegister_TEC32(TEMP_INTERRUPT_GARTM/0x04,0x19C8);usleep(1000);
	mmapTEC->WriteRegister_TEC32(TEMP_INTERRUPT_GARTH/0x04,0x1B58);usleep(1000);
	mmapTEC->WriteRegister_TEC32(TEMP_INTERRUPT_LCOSM/0x04,0x19C8);usleep(1000);
	mmapTEC->WriteRegister_TEC32(TEMP_INTERRUPT_LCOSH/0x04,0x1B58);usleep(1000);

#endif

	return (status);
}

void TemperatureMonitor::TemperatureMonitor_Closure()
{
	if (pthread_mutex_lock(&global_mutex[LOCK_CLOSE_TEMPLOOP]) != 0)	// locking and checking the result, if lock was successful and no deadlock happened
		std::cout << "[569]global_mutex[LOCK_CLOSE_TEMPLOOP] lock unsuccessful" << std::endl;
	else
	{
		b_LoopOn = false;

		if (pthread_mutex_unlock(&global_mutex[LOCK_CLOSE_TEMPLOOP]) != 0)	// Unlocking and checking the result, if lock was successful and no deadlock happened
			std::cout << "global_mutex[LOCK_CLOSE_TEMPLOOP] unlock unsuccessful" << std::endl;
	}
}

int TemperatureMonitor::BreakThreadLoop(void)
{
	if (pthread_mutex_lock(&global_mutex[LOCK_CLOSE_TEMPLOOP]) != 0)	// locking and checking the result, if lock was successful and no deadlock happened
		std::cout << "[597]global_mutex[LOCK_CLOSE_TEMPLOOP] lock unsuccessful" << std::endl;
	else
	{
		if(!b_LoopOn)
		{
			if (pthread_mutex_unlock(&global_mutex[LOCK_CLOSE_TEMPLOOP]) != 0)	// Unlocking and checking the result, if lock was successful and no deadlock happened
				std::cout << "global_mutex[LOCK_CLOSE_TEMPLOOP] unlock unsuccessful" << std::endl;

			return (0);		// break
		}

		if (pthread_mutex_unlock(&global_mutex[LOCK_CLOSE_TEMPLOOP]) != 0)	// Unlocking and checking the result, if lock was successful and no deadlock happened
			std::cout << "global_mutex[LOCK_CLOSE_TEMPLOOP] unlock unsuccessful" << std::endl;
	}

	return (-1);			// dont break loop
}

double TemperatureMonitor::GetLCOSTemperature(void)
{
	//return (previousTemp_LCOS);		// is the the stable temperature
	return (previousTemp_GRID);
}

bool TemperatureMonitor::Check_Need_For_TEC_Data_Transfer_To_PC()
{
#ifdef _DEVELOPMENT_MODE_
	int status;

	if (pthread_mutex_lock(&global_mutex[LOCK_DEVMODE_VARS]) != 0)	// locking and checking the result, if lock was successful and no deadlock happened
		std::cout << "global_mutex[LOCK_DEVMODE_VARS] lock unsuccessful" << std::endl;
	else
	{
		if(g_serialMod->cmd_decoder.structDevelopMode.startSendingTECData == true)
		{
			status = true;
		}
		else
		{
			status = false;
		}

		if(g_serialMod->cmd_decoder.structDevelopMode.PIDSet)
		{

			if(g_serialMod->cmd_decoder.structDevelopMode.TEC_tv != 0)
			{
				SetTargetTemp(g_serialMod->cmd_decoder.structDevelopMode.TEC_tv);
				g_serialMod->cmd_decoder.structDevelopMode.TEC_tv = 0;
			}

			if(g_serialMod->cmd_decoder.structDevelopMode.TEC1_kp != 0)
			{
				SetPID1_Kp(g_serialMod->cmd_decoder.structDevelopMode.TEC1_kp);
				g_serialMod->cmd_decoder.structDevelopMode.TEC1_kp = 0;
			}

			if(g_serialMod->cmd_decoder.structDevelopMode.TEC1_ki != 0)
			{
				SetPID1_Ki(g_serialMod->cmd_decoder.structDevelopMode.TEC1_ki);
				g_serialMod->cmd_decoder.structDevelopMode.TEC1_ki = 0;
			}

			if(g_serialMod->cmd_decoder.structDevelopMode.TEC1_kd != 0)
			{
				SetPID1_Kd(g_serialMod->cmd_decoder.structDevelopMode.TEC1_kd);
				g_serialMod->cmd_decoder.structDevelopMode.TEC1_kd = 0;
			}

			if(g_serialMod->cmd_decoder.structDevelopMode.TEC2_kp != 0)
			{
				SetPID2_Kp(g_serialMod->cmd_decoder.structDevelopMode.TEC2_kp);
				g_serialMod->cmd_decoder.structDevelopMode.TEC2_kp = 0;
			}

			if(g_serialMod->cmd_decoder.structDevelopMode.TEC2_ki != 0)
			{
				SetPID2_Ki(g_serialMod->cmd_decoder.structDevelopMode.TEC2_ki);
				g_serialMod->cmd_decoder.structDevelopMode.TEC2_ki = 0;
			}

			if(g_serialMod->cmd_decoder.structDevelopMode.TEC2_kd != 0)
			{
				SetPID2_Kd(g_serialMod->cmd_decoder.structDevelopMode.TEC2_kd);
				g_serialMod->cmd_decoder.structDevelopMode.TEC2_kd = 0;
			}

			g_serialMod->cmd_decoder.structDevelopMode.PIDSet = false;
		}

			m_testRigTemperatureSwitch = g_serialMod->cmd_decoder.structDevelopMode.m_switch;

		if (pthread_mutex_unlock(&global_mutex[LOCK_DEVMODE_VARS]) != 0)	// Unlocking and checking the result, if lock was successful and no deadlock happened
			std::cout << "global_mutex[LOCK_DEVMODE_VARS] unlock unsuccessful" << std::endl;
	}

	return status;

#endif
}

inline int TemperatureMonitor::SetHeaters(bool state)
{
	int status{};

	int bits{0b00};

	if(state)
	{
		bits = 0b11;
	}

	status |= mmapTEC->WriteRegister_TEC32(HEATER1_START_ADDR/0x4, bits);usleep(1000);				// heat or cold ... 00 is close TEC1, 11 turn of TEC 1
	status |= mmapTEC->WriteRegister_TEC32(HEATER2_START_ADDR/0x4, bits);usleep(1000);				// heat or cold ... 00 is close TEC2, 11 turn of TEC 2

	return status;
}

inline int TemperatureMonitor::SetTargetTemp(unsigned int target)
{
	int status = mmapTEC->WriteRegister_TEC32(TARGET_TEMP_ADDR/0x4, target);usleep(1000);return status;			// ADDR_PID1_TARGET Default 65.43c; 0x199C = 6556
}

inline int TemperatureMonitor::SetPID1_Kp(unsigned int p)
{
	int status = mmapTEC->WriteRegister_TEC32(KP_PID1_ADDR/0x4, p);usleep(1000);return status;			// ADDR_PID1_KP 4.0:400 = 0x0190
}
inline int TemperatureMonitor::SetPID1_Ki(unsigned int i)
{
	int status = mmapTEC->WriteRegister_TEC32(KI_PID1_ADDR/0x4, i);usleep(1000);return status;			// ADDR_PID1_KI 0.03:3
}
inline int TemperatureMonitor::SetPID1_Kd(unsigned int d)
{
	int status = mmapTEC->WriteRegister_TEC32(KD_PID1_ADDR/0x4, d);usleep(1000);return status;			// ADDR_PID1_KD 3.0:300 = 0x12c
}
inline int TemperatureMonitor::SetPID2_Kp(unsigned int p)
{
	int status = mmapTEC->WriteRegister_TEC32(KP_PID2_ADDR/0x4, p);usleep(1000);return status;			// ADDR_PID2_KP 21.7:2170 = 0x87A
}
inline int TemperatureMonitor::SetPID2_Ki(unsigned int i)
{
	int status = mmapTEC->WriteRegister_TEC32(KI_PID2_ADDR/0x4, i);usleep(1000);return status;			// ADDR_PID2_KI 0.01:1
}
inline int TemperatureMonitor::SetPID2_Kd(unsigned int d)
{
	int status = mmapTEC->WriteRegister_TEC32(KD_PID2_ADDR/0x4, d);usleep(1000);return status;			// ADDR_PID2_KD 4:400 = 0x0190
}

float TemperatureMonitor::CpuTemp(void)
{
	if(!m_bFilesOk)
		return 0.0;

	// Print CPU Temperature All the Time	// Source : https://parallella.org/forums/viewtopic.php?f=23&t=930
	std::fstream in_temp0_rawFD("/sys/bus/iio/devices/iio:device0/in_temp0_raw", std::ios::in);
	double cpu_Temp{0.0};
	std::string tmp_data{};

	if(!in_temp0_rawFD.is_open())
	{
		std::cout << "CPU temp file 'in_temp0_raw' can't open!" << std::endl;
	}
	else
	{
		in_temp0_rawFD >> tmp_data;
		double in_temp0_raw = stof(tmp_data);

		cpu_Temp = (((in_temp0_raw + in_temp0_offset)*in_temp0_scale)/1000);
	}

	in_temp0_rawFD.close();

	return cpu_Temp;
}

void TemperatureMonitor::GetZYNQTempVars()
{
	//CPU TEMP
	std::fstream in_temp0_offsetFD("/sys/bus/iio/devices/iio:device0/in_temp0_offset", std::ios::in);
	std::fstream in_temp0_scaleFD("/sys/bus/iio/devices/iio:device0/in_temp0_scale", std::ios::in);

	std::string tmp_data{};

	if (!(in_temp0_offsetFD.is_open() ||in_temp0_scaleFD.is_open())) {
		std::cout << "CPU temp file can't open!" << std::endl;
		m_bFilesOk = false;
	}
	else
	{
		in_temp0_offsetFD >> tmp_data;
		in_temp0_offset = std::stof(tmp_data);

		in_temp0_scaleFD >> tmp_data;
		in_temp0_scale = std::stof(tmp_data);
	}

	m_bFilesOk = true;

	in_temp0_scaleFD.close();
	in_temp0_offsetFD.close();
}
