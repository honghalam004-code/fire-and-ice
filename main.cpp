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
// 1. 상수 및 열거형 정의
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
    MODE_JOURNEY = 0,      // 월드 투어 (6개 테마 월드)
    MODE_ENDLESS = 1,      // 무한 러시 (생존 모드)
    MODE_ZEN = 2,          // 연습 모드 (게임오버 없음)
    MODE_PERFECTIONIST = 3 // 퍼펙트 챌린지 (원히트 도전)
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
    int difficultyLevel;
    float hue;
    int melodyNotes[8];
    int bassNotes[4];
    int instrumentType;
    bool cleared;
    float bestAcc;
};

// ==========================================
// 2. 전역 게임 상태
// ==========================================
Font gameFont = { 0 };
ScreenState screen = SCR_MODE_SELECT;
GameMode currentMode = MODE_JOURNEY;
Strictness strictness = STRICT_NORMAL;
int selectedWorld = 0;
bool autoPlay = false;
float audioOffsetMs = 0.0f;

const float NOTE_FREQS[18] = {
    220.00f, 233.08f, 246.94f, 261.63f, 277.18f, 293.66f,
    311.13f, 329.63f, 349.23f, 369.99f, 392.00f, 415.30f,
    440.00f, 466.16f, 493.88f, 523.25f, 587.33f, 659.25f
};

WorldInfo worlds[WORLD_COUNT] = {
    {
        "첫 번째 불꽃", "매우 쉬움", "오르골 발라드",
        "직진과 90도 회전만 등장하는 입문 코스",
        96.0f, 1, 24.0f,
        { 3, 7, 10, 12, 10, 7, 5, 10 },
        { 3, 8, 3, 10 }, 0, false, 0.0f
    },
    {
        "산들바람 왈츠", "쉬움", "따뜻한 일렉트릭 피아노",
        "계단형 코스와 첫 소용돌이(회전 반전)",
        108.0f, 2, 165.0f,
        { 8, 12, 15, 17, 16, 12, 10, 15 },
        { 8, 5, 10, 3 }, 1, false, 0.0f
    },
    {
        "빙하의 나선", "보통", "크리스탈 겨울 차임",
        "달팽이 나선형 코스와 45도 대각선",
        120.0f, 3, 198.0f,
        { 12, 15, 17, 14, 15, 12, 10, 7 },
        { 0, 8, 3, 10 }, 2, false, 0.0f
    },
    {
        "별빛 무도회", "도전", "천상의 하프 아르페지오",
        "다이아몬드 광장, 삼각 미드스핀, 360도 유턴",
        132.0f, 4, 248.0f,
        { 5, 8, 12, 15, 14, 10, 12, 8 },
        { 5, 10, 1, 0 }, 3, false, 0.0f
    },
    {
        "육각 성채", "어려움", "레트로 신스웨이브 리프",
        "60도·120도 육각형 벌집 셋잇단음표 코스",
        144.0f, 5, 290.0f,
        { 7, 8, 11, 14, 15, 14, 11, 8 },
        { 7, 8, 5, 7 }, 4, false, 0.0f
    },
    {
        "일식의 피날레", "마스터", "웅장한 보스 오르간",
        "모든 기믹이 총출동하는 십자 요새 코스",
        156.0f, 6, 345.0f,
        { 3, 6, 10, 11, 10, 6, 5, 2 },
        { 3, 11, 8, 10 }, 5, false, 0.0f
    }
};

std::vector<Tile> tiles;
std::vector<Vector2> previewPoints;
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
Sound sndMelody[6][18];
Sound sndBass[18];

