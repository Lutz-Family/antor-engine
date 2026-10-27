#include "antor/config.hpp"
#include "test_harness.hpp"

void test_config() {
    {
        char program[] = "antor-server";
        char* argv[] = {program};
        const auto parsed = antor::parse_args(1, argv);
        EXPECT(parsed.status == antor::ParseStatus::Ok);
        EXPECT(parsed.config.bind_host == "127.0.0.1");
        EXPECT(parsed.config.port == 7777);
        EXPECT(parsed.config.shard_id == 1);
        EXPECT(parsed.config.tick.count() == 50);
    }
    {
        char program[] = "antor-server";
        char port_flag[] = "--port";
        char port[] = "0";
        char shard_flag[] = "--shard";
        char shard[] = "7";
        char tick_flag[] = "--tick-ms";
        char tick[] = "20";
        char bind_flag[] = "--bind";
        char bind[] = "127.0.0.1";
        char* argv[] = {
            program, port_flag, port, shard_flag, shard, tick_flag, tick, bind_flag, bind};
        const auto parsed = antor::parse_args(9, argv);
        EXPECT(parsed.status == antor::ParseStatus::Ok);
        EXPECT(parsed.config.port == 0);
        EXPECT(parsed.config.shard_id == 7);
        EXPECT(parsed.config.tick.count() == 20);
        EXPECT(parsed.config.bind_host == "127.0.0.1");
    }
    {
        char program[] = "antor-server";
        char help[] = "--help";
        char* argv[] = {program, help};
        const auto parsed = antor::parse_args(2, argv);
        EXPECT(parsed.status == antor::ParseStatus::Help);
    }
    {
        char program[] = "antor-server";
        char port_flag[] = "--port";
        char port[] = "70000";
        char* argv[] = {program, port_flag, port};
        const auto parsed = antor::parse_args(3, argv);
        EXPECT(parsed.status == antor::ParseStatus::Error);
    }
    {
        char program[] = "antor-server";
        char tick_flag[] = "--tick-ms";
        char tick[] = "0";
        char* argv[] = {program, tick_flag, tick};
        const auto parsed = antor::parse_args(3, argv);
        EXPECT(parsed.status == antor::ParseStatus::Error);
    }
    {
        char program[] = "antor-server";
        char unknown[] = "--nope";
        char* argv[] = {program, unknown};
        const auto parsed = antor::parse_args(2, argv);
        EXPECT(parsed.status == antor::ParseStatus::Error);
    }
}
