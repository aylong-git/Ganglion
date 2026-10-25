#include "EEGRecorder.h"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <vector>

EEGRecorder::EEGRecorder() = default;

EEGRecorder::~EEGRecorder() {
    Stop();
}

bool EEGRecorder::Start(const std::string& filepath) {
    Stop();

    {
        std::lock_guard<std::mutex> lock(mtx_);
        // Binary mode so Windows does not turn \n into CRLF.
        file_.open(filepath, std::ios::out | std::ios::trunc | std::ios::binary);
        if (!file_.is_open()) {
            std::cerr << "[EEG Record] Failed to open '" << filepath << "'\n";
            return false;
        }
        file_ << "Channel_1,Channel_2,Channel_3,Channel_4\n";
        file_.flush();
    }

    running_.store(true);
    recording_.store(true);
    writer_ = std::thread(&EEGRecorder::WriterLoop, this);
    std::cout << "[EEG Record] Writing " << filepath << "\n";
    return true;
}

void EEGRecorder::Stop() {
    recording_.store(false);
    running_.store(false);
    cv_.notify_all();
    if (writer_.joinable()) {
        writer_.join();
    }

    std::lock_guard<std::mutex> lock(mtx_);
    if (file_.is_open()) {
        file_.flush();
        file_.close();
    }
    std::queue<Row> empty;
    queue_.swap(empty);
}

void EEGRecorder::PushSample(float ch1, float ch2, float ch3, float ch4) {
    if (!recording_.load()) return;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (queue_.size() > 200000) return; // drop rather than grow unbounded
        queue_.push(Row{ { ch1, ch2, ch3, ch4 } });
    }
    cv_.notify_one();
}

void EEGRecorder::WriterLoop() {
    std::vector<Row> batch;
    batch.reserve(256);

    while (true) {
        {
            std::unique_lock<std::mutex> lock(mtx_);
            cv_.wait_for(lock, std::chrono::milliseconds(50), [&] {
                return !queue_.empty() || !running_.load();
            });

            while (!queue_.empty()) {
                batch.push_back(queue_.front());
                queue_.pop();
            }

            if (!running_.load() && queue_.empty() && batch.empty()) {
                break;
            }
        }

        if (!batch.empty() && file_.is_open()) {
            file_ << std::setprecision(6);
            for (const Row& row : batch) {
                file_ << row.v[0] << ',' << row.v[1] << ',' << row.v[2] << ',' << row.v[3] << '\n';
            }
            file_.flush();
            batch.clear();
        }

        if (!running_.load()) {
            std::lock_guard<std::mutex> lock(mtx_);
            if (queue_.empty()) break;
        }
    }
}
