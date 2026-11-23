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
// 1. 게임 상수 및 열거형 정의
// ==========================================
const int SCREEN_WIDTH = 1280;
const int SCREEN_HEIGHT = 720;
const float TILE_DIST = 96.0f;       // 타일 간 표준 공전 반지름
const float PLANET_RADIUS = 16.5f;   // 불/얼음 행성 반지름

enum GameState {
    STATE_MENU,
    STATE_COUNTDOWN,
    STATE_PLAYING,
    STATE_GAMEOVER,
    STATE_CLEAR
};

enum Strictness {
    STRICT_LENIENT = 0, // 느슨함
    STRICT_NORMAL  = 1, // 보통
    STRICT_STRICT  = 2  // 엄격함
};

// 타일 데이터 구조체
struct Tile {
    Vector2 pos;
    bool swirl = false;          // 소용돌이 (회전 방향 반전)
    float bpmMultiplier = 1.0f;  // 배속 (>1.0 토끼, <1.0 달팽이)
    bool midspin = false;        // 미드스핀 (축 이동 없이 즉시 다음 각도로 이어지는 삼각 타일)
    bool checkpoint = false;     // 체크포인트 깃발
    bool cleared = false;
    float hitAnim = 0.0f;        // 타격 시 타일 확대 애니메이션
};

// 스테이지 설계용 명령어
struct MapStep {
    float angleDeg;      // 이전 타일 기준 배치 각도 (도)
    bool swirl = false;
    float mult = 1.0f;
    bool midspin = false;
    bool checkpoint = false;
};

// 행성 잔상 점
struct TrailPoint {
    Vector2 pos;
    Color color;
    float alpha;
};

// 타격 파티클
struct Particle {
    Vector2 pos;
    Vector2 vel;
    Color color;
    float size;
    float life;
    float maxLife;
};

// 판정 텍스트 팝업
struct JudgmentPopup {
    std::string text;
    Vector2 pos;
    Color color;
    float timer;
};

// 판정 오차 바(Hit Error Meter) 눈금 기록
struct HitTick {
    float errorMs; // 음수: 빠름, 양수: 느림
    Color color;
    float alpha;
};

// ==========================================
// 2. 전역 게임 상태 변수
// ==========================================
GameState state = STATE_MENU;
Strictness strictness = STRICT_NORMAL;
int selectedStage = 0;
bool autoPlay = false;
float audioOffsetMs = 0.0f; // 오디오 싱크 오프셋 (-150ms ~ +150ms)

std::vector<Tile> tiles;
std::deque<TrailPoint> fireTrail;
std::deque<TrailPoint> iceTrail;
std::vector<Particle> particles;
std::vector<JudgmentPopup> popups;
std::deque<HitTick> hitTicks;

int currentTile = 0;         // 현재 도달해야 할 목표 인덱스 기준 (tiles[currentTile] -> tiles[currentTile+1])
int lastCheckpoint = 0;      // 마지막으로 통과한 체크포인트 인덱스
Vector2 pivotPos = { 0, 0 }; // 현재 회전의 중심축 좌표
bool isFirePivot = true;     // true: 불 행성이 축, false: 얼음 행성이 축
bool cw = true;              // 시계 방향(CW) 여부

float baseBpm = 130.0f;
float currentBpm = 130.0f;
float currentAngle = 0.0f;   // 현재 공전 행성의 각도 (라디안)
float targetDiff = PI;       // 다음 타일까지 돌아야 할 목표 각도량
float traveledAngle = 0.0f;  // 현재 타일에서 누적 회전한 각도량

// 비트 및 사운드 스케줄러
int countdownBeats = 4;
float beatTimer = 0.0f;
int totalBeatCount = 0;
float cameraBeatPulse = 0.0f;
float screenShake = 0.0f;
float bgHue = 225.0f;

// 통계 (정확도 및 콤보)
int hitCount = 0;
int perfectCount = 0;
float accuracySum = 0.0f;
std::string failReason = "";

Camera2D camera = { 0 };

// 프로시저럴 신디사이저 사운드
bool audioReady = false;
Sound sndHit;
Sound sndKick;
Sound sndSnare;
Sound sndHat;
Sound sndSwirl;
Sound sndSpeedUp;
Sound sndSpeedDown;
Sound sndFail;
Sound sndClear;

