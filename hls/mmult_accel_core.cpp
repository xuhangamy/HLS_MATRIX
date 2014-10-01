#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include <ap_axi_sdata.h>


typedef ap_axiu<32,4,5,5> AXI_VAL;
//typedef ap_axiu<32,4,4,4> AXI_VAL;

#define DB_DEBUG



template <typename T, int DIM>
void matrix_multiply_ref(T a[DIM][DIM], T b[DIM][DIM], T out[DIM][DIM])
{

	// matrix multiplication of a A*B matrix
	for (int ia = 0; ia < DIM; ++ia)
		for (int ib = 0; ib < DIM; ++ib)
		{

			float sum = 0;

			for (int id = 0; id < DIM; ++id)

				sum += a[ia][id] * b[id][ib];

			out[ia][ib] = sum;
		}

		return;
}


// --------------------------------------------------------
// actual multiplication algorithm

template <typename T, int DIM>
void matrix_multiply_hw(T a[DIM][DIM], T b[DIM][DIM], T out[DIM][DIM])
{
	// partition with half dimension size b/c BRAM has two ports
	int const FACTOR = DIM/2;
#pragma HLS INLINE off
#pragma HLS array_partition variable=a block factor=FACTOR dim=2
#pragma HLS array_partition variable=b block factor=FACTOR dim=1

	// matrix multiplication of a A*B matrix
	for (int ia = 0; ia < DIM; ++ia)
		for (int ib = 0; ib < DIM; ++ib)
		{
#pragma HLS PIPELINE II=1
			T sum = 0;
			for (int id = 0; id < DIM; ++id)
				sum += a[ia][id] * b[id][ib];
			out[ia][ib] = sum;
		}

		return;
}

// --------------------------------------------------------
// functions to insert and extract elements from an axi stream
// includes conversion to correct data type

template <typename T, int U, int TI, int TD>
inline T pop_stream(ap_axiu <sizeof(T)*8,U,TI,TD> const &e)
{
#pragma HLS INLINE off
	// does not work b/c of:
	// 	unsupported pointer reinterpretation
	//T ret = *reinterpret_cast<T const *> (&e.data);

	// instead use a union
	assert(sizeof(T) == sizeof(int));
	union
	{
		int ival;
		T oval;
	} converter;
	converter.ival = e.data;
	T ret = converter.oval;

	volatile ap_uint<sizeof(T)> strb = e.strb;
	volatile ap_uint<sizeof(T)> keep = e.keep;
	volatile ap_uint<U> user = e.user;
	volatile ap_uint<1> last = e.last;
	volatile ap_uint<TI> id = e.id;
	volatile ap_uint<TD> dest = e.dest;

	return ret;
}

template <typename T, int U, int TI, int TD>
inline ap_axiu <sizeof(T)*8,U,TI,TD> push_stream(T const &v, bool last = false)
{
#pragma HLS INLINE off
	ap_axiu<sizeof(T)*8,U,TI,TD> e;

	// use a union, b/c reinterpret_cast not supported
	assert(sizeof(T) == sizeof(int));
	union
	{
		int oval;
		T ival;
	} converter;
	converter.ival = v;
	e.data = converter.oval;

	// set it to sizeof(T) ones
	e.strb = -1;
	e.keep = 15; //e.strb;
	e.user = 0;
	e.last = last ? 1 : 0;
	e.id = 0;
	e.dest = 0;
	return e;
}

// --------------------------------------------------------
// main accelerator entry function: Design Under Test
// should be template parameters at top level function

template <typename T, int DIM, int SIZE, int U, int TI, int TD>
void dut_mmult_accel_core (
	AXI_VAL in_stream[2*SIZE],
	AXI_VAL out_stream[SIZE])
{

	// Map ports to Vivado HLS interfaces
#pragma HLS INTERFACE ap_fifo port=in_stream
#pragma HLS INTERFACE ap_fifo port=out_stream

	// Map HLS ports to AXI interfaces
#pragma HLS RESOURCE variable=in_stream  core=AXIS metadata="-bus_bundle INPUT_STREAM"
#pragma HLS RESOURCE variable=out_stream core=AXIS metadata="-bus_bundle OUTPUT_STREAM"
#pragma HLS RESOURCE variable=return core=AXI4LiteS metadata="-bus_bundle CONTROL_BUS"

	T a[DIM][DIM];
	T b[DIM][DIM];
	T out[DIM][DIM];

	assert(sizeof(T)*8 == 32);

	// stream in first matrix
	for(int i=0; i<DIM; i++)
		for(int j=0; j<DIM; j++)
		{
#pragma HLS PIPELINE II=1
			int k = i*DIM+j;
			//a[i][j] = pop_stream<T,1,1,1>(in_stream[k]);
			a[i][j] = pop_stream<T,U,TI,TD>(in_stream[k]);

		}

		// stream in second matrix
		for(int i=0; i<DIM; i++)
			for(int j=0; j<DIM; j++)
			{
#pragma HLS PIPELINE II=1
				int k = i*DIM+j+SIZE;
				//b[i][j] = pop_stream<T,1,1,1>(in_stream[k]);
				b[i][j] = pop_stream<T,U,TI,TD>(in_stream[k]);
			}

			// do multiplication
			matrix_multiply_hw<T, DIM>(a,b,out);

			// stream out result matrix
			for(int i=0; i<DIM; i++)
				for(int j=0; j<DIM; j++)
				{
#pragma HLS PIPELINE II=1
					int k = i*DIM+j;
					//out_stream[k] = push_stream<T,1,1,1>(out[i][j],k == (SIZE-1));
					out_stream[k] = push_stream<T,U,TI,TD>(out[i][j],k == (SIZE-1));
				}
				return;

}


