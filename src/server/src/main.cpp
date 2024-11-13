#include <QCoreApplication>

#include "anything.hpp"

using namespace anything;

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    log::set_level(log::level::all, true);

    event_listenser listenser;
    auto handler = std::make_shared<default_event_handler>();
    listenser.set_handler([&handler](fs_event event) {
        handler->handle(std::move(event));
    });

    // Process the interrupt signal
    set_signal_handler(SIGINT, [&listenser, &handler, &app](int sig) {
        log::info("Interrupt signal ({}) received.", sig);
        log::info("Performing cleanup tasks...");
        listenser.stop_listening();
        handler->terminate_processing();
        app.exit();
    });

    listenser.async_listen();
    app.exec();
}