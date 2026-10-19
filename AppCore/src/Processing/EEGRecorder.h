#pragma once

#include <array>
#include <atomic>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

class EEGRecorder {
public:
    EEGRecorder();
    ~EEGRecorder();

    bool Start(const std::string& filepath);
    void Stop();
    bool IsRecording() const { return recording_.load(); }
    void PushSample(float ch1, float ch2, float ch3, float ch4);

private:
    struct Row {
        std::array<float, 4> v{};
    };

    void WriterLoop();

    std::ofstream file_;
    std::mutex mtx_;
    std::condition_variable cv_;
    std::queue<Row> queue_;
    std::thread writer_;
    std::atomic<bool> recording_{ false };
    std::atomic<bool> running_{ false };
};
