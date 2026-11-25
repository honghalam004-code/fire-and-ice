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
// 1. 상수 및 열거형 정의 (원작 ADOFAI 악보-타일 동기화 규격)
// ==========================================
const int SCREEN_WIDTH = 1280;
const int SCREEN_HEIGHT = 720;
const float TILE_STEP = 84.0f;
const float TILE_WIDTH = 52.0f;
const float PLANET_RADIUS = 16.5f;
const int WORLD_COUNT = 12;

enum ScreenState {
    SCR_MODE_SELECT,
    SCR_WORLD_SELECT,
    SCR_PLAYING,
    SCR_GAMEOVER,
    SCR_CLEAR
};

enum GameMode {
    MODE_JOURNEY = 0,      // 월드 투어 (1-X ~ 12-X)
    MODE_ENDLESS = 1,      // 무한 러시
    MODE_ZEN = 2,          // 연습 모드 (게임오버 없음)
    MODE_PERFECTIONIST = 3 // 퍼펙트 챌린지 (완벽 판정 전용)
};

enum Strictness {
    STRICT_EASY   = 0,
    STRICT_NORMAL = 1,
    STRICT_STRICT = 2
};

// 악보의 한 음표(Note)가 곧 하나의 타일 각도(박자 길이)를 결정함!
struct SongNote {
    float beatLen;      // 1.0f = 180도(1박), 0.5f = 90도(반박), 1.5f = 270도, 2.0f = 360도(유턴), 0.0f = 미드스핀
    int turnDir;        // 0: 직진/유지, -1: 위/반시계 꺾임, +1: 아래/시계 꺾임, 2: 유턴(180도 반전)
    int melodyMidi;     // 이 타일을 밟는 순간 울리는 멜로디 음정 (MIDI 번호, -1이면 드럼만)
    int chordRootMidi;  // 현재 마디의 화음 루트 (MIDI 번호, 0이면 유지)
    int drumType;       // 0: 없음, 1: 킥+베이스, 2: 스네어, 3: 하이햇, 4: 심벌 크래시
    bool swirl;         // 소용돌이 (회전 방향 반전)
    bool midspin;       // 삼각형 미드스핀
    bool checkpoint;    // 체크포인트
    float speedMult;    // 속도 배율 (>1 토끼, <1 달팽이)
};

struct Tile {
    Vector2 pos;
    float enterAngleDeg = 0.0f;
    float exitAngleDeg = 0.0f;
    float beatLen = 1.0f;
    int melodyMidi = -1;
    int chordRootMidi = 0;
    int drumType = 1;
    bool isLeadIn = false;
    bool swirl = false;
    bool midspin = false;
    bool checkpoint = false;
    float speedMult = 1.0f;
    bool cleared = false;
    float hitAnim = 0.0f;
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
    bool spark;
};

struct Shockwave {
    Vector2 pos;
    Color color;
    float radius;
    float maxRadius;
    float alpha;
    float thickness;
};

struct BgStar {
    Vector2 pos;
    float depth;
    float size;
    float phase;
};

struct ShootingStar {
    Vector2 pos;
    Vector2 vel;
    float life;
    float maxLife;
};

struct JudgmentPopup {
    std::string text;
    std::string subText;
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
    const char* code;
    const char* name;
    const char* diffLabel;
    const char* songStyle;
    const char* gimmickDesc;
    float bpm;
    int difficultyLevel;
    float hue;
    int synthPreset; // 0: 일렉트릭 그랜드, 1: 오르골 첼레스타, 2: 슈퍼소 신스웨이브, 3: 파이프 오르간
    bool cleared;
    float bestAcc;
};

// ==========================================
// 2. 실시간 DSP 신디사이저 오디오 스트림 엔진
//    (끊김 없는 스테레오 화음 + 딜레이 에코 + 사이드체인 베이스)
// ==========================================
const int SAMPLE_RATE = 44100;
const int DELAY_BUF_LEN = 22050; // 0.5초 스테레오 딜레이 에코 버퍼

struct SynthVoice {
    bool active = false;
    int type = 0;       // 0: 리드 벨/피아노, 1: 따뜻한 화음 패드, 2: 서브 베이스, 3: 킥, 4: 스네어, 5: 하이햇, 6: FX
    float freq = 440.0f;
    float phase1 = 0.0f;
    float phase2 = 0.0f;
    float time = 0.0f;
    float duration = 0.5f;
    float volume = 0.3f;
    float pan = 0.0f;   // -1.0(좌) ~ +1.0(우)
    float lpState = 0.0f;
};

const int MAX_VOICES = 32;
SynthVoice voices[MAX_VOICES];
float delayLineL[DELAY_BUF_LEN] = { 0 };
float delayLineR[DELAY_BUF_LEN] = { 0 };
int delayWriteIdx = 0;
float sidechainDuck = 1.0f;
int activeSynthPreset = 0;
AudioStream synthStream = { 0 };
bool audioReady = false;

float MidiToFreq(int midi) {
    return 440.0f * powf(2.0f, (midi - 69) / 12.0f);
}

void TriggerVoice(int type, float freq, float duration, float volume, float pan = 0.0f) {
    if (!audioReady) return;
    int slot = 0;
    float oldestTime = -1.0f;
    for (int i = 0; i < MAX_VOICES; i++) {
        if (!voices[i].active) {
            slot = i;
            break;
        }
        if (voices[i].time > oldestTime) {
            oldestTime = voices[i].time;
            slot = i;
        }
    }
    voices[slot].active = true;
    voices[slot].type = type;
    voices[slot].freq = freq;
    voices[slot].phase1 = 0.0f;
    voices[slot].phase2 = 0.25f;
    voices[slot].time = 0.0f;
    voices[slot].duration = duration;
    voices[slot].volume = volume;
    voices[slot].pan = pan;
    voices[slot].lpState = 0.0f;

    if (type == 3) {
        sidechainDuck = 0.28f; // 킥이 칠 때 패드가 숨쉬듯 울리는 사이드체인 펌핑!
    }
}

void PlayChordPad(int rootMidi, bool isMinor) {
    if (rootMidi <= 0) return;
    int third = isMinor ? 3 : 4;
    TriggerVoice(1, MidiToFreq(rootMidi), 2.2f, 0.13f, -0.35f);
    TriggerVoice(1, MidiToFreq(rootMidi + third), 2.2f, 0.11f, 0.35f);
    TriggerVoice(1, MidiToFreq(rootMidi + 7), 2.2f, 0.12f, 0.0f);
    TriggerVoice(2, MidiToFreq(rootMidi - 12), 0.85f, 0.28f, 0.0f);
}

void PlayNoteForTile(const Tile& t, int preset, bool isMinorScale) {
    if (!audioReady) return;
    activeSynthPreset = preset;

    // 1. 화음 진행 변경 시 풍성한 3화음 패드 + 서브 베이스 트리거
    if (t.chordRootMidi > 0) {
        PlayChordPad(t.chordRootMidi, isMinorScale);
    }

    // 2. 타일 고유 멜로디 음정 재생 (박자 길이에 맞춰 여운 조절)
    if (t.melodyMidi > 0) {
        float noteDur = Clamp(t.beatLen * 0.65f, 0.32f, 1.4f);
        float pan = sinf((float)t.melodyMidi * 0.7f) * 0.35f;
        TriggerVoice(0, MidiToFreq(t.melodyMidi), noteDur, 0.32f, pan);
        // 옥타브 하모니 플럭 추가
        TriggerVoice(0, MidiToFreq(t.melodyMidi + 12), noteDur * 0.6f, 0.10f, -pan);
    }

    // 3. 타일 리듬에 동기화된 드럼 킷
    if (t.drumType == 1) {
        TriggerVoice(3, 135.0f, 0.22f, 0.48f, 0.0f); // 묵직한 킥
    } else if (t.drumType == 2) {
        TriggerVoice(4, 210.0f, 0.18f, 0.34f, 0.05f); // 스네어
    } else if (t.drumType == 3) {
        TriggerVoice(5, 6000.0f, 0.06f, 0.16f, -0.25f); // 하이햇
    } else if (t.drumType == 4) {
        TriggerVoice(3, 140.0f, 0.25f, 0.52f, 0.0f);
        TriggerVoice(5, 4500.0f, 0.55f, 0.24f, 0.25f); // 심벌 크래시
    }
}

