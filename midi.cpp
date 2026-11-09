#include "midi.h"
#include "RtMidi.h"

struct MidiOut::Impl { std::unique_ptr<RtMidiOut> out; };

MidiOut::MidiOut() : impl_(new Impl) {
    try { impl_->out = std::make_unique<RtMidiOut>(); } catch (RtMidiError&) {}
}

MidiOut::~MidiOut() { close(); }

bool MidiOut::virtualSupported() {
#ifdef _WIN32
    return false;
#else
    return true;
#endif
}

std::vector<std::string> MidiOut::ports() const {
    std::vector<std::string> v;
    if (!impl_->out) return v;
    try {
        for (unsigned i = 0; i < impl_->out->getPortCount(); i++)
            v.push_back(impl_->out->getPortName(i));
    } catch (RtMidiError&) {}
    return v;
}

bool MidiOut::openPort(int i) {
    close();
    if (!impl_->out) return false;
    try {
        impl_->out->openPort((unsigned)i);
        name_ = impl_->out->getPortName((unsigned)i);
        open_ = true;
    } catch (RtMidiError&) {}
    return open_;
}

bool MidiOut::openVirtual(const std::string& n) {
    close();
    if (!impl_->out || !virtualSupported()) return false;
    try {
        impl_->out->openVirtualPort(n);
        name_ = n + " (virtual)";
        open_ = true;
    } catch (RtMidiError&) {}
    return open_;
}

void MidiOut::close() {
    if (open_ && impl_->out) impl_->out->closePort();
    open_ = false;
    name_.clear();
}

void MidiOut::send(uint8_t a, uint8_t b, uint8_t c) {
    if (!open_) return;
    unsigned char m[3] = {a, b, c};
    try { impl_->out->sendMessage(m, 3); } catch (RtMidiError&) {}
}

void MidiOut::send(uint8_t a) {
    if (!open_) return;
    try { impl_->out->sendMessage(&a, 1); } catch (RtMidiError&) {}
}
