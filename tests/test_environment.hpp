#pragma once

#include <cstdlib>
#include <stdexcept>
#include <string>

namespace datapump::testing {
// A null value removes the process-local test setting on either platform.
inline void set_environment(const char* key,const char* value) {
#ifdef _WIN32
    const int result=::_putenv_s(key,value?value:"");
#else
    const int result=value?::setenv(key,value,1) : ::unsetenv(key);
#endif
    if(result!=0)throw std::runtime_error(std::string("cannot update test environment: ")+key);
}
}
