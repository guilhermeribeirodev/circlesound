// CircleBeats - circular step sequencer (raylib) with MIDI out.
//
// Controls
//   Space ........ play / stop (starts from the cue triangle)
//   Left click ... add / remove a note on a ring (snaps to grid)
//   Right drag ... rotate a ring (snaps on release)
//   Drag centre .. move the whole circle
//   Drag triangle  move the cue point (Shift = no snap)
//   1 / 2 / 3 .... toggle 4/4, 3/4, 12/8 division lines
//   Up / Down .... BPM +/- 1 (hold Shift for 10)
//   G ............ cycle snap grid (16, 12, 8, 32)
//   M ............ cycle MIDI output (virtual port, then each hardware port)
//   C ............ toggle MIDI clock + start/stop output
//
// Extending: write a struct deriving from Component, then scene.add<MyThing>() in main().
// Anything that should react to a note firing: push a lambda onto World::onTrigger.

#include "raylib.h"
#include "raymath.h"
#include "synth.h"
#include "midi.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <vector>

constexpr float TAU_F = 6.28318530718f;

// ---------------------------------------------------------------- data

struct Track {
    std::string        name;
    Color              color;
    float              radius;
    std::vector<float> notes;       // positions 0..1 around the ring (0 = top, clockwise)
    float              rotation = 0;
    Sound              sound{};
    int                midiNote    = 60;
    int                midiChannel = 0;   // 0-15 (0 = channel 1, 9 = channel 10 drums)
};

struct World {
    Vector2 center{450, 450};
    float   bpm     = 128;
    float   phase   = 0;            // playhead, 0..1 = one bar (4 beats)
    float   cue     = 0;            // where playback starts
    bool    playing = false;
    int     grid    = 16;
    bool    showDiv[3] = {true, true, true};
    std::vector<Track> tracks;

    // Everything that should happen when a note fires (audio, MIDI, visuals...) hangs here.
    std::vector<std::function<void(const Track&)>> onTrigger;
    void fire(const Track& t) const { for (auto& f : onTrigger) f(t); }

    float outerRadius() const {
        float r = 0;
        for (auto& t : tracks) r = std::max(r, t.radius);
        return r;
    }
};

// ---------------------------------------------------------------- helpers

static float wrap01(float x) { x = std::fmod(x, 1.0f); return x < 0 ? x + 1.0f : x; }

static Vector2 pointOn(Vector2 c, float r, float pos) {
    float a = pos * TAU_F - PI / 2;
    return {c.x + std::cos(a) * r, c.y + std::sin(a) * r};
}

static float posFrom(Vector2 c, Vector2 p) {
    return wrap01((std::atan2(p.y - c.y, p.x - c.x) + PI / 2) / TAU_F);
}

static float snap(float pos, int grid) { return wrap01(std::round(pos * grid) / grid); }

// ---------------------------------------------------------------- component base

struct Component {
    virtual ~Component() = default;
    virtual void update(World&, float /*dt*/) {}
    virtual void draw(const World&) {}
    // return true to "grab" the mouse; drag() then receives moves until release.
    virtual bool press(World&, Vector2 /*mouse*/, int /*button*/) { return false; }
    virtual void drag(World&, Vector2 /*mouse*/, Vector2 /*delta*/) {}
    virtual void release(World&) {}
};

struct Scene {
    std::vector<std::unique_ptr<Component>> items;   // draw order; hit-testing goes in reverse
    Component* grabbed = nullptr;
    int        button  = -1;

    template <class T, class... A> T& add(A&&... a) {
        items.push_back(std::make_unique<T>(std::forward<A>(a)...));
        return static_cast<T&>(*items.back());
    }

    void input(World& w) {
        Vector2 m = GetMousePosition();
        if (!grabbed) {
            for (int b : {MOUSE_BUTTON_LEFT, MOUSE_BUTTON_RIGHT}) {
                if (!IsMouseButtonPressed(b)) continue;
                for (auto it = items.rbegin(); it != items.rend(); ++it)
                    if ((*it)->press(w, m, b)) { grabbed = it->get(); button = b; break; }
                if (grabbed) break;
            }
        }
        if (grabbed) {
            grabbed->drag(w, m, GetMouseDelta());
            if (IsMouseButtonReleased(button)) { grabbed->release(w); grabbed = nullptr; }
        }
    }
    void update(World& w, float dt) { for (auto& c : items) c->update(w, dt); }
    void draw(const World& w)       { for (auto& c : items) c->draw(w); }
};