void AudioStreamCallback(void* buffer, unsigned int frames) {
    short* out = (short*)buffer;
    float dt = 1.0f / (float)SAMPLE_RATE;
    static unsigned int rngState = 123456789u;

    for (unsigned int f = 0; f < frames; f++) {
        float mixL = 0.0f;
        float mixR = 0.0f;
        sidechainDuck = Lerp(sidechainDuck, 1.0f, dt * 5.5f);

        for (int i = 0; i < MAX_VOICES; i++) {
            SynthVoice& v = voices[i];
            if (!v.active) continue;

            v.time += dt;
            if (v.time >= v.duration) {
                v.active = false;
                continue;
            }

            float t = v.time;
            float normT = t / v.duration;
            float s = 0.0f;

            if (v.type == 0) {
                // 리드 악기 (월드 테마별 음색)
                v.phase1 += v.freq * dt;
                v.phase2 += (v.freq * 1.003f) * dt;
                float env = fminf(1.0f, t * 90.0f) * expf(-t * (3.2f / fmaxf(0.35f, v.duration)));

                if (activeSynthPreset == 0) {
                    // 일렉트릭 그랜드 피아노
                    float raw = sinf(2.0f * PI * v.phase1) * 0.62f
                              + sinf(4.0f * PI * v.phase1) * 0.26f * expf(-t * 5.0f)
                              + sinf(6.0f * PI * v.phase1) * 0.12f * expf(-t * 9.0f);
                    s = raw * env;
                } else if (activeSynthPreset == 1) {
                    // 영롱한 첼레스타 / 오르골 벨
                    float raw = sinf(2.0f * PI * v.phase1) * 0.55f
                              + sinf(4.0f * PI * v.phase2) * 0.30f
                              + sinf(8.0f * PI * v.phase1) * 0.15f * expf(-t * 14.0f);
                    s = raw * env;
                } else if (activeSynthPreset == 2) {
                    // 디튠 슈퍼소 신스웨이브 (로패스 필터 적용)
                    float saw1 = 2.0f * (v.phase1 - floorf(0.5f + v.phase1));
                    float saw2 = 2.0f * (v.phase2 - floorf(0.5f + v.phase2));
                    float raw = (saw1 + saw2) * 0.42f + sinf(PI * v.phase1) * 0.35f;
                    float cutoff = 0.14f + 0.22f * expf(-t * 6.0f);
                    v.lpState += cutoff * (raw - v.lpState);
                    s = v.lpState * env;
                } else {
                    // 대성당 파이프 오르간
                    float raw = sinf(PI * v.phase1) * 0.35f
                              + sinf(2.0f * PI * v.phase1) * 0.40f
                              + sinf(4.0f * PI * v.phase2) * 0.25f;
                    s = raw * fminf(1.0f, t * 40.0f) * (1.0f - normT * normT);
                }
            } else if (v.type == 1) {
                // 따뜻한 스트링/신스 3화음 패드 (사이드체인 펌핑 적용)
                v.phase1 += v.freq * dt;
                v.phase2 += (v.freq * 1.004f) * dt;
                float env = fminf(1.0f, t * 3.5f) * expf(-t * 1.25f) * sidechainDuck;
                float saw = (2.0f * (v.phase1 - floorf(0.5f + v.phase1))
                           + 2.0f * (v.phase2 - floorf(0.5f + v.phase2))) * 0.35f;
                float sine = sinf(2.0f * PI * v.phase1) * 0.45f;
                v.lpState += 0.08f * ((saw + sine) - v.lpState);
                s = v.lpState * env;
            } else if (v.type == 2) {
                // 묵직한 서브 베이스
                v.phase1 += v.freq * dt;
                float env = fminf(1.0f, t * 45.0f) * expf(-t * 2.8f);
                s = (sinf(2.0f * PI * v.phase1) * 0.85f + sinf(4.0f * PI * v.phase1) * 0.15f) * env;
            } else if (v.type == 3) {
                // 펀치감 있는 킥 드럼
                float pitch = v.freq * expf(-t * 26.0f) + 44.0f;
                v.phase1 += pitch * dt;
                float env = expf(-t * 14.5f);
                s = sinf(2.0f * PI * v.phase1) * env;
            } else if (v.type == 4) {
                // 스네어 드럼
                rngState = rngState * 1664525u + 1013904223u;
                float noise = ((float)(rngState >> 16) / 32768.0f) - 1.0f;
                v.phase1 += 185.0f * dt;
                float env = expf(-t * 20.0f);
                s = (noise * 0.68f + sinf(2.0f * PI * v.phase1) * 0.32f * expf(-t * 32.0f)) * env;
            } else if (v.type == 5) {
                // 하이햇 / 심벌
                rngState = rngState * 1664525u + 1013904223u;
                float noise = ((float)(rngState >> 16) / 32768.0f) - 1.0f;
                float env = expf(-t * (v.duration > 0.2f ? 7.5f : 58.0f));
                s = noise * env;
            } else if (v.type == 6) {
                // 기믹/실패/클리어 효과음
                v.phase1 += v.freq * dt;
                float env = expf(-t * 9.0f);
                s = (sinf(2.0f * PI * v.phase1) * 0.7f + sinf(4.0f * PI * v.phase1) * 0.3f) * env;
            }

            s *= v.volume;
            float gainL = 0.5f * (1.0f - v.pan);
            float gainR = 0.5f * (1.0f + v.pan);
            mixL += s * gainL;
            mixR += s * gainR;

            // 리드 악기(type 0)와 스네어(type 4)를 스테레오 딜레이 에코로 전송
            if (v.type == 0 || v.type == 4) {
                delayLineL[delayWriteIdx] += s * 0.26f * gainR;
                delayLineR[delayWriteIdx] += s * 0.26f * gainL;
            }
        }

        // 스테레오 피드백 딜레이 에코 읽기 (공간감 극대화)
        int readIdx = (delayWriteIdx + 1) % DELAY_BUF_LEN;
        float echoL = delayLineL[readIdx];
        float echoR = delayLineR[readIdx];
        delayLineL[readIdx] = echoR * 0.38f;
        delayLineR[readIdx] = echoL * 0.38f;
        delayWriteIdx = readIdx;

        mixL = tanhf((mixL + echoL) * 1.15f);
        mixR = tanhf((mixR + echoR) * 1.15f);

        out[f * 2 + 0] = (short)(Clamp(mixL, -1.0f, 1.0f) * 32767.0f);
        out[f * 2 + 1] = (short)(Clamp(mixR, -1.0f, 1.0f) * 32767.0f);
    }
}

void EnsureAudioInitialized() {
    if (audioReady) return;
    InitAudioDevice();
    SetAudioStreamBufferSizeDefault(2048);
    synthStream = LoadAudioStream(SAMPLE_RATE, 16, 2);
    SetAudioStreamCallback(synthStream, AudioStreamCallback);
    PlayAudioStream(synthStream);
    audioReady = true;
}

// ==========================================
// 3. 전역 게임 상태 & 12개 월드 정보
// ==========================================
Font gameFont = { 0 };
ScreenState screen = SCR_MODE_SELECT;
GameMode currentMode = MODE_JOURNEY;
Strictness strictness = STRICT_NORMAL;
int selectedWorld = 0;
bool autoPlay = false;
float audioOffsetMs = 0.0f;

float eqBars[32] = { 0 };

WorldInfo worlds[WORLD_COUNT] = {
    {
        "1-X", "불꽃의 춤", "입문 1", "서정적 일렉트릭 그랜드 & 패드",
        "한 음표씩 직선과 90도 계단을 밟으며 박자와 선율을 맞추는 첫 세계",
        96.0f, 1, 20.0f, 0, false, 0.0f
    },
    {
        "2-X", "산들바람 사원", "입문 2", "맑은 첼레스타 & 앰비언트 패드",
        "사각 회랑의 선율과 첫 소용돌이(회전 방향 반전)",
        102.0f, 2, 168.0f, 1, false, 0.0f
    },
    {
        "3-X", "달빛 언덕", "초급 1", "밤하늘 오르골 세레나데",
        "45도 오르막·내리막 아르페지오와 첫 토끼(가속) 구간",
        108.0f, 3, 205.0f, 1, false, 0.0f
    },
    {
        "4-X", "달팽이 정원", "초급 2", "어쿠스틱 피아노 & 스트링",
        "달팽이(감속)와 토끼(가속)가 곡의 템포와 맞물리는 정원",
        114.0f, 4, 135.0f, 0, false, 0.0f
    },
    {
        "5-X", "부메랑 회랑", "중급 1", "하프 아르페지오 & 첼로",
        "긴 음표(2박) 유턴 반환점에서 되돌아오는 왕복 선율 코스",
        120.0f, 5, 38.0f, 0, false, 0.0f
    },
    {
        "6-X", "별빛 무도회", "중급 2", "몽환적 신스 벨 왈츠",
        "마름모 광장과 삼각 미드스핀(빠른 2연타 멜로디 리듬)",
        126.0f, 6, 250.0f, 2, false, 0.0f
    },
    {
        "7-X", "육각 벌집", "상급 1", "네온 신스웨이브 드라이브",
        "60도·120도 육각형 리프와 변속 비트가 어우러지는 스테이지",
        132.0f, 7, 52.0f, 2, false, 0.0f
    },
    {
        "8-X", "모래시계 성채", "상급 2", "미드나잇 일렉트로닉",
        "대각선이 교차하는 모래시계 구조와 연속 방향 반전",
        138.0f, 8, 285.0f, 2, false, 0.0f
    },
    {
        "9-X", "황혼의 시계탑", "고수 1", "클락워크 오르골 & 심포니",
        "엇박 45도 스윙과 시계추처럼 왕복하는 리듬 코스",
        144.0f, 9, 32.0f, 1, false, 0.0f
    },
    {
        "10-X", "심해의 소용돌이", "고수 2", "딥 앰비언트 신스 펄스",
        "이중 유턴과 미드스핀 멜로디가 몰아치는 심해 스테이지",
        150.0f, 10, 212.0f, 2, false, 0.0f
    },
    {
        "11-X", "번개 폭풍의 회랑", "도전 1", "하이스피드 레이저 신스",
        "지그재그 대각선 질주와 삼각 미드스핀 연타 콤보",
        156.0f, 11, 48.0f, 3, false, 0.0f
    },
    {
        "12-X", "얼음과 불의 피날레", "도전 2", "대성당 파이프 오르간 & 신스",
        "모든 기믹과 웅장한 피날레 악보가 하나로 합쳐지는 최종장",
        164.0f, 12, 328.0f, 3, false, 0.0f
    }
};

std::vector<Tile> tiles;
std::vector<Vector2> previewPoints;
std::deque<TrailPoint> fireTrail;
std::deque<TrailPoint> iceTrail;
std::vector<Particle> particles;
std::vector<Shockwave> shockwaves;
std::vector<BgStar> bgStars;
std::vector<ShootingStar> shootingStars;
std::vector<JudgmentPopup> popups;
std::deque<HitTick> hitTicks;

int currentTile = 0;
int lastCheckpoint = 0;
Vector2 pivotPos = { 0, 0 };
Vector2 endlessBuildPivot = { 0, 0 };
float endlessLastDeg = 0.0f;
int endlessNoteStep = 0;
int endlessTilesCleared = 0;
int bestEndlessScore = 0;

bool isFirePivot = true;
bool cw = true;

float baseBpm = 96.0f;
float currentBpm = 96.0f;
float currentAngle = 0.0f;
float targetDiff = PI;
float traveledAngle = 0.0f;

float cameraBeatPulse = 0.0f;
float screenShake = 0.0f;
float overloadFlash = 0.0f;
float gameOverTimer = 0.0f;
float bgHue = 20.0f;
float worldClock = 0.0f;

int hitCount = 0;
int perfectCount = 0;
int earlyCount = 0;
int lateCount = 0;
int streakCount = 0;
int maxStreakCount = 0;
float accuracySum = 0.0f;
std::string failReason = "";

Camera2D camera = { 0 };

