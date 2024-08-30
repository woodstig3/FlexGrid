/*
 * PatternCalibModule.cpp
 *
 *  Created on: Feb 3, 2023
 *      Author: mib_n
 */

#include <unistd.h>

#include "GlobalVariables.h"
#include "PatternCalibModule.h"

/*
 * Pattern Calibration Module
 *
 * This Module creates 4 different threads and assign
 * different interpolation for different parameters i.e.
 * Sigma, Aopt,Kopt,Aatt,Katt,PixelPosition. The four
 * threads are supposed to work independently and perform
 * calculations without delaying any other threads.
 *
 * NOTE: Currently, as of 2023-02-23, the four threads are
 * sharing global_mutex[lOCK_CALIB_PARAMS] for each ready
 * signal and for each struct parameter setting. This sharing
 * of a single mutex across all may synchronize threads to work
 * in sequential manner which will completely destroy the
 * purpose of parallel calculation.
 *
 * SOLUTION: Have 4 different mutex for each ready signal
 * which is used to set each struct parameter settings also.
 * 1 shared mutex can be used on Binary search function, though
 * it will sequentialize the process a little but its minor
 * impact.
 *
 */
PatternCalibModule *PatternCalibModule::pinstance_{nullptr};

PatternCalibModule::PatternCalibModule()
{
	g_serialMod = SerialModule::GetInstance();

	int status = PatternCalib_Initialize();

	if(status != 0)
	{
		printf("Driver<PATTERN_CALIB>: Pattern Calib. Module Initialization Failed.\n");
		PatternCalib_Closure();
	}

	status = PatternCalib_LoadLUTs();

	if(status != 0)
	{
		printf("Driver<PATTERN_CALIB>: Pattern Calib. Module Load LUTs Failed.\n");
		g_serialMod->Serial_WritePort("\01INTERNAL_ERROR\04\n");
		m_bCalibDataOk = false;
	}
	else
	{
		m_bCalibDataOk = true;
	}


}

PatternCalibModule::~PatternCalibModule()
{

}

PatternCalibModule *PatternCalibModule::GetInstance()
{
	if (pthread_mutex_lock(&global_mutex[LOCK_CALIB_INSTANCE]) != 0)	// locking and checking the result, if lock was successful and no deadlock happened
		std::cout << "global_mutex[LOCK_CALIB_INSTANCE] lock unsuccessful" << std::endl;
	else
	{
	    if (pinstance_ == nullptr)
	    {
	        pinstance_ = new PatternCalibModule();
	    }

		if (pthread_mutex_unlock(&global_mutex[LOCK_CALIB_INSTANCE]) != 0)	// Unlocking and checking the result, if lock was successful and no deadlock happened
			std::cout << "global_mutex[LOCK_CALIB_INSTANCE] unlock unsuccessful" << std::endl;
	}

	return pinstance_;
}

int PatternCalibModule::MoveToThread()
{
//	PatternCalibModule *patternCalibModule_Instance = new PatternCalibModule();

	if (thread1_id == 0 && thread2_id == 0 && thread3_id == 0 && thread4_id == 0)
	{
		int status;

		status = pthread_create(&thread1_id, &thread_attrb, ThreadHandle1, (void*) this);
		status |= pthread_create(&thread2_id, &thread_attrb, ThreadHandle2, (void*) this);
		status |= pthread_create(&thread3_id, &thread_attrb, ThreadHandle3, (void*) this);
		status |= pthread_create(&thread4_id, &thread_attrb, ThreadHandle4, (void*) this);


		if (status != 0) // 'this' is passed to pointer, so pointer dies as function dies
		{
			printf("Driver<PATTERN_CALIB>: threads create fail %d.\n", status);
			return (-1);
		}
		else
		{
			printf("Driver<PATTERN_CALIB>: threads create OK.\n");
		}
	}
	else
	{
		printf("Driver<PATTERN_CALIB>: thread_id already exist.\n");
		return (-1);
	}

	return (0);
}

void *PatternCalibModule::ThreadHandle1(void *args)
{
	PatternCalibModule* recvPtr = static_cast<PatternCalibModule*>(args);

	recvPtr->Calculation_Aopt_Kopt();

	return (NULL);
}

void *PatternCalibModule::ThreadHandle2(void *args)
{
	PatternCalibModule* recvPtr = static_cast<PatternCalibModule*>(args);

	recvPtr->Calculation_Aatt_Katt();

	return (NULL);
}

void *PatternCalibModule::ThreadHandle3(void *args)
{
	PatternCalibModule* recvPtr = static_cast<PatternCalibModule*>(args);

	recvPtr->Calculation_Sigma();

	return (NULL);
}

void *PatternCalibModule::ThreadHandle4(void *args)
{
	PatternCalibModule* recvPtr = static_cast<PatternCalibModule*>(args);

	recvPtr->Calculation_Pixel_Shift();

	return (NULL);
}