// should be template parameters at top level function

template <typename T, int DIM, int SIZE, int U, int TI, int TD>
int test_matrix_mult(void)
{
	int i,j, err;

	T matOp1[DIM][DIM];
	T matOp2[DIM][DIM];
	T matMult_sw[DIM][DIM];
	T matMult_hw[DIM][DIM];

	/** Matrix Initiation */
	for(i = 0; i<DIM; i++)
		for(j = 0; j<DIM; j++)
			matOp1[i][j] = (float)(i+j);

	for(i = 0; i<DIM; i++)
		for(j = 0; j<DIM; j++)
			matOp2[i][j] = (float)(i*j);
	/** End of Initiation */

#ifdef DB_DEBUG

	printf("DEBUGGING AXI4 STREAMING DATA TYPES!\r\n");

	// prepare data for the DUT
	AXI_VAL inp_stream[2*SIZE];
	AXI_VAL out_stream[SIZE];

	assert(sizeof(T)*8 == 32);
	// stream in the first input  matrix
	for(int i=0; i<DIM; i++)
		for(int j=0; j<DIM; j++)
		{
			int k = i*DIM+j;
			inp_stream[k]      = push_stream<T,U,TI,TD>(matOp1[i][j],0);
		}
		// stream in the second input  matrix
		for(int i=0; i<DIM; i++)
			for(int j=0; j<DIM; j++)
			{
				int k = i*DIM+j;
				inp_stream[k+SIZE] = push_stream<T,U,TI,TD>(matOp2[i][j],k == (SIZE-1));
			}


			//call the DUT
			dut_mmult_accel_core<T, DIM, SIZE, U, TI, TD>(inp_stream, out_stream);

			// extract the output matrix from the out stream 
			for(int i=0; i<DIM; i++)
				for(int j=0; j<DIM; j++)
				{
					int k = i*DIM+j;
					matMult_hw[i][j] = pop_stream<T,U,TI,TD>(out_stream[k]);
				}


#else

	printf("NORMAL MODE\r\n");

	matrix_multiply_hw<T, DIM>(matOp1, matOp2, matMult_hw);


#endif




	/* reference Matrix Multiplication */
	matrix_multiply_ref<T, DIM>(matOp1, matOp2, matMult_sw);

	/** Matrix comparison */
	err = 0;
	for (i = 0; (i<DIM && !err); i++)
		for (j = 0; (j<DIM && !err); j++)
			if (matMult_sw[i][j] != matMult_hw[i][j]) 
				err++;

	if (err == 0)
		printf("Matrixes identical ... Test successful!\r\n");
	else
		printf("Test failed!\r\n");

	return err;
}


// THIS IS THE TOP LEVEL DESIGN THAT WILL BE SYNTHESIZED
#define MCR_SIZE 1024
void mmult_accel_core (AXI_VAL in_stream[2*MCR_SIZE], AXI_VAL out_stream[MCR_SIZE])
{
	// Map ports to Vivado HLS interfaces
	#pragma HLS INTERFACE ap_fifo port=in_stream
	#pragma HLS INTERFACE ap_fifo port=out_stream

	// Map HLS ports to AXI interfaces
	#pragma HLS RESOURCE variable=in_stream  core=AXIS metadata="-bus_bundle INPUT_STREAM"
	#pragma HLS RESOURCE variable=out_stream core=AXIS metadata="-bus_bundle OUTPUT_STREAM"
	#pragma HLS RESOURCE variable=return core=AXI4LiteS metadata="-bus_bundle CONTROL_BUS"
	
	dut_mmult_accel_core <float, 32, 32*32, 4, 5, 5>(in_stream, out_stream);
	
	return;
}



int main(void)
{

	typedef float T;
	int const DIM = 32;
	int const SIZE = DIM*DIM;
	int ret_val = 0;

	ret_val = test_matrix_mult<T, DIM, SIZE, 4,5,5>();

	return ret_val;

}
