

#include <stdio.h>
#include "xil_io.h"
#include "platform.h"
#include "xparameters.h"
#include "xscugic.h"
#include "xaxidma.h"
#include "xmmult_accel_core.h"
#include "xil_printf.h"
#include "lib_xmmult_hw.h"
#include "xtmrctr.h"
#include "temp.h"

#define UPDATE_PACE (100000000)

#undef USE_HIGH_LEVEL_API_TIMERS

// AXI DMA Instance
XAxiDma AxiDma;

// TIMER Instance
XTmrCtr timer_dev;


int init_dma(){
	XAxiDma_Config *CfgPtr;
	int status;

	CfgPtr = XAxiDma_LookupConfig(XPAR_AXI_DMA_0_DEVICE_ID);
	if(!CfgPtr){
		print("Error looking for AXI DMA config\n\r");
		return XST_FAILURE;
	}
	status = XAxiDma_CfgInitialize(&AxiDma,CfgPtr);
	if(status != XST_SUCCESS){
		print("Error initializing DMA\n\r");
		return XST_FAILURE;
	}
	//check for scatter gather mode
	if(XAxiDma_HasSg(&AxiDma)){
		print("Error DMA configured in SG mode\n\r");
		return XST_FAILURE;
	}
	/* Disable interrupts, we use polling mode */
	XAxiDma_IntrDisable(&AxiDma, XAXIDMA_IRQ_ALL_MASK,XAXIDMA_DEVICE_TO_DMA);
	XAxiDma_IntrDisable(&AxiDma, XAXIDMA_IRQ_ALL_MASK,XAXIDMA_DMA_TO_DEVICE);

	return XST_SUCCESS;
}