void PatternCalibModule::Set_Current_Module(int moduleNo)
{
	if(moduleNo == 1){
		lut_Opt = &M1_lut_Opt;
		lut_Att = &M1_lut_Att;
		lut_Sigma = &M1_lut_Sigma;
		lut_SigmaL = &M1_lut_SigmaL;
		lut_PixelPos = &M1_lut_PixelPos;
	}else{
		lut_Opt = &M2_lut_Opt;
		lut_Att = &M2_lut_Att;
		lut_Sigma = &M2_lut_Sigma;
		lut_SigmaL = &M2_lut_SigmaL;
		lut_PixelPos = &M2_lut_PixelPos;
	}
}

int PatternCalibModule::Set_Aopt_Kopt_Args(int port, double freq)
{
	Aopt_Kopt_params.port = port;
	Aopt_Kopt_params.freq = freq;

	return (0);
}
int PatternCalibModule::Set_Aatt_Katt_Args(int port, double freq, double ATT)
{
	Aatt_Katt_params.port = port;
	Aatt_Katt_params.freq = freq;
	Aatt_Katt_params.ATT = ATT;

	return (0);
}
int PatternCalibModule::Set_Sigma_Args(int port, double freq, double temperature, int cmp)
{
	Sigma_params.port = port;
	Sigma_params.freq = freq;
	Sigma_params.temperature = temperature;
	Sigma_params.cmp = cmp;

	return (0);
}
int PatternCalibModule::Set_Pixel_Pos_Args(double f1, double f2, double fc, double temperature)
{
	// mutex lock

	Pixel_Pos_params.f1 = f1;
	Pixel_Pos_params.f2 = f2;
	Pixel_Pos_params.fc = fc;
	Pixel_Pos_params.temperature = temperature;

	return (0);
}

void PatternCalibModule::Calculation_Aopt_Kopt()
{
	while(true) {

		if(BreakThreadLoops() == 0)
		{
			break;
		}

		usleep(100);

		//std::cout << "Calculation_Aopt_Kopt LOOPING.." << std::endl;
		if (pthread_mutex_lock(&global_mutex[lOCK_CALIB_PARAMS]) != 0)	// locking and checking the result, if lock was successful and no deadlock happened
			std::cout << "global_mutex[LOCK_CALIB_PARAMS] lock unsuccessful" << std::endl;
		else
		{
	        while (!g_ready1) {
	        	//std::cout << "Calculation_Aopt_Kopt THREAD IS WAITING..." << std::endl;
	            pthread_cond_wait(&cond, &global_mutex[lOCK_CALIB_PARAMS]);
	        }

	        if(b_LoopOn && m_bCalibDataOk)	// if and only if the main loop is running we will perform calculations
	        {
		        // Perform calculations
		        //std::cout << "Optimization Thread Performing Interpolations..." << std::endl;
		        int status = Interpolate_Aopt_Kopt_Linear(Aopt_Kopt_params.freq, Aopt_Kopt_params.port, Aopt_Kopt_params.result_Aopt, Aopt_Kopt_params.result_Kopt);

		        if(status != 0)
		        	g_Status_Opt = ERROR;
		        else
		        {
		        	g_Status_Opt = SUCCESS;
		        }

		        g_ready1 = false;
	        }

	        pthread_cond_signal(&cond_result_ready);

			if (pthread_mutex_unlock(&global_mutex[lOCK_CALIB_PARAMS]) != 0)	// Unlocking and checking the result, if lock was successful and no deadlock happened
				std::cout << "global_mutex[LOCK_CALIB_PARAMS] unlock unsuccessful" << std::endl;
		}
	}

	pthread_exit(NULL);
}

void PatternCalibModule::Calculation_Aatt_Katt()
{
	while(true) {

		if(BreakThreadLoops() == 0)
		{
			break;
		}

		usleep(100);

		//std::cout << "Calculation_Aatt_Katt LOOPING.." << std::endl;
		if (pthread_mutex_lock(&global_mutex[lOCK_CALIB_PARAMS]) != 0)	// locking and checking the result, if lock was successful and no deadlock happened
			std::cout << "global_mutex[LOCK_CALIB_PARAMS] lock unsuccessful" << std::endl;
		else
		{
			while (!g_ready2) {
				//std::cout << "Calculation_Aatt_Katt THREAD IS WAITING..." << std::endl;
				pthread_cond_wait(&cond, &global_mutex[lOCK_CALIB_PARAMS]);
			}

	        if(b_LoopOn && m_bCalibDataOk)	// if and only if the main loop is running we will perform calculations
	        {
				// Perform calculations
				//std::cout << "Attenuation Thread Performing Interpolations..." << std::endl;
				int status = Interpolate_Aatt_Katt_Bilinear(Aatt_Katt_params.freq, Aatt_Katt_params.port,Aatt_Katt_params.ATT, Aatt_Katt_params.result_Aatt, Aatt_Katt_params.result_Katt);

		        if(status != 0)
		        	g_Status_Att = ERROR;
		        else
		        {
		        	g_Status_Att = SUCCESS;
		        }

				g_ready2 = false;
	        }

			pthread_cond_signal(&cond_result_ready);

			if (pthread_mutex_unlock(&global_mutex[lOCK_CALIB_PARAMS]) != 0)	// Unlocking and checking the result, if lock was successful and no deadlock happened
				std::cout << "global_mutex[LOCK_CALIB_PARAMS] unlock unsuccessful" << std::endl;
		}
	}

	pthread_exit(NULL);
}