// ---------------------------------------------------------------- components

// Coloured spokes: 4/4 (4), 3/4 (3), 12/8 (12).
struct DivisionLines : Component {
    struct Div { int count; Color color; float thick; };
    Div divs[3] = {{4, {255, 160, 40, 255}, 2.0f},
                   {3, {60, 200, 255, 255}, 2.0f},
                   {12, {230, 80, 200, 255}, 1.0f}};

    void draw(const World& w) override {
        float R = w.outerRadius() + 14;
        for (int d = 0; d < 3; d++) {
            if (!w.showDiv[d]) continue;
            for (int i = 0; i < divs[d].count; i++) {
                Vector2 p = pointOn(w.center, R, (float)i / divs[d].count);
                DrawLineEx(w.center, p, divs[d].thick, Fade(divs[d].color, 0.35f));
            }
        }
    }
};

// One ring of notes. Left click toggles a note; right drag rotates the ring.
struct TrackRing : Component {
    int  index;
    bool rotating = false;
    float grabOffset = 0;

    explicit TrackRing(int i) : index(i) {}

    bool hit(const World& w, Vector2 m) const {
        return std::fabs(Vector2Distance(m, w.center) - w.tracks[index].radius) < 12;
    }

    bool press(World& w, Vector2 m, int button) override {
        if (!hit(w, m)) return false;
        Track& t = w.tracks[index];
        if (button == MOUSE_BUTTON_RIGHT) {
            rotating = true;
            grabOffset = posFrom(w.center, m) - t.rotation;
            return true;
        }
        float p = snap(posFrom(w.center, m) - t.rotation, w.grid);
        float tol = std::min(0.5f / w.grid, 0.02f);
        auto it = std::find_if(t.notes.begin(), t.notes.end(), [&](float n) {
            float d = std::fabs(n - p);
            return std::min(d, 1.0f - d) < tol;
        });
        if (it != t.notes.end()) t.notes.erase(it);
        else { t.notes.push_back(p); w.fire(t); }                       // audition the new note
        return true;
    }

    void drag(World& w, Vector2 m, Vector2) override {
        if (rotating) w.tracks[index].rotation = wrap01(posFrom(w.center, m) - grabOffset);
    }

    void release(World& w) override {
        if (rotating) w.tracks[index].rotation = snap(w.tracks[index].rotation, w.grid);
        rotating = false;
    }

    void draw(const World& w) override {
        const Track& t = w.tracks[index];
        DrawRing(w.center, t.radius - 1.5f, t.radius + 1.5f, 0, 360, 96, Fade(t.color, 0.45f));
        for (float n : t.notes) {
            float eff = wrap01(n + t.rotation);
            float since = wrap01(w.phase - eff);                        // time since playhead passed
            float flash = (w.playing && since < 0.08f) ? 1.0f - since / 0.08f : 0.0f;
            DrawCircleV(pointOn(w.center, t.radius, eff), 7.0f + 5.0f * flash, t.color);
        }
        // label just right of the top
        Vector2 lp = pointOn(w.center, t.radius, 0);
        DrawText(t.name.c_str(), (int)lp.x + 8, (int)lp.y - 16, 12, Fade(t.color, 0.8f));
    }
};

// Advances the playhead, fires notes, draws the playhead line.
struct Playhead : Component {
    bool wasPlaying = false;

    static bool crossed(float prev, float cur, float p) {
        return cur >= prev ? (p > prev && p <= cur) : (p > prev || p <= cur);
    }

    void update(World& w, float dt) override {
        // On the frame playback starts, notes sitting exactly on the cue fire too.
        if (w.playing && !wasPlaying) {
            for (auto& t : w.tracks)
                for (float n : t.notes) {
                    float d = std::fabs(wrap01(n + t.rotation) - w.cue);
                    if (std::min(d, 1.0f - d) < 1e-4f) w.fire(t);
                }
        }
        wasPlaying = w.playing;
        if (!w.playing) return;

        float prev = w.phase;
        w.phase = wrap01(prev + dt * w.bpm / 240.0f);                  // 4 beats per bar
        for (auto& t : w.tracks)
            for (float n : t.notes)
                if (crossed(prev, w.phase, wrap01(n + t.rotation))) w.fire(t);
    }

