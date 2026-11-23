#include "raylib.h"
#include "raymath.h"
#include <vector>
#include <string>
#include <cmath>
#include <deque>
#include <algorithm>
#include <cstdio>

#if defined(PLATFORM_WEB)
    #include <emscripten/emscripten.h>
#endif

// ==========================================
// 1. Constants & Enums
// ==========================================
const int SCREEN_WIDTH = 1280;
const int SCREEN_HEIGHT = 720;
const float TILE_DIST = 96.0f;
const float PLANET_RADIUS = 16.5f;
const int WORLD_COUNT = 6;

enum ScreenState {
    SCR_MODE_SELECT,
    SCR_WORLD_SELECT,
    SCR_COUNTDOWN,
    SCR_PLAYING,
    SCR_GAMEOVER,
    SCR_CLEAR
};

enum GameMode {
    MODE_JOURNEY = 0,      // 6 Thematic Worlds (Easy -> Master)
    MODE_ENDLESS = 1,      // Infinite Procedural Track
    MODE_ZEN = 2,          // No-Fail Practice Mode
    MODE_PERFECTIONIST = 3 // One Mistake = Instant Shatter
};

enum Strictness {
    STRICT_EASY   = 0,
    STRICT_NORMAL = 1,
    STRICT_STRICT = 2
};

struct Tile {
    Vector2 pos;
    bool swirl = false;
    bool midspin = false;
    bool checkpoint = false;
    bool cleared = false;
    float hitAnim = 0.0f;
};

struct MapStep {
    float angleDeg;
    bool swirl = false;
    bool midspin = false;
    bool checkpoint = false;
};

struct TrailPoint {
    Vector2 pos;
    Color color;
    float alpha;
};

struct Particle {
    Vector2 pos;
    Vector2 vel;
    Color color;
    float size;
    float life;
    float maxLife;
};

struct Shockwave {
    Vector2 pos;
    Color color;
    float radius;
    float maxRadius;
    float alpha;
};

struct AmbientMote {
    Vector2 pos;
    Vector2 vel;
    float size;
    float phase;
};

struct JudgmentPopup {
    std::string text;
    Vector2 pos;
    Color color;
    float timer;
};

struct HitTick {
    float errorMs;
    Color color;
    float alpha;
};

struct WorldInfo {
    const char* name;
    const char* diffLabel;
    const char* songStyle;
    const char* gimmickDesc;
    float bpm;
    int difficultyLevel; // 1 to 6
    float hue;
    int melodyNotes[8];  // Indices into note scale
    int bassNotes[4];
    int instrumentType;  // 0: Music Box, 1: E-Piano, 2: Crystal Chime, 3: Harp, 4: Synth Lead, 5: Boss Organ
    bool cleared;
    float bestAcc;
};

// ==========================================
// 2. Global Game State
// ==========================================
Font gameFont = { 0 };
ScreenState screen = SCR_MODE_SELECT;
GameMode currentMode = MODE_JOURNEY;
Strictness strictness = STRICT_NORMAL;
int selectedWorld = 0;
bool autoPlay = false;
float audioOffsetMs = 0.0f;

// Note frequency table (A3 to E5)
const float NOTE_FREQS[18] = {
    220.00f, // 0: A3
    233.08f, // 1: Bb3
    246.94f, // 2: B3
    261.63f, // 3: C4
    277.18f, // 4: C#4
    293.66f, // 5: D4
    311.13f, // 6: D#4
    329.63f, // 7: E4
    349.23f, // 8: F4
    369.99f, // 9: F#4
    392.00f, // 10: G4
    415.30f, // 11: G#4
    440.00f, // 12: A4
    466.16f, // 13: Bb4
    493.88f, // 14: B4
    523.25f, // 15: C5
    587.33f, // 16: D5
    659.25f  // 17: E5
};

WorldInfo worlds[WORLD_COUNT] = {
    {
        "First Spark", "VERY EASY", "Music Box Ballad",
        "Straight & 90-Deg Turns Only",
        96.0f, 1, 24.0f,
        { 3, 7, 10, 12, 10, 7, 5, 10 }, // C Major Pentatonic
        { 3, 8, 3, 10 }, 0, false, 0.0f
    },
    {
        "Whispering Breeze", "EASY", "Warm Electric Piano",
        "Introduces Stairs & Swirl Reversals",
        108.0f, 2, 165.0f,
        { 8, 12, 15, 17, 16, 12, 10, 15 }, // F Lydian
        { 8, 5, 10, 3 }, 1, false, 0.0f
    },
    {
        "Glacial Spiral", "MEDIUM", "Crystal Winter Chimes",
        "Expanding Spiral Coil & 45-Deg Diagonals",
        120.0f, 3, 198.0f,
        { 12, 15, 17, 14, 15, 12, 10, 7 }, // A Minor
        { 0, 8, 3, 10 }, 2, false, 0.0f
    },
    {
        "Starlight Waltz", "CHALLENGING", "Celestial Harp Arpeggio",
        "Diamond Arena, Triangle Midspins & U-Turns",
        132.0f, 4, 248.0f,
        { 5, 8, 12, 15, 14, 10, 12, 8 }, // D Dorian
        { 5, 10, 1, 0 }, 3, false, 0.0f
    },
    {
        "Hexagon Citadel", "HARD", "Retro Synthwave Riff",
        "60 & 120-Deg Honeycomb Triplet Paths",
        144.0f, 5, 290.0f,
        { 7, 8, 11, 14, 15, 14, 11, 8 }, // E Phrygian Dominant
        { 7, 8, 5, 7 }, 4, false, 0.0f
    },
    {
        "Eclipse Finale", "MASTER", "Grand Boss Organ",
        "Fortress Cross Combining Every Gimmick",
        156.0f, 6, 345.0f,
        { 3, 6, 10, 11, 10, 6, 5, 2 }, // C Harmonic Minor
        { 3, 11, 8, 10 }, 5, false, 0.0f
    }
};

std::vector<Tile> tiles;
std::vector<Vector2> previewPoints; // Normalized points for Mini-Map preview
std::deque<TrailPoint> fireTrail;
std::deque<TrailPoint> iceTrail;
std::vector<Particle> particles;
std::vector<Shockwave> shockwaves;
std::vector<AmbientMote> motes;
std::vector<JudgmentPopup> popups;
std::deque<HitTick> hitTicks;

int currentTile = 0;
int lastCheckpoint = 0;
Vector2 pivotPos = { 0, 0 };
Vector2 endlessBuildPivot = { 0, 0 };
int endlessTilesCleared = 0;
int bestEndlessScore = 0;

bool isFirePivot = true;
bool cw = true;

float currentBpm = 96.0f;
float currentAngle = 0.0f;
float targetDiff = PI;
float traveledAngle = 0.0f;

int countdownBeats = 4;
float beatTimer = 0.0f;
int totalBeatCount = 0;
float cameraBeatPulse = 0.0f;
float screenShake = 0.0f;
float bgHue = 24.0f;

int hitCount = 0;
int perfectCount = 0;
int streakCount = 0;
float accuracySum = 0.0f;
std::string failReason = "";

Camera2D camera = { 0 };

bool audioReady = false;
Sound sndHit;
Sound sndKick;
Sound sndSnare;
Sound sndHat;
Sound sndSwirl;
Sound sndFail;
Sound sndClear;
Sound sndMelody[6][18]; // Pre-synthesized instruments x notes
Sound sndBass[18];

// ==========================================
// 3. UI & Typography Helpers
// ==========================================
void DrawTextUI(const char* text, float x, float y, float size, Color color, bool shadow = true) {
    if (shadow) {
        DrawTextEx(gameFont, text, { x + 2.0f, y + 2.0f }, size, 1.0f, Fade(BLACK, (color.a / 255.0f) * 0.65f));
    }
    DrawTextEx(gameFont, text, { x, y }, size, 1.0f, color);
}

void DrawTextCentered(const char* text, float centerX, float y, float size, Color color, bool shadow = true) {
    Vector2 dim = MeasureTextEx(gameFont, text, size, 1.0f);
    DrawTextUI(text, centerX - dim.x * 0.5f, y, size, color, shadow);
}

// Version-safe outlined rounded card helper
void DrawCardBox(Rectangle rec, float roundness, Color fillCol, Color borderCol, float borderThick = 2.0f) {
    DrawRectangleRounded({ rec.x + 4, rec.y + 6, rec.width, rec.height }, roundness, 10, Fade(BLACK, 0.42f));
    DrawRectangleRounded(rec, roundness, 10, borderCol);
    Rectangle inner = {
        rec.x + borderThick,
        rec.y + borderThick,
        rec.width - borderThick * 2.0f,
        rec.height - borderThick * 2.0f
    };
    DrawRectangleRounded(inner, roundness, 10, fillCol);
}

float NormalizeAngle(float a) {
    while (a < 0.0f) a += 2.0f * PI;
    while (a >= 2.0f * PI) a -= 2.0f * PI;
    return a;
}

