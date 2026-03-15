#pragma once

enum class PlayerDeathPhase {
    InitialShock, // 激震開始
    WaitShake,    // 激震待機中
    ZoomAndFog,   // カメラ演出・フォグ変化中
    Completed     // 演出完了・UI表示待ち
};

// 【純粋データ】プレイヤーの死亡演出の進行状態を管理する
struct PlayerDeathSequenceComponent {
    PlayerDeathPhase phase      = PlayerDeathPhase::InitialShock;
    float            stateTimer = 0.0f;
};