    void draw(const World& w) override {
        Vector2 tip = pointOn(w.center, w.outerRadius() + 14, w.phase);
        DrawLineEx(w.center, tip, 2.0f, w.playing ? WHITE : Fade(WHITE, 0.3f));
    }
};

// Draggable triangle on the rim: where playback starts.
struct CueTriangle : Component {
    Vector2 centroid(const World& w) const { return pointOn(w.center, w.outerRadius() + 30, w.cue); }

    bool press(World& w, Vector2 m, int button) override {
        return button == MOUSE_BUTTON_LEFT && Vector2Distance(m, centroid(w)) < 18;
    }
    void drag(World& w, Vector2 m, Vector2) override {
        w.cue = IsKeyDown(KEY_LEFT_SHIFT) ? posFrom(w.center, m) : snap(posFrom(w.center, m), w.grid);
        if (!w.playing) w.phase = w.cue;
    }
    void draw(const World& w) override {
        float R = w.outerRadius();
        Vector2 tip   = pointOn(w.center, R + 14, w.cue);                  // points inward
        Vector2 baseC = pointOn(w.center, R + 36, w.cue);
        Vector2 dir   = Vector2Normalize(Vector2Subtract(baseC, tip));
        Vector2 perp  = {-dir.y, dir.x};
        Vector2 a = Vector2Add(baseC, Vector2Scale(perp,  11));
        Vector2 b = Vector2Add(baseC, Vector2Scale(perp, -11));
        DrawTriangle(tip, b, a, YELLOW);                                   // counter-clockwise order
    }
};

// Centre handle: drag to move the circle.
struct Hub : Component {
    bool press(World& w, Vector2 m, int button) override {
        return button == MOUSE_BUTTON_LEFT && Vector2Distance(m, w.center) < 16;
    }
    void drag(World& w, Vector2, Vector2 d) override { w.center = Vector2Add(w.center, d); }
    void draw(const World& w) override {
        DrawCircleV(w.center, 10, Fade(WHITE, 0.85f));
        DrawCircleLines((int)w.center.x, (int)w.center.y, 14, Fade(WHITE, 0.4f));
    }
};

// MIDI out: notes (with a short gate), plus optional clock and start/stop for hardware sync.
// Hook it up with:  w.onTrigger.push_back([&](const Track& t) { midi.noteOn(t); });
struct MidiSender : Component {
    MidiOut out;
    int   target = -1;              // -1 = virtual port, >= 0 = hardware/loopback port index
    bool  clock  = true;
    float gate   = 0.10f;           // seconds a note is held
    int   velocity = 100;

    MidiSender() {
        if (!MidiOut::virtualSupported()) target = 0;
        connect();
    }

    void connect() {
        if (target < 0) out.openVirtual("CircleBeats");
        else            out.openPort(target);
    }

    void cycle() {                  // virtual -> port 0 -> port 1 ... -> virtual
        int n = (int)out.ports().size();
        int first = MidiOut::virtualSupported() ? -1 : 0;
        target = (target + 1 >= n) ? first : target + 1;
        allOff();
        connect();
    }

    void noteOn(const Track& t) {
        for (auto it = pending.begin(); it != pending.end();) {         // retrigger: close the old note
            if (it->ch == t.midiChannel && it->note == t.midiNote) {
                out.noteOff(it->ch, it->note);
                it = pending.erase(it);
            } else ++it;
        }
        out.noteOn(t.midiChannel, t.midiNote, velocity);
        pending.push_back({t.midiChannel, t.midiNote, gate});
    }

    void update(World& w, float dt) override {
        for (auto it = pending.begin(); it != pending.end();) {
            it->left -= dt;
            if (it->left <= 0) { out.noteOff(it->ch, it->note); it = pending.erase(it); }
            else ++it;
        }
        if (clock && out.isOpen()) {
            if (w.playing && !wasPlaying) { out.start(); tickAcc = 0; }
            if (!w.playing && wasPlaying)   out.stop();
            if (w.playing) {                                            // 24 pulses per quarter note
                tickAcc += dt * w.bpm * 24.0f / 60.0f;
                while (tickAcc >= 1.0f) { out.clock(); tickAcc -= 1.0f; }
            }
        }
        wasPlaying = w.playing;
    }

