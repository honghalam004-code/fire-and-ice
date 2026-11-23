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
// 1. Constants & Data Structures
// ==========================================
const int SCREEN_WIDTH = 1280;
const int SCREEN_HEIGHT = 720;
const float TILE_DIST = 96.0f;
const float PLANET_RADIUS = 16.5f;
const int STAGE_COUNT = 6;

enum GameState {
    STATE_MENU,
    STATE_COUNTDOWN,
    STATE_PLAYING,
    STATE_GAMEOVER,
    STATE_CLEAR
};

enum Strictness {
    STRICT_LENIENT = 0,
    STRICT_NORMAL  = 1,
    STRICT_STRICT  = 2
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

struct StageMeta {
    const char* code;
    const char* title;
    const char* subtitle;
    float bpm;
    int stars;
    float hue;
    bool cleared;
    float bestAcc;
};

// ==========================================
// 2. Global State
// ==========================================
Font gameFont = { 0 };
GameState state = STATE_MENU;
Strictness strictness = STRICT_NORMAL;
int selectedStage = 0;
bool autoPlay = false;
float audioOffsetMs = 0.0f;

StageMeta stageMeta[STAGE_COUNT] = {
    { "1-X", "A Dance of Fire",    "Steady Basics & Swirls", 120.0f, 1, 14.0f,  false, 0.0f },
    { "2-X", "Waltz of Ice",       "Midspins & U-Turns",     128.0f, 2, 198.0f, false, 0.0f },
    { "3-X", "Neon Metropolis",    "Syncopated Stairs",      135.0f, 3, 265.0f, false, 0.0f },
    { "4-X", "Hexagonal Pulse",    "60-Deg Triplet Groove",  142.0f, 3, 152.0f, false, 0.0f },
    { "5-X", "Solar Flare",        "Midspin & Stair Chains", 150.0f, 4, 34.0f,  false, 0.0f },
    { "6-X", "Chrono Singularity", "Final Boss Rush",        158.0f, 5, 315.0f, false, 0.0f }
};

std::vector<Tile> tiles;
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
bool isFirePivot = true;
bool cw = true;

float baseBpm = 120.0f;
float currentBpm = 120.0f;
float currentAngle = 0.0f;
float targetDiff = PI;
float traveledAngle = 0.0f;

int countdownBeats = 4;
float beatTimer = 0.0f;
int totalBeatCount = 0;
float cameraBeatPulse = 0.0f;
float screenShake = 0.0f;
float bgHue = 14.0f;

int hitCount = 0;
int perfectCount = 0;
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
Sound sndPad[4];

// ==========================================
// 3. Typography & Math Helpers
// ==========================================
void DrawTextUI(const char* text, float x, float y, float size, Color color, bool shadow = true) {
    if (shadow) {
        DrawTextEx(gameFont, text, { x + 2.0f, y + 2.0f }, size, 1.2f, Fade(BLACK, (color.a / 255.0f) * 0.65f));
    }
    DrawTextEx(gameFont, text, { x, y }, size, 1.2f, color);
}

void DrawTextCentered(const char* text, float centerX, float y, float size, Color color, bool shadow = true) {
    Vector2 dim = MeasureTextEx(gameFont, text, size, 1.2f);
    DrawTextUI(text, centerX - dim.x * 0.5f, y, size, color, shadow);
}

float NormalizeAngle(float a) {
    while (a < 0.0f) a += 2.0f * PI;
    while (a >= 2.0f * PI) a -= 2.0f * PI;
    return a;
}

void GetJudgmentWindows(float& purePerfect, float& perfect, float& maxWindow) {
    if (strictness == STRICT_LENIENT) {
        purePerfect = 0.065f; perfect = 0.130f; maxWindow = 0.210f;
    } else if (strictness == STRICT_STRICT) {
        purePerfect = 0.032f; perfect = 0.070f; maxWindow = 0.125f;
    } else {
        purePerfect = 0.048f; perfect = 0.100f; maxWindow = 0.165f;
    }
}

// ==========================================
// 4. Procedural Audio Synthesizer
// ==========================================
Sound SynthWave(int type, float freq, float duration, float volume) {
    int sampleRate = 44100;
    int count = (int)(sampleRate * duration);
    short* data = (short*)RL_MALLOC(count * sizeof(short));

    for (int i = 0; i < count; i++) {
        float t = (float)i / sampleRate;
        float sample = 0.0f;

        if (type == 0) {
            float env = expf(-t * 36.0f);
            sample = sinf(2.0f * PI * freq * t) * 0.68f
                   + sinf(2.0f * PI * (freq * 1.5f) * t) * 0.32f;
            sample *= env;
        }
        else if (type == 1) {
            float env = expf(-t * 17.0f);
            float f = freq * expf(-t * 22.0f) + 44.0f;
            sample = sinf(2.0f * PI * f * t) * env;
        }
        else if (type == 2) {
            float env = expf(-t * (freq > 1000.0f ? 58.0f : 23.0f));
            float noise = ((float)GetRandomValue(-1000, 1000) / 1000.0f);
            float tone = (freq < 1000.0f) ? sinf(2.0f * PI * 185.0f * t) * 0.38f : 0.0f;
            sample = (noise * 0.72f + tone) * env;
        }
        else if (type == 3) {
            float env = expf(-t * 10.5f);
            float f = freq + (duration > 0.25f ? -freq * 0.65f * (t / duration) : freq * 0.55f * (t / duration));
            sample = (sinf(2.0f * PI * f * t) + 0.28f * sinf(4.0f * PI * f * t)) * env;
        }
        else if (type == 4) {
            float attack = fminf(1.0f, t * 45.0f);
            float env = attack * expf(-t * 6.5f);
            sample = (sinf(2.0f * PI * freq * t) * 0.55f
                    + sinf(2.0f * PI * (freq * 2.0f) * t) * 0.25f
                    + sinf(2.0f * PI * (freq * 0.5f) * t) * 0.20f) * env;
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
    sndHit   = SynthWave(0, 587.33f, 0.11f, 0.75f);
    sndKick  = SynthWave(1, 152.00f, 0.18f, 0.65f);
    sndSnare = SynthWave(2, 400.00f, 0.14f, 0.36f);
    sndHat   = SynthWave(2, 2600.0f, 0.05f, 0.20f);
    sndSwirl = SynthWave(3, 440.00f, 0.15f, 0.48f);
    sndFail  = SynthWave(3, 155.00f, 0.42f, 0.80f);
    sndClear = SynthWave(0, 880.00f, 0.48f, 0.80f);

    float padFreqs[4] = { 220.00f, 261.63f, 329.63f, 392.00f };
    for (int i = 0; i < 4; i++) {
        sndPad[i] = SynthWave(4, padFreqs[i], 0.42f, 0.26f);
    }
    audioReady = true;
}

// ==========================================
// 5. Six Steady-BPM Stage Definitions
// ==========================================
void BuildStage(int stageIdx) {
    tiles.clear();
    std::vector<MapStep> steps;
    baseBpm = stageMeta[stageIdx].bpm;
    bgHue   = stageMeta[stageIdx].hue;

    if (stageIdx == 0) {
        // 1-X: A Dance of Fire (120 BPM Constant)
        steps = {
            {0}, {0}, {0}, {0},
            {-90}, {0}, {-90}, {0},
            {90}, {0}, {90}, {0},
            {0, true}, {0}, {0}, {0},
            {90, false, false, true}, {0}, {90}, {0},
            {-90}, {0}, {-90}, {0},
            {0, true}, {0}, {-45}, {45}, {-45}, {45},
            {0}, {0}, {-90}, {0},
            {90}, {0}, {0}, {0}
        };
    }
    else if (stageIdx == 1) {
        // 2-X: Waltz of Ice (128 BPM Constant)
        steps = {
            {0}, {0}, {-90}, {0},
            {-90, false, true}, {0}, {90, false, true}, {0},
            {0, true}, {0}, {-45}, {-90}, {-45}, {0},
            {0, false, false, true},
            {180}, {0},
            {-45}, {45}, {-45}, {45},
            {-90, false, true}, {0}, {-90, false, true}, {0},
            {0, true}, {0}, {90}, {0}, {90}, {0},
            {0}, {0}, {-90}, {0},
            {0}, {0}, {0}, {0}
        };
    }
    else if (stageIdx == 2) {
        // 3-X: Neon Metropolis (135 BPM Constant)
        steps = {
            {0}, {-90}, {0}, {90}, {0},
            {0}, {0}, {-90}, {0}, {-90}, {0},
            {0, false, false, true},
            {0, true}, {0}, {90}, {0}, {90}, {0},
            {-90, false, true}, {0}, {90, false, true}, {0},
            {0}, {0}, {180}, {0},
            {0, false, false, true},
            {0}, {-45}, {45}, {-45}, {45}, {-90}, {0},
            {0}, {0, true}, {0}, {0}
        };
    }
    else if (stageIdx == 3) {
        // 4-X: Hexagonal Pulse (142 BPM Constant)
        steps = {
            {0}, {0}, {-60}, {0}, {60}, {0},
            {-60}, {-120}, {-60}, {0},
            {0, true}, {0}, {60}, {120}, {60}, {0},
            {0, false, false, true},
            {-60, false, true}, {0}, {60, false, true}, {0},
            {0}, {-60}, {0}, {60}, {0}, {-60}, {0}, {60}, {0},
            {0, false, false, true},
            {180, true}, {0}, {-60}, {60}, {-60}, {60},
            {0, true}, {0}, {0}, {0}
        };
    }
    else if (stageIdx == 4) {
        // 5-X: Solar Flare (150 BPM Constant)
        steps = {
            {0}, {-90}, {0}, {-90}, {0}, {90}, {0}, {90}, {0},
            {-90, false, true}, {0}, {-90, false, true}, {0},
            {0, true}, {90, false, true}, {0}, {90, false, true}, {0},
            {0, false, false, true},
            {0}, {0}, {-90}, {0}, {-90}, {0}, {90}, {0}, {90}, {0},
            {0, false, false, true},
            {-45, true}, {45}, {-45}, {45}, {-90}, {0}, {90}, {0},
            {180}, {0}, {-90, false, true}, {0},
            {0, true}, {0}, {0}, {0}
        };
    }
    else {
        // 6-X: Chrono Singularity (158 BPM Constant)
        steps = {
            {0}, {-90}, {0}, {90}, {0},
            {0, true}, {-90, true}, {0, true}, {90, true},
            {-90, false, true}, {0}, {-90, false, true}, {0},
            {0, false, false, true},
            {0}, {-45}, {45}, {-45}, {45}, {-90}, {0}, {-90}, {0},
            {0, false, false, true},
            {180, true}, {0}, {180, true}, {0},
            {-60}, {0}, {60}, {0}, {-60}, {0}, {60}, {0},
            {0, false, false, true},
            {0}, {-90}, {0}, {-90}, {0}, {90}, {0}, {90}, {0},
            {-90, false, true}, {0}, {90, false, true}, {0},
            {0}, {0, true}, {0}, {0}
        };
    }

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

// ==========================================
// 6. Visual Effects & Orbit Logic
// ==========================================
void InitAmbientMotes() {
    motes.clear();
    for (int i = 0; i < 48; i++) {
        motes.push_back({
            { (float)GetRandomValue(0, SCREEN_WIDTH), (float)GetRandomValue(0, SCREEN_HEIGHT) },
            { (float)GetRandomValue(-18, 18), (float)GetRandomValue(-28, -8) },
            (float)GetRandomValue(2, 5),
            (float)GetRandomValue(0, 628) / 100.0f
        });
    }
}

void SpawnHitParticles(Vector2 pos, Color col, int count) {
    for (int i = 0; i < count; i++) {
        float ang = (float)GetRandomValue(0, 360) * DEG2RAD;
        float spd = (float)GetRandomValue(85, 340);
        float life = (float)GetRandomValue(28, 55) / 100.0f;
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
        if (state == STATE_PLAYING) SpawnShockwave(pivotPos, MAGENTA, 95.0f);
        if (audioReady && state == STATE_PLAYING) PlaySound(sndSwirl);
    }

    float diff = cw ? NormalizeAngle(endAng - startAng) : NormalizeAngle(startAng - endAng);
    if (diff < 0.05f) diff += 2.0f * PI;

    currentAngle = startAng;
    targetDiff = diff;
    traveledAngle = 0.0f;
}

void StartStage(int stageIdx, int startFromTile = 0) {
    EnsureAudioInitialized();
    BuildStage(stageIdx);

    fireTrail.clear();
    iceTrail.clear();
    particles.clear();
    shockwaves.clear();
    popups.clear();
    hitTicks.clear();

    currentTile = startFromTile;
    lastCheckpoint = startFromTile;
    isFirePivot = (startFromTile % 2 == 0);
    cw = true;
    currentBpm = baseBpm;
    hitCount = 0;
    perfectCount = 0;
    accuracySum = 0.0f;
    failReason = "";

    Vector2 prevPiv = { 0, 0 };
    for (int i = 0; i <= startFromTile; i++) {
        tiles[i].cleared = true;
        if (i > 0 && tiles[i].swirl) {
            cw = !cw;
        }
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
    state = STATE_COUNTDOWN;
    PlaySound(sndKick);
    PlaySound(sndPad[0]);
}

bool IsHitKeyPressed() {
    return IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_Z) || IsKeyPressed(KEY_X) ||
           IsKeyPressed(KEY_J) || IsKeyPressed(KEY_K) || IsKeyPressed(KEY_D) ||
           IsKeyPressed(KEY_F) || IsMouseButtonPressed(MOUSE_LEFT_BUTTON);
}

// ==========================================
// 7. Main Update & Render Loop
// ==========================================
void UpdateAndDrawFrame() {
    float dt = GetFrameTime();
    if (dt > 0.05f) dt = 0.05f;

    if (IsKeyPressed(KEY_TAB)) autoPlay = !autoPlay;
    if (IsKeyPressed(KEY_LEFT_BRACKET))  audioOffsetMs = fmaxf(-150.0f, audioOffsetMs - 5.0f);
    if (IsKeyPressed(KEY_RIGHT_BRACKET)) audioOffsetMs = fminf(150.0f, audioOffsetMs + 5.0f);

    for (auto& m : motes) {
        m.pos = Vector2Add(m.pos, Vector2Scale(m.vel, dt));
        m.phase += 2.2f * dt;
        if (m.pos.y < -10) m.pos.y = SCREEN_HEIGHT + 10;
        if (m.pos.x < -10) m.pos.x = SCREEN_WIDTH + 10;
        if (m.pos.x > SCREEN_WIDTH + 10) m.pos.x = -10;
    }

    float angularSpeed = PI * (currentBpm / 60.0f);
    float beatDuration = 60.0f / currentBpm;

    if (state == STATE_MENU) {
        bgHue = Lerp(bgHue, stageMeta[selectedStage].hue, 1.0f - expf(-5.0f * dt));

        if (IsKeyPressed(KEY_LEFT)  || IsKeyPressed(KEY_A)) selectedStage = (selectedStage + STAGE_COUNT - 1) % STAGE_COUNT;
        if (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D)) selectedStage = (selectedStage + 1) % STAGE_COUNT;
        if (IsKeyPressed(KEY_UP)    || IsKeyPressed(KEY_W)) selectedStage = (selectedStage + STAGE_COUNT - 3) % STAGE_COUNT;
        if (IsKeyPressed(KEY_DOWN)  || IsKeyPressed(KEY_S)) selectedStage = (selectedStage + 3) % STAGE_COUNT;

        if (IsKeyPressed(KEY_ONE))   strictness = STRICT_LENIENT;
        if (IsKeyPressed(KEY_TWO))   strictness = STRICT_NORMAL;
        if (IsKeyPressed(KEY_THREE)) strictness = STRICT_STRICT;

        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            Vector2 mp = GetMousePosition();
            bool clickedCard = false;
            for (int i = 0; i < STAGE_COUNT; i++) {
                int col = i % 3;
                int row = i / 3;
                Rectangle card = { 130.0f + col * 345.0f, 225.0f + row * 145.0f, 325.0f, 125.0f };
                if (CheckCollisionPointRec(mp, card)) {
                    clickedCard = true;
                    if (selectedStage == i) {
                        StartStage(selectedStage, 0);
                    } else {
                        selectedStage = i;
                    }
                    break;
                }
            }
            if (!clickedCard) {
                StartStage(selectedStage, 0);
            }
        }
        else if (IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER)) {
            StartStage(selectedStage, 0);
        }
    }
    else if (state == STATE_COUNTDOWN) {
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
                PlaySound(sndPad[(selectedStage) % 4]);
                state = STATE_PLAYING;
            }
        }
    }
    else if (state == STATE_PLAYING) {
        if (IsKeyPressed(KEY_ESCAPE)) { state = STATE_MENU; }
        if (IsKeyPressed(KEY_R))      { StartStage(selectedStage, 0); }

        beatTimer += dt;
        if (beatTimer >= beatDuration * 0.5f) {
            beatTimer -= beatDuration * 0.5f;
            totalBeatCount++;
            if (totalBeatCount % 2 == 0) {
                int beatIdx = totalBeatCount / 2;
                cameraBeatPulse = 0.038f;
                PlaySound(beatIdx % 2 == 0 ? sndKick : sndSnare);
                if (beatIdx % 2 == 0) {
                    PlaySound(sndPad[(beatIdx / 2 + selectedStage) % 4]);
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

                if (autoPlay || fabsf(errorSec) <= pureWin) {
                    jText = "PURE PERFECT!"; jCol = { 70, 255, 195, 255 }; accVal = 1.0f; perfectCount++;
                } else if (errorSec < 0) {
                    if (fabsf(errorSec) <= perfWin) { jText = "EARLY"; jCol = { 120, 245, 95, 255 }; accVal = 0.85f; }
                    else { jText = "TOO EARLY"; jCol = GOLD; accVal = 0.55f; }
                } else {
                    if (fabsf(errorSec) <= perfWin) { jText = "LATE"; jCol = { 120, 245, 95, 255 }; accVal = 0.85f; }
                    else { jText = "TOO LATE"; jCol = GOLD; accVal = 0.55f; }
                }

                hitCount++;
                accuracySum += accVal;
                hitTicks.push_back({ autoPlay ? 0.0f : errMs, jCol, 1.0f });
                if (hitTicks.size() > 26) hitTicks.pop_front();

                Vector2 hitTilePos = tiles[currentTile + 1].pos;
                AddPopup(jText, hitTilePos, jCol);
                SpawnHitParticles(hitTilePos, isFirePivot ? SKYBLUE : ORANGE, 18);
                SpawnShockwave(hitTilePos, jCol, 72.0f);
                PlaySound(sndHit);

                screenShake = 5.0f;
                currentTile++;
                tiles[currentTile].cleared = true;
                tiles[currentTile].hitAnim = 1.0f;

                if (tiles[currentTile].checkpoint) {
                    lastCheckpoint = currentTile;
                    AddPopup("CHECKPOINT!", hitTilePos, GREEN);
                    SpawnShockwave(hitTilePos, GREEN, 110.0f);
                }

                if (!tiles[currentTile].midspin) {
                    isFirePivot = !isFirePivot;
                }

                if (currentTile >= (int)tiles.size() - 1) {
                    float finalAcc = (hitCount > 0) ? (accuracySum / hitCount * 100.0f) : 100.0f;
                    if (!autoPlay) {
                        stageMeta[selectedStage].cleared = true;
                        if (finalAcc > stageMeta[selectedStage].bestAcc) {
                            stageMeta[selectedStage].bestAcc = finalAcc;
                        }
                    }
                    PlaySound(sndClear);
                    SpawnShockwave(hitTilePos, GOLD, 160.0f);
                    state = STATE_CLEAR;
                } else {
                    Vector2 oldPivot = pivotPos;
                    SetupOrbitForTile(currentTile, oldPivot);
                }
            } else {
                PlaySound(sndFail);
                screenShake = 16.0f;
                failReason = "TOO EARLY! (OVERLOAD)";
                SpawnHitParticles(pivotPos, RED, 32);
                SpawnShockwave(pivotPos, RED, 120.0f);
                state = STATE_GAMEOVER;
            }
        }
        else if (errorSec > maxWin) {
            PlaySound(sndFail);
            screenShake = 16.0f;
            failReason = "MISSED THE BEAT!";
            SpawnHitParticles(tiles[currentTile + 1].pos, RED, 32);
            SpawnShockwave(tiles[currentTile + 1].pos, RED, 120.0f);
            state = STATE_GAMEOVER;
        }
    }
    else if (state == STATE_GAMEOVER) {
        if (IsKeyPressed(KEY_ESCAPE)) { state = STATE_MENU; }
        else if (IsKeyPressed(KEY_R)) { StartStage(selectedStage, 0); }
        else if (IsHitKeyPressed())   { StartStage(selectedStage, lastCheckpoint); }
    }
    else if (state == STATE_CLEAR) {
        if (IsKeyPressed(KEY_ESCAPE)) { state = STATE_MENU; }
        else if (IsHitKeyPressed())   {
            selectedStage = (selectedStage + 1) % STAGE_COUNT;
            StartStage(selectedStage, 0);
        }
    }

    if (!tiles.empty() && state != STATE_MENU) {
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

        if (state == STATE_PLAYING || state == STATE_COUNTDOWN) {
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
    DrawCircleLinesV({ SCREEN_WIDTH / 2.0f, SCREEN_HEIGHT / 2.0f }, 250.0f + cameraBeatPulse * 420.0f, Fade(WHITE, 0.045f));
    DrawCircleLinesV({ SCREEN_WIDTH / 2.0f, SCREEN_HEIGHT / 2.0f }, 450.0f + cameraBeatPulse * 620.0f, Fade(WHITE, 0.03f));

    if (state == STATE_MENU) {
        DrawTextCentered("A DANCE OF FIRE AND ICE", SCREEN_WIDTH / 2.0f, 65.0f, 54.0f, WHITE);
        DrawTextCentered("C++ WEBASSEMBLY RHYTHM ENGINE", SCREEN_WIDTH / 2.0f, 128.0f, 20.0f, ColorFromHSV(bgHue, 0.55f, 0.95f));

        for (int i = 0; i < STAGE_COUNT; i++) {
            int col = i % 3;
            int row = i / 3;
            float cx = 130.0f + col * 345.0f;
            float cy = 225.0f + row * 145.0f;
            bool sel = (i == selectedStage);
            Color themeCol = ColorFromHSV(stageMeta[i].hue, 0.65f, 0.95f);

            DrawRectangleRounded({ cx + 4, cy + 6, 325.0f, 125.0f }, 0.16f, 8, Fade(BLACK, 0.45f));
            DrawRectangleRounded({ cx, cy, 325.0f, 125.0f }, 0.16f, 8,
                                 sel ? Fade(themeCol, 0.24f) : Fade(WHITE, 0.06f));
            DrawRectangleRoundedLinesEx({ cx, cy, 325.0f, 125.0f }, 0.16f, 8, sel ? 2.5f : 1.0f,
                                        sel ? themeCol : Fade(WHITE, 0.18f));

            DrawTextUI(stageMeta[i].code, cx + 18, cy + 14, 28.0f, themeCol);
            DrawTextUI(TextFormat("%.0f BPM", stageMeta[i].bpm), cx + 225, cy + 18, 18.0f, LIGHTGRAY);
            DrawTextUI(stageMeta[i].title, cx + 18, cy + 48, 22.0f, sel ? WHITE : LIGHTGRAY);
            DrawTextUI(stageMeta[i].subtitle, cx + 18, cy + 76, 16.0f, GRAY);

            std::string starsStr = "";
            for (int s = 0; s < 5; s++) starsStr += (s < stageMeta[i].stars) ? "*" : "-";
            DrawTextUI(TextFormat("DIFF: %s", starsStr.c_str()), cx + 18, cy + 98, 15.0f, GOLD);

            if (stageMeta[i].cleared) {
                DrawTextUI(TextFormat("CLEAR %.1f%%", stageMeta[i].bestAcc), cx + 195, cy + 98, 15.0f, GREEN);
            }
        }

        const char* strictNames[3] = { "LENIENT [1]", "NORMAL [2]", "STRICT [3]" };
        DrawTextCentered(TextFormat("JUDGMENT: %s    |    AUTOPLAY: %s [TAB]    |    OFFSET: %.0f ms ([ / ])",
                         strictNames[strictness], autoPlay ? "ON" : "OFF", audioOffsetMs),
                         SCREEN_WIDTH / 2.0f, 545.0f, 20.0f, GOLD);
        DrawTextCentered("Select a Stage with Arrow Keys or Mouse, Press [SPACE] to Play",
                         SCREEN_WIDTH / 2.0f, 595.0f, 22.0f, WHITE);
        EndDrawing();
        return;
    }

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

        DrawCircleLinesV(pivotPos, TILE_DIST, Fade(WHITE, 0.16f));
        if (state == STATE_PLAYING) {
            float remRatio = Clamp(1.0f - (traveledAngle / targetDiff), 0.0f, 1.0f);
            float startDeg = currentAngle * RAD2DEG;
            float endDeg   = (currentAngle + (cw ? 1.0f : -1.0f) * targetDiff * remRatio) * RAD2DEG;
            DrawRing(pivotPos, TILE_DIST - 6.0f, TILE_DIST + 2.0f, startDeg, endDeg, 24,
                     Fade(isFirePivot ? SKYBLUE : ORANGE, 0.22f));
        }

        float timeNow = (float)GetTime();
        for (size_t i = 0; i < tiles.size(); i++) {
            float scale = 1.0f + tiles[i].hitAnim * 0.28f;
            Color rimCol = (i == (size_t)currentTile) ? WHITE :
                           (tiles[i].cleared ? Color{ 110, 150, 210, 255 } : Color{ 70, 86, 122, 255 });
            if (i == tiles.size() - 1) rimCol = GOLD;

            if (tiles[i].midspin) {
                DrawPoly(tiles[i].pos, 3, 26.0f * scale, timeNow * 70.0f, MAGENTA);
                DrawPoly(tiles[i].pos, 3, 16.0f * scale, timeNow * 70.0f, { 26, 30, 46, 255 });
            } else {
                DrawCircleV({ tiles[i].pos.x, tiles[i].pos.y + 4.0f }, 24.0f * scale, Fade(BLACK, 0.35f));
                DrawCircleV(tiles[i].pos, 24.5f * scale, rimCol);
                DrawCircleV(tiles[i].pos, 18.0f * scale, { 22, 26, 40, 255 });
            }

            if (i == tiles.size() - 1) {
                DrawRing(tiles[i].pos, 11.0f, 15.0f, timeNow * 140.0f, timeNow * 140.0f + 240.0f, 24, GOLD);
                DrawCircleV(tiles[i].pos, 7.0f + 2.0f * sinf(timeNow * 6.0f), WHITE);
            }

            if (tiles[i].swirl) {
                float rot = timeNow * 200.0f;
                DrawRing(tiles[i].pos, 9.0f, 13.5f, rot, rot + 140.0f, 16, { 240, 95, 255, 255 });
                DrawRing(tiles[i].pos, 9.0f, 13.5f, rot + 180.0f, rot + 320.0f, 16, { 240, 95, 255, 255 });
            }

            if (tiles[i].checkpoint) {
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

    int progress = (int)((float)currentTile / (tiles.size() - 1) * 100.0f);
    float accPct = (hitCount > 0) ? (accuracySum / hitCount * 100.0f) : 100.0f;

    DrawTextUI(TextFormat("%s  %s", stageMeta[selectedStage].code, stageMeta[selectedStage].title),
               28.0f, 20.0f, 24.0f, WHITE);
    DrawTextUI(TextFormat("PROGRESS: %d%%   |   ACCURACY: %.1f%%   |   BPM: %.0f",
               progress, accPct, currentBpm), 28.0f, 50.0f, 18.0f, LIGHTGRAY);

    if (autoPlay) {
        DrawTextUI("[AUTOPLAY ON]", SCREEN_WIDTH - 205.0f, 22.0f, 20.0f, GOLD);
    }

    int barW = 380, barH = 10;
    int barX = SCREEN_WIDTH / 2 - barW / 2;
    int barY = SCREEN_HEIGHT - 44;
    DrawRectangleRounded({ (float)barX - 4, (float)barY - 6, (float)barW + 8, (float)barH + 12 }, 0.4f, 6, Fade(BLACK, 0.45f));
    DrawRectangle(barX, barY, barW, barH, Fade(GOLD, 0.45f));
    DrawRectangle(barX + 70, barY, barW - 140, barH, Fade(GREEN, 0.65f));
    DrawRectangle(barX + 140, barY, barW - 280, barH, Fade(SKYBLUE, 0.85f));
    DrawRectangle(SCREEN_WIDTH / 2 - 2, barY - 6, 4, barH + 12, WHITE);

    for (const auto& tk : hitTicks) {
        float clamped = Clamp(tk.errorMs, -160.0f, 160.0f);
        int tx = SCREEN_WIDTH / 2 + (int)((clamped / 160.0f) * (barW / 2.0f));
        DrawRectangle(tx - 2, barY - 5, 4, barH + 10, Fade(tk.color, tk.alpha));
    }

    if (state == STATE_COUNTDOWN) {
        const char* cdText = (countdownBeats == 3) ? "3" :
                             (countdownBeats == 2) ? "2" :
                             (countdownBeats == 1) ? "1" : "GET READY";
        DrawTextCentered(cdText, SCREEN_WIDTH / 2.0f, 135.0f, 64.0f, GOLD);
    }
    else if (state == STATE_GAMEOVER) {
        DrawRectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, Fade(BLACK, 0.68f));
        DrawTextCentered("OVERLOAD!", SCREEN_WIDTH / 2.0f, 225.0f, 58.0f, RED);
        DrawTextCentered(failReason.c_str(), SCREEN_WIDTH / 2.0f, 298.0f, 24.0f, LIGHTGRAY);
        const char* retryMsg = (lastCheckpoint > 0)
            ? "[SPACE] Checkpoint   |   [R] Full Restart   |   [ESC] Stage Select"
            : "[SPACE] Retry   |   [ESC] Stage Select";
        DrawTextCentered(retryMsg, SCREEN_WIDTH / 2.0f, 368.0f, 22.0f, WHITE);
    }
    else if (state == STATE_CLEAR) {
        DrawRectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, Fade(BLACK, 0.68f));
        DrawTextCentered("STAGE CLEAR!", SCREEN_WIDTH / 2.0f, 215.0f, 58.0f, GOLD);
        DrawTextCentered(TextFormat("Final Accuracy: %.2f%%   (Pure Perfect: %d / %d)", accPct, perfectCount, hitCount),
                         SCREEN_WIDTH / 2.0f, 292.0f, 24.0f, SKYBLUE);
        DrawTextCentered("[SPACE] Next Stage   |   [ESC] Stage Select",
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
    BuildStage(0);

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
        for (int i = 0; i < 4; i++) UnloadSound(sndPad[i]);
        CloseAudioDevice();
    }
    CloseWindow();
    return 0;
}
