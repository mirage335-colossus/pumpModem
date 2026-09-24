#pragma once
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace datapump::gui {
struct ClipboardResult {
    // An engaged, empty string is a successful empty selection. Failure must
    // never be interpreted as a request to erase the editor's selection.
    std::optional<std::string> text;
    std::string error;
};
// Native services belong to the adapter. Clipboard reads complete from poll(),
// allowing modem reception to continue while another application responds.
class RevPlatform {
public:
    RevPlatform();
    ~RevPlatform();
    void poll();
    void copy(const std::string& text);
    void paste(std::function<void(ClipboardResult)> result);
    static void open_folder(const std::string& path);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
