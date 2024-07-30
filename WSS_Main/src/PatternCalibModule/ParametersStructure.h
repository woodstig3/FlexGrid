/*
 * ParametersStructure.h
 *
 *  Created on: Feb 7, 2023
 *      Author: mib_n
 */

#ifndef SRC_PATTERNCALIBMODULE_PARAMETERSSTRUCTURE_H_
#define SRC_PATTERNCALIBMODULE_PARAMETERSSTRUCTURE_H_


struct Aopt_Kopt_ThreadArgs
{
	int 			port;
	float 			freq;

	float 			result_Aopt;
	float 			result_Kopt;
};

struct Aatt_Katt_ThreadArgs
{
	int 			port;
	float 			freq;
	float 			ATT;

	float 			result_Aatt;
	float 			result_Katt;
};

struct Sigma_ThreadArgs
{
	int 			port;
	float 			freq;
	float 			temperature;

	float 			result_Sigma;
};

struct Pixel_Pos_ThreadArgs
{
	float 			f1;
	float 			f2;
	float 			fc;
	float 			temperature;

	float 			result_F1_PixelPos;
	float 			result_F2_PixelPos;
	float 			result_FC_PixelPos;
};

#endif /* SRC_PATTERNCALIBMODULE_PARAMETERSSTRUCTURE_H_ */
