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
    listenser.set_idle_task([&handler] {
        handler->process_documents_if_ready();
        handler->run_scheduled_task();
    }, 1000);

    // Process the interrupt signal
    set_signal_handler(SIGINT, [&listenser, &app](int sig) {
        log::info("Interrupt signal ({}) received.", sig);
        log::info("Performing cleanup tasks...");
        listenser.stop_listening();
        app.exit();
    });

    listenser.async_listen();
    app.exec();
}

// #include "mount_manager.h"

// int main() {
//     anything::mount_manager manager;
//     manager.update();

//     manager.update_mount_points();
// }

// #include <iostream>

// #include "anything/core/disk_scanner.h"
// #include "anything/core/file_index_manager.h"
// #include "anything/utils/print_helper.h"

// using namespace anything;

// int main()
// {
//     int directories = 0;
//     int files = 0;
//     file_index_manager index_manager{ "/home/dxnu/log-files/index-temp-dir" };

//     if (!index_manager.indexed()) {
//         auto start = std::chrono::high_resolution_clock::now();

//         disk_scanner scanner;
//         // scanner.scan("/data/home", [&directories, &files, &index_manager](auto record) {
//         //     if (record.is_directory) {
//         //         directories++;
//         //     } else {
//         //         files++;
//         //     }

//         //     // print(record);
//         //     index_manager.add_index_delay(std::move(record));
//         // });
//         [[maybe_unused]] auto records = scanner.parallel_scan("/data/home");
//         files = records.size();
//         int count = 0;
//         for (auto&& record : records) {
//             index_manager.add_index_delay(std::move(record));
//             if (++count % 200 == 0) {
//                 std::this_thread::sleep_for(std::chrono::milliseconds(50));
//             }
//         }

//         auto end = std::chrono::high_resolution_clock::now();
//         std::chrono::duration<double> duration = end - start;
//         std::cout << "Scan time: " << duration.count() << " seconds\n";
//         std::cout << "Files: " << files << " Directories: " << directories << "\n";
//     }

//     std::cout << "Document size: " << index_manager.document_size() << "\n";
//     // index_manager.search_index("index");
//     // std::cout << "---------------------------------\n";
//     // index_manager.search_index("/data/home/dxnu/dxnu-obsidian/Index", true);
//     // std::cout << "---------------------------------\n";
//     // index_manager.search_index("/data/home/dxnu/dxnu-obsidian/Project", true, true);
//     // std::cout << "---------------------------------\n";
//     // index_manager.test(L"/data/home/dxnu/dxnu-obsidian/Index");
// }


// #include <cppjieba/MPSegment.hpp>
// #include <cppjieba/HMMSegment.hpp>
// #include <cppjieba/MixSegment.hpp>
// #include <cppjieba/FullSegment.hpp>
// #include <cppjieba/QuerySegment.hpp>
// #include <iostream>
// #include <vector>
// #include <string>
// #include <chrono>

// // 定义字典路径
// const char* const DICT_PATH = "/home/dxnu/dxnu-github/deepin-anything/src/dict/jieba.dict.utf8";
// const char* const HMM_PATH = "/home/dxnu/dxnu-github/deepin-anything/src/dict/hmm_model.utf8";
// const char* const USER_DICT_PATH = "/home/dxnu/dxnu-github/deepin-anything/src/dict/user.dict.utf8";
// const char* const IDF_PATH = "/home/dxnu/dxnu-github/deepin-anything/src/dict/idf.utf8";
// const char* const STOP_WORD_PATH = "/home/dxnu/dxnu-github/deepin-anything/src/dict/stop_words.utf8";

// void measureSegmentTime(const std::string& sentence, const std::string& segment_name, const std::function<void(const std::string&)>& segment_func) {
//     auto start = std::chrono::high_resolution_clock::now();
//     segment_func(sentence);
//     auto end = std::chrono::high_resolution_clock::now();
//     auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
//     std::cout << segment_name << " 耗时: " << duration.count() << " 微秒\n";
// }

// void mpSegmentExample(const std::string& sentence) {
//     cppjieba::MPSegment segment(DICT_PATH);
//     std::vector<std::string> words;
//     segment.Cut(sentence, words);
// }

// void hmmSegmentExample(const std::string& sentence) {
//     cppjieba::HMMSegment segment(HMM_PATH);
//     std::vector<std::string> words;
//     segment.Cut(sentence, words);
// }

// void mixSegmentExample(const std::string& sentence) {
//     cppjieba::MixSegment segment(DICT_PATH, HMM_PATH);
//     std::vector<std::string> words;
//     segment.Cut(sentence, words);
// }

// void fullSegmentExample(const std::string& sentence) {
//     cppjieba::FullSegment segment(DICT_PATH);
//     std::vector<std::string> words;
//     segment.Cut(sentence, words);
// }

// void querySegmentExample(const std::string& sentence) {
//     cppjieba::QuerySegment segment(DICT_PATH, HMM_PATH, USER_DICT_PATH);
//     std::vector<std::string> words;
//     segment.Cut(sentence, words);
// }

// int main() {
//     std::string sentence = "/data/home/dxnu/scripts/bench-test-100.txt今天天气不错";
//     std::cout << "原句: " << sentence << "\n";

//     measureSegmentTime(sentence, "MPSegment", mpSegmentExample);
//     measureSegmentTime(sentence, "HMMSegment", hmmSegmentExample);
//     measureSegmentTime(sentence, "MixSegment", mixSegmentExample);
//     measureSegmentTime(sentence, "FullSegment", fullSegmentExample);
//     measureSegmentTime(sentence, "QuerySegment", querySegmentExample);

//     return 0;
// }