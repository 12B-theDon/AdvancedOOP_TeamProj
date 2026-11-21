#pragma once

#include <exception>
#include <functional>

namespace engine {

class ExportCancelled final : public std::exception {
public:
    const char* what() const noexcept override { return "Export stopped."; }
};

// Shared by raw export and reports. Callbacks run on the export thread.
struct ExportControl {
    std::function<bool()> cancelled;
    std::function<void()> advance;
    void Check() const { if (cancelled && cancelled()) throw ExportCancelled(); }
    void Step() const { Check(); if (advance) advance(); }
};

} // namespace engine