void PatternCalibModule::Calculation_Sigma()
{
	while(true) {

		if(BreakThreadLoops() == 0)
		{
			break;
		}

		usleep(100);

		//std::cout << "Calculation_Sigma LOOPING.." << std::endl;
		if (pthread_mutex_lock(&global_mutex[lOCK_CALIB_PARAMS]) != 0)	// locking and checking the result, if lock was successful and no deadlock happened
			std::cout << "global_mutex[LOCK_CALIB_PARAMS] lock unsuccessful" << std::endl;
		else
		{
	        while (!g_ready3) {
	        	//std::cout << "Calculation_Sigma THREAD IS WAITING..." << std::endl;
	            pthread_cond_wait(&cond, &global_mutex[lOCK_CALIB_PARAMS]);
	        }

	        if(b_LoopOn && m_bCalibDataOk)	// if and only if the main loop is running we will perform calculations
	        {
		        // Perform calculations
		        //std::cout << "Sigma Thread Performing Interpolations..." << std::endl;
		        int status = Interpolate_Sigma_Bilinear(Sigma_params.temperature, Sigma_params.freq, Sigma_params.port, Sigma_params.cmp, Sigma_params.result_Sigma);


		        if(status != 0)
		        	g_Status_Sigma = ERROR;
		        else
		        {
		        	g_Status_Sigma = SUCCESS;
		        }

		        g_ready3 = false;
	        }

	        pthread_cond_signal(&cond_result_ready);

			if (pthread_mutex_unlock(&global_mutex[lOCK_CALIB_PARAMS]) != 0)	// Unlocking and checking the result, if lock was successful and no deadlock happened
				std::cout << "global_mutex[LOCK_CALIB_PARAMS] unlock unsuccessful" << std::endl;
		}
	}

	pthread_exit(NULL);
}

void PatternCalibModule::Calculation_Pixel_Shift()
{
	while(true) {

		if(BreakThreadLoops() == 0)
		{
			break;
		}

		usleep(100);

		//std::cout << "Calculation_Sigma LOOPING.." << std::endl;
		if (pthread_mutex_lock(&global_mutex[lOCK_CALIB_PARAMS]) != 0)	// locking and checking the result, if lock was successful and no deadlock happened
			std::cout << "global_mutex[LOCK_CALIB_PARAMS] lock unsuccessful" << std::endl;
		else
		{
	        while (!g_ready4) {
	        	//std::cout << "Calculation_Sigma THREAD IS WAITING..." << std::endl;
	            pthread_cond_wait(&cond, &global_mutex[lOCK_CALIB_PARAMS]);
	        }

	        if(b_LoopOn && m_bCalibDataOk)	// if and only if the main loop is running we will perform calculations
	        {
		        // Perform calculations
	        	//  1- Check if FC is within range of LUT by binary search on LUT
	        	//  2- If FC is within range then perform F1,F2 interpolation
	        	//  3- If FC is within range but F1 or F2 are out of range then find FC pixel position and of F1 or F2.
	        	// OR if both F1 F2 are out of range but FC is within range then

		        //std::cout << "Sigma Thread Performing Interpolations..." << std::endl;
		        int status = Interpolate_PixelPos_Bilinear(Pixel_Pos_params.temperature, Pixel_Pos_params.f1, Pixel_Pos_params.result_F1_PixelPos);

		        if(status != 0)
		        {
		        	// ERROR MESSAGE
		        	Pixel_Pos_params.result_F1_PixelPos = 0;	// F1 not found means F1 is out of range
		        	std::cout << "F2 out of range" << std::endl;
		        }

		        status = Interpolate_PixelPos_Bilinear(Pixel_Pos_params.temperature, Pixel_Pos_params.f2, Pixel_Pos_params.result_F2_PixelPos);

		        if(status != 0)
		        {
		        	// ERROR MESSAGE
		        	std::cout << "F2 out of range" << std::endl;
		        	Pixel_Pos_params.result_F2_PixelPos = 1919;	// F2 not found means F2 is out of range
		        }

		        if(status != 0)
		        {
		        	// In either case of F1/F2 is not found we find FC.
		        	status = Interpolate_PixelPos_Bilinear(Pixel_Pos_params.temperature, Pixel_Pos_params.fc, Pixel_Pos_params.result_FC_PixelPos);

		        	if(status != 0)
		        		std::cout << "FC out of range" << std::endl;
		        }

		        if(status != 0)
		        	g_Status_PixelPos = ERROR;
		        else
		        {
		        	g_Status_PixelPos = SUCCESS;
		        }

		        g_ready4 = false;
	        }

	        pthread_cond_signal(&cond_result_ready);

			if (pthread_mutex_unlock(&global_mutex[lOCK_CALIB_PARAMS]) != 0)	// Unlocking and checking the result, if lock was successful and no deadlock happened
				std::cout << "global_mutex[LOCK_CALIB_PARAMS] unlock unsuccessful" << std::endl;
		}
	}

	pthread_exit(NULL);
}

