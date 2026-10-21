#pragma once
#include "datapump/live.hpp"
#include <filesystem>
#include <string>
namespace datapump::diagnostic::copied_help_session {
struct Context {
    live::Settings settings;
    std::string identity_json; // Root/Sol provides one complete valid JSON object; no key bytes.
};
Context make_context(const std::filesystem::path& retained_keyfile);
void arm_probes(); // Root/Sol overlay wrapper, called once before Session start.
}