// ==========================================
// 4. 한글 폰트 로더 및 UI 헬퍼
// ==========================================
Font LoadKoreanFont(const char* path, int fontSize) {
    std::string allChars;
    for (int c = 32; c <= 126; c++) allChars.push_back((char)c);
    allChars += "•→";

    for (int i = 0; i < WORLD_COUNT; i++) {
        allChars += worlds[i].code;
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
        "모드 선택 코스 모양 미리보기 월드 시작 난이도 음악 상태 클리어 완료 최고 정확도 "
        "아직 클리어하지 않음 연속 메뉴 빠름! 느림! 완벽! 너무 빠름! 너무 느림! 체크포인트 놓침 준비! 시작! "
        "행성 파괴 퍼펙트 챌린지 완벽하지 않은 타이밍 일찍 입력했습니다 과부하 놓쳤습니다 "
        "생존 기록 부활 처음부터 다시 도전 최종 판정 다음 가속! 감속 전주 구간 최대 콤보";

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
    DrawRectangleRounded({ rec.x + 4, rec.y + 6, rec.width, rec.height }, roundness, 10, Fade(BLACK, 0.44f));
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

float NormalizeDeg(float d) {
    while (d <= -180.0f) d += 360.0f;
    while (d > 180.0f) d -= 360.0f;
    return d;
}

void GetJudgmentWindows(float& purePerfect, float& perfect, float& maxWindow) {
    if (strictness == STRICT_EASY) {
        purePerfect = 0.075f; perfect = 0.150f; maxWindow = 0.240f;
    } else if (strictness == STRICT_STRICT) {
        purePerfect = 0.032f; perfect = 0.070f; maxWindow = 0.125f;
    } else {
        purePerfect = 0.052f; perfect = 0.110f; maxWindow = 0.185f;
    }
}

// ==========================================
// 5. 악보(SongNote) -> 타일 각도 자동 컴파일러
//    (음표의 박자 길이가 곧 타일 꺾임 각도가 되어 노래와 완벽 동기화!)
// ==========================================
std::vector<SongNote> GetWorldSongChart(int worldIdx) {
    // { beatLen, turnDir, melodyMidi, chordRootMidi, drumType, swirl, midspin, checkpoint, speedMult }
    // beatLen: 1.0f = 직진(1박), 0.5f = 90도 꺾임(반박), 0.75f = 45도 꺾임(0.75박), 2.0f = 유턴(2박)
    if (worldIdx == 0) {
        // 1-X 불꽃의 춤: C minor 서정 멜로디 (G4 - Eb4 - F4 - G4 - C5 - Bb4 - G4...)
        return {
            { 1.0f,  0, 67, 48, 1, false, false, false, 1.0f }, // G4 (Cm)
            { 1.0f,  0, 63,  0, 3, false, false, false, 1.0f }, // Eb4
            { 1.0f,  0, 65,  0, 2, false, false, false, 1.0f }, // F4
            { 1.0f,  0, 67,  0, 3, false, false, false, 1.0f }, // G4
            { 0.5f, -1, 72, 44, 1, false, false, false, 1.0f }, // C5 (Ab, 위 90도)
            { 0.5f,  1, 70,  0, 3, false, false, false, 1.0f }, // Bb4 (오른쪽 90도)
            { 0.5f, -1, 67,  0, 2, false, false, false, 1.0f }, // G4 (위 90도)
            { 0.5f,  1, 65,  0, 3, false, false, false, 1.0f }, // F4 (오른쪽 90도)
            { 1.0f,  0, 63, 46, 4, false, false, true,  1.0f }, // Eb4 (Bb, 체크포인트)
            { 1.0f,  0, 62,  0, 3, false, false, false, 1.0f }, // D4
            { 0.5f,  1, 63,  0, 2, false, false, false, 1.0f }, // Eb4 (아래 90도)
            { 0.5f, -1, 65,  0, 3, false, false, false, 1.0f }, // F4 (오른쪽 90도)
            { 0.5f,  1, 67, 43, 1, false, false, false, 1.0f }, // G4 (G, 아래 90도)
            { 0.5f, -1, 70,  0, 3, false, false, false, 1.0f }, // Bb4 (오른쪽 90도)
            { 1.0f,  0, 72, 48, 2, false, false, false, 1.0f }, // C5 (Cm)
            { 1.0f,  0, 74,  0, 3, false, false, false, 1.0f }, // D5
            { 1.0f,  0, 75,  0, 1, false, false, false, 1.0f }, // Eb5
            { 1.0f,  0, 72, 48, 4, false, false, false, 1.0f }  // C5 (피날레)
        };
    }
    else if (worldIdx == 1) {
        // 2-X 산들바람 사원: F minor 아르페지오 & 소용돌이 반전
        return {
            { 1.0f,  0, 72, 53, 1, false, false, false, 1.0f }, // C5 (Fm)
            { 1.0f,  0, 70,  0, 3, false, false, false, 1.0f }, // Bb4
            { 0.5f, -1, 68,  0, 2, false, false, false, 1.0f }, // Ab4
            { 0.5f, -1, 67, 49, 1, false, false, false, 1.0f }, // G4 (Db)
            { 1.0f,  0, 68,  0, 3, false, false, false, 1.0f }, // Ab4
            { 1.0f,  0, 70,  0, 2, false, false, false, 1.0f }, // Bb4
            { 0.5f, -1, 72, 51, 1, false, false, false, 1.0f }, // C5 (Eb)
            { 0.5f, -1, 75,  0, 3, false, false, false, 1.0f }, // Eb5
            { 1.0f,  0, 73, 48, 4, false, false, true,  1.0f }, // Db5 (C, 체크포인트)
            { 1.0f,  0, 72,  0, 1, true,  false, false, 1.0f }, // C5 (소용돌이 반전!)
            { 1.0f,  0, 68, 53, 3, false, false, false, 1.0f }, // Ab4 (Fm)
            { 0.5f,  1, 67,  0, 2, false, false, false, 1.0f }, // G4
            { 0.5f,  1, 65,  0, 3, false, false, false, 1.0f }, // F4
            { 1.0f,  0, 68, 49, 1, false, false, false, 1.0f }, // Ab4 (Db)
            { 1.0f,  0, 72,  0, 2, false, false, false, 1.0f }, // C5
            { 0.5f, -1, 75, 51, 1, false, false, false, 1.0f }, // Eb5 (Eb)
            { 0.5f,  1, 77,  0, 3, false, false, false, 1.0f }, // F5
            { 1.0f,  0, 75,  0, 2, false, false, false, 1.0f }, // Eb5
            { 1.0f,  0, 72, 53, 4, false, false, false, 1.0f }  // C5
        };
    }
    else if (worldIdx == 2) {
        // 3-X 달빛 언덕: A minor 45도 대각선 오르골 & 가속
        return {
            { 1.0f,  0, 69, 45, 1, false, false, false, 1.0f }, // A4 (Am)
            { 1.0f,  0, 72,  0, 3, false, false, false, 1.0f }, // C5
            { 0.75f,-1, 76,  0, 2, false, false, false, 1.0f }, // E5 (45도 위)
            { 0.75f, 0, 74, 41, 1, false, false, false, 1.0f }, // D5 (F)
            { 0.75f, 1, 72,  0, 3, false, false, false, 1.18f }, // C5 (가속!)
            { 0.75f, 1, 69,  0, 2, false, false, false, 1.18f }, // A4 (45도 아래)
            { 0.75f, 0, 72, 48, 1, false, false, false, 1.18f }, // C5 (C)
            { 0.75f,-1, 76,  0, 3, false, false, false, 1.0f }, // E5
            { 1.0f,  0, 79, 43, 4, false, false, true,  1.0f }, // G5 (G, 체크포인트)
            { 0.75f,-1, 76,  0, 3, false, false, false, 1.0f }, // E5
            { 0.75f, 1, 74,  0, 2, false, false, false, 1.0f }, // D5
            { 0.75f,-1, 72, 45, 1, false, false, false, 1.0f }, // C5 (Am)
            { 0.75f, 1, 71,  0, 3, false, false, false, 1.0f }, // B4
            { 1.0f,  0, 69,  0, 1, true,  false, false, 1.0f }, // A4 (소용돌이)
            { 1.0f,  0, 72, 41, 2, false, false, false, 1.0f }, // C5 (F)
            { 0.75f,-1, 76,  0, 3, false, false, false, 1.0f }, // E5
            { 0.75f, 1, 74, 43, 1, false, false, false, 1.0f }, // D5 (G)
            { 0.75f, 1, 71,  0, 2, false, false, false, 1.0f }, // B4
            { 0.75f,-1, 69, 45, 1, false, false, false, 1.0f }, // A4 (Am)
            { 1.0f,  0, 81, 45, 4, false, false, false, 1.0f }  // A5
        };
    }
    else if (worldIdx == 3) {
        // 4-X 달팽이 정원: D minor 감속/가속 나선 회랑
        return {
            { 1.0f,  0, 74, 50, 1, false, false, false, 1.0f }, // D5 (Dm)
            { 1.0f,  0, 70,  0, 3, false, false, false, 1.0f }, // Bb4
            { 1.0f,  0, 72,  0, 2, false, false, false, 0.80f }, // C5 (달팽이 감속)
            { 0.5f,  1, 74, 46, 1, false, false, false, 0.80f }, // D5 (Bb)
            { 0.5f,  1, 77,  0, 3, false, false, false, 0.80f }, // F5
            { 0.5f,  1, 74,  0, 2, false, false, false, 0.80f }, // D5
            { 0.5f,  1, 72, 41, 1, false, false, false, 0.80f }, // C5 (F)
            { 0.5f, -1, 70,  0, 3, false, false, false, 0.80f }, // Bb4
            { 1.0f,  0, 69, 43, 1, true,  false, false, 1.15f }, // A4 (G, 소용돌이 + 토끼 가속!)
            { 0.5f, -1, 70,  0, 3, false, false, false, 1.15f }, // Bb4
            { 0.5f, -1, 72,  0, 2, false, false, false, 1.15f }, // C5
            { 0.5f, -1, 74, 50, 1, false, false, false, 1.15f }, // D5 (Dm)
            { 0.5f, -1, 77,  0, 3, false, false, false, 1.15f }, // F5
            { 0.5f,  1, 76,  0, 2, false, false, false, 1.15f }, // E5
            { 1.0f,  0, 74,  0, 1, false, false, false, 1.0f },  // D5 (원래 속도)
            { 1.0f,  0, 72, 48, 4, false, false, true,  1.0f },  // C5 (C, 체크포인트)
            { 1.0f,  0, 74,  0, 3, false, false, false, 1.0f },  // D5
            { 0.5f, -1, 77, 46, 2, false, false, false, 1.0f },  // F5 (Bb)
            { 0.5f,  1, 76,  0, 3, false, false, false, 1.0f },  // E5
            { 0.5f,  1, 72, 43, 1, false, false, false, 1.0f },  // C5 (G)
            { 0.5f, -1, 73,  0, 2, false, false, false, 1.0f },  // C#5
            { 1.0f,  0, 74, 50, 1, false, false, false, 1.0f },  // D5 (Dm)
            { 1.0f,  0, 74, 50, 4, false, false, false, 1.0f }   // D5
        };
    }
    else if (worldIdx == 4) {
        // 5-X 부메랑 회랑: 2.0f(2박) 긴 음표로 180도 유턴 반환점 표현!
        return {
            { 1.0f,  0, 72, 48, 1, false, false, false, 1.0f }, // C5 (Cm)
            { 1.0f,  0, 75,  0, 3, false, false, false, 1.0f }, // Eb5
            { 1.0f,  0, 79,  0, 2, false, false, false, 1.0f }, // G5
            { 2.0f,  2, 84, 44, 4, false, false, false, 1.0f }, // C6 (Ab, 2박 긴 음표 + 180도 유턴!)
            { 1.0f,  0, 79,  0, 3, false, false, false, 1.0f }, // G5
            { 0.5f, -1, 77, 46, 2, false, false, false, 1.0f }, // F5 (Bb)
            { 0.5f,  1, 75,  0, 3, false, false, false, 1.0f }, // Eb5
            { 1.0f,  0, 74,  0, 1, false, false, false, 1.0f }, // D5
            { 1.0f,  0, 75, 48, 4, false, false, true,  1.0f }, // Eb5 (Cm, 체크포인트)
            { 0.75f, 1, 72,  0, 3, false, false, false, 1.0f }, // C5
            { 0.75f, 0, 75, 41, 1, false, false, false, 1.16f }, // Eb5 (Fm, 가속)
            { 2.0f,  2, 80,  0, 4, false, false, false, 1.16f }, // Ab5 (2박 긴 음표 + 180도 유턴!)
            { 1.0f,  0, 79, 43, 2, false, false, false, 1.0f }, // G5 (G)
            { 0.75f, 1, 77,  0, 3, true,  false, false, 1.0f }, // F5 (소용돌이)
            { 1.0f,  0, 75,  0, 1, false, false, false, 1.0f }, // Eb5
            { 0.5f,  1, 74, 48, 2, false, false, false, 1.0f }, // D5 (Cm)
            { 0.5f, -1, 72,  0, 3, false, false, false, 1.0f }, // C5
            { 0.5f, -1, 74,  0, 2, false, false, false, 1.0f }, // D5
            { 0.5f,  1, 75,  0, 3, false, false, false, 1.0f }, // Eb5
            { 1.0f,  0, 79, 48, 4, false, false, false, 1.0f }  // G5
        };
    }
    else {
        // 6-X ~ 12-X: 월드별 고유 스케일, 미드스핀 2연타, 소용돌이, 변속 악보 생성
        int scaleOffsets[7] = { 0, 2, 3, 5, 7, 8, 10 }; // 자연단음계
        int baseRoots[6] = { 51, 52, 45, 50, 48, 52 };
        int rBase = baseRoots[(worldIdx - 5) % 6];
        int prog[4] = { rBase, rBase - 4, rBase - 7, rBase - 5 };

        std::vector<SongNote> chart;
        int noteIdx = 0;
        for (int bar = 0; bar < 4; bar++) {
            int cRoot = prog[bar];
            // 마디 첫 박
            chart.push_back({ 1.0f, 0, cRoot + 24 + scaleOffsets[noteIdx % 7], cRoot, (bar == 2 ? 4 : 1), false, false, (bar == 2), 1.0f });
            noteIdx++;

            // 45도 대각선 리프
            chart.push_back({ 0.75f, -1, cRoot + 24 + scaleOffsets[(noteIdx + 2) % 7], 0, 3, false, false, false, 1.0f });
            chart.push_back({ 0.75f,  1, cRoot + 24 + scaleOffsets[(noteIdx + 4) % 7], 0, 2, (bar == 1), false, false, (bar == 1 ? 1.15f : 1.0f) });
            noteIdx += 2;

            // 삼각 미드스핀 2연타 (빠른 멜로디 꾸밈음!)
            chart.push_back({ 0.5f,  1, cRoot + 31, 0, 1, false, true,  false, (bar == 1 ? 1.15f : 1.0f) });
            chart.push_back({ 0.5f, -1, cRoot + 29, 0, 3, false, false, false, 1.0f });

            // 마디 마무리 (홀수 마디 끝에서 부메랑 유턴 또는 90도 꺾임)
            if (bar == 1 && worldIdx >= 8) {
                chart.push_back({ 2.0f, 2, cRoot + 36, 0, 4, false, false, false, 1.0f });
            } else {
                chart.push_back({ 0.5f, -1, cRoot + 27, 0, 2, (bar == 3), false, false, 1.0f });
                chart.push_back({ 0.5f,  1, cRoot + 24, 0, 3, false, false, false, 1.0f });
            }
        }
        chart.push_back({ 1.0f, 0, rBase + 36, rBase, 4, false, false, false, 1.0f });
        return chart;
    }
}

// 악보의 beatLen으로부터 정확한 타일 진행 각도(enterAngleDeg)를 계산하여 길 생성!
void BuildTilesFromSongChart(const std::vector<SongNote>& songNotes, bool addLeadIn) {
    tiles.clear();

    // 원작 ADOFAI와 동일한 전주 4박자 타일(Lead-in)
    std::vector<SongNote> fullChart;
    if (addLeadIn) {
        int introRoot = (!songNotes.empty() && songNotes[0].chordRootMidi > 0) ? songNotes[0].chordRootMidi : 48;
        fullChart.push_back({ 1.0f, 0, introRoot + 12, introRoot, 1, false, false, false, 1.0f });
        fullChart.push_back({ 1.0f, 0, introRoot + 19, 0,         3, false, false, false, 1.0f });
        fullChart.push_back({ 1.0f, 0, introRoot + 24, 0,         2, false, false, false, 1.0f });
        fullChart.push_back({ 1.0f, 0, introRoot + 19, 0,         3, false, false, false, 1.0f });
    }
    fullChart.insert(fullChart.end(), songNotes.begin(), songNotes.end());

    Vector2 curPos = { 0.0f, 0.0f };
    float curHeadingDeg = 0.0f;

    tiles.push_back({ curPos, 0.0f, 0.0f, 1.0f, -1, 0, 1, addLeadIn, false, false, false, 1.0f, true, 0.0f });

    Vector2 buildPivot = curPos;
    bool simCw = true;

    for (size_t i = 0; i < fullChart.size(); i++) {
        const SongNote& note = fullChart[i];

        // 1.0f = 직진(0도 변화), 0.5f = 90도 꺾임, 0.75f = 45도 꺾임, 2.0f = 180도 유턴!
        float deltaDeg = 0.0f;
        if (note.turnDir == 2) {
            deltaDeg = 180.0f; // 2박 유턴
        } else if (note.turnDir != 0) {
            float bend = (1.0f - note.beatLen) * 180.0f;
            deltaDeg = note.turnDir * bend;
        }

        curHeadingDeg = NormalizeDeg(curHeadingDeg + deltaDeg);
        float rad = curHeadingDeg * DEG2RAD;
        Vector2 nextPos = {
            buildPivot.x + cosf(rad) * TILE_STEP,
            buildPivot.y + sinf(rad) * TILE_STEP
        };

        bool isLead = addLeadIn && (i < 3);
        tiles.push_back({
            nextPos,
            curHeadingDeg,
            curHeadingDeg,
            note.beatLen,
            note.melodyMidi,
            note.chordRootMidi,
            note.drumType,
            isLead,
            note.swirl,
            note.midspin,
            note.checkpoint,
            note.speedMult,
            false,
            0.0f
        });

        if (note.swirl) simCw = !simCw;
        if (!note.midspin) {
            buildPivot = nextPos;
        }
    }

    // 각 타일의 진출 방향(exitAngleDeg) 업데이트
    for (size_t i = 0; i + 1 < tiles.size(); i++) {
        tiles[i].exitAngleDeg = tiles[i + 1].enterAngleDeg;
    }
    if (!tiles.empty()) {
        tiles.back().exitAngleDeg = tiles.back().enterAngleDeg;
    }
}

void UpdateWorldPreview(int worldIdx) {
    std::vector<SongNote> chart = GetWorldSongChart(worldIdx);
    previewPoints.clear();
    Vector2 p = { 0.0f, 0.0f };
    previewPoints.push_back(p);
    Vector2 piv = p;
    float deg = 0.0f;

    float minX = 0, maxX = 0, minY = 0, maxY = 0;
    for (const auto& note : chart) {
        float delta = 0.0f;
        if (note.turnDir == 2) delta = 180.0f;
        else if (note.turnDir != 0) delta = note.turnDir * ((1.0f - note.beatLen) * 180.0f);
        deg = NormalizeDeg(deg + delta);

        float rad = deg * DEG2RAD;
        Vector2 np = { piv.x + cosf(rad), piv.y + sinf(rad) };
        previewPoints.push_back(np);
        if (!note.midspin) piv = np;
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
    const int scaleNotes[8] = { 60, 63, 65, 67, 70, 72, 75, 79 };
    const int roots[4] = { 48, 44, 46, 43 };
    const float bends[5] = { 0.0f, -90.0f, 90.0f, -45.0f, 45.0f };

    for (int i = 0; i < count; i++) {
        float delta = bends[GetRandomValue(0, 4)];
        endlessLastDeg = NormalizeDeg(endlessLastDeg + delta);
        float beatLen = 1.0f - fabsf(delta) / 180.0f;

        bool swirl = (GetRandomValue(0, 8) == 0);
        float spMult = 1.0f;
        int rSp = GetRandomValue(0, 15);
        if (rSp == 0) spMult = 1.16f;
        else if (rSp == 1) spMult = 0.86f;

        int chord = (endlessNoteStep % 8 == 0) ? roots[(endlessNoteStep / 8) % 4] : 0;
        int mel   = scaleNotes[(endlessNoteStep + GetRandomValue(0, 2)) % 8];
        int drum  = (endlessNoteStep % 4 == 0) ? 1 : (endlessNoteStep % 4 == 2 ? 2 : 3);
        endlessNoteStep++;

        if (!tiles.empty()) {
            tiles.back().exitAngleDeg = endlessLastDeg;
        }

        float rad = endlessLastDeg * DEG2RAD;
        Vector2 nextPos = {
            endlessBuildPivot.x + cosf(rad) * TILE_STEP,
            endlessBuildPivot.y + sinf(rad) * TILE_STEP
        };
        tiles.push_back({
            nextPos, endlessLastDeg, endlessLastDeg, beatLen,
            mel, chord, drum, false, swirl, false, false, spMult, false, 0.0f
        });
        endlessBuildPivot = nextPos;
    }
}

// ==========================================
// 6. 배경 환경 & 원작 ADOFAI 입체 석판 타일 렌더러
// ==========================================
void InitSpaceEnvironment() {
    bgStars.clear();
    for (int i = 0; i < 95; i++) {
        bgStars.push_back({
            { (float)GetRandomValue(0, SCREEN_WIDTH), (float)GetRandomValue(0, SCREEN_HEIGHT) },
            (float)GetRandomValue(15, 65) / 100.0f,
            (float)GetRandomValue(12, 32) / 10.0f,
            (float)GetRandomValue(0, 628) / 100.0f
        });
    }
}

void DrawRichBackground(float hue, float beatPulse, float dt) {
    Color topCol = ColorFromHSV(hue, 0.48f, 0.065f + beatPulse * 0.25f);
    Color botCol = ColorFromHSV(fmodf(hue + 24.0f, 360.0f), 0.55f, 0.12f + beatPulse * 0.45f);
    DrawRectangleGradientV(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, topCol, botCol);

    Color nebulaCol = ColorFromHSV(hue, 0.65f, 0.85f);
    DrawCircleGradient(SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2, 460.0f + beatPulse * 350.0f,
                       Fade(nebulaCol, 0.13f + beatPulse * 0.18f), BLANK);

    for (auto& st : bgStars) {
        st.phase += (1.5f + st.depth * 2.0f) * dt;
        float px = fmodf(st.pos.x - camera.target.x * st.depth * 0.18f + SCREEN_WIDTH * 10.0f, (float)SCREEN_WIDTH);
        float py = fmodf(st.pos.y - camera.target.y * st.depth * 0.18f + SCREEN_HEIGHT * 10.0f, (float)SCREEN_HEIGHT);
        float alpha = 0.25f + 0.35f * sinf(st.phase);
        DrawCircleV({ px, py }, st.size, Fade(WHITE, alpha));
    }

    if (GetRandomValue(0, 140) == 0 && shootingStars.size() < 3) {
        shootingStars.push_back({
            { (float)GetRandomValue(200, SCREEN_WIDTH + 100), (float)GetRandomValue(-50, 220) },
            { -650.0f, 340.0f },
            0.48f, 0.48f
        });
    }
    for (int i = (int)shootingStars.size() - 1; i >= 0; i--) {
        shootingStars[i].pos = Vector2Add(shootingStars[i].pos, Vector2Scale(shootingStars[i].vel, dt));
        shootingStars[i].life -= dt;
        if (shootingStars[i].life <= 0.0f) {
            shootingStars.erase(shootingStars.begin() + i);
        } else {
            float r = shootingStars[i].life / shootingStars[i].maxLife;
            Vector2 tail = Vector2Subtract(shootingStars[i].pos, Vector2Scale(shootingStars[i].vel, 0.08f));
            DrawLineEx(shootingStars[i].pos, tail, 2.5f, Fade(WHITE, r * 0.8f));
            DrawCircleV(shootingStars[i].pos, 3.0f, Fade(SKYBLUE, r));
        }
    }

    Vector2 center = { SCREEN_WIDTH * 0.5f, SCREEN_HEIGHT * 0.5f };
    float rOuter = 290.0f + beatPulse * 240.0f;
    DrawPolyLinesEx(center, 8, rOuter, worldClock * 8.0f, 1.5f, Fade(nebulaCol, 0.10f));
    DrawPolyLinesEx(center, 6, rOuter * 0.72f, -worldClock * 12.0f, 1.5f, Fade(WHITE, 0.06f));

    if (screen == SCR_PLAYING || screen == SCR_CLEAR) {
        float barWidth = (float)SCREEN_WIDTH / 32.0f;
        for (int b = 0; b < 32; b++) {
            float h = eqBars[b] * 95.0f;
            if (h > 1.0f) {
                DrawRectangleRec({ b * barWidth + 3.0f, SCREEN_HEIGHT - h, barWidth - 6.0f, h },
                                 Fade(nebulaCol, 0.16f));
            }
        }
    }
}

void SpawnHitParticles(Vector2 pos, Color col, int count) {
    for (int i = 0; i < count; i++) {
        float ang = (float)GetRandomValue(0, 360) * DEG2RAD;
        float spd = (float)GetRandomValue(90, 320);
        float life = (float)GetRandomValue(25, 48) / 100.0f;
        particles.push_back({
            pos,
            { cosf(ang) * spd, sinf(ang) * spd },
            col,
            (float)GetRandomValue(3, 6),
            life,
            life,
            (i % 2 == 0)
        });
    }
}

void SpawnShockwave(Vector2 pos, Color col, float maxRad = 82.0f, float thick = 3.5f) {
    shockwaves.push_back({ pos, col, 16.0f, maxRad, 0.88f, thick });
}

void AddPopup(const std::string& msg, Vector2 pos, Color col, const std::string& sub = "") {
    popups.push_back({ msg, sub, { pos.x, pos.y - 46.0f }, col, 0.75f });
}

void SetupOrbitForTile(int index, Vector2 prevPivotPos) {
    if (tiles[index].speedMult != 1.0f) {
        float prevBpm = currentBpm;
        currentBpm = baseBpm * tiles[index].speedMult;
        if (fabsf(currentBpm - prevBpm) > 1.0f) {
            if (tiles[index].speedMult > 1.0f) {
                AddPopup("가속!", tiles[index].pos, ORANGE, TextFormat("%.0f BPM", currentBpm));
                SpawnShockwave(tiles[index].pos, ORANGE, 98.0f, 4.0f);
                TriggerVoice(6, 780.0f, 0.18f, 0.35f);
            } else {
                AddPopup("감속", tiles[index].pos, SKYBLUE, TextFormat("%.0f BPM", currentBpm));
                SpawnShockwave(tiles[index].pos, SKYBLUE, 98.0f, 4.0f);
                TriggerVoice(6, 330.0f, 0.22f, 0.35f);
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
            SpawnShockwave(tiles[index].pos, MAGENTA, 92.0f, 4.0f);
            TriggerVoice(6, 620.0f, 0.16f, 0.32f);
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
        SpawnShockwave(pivotPos, MAGENTA, 92.0f, 4.0f);
        TriggerVoice(6, 620.0f, 0.16f, 0.32f);
    }

    float diff = cw ? NormalizeAngle(endAng - startAng) : NormalizeAngle(startAng - endAng);
    if (diff < 0.05f) diff += 2.0f * PI;

    currentAngle = startAng;
    targetDiff = diff;
    traveledAngle = 0.0f;
}

void DrawSpeedChevron(Vector2 pos, float dirDeg, bool fast, float timeNow, float fog) {
    float rad = dirDeg * DEG2RAD;
    Vector2 fwd = { cosf(rad), sinf(rad) };
    Vector2 right = { -fwd.y, fwd.x };
    Color col = fast ? Color{ 255, 165, 45, 255 } : Color{ 75, 225, 255, 255 };

    int count = fast ? 2 : 1;
    for (int k = 0; k < count; k++) {
        float shift = (fast ? (-6.0f + k * 10.5f) : 0.0f) + 2.0f * sinf(timeNow * 7.5f);
        Vector2 c = Vector2Add(pos, Vector2Scale(fwd, shift));
        Vector2 tip = Vector2Add(c, Vector2Scale(fwd, 9.0f));
        Vector2 w1  = Vector2Add(Vector2Subtract(c, Vector2Scale(fwd, 6.0f)), Vector2Scale(right, 10.5f));
        Vector2 w2  = Vector2Subtract(Vector2Subtract(c, Vector2Scale(fwd, 6.0f)), Vector2Scale(right, 10.5f));
        DrawLineEx(w1, tip, 3.4f, Fade(col, fog));
        DrawLineEx(w2, tip, 3.4f, Fade(col, fog));
    }
}

void DrawSingleAdofaiTile(size_t i, float timeNow, int remainingVisits, bool isFirstStackHolder, bool isUTurnTile, float fog) {
    float scale = 1.0f + tiles[i].hitAnim * 0.24f;
    bool isCurrent = ((int)i == currentTile);
    bool isNextTarget = ((int)i == currentTile + 1);
    bool isGoal = (currentMode != MODE_ENDLESS && i == tiles.size() - 1);

    float inDeg  = tiles[i].enterAngleDeg;
    float outDeg = tiles[i].exitAngleDeg;
    float halfLen = TILE_STEP * 0.5f;

    Color borderCol = isCurrent    ? Color{ 245, 250, 255, 255 } :
                   isNextTarget ? (isFirePivot ? Color{ 85, 228, 255, 255 } : Color{ 255, 150, 65, 255 }) :
                   tiles[i].cleared ? Color{ 126, 174, 238, 255 } : Color{ 78, 98, 144, 255 };

    Color slabCol   = isCurrent    ? Color{ 58, 82, 124, 255 } :
                   isNextTarget ? Color{ 44, 66, 104, 255 } :
                   tiles[i].cleared ? Color{ 38, 56, 90, 255 } : Color{ 23, 30, 48, 255 };

    Color bevelCol  = isCurrent    ? Color{ 98, 134, 195, 255 } :
                   isNextTarget ? Color{ 76, 112, 172, 255 } :
                   tiles[i].cleared ? Color{ 62, 88, 138, 255 } : Color{ 38, 49, 76, 255 };

    if (tiles[i].isLeadIn) {
        borderCol = tiles[i].cleared ? Color{ 95, 165, 215, 220 } : Color{ 65, 105, 145, 210 };
        slabCol   = Color{ 20, 32, 52, 240 };
    }
    if (isGoal) {
        borderCol = GOLD;
        slabCol   = Color{ 68, 56, 24, 255 };
        bevelCol  = Color{ 125, 102, 42, 255 };
    }

    if (tiles[i].midspin) {
        DrawPoly({ tiles[i].pos.x, tiles[i].pos.y + 6.0f }, 3, 30.0f * scale, inDeg, Fade(BLACK, 0.42f * fog));
        DrawPoly(tiles[i].pos, 3, 30.0f * scale, inDeg, Fade(MAGENTA, fog));
        DrawPoly(tiles[i].pos, 3, 21.0f * scale, inDeg, Fade(Color{ 30, 22, 48, 255 }, fog));
        DrawPoly(tiles[i].pos, 3, 11.0f * scale, inDeg, Fade(WHITE, 0.55f * fog));
    } else {
        float wOut   = TILE_WIDTH * scale;
        float wSlab  = (TILE_WIDTH - 7.0f) * scale;
        float wBevel = (TILE_WIDTH - 17.0f) * scale;

        if (remainingVisits >= 2 && isFirstStackHolder) {
            Vector2 st = { tiles[i].pos.x - 5.0f, tiles[i].pos.y + 8.0f };
            DrawRectanglePro({ st.x, st.y, wOut, wOut }, { wOut * 0.5f, wOut * 0.5f }, inDeg, Fade(GOLD, 0.78f * fog));
        }

        Vector2 shPos = { tiles[i].pos.x, tiles[i].pos.y + 7.5f };
        DrawRectanglePro({ shPos.x, shPos.y, halfLen, wOut }, { halfLen, wOut * 0.5f }, inDeg, Fade(BLACK, 0.42f * fog));
        if (!isGoal && !isUTurnTile) {
            DrawRectanglePro({ shPos.x, shPos.y, halfLen, wOut }, { 0.0f, wOut * 0.5f }, outDeg, Fade(BLACK, 0.42f * fog));
        }
        DrawCircleV(shPos, wOut * 0.5f, Fade(BLACK, 0.42f * fog));

        DrawRectanglePro({ tiles[i].pos.x, tiles[i].pos.y, halfLen, wOut }, { halfLen, wOut * 0.5f }, inDeg, Fade(borderCol, fog));
        if (!isGoal && !isUTurnTile) {
            DrawRectanglePro({ tiles[i].pos.x, tiles[i].pos.y, halfLen, wOut }, { 0.0f, wOut * 0.5f }, outDeg, Fade(borderCol, fog));
        }
        DrawRectanglePro({ tiles[i].pos.x, tiles[i].pos.y, wOut, wOut }, { wOut * 0.5f, wOut * 0.5f }, inDeg, Fade(borderCol, fog));
        DrawCircleV(tiles[i].pos, wOut * 0.5f, Fade(borderCol, fog));

        DrawRectanglePro({ tiles[i].pos.x, tiles[i].pos.y, halfLen, wSlab }, { halfLen, wSlab * 0.5f }, inDeg, Fade(slabCol, fog));
        if (!isGoal && !isUTurnTile) {
            DrawRectanglePro({ tiles[i].pos.x, tiles[i].pos.y, halfLen, wSlab }, { 0.0f, wSlab * 0.5f }, outDeg, Fade(slabCol, fog));
        }
        DrawRectanglePro({ tiles[i].pos.x, tiles[i].pos.y, wSlab, wSlab }, { wSlab * 0.5f, wSlab * 0.5f }, inDeg, Fade(slabCol, fog));
        DrawCircleV(tiles[i].pos, wSlab * 0.5f, Fade(slabCol, fog));

        DrawRectanglePro({ tiles[i].pos.x, tiles[i].pos.y, halfLen * 0.85f, wBevel }, { halfLen * 0.85f, wBevel * 0.5f }, inDeg, Fade(bevelCol, 0.48f * fog));
        if (!isGoal && !isUTurnTile) {
            DrawRectanglePro({ tiles[i].pos.x, tiles[i].pos.y, halfLen * 0.85f, wBevel }, { 0.0f, wBevel * 0.5f }, outDeg, Fade(bevelCol, 0.48f * fog));
        }
        DrawRectanglePro({ tiles[i].pos.x, tiles[i].pos.y, wBevel, wBevel }, { wBevel * 0.5f, wBevel * 0.5f }, inDeg, Fade(bevelCol, 0.65f * fog));

        float radIn = inDeg * DEG2RAD;
        Vector2 edgeStart = { tiles[i].pos.x - cosf(radIn) * halfLen, tiles[i].pos.y - sinf(radIn) * halfLen };
        Vector2 perpIn = { -sinf(radIn), cosf(radIn) };
        DrawLineEx(Vector2Add(edgeStart, Vector2Scale(perpIn, wSlab * 0.48f)),
                   Vector2Subtract(edgeStart, Vector2Scale(perpIn, wSlab * 0.48f)),
                   2.2f, Fade(BLACK, 0.52f * fog));

        DrawCircleLinesV(tiles[i].pos, 8.5f * scale, Fade(borderCol, 0.45f * fog));
        DrawCircleV(tiles[i].pos, 3.5f * scale, Fade(borderCol, 0.85f * fog));
    }

    if (isNextTarget) {
        Color targetCol = isFirePivot ? Color{ 75, 228, 255, 255 } : Color{ 255, 148, 60, 255 };
        float pulse = 22.0f + 4.5f * sinf(timeNow * 11.0f);
        DrawCircleV(tiles[i].pos, 14.0f, Fade(targetCol, 0.25f));
        DrawCircleLinesV(tiles[i].pos, pulse, Fade(targetCol, 0.92f));

        if (!isGoal) {
            float outRad = outDeg * DEG2RAD;
            Vector2 fwd = { cosf(outRad), sinf(outRad) };
            Vector2 right = { -fwd.y, fwd.x };
            Vector2 base = Vector2Add(tiles[i].pos, Vector2Scale(fwd, 16.0f));
            Vector2 tip  = Vector2Add(tiles[i].pos, Vector2Scale(fwd, 34.0f));
            Vector2 w1   = Vector2Add(Vector2Subtract(tip, Vector2Scale(fwd, 8.0f)), Vector2Scale(right, 7.0f));
            Vector2 w2   = Vector2Subtract(Vector2Subtract(tip, Vector2Scale(fwd, 8.0f)), Vector2Scale(right, 7.0f));
            DrawLineEx(base, tip, 3.2f, WHITE);
            DrawTriangle(tip, w2, w1, WHITE);
        }
    }

    if (isGoal) {
        DrawRing(tiles[i].pos, 13.0f, 18.0f, timeNow * 140.0f, timeNow * 140.0f + 250.0f, 24, Fade(GOLD, fog));
        DrawCircleV(tiles[i].pos, 7.5f + 2.0f * sinf(timeNow * 6.0f), Fade(WHITE, fog));
    }

    if (tiles[i].swirl) {
        float rot = timeNow * 230.0f;
        DrawRing(tiles[i].pos, 10.0f, 14.5f, rot, rot + 145.0f, 16, Fade(Color{ 245, 95, 255, 255 }, fog));
        DrawRing(tiles[i].pos, 10.0f, 14.5f, rot + 180.0f, rot + 325.0f, 16, Fade(Color{ 245, 95, 255, 255 }, fog));
    }

    if (tiles[i].speedMult > 1.01f) {
        DrawSpeedChevron(tiles[i].pos, outDeg, true, timeNow, fog);
    } else if (tiles[i].speedMult < 0.99f) {
        DrawSpeedChevron(tiles[i].pos, outDeg + 180.0f, false, timeNow, fog);
    }

    if (isUTurnTile && !tiles[i].cleared) {
        float rot = timeNow * 105.0f;
        DrawRing(tiles[i].pos, 13.5f, 17.0f, rot, rot + 225.0f, 18, Fade(ORANGE, fog));
    }

    if (tiles[i].checkpoint && currentMode == MODE_JOURNEY) {
        DrawCircleLinesV(tiles[i].pos, 23.0f, Fade(GREEN, fog));
    }

    if (remainingVisits >= 2 && (isFirstStackHolder || isNextTarget)) {
        Rectangle badge = { tiles[i].pos.x + 14.0f, tiles[i].pos.y - 34.0f, 36.0f, 21.0f };
        DrawRectangleRounded(badge, 0.45f, 6, GOLD);
        DrawRectangleRoundedLinesEx(badge, 0.45f, 6, 1.5f, BLACK);
        DrawTextCentered(TextFormat("x%d", remainingVisits), badge.x + 18.0f, badge.y + 2.0f, 14.5f, BLACK, false);
    }
}

void DrawAdofaiTrack(float timeNow) {
    if (tiles.empty()) return;

    std::vector<Vector2> segStart(tiles.size());
    std::vector<Vector2> segEnd(tiles.size());
    Vector2 renderPivot = tiles[0].pos;
    for (size_t i = 0; i + 1 < tiles.size(); i++) {
        segStart[i] = renderPivot;
        segEnd[i]   = tiles[i + 1].pos;
        if (!tiles[i + 1].midspin) {
            renderPivot = tiles[i + 1].pos;
        }
    }

    std::vector<int> remainingVisits(tiles.size(), 1);
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
        remainingVisits[i] = count;
        firstStackHolder[i] = !seenEarlierRemaining;

        if (i > 0 && i + 1 < tiles.size() && !tiles[i].midspin && !tiles[i + 1].midspin) {
            Vector2 vIn  = Vector2Normalize(Vector2Subtract(segEnd[i - 1], segStart[i - 1]));
            Vector2 vOut = Vector2Normalize(Vector2Subtract(segEnd[i], segStart[i]));
            if (Vector2DotProduct(vIn, vOut) < -0.90f) {
                isUTurn[i] = true;
            }
        }
    }

    for (size_t i = 0; i < tiles.size(); i++) {
        if ((int)i >= currentTile) continue;
        int pastDist = currentTile - (int)i;
        if (pastDist > 16) continue;
        float fog = Clamp(0.65f - pastDist * 0.035f, 0.12f, 0.65f);
        DrawSingleAdofaiTile(i, timeNow, remainingVisits[i], firstStackHolder[i], isUTurn[i], fog);
    }

    int maxLookAhead = std::min((int)tiles.size() - 1, currentTile + 14);
    for (int i = maxLookAhead; i >= currentTile + 2; i--) {
        int dist = i - currentTile;
        float fog = (dist <= 4) ? 1.0f : Clamp(1.0f - (dist - 4) * 0.085f, 0.15f, 1.0f);
        DrawSingleAdofaiTile((size_t)i, timeNow, remainingVisits[i], firstStackHolder[i], isUTurn[i], fog);
    }

    if (currentTile >= 0 && currentTile < (int)tiles.size()) {
        DrawSingleAdofaiTile((size_t)currentTile, timeNow, remainingVisits[currentTile], firstStackHolder[currentTile], isUTurn[currentTile], 1.0f);
    }
    if (currentTile + 1 >= 0 && currentTile + 1 < (int)tiles.size()) {
        DrawSingleAdofaiTile((size_t)(currentTile + 1), timeNow, remainingVisits[currentTile + 1], firstStackHolder[currentTile + 1], isUTurn[currentTile + 1], 1.0f);
    }
}

void TriggerGameOver(const std::string& reason, Vector2 effectPos) {
    TriggerVoice(6, 145.0f, 0.45f, 0.55f);
    screenShake = 16.0f;
    overloadFlash = 0.80f;
    gameOverTimer = 0.55f;
    failReason = reason;
    SpawnHitParticles(effectPos, RED, 36);
    SpawnShockwave(effectPos, RED, 135.0f, 4.5f);
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
        tiles.push_back({ startPos, 0.0f, 0.0f, 1.0f, 60, 48, 1, true, false, false, false, 1.0f, true, 0.0f });
        endlessBuildPivot = startPos;
        endlessLastDeg = 0.0f;
        endlessNoteStep = 0;
        endlessTilesCleared = 0;
        baseBpm = 118.0f;
        currentBpm = baseBpm;
        bgHue = 185.0f;

        const int introNotes[4] = { 60, 67, 72, 67 };
        for (int k = 0; k < 4; k++) {
            Vector2 np = { endlessBuildPivot.x + TILE_STEP, endlessBuildPivot.y };
            tiles.push_back({ np, 0.0f, 0.0f, 1.0f, introNotes[k], (k == 0 ? 48 : 0), (k % 2 == 0 ? 1 : 3), (k < 3), false, false, false, 1.0f, false, 0.0f });
            endlessBuildPivot = np;
        }
        AppendEndlessTiles(40);
        startFromTile = 0;
    } else {
        BuildTilesFromSongChart(GetWorldSongChart(selectedWorld), true);
        baseBpm = worlds[selectedWorld].bpm;
        currentBpm = baseBpm;
        bgHue = worlds[selectedWorld].hue;
    }

    if (startFromTile < 0) startFromTile = 0;
    if (startFromTile >= (int)tiles.size() - 1) startFromTile = 0;

    currentTile = startFromTile;
    lastCheckpoint = startFromTile;
    isFirePivot = (startFromTile % 2 == 0);
    cw = true;
    hitCount = 0;
    perfectCount = 0;
    earlyCount = 0;
    lateCount = 0;
    streakCount = 0;
    maxStreakCount = 0;
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

    // 출발 타일의 첫 화음과 음표 즉시 연주!
    int preset = (currentMode == MODE_ENDLESS) ? 2 : worlds[selectedWorld].synthPreset;
    PlayNoteForTile(tiles[startFromTile], preset, true);
    cameraBeatPulse = 0.045f;
    for (int b = 0; b < 32; b++) eqBars[b] = (float)GetRandomValue(35, 95) / 100.0f;

    camera.target = pivotPos;
    camera.offset = { SCREEN_WIDTH / 2.0f, SCREEN_HEIGHT / 2.0f };
    camera.zoom = 1.08f;
    screen = SCR_PLAYING;
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

    // 바로 이 타일에 배정된 멜로디 음정 + 화음 + 드럼을 정박에 연주!
    int preset = (currentMode == MODE_ENDLESS) ? 2 : worlds[selectedWorld].synthPreset;
    PlayNoteForTile(tiles[currentTile], preset, true);

    for (int b = 0; b < 32; b++) {
        float boost = (b < 8) ? 0.85f : ((b % 4 == (tiles[currentTile].melodyMidi % 4)) ? 0.90f : 0.45f);
        eqBars[b] = fmaxf(eqBars[b], boost);
    }

    if (!wasMissInZen && tiles[currentTile].checkpoint && currentMode == MODE_JOURNEY) {
        lastCheckpoint = currentTile;
        AddPopup("체크포인트!", hitTilePos, GREEN);
        SpawnShockwave(hitTilePos, GREEN, 108.0f, 4.0f);
    }

    if (!tiles[currentTile].midspin) {
        isFirePivot = !isFirePivot;
    }

    if (currentMode == MODE_ENDLESS) {
        if (!tiles[currentTile].isLeadIn) endlessTilesCleared++;
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
            TriggerVoice(0, MidiToFreq(84), 1.4f, 0.42f, 0.0f);
            SpawnShockwave(hitTilePos, GOLD, 165.0f, 5.0f);
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
    worldClock += dt;

    if (overloadFlash > 0.0f) overloadFlash = fmaxf(0.0f, overloadFlash - 2.2f * dt);
    for (int b = 0; b < 32; b++) {
        eqBars[b] = fmaxf(0.04f, eqBars[b] - 1.9f * dt);
    }

    Vector2 mp = GetMousePosition();
    bool clicked = IsMouseButtonPressed(MOUSE_LEFT_BUTTON);

    if (screen == SCR_MODE_SELECT) {
        float modeHues[4] = { 20.0f, 185.0f, 145.0f, 345.0f };
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
            Rectangle prevBtn  = { 65.0f, 300.0f, 68.0f, 90.0f };
            Rectangle nextBtn  = { 1147.0f, 300.0f, 68.0f, 90.0f };
            Rectangle playBtn  = { 635.0f, 440.0f, 430.0f, 64.0f };

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
                int col = w % 6;
                int row = w / 6;
                Rectangle pill = { 165.0f + col * 160.0f, 575.0f + row * 52.0f, 148.0f, 42.0f };
                if (CheckCollisionPointRec(mp, pill)) {
                    selectedWorld = w;
                    UpdateWorldPreview(selectedWorld);
                }
            }
        }
    }
    else if (screen == SCR_PLAYING) {
        if (IsKeyPressed(KEY_TAB)) autoPlay = !autoPlay;
        if (IsKeyPressed(KEY_ESCAPE)) screen = (currentMode == MODE_ENDLESS) ? SCR_MODE_SELECT : SCR_WORLD_SELECT;
        if (IsKeyPressed(KEY_R)) StartGame(0);

        if (clicked && CheckCollisionPointRec(mp, { 1130.0f, 18.0f, 120.0f, 38.0f })) {
            screen = (currentMode == MODE_ENDLESS) ? SCR_MODE_SELECT : SCR_WORLD_SELECT;
        }

        float angularSpeed = PI * (currentBpm / 60.0f);
        float step = angularSpeed * dt;
        traveledAngle += step;
        currentAngle += (cw ? step : -step);

        float offsetRad = (audioOffsetMs / 1000.0f) * angularSpeed;
        float errorAngle = (traveledAngle - offsetRad) - targetDiff;
        float errorSec = errorAngle / angularSpeed;

        float pureWin, perfWin, maxWin;
        GetJudgmentWindows(pureWin, perfWin, maxWin);

        bool nextIsLeadIn = (currentTile + 1 < (int)tiles.size()) && tiles[currentTile + 1].isLeadIn;

        if (nextIsLeadIn) {
            if (errorAngle >= 0.0f) {
                Vector2 hitTilePos = tiles[currentTile + 1].pos;
                int remainLead = 3 - currentTile;
                if (remainLead > 0) AddPopup(TextFormat("%d", remainLead), hitTilePos, SKYBLUE);
                else AddPopup("시작!", hitTilePos, GOLD);
                SpawnShockwave(hitTilePos, SKYBLUE, 62.0f, 3.0f);
                cameraBeatPulse = 0.038f;
                AdvanceTileAfterHit(hitTilePos, false);
            }
        } else {
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
                        earlyCount++;
                        if (fabsf(errorSec) <= perfWin) { jText = "빠름!"; jCol = { 120, 245, 95, 255 }; accVal = 0.85f; }
                        else { jText = "너무 빠름!"; jCol = GOLD; accVal = 0.50f; isGoodHit = false; }
                    } else {
                        lateCount++;
                        if (fabsf(errorSec) <= perfWin) { jText = "느림!"; jCol = { 120, 245, 95, 255 }; accVal = 0.85f; }
                        else { jText = "너무 느림!"; jCol = GOLD; accVal = 0.50f; isGoodHit = false; }
                    }

                    if (currentMode == MODE_PERFECTIONIST && !isGoodHit) {
                        TriggerGameOver("퍼펙트 챌린지: 완벽하지 않은 타이밍!", tiles[currentTile + 1].pos);
                    } else {
                        hitCount++;
                        streakCount++;
                        if (streakCount > maxStreakCount) maxStreakCount = streakCount;
                        accuracySum += accVal;
                        hitTicks.push_back({ autoPlay ? 0.0f : errMs, jCol, 1.0f });
                        if (hitTicks.size() > 26) hitTicks.pop_front();

                        Vector2 hitTilePos = tiles[currentTile + 1].pos;
                        AddPopup(jText, hitTilePos, jCol);
                        SpawnHitParticles(hitTilePos, isFirePivot ? SKYBLUE : ORANGE, 20);
                        SpawnShockwave(hitTilePos, jCol, 76.0f, 3.8f);

                        cameraBeatPulse = 0.048f;
                        screenShake = 3.2f;
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

    if (!tiles.empty() && (screen == SCR_PLAYING || screen == SCR_GAMEOVER || screen == SCR_CLEAR)) {
        Vector2 lookAhead = (currentTile + 1 < (int)tiles.size())
            ? Vector2Lerp(pivotPos, tiles[currentTile + 1].pos, 0.28f)
            : pivotPos;
        camera.target = Vector2Lerp(camera.target, lookAhead, 1.0f - expf(-8.5f * dt));
        cameraBeatPulse = Lerp(cameraBeatPulse, 0.0f, 1.0f - expf(-12.0f * dt));
        camera.zoom = 1.08f + cameraBeatPulse;

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
            pivotPos.x + cosf(currentAngle) * TILE_STEP,
            pivotPos.y + sinf(currentAngle) * TILE_STEP
        };
        firePos = isFirePivot ? pivotPos : orbitPos;
        icePos  = isFirePivot ? orbitPos : pivotPos;

        if (screen == SCR_PLAYING) {
            fireTrail.push_back({ firePos, { 255, 80, 45, 255 }, 0.88f });
            iceTrail.push_back({ icePos, { 45, 205, 255, 255 }, 0.88f });
            if (fireTrail.size() > 30) fireTrail.pop_front();
            if (iceTrail.size() > 30) iceTrail.pop_front();
        }
    }

    for (auto& t : tiles) if (t.hitAnim > 0.0f) t.hitAnim = fmaxf(0.0f, t.hitAnim - 4.5f * dt);
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
    DrawRichBackground(bgHue, cameraBeatPulse, dt);

    if (screen == SCR_MODE_SELECT) {
        DrawTextCentered("얼음과 불의 춤", SCREEN_WIDTH / 2.0f, 46.0f, 52.0f, WHITE);
        DrawTextCentered("게임 모드를 선택하세요", SCREEN_WIDTH / 2.0f, 110.0f, 20.0f, SKYBLUE);

        const char* modeTitles[4] = { "월드 투어", "무한 러시", "연습 모드", "퍼펙트 챌린지" };
        const char* modeBadges[4] = { "1-X ~ 12-X", "생존 모드", "게임오버 없음", "원히트 도전" };
        const char* modeDescs[4]  = {
            "입문부터 도전까지 12개의 고유한 음악과 코스를 정복하세요.",
            "끝없이 생성되는 랜덤 코스를 밟으며 최고 기록에 도전하세요.",
            "박자를 놓쳐도 실패하지 않고 끝까지 연습할 수 있습니다.",
            "단 한 번의 실수나 어긋남도 허용되지 않는 고수용 모드입니다."
        };
        float modeHues[4] = { 20.0f, 185.0f, 145.0f, 345.0f };

        for (int i = 0; i < 4; i++) {
            int col = i % 2;
            int row = i / 2;
            Rectangle card = { 170.0f + col * 485.0f, 185.0f + row * 165.0f, 455.0f, 140.0f };
            bool sel = ((int)currentMode == i) || CheckCollisionPointRec(mp, card);
            Color theme = ColorFromHSV(modeHues[i], 0.65f, 0.95f);

            DrawCardBox(card, 0.16f,
                        sel ? Fade(theme, 0.24f) : Color{ 18, 22, 36, 235 },
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

        DrawCardBox({ 135.0f, 532.0f, 1010.0f, 68.0f }, 0.25f, Color{ 16, 20, 32, 235 }, Fade(WHITE, 0.14f), 1.5f);

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
        DrawCardBox(backBtn, 0.3f, Color{ 20, 24, 38, 240 }, Fade(WHITE, 0.25f), 1.5f);
        DrawTextCentered("< 모드 선택 [ESC]", backBtn.x + backBtn.width * 0.5f, backBtn.y + 11.0f, 17.0f, WHITE);

        const char* modeBanner = (currentMode == MODE_JOURNEY) ? "모드: 월드 투어" :
                                 (currentMode == MODE_ZEN)     ? "모드: 연습 모드 (실패 없음)" :
                                                                 "모드: 퍼펙트 챌린지";
        DrawTextCentered(modeBanner, SCREEN_WIDTH / 2.0f, 36.0f, 22.0f, theme);

        Rectangle prevBtn = { 65.0f, 300.0f, 68.0f, 90.0f };
        Rectangle nextBtn = { 1147.0f, 300.0f, 68.0f, 90.0f };
        DrawCardBox(prevBtn, 0.3f, Color{ 20, 24, 38, 240 }, theme, 2.0f);
        DrawCardBox(nextBtn, 0.3f, Color{ 20, 24, 38, 240 }, theme, 2.0f);
        DrawTextCentered("<", prevBtn.x + 34.0f, prevBtn.y + 24.0f, 42.0f, WHITE);
        DrawTextCentered(">", nextBtn.x + 34.0f, nextBtn.y + 24.0f, 42.0f, WHITE);

        Rectangle showcase = { 165.0f, 105.0f, 950.0f, 440.0f };
        DrawCardBox(showcase, 0.08f, Color{ 16, 20, 32, 245 }, theme, 2.5f);

        Rectangle mapBox = { 205.0f, 145.0f, 390.0f, 360.0f };
        DrawRectangleRounded(mapBox, 0.1f, 8, Color{ 11, 14, 22, 255 });
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
        DrawTextUI(TextFormat("%s  •  %s", w.code, w.diffLabel), rx, 145.0f, 20.0f, theme);
        DrawTextUI(w.name, rx, 178.0f, 38.0f, WHITE);

        DrawTextUI("난이도:", rx, 238.0f, 17.0f, GRAY);
        for (int d = 0; d < 12; d++) {
            Color segCol = (d < w.difficultyLevel) ? theme : Fade(WHITE, 0.12f);
            DrawRectangleRounded({ rx + 75.0f + d * 29.0f, 236.0f, 24.0f, 18.0f }, 0.3f, 4, segCol);
        }

        DrawTextUI(TextFormat("음악: %s (%.0f BPM)", w.songStyle, w.bpm), rx, 280.0f, 18.0f, LIGHTGRAY);
        DrawTextUI(TextFormat("코스: %s", w.gimmickDesc), rx, 315.0f, 17.5f, LIGHTGRAY);

        if (w.cleared) {
            DrawTextUI(TextFormat("상태: 클리어 완료!  (최고 정확도: %.1f%%)", w.bestAcc), rx, 358.0f, 17.5f, GREEN);
        } else {
            DrawTextUI("상태: 아직 클리어하지 않음", rx, 358.0f, 17.5f, GRAY);
        }

        Rectangle playBtn = { rx, 440.0f, 430.0f, 64.0f };
        bool hoverPlay = CheckCollisionPointRec(mp, playBtn);
        DrawRectangleRounded(playBtn, 0.25f, 8, hoverPlay ? WHITE : theme);
        DrawTextCentered("월드 시작  [SPACE / ZXCV]", playBtn.x + playBtn.width * 0.5f, playBtn.y + 18.0f, 22.0f, BLACK, false);

        for (int i = 0; i < WORLD_COUNT; i++) {
            int col = i % 6;
            int row = i / 6;
            Rectangle pill = { 165.0f + col * 160.0f, 575.0f + row * 52.0f, 148.0f, 42.0f };
            bool isSel = (i == selectedWorld);
            Color pCol = ColorFromHSV(worlds[i].hue, 0.65f, 0.95f);
            DrawCardBox(pill, 0.3f, isSel ? Fade(pCol, 0.32f) : Color{ 18, 22, 36, 230 }, isSel ? pCol : Fade(WHITE, 0.16f), 2.0f);
            DrawTextCentered(worlds[i].code, pill.x + 74.0f, pill.y + 11.0f, 17.0f, isSel ? WHITE : LIGHTGRAY);
        }

        EndDrawing();
        return;
    }

    // ------------------------------------------
    // 렌더링 3: 원작 ADOFAI 인게임 월드 렌더링
    // ------------------------------------------
    BeginMode2D(camera);
    if (!tiles.empty()) {
        float timeNow = (float)GetTime();

        DrawAdofaiTrack(timeNow);

        DrawCircleLinesV(pivotPos, TILE_STEP, Fade(WHITE, 0.16f));
        if (screen == SCR_PLAYING && currentTile + 1 < (int)tiles.size()) {
            float remRatio = Clamp(1.0f - (traveledAngle / targetDiff), 0.0f, 1.0f);
            float startDeg = currentAngle * RAD2DEG;
            float endDeg   = (currentAngle + (cw ? 1.0f : -1.0f) * targetDiff * remRatio) * RAD2DEG;
            Color arcCol   = isFirePivot ? Color{ 85, 228, 255, 255 } : Color{ 255, 148, 60, 255 };
            DrawRing(pivotPos, TILE_STEP - 3.5f, TILE_STEP + 2.0f, startDeg, endDeg, 24, Fade(arcCol, 0.42f));

            Vector2 nextPos = tiles[currentTile + 1].pos;
            DrawCircleLinesV(nextPos, PLANET_RADIUS + 4.0f * sinf(timeNow * 12.0f), Fade(arcCol, 0.65f));
        }

        for (const auto& sw : shockwaves) {
            DrawRing(sw.pos, sw.radius - sw.thickness, sw.radius, 0, 360, 32, Fade(sw.color, sw.alpha));
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
            float r = pt.life / pt.maxLife;
            if (pt.spark) {
                Vector2 tail = Vector2Subtract(pt.pos, Vector2Scale(pt.vel, 0.025f));
                DrawLineEx(pt.pos, tail, pt.size * 0.7f, Fade(pt.color, r));
            } else {
                DrawCircleV(pt.pos, pt.size * r, Fade(pt.color, r));
            }
        }

        DrawCircleV(firePos, PLANET_RADIUS + 10.0f, Fade(ORANGE, 0.28f));
        DrawCircleLinesV(firePos, PLANET_RADIUS + 3.0f, Fade(GOLD, 0.65f));
        DrawCircleV(firePos, PLANET_RADIUS, { 255, 62, 42, 255 });
        DrawCircleV(firePos, PLANET_RADIUS * 0.48f, { 255, 228, 168, 255 });

        DrawCircleV(icePos, PLANET_RADIUS + 10.0f, Fade(SKYBLUE, 0.28f));
        DrawCircleLinesV(icePos, PLANET_RADIUS + 3.0f, Fade(WHITE, 0.65f));
        DrawCircleV(icePos, PLANET_RADIUS, { 42, 198, 255, 255 });
        DrawCircleV(icePos, PLANET_RADIUS * 0.48f, { 228, 250, 255, 255 });

        for (const auto& p : popups) {
            float a = Clamp(p.timer * 1.4f, 0.0f, 1.0f);
            DrawTextCentered(p.text.c_str(), p.pos.x, p.pos.y, 20.0f, Fade(p.color, a));
            if (!p.subText.empty()) {
                DrawTextCentered(p.subText.c_str(), p.pos.x, p.pos.y + 22.0f, 14.0f, Fade(WHITE, a * 0.85f));
            }
        }
    }
    EndMode2D();

    if (overloadFlash > 0.0f) {
        DrawRectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, Fade(RED, overloadFlash * 0.35f));
    }

    float accPct = (hitCount > 0) ? (accuracySum / hitCount * 100.0f) : 100.0f;

    if (currentMode == MODE_ENDLESS) {
        DrawTextUI(TextFormat("무한 러시  •  타일: %d  (최고: %d)", endlessTilesCleared, bestEndlessScore), 28.0f, 20.0f, 24.0f, WHITE);
        DrawTextUI(TextFormat("정확도: %.1f%%   |   연속: %d   |   BPM: %.0f", accPct, streakCount, currentBpm), 28.0f, 50.0f, 18.0f, SKYBLUE);
    } else {
        int realStart = 3;
        int totalPlayable = (int)tiles.size() - 1 - realStart;
        int donePlayable = Clamp(currentTile - realStart, 0, totalPlayable);
        float ratio = (totalPlayable > 0) ? ((float)donePlayable / totalPlayable) : 0.0f;

        DrawTextUI(TextFormat("%s  %s", worlds[selectedWorld].code, worlds[selectedWorld].name), 28.0f, 18.0f, 24.0f, WHITE);

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

    if (screen == SCR_GAMEOVER) {
        DrawRectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, Fade(BLACK, 0.72f));
        DrawTextCentered("행성 파괴!", SCREEN_WIDTH / 2.0f, 205.0f, 58.0f, RED);
        DrawTextCentered(failReason.c_str(), SCREEN_WIDTH / 2.0f, 280.0f, 24.0f, LIGHTGRAY);
        DrawTextCentered(TextFormat("완벽: %d   |   빠름: %d   |   느림: %d   |   최대 콤보: %d",
                                    perfectCount, earlyCount, lateCount, maxStreakCount),
                         SCREEN_WIDTH / 2.0f, 322.0f, 19.0f, SKYBLUE);
        if (gameOverTimer <= 0.0f) {
            const char* retryMsg = (currentMode == MODE_JOURNEY && lastCheckpoint > 0)
                ? "[SPACE / ZXCV] 체크포인트 부활   |   [R] 처음부터   |   [ESC] 메뉴"
                : "[SPACE / ZXCV] 다시 도전   |   [ESC] 메뉴";
            DrawTextCentered(retryMsg, SCREEN_WIDTH / 2.0f, 385.0f, 22.0f, WHITE);
        }
    }
    else if (screen == SCR_CLEAR) {
        DrawRectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, Fade(BLACK, 0.72f));
        DrawTextCentered("월드 클리어!", SCREEN_WIDTH / 2.0f, 200.0f, 58.0f, GOLD);
        DrawTextCentered(TextFormat("최종 정확도: %.2f%%   (최대 콤보: %d)", accPct, maxStreakCount),
                         SCREEN_WIDTH / 2.0f, 278.0f, 25.0f, SKYBLUE);
        DrawTextCentered(TextFormat("완벽: %d   |   빠름: %d   |   느림: %d",
                                    perfectCount, earlyCount, lateCount),
                         SCREEN_WIDTH / 2.0f, 318.0f, 20.0f, LIGHTGRAY);
        DrawTextCentered("[SPACE / ZXCV] 다음 월드   |   [ESC] 월드 선택",
                         SCREEN_WIDTH / 2.0f, 380.0f, 22.0f, WHITE);
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

    InitSpaceEnvironment();
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
        UnloadAudioStream(synthStream);
        CloseAudioDevice();
    }
    CloseWindow();
    return 0;
}