void GetJudgmentWindows(float& purePerfect, float& perfect, float& maxWindow) {
    if (strictness == STRICT_EASY) {
        purePerfect = 0.070f; perfect = 0.145f; maxWindow = 0.230f;
    } else if (strictness == STRICT_STRICT) {
        purePerfect = 0.032f; perfect = 0.070f; maxWindow = 0.125f;
    } else {
        purePerfect = 0.050f; perfect = 0.105f; maxWindow = 0.175f;
    }
}

// ==========================================
// 4. Multi-Instrument Audio Synthesizer
// ==========================================
Sound SynthSFX(int type, float freq, float duration, float volume) {
    int sampleRate = 44100;
    int count = (int)(sampleRate * duration);
    short* data = (short*)RL_MALLOC(count * sizeof(short));

    for (int i = 0; i < count; i++) {
        float t = (float)i / sampleRate;
        float sample = 0.0f;

        if (type == 0) { // Crisp Hit
            float env = expf(-t * 36.0f);
            sample = (sinf(2.0f * PI * freq * t) * 0.68f + sinf(2.0f * PI * (freq * 1.5f) * t) * 0.32f) * env;
        } else if (type == 1) { // Kick
            float env = expf(-t * 17.0f);
            float f = freq * expf(-t * 22.0f) + 44.0f;
            sample = sinf(2.0f * PI * f * t) * env;
        } else if (type == 2) { // Snare / Hat
            float env = expf(-t * (freq > 1000.0f ? 58.0f : 23.0f));
            float noise = ((float)GetRandomValue(-1000, 1000) / 1000.0f);
            float tone = (freq < 1000.0f) ? sinf(2.0f * PI * 185.0f * t) * 0.38f : 0.0f;
            sample = (noise * 0.72f + tone) * env;
        } else if (type == 3) { // Swirl / Fail Sweep
            float env = expf(-t * 10.5f);
            float f = freq + (duration > 0.25f ? -freq * 0.65f * (t / duration) : freq * 0.55f * (t / duration));
            sample = (sinf(2.0f * PI * f * t) + 0.28f * sinf(4.0f * PI * f * t)) * env;
        }

        sample = Clamp(sample, -1.0f, 1.0f);
        data[i] = (short)(sample * 32767.0f * volume);
    }

    Wave w = { (unsigned int)count, (unsigned int)sampleRate, 16, 1, data };
    Sound s = LoadSoundFromWave(w);
    UnloadWave(w);
    return s;
}

Sound SynthInstrumentNote(int inst, float freq, float duration, float volume) {
    int sampleRate = 44100;
    int count = (int)(sampleRate * duration);
    short* data = (short*)RL_MALLOC(count * sizeof(short));

    for (int i = 0; i < count; i++) {
        float t = (float)i / sampleRate;
        float sample = 0.0f;

        if (inst == 0) { // Music Box (pure bell harmonics)
            float env = expf(-t * 8.0f);
            sample = (sinf(2.0f * PI * freq * t) * 0.65f + sinf(2.0f * PI * (freq * 2.0f) * t) * 0.35f) * env;
        } else if (inst == 1) { // Electric Piano (warm tine + tremolo)
            float env = fminf(1.0f, t * 40.0f) * expf(-t * 5.0f);
            float trem = 1.0f + 0.15f * sinf(2.0f * PI * 5.5f * t);
            sample = (sinf(2.0f * PI * freq * t) * 0.7f + sinf(2.0f * PI * (freq * 3.0f) * t) * 0.15f * expf(-t * 14.0f)) * env * trem;
        } else if (inst == 2) { // Crystal Chimes (shimmering overtones)
            float env = expf(-t * 7.5f);
            sample = (sinf(2.0f * PI * freq * t) * 0.5f + sinf(2.0f * PI * (freq * 2.5f) * t) * 0.3f + sinf(2.0f * PI * (freq * 4.0f) * t) * 0.2f) * env;
        } else if (inst == 3) { // Harp Pluck
            float env = fminf(1.0f, t * 60.0f) * expf(-t * 6.5f);
            sample = (sinf(2.0f * PI * freq * t) * 0.6f + sinf(2.0f * PI * (freq * 2.0f) * t) * 0.3f + sinf(2.0f * PI * (freq * 3.0f) * t) * 0.1f) * env;
        } else if (inst == 4) { // Retro Synth Lead (soft saw/square blend)
            float env = fminf(1.0f, t * 50.0f) * expf(-t * 6.0f);
            float s1 = sinf(2.0f * PI * freq * t);
            float s2 = (sinf(2.0f * PI * freq * t) > 0.0f ? 0.35f : -0.35f);
            sample = (s1 * 0.65f + s2 * 0.35f) * env;
        } else if (inst == 5) { // Boss Organ (rich octave stack)
            float env = fminf(1.0f, t * 35.0f) * expf(-t * 4.5f);
            sample = (sinf(2.0f * PI * (freq * 0.5f) * t) * 0.4f
                    + sinf(2.0f * PI * freq * t) * 0.4f
                    + sinf(2.0f * PI * (freq * 2.0f) * t) * 0.2f) * env;
        } else { // Deep Sub Bass
            float env = fminf(1.0f, t * 30.0f) * expf(-t * 4.5f);
            sample = (sinf(2.0f * PI * (freq * 0.5f) * t) * 0.8f + sinf(2.0f * PI * freq * t) * 0.2f) * env;
        }

        sample = Clamp(sample, -1.0f, 1.0f);
        data[i] = (short)(sample * 32767.0f * volume);
    }

    Wave w = { (unsigned int)count, (unsigned int)sampleRate, 16, 1, data };
    Sound s = LoadSoundFromWave(w);
    UnloadWave(w);
    return s;
}

void EnsureAudioInitialized() {
    if (audioReady) return;
    InitAudioDevice();
    sndHit   = SynthSFX(0, 587.33f, 0.10f, 0.72f);
    sndKick  = SynthSFX(1, 152.00f, 0.17f, 0.60f);
    sndSnare = SynthSFX(2, 400.00f, 0.13f, 0.32f);
    sndHat   = SynthSFX(2, 2600.0f, 0.045f, 0.18f);
    sndSwirl = SynthSFX(3, 440.00f, 0.14f, 0.45f);
    sndFail  = SynthSFX(3, 155.00f, 0.40f, 0.78f);
    sndClear = SynthSFX(0, 880.00f, 0.45f, 0.78f);

    for (int inst = 0; inst < 6; inst++) {
        for (int n = 0; n < 18; n++) {
            sndMelody[inst][n] = SynthInstrumentNote(inst, NOTE_FREQS[n], 0.36f, 0.26f);
        }
    }
    for (int n = 0; n < 18; n++) {
        sndBass[n] = SynthInstrumentNote(6, NOTE_FREQS[n], 0.44f, 0.28f);
    }
    audioReady = true;
}

// ==========================================
// 5. Thematic Geometric Course Builders
// ==========================================
std::vector<MapStep> GetWorldSteps(int worldIdx) {
    if (worldIdx == 0) {
        // World 1: First Spark (96 BPM) - Gentle Straight Runway + Wide L-Turns (NO swirls, NO midspins)
        return {
            {0}, {0}, {0}, {0}, {0}, {0},
            {-90}, {0}, {0}, {0},
            {0, false, false, true}, // Checkpoint
            {0}, {90}, {0}, {0}, {0},
            {-90}, {0}, {0}, {0}, {0}, {0}
        };
    }
    else if (worldIdx == 1) {
        // World 2: Whispering Breeze (108 BPM) - Ascending Sky Staircase + Gentle Swirls
        return {
            {0}, {0}, {0},
            {-90}, {0}, {-90}, {0}, {-90}, {0},
            {0, true}, {0}, {0}, {0}, // First Swirl!
            {90, false, false, true}, {0}, {90}, {0}, {90}, {0},
            {0, true}, {0}, {-90}, {0}, {0}, {0}, {0}
        };
    }
    else if (worldIdx == 2) {
        // World 3: Glacial Spiral (120 BPM) - Spiral Coil + 45-Deg Diagonals
        return {
            {0}, {0}, {0}, {0},
            {90}, {90}, {90},
            {180}, {180},
            {-90}, {-90},
            {0, false, false, true}, // Checkpoint
            {0, true}, {0},
            {-45}, {45}, {-45}, {45}, {-45}, {45},
            {0, true}, {0}, {-90}, {0}, {90}, {0}, {0}, {0}
        };
    }
    else if (worldIdx == 3) {
        // World 4: Starlight Waltz (132 BPM) - Diamond Arena + Triangle Midspins + 360 U-Turn
        return {
            {0}, {-45}, {-45}, {45}, {45},
            {-90, false, true}, {0}, {90, false, true}, {0}, // Midspin peaks
            {0, true}, {0}, {180}, {0}, // 360 U-Turn
            {0, false, false, true}, // Checkpoint
            {45}, {45}, {-45}, {-45},
            {-90, false, true}, {0}, {-90, false, true}, {0},
            {0, true}, {0}, {-90}, {0}, {90}, {0}, {0}, {0}
        };
    }
    else if (worldIdx == 4) {
        // World 5: Hexagon Citadel (144 BPM) - 60 & 120-Deg Honeycomb Loops
        return {
            {0}, {-60}, {0}, {60}, {120}, {60}, {0},
            {-60, false, true}, {0}, {60, false, true}, {0},
            {0, true}, {0}, {-60}, {-120}, {-60}, {0},
            {0, false, false, true}, // Checkpoint
            {60}, {0}, {-60}, {0}, {60}, {0}, {-60}, {0},
            {180, true}, {0}, {-60, false, true}, {0},
            {0, true}, {0}, {0}, {0}
        };
    }
    else {
        // World 6: Eclipse Finale (156 BPM) - Grand Fortress Cross
        return {
            {0}, {-90}, {0}, {90}, {0},
            {0, true}, {-90, true}, {0, true}, {90, true},
            {-90, false, true}, {0}, {-90, false, true}, {0},
            {0, false, false, true}, // Checkpoint 1
            {-45}, {45}, {-45}, {45}, {180, true}, {0},
            {-60}, {0}, {60}, {0}, {-60}, {0}, {60}, {0},
            {0, false, false, true}, // Checkpoint 2
            {-90}, {0}, {-90}, {0}, {90}, {0}, {90}, {0},
            {-90, false, true}, {0}, {90, false, true}, {0},
            {0, true}, {0}, {0}, {0}
        };
    }
}

