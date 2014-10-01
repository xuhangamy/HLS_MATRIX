############################################################
## This file is generated automatically by Vivado HLS.
## Please DO NOT edit it.
## Copyright (C) 2012 Xilinx Inc. All rights reserved.
############################################################
open_project hls2013v1_fp_matrix_mult_prj -reset
set_top mmult_accel_core
add_files mmult_accel_core.cpp
add_files -tb mmult_accel_core.cpp
open_solution "solution8"
set_part  {xc7z020clg484-1}
create_clock -period 20
#config_interface -trim_dangling_port

source "./directives.tcl"
csynth_design
export_design -evaluate vhdl -format pcore  -version \"2.00.a\"