#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

int main() {
    if (std::getenv("HATTEDA_SMOKE_FAKE_EXIT") != nullptr) {
        std::cerr << "intentional startup failure\n";
        return 23;
    }
    std::this_thread::sleep_for(std::chrono::seconds(30));
    return 0;
}
