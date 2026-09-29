#pragma once
#include <span>

namespace datapump::host {
// Installs an inherited, thread-wide kernel prohibition on every socket syscall
// family, alternative asynchronous dispatch and descriptor stealing. Unsupported
// platforms fail closed; a loopback convention is never a substitute.
void install_no_socket_boundary();
void verify_anonymous_pipe(int descriptor,bool input);
// Only call before application threads exist. Validate retained descriptors
// first; close every other inherited descriptor before processing host input.
void restrict_descriptors(std::span<const int> retained);
bool no_socket_boundary_active();
}
