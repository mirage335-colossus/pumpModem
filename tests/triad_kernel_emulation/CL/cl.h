#pragma once

// The subset of the OpenCL 1.2 C interface used by the SM120 host code,
// declared for the kernel emulation build. A device build uses the system
// headers. Constants carry their standard values.

#include <cstddef>
#include <cstdint>

#define CL_CALLBACK

using cl_int = std::int32_t;
using cl_uint = std::uint32_t;
using cl_long = std::int64_t;
using cl_ulong = std::uint64_t;
using cl_bool = cl_uint;
using cl_bitfield = cl_ulong;
using cl_device_type = cl_bitfield;
using cl_device_info = cl_uint;
using cl_mem_flags = cl_bitfield;
using cl_map_flags = cl_bitfield;
using cl_command_queue_properties = cl_bitfield;
using cl_profiling_info = cl_uint;
using cl_context_properties = std::intptr_t;

using cl_platform_id = struct _cl_platform_id*;
using cl_device_id = struct _cl_device_id*;
using cl_context = struct _cl_context*;
using cl_command_queue = struct _cl_command_queue*;
using cl_mem = struct _cl_mem*;
using cl_program = struct _cl_program*;
using cl_kernel = struct _cl_kernel*;
using cl_event = struct _cl_event*;

#define CL_SUCCESS 0
#define CL_DEVICE_NOT_FOUND -1
#define CL_MEM_OBJECT_ALLOCATION_FAILURE -4
#define CL_OUT_OF_RESOURCES -5
#define CL_OUT_OF_HOST_MEMORY -6
#define CL_PROFILING_INFO_NOT_AVAILABLE -7
#define CL_INVALID_VALUE -30
#define CL_INVALID_KERNEL_NAME -46
#define CL_INVALID_KERNEL_DEFINITION -47
#define CL_INVALID_ARG_INDEX -49
#define CL_INVALID_ARG_SIZE -51
#define CL_INVALID_WORK_GROUP_SIZE -54
#define CL_FALSE 0
#define CL_TRUE 1
#define CL_DEVICE_TYPE_GPU (1 << 2)
#define CL_DEVICE_VENDOR_ID 0x1001
#define CL_DEVICE_MAX_MEM_ALLOC_SIZE 0x1010
#define CL_DEVICE_GLOBAL_MEM_SIZE 0x101F
#define CL_QUEUE_PROFILING_ENABLE (1 << 1)
#define CL_MEM_READ_WRITE (1 << 0)
#define CL_MEM_ALLOC_HOST_PTR (1 << 4)
#define CL_MAP_READ (1 << 0)
#define CL_MAP_WRITE (1 << 1)
#define CL_PROFILING_COMMAND_QUEUED 0x1280
#define CL_PROFILING_COMMAND_SUBMIT 0x1281
#define CL_PROFILING_COMMAND_START 0x1282
#define CL_PROFILING_COMMAND_END 0x1283

extern "C" {
cl_int clGetPlatformIDs(cl_uint, cl_platform_id*, cl_uint*);
cl_int clGetDeviceIDs(cl_platform_id, cl_device_type, cl_uint, cl_device_id*, cl_uint*);
cl_int clGetDeviceInfo(cl_device_id, cl_device_info, std::size_t, void*, std::size_t*);
cl_context clCreateContext(const cl_context_properties*, cl_uint, const cl_device_id*,
                           void (CL_CALLBACK*)(const char*, const void*, std::size_t, void*), void*, cl_int*);
cl_int clReleaseContext(cl_context);
cl_program clCreateProgramWithBinary(cl_context, cl_uint, const cl_device_id*, const std::size_t*,
                                     const unsigned char**, cl_int*, cl_int*);
cl_int clBuildProgram(cl_program, cl_uint, const cl_device_id*, const char*,
                      void (CL_CALLBACK*)(cl_program, void*), void*);
cl_int clReleaseProgram(cl_program);
cl_command_queue clCreateCommandQueue(cl_context, cl_device_id, cl_command_queue_properties, cl_int*);
cl_int clReleaseCommandQueue(cl_command_queue);
cl_kernel clCreateKernel(cl_program, const char*, cl_int*);
cl_int clReleaseKernel(cl_kernel);
cl_mem clCreateBuffer(cl_context, cl_mem_flags, std::size_t, void*, cl_int*);
cl_int clReleaseMemObject(cl_mem);
cl_int clSetKernelArg(cl_kernel, cl_uint, std::size_t, const void*);
cl_int clEnqueueWriteBuffer(cl_command_queue, cl_mem, cl_bool, std::size_t, std::size_t, const void*,
                            cl_uint, const cl_event*, cl_event*);
cl_int clEnqueueFillBuffer(cl_command_queue, cl_mem, const void*, std::size_t, std::size_t, std::size_t,
                           cl_uint, const cl_event*, cl_event*);
cl_int clEnqueueNDRangeKernel(cl_command_queue, cl_kernel, cl_uint, const std::size_t*, const std::size_t*,
                              const std::size_t*, cl_uint, const cl_event*, cl_event*);
cl_int clEnqueueReadBuffer(cl_command_queue, cl_mem, cl_bool, std::size_t, std::size_t, void*,
                           cl_uint, const cl_event*, cl_event*);
void* clEnqueueMapBuffer(cl_command_queue, cl_mem, cl_bool, cl_map_flags, std::size_t, std::size_t,
                         cl_uint, const cl_event*, cl_event*, cl_int*);
cl_int clEnqueueUnmapMemObject(cl_command_queue, cl_mem, void*, cl_uint, const cl_event*, cl_event*);
cl_int clFinish(cl_command_queue);
cl_int clGetEventProfilingInfo(cl_event, cl_profiling_info, std::size_t, void*, std::size_t*);
cl_int clReleaseEvent(cl_event);
}
