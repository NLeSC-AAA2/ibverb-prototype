#include <fstream>
#include <iostream>
#include "opencl_utils.hpp"

std::string errorMessage(cl_int error)
{
    switch (error) {
        case CL_SUCCESS:                            return "Success!";
        case CL_DEVICE_NOT_FOUND:                   return "Device not found.";
        case CL_DEVICE_NOT_AVAILABLE:               return "Device not available";
        case CL_COMPILER_NOT_AVAILABLE:             return "Compiler not available";
        case CL_MEM_OBJECT_ALLOCATION_FAILURE:      return "Memory object allocation failure";
        case CL_OUT_OF_RESOURCES:                   return "Out of resources";
        case CL_OUT_OF_HOST_MEMORY:                 return "Out of host memory";
        case CL_PROFILING_INFO_NOT_AVAILABLE:       return "Profiling information not available";
        case CL_MEM_COPY_OVERLAP:                   return "Memory copy overlap";
        case CL_IMAGE_FORMAT_MISMATCH:              return "Image format mismatch";
        case CL_IMAGE_FORMAT_NOT_SUPPORTED:         return "Image format not supported";
        case CL_BUILD_PROGRAM_FAILURE:              return "Program build failure";
        case CL_MAP_FAILURE:                        return "Map failure";
        case CL_INVALID_VALUE:                      return "Invalid value";
        case CL_INVALID_DEVICE_TYPE:                return "Invalid device type";
        case CL_INVALID_PLATFORM:                   return "Invalid platform";
        case CL_INVALID_DEVICE:                     return "Invalid device";
        case CL_INVALID_CONTEXT:                    return "Invalid context";
        case CL_INVALID_QUEUE_PROPERTIES:           return "Invalid queue properties";
        case CL_INVALID_COMMAND_QUEUE:              return "Invalid command queue";
        case CL_INVALID_HOST_PTR:                   return "Invalid host pointer";
        case CL_INVALID_MEM_OBJECT:                 return "Invalid memory object";
        case CL_INVALID_IMAGE_FORMAT_DESCRIPTOR:    return "Invalid image format descriptor";
        case CL_INVALID_IMAGE_SIZE:                 return "Invalid image size";
        case CL_INVALID_SAMPLER:                    return "Invalid sampler";
        case CL_INVALID_BINARY:                     return "Invalid binary";
        case CL_INVALID_BUILD_OPTIONS:              return "Invalid build options";
        case CL_INVALID_PROGRAM:                    return "Invalid program";
        case CL_INVALID_PROGRAM_EXECUTABLE:         return "Invalid program executable";
        case CL_INVALID_KERNEL_NAME:                return "Invalid kernel name";
        case CL_INVALID_KERNEL_DEFINITION:          return "Invalid kernel definition";
        case CL_INVALID_KERNEL:                     return "Invalid kernel";
        case CL_INVALID_ARG_INDEX:                  return "Invalid argument index";
        case CL_INVALID_ARG_VALUE:                  return "Invalid argument value";
        case CL_INVALID_ARG_SIZE:                   return "Invalid argument size";
        case CL_INVALID_KERNEL_ARGS:                return "Invalid kernel arguments";
        case CL_INVALID_WORK_DIMENSION:             return "Invalid work dimension";
        case CL_INVALID_WORK_GROUP_SIZE:            return "Invalid work group size";
        case CL_INVALID_WORK_ITEM_SIZE:             return "Invalid work item size";
        case CL_INVALID_GLOBAL_OFFSET:              return "Invalid global offset";
        case CL_INVALID_EVENT_WAIT_LIST:            return "Invalid event wait list";
        case CL_INVALID_EVENT:                      return "Invalid event";
        case CL_INVALID_OPERATION:                  return "Invalid operation";
        case CL_INVALID_GL_OBJECT:                  return "Invalid OpenGL object";
        case CL_INVALID_BUFFER_SIZE:                return "Invalid buffer size";
        case CL_INVALID_MIP_LEVEL:                  return "Invalid mip-map level";
        case CL_INVALID_GLOBAL_WORK_SIZE:           return "Invalid global work size";
#if defined CL_INVALID_PROPERTY
        case CL_INVALID_PROPERTY:                   return "Invalid property";
#endif
#if defined CL_INVALID_IMAGE_DESCRIPTOR
        case CL_INVALID_IMAGE_DESCRIPTOR:           return "Invalid image descriptor";
#endif
#if defined CL_INVALID_COMPILER_OPTIONS
        case CL_INVALID_COMPILER_OPTIONS:           return "Invalid compiler options";
#endif
#if defined CL_INVALID_LINKER_OPTIONS
        case CL_INVALID_LINKER_OPTIONS:             return "Invalid linker options";
#endif
#if defined CL_INVALID_DEVICE_PARTITION_COUNT
        case CL_INVALID_DEVICE_PARTITION_COUNT:     return "Invalid device partition count";
#endif
        default:                                    return std::string("Unknown (") + std::to_string(error) + std::string(")");
  }
}