void BuildTilesFromSteps(const std::vector<MapStep>& steps) {
    tiles.clear();
    Vector2 curPos = { 0.0f, 0.0f };
    tiles.push_back({ curPos, false, false, false, true, 0.0f });

    Vector2 buildPivot = curPos;
    for (size_t i = 0; i < steps.size(); i++) {
        float rad = steps[i].angleDeg * DEG2RAD;
        Vector2 nextPos = {
            buildPivot.x + cosf(rad) * TILE_DIST,
            buildPivot.y + sinf(rad) * TILE_DIST
        };
        tiles.push_back({
            nextPos,
            steps[i].swirl,
            steps[i].midspin,
            steps[i].checkpoint,
            false,
            0.0f
        });
        if (!steps[i].midspin) {
            buildPivot = nextPos;
        }
    }
}

// Compute normalized mini-map points for World Select showcase
void UpdateWorldPreview(int worldIdx) {
    std::vector<MapStep> steps = GetWorldSteps(worldIdx);
    previewPoints.clear();
    Vector2 p = { 0.0f, 0.0f };
    previewPoints.push_back(p);
    Vector2 piv = p;

    float minX = 0, maxX = 0, minY = 0, maxY = 0;
    for (const auto& s : steps) {
        float rad = s.angleDeg * DEG2RAD;
        Vector2 np = { piv.x + cosf(rad), piv.y + sinf(rad) };
        previewPoints.push_back(np);
        if (!s.midspin) piv = np;
        minX = fminf(minX, np.x); maxX = fmaxf(maxX, np.x);
        minY = fminf(minY, np.y); maxY = fmaxf(maxY, np.y);
    }

    float spanX = fmaxf(1.0f, maxX - minX);
    float spanY = fmaxf(1.0f, maxY - minY);
    float scale = 1.0f / fmaxf(spanX, spanY);
    float midX = (minX + maxX) * 0.5f;
    float midY = (minY + maxY) * 0.5f;

    for (auto& pt : previewPoints) {
        pt.x = (pt.x - midX) * scale;
        pt.y = (pt.y - midY) * scale;
    }
}

// Append procedural tiles for Endless Rush Mode
void AppendEndlessTiles(int count) {
    const float angles[7] = { 0.0f, 0.0f, -90.0f, 90.0f, -45.0f, 45.0f, 180.0f };
    for (int i = 0; i < count; i++) {
        float deg = angles[GetRandomValue(0, 6)];
        bool swirl = (GetRandomValue(0, 7) == 0);
        bool midspin = (!swirl && deg != 180.0f && GetRandomValue(0, 8) == 0);
        float rad = deg * DEG2RAD;

        Vector2 nextPos = {
            endlessBuildPivot.x + cosf(rad) * TILE_DIST,
            endlessBuildPivot.y + sinf(rad) * TILE_DIST
        };
        tiles.push_back({ nextPos, swirl, midspin, false, false, 0.0f });
        if (!midspin) {
            endlessBuildPivot = nextPos;
        }
    }
}

// ==========================================
// 6. Visual Effects & Orbit Logic
// ==========================================
void InitAmbientMotes() {
    motes.clear();
    for (int i = 0; i < 45; i++) {
        motes.push_back({
            { (float)GetRandomValue(0, SCREEN_WIDTH), (float)GetRandomValue(0, SCREEN_HEIGHT) },
            { (float)GetRandomValue(-16, 16), (float)GetRandomValue(-26, -8) },
            (float)GetRandomValue(2, 5),
            (float)GetRandomValue(0, 628) / 100.0f
        });
    }
}

void SpawnHitParticles(Vector2 pos, Color col, int count) {
    for (int i = 0; i < count; i++) {
        float ang = (float)GetRandomValue(0, 360) * DEG2RAD;
        float spd = (float)GetRandomValue(85, 330);
        float life = (float)GetRandomValue(28, 52) / 100.0f;
        particles.push_back({
            pos,
            { cosf(ang) * spd, sinf(ang) * spd },
            col,
            (float)GetRandomValue(3, 7),
            life,
            life
        });
    }
}

void SpawnShockwave(Vector2 pos, Color col, float maxRad = 85.0f) {
    shockwaves.push_back({ pos, col, 18.0f, maxRad, 0.85f });
}

void AddPopup(const std::string& msg, Vector2 pos, Color col) {
    popups.push_back({ msg, { pos.x, pos.y - 44.0f }, col, 0.75f });
}

void SetupOrbitForTile(int index, Vector2 prevPivotPos) {
    if (tiles[index].midspin) {
        Vector2 currTarget = tiles[index].pos;
        Vector2 nextTarget = tiles[index + 1].pos;
        float startAng = atan2f(currTarget.y - pivotPos.y, currTarget.x - pivotPos.x);
        float endAng   = atan2f(nextTarget.y - pivotPos.y, nextTarget.x - pivotPos.x);

        if (tiles[index].swirl) {
            cw = !cw;
            SpawnShockwave(tiles[index].pos, MAGENTA, 95.0f);
            if (audioReady) PlaySound(sndSwirl);
        }

        float diff = cw ? NormalizeAngle(endAng - startAng) : NormalizeAngle(startAng - endAng);
        if (diff < 0.05f) diff += 2.0f * PI;

        currentAngle = startAng;
        targetDiff = diff;
        traveledAngle = 0.0f;
        return;
    }

    pivotPos = tiles[index].pos;
    Vector2 nextTarget = tiles[index + 1].pos;
    float endAng = atan2f(nextTarget.y - pivotPos.y, nextTarget.x - pivotPos.x);

    float startAng;
    if (index == 0) {
        startAng = NormalizeAngle(endAng + PI);
    } else {
        startAng = atan2f(prevPivotPos.y - pivotPos.y, prevPivotPos.x - pivotPos.x);
    }

    if (tiles[index].swirl) {
        cw = !cw;
        if (screen == SCR_PLAYING) SpawnShockwave(pivotPos, MAGENTA, 95.0f);
        if (audioReady && screen == SCR_PLAYING) PlaySound(sndSwirl);
    }

    float diff = cw ? NormalizeAngle(endAng - startAng) : NormalizeAngle(startAng - endAng);
    if (diff < 0.05f) diff += 2.0f * PI;

    currentAngle = startAng;
    targetDiff = diff;
    traveledAngle = 0.0f;
}

void StartGame(int startFromTile = 0) {
    EnsureAudioInitialized();

    fireTrail.clear();
    iceTrail.clear();
    particles.clear();
    shockwaves.clear();
    popups.clear();
    hitTicks.clear();

    if (currentMode == MODE_ENDLESS) {
        tiles.clear();
        Vector2 startPos = { 0.0f, 0.0f };
        tiles.push_back({ startPos, false, false, false, true, 0.0f });
        endlessBuildPivot = startPos;
        endlessTilesCleared = 0;
        currentBpm = 126.0f;
        bgHue = 185.0f;
        AppendEndlessTiles(40);
        startFromTile = 0;
    } else {
        BuildTilesFromSteps(GetWorldSteps(selectedWorld));
        currentBpm = worlds[selectedWorld].bpm;
        bgHue = worlds[selectedWorld].hue;
    }

    currentTile = startFromTile;
    lastCheckpoint = startFromTile;
    isFirePivot = (startFromTile % 2 == 0);
    cw = true;
    hitCount = 0;
    perfectCount = 0;
    streakCount = 0;
    accuracySum = 0.0f;
    failReason = "";

    Vector2 prevPiv = { 0, 0 };
    for (int i = 0; i <= startFromTile; i++) {
        tiles[i].cleared = true;
        if (i > 0 && tiles[i].swirl) cw = !cw;
        if (!tiles[i].midspin) {
            if (i > 0) prevPiv = pivotPos;
            pivotPos = tiles[i].pos;
        }
    }

    SetupOrbitForTile(startFromTile, prevPiv);

    countdownBeats = 4;
    beatTimer = 0.0f;
    totalBeatCount = 0;
    cameraBeatPulse = 0.06f;

    camera.target = pivotPos;
    camera.offset = { SCREEN_WIDTH / 2.0f, SCREEN_HEIGHT / 2.0f };
    camera.zoom = 1.0f;
    screen = SCR_COUNTDOWN;
    PlaySound(sndKick);
}

