// Build-time discovery, performed with the production worker's socket filter.
#include "datapump/host/sandbox.hpp"

#include <cuda_runtime_api.h>
#include <cstdio>

int main() {
    datapump::host::install_no_socket_boundary();
    int count = 0;
    auto status = cudaGetDeviceCount(&count);
    if (status != cudaSuccess || count == 0) {
        std::fprintf(stderr, "CUDA worker discovery: %s, %d devices\n",
                     cudaGetErrorString(status), count);
        return 2;
    }
    return 0;
}
