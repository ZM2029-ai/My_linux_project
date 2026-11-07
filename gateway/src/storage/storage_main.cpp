#include "device_store.hpp"
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <stdexcept>

namespace {
int usage()
{
    std::cerr << "Usage: edgelink_db DATABASE list\n"
              << "       edgelink_db DATABASE show DEVICE_ID\n"
              << "       edgelink_db DATABASE rename DEVICE_ID NAME\n"
              << "       edgelink_db DATABASE history DEVICE_ID START END [LIMIT]\n"
              << "START and END: YYYY-MM-DD HH:MM:SS (Beijing time), range [START, END)\n";
    return 1;
}

void printDevice(const StoredDevice &device)
{
    std::cout << device.device_id << '\t' << device.display_name << '\t' << device.presence
              << '\t' << device.registered_at << '\t' << device.last_received_at << '\n';
}

int historyLimit(const char *value)
{
    char *end = nullptr;
    const long limit = std::strtol(value, &end, 10);
    if (end == value || *end || limit < 1 || limit > 1000)
        throw std::invalid_argument("history limit must be 1-1000");
    return static_cast<int>(limit);
}
}

int main(int argc, char **argv)
{
    if (argc < 3) return usage();
    const std::string command = argv[2];
    const bool list = command == "list" && argc == 3;
    const bool show = command == "show" && argc == 4;
    const bool rename = command == "rename" && argc == 5;
    const bool history = command == "history" && (argc == 6 || argc == 7);
    if (!list && !show && !rename && !history) return usage();

    try {
        DeviceStore store(argv[1], rename ? DeviceStore::OpenMode::ReadWrite : DeviceStore::OpenMode::ReadOnly);
        if (rename) {
            if (store.rename(argv[3], argv[4], beijingTimestampNow())) return 0;
            std::cerr << "[storage] device not found: " << argv[3] << '\n';
            return 2;
        }
        if (history) {
            const int limit = argc == 7 ? historyLimit(argv[6]) : 100;
            const auto samples = store.history(argv[3], argv[4], argv[5], limit);
            std::cout << "id\tsampled_at\treceived_at\ttemperature_c\thumidity_percent\n" << std::setprecision(15);
            for (const auto &row : samples) {
                std::cout << row.id << '\t' << row.sample.sampled_at << '\t' << row.received_at
                          << '\t' << row.sample.temperature_c << '\t' << row.sample.humidity_percent << '\n';
            }
            return 0;
        }
        StoredDevice device;
        if (show && !store.find(argv[3], device)) {
            std::cerr << "[storage] device not found: " << argv[3] << '\n';
            return 2;
        }
        std::cout << "device_id\tdisplay_name\tpresence\tregistered_at\tlast_received_at\n";
        if (show) {
            printDevice(device);
        } else {
            for (const auto &row : store.list()) printDevice(row);
        }
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "[storage] " << argv[1] << ": " << error.what() << '\n';
        return 1;
    }
}
