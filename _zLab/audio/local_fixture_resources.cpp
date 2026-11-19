// Test-only fixture memory report; native helper links the real OS probes.
#include "datapump/execution_resources.hpp"
namespace datapump::execution {
HostResources host_resources(bool) {return {4,4ULL*1024*1024*1024};}
std::size_t process_footprint_bytes(){return 0;}
std::size_t worker_stack_bytes(){return 8*1024*1024;}
}