bool IsHitKeyPressed() {
    return IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_Z) || IsKeyPressed(KEY_X) ||
           IsKeyPressed(KEY_J) || IsKeyPressed(KEY_K) || IsKeyPressed(KEY_D) ||
           IsKeyPressed(KEY_F) || IsMouseButtonPressed(MOUSE_LEFT_BUTTON);
}

void AdvanceTileAfterHit(Vector2 hitTilePos, bool wasMissInZen = false) {
    currentTile++;
    tiles[currentTile].cleared = true;
    tiles[currentTile].hitAnim = 1.0f;

    if (!wasMissInZen && tiles[currentTile].checkpoint && currentMode == MODE_JOURNEY) {
        lastCheckpoint = currentTile;
        AddPopup("CHECKPOINT!", hitTilePos, GREEN);
        SpawnShockwave(hitTilePos, GREEN, 110.0f);
    }

    if (!tiles[currentTile].midspin) {
        isFirePivot = !isFirePivot;
    }

    if (currentMode == MODE_ENDLESS) {
        endlessTilesCleared++;
        if (endlessTilesCleared > bestEndlessScore) bestEndlessScore = endlessTilesCleared;
        if (currentTile + 20 >= (int)tiles.size()) {
            AppendEndlessTiles(25);
        }
        Vector2 oldPivot = pivotPos;
        SetupOrbitForTile(currentTile, oldPivot);
    } else {
        if (currentTile >= (int)tiles.size() - 1) {
            float finalAcc = (hitCount > 0) ? (accuracySum / hitCount * 100.0f) : 100.0f;
            if (!autoPlay && currentMode != MODE_ZEN) {
                worlds[selectedWorld].cleared = true;
                if (finalAcc > worlds[selectedWorld].bestAcc) {
                    worlds[selectedWorld].bestAcc = finalAcc;
                }
            }
            PlaySound(sndClear);
            SpawnShockwave(hitTilePos, GOLD, 160.0f);
            screen = SCR_CLEAR;
        } else {
            Vector2 oldPivot = pivotPos;
            SetupOrbitForTile(currentTile, oldPivot);
        }
    }
}

