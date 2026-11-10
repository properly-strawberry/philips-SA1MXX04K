#pragma once
#include "interface.h"
class Recorder {
public:
    ~Recorder();
    void start(const std::filesystem::path& directory);
    void pause(bool value);
    void finish(bool keep);
    player::Recording snapshot();
private:
    std::mutex mutex_;
    std::thread thread_;
    player::Recording state_;
    bool stop_=false,keep_=true,paused_=false;
    void run(std::filesystem::path path);
};
