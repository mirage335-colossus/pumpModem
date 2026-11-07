#include "fixture_keyfile.hpp"

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

// Test-only generator. Its synthetic audio is not entropy and the output must
// never be used as an operational keyfile. It exposes no secret key bytes.
int main(int argc, char** argv) {
    try {
        if (argc != 3 && argc != 4) throw datapump::Error("fixture usage: PATH single|multi [PAD]");
        const std::string mode(argv[2]);
        if (mode != "single" && mode != "multi") throw datapump::Error("invalid fixture mode");
        std::optional<std::filesystem::path> pad;
        if (argc == 4) pad=std::filesystem::path(argv[3]);
        const std::vector<std::string> names = mode == "single"
            ? std::vector<std::string>{"Single"} : std::vector<std::string>{"First", "Second"};
        const auto result=datapump::create_keyring(argv[1],names,pad,fixture_keyfile::generation());
        if (!result.audio_used) throw datapump::Error("fixture synthetic input was not consumed");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