// ==========================================
// 3. 한글 폰트 로더 및 UI 유틸리티
// ==========================================
Font LoadKoreanFont(const char* path, int fontSize) {
    std::string allChars;
    for (int c = 32; c <= 126; c++) allChars.push_back((char)c);
    allChars += "•";

    for (int i = 0; i < WORLD_COUNT; i++) {
        allChars += worlds[i].name;
        allChars += worlds[i].diffLabel;
        allChars += worlds[i].songStyle;
        allChars += worlds[i].gimmickDesc;
    }

    allChars +=
        "얼음과 불의 춤 게임 모드를 선택하세요 월드 투어 개 입문부터 마스터까지 고유한 음악과 코스를 정복하세요 "
        "무한 러시 생존 모드 끝없이 생성되는 랜덤 코스를 밟으며 최고 기록에 도전하세요 연습 게임오버 없음 "
        "박자를 놓쳐도 실패하지 않고 끝까지 연습할 수 있습니다 퍼펙트 챌린지 원히트 도전 "
        "단 한 번의 실수나 어긋남도 허용되지 않는 고수용 모드입니다 최고 기록 타일 "
        "클릭 또는 방향키로 선택 판정 쉬움 보통 엄격 오토플레이 켜짐 꺼짐 싱크 "
        "모드 선택 코스 모양 미리보기 월드 시작 난이도 음악 고정 코스 상태 클리어 완료 최고 정확도 "
        "아직 클리어하지 않음 진행률 연속 메뉴 빠름 느림 완벽 너무 체크포인트 놓침 준비 "
        "행성 파괴 퍼펙트 챌린지 완벽하지 않은 타이밍 일찍 입력했습니다 과부하 놓쳤습니다 "
        "생존 기록 부활 처음부터 다시 도전 최종 판정 다음";

    int cpCount = 0;
    int* rawCps = LoadCodepoints(allChars.c_str(), &cpCount);

    std::vector<int> uniqueCps;
    uniqueCps.reserve(cpCount);
    for (int i = 0; i < cpCount; i++) {
        if (std::find(uniqueCps.begin(), uniqueCps.end(), rawCps[i]) == uniqueCps.end()) {
            uniqueCps.push_back(rawCps[i]);
        }
    }
    UnloadCodepoints(rawCps);

    return LoadFontEx(path, fontSize, uniqueCps.data(), (int)uniqueCps.size());
}

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
// 4. 다중 악기 사운드 합성기
// ==========================================
Sound SynthSFX(int type, float freq, float duration, float volume) {
    int sampleRate = 44100;
    int count = (int)(sampleRate * duration);
    short* data = (short*)RL_MALLOC(count * sizeof(short));

    for (int i = 0; i < count; i++) {
        float t = (float)i / sampleRate;
        float sample = 0.0f;

        if (type == 0) {
            float env = expf(-t * 36.0f);
            sample = (sinf(2.0f * PI * freq * t) * 0.68f + sinf(2.0f * PI * (freq * 1.5f) * t) * 0.32f) * env;
        } else if (type == 1) {
            float env = expf(-t * 17.0f);
            float f = freq * expf(-t * 22.0f) + 44.0f;
            sample = sinf(2.0f * PI * f * t) * env;
        } else if (type == 2) {
            float env = expf(-t * (freq > 1000.0f ? 58.0f : 23.0f));
            float noise = ((float)GetRandomValue(-1000, 1000) / 1000.0f);
            float tone = (freq < 1000.0f) ? sinf(2.0f * PI * 185.0f * t) * 0.38f : 0.0f;
            sample = (noise * 0.72f + tone) * env;
        } else if (type == 3) {
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

        if (inst == 0) {
            float env = expf(-t * 8.0f);
            sample = (sinf(2.0f * PI * freq * t) * 0.65f + sinf(2.0f * PI * (freq * 2.0f) * t) * 0.35f) * env;
        } else if (inst == 1) {
            float env = fminf(1.0f, t * 40.0f) * expf(-t * 5.0f);
            float trem = 1.0f + 0.15f * sinf(2.0f * PI * 5.5f * t);
            sample = (sinf(2.0f * PI * freq * t) * 0.7f + sinf(2.0f * PI * (freq * 3.0f) * t) * 0.15f * expf(-t * 14.0f)) * env * trem;
        } else if (inst == 2) {
            float env = expf(-t * 7.5f);
            sample = (sinf(2.0f * PI * freq * t) * 0.5f + sinf(2.0f * PI * (freq * 2.5f) * t) * 0.3f + sinf(2.0f * PI * (freq * 4.0f) * t) * 0.2f) * env;
        } else if (inst == 3) {
            float env = fminf(1.0f, t * 60.0f) * expf(-t * 6.5f);
            sample = (sinf(2.0f * PI * freq * t) * 0.6f + sinf(2.0f * PI * (freq * 2.0f) * t) * 0.3f + sinf(2.0f * PI * (freq * 3.0f) * t) * 0.1f) * env;
        } else if (inst == 4) {
            float env = fminf(1.0f, t * 50.0f) * expf(-t * 6.0f);
            float s1 = sinf(2.0f * PI * freq * t);
            float s2 = (sinf(2.0f * PI * freq * t) > 0.0f ? 0.35f : -0.35f);
            sample = (s1 * 0.65f + s2 * 0.35f) * env;
        } else if (inst == 5) {
            float env = fminf(1.0f, t * 35.0f) * expf(-t * 4.5f);
            sample = (sinf(2.0f * PI * (freq * 0.5f) * t) * 0.4f
                    + sinf(2.0f * PI * freq * t) * 0.4f
                    + sinf(2.0f * PI * (freq * 2.0f) * t) * 0.2f) * env;
        } else {
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
// 5. 6개 테마 월드 코스 생성기
// ==========================================
std::vector<MapStep> GetWorldSteps(int worldIdx) {
    if (worldIdx == 0) {
        return {
            {0}, {0}, {0}, {0}, {0}, {0},
            {-90}, {0}, {0}, {0},
            {0, false, false, true},
            {0}, {90}, {0}, {0}, {0},
            {-90}, {0}, {0}, {0}, {0}, {0}
        };
    }
    else if (worldIdx == 1) {
        return {
            {0}, {0}, {0},
            {-90}, {0}, {-90}, {0}, {-90}, {0},
            {0, true}, {0}, {0}, {0},
            {90, false, false, true}, {0}, {90}, {0}, {90}, {0},
            {0, true}, {0}, {-90}, {0}, {0}, {0}, {0}
        };
    }
    else if (worldIdx == 2) {
        return {
            {0}, {0}, {0}, {0},
            {90}, {90}, {90},
            {180}, {180},
            {-90}, {-90},
            {0, false, false, true},
            {0, true}, {0},
            {-45}, {45}, {-45}, {45}, {-45}, {45},
            {0, true}, {0}, {-90}, {0}, {90}, {0}, {0}, {0}
        };
    }
    else if (worldIdx == 3) {
        return {
            {0}, {-45}, {-45}, {45}, {45},
            {-90, false, true}, {0}, {90, false, true}, {0},
            {0, true}, {0}, {180}, {0},
            {0, false, false, true},
            {45}, {45}, {-45}, {-45},
            {-90, false, true}, {0}, {-90, false, true}, {0},
            {0, true}, {0}, {-90}, {0}, {90}, {0}, {0}, {0}
        };
    }
    else if (worldIdx == 4) {
        return {
            {0}, {-60}, {0}, {60}, {120}, {60}, {0},
            {-60, false, true}, {0}, {60, false, true}, {0},
            {0, true}, {0}, {-60}, {-120}, {-60}, {0},
            {0, false, false, true},
            {60}, {0}, {-60}, {0}, {60}, {0}, {-60}, {0},
            {180, true}, {0}, {-60, false, true}, {0},
            {0, true}, {0}, {0}, {0}
        };
    }
    else {
        return {
            {0}, {-90}, {0}, {90}, {0},
            {0, true}, {-90, true}, {0, true}, {90, true},
            {-90, false, true}, {0}, {-90, false, true}, {0},
            {0, false, false, true},
            {-45}, {45}, {-45}, {45}, {180, true}, {0},
            {-60}, {0}, {60}, {0}, {-60}, {0}, {60}, {0},
            {0, false, false, true},
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
// 6. 이펙트 및 공전 궤도 제어
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
        AddPopup("체크포인트!", hitTilePos, GREEN);
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
// 7. 메인 업데이트 및 렌더링 루프
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
    // 화면 1: 모드 선택
    // ------------------------------------------
    if (screen == SCR_MODE_SELECT) {
        float modeHues[4] = { 24.0f, 185.0f, 145.0f, 345.0f };
        bgHue = Lerp(bgHue, modeHues[(int)currentMode], 1.0f - expf(-5.0f * dt));

        if (IsKeyPressed(KEY_LEFT)  || IsKeyPressed(KEY_A)) currentMode = (GameMode)(((int)currentMode + 3) % 4);
        if (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D)) currentMode = (GameMode)(((int)currentMode + 1) % 4);
        if (IsKeyPressed(KEY_UP)    || IsKeyPressed(KEY_W) || IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S)) {
            currentMode = (GameMode)(((int)currentMode + 2) % 4);
        }

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

        if (clicked) {
            for (int s = 0; s < 3; s++) {
                Rectangle b = { 210.0f + s * 88.0f, 548.0f, 80.0f, 36.0f };
                if (CheckCollisionPointRec(mp, b)) strictness = (Strictness)s;
            }
            Rectangle autoBtn = { 600.0f, 548.0f, 90.0f, 36.0f };
            if (CheckCollisionPointRec(mp, autoBtn)) autoPlay = !autoPlay;

            Rectangle offMinus = { 780.0f, 548.0f, 40.0f, 36.0f };
            Rectangle offPlus  = { 920.0f, 548.0f, 40.0f, 36.0f };
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
    // 화면 2: 월드 / 코스 선택
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
            Rectangle backBtn  = { 40.0f, 28.0f, 195.0f, 42.0f };
            Rectangle prevBtn  = { 65.0f, 315.0f, 68.0f, 90.0f };
            Rectangle nextBtn  = { 1147.0f, 315.0f, 68.0f, 90.0f };
            Rectangle playBtn  = { 635.0f, 445.0f, 430.0f, 64.0f };

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

            for (int w = 0; w < WORLD_COUNT; w++) {
                Rectangle pill = { 335.0f + w * 102.0f, 595.0f, 88.0f, 46.0f };
                if (CheckCollisionPointRec(mp, pill)) {
                    selectedWorld = w;
                    UpdateWorldPreview(selectedWorld);
                }
            }
        }
    }
    // ------------------------------------------
    // 화면 3: 카운트다운 및 플레이
    // ------------------------------------------
    else if (screen == SCR_COUNTDOWN || screen == SCR_PLAYING) {
        if (IsKeyPressed(KEY_TAB)) autoPlay = !autoPlay;
        if (IsKeyPressed(KEY_ESCAPE)) screen = (currentMode == MODE_ENDLESS) ? SCR_MODE_SELECT : SCR_WORLD_SELECT;
        if (IsKeyPressed(KEY_R)) StartGame(0);

        if (clicked && CheckCollisionPointRec(mp, { 1130.0f, 18.0f, 120.0f, 38.0f })) {
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
            beatTimer += dt;
            if (beatTimer >= beatDuration * 0.5f) {
                beatTimer -= beatDuration * 0.5f;
                totalBeatCount++;

                int wIdx = (currentMode == MODE_ENDLESS) ? ((endlessTilesCleared / 16) % WORLD_COUNT) : selectedWorld;
                int inst = worlds[wIdx].instrumentType;
                int step8 = totalBeatCount % 8;
                int beat4 = (totalBeatCount / 2) % 4;

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
                    std::string jText = "완벽!";
                    float accVal = 1.0f;
                    bool isGoodHit = true;

                    if (autoPlay || fabsf(errorSec) <= pureWin) {
                        jText = "완벽!"; jCol = { 70, 255, 195, 255 }; accVal = 1.0f; perfectCount++;
                    } else if (errorSec < 0) {
                        if (fabsf(errorSec) <= perfWin) { jText = "빠름"; jCol = { 120, 245, 95, 255 }; accVal = 0.85f; }
                        else { jText = "너무 빠름"; jCol = GOLD; accVal = 0.50f; isGoodHit = false; }
                    } else {
                        if (fabsf(errorSec) <= perfWin) { jText = "느림"; jCol = { 120, 245, 95, 255 }; accVal = 0.85f; }
                        else { jText = "너무 느림"; jCol = GOLD; accVal = 0.50f; isGoodHit = false; }
                    }

                    if (currentMode == MODE_PERFECTIONIST && !isGoodHit) {
                        PlaySound(sndFail);
                        screenShake = 16.0f;
                        failReason = "퍼펙트 챌린지: 완벽하지 않은 타이밍!";
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
                    if (currentMode == MODE_ZEN) {
                        screenShake = 8.0f;
                        streakCount = 0;
                        AddPopup("너무 빠름!", pivotPos, ORANGE);
                    } else {
                        PlaySound(sndFail);
                        screenShake = 16.0f;
                        failReason = "너무 일찍 입력했습니다! (과부하)";
                        SpawnHitParticles(pivotPos, RED, 32);
                        SpawnShockwave(pivotPos, RED, 120.0f);
                        screen = SCR_GAMEOVER;
                    }
                }
            }
            else if (errorSec > maxWin) {
                if (currentMode == MODE_ZEN) {
                    streakCount = 0;
                    hitCount++;
                    Vector2 hitTilePos = tiles[currentTile + 1].pos;
                    AddPopup("놓침 (연습)", hitTilePos, RED);
                    AdvanceTileAfterHit(hitTilePos, true);
                } else {
                    PlaySound(sndFail);
                    screenShake = 16.0f;
                    failReason = "박자를 놓쳤습니다!";
                    SpawnHitParticles(tiles[currentTile + 1].pos, RED, 32);
                    SpawnShockwave(tiles[currentTile + 1].pos, RED, 120.0f);
                    screen = SCR_GAMEOVER;
                }
            }
        }
    }
    // ------------------------------------------
    // 화면 4 & 5: 게임오버 / 클리어
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
    // 8. 화면 렌더링
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
    // 렌더링 1: 모드 선택 화면
    // ------------------------------------------
    if (screen == SCR_MODE_SELECT) {
        DrawTextCentered("얼음과 불의 춤", SCREEN_WIDTH / 2.0f, 46.0f, 52.0f, WHITE);
        DrawTextCentered("게임 모드를 선택하세요", SCREEN_WIDTH / 2.0f, 110.0f, 20.0f, SKYBLUE);

        const char* modeTitles[4] = { "월드 투어", "무한 러시", "연습 모드", "퍼펙트 챌린지" };
        const char* modeBadges[4] = { "6개 월드", "생존 모드", "게임오버 없음", "원히트 도전" };
        const char* modeDescs[4]  = {
            "입문부터 마스터까지 6개의 고유한 음악과 코스를 정복하세요.",
            "끝없이 생성되는 랜덤 코스를 밟으며 최고 기록에 도전하세요.",
            "박자를 놓쳐도 실패하지 않고 끝까지 연습할 수 있습니다.",
            "단 한 번의 실수나 어긋남도 허용되지 않는 고수용 모드입니다."
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

            DrawTextUI(modeTitles[i], card.x + 24, card.y + 18, 28.0f, WHITE);
            DrawRectangleRounded({ card.x + 285, card.y + 20, 148.0f, 28.0f }, 0.4f, 6, Fade(theme, 0.25f));
            DrawTextCentered(modeBadges[i], card.x + 359, card.y + 26, 15.0f, theme);

            DrawTextUI(modeDescs[i], card.x + 24, card.y + 66, 15.5f, LIGHTGRAY);
            if (i == 1 && bestEndlessScore > 0) {
                DrawTextUI(TextFormat("최고 기록: %d 타일", bestEndlessScore), card.x + 24, card.y + 102, 16.0f, GOLD);
            } else {
                DrawTextUI("클릭 또는 방향키 + [SPACE]로 선택", card.x + 24, card.y + 102, 15.0f, theme);
            }
        }

        // 하단 설정 바
        DrawCardBox({ 135.0f, 532.0f, 1010.0f, 68.0f }, 0.25f, Color{ 18, 22, 34, 235 }, Fade(WHITE, 0.14f), 1.5f);

        DrawTextUI("판정:", 160.0f, 556.0f, 17.0f, LIGHTGRAY);
        const char* sLabels[3] = { "쉬움", "보통", "엄격" };
        for (int s = 0; s < 3; s++) {
            Rectangle b = { 210.0f + s * 88.0f, 548.0f, 80.0f, 36.0f };
            bool active = ((int)strictness == s);
            DrawRectangleRounded(b, 0.35f, 6, active ? SKYBLUE : Fade(WHITE, 0.08f));
            DrawTextCentered(sLabels[s], b.x + b.width * 0.5f, b.y + 9.0f, 16.0f, active ? BLACK : WHITE, false);
        }

        DrawTextUI("오토플레이:", 505.0f, 556.0f, 17.0f, LIGHTGRAY);
        Rectangle autoBtn = { 600.0f, 548.0f, 90.0f, 36.0f };
        DrawRectangleRounded(autoBtn, 0.35f, 6, autoPlay ? GOLD : Fade(WHITE, 0.08f));
        DrawTextCentered(autoPlay ? "켜짐" : "꺼짐", autoBtn.x + 45.0f, autoBtn.y + 9.0f, 16.0f, autoPlay ? BLACK : WHITE, false);

        DrawTextUI("싱크:", 730.0f, 556.0f, 17.0f, LIGHTGRAY);
        Rectangle offMinus = { 780.0f, 548.0f, 40.0f, 36.0f };
        Rectangle offPlus  = { 920.0f, 548.0f, 40.0f, 36.0f };
        DrawRectangleRounded(offMinus, 0.35f, 6, Fade(WHITE, 0.12f));
        DrawRectangleRounded(offPlus, 0.35f, 6, Fade(WHITE, 0.12f));
        DrawTextCentered("-", offMinus.x + 20.0f, offMinus.y + 7.0f, 20.0f, WHITE, false);
        DrawTextCentered("+", offPlus.x + 20.0f, offPlus.y + 7.0f, 20.0f, WHITE, false);
        DrawTextCentered(TextFormat("%.0f ms", audioOffsetMs), 870.0f, 556.0f, 17.0f, WHITE);

        EndDrawing();
        return;
    }

    // ------------------------------------------
    // 렌더링 2: 월드 선택 및 코스 미리보기
    // ------------------------------------------
    if (screen == SCR_WORLD_SELECT) {
        WorldInfo& w = worlds[selectedWorld];
        Color theme = ColorFromHSV(w.hue, 0.68f, 0.95f);

        Rectangle backBtn = { 40.0f, 28.0f, 195.0f, 42.0f };
        DrawCardBox(backBtn, 0.3f, Color{ 24, 28, 44, 240 }, Fade(WHITE, 0.25f), 1.5f);
        DrawTextCentered("< 모드 선택 [ESC]", backBtn.x + backBtn.width * 0.5f, backBtn.y + 11.0f, 17.0f, WHITE);

        const char* modeBanner = (currentMode == MODE_JOURNEY) ? "모드: 월드 투어" :
                                 (currentMode == MODE_ZEN)     ? "모드: 연습 모드 (실패 없음)" :
                                                                 "모드: 퍼펙트 챌린지";
        DrawTextCentered(modeBanner, SCREEN_WIDTH / 2.0f, 36.0f, 22.0f, theme);

        Rectangle prevBtn = { 65.0f, 315.0f, 68.0f, 90.0f };
        Rectangle nextBtn = { 1147.0f, 315.0f, 68.0f, 90.0f };
        DrawCardBox(prevBtn, 0.3f, Color{ 24, 28, 44, 240 }, theme, 2.0f);
        DrawCardBox(nextBtn, 0.3f, Color{ 24, 28, 44, 240 }, theme, 2.0f);
        DrawTextCentered("<", prevBtn.x + 34.0f, prevBtn.y + 24.0f, 42.0f, WHITE);
        DrawTextCentered(">", nextBtn.x + 34.0f, nextBtn.y + 24.0f, 42.0f, WHITE);

        Rectangle showcase = { 165.0f, 115.0f, 950.0f, 435.0f };
        DrawCardBox(showcase, 0.08f, Color{ 18, 22, 35, 245 }, theme, 2.5f);

        // 왼쪽: 코스 모양 미리보기
        Rectangle mapBox = { 205.0f, 155.0f, 390.0f, 355.0f };
        DrawRectangleRounded(mapBox, 0.1f, 8, Color{ 12, 15, 24, 255 });
        DrawTextCentered("코스 모양 미리보기", mapBox.x + mapBox.width * 0.5f, mapBox.y + 16.0f, 16.0f, GRAY);

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

        // 오른쪽: 월드 정보 및 시작 버튼
        float rx = 635.0f;
        DrawTextUI(TextFormat("%d월드  •  %s", selectedWorld + 1, w.diffLabel), rx, 155.0f, 20.0f, theme);
        DrawTextUI(w.name, rx, 188.0f, 38.0f, WHITE);

        DrawTextUI("난이도:", rx, 248.0f, 17.0f, GRAY);
        for (int d = 0; d < 6; d++) {
            Color segCol = (d < w.difficultyLevel) ? theme : Fade(WHITE, 0.12f);
            DrawRectangleRounded({ rx + 75.0f + d * 52.0f, 246.0f, 44.0f, 18.0f }, 0.3f, 4, segCol);
        }

        DrawTextUI(TextFormat("음악: %s (%.0f BPM 고정)", w.songStyle, w.bpm), rx, 290.0f, 18.0f, LIGHTGRAY);
        DrawTextUI(TextFormat("코스: %s", w.gimmickDesc), rx, 325.0f, 17.5f, LIGHTGRAY);

        if (w.cleared) {
            DrawTextUI(TextFormat("상태: 클리어 완료!  (최고 정확도: %.1f%%)", w.bestAcc), rx, 368.0f, 17.5f, GREEN);
        } else {
            DrawTextUI("상태: 아직 클리어하지 않음", rx, 368.0f, 17.5f, GRAY);
        }

        Rectangle playBtn = { rx, 445.0f, 430.0f, 64.0f };
        bool hoverPlay = CheckCollisionPointRec(mp, playBtn);
        DrawRectangleRounded(playBtn, 0.25f, 8, hoverPlay ? WHITE : theme);
        DrawTextCentered("월드 시작  [SPACE]", playBtn.x + playBtn.width * 0.5f, playBtn.y + 18.0f, 24.0f, BLACK, false);

        for (int i = 0; i < WORLD_COUNT; i++) {
            Rectangle pill = { 335.0f + i * 102.0f, 595.0f, 88.0f, 46.0f };
            bool isSel = (i == selectedWorld);
            Color pCol = ColorFromHSV(worlds[i].hue, 0.65f, 0.95f);
            DrawCardBox(pill, 0.3f, isSel ? Fade(pCol, 0.32f) : Color{ 20, 24, 38, 230 }, isSel ? pCol : Fade(WHITE, 0.16f), 2.0f);
            DrawTextCentered(TextFormat("%d월드", i + 1), pill.x + 44.0f, pill.y + 13.0f, 18.0f, isSel ? WHITE : LIGHTGRAY);
        }

        EndDrawing();
        return;
    }

    // ------------------------------------------
    // 렌더링 3: 인게임 월드 렌더링
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
            DrawTextCentered(p.text.c_str(), p.pos.x, p.pos.y, 20.0f, Fade(p.color, Clamp(p.timer * 1.4f, 0.0f, 1.0f)));
        }
    }
    EndMode2D();

    // --- 상단 HUD ---
    float accPct = (hitCount > 0) ? (accuracySum / hitCount * 100.0f) : 100.0f;

    if (currentMode == MODE_ENDLESS) {
        DrawTextUI(TextFormat("무한 러시  •  타일: %d  (최고: %d)", endlessTilesCleared, bestEndlessScore), 28.0f, 20.0f, 24.0f, WHITE);
        DrawTextUI(TextFormat("정확도: %.1f%%   |   연속: %d   |   BPM: %.0f", accPct, streakCount, currentBpm), 28.0f, 50.0f, 18.0f, SKYBLUE);
    } else {
        float ratio = (float)currentTile / (float)(tiles.size() - 1);
        DrawTextUI(TextFormat("%d월드: %s", selectedWorld + 1, worlds[selectedWorld].name), 28.0f, 18.0f, 24.0f, WHITE);

        DrawRectangleRounded({ 28.0f, 52.0f, 260.0f, 12.0f }, 0.5f, 4, Fade(WHITE, 0.15f));
        DrawRectangleRounded({ 28.0f, 52.0f, 260.0f * ratio, 12.0f }, 0.5f, 4, ColorFromHSV(bgHue, 0.65f, 0.95f));
        DrawTextUI(TextFormat("%d%%   •   정확도: %.1f%%", (int)(ratio * 100.0f), accPct), 300.0f, 48.0f, 18.0f, LIGHTGRAY);
    }

    Rectangle menuBtn = { 1130.0f, 18.0f, 120.0f, 38.0f };
    DrawCardBox(menuBtn, 0.3f, Color{ 20, 24, 38, 220 }, Fade(WHITE, 0.25f), 1.5f);
    DrawTextCentered("메뉴 [ESC]", menuBtn.x + 60.0f, menuBtn.y + 10.0f, 16.0f, WHITE);

    if (autoPlay) {
        DrawTextUI("[오토플레이]", 990.0f, 26.0f, 18.0f, GOLD);
    }

    // 하단 판정 오차 바
    int barW = 380, barH = 10;
    int barX = SCREEN_WIDTH / 2 - barW / 2;
    int barY = SCREEN_HEIGHT - 44;
    DrawRectangleRounded({ (float)barX - 4, (float)barY - 6, (float)barW + 8, (float)barH + 12 }, 0.4f, 6, Fade(BLACK, 0.45f));
    DrawRectangle(barX, barY, barW, barH, Fade(GOLD, 0.45f));
    DrawRectangle(barX + 70, barY, barW - 140, barH, Fade(GREEN, 0.65f));
    DrawRectangle(barX + 140, barY, barW - 280, barH, Fade(SKYBLUE, 0.85f));
    DrawRectangle(SCREEN_WIDTH / 2 - 2, barY - 6, 4, barH + 12, WHITE);

    DrawTextUI("빠름", (float)barX - 48.0f, (float)barY - 3.0f, 15.0f, GOLD);
    DrawTextUI("느림", (float)barX + barW + 14.0f, (float)barY - 3.0f, 15.0f, GOLD);

    for (const auto& tk : hitTicks) {
        float clamped = Clamp(tk.errorMs, -160.0f, 160.0f);
        int tx = SCREEN_WIDTH / 2 + (int)((clamped / 160.0f) * (barW / 2.0f));
        DrawRectangle(tx - 2, barY - 5, 4, barH + 10, Fade(tk.color, tk.alpha));
    }

    // --- 상태 오버레이 ---
    if (screen == SCR_COUNTDOWN) {
        const char* cdText = (countdownBeats == 3) ? "3" :
                             (countdownBeats == 2) ? "2" :
                             (countdownBeats == 1) ? "1" : "준비!";
        DrawTextCentered(cdText, SCREEN_WIDTH / 2.0f, 135.0f, 64.0f, GOLD);
    }
    else if (screen == SCR_GAMEOVER) {
        DrawRectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, Fade(BLACK, 0.70f));
        DrawTextCentered("행성 파괴!", SCREEN_WIDTH / 2.0f, 215.0f, 58.0f, RED);
        DrawTextCentered(failReason.c_str(), SCREEN_WIDTH / 2.0f, 290.0f, 24.0f, LIGHTGRAY);
        if (currentMode == MODE_ENDLESS) {
            DrawTextCentered(TextFormat("생존 기록: %d 타일  (최고 기록: %d 타일)", endlessTilesCleared, bestEndlessScore),
                             SCREEN_WIDTH / 2.0f, 332.0f, 22.0f, SKYBLUE);
        }
        const char* retryMsg = (currentMode == MODE_JOURNEY && lastCheckpoint > 0)
            ? "[SPACE] 체크포인트 부활   |   [R] 처음부터   |   [ESC] 메뉴"
            : "[SPACE] 다시 도전   |   [ESC] 메뉴";
        DrawTextCentered(retryMsg, SCREEN_WIDTH / 2.0f, 390.0f, 22.0f, WHITE);
    }
    else if (screen == SCR_CLEAR) {
        DrawRectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, Fade(BLACK, 0.70f));
        DrawTextCentered("월드 클리어!", SCREEN_WIDTH / 2.0f, 215.0f, 58.0f, GOLD);
        DrawTextCentered(TextFormat("최종 정확도: %.2f%%   (완벽 판정: %d / %d)", accPct, perfectCount, hitCount),
                         SCREEN_WIDTH / 2.0f, 292.0f, 24.0f, SKYBLUE);
        DrawTextCentered("[SPACE] 다음 월드   |   [ESC] 월드 선택",
                         SCREEN_WIDTH / 2.0f, 365.0f, 22.0f, WHITE);
    }

    EndDrawing();
}

int main() {
    SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "얼음과 불의 춤 - C++");

    gameFont = LoadKoreanFont("font.ttf", 48);
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