// ==========================================
// 7. Main Update & Render Loop
// ==========================================
void UpdateAndDrawFrame() {
    float dt = GetFrameTime();
    if (dt > 0.05f) dt = 0.05f;

    for (auto& m : motes) {
        m.pos = Vector2Add(m.pos, Vector2Scale(m.vel, dt));
        m.phase += 2.0f * dt;
        if (m.pos.y < -10) m.pos.y = SCREEN_HEIGHT + 10;
        if (m.pos.x < -10) m.pos.x = SCREEN_WIDTH + 10;
        if (m.pos.x > SCREEN_WIDTH + 10) m.pos.x = -10;
    }

    Vector2 mp = GetMousePosition();
    bool clicked = IsMouseButtonPressed(MOUSE_LEFT_BUTTON);

    // ------------------------------------------
    // SCREEN 1: MODE SELECT
    // ------------------------------------------
    if (screen == SCR_MODE_SELECT) {
        float modeHues[4] = { 24.0f, 185.0f, 145.0f, 345.0f };
        bgHue = Lerp(bgHue, modeHues[(int)currentMode], 1.0f - expf(-5.0f * dt));

        if (IsKeyPressed(KEY_LEFT)  || IsKeyPressed(KEY_A)) currentMode = (GameMode)(((int)currentMode + 3) % 4);
        if (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D)) currentMode = (GameMode)(((int)currentMode + 1) % 4);
        if (IsKeyPressed(KEY_UP)    || IsKeyPressed(KEY_W) || IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S)) {
            currentMode = (GameMode)(((int)currentMode + 2) % 4);
        }

        // Mode cards click detection
        for (int i = 0; i < 4; i++) {
            int col = i % 2;
            int row = i / 2;
            Rectangle card = { 170.0f + col * 485.0f, 185.0f + row * 165.0f, 455.0f, 140.0f };
            if (clicked && CheckCollisionPointRec(mp, card)) {
                currentMode = (GameMode)i;
                if (currentMode == MODE_ENDLESS) {
                    StartGame(0);
                } else {
                    UpdateWorldPreview(selectedWorld);
                    screen = SCR_WORLD_SELECT;
                }
            }
        }

        // Bottom clickable settings pills
        if (clicked) {
            // Timing buttons
            for (int s = 0; s < 3; s++) {
                Rectangle b = { 255.0f + s * 98.0f, 548.0f, 90.0f, 36.0f };
                if (CheckCollisionPointRec(mp, b)) strictness = (Strictness)s;
            }
            // Autoplay button
            Rectangle autoBtn = { 665.0f, 548.0f, 100.0f, 36.0f };
            if (CheckCollisionPointRec(mp, autoBtn)) autoPlay = !autoPlay;

            // Offset - / + buttons
            Rectangle offMinus = { 895.0f, 548.0f, 40.0f, 36.0f };
            Rectangle offPlus  = { 1035.0f, 548.0f, 40.0f, 36.0f };
            if (CheckCollisionPointRec(mp, offMinus)) audioOffsetMs = fmaxf(-150.0f, audioOffsetMs - 5.0f);
            if (CheckCollisionPointRec(mp, offPlus))  audioOffsetMs = fminf(150.0f, audioOffsetMs + 5.0f);
        }

        if (IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER)) {
            if (currentMode == MODE_ENDLESS) {
                StartGame(0);
            } else {
                UpdateWorldPreview(selectedWorld);
                screen = SCR_WORLD_SELECT;
            }
        }
    }
    // ------------------------------------------
    // SCREEN 2: WORLD / COURSE SHOWCASE SELECT
    // ------------------------------------------
    else if (screen == SCR_WORLD_SELECT) {
        bgHue = Lerp(bgHue, worlds[selectedWorld].hue, 1.0f - expf(-6.0f * dt));

        if (IsKeyPressed(KEY_ESCAPE)) screen = SCR_MODE_SELECT;
        if (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_A)) {
            selectedWorld = (selectedWorld + WORLD_COUNT - 1) % WORLD_COUNT;
            UpdateWorldPreview(selectedWorld);
        }
        if (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D)) {
            selectedWorld = (selectedWorld + 1) % WORLD_COUNT;
            UpdateWorldPreview(selectedWorld);
        }
        if (IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER)) {
            StartGame(0);
        }

        if (clicked) {
            Rectangle backBtn  = { 40.0f, 28.0f, 175.0f, 42.0f };
            Rectangle prevBtn  = { 65.0f, 315.0f, 68.0f, 90.0f };
            Rectangle nextBtn  = { 1147.0f, 315.0f, 68.0f, 90.0f };
            Rectangle playBtn  = { 640.0f, 455.0f, 420.0f, 64.0f };

            if (CheckCollisionPointRec(mp, backBtn)) screen = SCR_MODE_SELECT;
            else if (CheckCollisionPointRec(mp, prevBtn)) {
                selectedWorld = (selectedWorld + WORLD_COUNT - 1) % WORLD_COUNT;
                UpdateWorldPreview(selectedWorld);
            }
            else if (CheckCollisionPointRec(mp, nextBtn)) {
                selectedWorld = (selectedWorld + 1) % WORLD_COUNT;
                UpdateWorldPreview(selectedWorld);
            }
            else if (CheckCollisionPointRec(mp, playBtn)) {
                StartGame(0);
            }

            // Bottom 1..6 world quick-jump pills
            for (int w = 0; w < WORLD_COUNT; w++) {
                Rectangle pill = { 340.0f + w * 102.0f, 595.0f, 88.0f, 46.0f };
                if (CheckCollisionPointRec(mp, pill)) {
                    selectedWorld = w;
                    UpdateWorldPreview(selectedWorld);
                }
            }
        }
    }
    // ------------------------------------------
    // SCREEN 3: COUNTDOWN & GAMEPLAY
    // ------------------------------------------
    else if (screen == SCR_COUNTDOWN || screen == SCR_PLAYING) {
        if (IsKeyPressed(KEY_TAB)) autoPlay = !autoPlay;
        if (IsKeyPressed(KEY_ESCAPE)) screen = (currentMode == MODE_ENDLESS) ? SCR_MODE_SELECT : SCR_WORLD_SELECT;
        if (IsKeyPressed(KEY_R)) StartGame(0);

        // Top-right clickable Menu button during gameplay
        if (clicked && CheckCollisionPointRec(mp, { 1135.0f, 18.0f, 115.0f, 38.0f })) {
            screen = (currentMode == MODE_ENDLESS) ? SCR_MODE_SELECT : SCR_WORLD_SELECT;
        }

        float angularSpeed = PI * (currentBpm / 60.0f);
        float beatDuration = 60.0f / currentBpm;

        if (screen == SCR_COUNTDOWN) {
            beatTimer += dt;
            currentAngle += (cw ? angularSpeed : -angularSpeed) * dt;

            if (beatTimer >= beatDuration) {
                beatTimer -= beatDuration;
                countdownBeats--;
                totalBeatCount++;
                cameraBeatPulse = 0.045f;

                if (countdownBeats > 0) {
                    PlaySound(totalBeatCount % 2 == 0 ? sndKick : sndSnare);
                } else {
                    traveledAngle = 0.0f;
                    PlaySound(sndKick);
                    int wIdx = (currentMode == MODE_ENDLESS) ? 2 : selectedWorld;
                    PlaySound(sndMelody[worlds[wIdx].instrumentType][worlds[wIdx].melodyNotes[0]]);
                    PlaySound(sndBass[worlds[wIdx].bassNotes[0]]);
                    screen = SCR_PLAYING;
                }
            }
        }
        else if (screen == SCR_PLAYING) {
            // Distinct Per-World Musical Sequencer (8th-note grid)
            beatTimer += dt;
            if (beatTimer >= beatDuration * 0.5f) {
                beatTimer -= beatDuration * 0.5f;
                totalBeatCount++;

                int wIdx = (currentMode == MODE_ENDLESS) ? ((endlessTilesCleared / 16) % WORLD_COUNT) : selectedWorld;
                int inst = worlds[wIdx].instrumentType;
                int step8 = totalBeatCount % 8;
                int beat4 = (totalBeatCount / 2) % 4;

                // Play distinct melody note on every 8th note
                int noteIdx = worlds[wIdx].melodyNotes[step8];
                PlaySound(sndMelody[inst][noteIdx]);

                if (totalBeatCount % 2 == 0) {
                    cameraBeatPulse = 0.036f;
                    PlaySound(beat4 % 2 == 0 ? sndKick : sndSnare);
                    if (beat4 % 2 == 0) {
                        PlaySound(sndBass[worlds[wIdx].bassNotes[beat4]]);
                    }
                } else {
                    PlaySound(sndHat);
                }
            }

            float step = angularSpeed * dt;
            traveledAngle += step;
            currentAngle += (cw ? step : -step);

            float offsetRad = (audioOffsetMs / 1000.0f) * angularSpeed;
            float errorAngle = (traveledAngle - offsetRad) - targetDiff;
            float errorSec = errorAngle / angularSpeed;

            float pureWin, perfWin, maxWin;
            GetJudgmentWindows(pureWin, perfWin, maxWin);

            bool triggered = autoPlay ? (errorAngle >= 0.0f) : IsHitKeyPressed();

            if (triggered) {
                if (fabsf(errorSec) <= maxWin) {
                    float errMs = errorSec * 1000.0f;
                    Color jCol = GREEN;
                    std::string jText = "PERFECT!";
                    float accVal = 1.0f;
                    bool isGoodHit = true;

                    if (autoPlay || fabsf(errorSec) <= pureWin) {
                        jText = "PURE PERFECT!"; jCol = { 70, 255, 195, 255 }; accVal = 1.0f; perfectCount++;
                    } else if (errorSec < 0) {
                        if (fabsf(errorSec) <= perfWin) { jText = "EARLY"; jCol = { 120, 245, 95, 255 }; accVal = 0.85f; }
                        else { jText = "TOO EARLY"; jCol = GOLD; accVal = 0.50f; isGoodHit = false; }
                    } else {
                        if (fabsf(errorSec) <= perfWin) { jText = "LATE"; jCol = { 120, 245, 95, 255 }; accVal = 0.85f; }
                        else { jText = "TOO LATE"; jCol = GOLD; accVal = 0.50f; isGoodHit = false; }
                    }

                    // Perfectionist Mode fails on ANY non-green hit
                    if (currentMode == MODE_PERFECTIONIST && !isGoodHit) {
                        PlaySound(sndFail);
                        screenShake = 16.0f;
                        failReason = "PERFECTIONIST MODE: IMPERFECT TIMING!";
                        SpawnHitParticles(tiles[currentTile + 1].pos, RED, 32);
                        screen = SCR_GAMEOVER;
                    } else {
                        hitCount++;
                        streakCount++;
                        accuracySum += accVal;
                        hitTicks.push_back({ autoPlay ? 0.0f : errMs, jCol, 1.0f });
                        if (hitTicks.size() > 26) hitTicks.pop_front();

                        Vector2 hitTilePos = tiles[currentTile + 1].pos;
                        AddPopup(jText, hitTilePos, jCol);
                        SpawnHitParticles(hitTilePos, isFirePivot ? SKYBLUE : ORANGE, 18);
                        SpawnShockwave(hitTilePos, jCol, 72.0f);
                        PlaySound(sndHit);

                        screenShake = 4.5f;
                        AdvanceTileAfterHit(hitTilePos, false);
                    }
                } else {
                    // Too early input
                    if (currentMode == MODE_ZEN) {
                        screenShake = 8.0f;
                        streakCount = 0;
                        AddPopup("TOO EARLY!", pivotPos, ORANGE);
                    } else {
                        PlaySound(sndFail);
                        screenShake = 16.0f;
                        failReason = "TOO EARLY! (OVERLOAD)";
                        SpawnHitParticles(pivotPos, RED, 32);
                        SpawnShockwave(pivotPos, RED, 120.0f);
                        screen = SCR_GAMEOVER;
                    }
                }
            }
            else if (errorSec > maxWin) {
                // Missed beat
                if (currentMode == MODE_ZEN) {
                    streakCount = 0;
                    hitCount++;
                    Vector2 hitTilePos = tiles[currentTile + 1].pos;
                    AddPopup("MISSED (ZEN)", hitTilePos, RED);
                    AdvanceTileAfterHit(hitTilePos, true);
                } else {
                    PlaySound(sndFail);
                    screenShake = 16.0f;
                    failReason = "MISSED THE BEAT!";
                    SpawnHitParticles(tiles[currentTile + 1].pos, RED, 32);
                    SpawnShockwave(tiles[currentTile + 1].pos, RED, 120.0f);
                    screen = SCR_GAMEOVER;
                }
            }
        }
    }
    // ------------------------------------------
    // SCREEN 4 & 5: GAMEOVER / CLEAR
    // ------------------------------------------
    else if (screen == SCR_GAMEOVER) {
        if (IsKeyPressed(KEY_ESCAPE)) screen = (currentMode == MODE_ENDLESS) ? SCR_MODE_SELECT : SCR_WORLD_SELECT;
        else if (IsKeyPressed(KEY_R)) StartGame(0);
        else if (IsHitKeyPressed())   StartGame(currentMode == MODE_JOURNEY ? lastCheckpoint : 0);
    }
    else if (screen == SCR_CLEAR) {
        if (IsKeyPressed(KEY_ESCAPE)) screen = SCR_WORLD_SELECT;
        else if (IsHitKeyPressed()) {
            selectedWorld = (selectedWorld + 1) % WORLD_COUNT;
            UpdateWorldPreview(selectedWorld);
            StartGame(0);
        }
    }

    // Camera & Particle Updates
    if (!tiles.empty() && (screen == SCR_COUNTDOWN || screen == SCR_PLAYING || screen == SCR_GAMEOVER || screen == SCR_CLEAR)) {
        camera.target = Vector2Lerp(camera.target, pivotPos, 1.0f - expf(-9.0f * dt));
        cameraBeatPulse = Lerp(cameraBeatPulse, 0.0f, 1.0f - expf(-12.0f * dt));
        camera.zoom = 1.0f + cameraBeatPulse;

        if (screenShake > 0.0f) {
            camera.offset.x = SCREEN_WIDTH / 2.0f + GetRandomValue(-10, 10) * (screenShake * 0.14f);
            camera.offset.y = SCREEN_HEIGHT / 2.0f + GetRandomValue(-10, 10) * (screenShake * 0.14f);
            screenShake = fmaxf(0.0f, screenShake - 45.0f * dt);
        } else {
            camera.offset = { SCREEN_WIDTH / 2.0f, SCREEN_HEIGHT / 2.0f };
        }
    }

    Vector2 firePos = { 0, 0 }, icePos = { 0, 0 };
    if (!tiles.empty()) {
        Vector2 orbitPos = {
            pivotPos.x + cosf(currentAngle) * TILE_DIST,
            pivotPos.y + sinf(currentAngle) * TILE_DIST
        };
        firePos = isFirePivot ? pivotPos : orbitPos;
        icePos  = isFirePivot ? orbitPos : pivotPos;

        if (screen == SCR_PLAYING || screen == SCR_COUNTDOWN) {
            fireTrail.push_back({ firePos, { 255, 80, 45, 255 }, 0.78f });
            iceTrail.push_back({ icePos, { 45, 205, 255, 255 }, 0.78f });
            if (fireTrail.size() > 24) fireTrail.pop_front();
            if (iceTrail.size() > 24) iceTrail.pop_front();
        }
    }

    for (auto& t : tiles) if (t.hitAnim > 0.0f) t.hitAnim = fmaxf(0.0f, t.hitAnim - 4.2f * dt);
    for (auto& tr : fireTrail) tr.alpha *= 0.89f;
    for (auto& tr : iceTrail)  tr.alpha *= 0.89f;
    for (auto& tk : hitTicks)  tk.alpha = fmaxf(0.15f, tk.alpha - 0.12f * dt);

    for (int i = (int)shockwaves.size() - 1; i >= 0; i--) {
        shockwaves[i].radius = Lerp(shockwaves[i].radius, shockwaves[i].maxRadius, 1.0f - expf(-9.0f * dt));
        shockwaves[i].alpha -= 1.8f * dt;
        if (shockwaves[i].alpha <= 0.0f) shockwaves.erase(shockwaves.begin() + i);
    }
    for (int i = (int)particles.size() - 1; i >= 0; i--) {
        particles[i].pos = Vector2Add(particles[i].pos, Vector2Scale(particles[i].vel, dt));
        particles[i].vel = Vector2Scale(particles[i].vel, 0.92f);
        particles[i].life -= dt;
        if (particles[i].life <= 0.0f) particles.erase(particles.begin() + i);
    }
    for (int i = (int)popups.size() - 1; i >= 0; i--) {
        popups[i].pos.y -= 38.0f * dt;
        popups[i].timer -= dt;
        if (popups[i].timer <= 0.0f) popups.erase(popups.begin() + i);
    }

    // ==========================================
    // 8. Rendering
    // ==========================================
    BeginDrawing();
    Color bgCol = ColorFromHSV(bgHue, 0.45f, 0.10f + cameraBeatPulse * 0.55f);
    ClearBackground(bgCol);

    for (const auto& m : motes) {
        float pulseAlpha = 0.12f + 0.08f * sinf(m.phase);
        DrawCircleV(m.pos, m.size, Fade(ColorFromHSV(bgHue, 0.5f, 0.95f), pulseAlpha));
    }
    DrawCircleLinesV({ SCREEN_WIDTH / 2.0f, SCREEN_HEIGHT / 2.0f }, 260.0f + cameraBeatPulse * 420.0f, Fade(WHITE, 0.045f));
    DrawCircleLinesV({ SCREEN_WIDTH / 2.0f, SCREEN_HEIGHT / 2.0f }, 460.0f + cameraBeatPulse * 620.0f, Fade(WHITE, 0.03f));

    // ------------------------------------------
    // RENDER 1: CLEAN 2x2 MODE SELECT SCREEN
    // ------------------------------------------
    if (screen == SCR_MODE_SELECT) {
        DrawTextCentered("A DANCE OF FIRE AND ICE", SCREEN_WIDTH / 2.0f, 48.0f, 50.0f, WHITE);
        DrawTextCentered("SELECT GAME MODE", SCREEN_WIDTH / 2.0f, 108.0f, 20.0f, SKYBLUE);

        const char* modeTitles[4] = { "WORLD TOUR", "ENDLESS RUSH", "ZEN PRACTICE", "PERFECTIONIST" };
        const char* modeBadges[4] = { "6 WORLDS", "SURVIVAL", "NO GAME OVER", "1-HIT CHALLENGE" };
        const char* modeDescs[4]  = {
            "Progress from Easy to Master across 6 distinct songs & courses.",
            "Infinite procedural track! Survive as many tiles as possible.",
            "Relax and practice any world without failing when you miss.",
            "Every hit must be Pure Perfect or Perfect. Zero margin for error."
        };
        float modeHues[4] = { 24.0f, 185.0f, 145.0f, 345.0f };

        for (int i = 0; i < 4; i++) {
            int col = i % 2;
            int row = i / 2;
            Rectangle card = { 170.0f + col * 485.0f, 185.0f + row * 165.0f, 455.0f, 140.0f };
            bool sel = ((int)currentMode == i) || CheckCollisionPointRec(mp, card);
            Color theme = ColorFromHSV(modeHues[i], 0.65f, 0.95f);

            DrawCardBox(card, 0.16f,
                        sel ? Fade(theme, 0.22f) : Color{ 20, 24, 38, 235 },
                        sel ? theme : Fade(WHITE, 0.16f),
                        sel ? 2.5f : 1.5f);

            DrawTextUI(modeTitles[i], card.x + 24, card.y + 20, 28.0f, WHITE);
            DrawRectangleRounded({ card.x + 285, card.y + 22, 148.0f, 26.0f }, 0.4f, 6, Fade(theme, 0.25f));
            DrawTextCentered(modeBadges[i], card.x + 359, card.y + 27, 14.0f, theme);

            DrawTextUI(modeDescs[i], card.x + 24, card.y + 68, 16.0f, LIGHTGRAY);
            if (i == 1 && bestEndlessScore > 0) {
                DrawTextUI(TextFormat("BEST RECORD: %d TILES", bestEndlessScore), card.x + 24, card.y + 102, 16.0f, GOLD);
            } else {
                DrawTextUI("Click or Press [SPACE] to Enter ->", card.x + 24, card.y + 102, 15.0f, theme);
            }
        }

        // Clean Clickable Settings Bar
        DrawCardBox({ 155.0f, 532.0f, 970.0f, 68.0f }, 0.25f, Color{ 18, 22, 34, 235 }, Fade(WHITE, 0.14f), 1.5f);

        DrawTextUI("TIMING:", 180.0f, 556.0f, 17.0f, LIGHTGRAY);
        const char* sLabels[3] = { "EASY", "NORMAL", "STRICT" };
        for (int s = 0; s < 3; s++) {
            Rectangle b = { 255.0f + s * 98.0f, 548.0f, 90.0f, 36.0f };
            bool active = ((int)strictness == s);
            DrawRectangleRounded(b, 0.35f, 6, active ? SKYBLUE : Fade(WHITE, 0.08f));
            DrawTextCentered(sLabels[s], b.x + b.width * 0.5f, b.y + 9.0f, 16.0f, active ? BLACK : WHITE, false);
        }

        DrawTextUI("AUTOPLAY:", 575.0f, 556.0f, 17.0f, LIGHTGRAY);
        Rectangle autoBtn = { 665.0f, 548.0f, 100.0f, 36.0f };
        DrawRectangleRounded(autoBtn, 0.35f, 6, autoPlay ? GOLD : Fade(WHITE, 0.08f));
        DrawTextCentered(autoPlay ? "ON" : "OFF", autoBtn.x + 50.0f, autoBtn.y + 9.0f, 16.0f, autoPlay ? BLACK : WHITE, false);

        DrawTextUI("SYNC:", 840.0f, 556.0f, 17.0f, LIGHTGRAY);
        Rectangle offMinus = { 895.0f, 548.0f, 40.0f, 36.0f };
        Rectangle offPlus  = { 1035.0f, 548.0f, 40.0f, 36.0f };
        DrawRectangleRounded(offMinus, 0.35f, 6, Fade(WHITE, 0.12f));
        DrawRectangleRounded(offPlus, 0.35f, 6, Fade(WHITE, 0.12f));
        DrawTextCentered("-", offMinus.x + 20.0f, offMinus.y + 7.0f, 20.0f, WHITE, false);
        DrawTextCentered("+", offPlus.x + 20.0f, offPlus.y + 7.0f, 20.0f, WHITE, false);
        DrawTextCentered(TextFormat("%.0f ms", audioOffsetMs), 985.0f, 556.0f, 17.0f, WHITE);

        EndDrawing();
        return;
    }

    // ------------------------------------------
    // RENDER 2: FOCUSED WORLD SHOWCASE SELECT
    // ------------------------------------------
    if (screen == SCR_WORLD_SELECT) {
        WorldInfo& w = worlds[selectedWorld];
        Color theme = ColorFromHSV(w.hue, 0.68f, 0.95f);

        // Top Bar
        Rectangle backBtn = { 40.0f, 28.0f, 175.0f, 42.0f };
        DrawCardBox(backBtn, 0.3f, Color{ 24, 28, 44, 240 }, Fade(WHITE, 0.25f), 1.5f);
        DrawTextCentered("< MODES [ESC]", backBtn.x + backBtn.width * 0.5f, backBtn.y + 11.0f, 17.0f, WHITE);

        const char* modeBanner = (currentMode == MODE_JOURNEY) ? "MODE: WORLD TOUR" :
                                 (currentMode == MODE_ZEN)     ? "MODE: ZEN PRACTICE (NO-FAIL)" :
                                                                 "MODE: PERFECTIONIST CHALLENGE";
        DrawTextCentered(modeBanner, SCREEN_WIDTH / 2.0f, 36.0f, 22.0f, theme);

        // Left / Right Carousel Arrows
        Rectangle prevBtn = { 65.0f, 315.0f, 68.0f, 90.0f };
        Rectangle nextBtn = { 1147.0f, 315.0f, 68.0f, 90.0f };
        DrawCardBox(prevBtn, 0.3f, Color{ 24, 28, 44, 240 }, theme, 2.0f);
        DrawCardBox(nextBtn, 0.3f, Color{ 24, 28, 44, 240 }, theme, 2.0f);
        DrawTextCentered("<", prevBtn.x + 34.0f, prevBtn.y + 24.0f, 42.0f, WHITE);
        DrawTextCentered(">", nextBtn.x + 34.0f, nextBtn.y + 24.0f, 42.0f, WHITE);

        // Center Showcase Card
        Rectangle showcase = { 165.0f, 115.0f, 950.0f, 435.0f };
        DrawCardBox(showcase, 0.08f, Color{ 18, 22, 35, 245 }, theme, 2.5f);

        // Left Half: Course Shape Live Mini-Map Preview
        Rectangle mapBox = { 205.0f, 155.0f, 390.0f, 355.0f };
        DrawRectangleRounded(mapBox, 0.1f, 8, Color{ 12, 15, 24, 255 });
        DrawTextCentered("COURSE SHAPE PREVIEW", mapBox.x + mapBox.width * 0.5f, mapBox.y + 16.0f, 15.0f, GRAY);

        Vector2 mapCenter = { mapBox.x + mapBox.width * 0.5f, mapBox.y + mapBox.height * 0.53f };
        float mapRadius = 125.0f;
        for (size_t i = 0; i + 1 < previewPoints.size(); i++) {
            Vector2 p1 = { mapCenter.x + previewPoints[i].x * mapRadius, mapCenter.y + previewPoints[i].y * mapRadius };
            Vector2 p2 = { mapCenter.x + previewPoints[i + 1].x * mapRadius, mapCenter.y + previewPoints[i + 1].y * mapRadius };
            DrawLineEx(p1, p2, 6.0f, Fade(theme, 0.75f));
            DrawCircleV(p1, 4.5f, WHITE);
        }
        if (!previewPoints.empty()) {
            Vector2 startPt = { mapCenter.x + previewPoints[0].x * mapRadius, mapCenter.y + previewPoints[0].y * mapRadius };
            Vector2 endPt   = { mapCenter.x + previewPoints.back().x * mapRadius, mapCenter.y + previewPoints.back().y * mapRadius };
            DrawCircleV(startPt, 7.0f, SKYBLUE);
            DrawCircleV(endPt, 7.0f, GOLD);
        }

        // Right Half: Clean World Details & Big Play Button
        float rx = 640.0f;
        DrawTextUI(TextFormat("WORLD %d  •  %s", selectedWorld + 1, w.diffLabel), rx, 155.0f, 20.0f, theme);
        DrawTextUI(w.name, rx, 188.0f, 40.0f, WHITE);

        // Segmented Difficulty Bar
        DrawTextUI("DIFFICULTY:", rx, 248.0f, 16.0f, GRAY);
        for (int d = 0; d < 6; d++) {
            Color segCol = (d < w.difficultyLevel) ? theme : Fade(WHITE, 0.12f);
            DrawRectangleRounded({ rx + 115.0f + d * 48.0f, 246.0f, 40.0f, 18.0f }, 0.3f, 4, segCol);
        }

        DrawTextUI(TextFormat("MUSIC:  %s  (%.0f BPM Steady)", w.songStyle, w.bpm), rx, 290.0f, 19.0f, LIGHTGRAY);
        DrawTextUI(TextFormat("COURSE: %s", w.gimmickDesc), rx, 325.0f, 18.0f, LIGHTGRAY);

        if (w.cleared) {
            DrawTextUI(TextFormat("STATUS: CLEARED!  (Best Accuracy: %.1f%%)", w.bestAcc), rx, 368.0f, 18.0f, GREEN);
        } else {
            DrawTextUI("STATUS: NOT CLEARED YET", rx, 368.0f, 18.0f, GRAY);
        }

        // Big Play Button
        Rectangle playBtn = { rx, 445.0f, 420.0f, 64.0f };
        bool hoverPlay = CheckCollisionPointRec(mp, playBtn);
        DrawRectangleRounded(playBtn, 0.25f, 8, hoverPlay ? WHITE : theme);
        DrawTextCentered("START WORLD  [SPACE]", playBtn.x + playBtn.width * 0.5f, playBtn.y + 18.0f, 24.0f, BLACK, false);

        // Bottom Quick-Jump World Pills (1 to 6)
        for (int i = 0; i < WORLD_COUNT; i++) {
            Rectangle pill = { 340.0f + i * 102.0f, 595.0f, 88.0f, 46.0f };
            bool isSel = (i == selectedWorld);
            Color pCol = ColorFromHSV(worlds[i].hue, 0.65f, 0.95f);
            DrawCardBox(pill, 0.3f, isSel ? Fade(pCol, 0.32f) : Color{ 20, 24, 38, 230 }, isSel ? pCol : Fade(WHITE, 0.16f), 2.0f);
            DrawTextCentered(TextFormat("W-%d", i + 1), pill.x + 44.0f, pill.y + 13.0f, 18.0f, isSel ? WHITE : LIGHTGRAY);
        }

        EndDrawing();
        return;
    }

    // ------------------------------------------
    // RENDER 3: WORLD CAMERA GAMEPLAY
    // ------------------------------------------
    BeginMode2D(camera);
    if (!tiles.empty()) {
        Vector2 renderPivot = tiles[0].pos;
        for (size_t i = 0; i + 1 < tiles.size(); i++) {
            Vector2 p1 = renderPivot;
            Vector2 p2 = tiles[i + 1].pos;
            Color borderCol = tiles[i].cleared ? Color{ 95, 130, 185, 255 } : Color{ 52, 65, 98, 255 };
            Color coreCol   = tiles[i].cleared ? Color{ 38, 54, 82, 255 }  : Color{ 22, 28, 44, 255 };
            DrawLineEx({ p1.x, p1.y + 6.0f }, { p2.x, p2.y + 6.0f }, 50.0f, Fade(BLACK, 0.35f));
            DrawLineEx(p1, p2, 48.0f, borderCol);
            DrawLineEx(p1, p2, 36.0f, coreCol);
            if (!tiles[i + 1].midspin) {
                renderPivot = tiles[i + 1].pos;
            }
        }

        // Orbit guide & Remaining angle arc
        DrawCircleLinesV(pivotPos, TILE_DIST, Fade(WHITE, 0.16f));
        if (screen == SCR_PLAYING) {
            float remRatio = Clamp(1.0f - (traveledAngle / targetDiff), 0.0f, 1.0f);
            float startDeg = currentAngle * RAD2DEG;
            float endDeg   = (currentAngle + (cw ? 1.0f : -1.0f) * targetDiff * remRatio) * RAD2DEG;
            DrawRing(pivotPos, TILE_DIST - 6.0f, TILE_DIST + 2.0f, startDeg, endDeg, 24,
                     Fade(isFirePivot ? SKYBLUE : ORANGE, 0.24f));
        }

        float timeNow = (float)GetTime();
        for (size_t i = 0; i < tiles.size(); i++) {
            float scale = 1.0f + tiles[i].hitAnim * 0.28f;
            Color rimCol = (i == (size_t)currentTile) ? WHITE :
                           (tiles[i].cleared ? Color{ 110, 150, 210, 255 } : Color{ 70, 86, 122, 255 });
            if (currentMode != MODE_ENDLESS && i == tiles.size() - 1) rimCol = GOLD;

            if (tiles[i].midspin) {
                DrawPoly(tiles[i].pos, 3, 26.0f * scale, timeNow * 70.0f, MAGENTA);
                DrawPoly(tiles[i].pos, 3, 16.0f * scale, timeNow * 70.0f, { 26, 30, 46, 255 });
            } else {
                DrawCircleV({ tiles[i].pos.x, tiles[i].pos.y + 4.0f }, 24.0f * scale, Fade(BLACK, 0.35f));
                DrawCircleV(tiles[i].pos, 24.5f * scale, rimCol);
                DrawCircleV(tiles[i].pos, 18.0f * scale, { 22, 26, 40, 255 });
            }

            if (currentMode != MODE_ENDLESS && i == tiles.size() - 1) {
                DrawRing(tiles[i].pos, 11.0f, 15.0f, timeNow * 140.0f, timeNow * 140.0f + 240.0f, 24, GOLD);
                DrawCircleV(tiles[i].pos, 7.0f + 2.0f * sinf(timeNow * 6.0f), WHITE);
            }

            if (tiles[i].swirl) {
                float rot = timeNow * 200.0f;
                DrawRing(tiles[i].pos, 9.0f, 13.5f, rot, rot + 140.0f, 16, { 240, 95, 255, 255 });
                DrawRing(tiles[i].pos, 9.0f, 13.5f, rot + 180.0f, rot + 320.0f, 16, { 240, 95, 255, 255 });
            }

            if (tiles[i].checkpoint && currentMode == MODE_JOURNEY) {
                DrawCircleLinesV(tiles[i].pos, 29.5f, GREEN);
                DrawCircleLinesV(tiles[i].pos, 32.5f, Fade(GREEN, 0.45f));
            }
        }

        for (const auto& sw : shockwaves) {
            DrawRing(sw.pos, sw.radius - 3.0f, sw.radius, 0, 360, 32, Fade(sw.color, sw.alpha));
        }

        for (size_t i = 0; i < fireTrail.size(); i++) {
            float r = PLANET_RADIUS * ((float)i / fireTrail.size());
            DrawCircleV(fireTrail[i].pos, r, Fade(fireTrail[i].color, fireTrail[i].alpha));
        }
        for (size_t i = 0; i < iceTrail.size(); i++) {
            float r = PLANET_RADIUS * ((float)i / iceTrail.size());
            DrawCircleV(iceTrail[i].pos, r, Fade(iceTrail[i].color, iceTrail[i].alpha));
        }

        for (const auto& pt : particles) {
            DrawCircleV(pt.pos, pt.size * (pt.life / pt.maxLife), Fade(pt.color, pt.life / pt.maxLife));
        }

        DrawCircleV(firePos, PLANET_RADIUS + 8.0f, Fade(ORANGE, 0.25f));
        DrawCircleV(firePos, PLANET_RADIUS, { 255, 65, 45, 255 });
        DrawCircleV(firePos, PLANET_RADIUS * 0.48f, { 255, 220, 150, 255 });

        DrawCircleV(icePos, PLANET_RADIUS + 8.0f, Fade(SKYBLUE, 0.25f));
        DrawCircleV(icePos, PLANET_RADIUS, { 45, 195, 255, 255 });
        DrawCircleV(icePos, PLANET_RADIUS * 0.48f, { 215, 248, 255, 255 });

        for (const auto& p : popups) {
            DrawTextCentered(p.text.c_str(), p.pos.x, p.pos.y, 19.0f, Fade(p.color, Clamp(p.timer * 1.4f, 0.0f, 1.0f)));
        }
    }
    EndMode2D();

    // --- CLEAN HUD ---
    float accPct = (hitCount > 0) ? (accuracySum / hitCount * 100.0f) : 100.0f;

    if (currentMode == MODE_ENDLESS) {
        DrawTextUI(TextFormat("ENDLESS RUSH  •  TILES: %d  (BEST: %d)", endlessTilesCleared, bestEndlessScore), 28.0f, 20.0f, 24.0f, WHITE);
        DrawTextUI(TextFormat("ACCURACY: %.1f%%   |   STREAK: %d   |   BPM: %.0f", accPct, streakCount, currentBpm), 28.0f, 50.0f, 18.0f, SKYBLUE);
    } else {
        float ratio = (float)currentTile / (float)(tiles.size() - 1);
        DrawTextUI(TextFormat("WORLD %d: %s", selectedWorld + 1, worlds[selectedWorld].name), 28.0f, 18.0f, 24.0f, WHITE);

        // Visual Progress Bar
        DrawRectangleRounded({ 28.0f, 52.0f, 260.0f, 12.0f }, 0.5f, 4, Fade(WHITE, 0.15f));
        DrawRectangleRounded({ 28.0f, 52.0f, 260.0f * ratio, 12.0f }, 0.5f, 4, ColorFromHSV(bgHue, 0.65f, 0.95f));
        DrawTextUI(TextFormat("%d%%   •   ACC: %.1f%%", (int)(ratio * 100.0f), accPct), 300.0f, 48.0f, 18.0f, LIGHTGRAY);
    }

    // Top-Right Menu Button & Autoplay indicator
    Rectangle menuBtn = { 1135.0f, 18.0f, 115.0f, 38.0f };
    DrawCardBox(menuBtn, 0.3f, Color{ 20, 24, 38, 220 }, Fade(WHITE, 0.25f), 1.5f);
    DrawTextCentered("MENU [ESC]", menuBtn.x + 57.5f, menuBtn.y + 10.0f, 15.0f, WHITE);

    if (autoPlay) {
        DrawTextUI("[AUTOPLAY]", 1005.0f, 26.0f, 18.0f, GOLD);
    }

    // Bottom Timing Meter with Clear Labels
    int barW = 380, barH = 10;
    int barX = SCREEN_WIDTH / 2 - barW / 2;
    int barY = SCREEN_HEIGHT - 44;
    DrawRectangleRounded({ (float)barX - 4, (float)barY - 6, (float)barW + 8, (float)barH + 12 }, 0.4f, 6, Fade(BLACK, 0.45f));
    DrawRectangle(barX, barY, barW, barH, Fade(GOLD, 0.45f));
    DrawRectangle(barX + 70, barY, barW - 140, barH, Fade(GREEN, 0.65f));
    DrawRectangle(barX + 140, barY, barW - 280, barH, Fade(SKYBLUE, 0.85f));
    DrawRectangle(SCREEN_WIDTH / 2 - 2, barY - 6, 4, barH + 12, WHITE);

    DrawTextUI("EARLY", (float)barX - 58.0f, (float)barY - 3.0f, 14.0f, GOLD);
    DrawTextUI("LATE", (float)barX + barW + 12.0f, (float)barY - 3.0f, 14.0f, GOLD);

    for (const auto& tk : hitTicks) {
        float clamped = Clamp(tk.errorMs, -160.0f, 160.0f);
        int tx = SCREEN_WIDTH / 2 + (int)((clamped / 160.0f) * (barW / 2.0f));
        DrawRectangle(tx - 2, barY - 5, 4, barH + 10, Fade(tk.color, tk.alpha));
    }

    // --- OVERLAYS ---
    if (screen == SCR_COUNTDOWN) {
        const char* cdText = (countdownBeats == 3) ? "3" :
                             (countdownBeats == 2) ? "2" :
                             (countdownBeats == 1) ? "1" : "GET READY";
        DrawTextCentered(cdText, SCREEN_WIDTH / 2.0f, 135.0f, 64.0f, GOLD);
    }
    else if (screen == SCR_GAMEOVER) {
        DrawRectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, Fade(BLACK, 0.70f));
        DrawTextCentered("SHATTERED!", SCREEN_WIDTH / 2.0f, 215.0f, 58.0f, RED);
        DrawTextCentered(failReason.c_str(), SCREEN_WIDTH / 2.0f, 290.0f, 24.0f, LIGHTGRAY);
        if (currentMode == MODE_ENDLESS) {
            DrawTextCentered(TextFormat("Survived %d Tiles  (Best: %d)", endlessTilesCleared, bestEndlessScore),
                             SCREEN_WIDTH / 2.0f, 332.0f, 22.0f, SKYBLUE);
        }
        const char* retryMsg = (currentMode == MODE_JOURNEY && lastCheckpoint > 0)
            ? "[SPACE] Checkpoint   |   [R] Full Restart   |   [ESC] Menu"
            : "[SPACE] Try Again   |   [ESC] Menu";
        DrawTextCentered(retryMsg, SCREEN_WIDTH / 2.0f, 390.0f, 22.0f, WHITE);
    }
    else if (screen == SCR_CLEAR) {
        DrawRectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, Fade(BLACK, 0.70f));
        DrawTextCentered("WORLD CLEARED!", SCREEN_WIDTH / 2.0f, 215.0f, 58.0f, GOLD);
        DrawTextCentered(TextFormat("Final Accuracy: %.2f%%   (Pure Perfect: %d / %d)", accPct, perfectCount, hitCount),
                         SCREEN_WIDTH / 2.0f, 292.0f, 24.0f, SKYBLUE);
        DrawTextCentered("[SPACE] Next World   |   [ESC] World Select",
                         SCREEN_WIDTH / 2.0f, 365.0f, 22.0f, WHITE);
    }

    EndDrawing();
}