// ==========================================
// 3. 수학 및 오디오 합성 유틸리티
// ==========================================
float NormalizeAngle(float a) {
    while (a < 0.0f) a += 2.0f * PI;
    while (a >= 2.0f * PI) a -= 2.0f * PI;
    return a;
}

// 판정 허용 시간(초) 반환
void GetJudgmentWindows(float& purePerfect, float& perfect, float& maxWindow) {
    if (strictness == STRICT_LENIENT) {
        purePerfect = 0.065f; perfect = 0.130f; maxWindow = 0.210f;
    } else if (strictness == STRICT_STRICT) {
        purePerfect = 0.032f; perfect = 0.070f; maxWindow = 0.125f;
    } else { // NORMAL
        purePerfect = 0.048f; perfect = 0.100f; maxWindow = 0.165f;
    }
}

// 파형 합성기 (외부 wav/mp3 없이 브라우저에서 즉시 사운드 생성)
Sound SynthWave(int type, float freq, float duration, float volume) {
    int sampleRate = 44100;
    int count = (int)(sampleRate * duration);
    short* data = (short*)RL_MALLOC(count * sizeof(short));

    for (int i = 0; i < count; i++) {
        float t = (float)i / sampleRate;
        float sample = 0.0f;

        if (type == 0) { // 타격음 (맑은 퍼커션 벨 + 클릭)
            float env = expf(-t * 36.0f);
            sample = sinf(2.0f * PI * freq * t) * 0.7f
                   + sinf(2.0f * PI * (freq * 1.5f) * t) * 0.3f;
            sample *= env;
        }
        else if (type == 1) { // 킥 드럼 (저음 피치 드롭)
            float env = expf(-t * 18.0f);
            float f = freq * expf(-t * 22.0f) + 42.0f;
            sample = sinf(2.0f * PI * f * t) * env;
        }
        else if (type == 2) { // 스네어 / 하이햇 (화이트 노이즈 + 톤)
            float env = expf(-t * (freq > 1000.0f ? 55.0f : 24.0f));
            float noise = ((float)GetRandomValue(-1000, 1000) / 1000.0f);
            float tone = (freq < 1000.0f) ? sinf(2.0f * PI * 190.0f * t) * 0.4f : 0.0f;
            sample = (noise * 0.75f + tone) * env;
        }
        else if (type == 3) { // 피치 상승/하강 효과음 (소용돌이/배속/실패)
            float env = expf(-t * 11.0f);
            float f = freq + (duration > 0.25f ? -freq * 0.65f * (t / duration) : freq * 0.5f * (t / duration));
            sample = (sinf(2.0f * PI * f * t) + 0.3f * sinf(4.0f * PI * f * t)) * env;
        }

        if (sample > 1.0f) sample = 1.0f;
        if (sample < -1.0f) sample = -1.0f;
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
    sndHit       = SynthWave(0, 587.33f, 0.11f, 0.75f); // D5 타격음
    sndKick      = SynthWave(1, 150.00f, 0.18f, 0.65f); // 킥
    sndSnare     = SynthWave(2, 400.00f, 0.14f, 0.40f); // 스네어
    sndHat       = SynthWave(2, 2500.0f, 0.05f, 0.22f); // 하이햇
    sndSwirl     = SynthWave(3, 440.00f, 0.15f, 0.50f); // 소용돌이
    sndSpeedUp   = SynthWave(3, 660.00f, 0.16f, 0.55f); // 토끼(가속)
    sndSpeedDown = SynthWave(3, 330.00f, 0.20f, 0.55f); // 달팽이(감속)
    sndFail      = SynthWave(3, 160.00f, 0.40f, 0.80f); // 과부하 실패
    sndClear     = SynthWave(0, 880.00f, 0.45f, 0.80f); // 클리어
    audioReady = true;
}

// ==========================================
// 4. 스테이지 레벨 데이터 빌더 (3개 월드)
// ==========================================
void BuildStage(int stageIdx) {
    tiles.clear();
    std::vector<MapStep> steps;

    if (stageIdx == 0) {
        // [Stage 1] 1-X 불의 춤 (128 BPM) - 직진, 90도 계단, 소용돌이 기초
        baseBpm = 128.0f;
        bgHue = 225.0f;
        steps = {
            {0}, {0}, {0}, {0},
            {-90}, {0}, {-90}, {0},
            {90}, {0}, {90}, {0},
            {0, true}, {0}, {0}, {0}, // 소용돌이 반전
            {-90, false, 1.0f, false, true}, {0}, {90}, {0}, // 체크포인트 1
            {-45}, {45}, {-45}, {45},
            {0, false, 1.25f}, {0}, {-90}, {0}, // 토끼 가속
            {90, false, 0.8f}, {0}, {0}, {0}    // 달팽이 감속 후 피날레
        };
    }
    else if (stageIdx == 1) {
        // [Stage 2] 2-X 얼음의 왈츠 (142 BPM) - 삼각 미드스핀, 360도 유턴, 예각 리듬
        baseBpm = 142.0f;
        bgHue = 198.0f;
        steps = {
            {0}, {0}, {-90}, {0},
            // 미드스핀 콤보 (삼각 타일을 거쳐 연속 꺾기)
            {-90, false, 1.0f, true}, {0}, {90, false, 1.0f, true}, {0},
            {0, true}, {0}, {-45}, {-90}, {-45}, {0},
            {0, false, 1.0f, false, true}, // 체크포인트 1
            {180}, {0}, // 360도 유턴 (2박자 대기)
            {0, false, 1.35f}, {-90}, {0}, {90}, {0},
            {-45, true}, {45}, {-45}, {45},
            {0, false, 1.0f / 1.35f}, {0}, {0}, {0}
        };
    }
    else {
        // [Stage 3] 3-X 시공간 소용돌이 (160 BPM) - 고속 변속, 연속 소용돌이, 클라이맥스
        baseBpm = 160.0f;
        bgHue = 275.0f;
        steps = {
            {0}, {-90}, {0}, {90}, {0},
            {0, true}, {-90, true}, {0, true}, {90, true},
            {0, false, 1.4f}, {0}, {-45}, {45}, {-45}, {45},
            {0, false, 1.0f / 1.4f, false, true}, // 체크포인트 1
            {-90, false, 1.0f, true}, {0}, {-90, false, 1.0f, true}, {0},
            {180, true}, {0},
            {0, false, 1.5f}, {-90}, {0}, {-90}, {0}, {90}, {0}, {90}, {0},
            {0, false, 1.0f / 1.5f}, {0}, {0}
        };
    }

    // 시작 타일 (0번) 배치
    Vector2 curPos = { 0.0f, 0.0f };
    tiles.push_back({ curPos, false, 1.0f, false, false, true, 0.0f });

    // 각 스텝마다 유효한 회전 중심축(pivot)으로부터 정확히 TILE_DIST 거리에 다음 타일 배치
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
            steps[i].mult,
            steps[i].midspin,
            steps[i].checkpoint,
            false,
            0.0f
        });
        // 미드스핀이 아닌 일반 타일에 도착했을 때만 중심축이 이동함
        if (!steps[i].midspin) {
            buildPivot = nextPos;
        }
    }
}

