#pragma once
// Thin wrapper over RtMidi. Kept in its own .cpp so RtMidi's platform headers
// (windows.h on Windows) never get included alongside raylib.
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class MidiOut {
public:
    MidiOut();
    ~MidiOut();

    static bool virtualSupported();            // true on macOS / Linux, false on Windows
    std::vector<std::string> ports() const;    // hardware / loopback destinations
    bool openPort(int index);
    bool openVirtual(const std::string& name); // shows up as an input in your DAW
    void close();
    bool isOpen() const { return open_; }
    const std::string& portName() const { return name_; }

    void send(uint8_t a, uint8_t b, uint8_t c);
    void send(uint8_t a);
    void noteOn (int ch, int note, int vel) { send(0x90 | (ch & 15), note & 127, vel & 127); }
    void noteOff(int ch, int note)          { send(0x80 | (ch & 15), note & 127, 0); }
    void clock() { send(0xF8); }
    void start() { send(0xFA); }
    void stop()  { send(0xFC); }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::string name_;
    bool open_ = false;
};
