#pragma once
#include <DirectXMath.h>

// ---------------------------------------------------------
//  1. 脳の状態（現在何をしているか）
// ---------------------------------------------------------
enum class AIState {
    Idle,       // 待機
    Patrol,     // 徘徊（Day1のメイン）
    Investigate,// 調査（音や不審な場所へ向かう）
    Chase,      // 追跡（視界に入ったプレイヤーを追う）
    Telekinesis // 念力攻撃（Day4以降）
};

struct AIStateComponent {
    AIState currentState = AIState::Patrol; // 最初は徘徊からスタート
    float timeInState = 0.0f;               // このステートになってからの経過時間

    // 徘徊用のパラメータ（JSONからホットリロードできるようにすると便利です）
    float patrolWaitTime = 2.0f;            // 目的地に着いてから立ち止まる時間
    float patrolRadius = 15.0f;             // 自分の周囲何メートルから次の目的地を探すか
};

// ---------------------------------------------------------
//  2. 知覚能力（Dayごとの能力解放用）
// ---------------------------------------------------------
struct AIPerceptionComponent {
    // Day1ではすべて0（無能）に設定しておき、Dayが進むごとにJSONから数値を引き上げます
    float visionRange = 0.0f;
    float visionAngle = 45.0f;
    float hearingRadius = 0.0f;
};

// ---------------------------------------------------------
//  3. 記憶と適応（プレイヤーの癖を記憶するメタAI用）
// ---------------------------------------------------------
struct AIMemoryComponent {
    DirectX::XMFLOAT3 lastKnownPlayerPos = { 0.0f, 0.0f, 0.0f };
    float suspicionLevel = 0.0f; // 疑心暗鬼度

    // 適応レベル用カウンター
    int decoyThrowCount = 0;
    int hideSuccessCount = 0;
};