#include "test_harness.hpp"

void test_config();
void test_spsc_queue();
void test_tick_clock();
void test_frame();
void test_codec();
void test_interest();
void test_session();
void test_session_bus_overflow();
void test_loopback_handshake();

int main() {
    int failed = 0;
    failed += antor::test::run_test("config", test_config);
    failed += antor::test::run_test("spsc_queue", test_spsc_queue);
    failed += antor::test::run_test("tick_clock", test_tick_clock);
    failed += antor::test::run_test("frame", test_frame);
    failed += antor::test::run_test("codec", test_codec);
    failed += antor::test::run_test("interest", test_interest);
    failed += antor::test::run_test("session", test_session);
    failed += antor::test::run_test("session_bus", test_session_bus_overflow);
    failed += antor::test::run_test("loopback", test_loopback_handshake);
    return failed == 0 ? 0 : 1;
}
