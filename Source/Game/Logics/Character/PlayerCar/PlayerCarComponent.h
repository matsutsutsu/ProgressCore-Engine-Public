#pragma once
#include "ECS/Common/CCL_Common.h"
#include <DirectXMath.h>

struct PlayerCarComponent {
    // --- 抽象化された入力データ (Input State) ---
    struct InputData {
        float throttle = 0.0f; // アクセル/ブレーキ (-1.0 ~ 1.0)
        float steering = 0.0f; // ハンドル操作 (-1.0 ~ 1.0)
        bool jump = false;     // ジャンプやブースト用
    } input;

    // --- リンクする物理球体の EntityID ---
    // 見た目の車（このコンポーネントを持つEntity）と、見えないボウリングの球をリンクさせます
    CCL::ECS::EntityID physicsSphereID = CCL::ECS::InvalidEntityID;

    // --- 車両固有のパラメータ ---
    float acceleration = 2000.0f; // 球体を押し出す力
    float turnSpeed = 150.0f;  // ハンドルを切る速度（度/秒）
};