#pragma once
#include <cstdint>

struct WinEvent {

    enum Type {
        Null,
        Create, Destroy, Close,
        Focus, Defocus,
        Move, Resize, Maximize, Minimize, Restore,
        Scale, Clear, Paint,
        MouseButton, MouseMove, MouseWheel, CaptureLost,
        Keyboard, Character
    };

    Type type;
    uint64_t a, b;
    int64_t c, d;

    bool rejected = false;
    void* subject = nullptr;

    void reject() {
        this->rejected = true;
    }
};