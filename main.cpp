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
const int WORLD_COUNT = 10;

enum ScreenState {
    SCR_MODE_SELECT,
    SCR_WORLD_SELECT,
    SCR_COUNTDOWN,
    SCR_PLAYING,
    SCR_GAMEOVER,
    SCR_CLEAR
};

enum GameMode {
    MODE_JOURNEY = 0,      // 월드 투어 (10개 테마 월드)
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
    float speedMult = 1.0f; // 1.0f = 기본, >1.0f = 토끼(가속), <1.0f = 달팽이(감속)
    bool cleared = false;
    float hitAnim = 0.0f;
};

struct MapStep {
    float angleDeg;
    bool swirl = false;
    bool midspin = false;
    bool checkpoint = false;
    float speedMult = 1.0f;
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
        "첫 번째 불꽃", "입문 1", "오르골 발라드",
        "직진과 90도 계단으로 리듬감을 익히는 기초 코스",
        92.0f, 1, 24.0f,
        { 3, 7, 10, 12, 10, 7, 5, 10 },
        { 3, 8, 3, 10 }, 0, false, 0.0f
    },
    {
        "호숫가의 징검다리", "입문 2", "잔잔한 일렉트릭 피아노",
        "사각 연못 둘레와 첫 소용돌이(회전 방향 반전)",
        98.0f, 2, 165.0f,
        { 8, 12, 15, 17, 16, 12, 10, 15 },
        { 8, 5, 10, 3 }, 1, false, 0.0f
    },
    {
        "산들바람 언덕", "초급 1", "맑은 겨울 차임",
        "45도 완만한 오르막·내리막 능선과 첫 가속(토끼) 타일",
        104.0f, 3, 195.0f,
        { 12, 15, 17, 14, 15, 12, 10, 7 },
        { 0, 8, 3, 10 }, 2, false, 0.0f
    },
    {
        "달팽이 정원", "초급 2", "따뜻한 오르골 하모니",
        "감속(달팽이)과 가속(토끼)이 교차하는 나선형 정원",
        110.0f, 4, 130.0f,
        { 5, 8, 10, 12, 15, 12, 10, 8 },
        { 5, 0, 8, 10 }, 0, false, 0.0f
    },
    {
        "쌍둥이 부메랑", "중급 1", "천상의 하프 아르페지오",
        "길 끝에서 180도 되돌아오는 왕복 유턴 부메랑",
        116.0f, 5, 42.0f,
        { 5, 8, 12, 15, 14, 10, 12, 8 },
        { 5, 10, 1, 0 }, 3, false, 0.0f
    },
    {
        "별빛 다이아몬드", "중급 2", "몽환적 첼레스타 선율",
        "마름모 광장과 첫 삼각 미드스핀(3박자 왈츠 스텝)",
        122.0f, 6, 248.0f,
        { 7, 10, 14, 15, 14, 10, 7, 10 },
        { 7, 3, 8, 10 }, 2, false, 0.0f
    },
    {
        "벌집의 춤", "상급 1", "레트로 신스웨이브 리프",
        "60도·120도 육각형 벌집 테두리와 변속 리듬",
        128.0f, 7, 54.0f,
        { 7, 8, 11, 14, 15, 14, 11, 8 },
        { 7, 8, 5, 7 }, 4, false, 0.0f
    },
    {
        "모래시계 회랑", "상급 2", "깊은 일렉트로닉 펄스",
        "대각선이 교차하는 모래시계와 연속 방향 반전",
        135.0f, 8, 285.0f,
        { 0, 3, 7, 10, 12, 10, 7, 3 },
        { 0, 5, 7, 0 }, 1, false, 0.0f
    },
    {
        "왕관의 성채", "도전 1", "웅장한 파이프 오르간",
        "뾰족한 왕관 능선을 따라 이어지는 미드스핀 연타",
        142.0f, 9, 340.0f,
        { 3, 6, 10, 11, 10, 6, 5, 2 },
        { 3, 11, 8, 10 }, 5, false, 0.0f
    },
    {
        "무지개 피날레", "도전 2", "축제 심포니 오르간",
        "소용돌이·미드스핀·변속이 모두 어우러지는 대단원",
        150.0f, 10, 315.0f,
        { 3, 7, 10, 15, 14, 10, 12, 15 },
        { 3, 10, 8, 10 }, 5, false, 0.0f
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

float baseBpm = 92.0f;
float currentBpm = 92.0f;
float currentAngle = 0.0f;
float targetDiff = PI;
float traveledAngle = 0.0f;

int countdownBeats = 5;
float beatTimer = 0.0f;
int totalBeatCount = 0;
float cameraBeatPulse = 0.0f;
float screenShake = 0.0f;
float overloadFlash = 0.0f;
float gameOverTimer = 0.0f;
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
Sound sndSpeedUp;
Sound sndSlowDown;
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
        "얼음과 불의 춤 게임 모드를 선택하세요 월드 투어 개 입문부터 도전까지 고유한 음악과 코스를 정복하세요 "
        "무한 러시 생존 모드 끝없이 생성되는 랜덤 코스를 밟으며 최고 기록에 도전하세요 연습 게임오버 없음 "
        "박자를 놓쳐도 실패하지 않고 끝까지 연습할 수 있습니다 퍼펙트 챌린지 원히트 도전 "
        "단 한 번의 실수나 어긋남도 허용되지 않는 고수용 모드입니다 최고 기록 타일 "
        "클릭 또는 방향키로 선택 판정 쉬움 보통 엄격 오토플레이 켜짐 꺼짐 싱크 조작 "
        "모드 선택 코스 모양 미리보기 월드 시작 난이도 음악 고정 코스 상태 클리어 완료 최고 정확도 "
        "아직 클리어하지 않음 진행률 연속 메뉴 빠름 느림 완벽 너무 체크포인트 놓침 준비 시작 "
        "행성 파괴 퍼펙트 챌린지 완벽하지 않은 타이밍 일찍 입력했습니다 과부하 놓쳤습니다 "
        "생존 기록 부활 처음부터 다시 도전 최종 판정 다음 가속 감속";

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
        } else if (type == 4) {
            // 상승 챠임 (토끼 가속)
            float env = expf(-t * 12.0f);
            float f = freq * (1.0f + 0.8f * (t / duration));
            sample = (sinf(2.0f * PI * f * t) * 0.7f + sinf(4.0f * PI * f * t) * 0.3f) * env;
        } else if (type == 5) {
            // 하강 챠임 (달팽이 감속)
            float env = expf(-t * 10.0f);
            float f = freq * (1.0f - 0.45f * (t / duration));
            sample = sinf(2.0f * PI * f * t) * env;
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
    sndHit      = SynthSFX(0, 587.33f, 0.10f, 0.72f);
    sndKick     = SynthSFX(1, 152.00f, 0.17f, 0.60f);
    sndSnare    = SynthSFX(2, 400.00f, 0.13f, 0.32f);
    sndHat      = SynthSFX(2, 2600.0f, 0.045f, 0.18f);
    sndSwirl    = SynthSFX(3, 440.00f, 0.14f, 0.45f);
    sndSpeedUp  = SynthSFX(4, 523.25f, 0.16f, 0.48f);
    sndSlowDown = SynthSFX(5, 440.00f, 0.18f, 0.48f);
    sndFail     = SynthSFX(3, 155.00f, 0.40f, 0.78f);
    sndClear    = SynthSFX(0, 880.00f, 0.45f, 0.78f);

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
// 5. 10개 테마 월드 코스 생성기 (진짜 얼불춤 기믹 포함)
// ==========================================
std::vector<MapStep> GetWorldSteps(int worldIdx) {
    if (worldIdx == 0) {
        // 1월드: 첫 번째 불꽃 (기초 직선 + 90도 계단)
        return {
            {0}, {0}, {0}, {0},
            {-90}, {0}, {-90}, {0},
            {0, false, false, true},
            {0}, {90}, {0}, {90},
            {0}, {0}, {0}, {0}
        };
    }
    else if (worldIdx == 1) {
        // 2월드: 호숫가의 징검다리 (사각 연못 + 첫 소용돌이 방향 반전)
        return {
            {0}, {0}, {-90}, {-90}, {180}, {180}, {90}, {0},
            {0, false, false, true},
            {0, true}, {0}, {90}, {90}, {0}, {0}, {-90}, {0}, {0}, {0}
        };
    }
    else if (worldIdx == 2) {
        // 3월드: 산들바람 언덕 (45도 능선 + 첫 토끼 가속 타일)
        return {
            {0}, {0}, {-45}, {-45}, {0, false, false, false, 1.25f}, {45}, {45}, {0, false, false, false, 1.0f},
            {0, false, false, true},
            {-45}, {45}, {-45}, {45},
            {0, true}, {0}, {-45}, {0}, {45}, {0}, {0}
        };
    }
    else if (worldIdx == 3) {
        // 4월드: 달팽이 정원 (달팽이 감속 + 나선 회전 + 토끼 가속 탈출)
        return {
            {0}, {0}, {0, false, false, false, 0.80f},
            {90}, {90}, {180}, {180}, {-90},
            {0, true, false, false, 1.15f},
            {-90}, {180}, {180}, {90}, {90}, {0, false, false, false, 1.0f},
            {0, false, false, true},
            {0}, {-90}, {0}, {90}, {0}, {0}, {0}
        };
    }
    else if (worldIdx == 4) {
        // 5월드: 쌍둥이 부메랑 (180도 왕복 부메랑 코스)
        return {
            {0}, {0}, {0}, {180}, {180},
            {-90}, {0}, {0},
            {0, false, false, true},
            {45}, {45, false, false, false, 1.20f}, {-135}, {-135, false, false, false, 1.0f},
            {0, true}, {0}, {90}, {0}, {-90}, {0}, {0}
        };
    }
    else if (worldIdx == 5) {
        // 6월드: 별빛 다이아몬드 (마름모 광장 + 첫 삼각 미드스핀 왈츠)
        return {
            {0}, {-45}, {-45}, {45}, {45},
            {90, false, true}, {0}, {90, false, true}, {0},
            {0, false, false, true},
            {45}, {45}, {-45}, {-45},
            {0, true},
            {-90, false, true}, {0}, {0}, {0}
        };
    }
    else if (worldIdx == 6) {
        // 7월드: 벌집의 춤 (60도·120도 육각형 벌집 테두리 + 변속)
        return {
            {0}, {-60}, {0}, {60, false, false, false, 1.20f}, {120}, {60}, {0, false, false, false, 1.0f},
            {0, false, false, true},
            {0, true},
            {60}, {0}, {-60, false, false, false, 0.85f}, {-120}, {-60}, {0, false, false, false, 1.0f},
            {-60}, {60}, {-60}, {60}, {0}, {0}
        };
    }
    else if (worldIdx == 7) {
        // 8월드: 모래시계 회랑 (대각선 교차 모래시계 + 연속 방향 반전)
        return {
            {0}, {-45}, {-45}, {90}, {45}, {45},
            {0, false, false, true},
            {0, true}, {-90, true}, {0, true}, {90, true},
            {-45, false, false, false, 1.22f}, {45}, {135}, {-135, false, false, false, 1.0f},
            {0, true}, {0}, {-45}, {45}, {0}, {0}
        };
    }
    else if (worldIdx == 8) {
        // 9월드: 왕관의 성채 (왕관 톱니 능선 + 미드스핀 연타)
        return {
            {0}, {-45}, {45}, {-90}, {90}, {-45}, {45}, {0},
            {0, false, false, true},
            {90, false, true}, {0}, {90, false, true}, {0},
            {0, true, false, false, 1.18f},
            {-45}, {45}, {-45}, {45, false, false, false, 1.0f},
            {-90, false, true}, {0}, {-90, false, true}, {0},
            {0, true}, {0}, {0}, {0}
        };
    }
    else {
        // 10월드: 무지개 피날레 (모든 기믹과 변속이 어우러지는 대단원)
        return {
            {0}, {0}, {-90}, {0}, {90}, {0},
            {90, false, false, false, 0.85f}, {180}, {-90}, {0, false, false, false, 1.0f},
            {0, false, false, true},
            {0, true}, {-45}, {45}, {-45}, {45}, {0},
            {-90, false, true}, {0}, {-90, false, true}, {0},
            {0, false, false, true},
            {0, true, false, false, 1.22f}, {60}, {0}, {-60}, {0}, {180, false, false, false, 1.0f}, {0},
            {90, false, true}, {0}, {0}, {0}
        };
    }
}

void BuildTilesFromSteps(const std::vector<MapStep>& steps) {
    tiles.clear();
    Vector2 curPos = { 0.0f, 0.0f };
    tiles.push_back({ curPos, false, false, false, 1.0f, true, 0.0f });

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
            steps[i].speedMult,
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
    const float angles[6] = { 0.0f, 0.0f, -90.0f, 90.0f, -45.0f, 45.0f };
    for (int i = 0; i < count; i++) {
        float deg = angles[GetRandomValue(0, 5)];
        bool swirl = (GetRandomValue(0, 7) == 0);
        float spMult = 1.0f;
        int rSp = GetRandomValue(0, 14);
        if (rSp == 0) spMult = 1.22f;
        else if (rSp == 1) spMult = 0.85f;

        float rad = deg * DEG2RAD;
        Vector2 nextPos = {
            endlessBuildPivot.x + cosf(rad) * TILE_DIST,
            endlessBuildPivot.y + sinf(rad) * TILE_DIST
        };
        tiles.push_back({ nextPos, swirl, false, false, spMult, false, 0.0f });
        endlessBuildPivot = nextPos;
    }
}

// ==========================================
// 6. 진짜 얼불춤 스타일 타일 · 이펙트 · 궤도 제어
// ==========================================
void InitAmbientMotes() {
    motes.clear();
    for (int i = 0; i < 48; i++) {
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
    shockwaves.push_back({ pos, col, 18.0f, maxRad, 0.88f });
}

void AddPopup(const std::string& msg, Vector2 pos, Color col) {
    popups.push_back({ msg, { pos.x, pos.y - 46.0f }, col, 0.78f });
}

void SetupOrbitForTile(int index, Vector2 prevPivotPos) {
    // 속도 변경 타일 적용 (토끼 / 달팽이)
    if (tiles[index].speedMult != 1.0f) {
        float prevBpm = currentBpm;
        currentBpm = baseBpm * tiles[index].speedMult;
        if (screen == SCR_PLAYING && fabsf(currentBpm - prevBpm) > 1.0f) {
            if (tiles[index].speedMult > 1.0f) {
                AddPopup("가속!", tiles[index].pos, ORANGE);
                SpawnShockwave(tiles[index].pos, ORANGE, 100.0f);
                if (audioReady) PlaySound(sndSpeedUp);
            } else {
                AddPopup("감속", tiles[index].pos, SKYBLUE);
                SpawnShockwave(tiles[index].pos, SKYBLUE, 100.0f);
                if (audioReady) PlaySound(sndSlowDown);
            }
        }
    }

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

void DrawDashedGlowLine(Vector2 p1, Vector2 p2, Color col) {
    Vector2 diff = Vector2Subtract(p2, p1);
    float len = Vector2Length(diff);
    if (len < 1.0f) return;
    Vector2 dir = Vector2Scale(diff, 1.0f / len);
    for (float d = 18.0f; d < len - 18.0f; d += 18.0f) {
        Vector2 a = Vector2Add(p1, Vector2Scale(dir, d));
        Vector2 b = Vector2Add(p1, Vector2Scale(dir, fminf(d + 9.0f, len - 18.0f)));
        DrawLineEx(a, b, 6.0f, col);
    }
}

// 진짜 얼불춤의 각진 사각 도로 세그먼트 렌더링
void DrawAdofaiRoadSegment(Vector2 p1, Vector2 p2, bool cleared, bool isActive, bool isFireActive, float timeNow) {
    Vector2 diff = Vector2Subtract(p2, p1);
    float len = Vector2Length(diff);
    if (len < 1.0f) return;
    float deg = atan2f(diff.y, diff.x) * RAD2DEG;

    float outerThick = isActive ? 54.0f : 50.0f;
    float innerThick = isActive ? 42.0f : 38.0f;

    Color borderCol = isActive ? (isFireActive ? SKYBLUE : ORANGE) :
                      cleared  ? Color{ 105, 148, 215, 220 } : Color{ 68, 86, 130, 240 };
    Color coreCol   = isActive ? Color{ 44, 62, 98, 255 } :
                      cleared  ? Color{ 34, 50, 82, 230 } : Color{ 23, 30, 48, 248 };

    // 그림자
    Rectangle shadowRec = { p1.x, p1.y + 7.0f, len, outerThick };
    DrawRectanglePro(shadowRec, { 0.0f, outerThick * 0.5f }, deg, Fade(BLACK, 0.35f));

    // 외곽 네온 테두리
    Rectangle outerRec = { p1.x, p1.y, len, outerThick };
    DrawRectanglePro(outerRec, { 0.0f, outerThick * 0.5f }, deg, borderCol);

    // 내부 타일 바닥
    Rectangle innerRec = { p1.x, p1.y, len, innerThick };
    DrawRectanglePro(innerRec, { 0.0f, innerThick * 0.5f }, deg, coreCol);

    // 중앙 미세 레일 라인
    Rectangle centerLine = { p1.x, p1.y, len, 3.0f };
    DrawRectanglePro(centerLine, { 0.0f, 1.5f }, deg, Fade(WHITE, isActive ? 0.35f : 0.10f));

    if (isActive) {
        float pulse = 0.25f + 0.15f * sinf(timeNow * 12.0f);
        DrawRectanglePro(outerRec, { 0.0f, outerThick * 0.5f }, deg, Fade(WHITE, pulse));
    }
}

void DrawSpeedChevron(Vector2 pos, float dirDeg, bool fast, float timeNow) {
    float rad = dirDeg * DEG2RAD;
    Vector2 fwd = { cosf(rad), sinf(rad) };
    Vector2 right = { -fwd.y, fwd.x };
    Color col = fast ? Color{ 255, 155, 45, 255 } : Color{ 75, 225, 255, 255 };

    int count = fast ? 2 : 1;
    for (int k = 0; k < count; k++) {
        float shift = (fast ? (-6.0f + k * 11.0f) : 0.0f) + 2.5f * sinf(timeNow * 7.0f);
        Vector2 c = Vector2Add(pos, Vector2Scale(fwd, shift));
        Vector2 tip = Vector2Add(c, Vector2Scale(fwd, 9.0f));
        Vector2 w1  = Vector2Add(Vector2Subtract(c, Vector2Scale(fwd, 6.0f)), Vector2Scale(right, 11.0f));
        Vector2 w2  = Vector2Subtract(Vector2Subtract(c, Vector2Scale(fwd, 6.0f)), Vector2Scale(right, 11.0f));
        DrawLineEx(w1, tip, 3.5f, col);
        DrawLineEx(w2, tip, 3.5f, col);
    }
}

void DrawSingleTile(size_t i, float timeNow, float segDeg, int remainingStackCount, bool isFirstStackHolder, bool isUTurnTile) {
    float scale = 1.0f + tiles[i].hitAnim * 0.28f;
    bool isCurrent = ((int)i == currentTile);
    bool isNextTarget = ((int)i == currentTile + 1);
    bool isGoal = (currentMode != MODE_ENDLESS && i == tiles.size() - 1);

    Color rimCol = isCurrent    ? WHITE :
                   isNextTarget ? Color{ 120, 240, 255, 255 } :
                   tiles[i].cleared ? Color{ 115, 158, 225, 255 } : Color{ 75, 92, 130, 255 };
    if (isGoal) rimCol = GOLD;

    // 겹침 타일 하단 스택 표시
    if (remainingStackCount >= 2 && isFirstStackHolder) {
        Vector2 stackOffset = { tiles[i].pos.x - 5.0f, tiles[i].pos.y + 7.0f };
        if (tiles[i].midspin) {
            DrawPoly(stackOffset, 3, 27.0f, timeNow * 70.0f, Fade(GOLD, 0.75f));
        } else {
            Rectangle stRec = { stackOffset.x, stackOffset.y, 48.0f, 48.0f };
            DrawRectanglePro(stRec, { 24.0f, 24.0f }, segDeg, Color{ 255, 190, 60, 220 });
            Rectangle stIn  = { stackOffset.x, stackOffset.y, 38.0f, 38.0f };
            DrawRectanglePro(stIn, { 19.0f, 19.0f }, segDeg, Color{ 35, 32, 48, 255 });
        }
    }

    if (tiles[i].midspin) {
        // 미드스핀 삼각형 타일
        DrawPoly(tiles[i].pos, 3, 27.5f * scale, timeNow * 70.0f, MAGENTA);
        DrawPoly(tiles[i].pos, 3, 17.0f * scale, timeNow * 70.0f, { 26, 30, 46, 255 });
    } else {
        // 진짜 얼불춤의 각진 사각형 연결 노드 타일
        float outerSz = 50.0f * scale;
        float innerSz = 38.0f * scale;
        Rectangle outerBox = { tiles[i].pos.x, tiles[i].pos.y, outerSz, outerSz };
        Rectangle innerBox = { tiles[i].pos.x, tiles[i].pos.y, innerSz, innerSz };

        DrawRectanglePro(outerBox, { outerSz * 0.5f, outerSz * 0.5f }, segDeg, rimCol);
        DrawRectanglePro(innerBox, { innerSz * 0.5f, innerSz * 0.5f }, segDeg,
                         isNextTarget ? Color{ 34, 48, 76, 255 } : Color{ 23, 30, 48, 255 });
    }

    // 다음 타겟 펄스 프레임
    if (isNextTarget) {
        float pulse = 56.0f + 6.0f * sinf(timeNow * 9.0f);
        Rectangle pRec = { tiles[i].pos.x, tiles[i].pos.y, pulse, pulse };
        DrawRectanglePro(pRec, { pulse * 0.5f, pulse * 0.5f }, segDeg, Fade(WHITE, 0.22f));
    }

    // 도착 골 포털
    if (isGoal) {
        DrawRing(tiles[i].pos, 12.0f, 16.5f, timeNow * 140.0f, timeNow * 140.0f + 240.0f, 24, GOLD);
        DrawCircleV(tiles[i].pos, 7.5f + 2.0f * sinf(timeNow * 6.0f), WHITE);
    }

    // 소용돌이(회전 방향 반전) 아이콘
    if (tiles[i].swirl) {
        float rot = timeNow * 220.0f;
        DrawRing(tiles[i].pos, 9.5f, 14.0f, rot, rot + 145.0f, 16, { 245, 95, 255, 255 });
        DrawRing(tiles[i].pos, 9.5f, 14.0f, rot + 180.0f, rot + 325.0f, 16, { 245, 95, 255, 255 });
    }

    // 토끼(가속) / 달팽이(감속) 기믹 아이콘
    if (tiles[i].speedMult > 1.01f) {
        DrawSpeedChevron(tiles[i].pos, segDeg, true, timeNow);
    } else if (tiles[i].speedMult < 0.99f) {
        DrawSpeedChevron(tiles[i].pos, segDeg + 180.0f, false, timeNow);
    }

    // 180도 유턴 표시
    if (isUTurnTile && !tiles[i].cleared) {
        float rot = timeNow * 95.0f;
        DrawRing(tiles[i].pos, 12.0f, 15.5f, rot, rot + 220.0f, 18, ORANGE);
    }

    // 체크포인트 표시
    if (tiles[i].checkpoint && currentMode == MODE_JOURNEY) {
        DrawCircleLinesV(tiles[i].pos, 29.0f, GREEN);
        DrawCircleLinesV(tiles[i].pos, 32.0f, Fade(GREEN, 0.45f));
    }

    // 겹침 카운트 배지
    if (remainingStackCount >= 2 && isFirstStackHolder) {
        Rectangle badge = { tiles[i].pos.x + 14.0f, tiles[i].pos.y - 36.0f, 34.0f, 20.0f };
        DrawRectangleRounded(badge, 0.45f, 6, GOLD);
        DrawRectangleRoundedLinesEx(badge, 0.45f, 6, 1.5f, BLACK);
        DrawTextCentered(TextFormat("x%d", remainingStackCount), badge.x + 17.0f, badge.y + 2.0f, 14.0f, BLACK, false);
    }
}

void DrawPathDirectionArrow(Vector2 p1, Vector2 p2, float tPos, Color col, float size) {
    Vector2 dir = Vector2Subtract(p2, p1);
    float len = Vector2Length(dir);
    if (len < 1.0f) return;
    dir = Vector2Scale(dir, 1.0f / len);
    Vector2 perp = { -dir.y, dir.x };
    Vector2 tip = Vector2Add(Vector2Lerp(p1, p2, tPos), Vector2Scale(dir, size * 0.6f));
    Vector2 left  = Vector2Add(Vector2Subtract(tip, Vector2Scale(dir, size * 1.3f)), Vector2Scale(perp, size * 0.85f));
    Vector2 right = Vector2Subtract(Vector2Subtract(tip, Vector2Scale(dir, size * 1.3f)), Vector2Scale(perp, size * 0.85f));
    DrawTriangle(tip, right, left, col);
}

void TriggerGameOver(const std::string& reason, Vector2 effectPos) {
    PlaySound(sndFail);
    screenShake = 18.0f;
    overloadFlash = 0.85f;
    gameOverTimer = 0.55f;
    failReason = reason;
    SpawnHitParticles(effectPos, RED, 36);
    SpawnShockwave(effectPos, RED, 135.0f);
    screen = SCR_GAMEOVER;
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
        tiles.push_back({ startPos, false, false, false, 1.0f, true, 0.0f });
        endlessBuildPivot = startPos;
        endlessTilesCleared = 0;
        baseBpm = 120.0f;
        currentBpm = baseBpm;
        bgHue = 185.0f;
        AppendEndlessTiles(40);
        startFromTile = 0;
    } else {
        BuildTilesFromSteps(GetWorldSteps(selectedWorld));
        baseBpm = worlds[selectedWorld].bpm;
        currentBpm = baseBpm;
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
    gameOverTimer = 0.0f;
    overloadFlash = 0.0f;

    Vector2 prevPiv = { 0, 0 };
    for (int i = 0; i <= startFromTile; i++) {
        tiles[i].cleared = true;
        if (i > 0 && tiles[i].swirl) cw = !cw;
        if (tiles[i].speedMult != 1.0f) currentBpm = baseBpm * tiles[i].speedMult;
        if (!tiles[i].midspin) {
            if (i > 0) prevPiv = pivotPos;
            pivotPos = tiles[i].pos;
        }
    }

    SetupOrbitForTile(startFromTile, prevPiv);

    // 실제 얼불춤 카운트다운: "준비!" -> "3" -> "2" -> "1" -> "시작!"
    countdownBeats = 5;
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
    return IsKeyPressed(KEY_SPACE) ||
           IsKeyPressed(KEY_Z) || IsKeyPressed(KEY_X) || IsKeyPressed(KEY_C) || IsKeyPressed(KEY_V) ||
           IsKeyPressed(KEY_D) || IsKeyPressed(KEY_F) || IsKeyPressed(KEY_J) || IsKeyPressed(KEY_K) ||
           IsKeyPressed(KEY_N) || IsKeyPressed(KEY_M) ||
           IsMouseButtonPressed(MOUSE_LEFT_BUTTON);
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
            SpawnShockwave(hitTilePos, GOLD, 165.0f);
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

    if (overloadFlash > 0.0f) overloadFlash = fmaxf(0.0f, overloadFlash - 2.2f * dt);

    for (auto& m : motes) {
        m.pos = Vector2Add(m.pos, Vector2Scale(m.vel, dt));
        m.phase += 2.0f * dt;
        if (m.pos.y < -10) m.pos.y = SCREEN_HEIGHT + 10;
        if (m.pos.x < -10) m.pos.x = SCREEN_WIDTH + 10;
        if (m.pos.x > SCREEN_WIDTH + 10) m.pos.x = -10;
    }

    Vector2 mp = GetMousePosition();
    bool clicked = IsMouseButtonPressed(MOUSE_LEFT_BUTTON);

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
                Rectangle pill = { 115.0f + w * 106.0f, 595.0f, 96.0f, 46.0f };
                if (CheckCollisionPointRec(mp, pill)) {
                    selectedWorld = w;
                    UpdateWorldPreview(selectedWorld);
                }
            }
        }
    }
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
                cameraBeatPulse = 0.048f;

                if (countdownBeats > 0) {
                    PlaySound(totalBeatCount % 2 == 0 ? sndKick : sndSnare);
                } else {
                    traveledAngle = 0.0f;
                    AddPopup("시작!", pivotPos, GOLD);
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
                    cameraBeatPulse = 0.038f;
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
                        TriggerGameOver("퍼펙트 챌린지: 완벽하지 않은 타이밍!", tiles[currentTile + 1].pos);
                    } else {
                        hitCount++;
                        streakCount++;
                        accuracySum += accVal;
                        hitTicks.push_back({ autoPlay ? 0.0f : errMs, jCol, 1.0f });
                        if (hitTicks.size() > 26) hitTicks.pop_front();

                        Vector2 hitTilePos = tiles[currentTile + 1].pos;
                        AddPopup(jText, hitTilePos, jCol);
                        SpawnHitParticles(hitTilePos, isFirePivot ? SKYBLUE : ORANGE, 20);
                        SpawnShockwave(hitTilePos, jCol, 76.0f);
                        PlaySound(sndHit);

                        screenShake = 4.5f;
                        AdvanceTileAfterHit(hitTilePos, false);
                    }
                } else {
                    if (currentMode == MODE_ZEN) {
                        screenShake = 8.0f;
                        overloadFlash = 0.35f;
                        streakCount = 0;
                        AddPopup("너무 빠름!", pivotPos, ORANGE);
                    } else {
                        TriggerGameOver("너무 일찍 입력했습니다! (과부하)", pivotPos);
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
                    TriggerGameOver("박자를 놓쳤습니다!", tiles[currentTile + 1].pos);
                }
            }
        }
    }
    else if (screen == SCR_GAMEOVER) {
        if (gameOverTimer > 0.0f) gameOverTimer -= dt;

        if (IsKeyPressed(KEY_ESCAPE)) screen = (currentMode == MODE_ENDLESS) ? SCR_MODE_SELECT : SCR_WORLD_SELECT;
        else if (IsKeyPressed(KEY_R)) StartGame(0);
        else if (gameOverTimer <= 0.0f && IsHitKeyPressed()) {
            StartGame(currentMode == MODE_JOURNEY ? lastCheckpoint : 0);
        }
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
            fireTrail.push_back({ firePos, { 255, 80, 45, 255 }, 0.82f });
            iceTrail.push_back({ icePos, { 45, 205, 255, 255 }, 0.82f });
            if (fireTrail.size() > 26) fireTrail.pop_front();
            if (iceTrail.size() > 26) iceTrail.pop_front();
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

    if (screen == SCR_MODE_SELECT) {
        DrawTextCentered("얼음과 불의 춤", SCREEN_WIDTH / 2.0f, 46.0f, 52.0f, WHITE);
        DrawTextCentered("게임 모드를 선택하세요", SCREEN_WIDTH / 2.0f, 110.0f, 20.0f, SKYBLUE);

        const char* modeTitles[4] = { "월드 투어", "무한 러시", "연습 모드", "퍼펙트 챌린지" };
        const char* modeBadges[4] = { "10개 월드", "생존 모드", "게임오버 없음", "원히트 도전" };
        const char* modeDescs[4]  = {
            "입문부터 도전까지 10개의 고유한 음악과 코스를 정복하세요.",
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

        DrawTextCentered("조작: Z / X / C / V / SPACE / 클릭", SCREEN_WIDTH / 2.0f, 622.0f, 16.0f, GRAY);

        EndDrawing();
        return;
    }

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

        Rectangle mapBox = { 205.0f, 155.0f, 390.0f, 355.0f };
        DrawRectangleRounded(mapBox, 0.1f, 8, Color{ 12, 15, 24, 255 });
        DrawTextCentered("코스 모양 미리보기", mapBox.x + mapBox.width * 0.5f, mapBox.y + 16.0f, 16.0f, GRAY);

        Vector2 mapCenter = { mapBox.x + mapBox.width * 0.5f, mapBox.y + mapBox.height * 0.53f };
        float mapRadius = 125.0f;
        for (size_t i = 0; i + 1 < previewPoints.size(); i++) {
            Vector2 p1 = { mapCenter.x + previewPoints[i].x * mapRadius, mapCenter.y + previewPoints[i].y * mapRadius };
            Vector2 p2 = { mapCenter.x + previewPoints[i + 1].x * mapRadius, mapCenter.y + previewPoints[i + 1].y * mapRadius };
            DrawLineEx(p1, p2, 7.0f, Fade(theme, 0.45f));
            DrawLineEx(p1, p2, 2.5f, Fade(WHITE, 0.65f));
            DrawCircleV(p1, 4.5f, WHITE);
        }
        if (!previewPoints.empty()) {
            Vector2 startPt = { mapCenter.x + previewPoints[0].x * mapRadius, mapCenter.y + previewPoints[0].y * mapRadius };
            Vector2 endPt   = { mapCenter.x + previewPoints.back().x * mapRadius, mapCenter.y + previewPoints.back().y * mapRadius };
            DrawCircleV(startPt, 7.5f, SKYBLUE);
            DrawCircleV(endPt, 7.5f, GOLD);
        }

        float rx = 635.0f;
        DrawTextUI(TextFormat("%d월드  •  %s", selectedWorld + 1, w.diffLabel), rx, 155.0f, 20.0f, theme);
        DrawTextUI(w.name, rx, 188.0f, 38.0f, WHITE);

        DrawTextUI("난이도:", rx, 248.0f, 17.0f, GRAY);
        for (int d = 0; d < 10; d++) {
            Color segCol = (d < w.difficultyLevel) ? theme : Fade(WHITE, 0.12f);
            DrawRectangleRounded({ rx + 75.0f + d * 35.0f, 246.0f, 29.0f, 18.0f }, 0.3f, 4, segCol);
        }

        DrawTextUI(TextFormat("음악: %s (%.0f BPM)", w.songStyle, w.bpm), rx, 290.0f, 18.0f, LIGHTGRAY);
        DrawTextUI(TextFormat("코스: %s", w.gimmickDesc), rx, 325.0f, 17.5f, LIGHTGRAY);

        if (w.cleared) {
            DrawTextUI(TextFormat("상태: 클리어 완료!  (최고 정확도: %.1f%%)", w.bestAcc), rx, 368.0f, 17.5f, GREEN);
        } else {
            DrawTextUI("상태: 아직 클리어하지 않음", rx, 368.0f, 17.5f, GRAY);
        }

        Rectangle playBtn = { rx, 445.0f, 430.0f, 64.0f };
        bool hoverPlay = CheckCollisionPointRec(mp, playBtn);
        DrawRectangleRounded(playBtn, 0.25f, 8, hoverPlay ? WHITE : theme);
        DrawTextCentered("월드 시작  [SPACE / ZXCV]", playBtn.x + playBtn.width * 0.5f, playBtn.y + 18.0f, 22.0f, BLACK, false);

        for (int i = 0; i < WORLD_COUNT; i++) {
            Rectangle pill = { 115.0f + i * 106.0f, 595.0f, 96.0f, 46.0f };
            bool isSel = (i == selectedWorld);
            Color pCol = ColorFromHSV(worlds[i].hue, 0.65f, 0.95f);
            DrawCardBox(pill, 0.3f, isSel ? Fade(pCol, 0.32f) : Color{ 20, 24, 38, 230 }, isSel ? pCol : Fade(WHITE, 0.16f), 2.0f);
            DrawTextCentered(TextFormat("%d월드", i + 1), pill.x + 48.0f, pill.y + 13.0f, 17.5f, isSel ? WHITE : LIGHTGRAY);
        }

        EndDrawing();
        return;
    }

    // ------------------------------------------
    // 렌더링 3: 인게임 월드 렌더링
    // ------------------------------------------
    BeginMode2D(camera);
    if (!tiles.empty()) {
        float timeNow = (float)GetTime();

        std::vector<Vector2> segStart(tiles.size());
        std::vector<Vector2> segEnd(tiles.size());
        std::vector<float>   tileDeg(tiles.size(), 0.0f);
        Vector2 renderPivot = tiles[0].pos;
        for (size_t i = 0; i + 1 < tiles.size(); i++) {
            segStart[i] = renderPivot;
            segEnd[i]   = tiles[i + 1].pos;
            float deg = atan2f(segEnd[i].y - segStart[i].y, segEnd[i].x - segStart[i].x) * RAD2DEG;
            tileDeg[i] = deg;
            if (i + 1 == tiles.size() - 1) tileDeg[i + 1] = deg;
            if (!tiles[i + 1].midspin) {
                renderPivot = tiles[i + 1].pos;
            }
        }

        // [1단계] 지나간 길 & 미래의 길 (각진 사각 도로)
        for (size_t i = 0; i + 1 < tiles.size(); i++) {
            if ((int)i == currentTile) continue;
            Vector2 p1 = segStart[i];
            Vector2 p2 = segEnd[i];

            if (tiles[i + 1].midspin) {
                DrawDashedGlowLine(p1, p2, Fade(MAGENTA, tiles[i].cleared ? 0.30f : 0.75f));
                continue;
            }

            DrawAdofaiRoadSegment(p1, p2, tiles[i].cleared, false, isFirePivot, timeNow);

            if ((int)i > currentTile && (int)i <= currentTile + 3) {
                DrawPathDirectionArrow(p1, p2, 0.52f, Fade(WHITE, 0.28f), 8.0f);
            }
        }

        // [2단계] 현재 진행 중인 활성 길 강조
        if (currentTile >= 0 && currentTile + 1 < (int)tiles.size()) {
            Vector2 p1 = segStart[currentTile];
            Vector2 p2 = segEnd[currentTile];
            if (tiles[currentTile + 1].midspin) {
                DrawDashedGlowLine(p1, p2, MAGENTA);
            } else {
                DrawAdofaiRoadSegment(p1, p2, true, true, isFirePivot, timeNow);
                float flow = fmodf(timeNow * 1.4f, 1.0f);
                DrawPathDirectionArrow(p1, p2, 0.25f + flow * 0.5f, Fade(WHITE, 0.85f), 10.0f);
            }
        }

        // 공전 궤도 및 남은 각도 호
        DrawCircleLinesV(pivotPos, TILE_DIST, Fade(WHITE, 0.20f));
        if (screen == SCR_PLAYING) {
            float remRatio = Clamp(1.0f - (traveledAngle / targetDiff), 0.0f, 1.0f);
            float startDeg = currentAngle * RAD2DEG;
            float endDeg   = (currentAngle + (cw ? 1.0f : -1.0f) * targetDiff * remRatio) * RAD2DEG;
            DrawRing(pivotPos, TILE_DIST - 6.0f, TILE_DIST + 2.0f, startDeg, endDeg, 24,
                     Fade(isFirePivot ? SKYBLUE : ORANGE, 0.30f));
        }

        std::vector<int> remainingStack(tiles.size(), 1);
        std::vector<bool> firstStackHolder(tiles.size(), true);
        std::vector<bool> isUTurn(tiles.size(), false);

        for (size_t i = 0; i < tiles.size(); i++) {
            int count = 0;
            bool seenEarlierRemaining = false;
            for (size_t j = (size_t)currentTile; j < tiles.size(); j++) {
                if (Vector2Distance(tiles[i].pos, tiles[j].pos) < 10.0f) {
                    if (j < i) seenEarlierRemaining = true;
                    count++;
                }
            }
            remainingStack[i] = count;
            firstStackHolder[i] = !seenEarlierRemaining;

            if (i > 0 && i + 1 < tiles.size() && !tiles[i].midspin && !tiles[i + 1].midspin) {
                Vector2 vIn  = Vector2Normalize(Vector2Subtract(segEnd[i - 1], segStart[i - 1]));
                Vector2 vOut = Vector2Normalize(Vector2Subtract(segEnd[i], segStart[i]));
                if (Vector2DotProduct(vIn, vOut) < -0.90f) {
                    isUTurn[i] = true;
                }
            }
        }

        // [3단계] 타일 Z-순서 렌더링
        for (size_t i = 0; i < tiles.size(); i++) {
            if ((int)i == currentTile || (int)i == currentTile + 1) continue;
            if (!tiles[i].cleared) continue;
            DrawSingleTile(i, timeNow, tileDeg[i], remainingStack[i], firstStackHolder[i], isUTurn[i]);
        }
        for (size_t i = 0; i < tiles.size(); i++) {
            if ((int)i == currentTile || (int)i == currentTile + 1) continue;
            if (tiles[i].cleared) continue;
            DrawSingleTile(i, timeNow, tileDeg[i], remainingStack[i], firstStackHolder[i], isUTurn[i]);
        }
        if (currentTile >= 0 && currentTile < (int)tiles.size()) {
            DrawSingleTile((size_t)currentTile, timeNow, tileDeg[currentTile], remainingStack[currentTile], firstStackHolder[currentTile], isUTurn[currentTile]);
        }
        if (currentTile + 1 >= 0 && currentTile + 1 < (int)tiles.size()) {
            DrawSingleTile((size_t)(currentTile + 1), timeNow, tileDeg[currentTile + 1], remainingStack[currentTile + 1], firstStackHolder[currentTile + 1], isUTurn[currentTile + 1]);
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

        // 불 행성 (다층 글로우 + 코어)
        DrawCircleV(firePos, PLANET_RADIUS + 10.0f, Fade(ORANGE, 0.25f));
        DrawCircleLinesV(firePos, PLANET_RADIUS + 4.0f, Fade(GOLD, 0.55f));
        DrawCircleV(firePos, PLANET_RADIUS, { 255, 65, 45, 255 });
        DrawCircleV(firePos, PLANET_RADIUS * 0.52f, { 255, 225, 160, 255 });

        // 얼음 행성 (다층 글로우 + 수정 코어)
        DrawCircleV(icePos, PLANET_RADIUS + 10.0f, Fade(SKYBLUE, 0.25f));
        DrawCircleLinesV(icePos, PLANET_RADIUS + 4.0f, Fade(WHITE, 0.55f));
        DrawCircleV(icePos, PLANET_RADIUS, { 45, 195, 255, 255 });
        DrawCircleV(icePos, PLANET_RADIUS * 0.52f, { 220, 250, 255, 255 });

        for (const auto& p : popups) {
            DrawTextCentered(p.text.c_str(), p.pos.x, p.pos.y, 20.0f, Fade(p.color, Clamp(p.timer * 1.4f, 0.0f, 1.0f)));
        }
    }
    EndMode2D();

    if (overloadFlash > 0.0f) {
        DrawRectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, Fade(RED, overloadFlash * 0.35f));
    }

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
        DrawTextUI(TextFormat("%d%%   •   정확도: %.1f%%   •   BPM: %.0f", (int)(ratio * 100.0f), accPct, currentBpm), 300.0f, 48.0f, 18.0f, LIGHTGRAY);
    }

    Rectangle menuBtn = { 1130.0f, 18.0f, 120.0f, 38.0f };
    DrawCardBox(menuBtn, 0.3f, Color{ 20, 24, 38, 220 }, Fade(WHITE, 0.25f), 1.5f);
    DrawTextCentered("메뉴 [ESC]", menuBtn.x + 60.0f, menuBtn.y + 10.0f, 16.0f, WHITE);

    if (autoPlay) {
        DrawTextUI("[오토플레이]", 990.0f, 26.0f, 18.0f, GOLD);
    }

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

    if (screen == SCR_COUNTDOWN) {
        const char* cdText = (countdownBeats == 5) ? "준비!" :
                             (countdownBeats == 4) ? "3" :
                             (countdownBeats == 3) ? "2" :
                             (countdownBeats == 2) ? "1" : "준비!";
        Color cdCol = (countdownBeats == 5 || countdownBeats == 1) ? SKYBLUE : GOLD;
        DrawTextCentered(cdText, SCREEN_WIDTH / 2.0f, 135.0f, 64.0f, cdCol);
    }
    else if (screen == SCR_GAMEOVER) {
        DrawRectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, Fade(BLACK, 0.70f));
        DrawTextCentered("행성 파괴!", SCREEN_WIDTH / 2.0f, 215.0f, 58.0f, RED);
        DrawTextCentered(failReason.c_str(), SCREEN_WIDTH / 2.0f, 290.0f, 24.0f, LIGHTGRAY);
        if (currentMode == MODE_ENDLESS) {
            DrawTextCentered(TextFormat("생존 기록: %d 타일  (최고 기록: %d 타일)", endlessTilesCleared, bestEndlessScore),
                             SCREEN_WIDTH / 2.0f, 332.0f, 22.0f, SKYBLUE);
        }
        if (gameOverTimer <= 0.0f) {
            const char* retryMsg = (currentMode == MODE_JOURNEY && lastCheckpoint > 0)
                ? "[SPACE / ZXCV] 체크포인트 부활   |   [R] 처음부터   |   [ESC] 메뉴"
                : "[SPACE / ZXCV] 다시 도전   |   [ESC] 메뉴";
            DrawTextCentered(retryMsg, SCREEN_WIDTH / 2.0f, 390.0f, 22.0f, WHITE);
        }
    }
    else if (screen == SCR_CLEAR) {
        DrawRectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, Fade(BLACK, 0.70f));
        DrawTextCentered("월드 클리어!", SCREEN_WIDTH / 2.0f, 215.0f, 58.0f, GOLD);
        DrawTextCentered(TextFormat("최종 정확도: %.2f%%   (완벽 판정: %d / %d)", accPct, perfectCount, hitCount),
                         SCREEN_WIDTH / 2.0f, 292.0f, 24.0f, SKYBLUE);
        DrawTextCentered("[SPACE / ZXCV] 다음 월드   |   [ESC] 월드 선택",
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
        UnloadSound(sndHat); UnloadSound(sndSwirl); UnloadSound(sndSpeedUp);
        UnloadSound(sndSlowDown); UnloadSound(sndFail); UnloadSound(sndClear);
        for (int inst = 0; inst < 6; inst++) {
            for (int n = 0; n < 18; n++) UnloadSound(sndMelody[inst][n]);
        }
        for (int n = 0; n < 18; n++) UnloadSound(sndBass[n]);
        CloseAudioDevice();
    }
    CloseWindow();
    return 0;
}