int PatternCalibModule::Interpolate_Aatt_Katt_Bilinear(double frequency, int port, double Attenuation, double &result_Aatt, double &result_Katt)
{
	// For Aatt
	result_Aatt = lut_Att->Aatt[port];

	// For Katt
	int freqLow;
	int freqHigh;
	int attLow;
	int attHigh;

	int error1 = BinarySearch_LowIndex(lut_Att->Freq, LUT_ATT_FREQ_NUM, frequency, freqLow);
	int error2 = BinarySearch_LowIndex(lut_Att->ATT, LUT_ATT_ATT_NUM, Attenuation, attLow);

	if(error1 == -1 || error2 == -1)
	{
		std::cout << "[ERROR] Calibration File Format is Modified <ATT>" << std::endl;
		return (-1);
	}

	freqHigh = freqLow + 1;
	attHigh = attLow + 1;

	double a1 = lut_Att->ATT[attLow];
	double a2 = lut_Att->ATT[attHigh];
	double f1 = lut_Att->Freq[freqLow];
	double f2 = lut_Att->Freq[freqHigh];

	//std::cout << "a1 " << a1 << " a2 " << a2 << " f1 " << f1 <<  "f2 " << f2 << std::endl;

	double wT1 = (a2 - Attenuation) / (a2 - a1);
	double wT2 = (Attenuation - a1) / (a2 - a1);
	double wF1 = (f2 - frequency) / (f2 - f1);
	double wF2 = (frequency - f1) / (f2 - f1);

	double v11 = lut_Att->Katt[port][freqLow][attLow];
	double v12 = lut_Att->Katt[port][freqHigh][attLow];
	double v21 = lut_Att->Katt[port][freqLow][attHigh];
	double v22 = lut_Att->Katt[port][freqHigh][attHigh];

	// Two interpolation along x-axis (temperature)
	double R1 = wT1*v11 + wT2*v21;
	double R2 = wT1*v12 + wT2*v22;

	// One interpolation along y-axis (Frequency)

	result_Katt = R1*wF1 + R2*wF2;

	return (0);
}

int PatternCalibModule::Interpolate_Aopt_Kopt_Linear(double frequency, int port, double &result_Aopt, double &result_Kopt)
{	//std::cout << "freq " << frequency << "port " << port << std::endl;
    int freqLow;		// low freq index
	int freqHigh;		// high freq index

	int error = BinarySearch_LowIndex(lut_Opt->Freq, LUT_OPT_FREQ_NUM, frequency, freqLow);

	if(error == -1)
	{
		std::cout << "[ERROR] Calibration File Format is Modified <OPT>" << std::endl;
		return (-1);
	}

	freqHigh = freqLow + 1;

	// Perform Linear interpolation for Aopt first
	double x1 = lut_Opt->Freq[freqLow];				// x-axis is independent variable which is frequency
	double x2 = lut_Opt->Freq[freqHigh];			// x-axis is independent variable which is frequency
	double y1 = lut_Opt->Aopt[port][freqLow];
	double y2 = lut_Opt->Aopt[port][freqHigh];

	double slope = (y2-y1)/(x2-x1);

	result_Aopt =  y1 + (frequency-x1)*slope;


	// Perform Linear interpolation for Kopt first
	y1 = lut_Opt->Kopt[port][freqLow];
	y2 = lut_Opt->Kopt[port][freqHigh];

	slope = (y2-y1)/(x2-x1);

	result_Kopt = y1 + (frequency-x1)*slope;

	//std::cout << "Aopt = " << result_Aopt << "Kopt = " << result_Kopt << std::endl;

	return (0);

}

