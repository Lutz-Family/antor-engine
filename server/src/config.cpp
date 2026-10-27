#include "antor/config.hpp"

#include <charconv>
#include <string>

namespace antor {
namespace {

const char kUsage[] =
    "usage: antor-server [--bind <ipv4>] [--port <0-65535>] [--shard <u32>] [--tick-ms <1-60000>]\n"
    "\n"
    "  --bind     IPv4 address to listen on (default 127.0.0.1)\n"
    "  --port     TCP port, 0 for an ephemeral port (default 7777)\n"
    "  --shard    Shard id recorded on the zone (default 1)\n"
    "  --tick-ms  Fixed simulation period in milliseconds (default 50)\n";

template <typename T> bool parse_number(const std::string& text, T& out) {
    if (text.empty()) {
        return false;
    }
    T value{};
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    const auto parsed = std::from_chars(begin, end, value);
    if (parsed.ec != std::errc{} || parsed.ptr != end) {
        return false;
    }
    out = value;
    return true;
}

} // namespace

const char* usage_text() {
    return kUsage;
}

ParseResult parse_args(int argc, char** argv) {
    ParseResult result;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i] == nullptr ? std::string{} : std::string{argv[i]};
        const auto take = [&](const char* name, std::string& value) -> bool {
            if (i + 1 >= argc || argv[i + 1] == nullptr) {
                result.status = ParseStatus::Error;
                result.error = std::string{"missing value for "} + name;
                return false;
            }
            value = argv[++i];
            return true;
        };

        if (arg == "--help" || arg == "-h") {
            result.status = ParseStatus::Help;
            return result;
        }
        if (arg == "--bind") {
            std::string value;
            if (!take("--bind", value)) {
                return result;
            }
            if (value.empty()) {
                result.status = ParseStatus::Error;
                result.error = "bind host is empty";
                return result;
            }
            result.config.bind_host = std::move(value);
            continue;
        }
        if (arg == "--port") {
            std::string value;
            if (!take("--port", value)) {
                return result;
            }
            std::uint16_t port = 0;
            if (!parse_number(value, port)) {
                result.status = ParseStatus::Error;
                result.error = "port must be an integer from 0 to 65535";
                return result;
            }
            result.config.port = port;
            continue;
        }
        if (arg == "--shard") {
            std::string value;
            if (!take("--shard", value)) {
                return result;
            }
            std::uint32_t shard = 0;
            if (!parse_number(value, shard)) {
                result.status = ParseStatus::Error;
                result.error = "shard must be an unsigned 32-bit integer";
                return result;
            }
            result.config.shard_id = shard;
            continue;
        }
        if (arg == "--tick-ms") {
            std::string value;
            if (!take("--tick-ms", value)) {
                return result;
            }
            int tick_ms = 0;
            if (!parse_number(value, tick_ms) || tick_ms < 1 || tick_ms > 60000) {
                result.status = ParseStatus::Error;
                result.error = "tick-ms must be an integer from 1 to 60000";
                return result;
            }
            result.config.tick = std::chrono::milliseconds{tick_ms};
            continue;
        }
        result.status = ParseStatus::Error;
        result.error = "unknown argument: " + arg;
        return result;
    }
    return result;
}

} // namespace antor
