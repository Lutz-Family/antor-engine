#include "antor/server.hpp"

#include <iostream>
#include <pthread.h>
#include <signal.h>
#include <system_error>

namespace {

int run_until_signal(const antor::ServerConfig& config) {
    sigset_t blocked;
    sigemptyset(&blocked);
    sigaddset(&blocked, SIGINT);
    sigaddset(&blocked, SIGTERM);
    sigaddset(&blocked, SIGPIPE);
    if (pthread_sigmask(SIG_BLOCK, &blocked, nullptr) != 0) {
        throw std::system_error(errno, std::generic_category(), "pthread_sigmask");
    }

    antor::Server server(config);
    server.start();
    std::cout << "listening " << config.bind_host << ' ' << server.port() << '\n' << std::flush;

    sigset_t waited;
    sigemptyset(&waited);
    sigaddset(&waited, SIGINT);
    sigaddset(&waited, SIGTERM);
    int signal_number = 0;
    if (sigwait(&waited, &signal_number) != 0) {
        server.request_stop();
        server.join();
        throw std::runtime_error("sigwait failed");
    }
    server.request_stop();
    server.join();
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    const antor::ParseResult parsed = antor::parse_args(argc, argv);
    if (parsed.status == antor::ParseStatus::Help) {
        std::cout << antor::usage_text();
        return 0;
    }
    if (parsed.status == antor::ParseStatus::Error) {
        std::cerr << parsed.error << '\n' << antor::usage_text();
        return 2;
    }
    try {
        return run_until_signal(parsed.config);
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}
