/*=====================================================================
OpenCL.cpp
----------
File created by ClassTemplate on Mon Nov 02 17:13:50 2009
Code By Nicholas Chapman.
=====================================================================*/
#include "OpenCL.h"


// Stop windows.h from defining the min() and max() macros
#define NOMINMAX
#include <windows.h>
#include "../utils/stringutils.h"
#include "../utils/Exception.h"
#include "../utils/timer.h"
#include <iostream>
#include <cmath>
#include "../maths/mathstypes.h"


OpenCL::OpenCL()
{
#if USE_OPENCL
	context = 0;
	command_queue = 0;

	const std::wstring path = StringUtils::UTF8ToPlatformUnicodeEncoding("C:\\Windows\\System32\\OpenCL.dll");
	HMODULE module = ::LoadLibrary(path.c_str()); // TEMP HACK

	if(!module)
		throw Indigo::Exception("Failed to load OpenCL library from '" + StringUtils::PlatformToUTF8UnicodeEncoding(path) + "'");

	clGetPlatformIDs = (clGetPlatformIDs_TYPE)::GetProcAddress(module, "clGetPlatformIDs");
	clGetPlatformInfo = (clGetPlatformInfo_TYPE)::GetProcAddress(module, "clGetPlatformInfo");
	clGetDeviceIDs = (clGetDeviceIDs_TYPE)::GetProcAddress(module, "clGetDeviceIDs");
	clGetDeviceInfo = (clGetDeviceInfo_TYPE)::GetProcAddress(module, "clGetDeviceInfo");
	clCreateContextFromType = (clCreateContextFromType_TYPE)::GetProcAddress(module, "clCreateContextFromType");
	clReleaseContext = (clReleaseContext_TYPE)::GetProcAddress(module, "clReleaseContext");
	clCreateCommandQueue = (clCreateCommandQueue_TYPE)::GetProcAddress(module, "clCreateCommandQueue");
	clReleaseCommandQueue = (clReleaseCommandQueue_TYPE)::GetProcAddress(module, "clReleaseCommandQueue");
	clCreateBuffer = (clCreateBuffer_TYPE)::GetProcAddress(module, "clCreateBuffer");
	clReleaseMemObject = (clReleaseMemObject_TYPE)::GetProcAddress(module, "clReleaseMemObject");
	clCreateProgramWithSource = (clCreateProgramWithSource_TYPE)::GetProcAddress(module, "clCreateProgramWithSource");
	clBuildProgram = (clBuildProgram_TYPE)::GetProcAddress(module, "clBuildProgram");
	clGetProgramBuildInfo = (clGetProgramBuildInfo_TYPE)::GetProcAddress(module, "clGetProgramBuildInfo");
	clCreateKernel = (clCreateKernel_TYPE)::GetProcAddress(module, "clCreateKernel");
	clSetKernelArg = (clSetKernelArg_TYPE)::GetProcAddress(module, "clSetKernelArg");
	clEnqueueWriteBuffer = (clEnqueueWriteBuffer_TYPE)::GetProcAddress(module, "clEnqueueWriteBuffer");
	clEnqueueReadBuffer = (clEnqueueReadBuffer_TYPE)::GetProcAddress(module, "clEnqueueReadBuffer");
	clEnqueueNDRangeKernel = (clEnqueueNDRangeKernel_TYPE)::GetProcAddress(module, "clEnqueueNDRangeKernel");


	this->device_to_use_id = 0;

	std::vector<cl_platform_id> platforms(10);
	cl_uint num_platforms = 0;
	if(this->clGetPlatformIDs(10, &platforms[0], &num_platforms) != CL_SUCCESS)
		throw Indigo::Exception("clGetPlatformIDs failed");

	std::cout << "Num platforms: " << num_platforms << std::endl;

	for(cl_uint i=0; i<num_platforms; ++i)
	{
		std::vector<char> val(100000);
		if(clGetPlatformInfo(platforms[i], CL_PLATFORM_PROFILE, val.size(), &val[0], NULL) != CL_SUCCESS)
			throw Indigo::Exception("clGetPlatformInfo failed");
		const std::string platform_profile(&val[0]);
		if(clGetPlatformInfo(platforms[i], CL_PLATFORM_VERSION, val.size(), &val[0], NULL) != CL_SUCCESS)
			throw Indigo::Exception("clGetPlatformInfo failed");
		const std::string platform_version(&val[0]);
		if(clGetPlatformInfo(platforms[i], CL_PLATFORM_NAME, val.size(), &val[0], NULL) != CL_SUCCESS)
			throw Indigo::Exception("clGetPlatformInfo failed");
		const std::string platform_name(&val[0]);
		if(clGetPlatformInfo(platforms[i], CL_PLATFORM_VENDOR, val.size(), &val[0], NULL) != CL_SUCCESS)
			throw Indigo::Exception("clGetPlatformInfo failed");
		const std::string platform_vendor(&val[0]);
		if(clGetPlatformInfo(platforms[i], CL_PLATFORM_EXTENSIONS, val.size(), &val[0], NULL) != CL_SUCCESS)
			throw Indigo::Exception("clGetPlatformInfo failed");
		const std::string platform_extensions(&val[0]);

		std::cout << "platform_profile: " << platform_profile << std::endl;
		std::cout << "platform_version: " << platform_version << std::endl;
		std::cout << "platform_name: " << platform_name << std::endl;
		std::cout << "platform_vendor: " << platform_vendor << std::endl;
		std::cout << "platform_extensions: " << platform_extensions << std::endl;

		std::vector<cl_device_id> device_ids(10);
		cl_uint num_devices = 0;
		if(clGetDeviceIDs(platforms[i], CL_DEVICE_TYPE_ALL, device_ids.size(), &device_ids[0], &num_devices) != CL_SUCCESS)
			throw Indigo::Exception("clGetDeviceIDs failed");

		for(cl_uint d=0; d<num_devices; ++d)
		{
			cl_device_type device_type;
			if(clGetDeviceInfo(device_ids[d], CL_DEVICE_TYPE, sizeof(device_type), &device_type, NULL) != CL_SUCCESS)
				throw Indigo::Exception("clGetDeviceInfo failed");

			if(device_type & CL_DEVICE_TYPE_CPU)
				std::cout << "device_type: CL_DEVICE_TYPE_CPU" << std::endl;
			if(device_type & CL_DEVICE_TYPE_GPU)
				std::cout << "device_type: CL_DEVICE_TYPE_GPU" << std::endl;
			if(device_type & CL_DEVICE_TYPE_ACCELERATOR)
				std::cout << "device_type: CL_DEVICE_TYPE_ACCELERATOR" << std::endl;
			if(device_type & CL_DEVICE_TYPE_DEFAULT)
				std::cout << "device_type: CL_DEVICE_TYPE_DEFAULT" << std::endl;

			std::vector<char> buf(100000);
			if(clGetDeviceInfo(device_ids[d], CL_DEVICE_NAME, buf.size(), &buf[0], NULL) != CL_SUCCESS)
				throw Indigo::Exception("clGetDeviceInfo failed");
			const std::string device_name(&buf[0]);
			if(clGetDeviceInfo(device_ids[d], CL_DRIVER_VERSION, buf.size(), &buf[0], NULL) != CL_SUCCESS)
				throw Indigo::Exception("clGetDeviceInfo failed");
			const std::string driver_version(&buf[0]);
			if(clGetDeviceInfo(device_ids[d], CL_DEVICE_PROFILE, buf.size(), &buf[0], NULL) != CL_SUCCESS)
				throw Indigo::Exception("clGetDeviceInfo failed");
			const std::string device_profile(&buf[0]);
			if(clGetDeviceInfo(device_ids[d], CL_DEVICE_VERSION, buf.size(), &buf[0], NULL) != CL_SUCCESS)
				throw Indigo::Exception("clGetDeviceInfo failed");
			const std::string device_version(&buf[0]);

			cl_uint device_max_compute_units = 0;
			if(clGetDeviceInfo(device_ids[d], CL_DEVICE_MAX_COMPUTE_UNITS, sizeof(device_max_compute_units), &device_max_compute_units, NULL) != CL_SUCCESS)
				throw Indigo::Exception("clGetDeviceInfo failed");
			
			size_t device_max_work_group_size = 0;
			if(clGetDeviceInfo(device_ids[d], CL_DEVICE_MAX_WORK_GROUP_SIZE, sizeof(device_max_work_group_size), &device_max_work_group_size, NULL) != CL_SUCCESS)
				throw Indigo::Exception("clGetDeviceInfo failed");

			cl_uint device_max_work_item_dimensions = 0;
			if(clGetDeviceInfo(device_ids[d], CL_DEVICE_MAX_WORK_ITEM_DIMENSIONS, sizeof(device_max_work_item_dimensions), &device_max_work_item_dimensions, NULL) != CL_SUCCESS)
				throw Indigo::Exception("clGetDeviceInfo failed");

			std::vector<size_t> device_max_num_work_items(device_max_work_item_dimensions);
			if(clGetDeviceInfo(device_ids[d], CL_DEVICE_MAX_WORK_ITEM_SIZES, sizeof(size_t) * device_max_num_work_items.size(), &device_max_num_work_items[0], NULL) != CL_SUCCESS)
				throw Indigo::Exception("clGetDeviceInfo failed");

			cl_uint device_max_clock_frequency = 0;
			if(clGetDeviceInfo(device_ids[d], CL_DEVICE_MAX_CLOCK_FREQUENCY, sizeof(device_max_clock_frequency), &device_max_clock_frequency, NULL) != CL_SUCCESS)
				throw Indigo::Exception("clGetDeviceInfo failed");
			
			cl_ulong device_global_mem_size = 0;
			if(clGetDeviceInfo(device_ids[d], CL_DEVICE_GLOBAL_MEM_SIZE, sizeof(device_global_mem_size), &device_global_mem_size, NULL) != CL_SUCCESS)
				throw Indigo::Exception("clGetDeviceInfo failed");

			std::cout << "device_name: " << device_name << std::endl;
			std::cout << "driver_version: " << driver_version << std::endl;
			std::cout << "device_profile: " << device_profile << std::endl;
			std::cout << "device_version: " << device_version << std::endl;
			std::cout << "device_max_compute_units: " << device_max_compute_units << std::endl;
			std::cout << "device_max_work_group_size: " << device_max_work_group_size << std::endl;
			std::cout << "device_max_work_item_dimensions: " << device_max_work_item_dimensions << std::endl;

			for(size_t z=0; z<device_max_num_work_items.size(); ++z)
				std::cout << "Dim " << z << " device_max_num_work_items: " << device_max_num_work_items[z] << std::endl;


			std::cout << "device_max_clock_frequency: " << device_max_clock_frequency << " MHz" << std::endl;
			std::cout << "device_global_mem_size: " << device_global_mem_size << " B" << std::endl;

			this->device_to_use_id = device_ids[d];
		}
	}

	cl_int error_code;
	this->context = clCreateContextFromType(
		NULL, // properties
		CL_DEVICE_TYPE_GPU, // device type
		NULL, // pfn notifiy
		NULL, // user data
		&error_code
	);

	if(this->context == 0)
		throw Indigo::Exception("clCreateContextFromType failed");

	// Run a test
	if(false)
	{
		// Create command queue
		this->command_queue = this->clCreateCommandQueue(
			context,
			device_to_use_id, // TEMP HACK, may not be same device as context
			0, // queue properties
			&error_code);

		if(command_queue == 0)
			throw Indigo::Exception("clCreateCommandQueue failed");


		// Create buffer
		const int N = 10000 * 512; // Should be a multiple of work group size (512)
		std::cout << "N: " << N << std::endl;
		cl_mem buffer_a = clCreateBuffer(
			context,
			CL_MEM_READ_ONLY,
			sizeof(cl_float) * N,
			NULL, // host ptr
			&error_code
		);

		if(!buffer_a)
			throw Indigo::Exception("clCreateBuffer failed");



		cl_mem buffer_c = clCreateBuffer(
			context,
			CL_MEM_WRITE_ONLY,
			sizeof(cl_float) * N,
			NULL, // host ptr
			&error_code
		);

		if(!buffer_c)
			throw Indigo::Exception("clCreateBuffer failed");


		// Create program
		std::vector<std::string> program_lines;
		program_lines.push_back("__kernel void applyPow(__global const float* a, __global float* c, int iNumElements)\n");
		program_lines.push_back("{\n");
		program_lines.push_back("	int iGID = get_global_id(0);\n");
		program_lines.push_back("	if (iGID >= iNumElements)\n");
		program_lines.push_back("		return;\n");
		program_lines.push_back("	c[iGID] = pow(a[iGID], 2.2f);\n");
		program_lines.push_back("}\n");

		std::vector<const char*> strings(program_lines.size());
		for(size_t i=0; i<program_lines.size(); ++i)
			strings[i] = program_lines[i].c_str();

		cl_program program = clCreateProgramWithSource(
			context,
			program_lines.size(),
			&strings[0],
			NULL, // lengths, can NULL because all strings are null-terminated.
			&error_code
		);

		// Build program
		const std::string options = "";
		if(clBuildProgram(
			program,
			1, // num devices
			&device_to_use_id, // device ids
			options.c_str(), // options
			NULL, // pfn_notify
			NULL // user data
		) !=  CL_SUCCESS)
		{

			//throw Indigo::Exception("clBuildProgram failed");

			std::vector<char> buf(100000);
			clGetProgramBuildInfo(
				program,
				device_to_use_id,
				CL_PROGRAM_BUILD_LOG,
				buf.size(),
				&buf[0],
				NULL);
			const std::string log(&buf[0]);
			std::cout << "build log: " << log << std::endl;
		}

		cl_kernel kernel = clCreateKernel(
			program,
			"applyPow",
			&error_code
		);
		if(!kernel)
			throw Indigo::Exception("clCreateKernel failed");


		// Set kernel args
		if(clSetKernelArg(
			kernel,
			0, // arg index
			sizeof(cl_mem), // arg size
			&buffer_a
		) != CL_SUCCESS)
			throw Indigo::Exception("clSetKernelArg failed");

		if(clSetKernelArg(
			kernel,
			1, // arg index
			sizeof(cl_mem), // arg size
			&buffer_c
		) != CL_SUCCESS)
			throw Indigo::Exception("clSetKernelArg failed");

		const cl_int numElements = N;
		if(clSetKernelArg(
			kernel,
			2, // arg index
			sizeof(cl_int), // arg size
			&numElements
		) != CL_SUCCESS)
			throw Indigo::Exception("clSetKernelArg failed");

		std::vector<float> host_buffer_a(N, 2.0f);
		std::vector<float> host_buffer_c(N, 2.0f);
		std::vector<float> ref_buffer_c(N, 2.0f);

		const size_t global_work_size = N;
		const size_t local_work_size = 512;


		// Enqueue write event
		clEnqueueWriteBuffer(
			command_queue,
			buffer_a,
			CL_FALSE, // blocking write
			0, // offset
			sizeof(cl_float) * N, // size in bytes
			&host_buffer_a[0], // host buffer pointer
			0, // num events in wait list
			NULL, // wait list
			NULL // event
		);

		// Enqueue kernel execution
		cl_int result = clEnqueueNDRangeKernel(
			command_queue,
			kernel,
			1, // dimension
			NULL, // global_work_offset
			&global_work_size, // global_work_size
			&local_work_size,
			0, // num_events_in_wait_list
			NULL, // event_wait_list
			NULL // event
		);
		if(result != CL_SUCCESS)
		{
			if(result == CL_INVALID_WORK_DIMENSION)
				throw Indigo::Exception("clEnqueueNDRangeKernel failed: CL_INVALID_WORK_DIMENSION");
			else if(result == CL_INVALID_WORK_GROUP_SIZE)
				throw Indigo::Exception("clEnqueueNDRangeKernel failed: CL_INVALID_WORK_GROUP_SIZE");
			else
				throw Indigo::Exception("clEnqueueNDRangeKernel failed.");

		}

		// Enqueue read
		clEnqueueReadBuffer(
			command_queue,
			buffer_c,
			CL_TRUE, // blocking read
			0, // offset
			sizeof(cl_float) * N, // size in bytes
			&host_buffer_c[0], // host buffer pointer
			0, // num events in wait list
			NULL, // wait list
			NULL // event
		);


		// TEMP: do again
		Timer timer;

		// Enqueue write event
		clEnqueueWriteBuffer(
			command_queue,
			buffer_a,
			CL_FALSE, // blocking write
			0, // offset
			sizeof(cl_float) * N, // size in bytes
			&host_buffer_a[0], // host buffer pointer
			0, // num events in wait list
			NULL, // wait list
			NULL // event
		);

		// Enqueue kernel execution
		result = clEnqueueNDRangeKernel(
			command_queue,
			kernel,
			1, // dimension
			NULL, // global_work_offset
			&global_work_size, // global_work_size
			&local_work_size,
			0, // num_events_in_wait_list
			NULL, // event_wait_list
			NULL // event
		);
		if(result != CL_SUCCESS)
		{
			if(result == CL_INVALID_WORK_DIMENSION)
				throw Indigo::Exception("clEnqueueNDRangeKernel failed: CL_INVALID_WORK_DIMENSION");
			else if(result == CL_INVALID_WORK_GROUP_SIZE)
				throw Indigo::Exception("clEnqueueNDRangeKernel failed: CL_INVALID_WORK_GROUP_SIZE");
			else
				throw Indigo::Exception("clEnqueueNDRangeKernel failed.");

		}

		// Enqueue read
		clEnqueueReadBuffer(
			command_queue,
			buffer_c,
			CL_TRUE, // blocking read
			0, // offset
			sizeof(cl_float) * N, // size in bytes
			&host_buffer_c[0], // host buffer pointer
			0, // num events in wait list
			NULL, // wait list
			NULL // event
		);

		// Computation should have finished by now.  
		const double open_cl_elapsed = timer.elapsed();
		// cycles/op = elapsed 
		const double open_cl_cycles_per_op = open_cl_elapsed * 2.4e9     / (double)N;
		//			cycles / op			  = s               * (cycles / s) / op

		timer.reset();

		// Compute on host
		#pragma omp parallel for
		for(int i=0; i<(int)host_buffer_c.size(); ++i)
			ref_buffer_c[i] = std::pow(host_buffer_a[i], 2.2f);

		const double host_elapsed = timer.elapsed();
		const double host_cycles_per_op = host_elapsed * 2.4e9 / (double)N;

		std::cout << "Open CL took " << open_cl_elapsed << " s" << std::endl;
		std::cout << "open_cl_cycles_per_op " << open_cl_cycles_per_op << " cycles" << std::endl;
		std::cout << "CPU took " << host_elapsed << " s" << std::endl;
		std::cout << "host_cycles_per_op " << host_cycles_per_op << " cycles" << std::endl;

		std::cout << "Speedup factor: " << (host_elapsed / open_cl_elapsed) << std::endl;

		// Check against CPU version:
		for(size_t i=0; i<host_buffer_c.size(); ++i)
		{
			const float expected = ref_buffer_c[i];
			const float open_cl_computed = host_buffer_c[i];
			if(!::epsEqual(expected, open_cl_computed))
			{
				std::cout << "expected: " << expected << std::endl;
				std::cout << "open_cl_computed: " << open_cl_computed << std::endl;
			}
		}



		// Free buffers
		if(clReleaseMemObject(buffer_a) != CL_SUCCESS)
			throw Indigo::Exception("clReleaseMemObject failed");
		if(clReleaseMemObject(buffer_c) != CL_SUCCESS)
			throw Indigo::Exception("clReleaseMemObject failed");


		// Free command queue
		if(command_queue)
		{
			if(clReleaseCommandQueue(command_queue) != CL_SUCCESS)
				throw Indigo::Exception("clReleaseCommandQueue failed");
		}
	}
#endif
}


OpenCL::~OpenCL()
{
#if USE_OPENCL
	// Cleanup
	if(this->context)
	{
		if(clReleaseContext(this->context) != CL_SUCCESS)
			throw Indigo::Exception("clReleaseContext failed");
	}
#endif
}