int main() {
    SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "A Dance of Fire and Ice - C++");

    gameFont = LoadFontEx("font.ttf", 64, 0, 0);
    if (gameFont.texture.id == 0) {
        gameFont = GetFontDefault();
    } else {
        SetTextureFilter(gameFont.texture, TEXTURE_FILTER_BILINEAR);
    }

    InitAmbientMotes();
    UpdateWorldPreview(0);

#if defined(PLATFORM_WEB)
    emscripten_set_main_loop(UpdateAndDrawFrame, 0, 1);
#else
    SetTargetFPS(60);
    while (!WindowShouldClose()) {
        UpdateAndDrawFrame();
    }
#endif

    if (gameFont.texture.id != GetFontDefault().texture.id) {
        UnloadFont(gameFont);
    }
    if (audioReady) {
        UnloadSound(sndHit); UnloadSound(sndKick); UnloadSound(sndSnare);
        UnloadSound(sndHat); UnloadSound(sndSwirl); UnloadSound(sndFail);
        UnloadSound(sndClear);
        for (int inst = 0; inst < 6; inst++) {
            for (int n = 0; n < 18; n++) UnloadSound(sndMelody[inst][n]);
        }
        for (int n = 0; n < 18; n++) UnloadSound(sndBass[n]);
        CloseAudioDevice();
    }
    CloseWindow();
    return 0;
}
