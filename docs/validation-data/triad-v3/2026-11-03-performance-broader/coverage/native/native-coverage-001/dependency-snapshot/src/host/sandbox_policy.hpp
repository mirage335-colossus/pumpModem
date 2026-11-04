#pragma once
#if defined(__linux__)
#include <linux/filter.h>
#include <vector>
namespace datapump::host {
// Pure policy construction, also interpreted by tests without creating sockets.
std::vector<sock_filter> no_socket_filter();
}
#endif
