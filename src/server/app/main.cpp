#include <iostream>
#include "default_event_handler.h"
#include "event_listenser.h"
#include "service_manager.h"
#include "signal_handler.h"


int main() {
    anything::service_manager manager;
    auto ret = manager.register_service("com.deepin.anything");
    if (!ret) {
        std::cerr << "Failed to register service\n";
        return -1;
    }

    std::cout << "register service succeed\n";

    anything::default_event_handler handler;
    anything::event_listenser listenser;
    listenser.set_handler([&handler](anything::fs_event event) {
        handler.handle(std::move(event));
    });

    anything::set_signal_handler(SIGINT, [&listenser](int sig) {
        std::cout << "\nInterrupt signal (" << sig << ") received.\n";
        std::cout << "Performing cleanup tasks...\n";
        listenser.stop_listening();
    });

    listenser.start_listening();

    std::cout << "Exit the anything\n";
}

// #include "mount_manager.h"

// int main() {
//     anything::mount_manager manager;
//     manager.update();

//     manager.update_mount_points();
// }

// #include <iostream>

// #include "disk_scanner.h"
// #include "file_index_manager.h"
// #include "print_helper.h"

// using namespace anything;


// int main() {
//     int directories = 0;
//     int files = 0;
//     file_index_manager index_manager{ "/home/dxnu/log-files/index-test-dir" };

//     if (!index_manager.indexed()) {
//         auto start = std::chrono::high_resolution_clock::now();

//         disk_scanner scanner;
//         scanner.scan("/data/home/dxnu/dxnu-obsidian", [&directories, &files, &index_manager](auto record) {
//             if (record.is_directory) {
//                 directories++;
//             } else {
//                 files++;
//             }

//             print(record);
//             // index_manager.add_index(std::move(record));
//         });

//         auto end = std::chrono::high_resolution_clock::now();
//         std::chrono::duration<double> duration = end - start;
//         std::cout << "Scan time: " << duration.count() << " seconds\n";
//         std::cout << "Files: " << files << " Directories: " << directories << "\n";
//     }

//     std::cout << "Document size: " << index_manager.document_size() << "\n";
//     index_manager.search_index("index");
//     std::cout << "---------------------------------\n";
//     index_manager.search_index("/data/home/dxnu/dxnu-obsidian/Index", true);
//     std::cout << "---------------------------------\n";
//     index_manager.search_index("/data/home/dxnu/dxnu-obsidian/Project", true, true);
//     std::cout << "---------------------------------\n";
//     index_manager.test(L"/data/home/dxnu/dxnu-obsidian/Index");
// }