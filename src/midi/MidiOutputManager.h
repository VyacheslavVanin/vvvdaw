#pragma once
#include <QString>
#include <vector>
#include <memory>
#include <cstdint>

class MidiOutputManager {
public:
    MidiOutputManager();
    ~MidiOutputManager();

    struct Device {
        int id = -1;
        QString name;
    };

    static std::vector<Device> enumerateOutputDevices();

    void open(int deviceId);
    void close(int deviceId);
    void closeAll();

    void send(int deviceId, uint8_t status, uint8_t data1, uint8_t data2);
    void sendAllNotesOff(int deviceId);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