int PatternCalibModule::Interpolate_Sigma_Bilinear(double temperature, double frequency, unsigned int portNum, int cmp, double &result_sigma)
{	//std::cout << "freq " << frequency << "port " << portNum << "temperature  "<< temperature << std::endl;
    int freqLow;		// low freq index
	int freqHigh;		// high freq index
	int tempLow;		// low temperature index
	int tempHigh;		// high temperature index
	int error1 = 0, error2 = 0;

	double t1,t2,f1,f2,v11,v12,v21,v22;

	if(cmp == 1) // C port
	{
		error1 = BinarySearch_LowIndex(lut_Sigma->Freq, LUT_SIGMA_FREQ_NUM, frequency, freqLow);
		error2 = BinarySearch_LowIndex(lut_Sigma->Temp, LUT_SIGMA_TEMP_NUM, temperature, tempLow);
	}
	else if(cmp == 2) // L port
	{
		error1 = BinarySearch_LowIndex(lut_SigmaL->Freq, LUT_SIGMA_FREQ_NUM, frequency, freqLow);
		error2 = BinarySearch_LowIndex(lut_SigmaL->Temp, LUT_SIGMA_TEMP_NUM, temperature, tempLow);
	}
	else
	{
		return(-1);
	}

	if(error1 == -1 || error2 == -1)
	{
		std::cout << "[ERROR] Calibration File Format is Modified <Sigma>" << std::endl;
		return (-1);
	}

    freqHigh = freqLow + 1;
    tempHigh = tempLow + 1;

	if(cmp == 1)
	{
		t1 = lut_Sigma->Temp[tempLow];
	    t2 = lut_Sigma->Temp[tempHigh];
	    f1 = lut_Sigma->Freq[freqLow];
	    f2 = lut_Sigma->Freq[freqHigh];

	    v11 = lut_Sigma->Sigma[portNum][freqLow][tempLow];
		v12 = lut_Sigma->Sigma[portNum][freqHigh][tempLow];
		v21 = lut_Sigma->Sigma[portNum][freqLow][tempHigh];
		v22 = lut_Sigma->Sigma[portNum][freqHigh][tempHigh];
	}
	else if(cmp == 2)
	{
		t1 = lut_SigmaL->Temp[tempLow];
		t2 = lut_SigmaL->Temp[tempHigh];
		f1 = lut_SigmaL->Freq[freqLow];
		f2 = lut_SigmaL->Freq[freqHigh];

		v11 = lut_SigmaL->Sigma[portNum][freqLow][tempLow];
		v12 = lut_SigmaL->Sigma[portNum][freqHigh][tempLow];
		v21 = lut_SigmaL->Sigma[portNum][freqLow][tempHigh];
		v22 = lut_SigmaL->Sigma[portNum][freqHigh][tempHigh];

	}
	else
	{
		return(-1);
	}//std::cout << "t1 " << t1 << " t2 " << t2 << " f1 " << f1 <<  "f2 " << f2 << std::endl;

	double wT1 = (t2 - temperature) / (t2 - t1);
	double wT2 = (temperature - t1) / (t2 - t1);
	double wF1 = (f2 - frequency) / (f2 - f1);
	double wF2 = (frequency - f1) / (f2 - f1);

	// Two interpolation along x-axis (temperature)
	double R1 = wT1*v11 + wT2*v21;
	double R2 = wT1*v12 + wT2*v22;

	// One interpolation along y-axis (Frequency)

	result_sigma = R1*wF1 + R2*wF2;

	//std::cout << "sigma = " << result_sigma << std::endl;

	return(0);
}

int PatternCalibModule::Interpolate_PixelPos_Bilinear(double temperature, double frequency, double& result_pixelPos)
{	//std::cout << "freq " << frequency  << "temperature  "<< temperature << std::endl;
	int freqLow;		// low freq index
	int freqHigh;		// high freq index
	int tempLow;		// low temperature index
	int tempHigh;		// high temperature index

	int error1 = BinarySearch_LowIndex(lut_PixelPos->Freq, LUT_PIXELPOS_FREQ_NUM, frequency, freqLow);

	int error2 = BinarySearch_LowIndex(lut_PixelPos->Temp, LUT_PIXELPOS_TEMP_NUM, temperature, tempLow);

	if(error1 == -1 || error2 == -1)
	{
		std::cout << "[ERROR] Calibration File Format is Modified <PixelPos>" << std::endl;
		return (-1);
	}

		freqHigh = freqLow + 1;
		tempHigh = tempLow + 1;

		double t1 = lut_PixelPos->Temp[tempLow];
		double t2 = lut_PixelPos->Temp[tempHigh];
		double f1 = lut_PixelPos->Freq[freqLow];
		double f2 = lut_PixelPos->Freq[freqHigh];

		double wT1 = (t2 - temperature) / (t2 - t1);
		double wT2 = (temperature - t1) / (t2 - t1);
		double wF1 = (f2 - frequency) / (f2 - f1);
		double wF2 = (frequency - f1) / (f2 - f1);

		double v11 = lut_PixelPos->Pos[freqLow][tempLow];
		double v12 = lut_PixelPos->Pos[freqHigh][tempLow];
		double v21 = lut_PixelPos->Pos[freqLow][tempHigh];
		double v22 = lut_PixelPos->Pos[freqHigh][tempHigh];

		// Two interpolation along x-axis (temperature)
		double R1 = wT1*v11 + wT2*v21;
		double R2 = wT1*v12 + wT2*v22;

		// One interpolation along y-axis (Frequency)

		result_pixelPos = R1*wF1 + R2*wF2;

		//std::cout << "sigma = " << result_sigma << std::endl;

		return(0);
}

int PatternCalibModule::BinarySearch_LowIndex(const double *array, int size, double target, int &index)
{
    int low = 0;
    int high = size -1;

    while(low <= high)
    {
        int mid = low + (high - low) / 2;

        if(array[mid] < target)
        {
            low = mid + 1;
        }
        else
        {
            high = mid - 1;
        }

    }

    if(low > 0)
    {
        low -= 1;
    }

    if(target < array[0] || target > array[size-1])		//  if target is less than first element or target is greater than last element
    {
        index = -1;
        return -1;  // error, low can't be the last index number of array
    }

    index = low;

    return 0;
}

int PatternCalibModule::PatternCalib_Initialize(void)
{
	b_LoopOn = true;												// Loop running on threads

	pthread_attr_init(&thread_attrb);									// Default initialize thread attributes

	return (0);
}

