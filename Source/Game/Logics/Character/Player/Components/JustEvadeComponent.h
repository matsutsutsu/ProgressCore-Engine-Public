#pragma once
#include "Engine/ECS/Common/CCL_IDEmitter.h"

/// @brief JUST回避の成功状態を管理するコンポーネント
struct JustEvadeComponent {
    bool isJustSuccess = false;

    // 多段発動・無限ループ防止 (Traffic Control) 用データ
    CCL::ECS::EntityID lastEvadedTarget = CCL::ECS::InvalidEntityID; ///< 最後にジャスト回避した相手の顔（ID）
    float              resetTimer       = 0.0f;                      ///< ブラックリストの記憶を消すまでの時間
};