/// @file PlayerComponent.h
/// @brief プレイヤー固有のステータス（速度や設定）のみを保持するコンポーネント
#pragma once

struct PlayerComponent {
    float moveSpeed = 30.0f;
    float turnSpeed = 10.0f;
    int padSlot = 0;
};