void PatternCalibModule::PatternCalib_Closure(void)
{
	if (pthread_mutex_lock(&global_mutex[LOCK_CLOSE_CALIBLOOPS]) != 0)	// locking and checking the result, if lock was successful and no deadlock happened
		std::cout << "global_mutex[LOCK_CLOSE_CALIBLOOPS] lock unsuccessful" << std::endl;
	else
	{
		b_LoopOn = false;

		if (pthread_mutex_unlock(&global_mutex[LOCK_CLOSE_CALIBLOOPS]) != 0)	// Unlocking and checking the result, if lock was successful and no deadlock happened
			std::cout << "global_mutex[LOCK_CLOSE_CALIBLOOPS] unlock unsuccessful" << std::endl;
	}


	// Stop threads from waiting on conditional variables

	if (pthread_mutex_lock(&global_mutex[lOCK_CALIB_PARAMS]) != 0)	// locking and checking the result, if lock was successful and no deadlock happened
		std::cout << "global_mutex[LOCK_CALIB_PARAMS] lock unsuccessful" << std::endl;
	else
	{
		g_ready1 = true;					// Close the second while loop
		g_ready2 = true;					// Close the second while loop
		g_ready3 = true;					// Close the second while loop
		g_ready4 = true;					// Close the second while loop

		pthread_cond_broadcast(&cond);		// Broadcast here again to release from wait condition

		if (pthread_mutex_unlock(&global_mutex[lOCK_CALIB_PARAMS]) != 0)	// Unlocking and checking the result, if lock was successful and no deadlock happened
			std::cout << "global_mutex[LOCK_CALIB_PARAMS] unlock unsuccessful" << std::endl;
	}

    // Clean up dynamically allocated instance
   // delete patternCalibModule_Instance;
}

int PatternCalibModule::PatternCalib_LoadLUTs(void)
{
	int status{0};
	status |= Load_Opt_LUT(M1_lut_Opt, "/mnt/Opt_LUT_M1.csv");
	status |= Load_Att_LUT(M1_lut_Att, "/mnt/Att_LUT_M1.csv");
	status |= Load_Sigma_LUT(M1_lut_Sigma, "/mnt/Sigma_LUT_M1.csv");
	status |= Load_Sigma_LUT(M1_lut_SigmaL, "/mnt/SigmaL_LUT_M1.csv");  //drc added for L port calibration
	status |= Load_PixelPos_LUT(M1_lut_PixelPos, "/mnt/PixelPos_LUT_M1.csv");

#ifdef _TWIN_WSS_
	status |= Load_Opt_LUT(M2_lut_Opt, "/mnt/Opt_LUT_M2.csv");
	status |= Load_Att_LUT(M2_lut_Att, "/mnt/Att_LUT_M2.csv");
	status |= Load_Sigma_LUT(M2_lut_Sigma, "/mnt/Sigma_LUT_M2.csv");
	status |= Load_Sigma_LUT(M2_lut_SigmaL, "/mnt/SigmaL_LUT_M2.csv");  //drc added for L port calibration
	status |= Load_PixelPos_LUT(M2_lut_PixelPos, "/mnt/PixelPos_LUT_M2.csv");
#endif

	if(status != 0)
	{
		return (-1);
	}

	return (0);
}

int PatternCalibModule::Load_Opt_LUT(Opt& lut, const std::string& path)
{
    std::ifstream file(path);

    if (file.is_open()) {
        std::cout << "[Opt_LUT] File has been opened" << std::endl;
    }
    else {
        std::cout << "[Opt_LUT] File opening Error" << std::endl;
        return (-1);
    }


    // The file reading is based on EXCEL/CSV LUT file template.

    std::string row;
    int rowNumber = 1;
    int freqIndex = 0;

    while (std::getline(file, row)) {			// Fetch one entire row

       std::istringstream iss(row);
       std::string column;


       int columnNumber = 1;
       int portIndex = 0;
       double value;

       while (std::getline(iss, column, ',')) {			// Within one row get each column

           if(rowNumber >= 4 && columnNumber == 1)
           {
               std::istringstream(column) >> value;
               lut.Freq[freqIndex] = value;
           }
           else if (rowNumber >= 4 && columnNumber > 1)
           {
               if(!(columnNumber%2))        // even coulmn number is a
               {
                   std::istringstream(column) >> value;
                   lut.Aopt[portIndex][freqIndex] = value;
               }
               else                      // odd coulmn number is k
               {
                   std::istringstream(column) >> value;
                   lut.Kopt[portIndex][freqIndex] = value;

                   portIndex++;
               }
           }
           columnNumber++;
       }

       if(rowNumber >= 4)
       {
           ++freqIndex;
       }

       rowNumber++;
     }

    file.close();

#ifdef _DEBUG_
    // Verify values...

    for(int f=0; f< LUT_OPT_FREQ_NUM; f++)
    {
    	std::cout << "f = " << lut.Freq[f] << std::endl;
    }

    for(int port =0; port < 23; port++)
    {
    	std::cout << "port = " << port+1 << std::endl;

    	for(int index =0; index<LUT_OPT_FREQ_NUM; index++)
    	{
    		std::cout << "Aopt = " << lut.Aopt[port][index] << "Kopt = "<< lut.Kopt[port][index] << std::endl;
    	}

    	std::cout << std::endl;
    }
#endif
	return (0);
}

