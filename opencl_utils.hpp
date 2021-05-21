/*
 * Copyright 2021 Netherlands eScience Center and ASTRON.
 * Licensed under the Apache License, version 2.0. See LICENSE for details.
 */
#ifndef OPENCL_UTILS_HPP
#define OPENCL_UTILS_HPP
#define CL_HPP_TARGET_OPENCL_VERSION 200
#define CL_HPP_ENABLE_EXCEPTIONS
#include <CL/cl2.hpp>

std::string errorMessage(cl_int error);

template <unsigned N>
void set_args_n(cl::Kernel&) {}

template <unsigned N, typename First, typename ...Rest>
void set_args_n(cl::Kernel &k, First &&first, Rest &&...rest)
{
    k.setArg(N, first);
    set_args_n<N+1>(k, std::forward<Rest>(rest)...);
}

template <typename ...Args>
void set_args(cl::Kernel &k, Args &&...args)
{
    set_args_n<0>(k, std::forward<Args>(args)...);
}

class Kernel
{
    cl::Kernel kernel;
    cl::CommandQueue queue;
    cl::Event event;

    static cl::Context getContext(const cl::Program& prog);

  public:
    Kernel(const cl::Program& prog, std::string name);

    template<typename... Args>
    void operator()(Args&&... args)
    {
        try {
            set_args(kernel, std::forward<Args>(args)...);
        } catch (const cl::Error& err) {
            std::cerr << "Error: " << err.what() << std::endl;
            std::cerr << errorMessage(err.err()) << std::endl;
            exit(EXIT_FAILURE);
        }
        queue.enqueueNDRangeKernel(kernel, cl::NullRange, cl::NDRange(1), cl::NullRange, nullptr, &event);
    }

    void finish();
};

void
createContext(cl::Context &context, std::vector<cl::Device> &devices);

cl::Program
createProgramFromBinaries
( cl::Context &context
, std::vector<cl::Device> &devices
, const std::string &name
);

cl::Program
get_program
( cl::Context& context
, cl::Device& device
, std::string const &filename
);
#endif