// ==========================================
// 5. 공전 궤도 및 타격 이펙트 제어
// ==========================================
void SpawnHitParticles(Vector2 pos, Color col, int count) {
    for (int i = 0; i < count; i++) {
        float ang = (float)GetRandomValue(0, 360) * DEG2RAD;
        float spd = (float)GetRandomValue(90, 320);
        float life = (float)GetRandomValue(25, 50) / 100.0f;
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

void AddPopup(const std::string& msg, Vector2 pos, Color col) {
    popups.push_back({ msg, { pos.x, pos.y - 42.0f }, col, 0.75f });
}

// index번 타일에서 index+1번 타일로 이동하기 위한 회전 각도 세팅
void SetupOrbitForTile(int index, Vector2 prevPivotPos) {
    if (tiles[index].midspin) {
        // 미드스핀 타일: 중심축(pivotPos)과 기준 행성(isFirePivot)이 바뀌지 않음!
        Vector2 currTarget = tiles[index].pos;
        Vector2 nextTarget = tiles[index + 1].pos;
        float startAng = atan2f(currTarget.y - pivotPos.y, currTarget.x - pivotPos.x);
        float endAng   = atan2f(nextTarget.y - pivotPos.y, nextTarget.x - pivotPos.x);

        if (tiles[index].swirl) { cw = !cw; if (audioReady) PlaySound(sndSwirl); }
        if (tiles[index].bpmMultiplier > 1.05f && audioReady) PlaySound(sndSpeedUp);
        if (tiles[index].bpmMultiplier < 0.95f && audioReady) PlaySound(sndSpeedDown);
        currentBpm *= tiles[index].bpmMultiplier;

        float diff = cw ? NormalizeAngle(endAng - startAng) : NormalizeAngle(startAng - endAng);
        if (diff < 0.05f) diff += 2.0f * PI;

        currentAngle = startAng;
        targetDiff = diff;
        traveledAngle = 0.0f;
        return;
    }

    // 일반 타일: 중심축이 tiles[index].pos로 이동
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
        if (audioReady && state == STATE_PLAYING) PlaySound(sndSwirl);
    }
    if (tiles[index].bpmMultiplier > 1.05f && audioReady && state == STATE_PLAYING) PlaySound(sndSpeedUp);
    if (tiles[index].bpmMultiplier < 0.95f && audioReady && state == STATE_PLAYING) PlaySound(sndSpeedDown);
    currentBpm *= tiles[index].bpmMultiplier;

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

    // 체크포인트 재시작 시 해당 타일까지의 속도/소용돌이 누적 상태 복원
    Vector2 prevPiv = { 0, 0 };
    for (int i = 0; i <= startFromTile; i++) {
        tiles[i].cleared = true;
        if (i > 0) {
            if (tiles[i].swirl) cw = !cw;
            currentBpm *= tiles[i].bpmMultiplier;
        }
        if (!tiles[i].midspin) {
            if (i > 0) prevPiv = pivotPos;
            pivotPos = tiles[i].pos;
        }
    }

    SetupOrbitForTile(startFromTile, prevPiv);

    // 4박자 카운트다운 후 정확히 currentAngle 위치로 돌아오도록 설정 (4 * PI = 2바퀴)
    countdownBeats = 4;
    beatTimer = 0.0f;
    totalBeatCount = 0;
    cameraBeatPulse = 0.06f;

    camera.target = pivotPos;
    camera.offset = { SCREEN_WIDTH / 2.0f, SCREEN_HEIGHT / 2.0f };
    camera.zoom = 1.0f;
    state = STATE_COUNTDOWN;
    PlaySound(sndKick);
}

bool IsHitKeyPressed() {
    return IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_Z) || IsKeyPressed(KEY_X) ||
           IsKeyPressed(KEY_J) || IsKeyPressed(KEY_K) || IsKeyPressed(KEY_D) ||
           IsKeyPressed(KEY_F) || IsMouseButtonPressed(MOUSE_LEFT_BUTTON);
}