    void draw(const World&) override {
        if (out.isOpen())
            DrawText(TextFormat("MIDI > %s   clock %s", out.portName().c_str(), clock ? "on" : "off"),
                     16, 40, 14, {120, 255, 150, 255});
        else
            DrawText("MIDI: no output (press M to try again)", 16, 40, 14, {255, 120, 100, 255});
    }

    ~MidiSender() override { allOff(); }

private:
    struct Off { int ch, note; float left; };
    std::vector<Off> pending;
    bool  wasPlaying = false;
    float tickAcc    = 0;

    void allOff() {
        for (auto& p : pending) out.noteOff(p.ch, p.note);
        pending.clear();
    }
};

// ---------------------------------------------------------------- main

int main() {
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(900, 900, "CircleBeats");
    InitAudioDevice();
    SetTargetFPS(120);

    World w;
    w.center = {GetScreenWidth() / 2.0f, GetScreenHeight() / 2.0f};
    //            name     colour                 radius  notes                                  rot  sound          note ch
    w.tracks = {
        {"kick",  {255, 90, 70, 255},   100, {0.0f, 0.25f, 0.5f, 0.75f},         0, synth::kick(),  36, 9},
        {"snare", {255, 220, 90, 255},  150, {0.25f, 0.75f},                     0, synth::snare(), 38, 9},
        {"bass",  {120, 255, 150, 255}, 200, {0.1875f, 0.4375f, 0.6875f},        0, synth::bass(),  33, 1},
        {"hat",   {140, 180, 255, 255}, 250, {0.125f, 0.375f, 0.625f, 0.875f},   0, synth::hat(),   42, 9},
    };

    Scene scene;                                   // draw order, back to front
    scene.add<DivisionLines>();
    for (int i = 0; i < (int)w.tracks.size(); i++) scene.add<TrackRing>(i);
    scene.add<Playhead>();
    scene.add<CueTriangle>();
    scene.add<Hub>();
    auto& midi = scene.add<MidiSender>();

    w.onTrigger.push_back([](const Track& t)  { PlaySound(t.sound); });
    w.onTrigger.push_back([&midi](const Track& t) { midi.noteOn(t); });

    const int grids[] = {16, 12, 8, 32};
    int gridIdx = 0;

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();

        if (IsKeyPressed(KEY_SPACE)) { w.playing = !w.playing; if (w.playing) w.phase = w.cue; }
        if (IsKeyPressed(KEY_ONE))   w.showDiv[0] = !w.showDiv[0];
        if (IsKeyPressed(KEY_TWO))   w.showDiv[1] = !w.showDiv[1];
        if (IsKeyPressed(KEY_THREE)) w.showDiv[2] = !w.showDiv[2];
        if (IsKeyPressed(KEY_G))     { gridIdx = (gridIdx + 1) % 4; w.grid = grids[gridIdx]; }
        if (IsKeyPressed(KEY_M))     midi.cycle();
        if (IsKeyPressed(KEY_C))     midi.clock = !midi.clock;
        float step = IsKeyDown(KEY_LEFT_SHIFT) ? 10.0f : 1.0f;
        if (IsKeyPressed(KEY_UP))    w.bpm = std::min(300.0f, w.bpm + step);
        if (IsKeyPressed(KEY_DOWN))  w.bpm = std::max(40.0f,  w.bpm - step);

        scene.input(w);
        scene.update(w, dt);

        BeginDrawing();
        ClearBackground({14, 14, 20, 255});
        scene.draw(w);
        DrawText(TextFormat("%s   BPM %.0f   grid 1/%d", w.playing ? "PLAYING" : "stopped", w.bpm, w.grid),
                 16, 14, 18, RAYWHITE);
        DrawText("space play | click note | right-drag rotate | 1 2 3 lines | G grid | M midi port | C clock | up/down bpm",
                 16, GetScreenHeight() - 26, 14, GRAY);
        EndDrawing();
    }

    for (auto& t : w.tracks) UnloadSound(t.sound);
    CloseAudioDevice();
    CloseWindow();
    return 0;
}
