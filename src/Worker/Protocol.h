#pragma once
#include <cstdint>
namespace ipc {
inline constexpr std::uint32_t Magic = 0x4b495341;
inline constexpr std::uint32_t Version = 2;
enum class Operation : std::uint32_t { Enroll = 1, Attempt = 2, Compare = 3 };
enum class Status : std::uint32_t { Normal, Crash, Mismatch };
// Fixed-width, little-endian Windows wire layout, followed by exact payload lengths.
struct Request {
    std::uint32_t magic = Magic;
    std::uint32_t version = Version;
    Operation operation = Operation::Enroll;
    std::uint32_t type = 0;
    std::uint32_t first_size = 0;
    std::uint32_t second_size = 0;
};
struct Response {
    std::uint32_t magic = Magic;
    std::uint32_t version = Version;
    Status status = Status::Normal;
    std::int32_t result = 0;
    std::int32_t error_code = 0;
    std::uint32_t data_size = 0;
    double score = 0;
    double call_time_ms = 0;
    char message[256] = {};
};
static_assert(sizeof(Request) == 24, "IPC request layout changed");
static_assert(sizeof(Response) == 296, "IPC response layout changed");
}
