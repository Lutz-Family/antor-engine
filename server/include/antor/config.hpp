#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace antor {

struct ServerConfig {
    std::string bind_host = "127.0.0.1";
    std::uint16_t port = 7777;
    std::uint32_t shard_id = 1;
    std::chrono::milliseconds tick{50};
};

enum class ParseStatus { Ok, Help, Error };

struct ParseResult {
    ParseStatus status = ParseStatus::Ok;
    ServerConfig config;
    std::string error;
};

const char* usage_text();
ParseResult parse_args(int argc, char** argv);

} // namespace antor