cl::Context
Kernel::getContext(const cl::Program& prog)
{
    cl::Context result;
    prog.getInfo(CL_PROGRAM_CONTEXT, &result);
    return result;
}

Kernel::Kernel(const cl::Program& prog, std::string name)
    : kernel(prog, name.c_str())
    , queue(getContext(prog))
{}

void
Kernel::finish()
{ event.wait(); }

void
createContext(cl::Context &context, std::vector<cl::Device> &devices)
{
  const char *platformName = getenv("PLATFORM");

  if (platformName == NULL) {
    platformName = "Intel(R) FPGA SDK for OpenCL(TM)";
  }

  cl_device_type type = CL_DEVICE_TYPE_DEFAULT;

  const char *deviceType = getenv("TYPE");

  if (deviceType != 0) {
    if (strcmp(deviceType, "GPU") == 0)
      type = CL_DEVICE_TYPE_GPU;
    else if (strcmp(deviceType, "CPU") == 0)
      type = CL_DEVICE_TYPE_CPU;
    else if (strcmp(deviceType, "ACCELERATOR") == 0)
      type = CL_DEVICE_TYPE_ACCELERATOR;
    else
      std::cerr << "Unrecognized device type: " << deviceType;
  }

  std::vector<cl::Platform> platforms;
  cl::Platform::get(&platforms);

  for (cl::Platform &platform : platforms) {
    std::cout << "Platform name: " << platform.getInfo<CL_PLATFORM_NAME>() << std::endl;
    std::cout << "Platform version: " << platform.getInfo<CL_PLATFORM_VERSION>() << std::endl;
    std::cout << "Platform extensions: " << platform.getInfo<CL_PLATFORM_EXTENSIONS>() << std::endl;
  }

  for (cl::Platform &platform : platforms) {
    if (strcmp(platform.getInfo<CL_PLATFORM_NAME>().c_str(), platformName) == 0) {
      platform.getDevices(type, &devices);

      for (cl::Device &device : devices) {
        std::cout
            << "Device: " << device.getInfo<CL_DEVICE_NAME>() << ", "
            << "mem: " << device.getInfo<CL_DEVICE_GLOBAL_MEM_SIZE>() / (1024 * 1024)
            << " MB, max alloc: "
            << device.getInfo<CL_DEVICE_MAX_MEM_ALLOC_SIZE>() / (1024 * 1024)
            << " MB"
            << std::endl;
      }

      context = cl::Context(devices);
      return;
    }
  }

  std::cerr << "Platform not found: \"" << platformName << '"' << std::endl;
  exit(1);
}

cl::Program
createProgramFromBinaries
( cl::Context &context
, std::vector<cl::Device> &devices
, const std::string &name
)
{
  std::ifstream ifs(name, std::ios::in | std::ios::binary);
  std::string str((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

  cl::Program::Binaries binaries{std::vector<unsigned char>(str.begin(), str.end())};
  return cl::Program(context, devices, binaries);
}

cl::Program
get_program
( cl::Context& context
, cl::Device& device
, std::string const &filename
)
{
    std::cout << ">>> Loading program from binary: " << filename << std::endl;
    try {
        std::vector<cl::Device> devices;
        devices.push_back(device);
        auto result = createProgramFromBinaries(context, devices, filename);
        std::cout << std::endl;
        return result;
    } catch (cl::Error& error) {
        std::cerr << "Loading binary failed: " << error.what() << std::endl
             << "Error code: " << error.err() << std::endl
             << "Error message: " << errorMessage(error.err()) << std::endl;
        exit(EXIT_FAILURE);
    }
}
