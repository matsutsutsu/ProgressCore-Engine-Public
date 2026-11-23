#pragma once
#include "Engine/Serialization/Core/EntityLink.h"

// プレイヤーのカメラの、ロックオン中の補正と構図の調整値と状態
// 付いていないカメラはロックオンしない
struct PlayerCameraLockOnComponent
{
    // --- 狙う敵（PlayerLockOnSystem が毎フレーム書く） ---
    CCL::Serialization::EntityLink target;           // ロックオン中の敵
    bool                           isActive = false; // ロックオン中か

    // --- 見上げ角 ---
    float pitch        = 8.0f; // 保つピッチ（度）。正で見上げる
    float pitchDamping = 8.0f; // pitch へ寄せる速さ

    // --- 構図 ---
    float subjugationRatio = 0.35f; // 注視点を敵側へ寄せる割合（0〜1）
    float maxLookShift     = 3.0f;  // 注視点をずらす上限（m）
    float distanceGain     = 0.35f; // 敵との距離に掛けて引く係数
    float maxExtraDistance = 8.0f;  // 追加で引く上限（m）

    // --- ヨーの自動補正 ---
    bool  allowManualCamera  = true;   // ロックオン中も手で回せるか
    float yawDeadZone        = 20.0f;  // 補正しない角度の幅（度）
    float assistStrength     = 3.0f;   // はみ出た角度に掛ける係数（/秒）
    float maxCorrectionSpeed = 180.0f; // 補正の最大速度（度/秒）
    float manualOverrideTime = 0.25f;  // 手で回した後、補正を止める時間（秒）

    // --- 実行時の状態 ---
    float manualOverrideTimer = 0.0f; // 補正を止めている残り時間（秒）
};