int PatternCalibModule::Load_Att_LUT(Att& lut, const std::string& path)
{
    std::ifstream file(path);

    if (file.is_open()) {
        std::cout << "[Att_LUT] File has been opened" << std::endl;
    }
    else {
        std::cout << "[Att_LUT] File opening Error" << std::endl;
        return (-1);
    }

    int portIndex = 0;
    int attIndex = 0;
    std::string line;
    int rowNumber = 1;


    while (std::getline(file, line)) {

      std::istringstream iss(line);
      std::string column;

      int columnNumber = 1;
      int freqIndex = 0;
      double value;

      while (std::getline(iss, column, ',')) {

          if(rowNumber == 3 && columnNumber == 2)               // Fetch value of Aatt
          {
              std::istringstream(column) >> value;
              lut.Aatt[portIndex] = value;
          }
          else if(rowNumber >= 6 && columnNumber == 1)
          {
              std::istringstream(column) >> value;
              lut.ATT[attIndex] = value;
          }
          else if (rowNumber == 4 && columnNumber > 1 && portIndex == 0)           // Since Freqs are same for each port, we load it onces only
          {
              std::istringstream(column) >> value;
              lut.Freq[freqIndex] = value;
              ++freqIndex;
          }
          else if (rowNumber >= 6 && columnNumber > 1)
          {
              std::istringstream(column) >> value;
              lut.Katt[portIndex][freqIndex][attIndex] = value;
              ++freqIndex;
          }

          ++columnNumber;
      }

      ++rowNumber;

      if(rowNumber > 6)
      {
          ++attIndex;
      }

      if(rowNumber > 9)
      {
          ++portIndex;
          rowNumber = 1;
          attIndex = 0;
      }
    }

	file.close();

#ifdef _DEBUG_
    // Verify
     for(int f=0; f<LUT_ATT_FREQ_NUM; f++)
     {
         std::cout << lut.Freq[f] << std::endl;
     }

     std::cout<< "ATT values=" << std::endl;

     for(int a=0; a<LUT_ATT_ATT_NUM; a++)
     {
          std::cout << lut.ATT[a] << std::endl;
     }

     std::cout<< "a values=" << std::endl;

     for(int port = 0; port < 23; port++)
     {
         std::cout << port << " " <<lut.Aatt[port] << std::endl;
     }

     for(int port = 0; port < 23; port++)
     {
         std::cout << "PORT " << port+1 << std::endl;

         for(int f=0; f<LUT_ATT_FREQ_NUM; f++)
         {
             std::cout << "Freq " << lut.Freq[f] << "\t";
         }

         std::cout << std::endl;

         for(int a= 0; a< LUT_ATT_ATT_NUM; a++ )
         {
             for(int f=0; f<LUT_ATT_FREQ_NUM; f++)
             {
                std::cout << lut.Katt[port][f][a] << "\t\t";
             }

             std::cout << std::endl;
         }

         std::cout << std::endl;
     }

#endif

	return (0);
}

int PatternCalibModule::Load_Sigma_LUT(Sigma& lut, const std::string& path)
{
    std::ifstream file(path);

    if (file.is_open()) {
        std::cout << "[Sigma_LUT] File has been opened" << std::endl;
    }
    else {
        std::cout << "[Sigma_LUT] File opening Error" << std::endl;
        return (-1);
    }

    // The file reading is based on EXCEL/CSV LUT file template.

    std::string row;
	int rowNumber = 1;
	int freqIndex = 0;

	while (std::getline(file, row)) {

	  std::istringstream iss(row);
	  std::string column;


	  int columnNumber = 1;
	  int portIndex = 0;
	  int tempIndex =0;
	  double value;

	  while (std::getline(iss, column, ',')) {

		  if(rowNumber == 3 && (columnNumber > 1 && columnNumber <=6))       // Temperature readings, only 3 readings from first 3 columns, rest are same
		  {
			  std::istringstream(column) >> value;
			  lut.Temp[tempIndex] = value;
		  }
		  else if (rowNumber >= 5 && columnNumber == 1)
		  {
			  std::istringstream(column) >> value;
			  lut.Freq[freqIndex] = value;
		  }
		  else if (rowNumber >= 5 && columnNumber > 1)
		  {
			  std::istringstream(column) >> value;
			  lut.Sigma[portIndex][freqIndex][tempIndex] = value;
		  }

		  if(rowNumber >= 3 && columnNumber > 1)
		  {
			  ++tempIndex;

			  if(tempIndex >= 5)                       // Only 3 temperature values per port
			  {
				  tempIndex = 0;
				  ++portIndex;
			  }
		  }
		  columnNumber++;
	  }

	  if(rowNumber >= 5)
	  {
		  ++freqIndex;
	  }
	  // Now you have a vector of doubles, each double is a column value
	  // You can access them by index, e.g. columns[0], columns[1], etc.
	  rowNumber++;
	}

	 file.close();

#ifdef _DEBUG_
	 // Verify

	 for(int f=0; f<LUT_SIGMA_FREQ_NUM; f++)
	 {
		 std::cout << "Freq: " << lut.Freq[f] << std::endl;
	 }

	 for(int t=0; t<LUT_SIGMA_TEMP_NUM; t++)
	 {
		 std::cout << "Temp: " << lut.Temp[t] << std::endl;
	 }

	 for(int port = 0; port< 23 ; port++)
	 {
		 std::cout << "PORT " << port+1 << std::endl;

		 for(int temp =0; temp < LUT_SIGMA_TEMP_NUM; temp++)
		 {
			 std::cout << lut.Temp[temp] << "  ";
		 }

		 std::cout << std::endl;

		 for(int f=0; f<LUT_SIGMA_FREQ_NUM; f++)
		 {
			 for(int temp =0; temp< LUT_SIGMA_TEMP_NUM; temp++)
			 {
				 std::cout << lut.Sigma[port][f][temp] << "  ";
			 }

			 std::cout << std::endl;
		 }
	 }

#endif
	return (0);
}