int main(){
	int i, j, k;
	int err=0;
	int status;
	float A[DIM][DIM];
	float B[DIM][DIM];
	float res_hw[DIM][DIM];
	float res_sw[DIM][DIM];

	unsigned int dma_size = SIZE * sizeof(float);

    float acc_factor;
	unsigned int init_time, curr_time, calibration;
	unsigned int begin_time;
	unsigned int end_time;
	unsigned int run_time_sw = 0;
	unsigned int run_time_hw = 0;

	init_platform();

	xil_printf("\r********************************\n\r");
	xil_printf("\rVivado HLS: FP MATRIX MULT + DMA\n\n\r");

	// Init DMA
	status = init_dma();
	if(status != XST_SUCCESS){
		print("\rError: DMA init failed\n");
		return XST_FAILURE;
	}
	print("\rDMA Init done\n\r");

#ifdef	USE_HIGH_LEVEL_API_TIMERS
	// Setup timer
	status = XTmrCtr_Initialize(&timer_dev, XPAR_AXI_TIMER_0_DEVICE_ID);
	if(status != XST_SUCCESS)
	{
		print("\rError: timer setup failed\n");
		//return XST_FAILURE;
	}
	XTmrCtr_SetOptions(&timer_dev, XPAR_AXI_TIMER_0_DEVICE_ID, XTC_ENABLE_ALL_OPTION);
#else
	/*	Initialize Timer   */
	status = TmrCtrLowLevelExample(XPAR_TMRCTR_0_BASEADDR, TIMER_COUNTER_0);
	if (status != XST_SUCCESS) {
		return XST_FAILURE;
	}
#endif

	// Calibrate timer
#ifdef	USE_HIGH_LEVEL_API_TIMERS
	XTmrCtr_Reset(&timer_dev, XPAR_AXI_TIMER_0_DEVICE_ID);
	init_time = XTmrCtr_GetValue(&timer_dev, XPAR_AXI_TIMER_0_DEVICE_ID);
	curr_time = XTmrCtr_GetValue(&timer_dev, XPAR_AXI_TIMER_0_DEVICE_ID);
#else
	init_time = XTmrCtr_GetTimerCounterReg(XPAR_TMRCTR_0_BASEADDR, TIMER_COUNTER_0);
	curr_time = XTmrCtr_GetTimerCounterReg(XPAR_TMRCTR_0_BASEADDR, TIMER_COUNTER_0);
#endif

	calibration = curr_time - init_time;
	xil_printf("\rCalibration report:\r\n");
	xil_printf("\rinit_time: %d cycles.\r\n", init_time);
	xil_printf("\rcurr_time: %d cycles.\r\n", curr_time);
	xil_printf("\rcalibration: %d cycles.\r\n", calibration);

	// Loop measurement
#ifdef	USE_HIGH_LEVEL_API_TIMERS
	XTmrCtr_Reset(&timer_dev, XPAR_AXI_TIMER_0_DEVICE_ID);
	begin_time = XTmrCtr_GetValue(&timer_dev, XPAR_AXI_TIMER_0_DEVICE_ID);
#else
	begin_time = XTmrCtr_GetTimerCounterReg(XPAR_TMRCTR_0_BASEADDR, TIMER_COUNTER_0);
#endif

	for (i = 0; i< 1000; i++);

#ifdef	USE_HIGH_LEVEL_API_TIMERS
	end_time = XTmrCtr_GetValue(&timer_dev, XPAR_AXI_TIMER_0_DEVICE_ID);
#else
	end_time = XTmrCtr_GetTimerCounterReg(XPAR_TMRCTR_0_BASEADDR, TIMER_COUNTER_0);
#endif

	run_time_sw = end_time - begin_time - calibration;
	xil_printf("\rLoop time for 100 iterations is %d cycles.\r\n", run_time_sw);

	// input data
	/** Matrix Initiation */
	for(i = 0; i<DIM; i++)
		for(j = 0; j<DIM; j++)
		{
			A[i][j] = (float)(i+j);
			B[i][j] = (float)(i*j);
		}
	/** End of Initiation */

	for (k = 0; k < 4; k++)
	// setup the HW accelerator
	{
		//call the software version of the function
		//print("\r now ARM is running the SW IP\n\r");
#ifdef	USE_HIGH_LEVEL_API_TIMERS
		XTmrCtr_Reset(&timer_dev, XPAR_AXI_TIMER_0_DEVICE_ID);
		begin_time = XTmrCtr_GetValue(&timer_dev, XPAR_AXI_TIMER_0_DEVICE_ID);
#else
		begin_time = XTmrCtr_GetTimerCounterReg(XPAR_TMRCTR_0_BASEADDR, TIMER_COUNTER_0);
#endif

		matrix_multiply_ref(A, B, res_sw);

#ifdef	USE_HIGH_LEVEL_API_TIMERS
		end_time = XTmrCtr_GetValue(&timer_dev, XPAR_AXI_TIMER_0_DEVICE_ID);
#else
		end_time = XTmrCtr_GetTimerCounterReg(XPAR_TMRCTR_0_BASEADDR, TIMER_COUNTER_0);
#endif
		run_time_sw = end_time - begin_time - calibration;
		xil_printf("\r\nTotal run time for SW on Processor is %d cycles.\r\n",
				run_time_sw);

	    //Xil_DCacheFlushRange((unsigned int)A,dma_size);
        //Xil_DCacheFlushRange((unsigned int)B,dma_size);
		//Xil_DCacheFlushRange((unsigned int)res_hw,dma_size);

		// Run the HW Accelerator
		Setup_HW_Accelerator(A, B, res_hw, dma_size);

#ifdef	USE_HIGH_LEVEL_API_TIMERS
		XTmrCtr_Reset(&timer_dev, XPAR_AXI_TIMER_0_DEVICE_ID);
		begin_time = XTmrCtr_GetValue(&timer_dev, XPAR_AXI_TIMER_0_DEVICE_ID);
#else
		begin_time = XTmrCtr_GetTimerCounterReg(XPAR_TMRCTR_0_BASEADDR, TIMER_COUNTER_0);
#endif

		Run_HW_Accelerator(A, B, res_hw, dma_size);

#ifdef	USE_HIGH_LEVEL_API_TIMERS
		end_time = XTmrCtr_GetValue(&timer_dev, XPAR_AXI_TIMER_0_DEVICE_ID);
#else
		end_time = XTmrCtr_GetTimerCounterReg(XPAR_TMRCTR_0_BASEADDR, TIMER_COUNTER_0);
#endif
		run_time_hw = end_time - begin_time - calibration;
		xil_printf(
				"\rTotal run time for AXI DMA + HW accelerator is %d cycles.\r\n",
				run_time_hw);


		//Compare the results from sw and hw
		for (i = 0; i < DIM; i++)
			for (j = 0; j < DIM; j++)
				if (res_sw[i][j] != res_hw[i][j]) {
					err = 1;
				}

		if (err == 0)
			print("\rSW and HW results match!\n\r");
		else
			print("\rERROR: results mismatch\n\r");

		// HW vs. SW speedup factor
		acc_factor = (float) run_time_sw / (float) run_time_hw;
		xil_printf("\r\033[1mAcceleration factor: %d.%d \033[0m\r\n\r\n",
				(int) acc_factor, (int) (acc_factor * 1000) % 1000);

#ifdef	USE_HIGH_LEVEL_API_TIMERS
		/** wait for next execution */
		XTmrCtr_Reset(&timer_dev, XPAR_AXI_TIMER_0_DEVICE_ID);
		begin_time = XTmrCtr_GetValue(&timer_dev, XPAR_AXI_TIMER_0_DEVICE_ID);
		end_time = 0;
		while (end_time < UPDATE_PACE)
			end_time = XTmrCtr_GetValue(&timer_dev, XPAR_AXI_TIMER_0_DEVICE_ID)
					- begin_time;
#endif

	}

	print("\rEND\n\n\r");

#ifndef USE_HIGH_LEVEL_API_TIMERS
	XTmrCtr_Disable(XPAR_AXI_TIMER_0_BASEADDR, TIMER_COUNTER_0);
#endif

	cleanup_platform();


	return  err;
}
