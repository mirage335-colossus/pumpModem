#pragma once

namespace datapump::gui {
struct Launch;
// Terminal host for the shared application presentation. No modem or screen
// identity crosses this interface.
int run_terminal(Launch);
}
