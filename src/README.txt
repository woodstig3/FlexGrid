[UPDATES]

20-JAN-2023:	[Serial Module] 	Module was restructure and logic is updated. Now serial will read all command from buffer until 
									no command is there and then execute each command one by one based on delimiter.
				
20-JAN-2023:	[CmdDecoder] 		Module was refactored heavily. Repeated logic sections were combined in one function i.e. Print_SearchAttrib() 
									'buff' write logic was refactored.
				
20-JAN-2023:	[CmdDecoder] 		return(1) error value was changed to -1 in order to keep it same as other module return value.

26-JAB-2023:	[PatternCalib]		Pthread_cond_wait and Pthread_cond_signal functions are added to synchronize threads between Patter
									Generation and Calibration modules

28-FEB-2023:	[All Modules] 		Threads loop termination methods are updated. Pthread_cancel is removed and Pthread_exit is used.
			      					New method to close thread properly is added

28-FEB-2023:	[MAIN]				StopThreads() ability is give to main

28-FEB-2023:	[PatternGen]		Pattern Generation Module logic is started

01-MAR-2023		[PatternGen]		All Excel Formulas for Pattern Generation are finished.

01-MAR-2023		[CmdDecoder]		<DevelopMod> Phase_depth update by command is added

02-MAR-2023		[PatternCalib]		The return values for interpolation errors are properly directed to PatternGen module, in order to know
									if calibration module failed or not.
									
02-MAR-2023		[PatternCalib]		Interpolation for pixel position search is updated. Two interpolations will be performed one for F1 nad other 
									F2. In case either of the interpolation failed due to F1/F2 outside the boundary, then we interpolate FC.
									F1/F2 outside the boundary will not occur in normal cases because for such to happen channel BW must be very wide.
									
02-MAR-2023		[PatternGen]		Negative Sigma logic is added. To calculate -ve sigma we calculate +ve sigma value and flip each period value.
									A temporary FlipArray is used to flip each period and update final attenuatedPattern_Limited[] array.
									
09-MAR-2023		[CmdDecoder]		Removed g_mandatoryAttribute count variable and added a function TestMandatoryAttributes to check each value
									of mandatory variable and if they are not zero it is success.
									
10-MAR-2023		[Interface]			Memory Mapping Interface is completed. LCOS Display pin connection test also completed		

21-MAR-2023		[Design Update]		All modules/classes are moved to singleton class, with only one instance available to access.

22-MAY-2023		[OCM Transfer]		Prior to this change, OCM was working well and stable however the timing taking by ocm transfer was 
									approx 200ms. Now I have remove usleep operations in SendPatternData() function. OCM transfer time is 
									decreased to approx 100ms.
									
25-MAY-2023		[PatternGen]		*Updated Create_Linear_LUT to create maximum 2.2Pi LUT always. 
									*Updated default phaseDepth to 2.2Pi for calculation.
									Updated Fill_Channel_ColumnData function in order to select LUT range for usage (is in optional function in order to select LUT range by grayscale)										
						
																		