int PatternCalibModule::Load_PixelPos_LUT(PixelPos& lut, const std::string& path)
{
    std::ifstream file(path);

    if (file.is_open()) {
        std::cout << "[PixelPos_LUT] File has been opened" << std::endl;
    }
    else {
        std::cout << "[PixelPos_LUT] File opening Error" << std::endl;
        return (-1);
    }

    std::string row;
    int rowNumber = 1;
    int tempIndex = 0;

    while (std::getline(file, row)) {

      std::istringstream iss(row);
      std::string column;


      int columnNumber = 1;
      int freqIndex = 0;
      double value;

      while (std::getline(iss, column, ',')) {

          if(rowNumber >= 3 && columnNumber == 1)
          {
              std::istringstream(column) >> value;
              lut.Temp[tempIndex] = value;
          }
          else if (rowNumber == 2 && columnNumber > 1)
          {
              std::istringstream(column) >> value;
              lut.Freq[freqIndex] = value;
          }
          else if (rowNumber >=3 && columnNumber > 1)
          {
              std::istringstream(column) >> value;
              lut.Pos[freqIndex][tempIndex] = value;
          }
          columnNumber++;

          if(rowNumber >=2 && columnNumber > 2)
          {
              ++freqIndex;
          }
      }

      if(rowNumber >= 3)
      {
          ++tempIndex;
      }
      rowNumber++;
    }

     file.close();

#ifdef _DEBUG_
     // Verify

     for(int t=0; t<LUT_PIXELPOS_TEMP_NUM; t++)
     {
         std::cout << lut.Temp[t] << std::endl;
     }

     for(int f=0; f<LUT_PIXELPOS_FREQ_NUM; f++)
     {
         std::cout << lut.Freq[f] << "\t";
     }

     std::cout << std::endl;

     for(int t=0; t<LUT_PIXELPOS_TEMP_NUM; t++)
     {
         for(int f=0; f<LUT_PIXELPOS_FREQ_NUM; f++)
         {
             std::cout << lut.Pos[f][t] << "\t";
         }

         std::cout << std::endl;
     }

#endif

	return (0);
}

void PatternCalibModule::StopThread()
{
	PatternCalib_Closure();


	// Wait for all threads to exit normally
	if (thread1_id != 0 && thread2_id != 0 && thread3_id != 0 && thread4_id != 0)
	{
		pthread_join(thread1_id, NULL);
		pthread_join(thread2_id, NULL);
		pthread_join(thread3_id, NULL);
		pthread_join(thread4_id, NULL);

		thread1_id = 0;
		thread2_id = 0;
		thread3_id = 0;
		thread4_id = 0;
	}

	printf("Driver<PATTERN_CALIB> Threads terminated \n");

	if(pinstance_ != nullptr)
	{
		delete pinstance_;
		pinstance_ = nullptr;
	}

}
int PatternCalibModule::Get_Interpolation_Status()
{
	if(g_Status_Opt == ERROR || g_Status_Att == ERROR || g_Status_Sigma == ERROR || g_Status_PixelPos== ERROR)
		return -1;
	else
		return 0;
}

int PatternCalibModule::Get_LUT_Load_Status()
{
	return m_bCalibDataOk;
}

int PatternCalibModule::BreakThreadLoops()
{
	if (pthread_mutex_lock(&global_mutex[LOCK_CLOSE_CALIBLOOPS]) != 0)	// locking and checking the result, if lock was successful and no deadlock happened
		std::cout << "global_mutex[LOCK_CLOSE_CALIBLOOPS] lock unsuccessful" << std::endl;
	else
	{
		if(!b_LoopOn)
		{
			if (pthread_mutex_unlock(&global_mutex[LOCK_CLOSE_CALIBLOOPS]) != 0)	// Unlocking and checking the result, if lock was successful and no deadlock happened
				std::cout << "global_mutex[LOCK_CLOSE_CALIBLOOPS] unlock unsuccessful" << std::endl;

			return (0);		// break
		}

		if (pthread_mutex_unlock(&global_mutex[LOCK_CLOSE_CALIBLOOPS]) != 0)	// Unlocking and checking the result, if lock was successful and no deadlock happened
			std::cout << "global_mutex[LOCK_CLOSE_CALIBLOOPS] unlock unsuccessful" << std::endl;
	}

	return (-1);			// dont break loop
}
