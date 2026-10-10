#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

int main() {
    const char* platform = std::getenv("QT_QPA_PLATFORM");
    const char* path = std::getenv("PATH");
    const char* systemRoot = std::getenv("SystemRoot");
    if (platform == nullptr || std::string(platform) != "offscreen" || path == nullptr ||
        systemRoot == nullptr ||
        std::string(path) != std::string(systemRoot) + "/system32;" + systemRoot) {
        std::cerr << "smoke environment was not isolated\n";
        return 24;
    }
    std::cout << std::filesystem::current_path().string() << std::endl;
    if (std::getenv("HATTEDA_SMOKE_FAKE_EXIT") != nullptr) {
        std::cerr << "intentional startup failure\n";
        return 23;
    }
    std::this_thread::sleep_for(std::chrono::seconds(30));
    return 0;
}