// ==========================================
// 6. 메인 업데이트 및 렌더링 루프
// ==========================================
void UpdateAndDrawFrame() {
    float dt = GetFrameTime();
    if (dt > 0.05f) dt = 0.05f;

    // 전역 단축키 (오토플레이 & 오디오 오프셋 조절)
    if (IsKeyPressed(KEY_TAB)) autoPlay = !autoPlay;
    if (IsKeyPressed(KEY_LEFT_BRACKET))  audioOffsetMs = fmaxf(-150.0f, audioOffsetMs - 5.0f);
    if (IsKeyPressed(KEY_RIGHT_BRACKET)) audioOffsetMs = fminf(150.0f, audioOffsetMs + 5.0f);

    float angularSpeed = PI * (currentBpm / 60.0f); // 1박자 = 180도(PI)
    float beatDuration = 60.0f / currentBpm;

    // --- 상태 머신 ---
    if (state == STATE_MENU) {
        if (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_A))  selectedStage = (selectedStage + 2) % 3;
        if (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D)) selectedStage = (selectedStage + 1) % 3;
        if (IsKeyPressed(KEY_ONE))   strictness = STRICT_LENIENT;
        if (IsKeyPressed(KEY_TWO))   strictness = STRICT_NORMAL;
        if (IsKeyPressed(KEY_THREE)) strictness = STRICT_STRICT;

        if (IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER) || IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
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
                // 카운트다운 종료 -> 본 게임 돌입
                traveledAngle = 0.0f;
                PlaySound(sndKick);
                state = STATE_PLAYING;
            }
        }
    }
    else if (state == STATE_PLAYING) {
        if (IsKeyPressed(KEY_ESCAPE)) { state = STATE_MENU; }
        if (IsKeyPressed(KEY_R))      { StartStage(selectedStage, 0); }

        // 배경 비트 그루브 사운드 (4비트 드럼 루프)
        beatTimer += dt;
        if (beatTimer >= beatDuration * 0.5f) { // 8분음표 단위
            beatTimer -= beatDuration * 0.5f;
            totalBeatCount++;
            if (totalBeatCount % 2 == 0) {
                cameraBeatPulse = 0.04f;
                PlaySound((totalBeatCount / 2) % 2 == 0 ? sndKick : sndSnare);
            } else {
                PlaySound(sndHat);
            }
        }

        float step = angularSpeed * dt;
        traveledAngle += step;
        currentAngle += (cw ? step : -step);

        // 오프셋 반영 판정 오차 계산
        float offsetRad = (audioOffsetMs / 1000.0f) * angularSpeed;
        float errorAngle = (traveledAngle - offsetRad) - targetDiff;
        float errorSec = errorAngle / angularSpeed;

        float pureWin, perfWin, maxWin;
        GetJudgmentWindows(pureWin, perfWin, maxWin);

        bool triggered = autoPlay ? (errorAngle >= 0.0f) : IsHitKeyPressed();

        if (triggered) {
            if (fabsf(errorSec) <= maxWin) {
                // 성공 판정!
                float errMs = errorSec * 1000.0f;
                Color jCol = GREEN;
                std::string jText = "정확!";
                float accVal = 1.0f;

                if (autoPlay || fabsf(errorSec) <= pureWin) {
                    jText = "정확!"; jCol = { 80, 255, 120, 255 }; accVal = 1.0f; perfectCount++;
                } else if (errorSec < 0) {
                    if (fabsf(errorSec) <= perfWin) { jText = "빠름"; jCol = GOLD; accVal = 0.75f; }
                    else { jText = "너무 빠름"; jCol = ORANGE; accVal = 0.45f; }
                } else {
                    if (fabsf(errorSec) <= perfWin) { jText = "느림"; jCol = GOLD; accVal = 0.75f; }
                    else { jText = "너무 느림"; jCol = ORANGE; accVal = 0.45f; }
                }

                hitCount++;
                accuracySum += accVal;
                hitTicks.push_back({ autoPlay ? 0.0f : errMs, jCol, 1.0f });
                if (hitTicks.size() > 24) hitTicks.pop_front();

                Vector2 hitTilePos = tiles[currentTile + 1].pos;
                AddPopup(jText, hitTilePos, jCol);
                SpawnHitParticles(hitTilePos, isFirePivot ? SKYBLUE : ORANGE, 16);
                PlaySound(sndHit);

                screenShake = 5.5f;
                currentTile++;
                tiles[currentTile].cleared = true;
                tiles[currentTile].hitAnim = 1.0f;

                if (tiles[currentTile].checkpoint) {
                    lastCheckpoint = currentTile;
                    AddPopup("체크포인트 저장!", hitTilePos, SKYBLUE);
                }

                // 미드스핀 타일이 아니면 불/얼음 축 교대
                if (!tiles[currentTile].midspin) {
                    isFirePivot = !isFirePivot;
                }

                if (currentTile >= (int)tiles.size() - 1) {
                    PlaySound(sndClear);
                    state = STATE_CLEAR;
                } else {
                    Vector2 oldPivot = pivotPos;
                    SetupOrbitForTile(currentTile, oldPivot);
                }
            } else {
                // 너무 일찍 누름 (과부하)
                PlaySound(sndFail);
                screenShake = 16.0f;
                failReason = "너무 일찍 입력했습니다 (과부하!)";
                SpawnHitParticles(pivotPos, RED, 28);
                state = STATE_GAMEOVER;
            }
        }
        else if (errorSec > maxWin) {
            // 입력 없이 타일을 지나침 (놓침)
            PlaySound(sndFail);
            screenShake = 16.0f;
            failReason = "박자를 놓쳤습니다!";
            SpawnHitParticles(tiles[currentTile + 1].pos, RED, 28);
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
        else if (IsHitKeyPressed())   { StartStage((selectedStage + 1) % 3, 0); }
    }

    // --- 카메라 추적 & 파티클/잔상 업데이트 ---
    if (!tiles.empty() && state != STATE_MENU) {
        camera.target = Vector2Lerp(camera.target, pivotPos, 1.0f - expf(-8.5f * dt));
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
            fireTrail.push_back({ firePos, { 255, 75, 45, 255 }, 0.75f });
            iceTrail.push_back({ icePos, { 45, 200, 255, 255 }, 0.75f });
            if (fireTrail.size() > 22) fireTrail.pop_front();
            if (iceTrail.size() > 22) iceTrail.pop_front();
        }
    }

    for (auto& t : tiles) if (t.hitAnim > 0.0f) t.hitAnim = fmaxf(0.0f, t.hitAnim - 4.0f * dt);
    for (auto& tr : fireTrail) tr.alpha *= 0.89f;
    for (auto& tr : iceTrail)  tr.alpha *= 0.89f;
    for (auto& tk : hitTicks)  tk.alpha = fmaxf(0.15f, tk.alpha - 0.12f * dt);

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
    // 7. 화면 렌더링
    // ==========================================
    BeginDrawing();
    Color bgCol = ColorFromHSV(bgHue, 0.42f, 0.11f + cameraBeatPulse * 0.6f);
    ClearBackground(bgCol);

    // 배경 동심원 비트 비주얼라이저
    DrawCircleLinesV({ SCREEN_WIDTH / 2.0f, SCREEN_HEIGHT / 2.0f }, 260.0f + cameraBeatPulse * 400.0f, Fade(WHITE, 0.04f));
    DrawCircleLinesV({ SCREEN_WIDTH / 2.0f, SCREEN_HEIGHT / 2.0f }, 440.0f + cameraBeatPulse * 600.0f, Fade(WHITE, 0.03f));

    if (state == STATE_MENU) {
        DrawText("A DANCE OF FIRE AND ICE", 275, 130, 52, WHITE);
        DrawText("C++ WebAssembly Rhythm Engine", 435, 195, 24, SKYBLUE);

        const char* stageNames[3] = {
            "1-X  불의 춤 (128 BPM) - 입문 & 소용돌이",
            "2-X  얼음의 왈츠 (142 BPM) - 미드스핀 & 유턴",
            "3-X  시공간 소용돌이 (160 BPM) - 고속 변속 러시"
        };
        for (int i = 0; i < 3; i++) {
            int y = 275 + i * 68;
            bool sel = (i == selectedStage);
            DrawRectangleRounded({ 330.0f, (float)y, 620.0f, 52.0f }, 0.25f, 8, sel ? Fade(SKYBLUE, 0.28f) : Fade(WHITE, 0.06f));
            if (sel) DrawRectangleLines(330, y, 620, 52, SKYBLUE);
            DrawText(stageNames[i], 360, y + 15, 22, sel ? WHITE : LIGHTGRAY);
        }

        const char* strictNames[3] = { "느슨함 [1]", "보통 [2]", "엄격함 [3]" };
        DrawText(TextFormat("판정 난이도: %s    |    오토플레이: %s [TAB]    |    싱크: %.0f ms ([ / ])",
                 strictNames[strictness], autoPlay ? "ON" : "OFF", audioOffsetMs), 255, 515, 20, GOLD);
        DrawText("방향키로 스테이지 선택 후 [SPACE] 또는 [클릭]으로 시작", 335, 580, 24, WHITE);
        EndDrawing();
        return;
    }

    // --- 월드 카메라 렌더링 ---
    BeginMode2D(camera);
    if (!tiles.empty()) {
        // 1. 타일 도로(트랙) 그리기
        for (size_t i = 0; i + 1 < tiles.size(); i++) {
            Vector2 p1 = tiles[i].pos;
            Vector2 p2 = tiles[i + 1].pos;
            if (tiles[i].midspin && i > 0) {
                // 미드스핀 타일은 활성 중심축에서 뻗어나오는 삼각 경로로 표시
                p1 = tiles[i - 1].pos;
            }
            Color borderCol = tiles[i].cleared ? Color{ 85, 115, 165, 255 } : Color{ 50, 62, 92, 255 };
            Color coreCol   = tiles[i].cleared ? Color{ 38, 52, 78, 255 }   : Color{ 24, 30, 46, 255 };
            DrawLineEx(p1, p2, 48.0f, borderCol);
            DrawLineEx(p1, p2, 38.0f, coreCol);
        }

        // 2. 타일 노드 및 특수 기믹 아이콘
        for (size_t i = 0; i < tiles.size(); i++) {
            float scale = 1.0f + tiles[i].hitAnim * 0.28f;
            Color rimCol = (i == (size_t)currentTile) ? WHITE :
                           (tiles[i].cleared ? Color{ 100, 135, 190, 255 } : Color{ 68, 82, 115, 255 });
            if (i == tiles.size() - 1) rimCol = GOLD;

            if (tiles[i].midspin) {
                // 미드스핀 전용 삼각형 타일
                DrawPoly(tiles[i].pos, 3, 25.0f * scale, (float)GetTime() * 60.0f, MAGENTA);
                DrawPoly(tiles[i].pos, 3, 16.0f * scale, (float)GetTime() * 60.0f, { 28, 32, 48, 255 });
            } else {
                DrawCircleV(tiles[i].pos, 24.0f * scale, rimCol);
                DrawCircleV(tiles[i].pos, 18.5f * scale, { 24, 28, 42, 255 });
            }

            // 소용돌이 아이콘 (회전하는 링)
            if (tiles[i].swirl) {
                float rot = (float)GetTime() * 180.0f;
                DrawRing(tiles[i].pos, 9.5f, 13.5f, rot, rot + 260.0f, 20, { 235, 90, 255, 255 });
            }
            // 토끼(가속) / 달팽이(감속) 표시
            if (tiles[i].bpmMultiplier > 1.05f) {
                DrawPoly(tiles[i].pos, 3, 10.0f, 0.0f, RED);
            } else if (tiles[i].bpmMultiplier < 0.95f) {
                DrawPoly(tiles[i].pos, 3, 10.0f, 180.0f, SKYBLUE);
            }
            // 체크포인트 깃발 표시
            if (tiles[i].checkpoint) {
                DrawCircleLinesV(tiles[i].pos, 29.0f, GREEN);
            }
        }

        // 3. 현재 공전 궤도 가이드
        DrawCircleLinesV(pivotPos, TILE_DIST, Fade(WHITE, 0.16f));

        // 4. 불 & 얼음 행성 잔상
        for (size_t i = 0; i < fireTrail.size(); i++) {
            float r = PLANET_RADIUS * ((float)i / fireTrail.size());
            DrawCircleV(fireTrail[i].pos, r, Fade(fireTrail[i].color, fireTrail[i].alpha));
        }
        for (size_t i = 0; i < iceTrail.size(); i++) {
            float r = PLANET_RADIUS * ((float)i / iceTrail.size());
            DrawCircleV(iceTrail[i].pos, r, Fade(iceTrail[i].color, iceTrail[i].alpha));
        }

        // 5. 타격 파티클
        for (const auto& pt : particles) {
            DrawCircleV(pt.pos, pt.size * (pt.life / pt.maxLife), Fade(pt.color, pt.life / pt.maxLife));
        }

        // 6. 불 & 얼음 행성 본체
        DrawCircleV(firePos, PLANET_RADIUS + 6.0f, Fade(ORANGE, 0.30f));
        DrawCircleV(firePos, PLANET_RADIUS, { 255, 65, 45, 255 });
        DrawCircleV(firePos, PLANET_RADIUS * 0.45f, { 255, 210, 140, 255 });

        DrawCircleV(icePos, PLANET_RADIUS + 6.0f, Fade(SKYBLUE, 0.30f));
        DrawCircleV(icePos, PLANET_RADIUS, { 45, 195, 255, 255 });
        DrawCircleV(icePos, PLANET_RADIUS * 0.45f, { 210, 245, 255, 255 });

        // 7. 판정 텍스트
        for (const auto& p : popups) {
            DrawText(p.text.c_str(), (int)p.pos.x - 32, (int)p.pos.y, 22, Fade(p.color, p.timer * 1.3f));
        }
    }
    EndMode2D();

    // --- 상단 HUD & 하단 판정 오차 바(Hit Error Meter) ---
    int progress = (int)((float)currentTile / (tiles.size() - 1) * 100.0f);
    float accPct = (hitCount > 0) ? (accuracySum / hitCount * 100.0f) : 100.0f;
    DrawText(TextFormat("STAGE %d-X   진행률: %d%%   정확도: %.1f%%   BPM: %.0f",
             selectedStage + 1, progress, accPct, currentBpm), 28, 22, 22, WHITE);

    if (autoPlay) {
        DrawText("[AUTOPLAY ON]", SCREEN_WIDTH - 210, 22, 22, GOLD);
    }

    // 하단 판정 오차 바 (중앙=정확, 왼쪽=빠름, 오른쪽=느림)
    int barW = 360, barH = 10;
    int barX = SCREEN_WIDTH / 2 - barW / 2;
    int barY = SCREEN_HEIGHT - 44;
    DrawRectangle(barX, barY, barW, barH, Fade(ORANGE, 0.45f));
    DrawRectangle(barX + 60, barY, barW - 120, barH, Fade(GOLD, 0.55f));
    DrawRectangle(barX + 130, barY, barW - 260, barH, Fade(GREEN, 0.75f));
    DrawRectangle(SCREEN_WIDTH / 2 - 2, barY - 6, 4, barH + 12, WHITE); // 정중앙 기준선

    for (const auto& tk : hitTicks) {
        float clamped = Clamp(tk.errorMs, -160.0f, 160.0f);
        int tx = SCREEN_WIDTH / 2 + (int)((clamped / 160.0f) * (barW / 2.0f));
        DrawRectangle(tx - 2, barY - 5, 4, barH + 10, Fade(tk.color, tk.alpha));
    }

    // --- 상태별 중앙 오버레이 ---
    if (state == STATE_COUNTDOWN) {
        const char* cdText = (countdownBeats == 3) ? "3" :
                             (countdownBeats == 2) ? "2" :
                             (countdownBeats == 1) ? "1" : "준비";
        DrawText(cdText, SCREEN_WIDTH / 2 - MeasureText(cdText, 64) / 2, 130, 64, GOLD);
    }
    else if (state == STATE_GAMEOVER) {
        DrawRectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, Fade(BLACK, 0.65f));
        DrawText("OVERLOAD!", 495, 230, 54, RED);
        DrawText(failReason.c_str(), SCREEN_WIDTH / 2 - MeasureText(failReason.c_str(), 24) / 2, 300, 24, LIGHTGRAY);
        DrawText(lastCheckpoint > 0 ? "[SPACE] 체크포인트부터 부활   |   [R] 처음부터   |   [ESC] 메뉴"
                                    : "[SPACE] 다시 도전   |   [ESC] 스테이지 메뉴",
                 315, 370, 22, WHITE);
    }
    else if (state == STATE_CLEAR) {
        DrawRectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, Fade(BLACK, 0.65f));
        DrawText("STAGE CLEAR!", 455, 220, 54, GOLD);
        DrawText(TextFormat("최종 정확도: %.2f%%   (완벽 판정: %d / %d)", accPct, perfectCount, hitCount),
                 415, 295, 24, SKYBLUE);
        DrawText("[SPACE] 다음 스테이지   |   [ESC] 스테이지 메뉴", 390, 365, 22, WHITE);
    }

    EndDrawing();
}

int main() {
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "A Dance of Fire and Ice - C++");
    BuildStage(0);

#if defined(PLATFORM_WEB)
    emscripten_set_main_loop(UpdateAndDrawFrame, 0, 1);
#else
    SetTargetFPS(60);
    while (!WindowShouldClose()) {
        UpdateAndDrawFrame();
    }
#endif

    if (audioReady) {
        UnloadSound(sndHit); UnloadSound(sndKick); UnloadSound(sndSnare);
        UnloadSound(sndHat); UnloadSound(sndSwirl); UnloadSound(sndSpeedUp);
        UnloadSound(sndSpeedDown); UnloadSound(sndFail); UnloadSound(sndClear);
        CloseAudioDevice();
    }
    CloseWindow();
    return 0;
}
