#pragma once
// ===========================================================================
// Effects/TravelResolution.h
// --------------------------
// 役割:
//   Travelフェーズ(飛行中・毎フレーム)の解決結果。各 <Name>Travel ハンドラが
//   ここへ積み、BulletTravelSystem が1フレームの最後に弾の状態へ適用する。
//
// 責務:
//   - このフレームで弾の speed / direction に与える変化量の集約のみ。
//
// アーキテクチャ上の位置付け:
//   Spawn(発射時1回)と異なり Travel は毎フレーム解決される。よって本構造体は
//   毎フレーム生成・破棄される一時データで、状態は持たない(状態は BulletState 側)。
//
// 設計方針:
//   - Spawn と同じ add/mult 分離の思想。speed は乗算アキュムレータ、向き補正は
//     「目標方向」と「補正の強さ(0〜1)」で表現する。掛け算・補間はそれぞれ可換／
//     last-wins になるよう設計し、ハンドラの並び順に依存しないようにする。
//   - 向き補正(Homing等)は「目標方向ベクトル」と「lerp率」を持ち、System側で
//     direction を目標へ補間する。複数の向き効果は最も強い lerp 率を採用する。
// ===========================================================================
#include "SimpleMath.h"

struct TravelResolution
{
    // --- 速度(乗算アキュムレータ) ---
    float speedMult = 1.0f; ///< このフレームの速度倍率(Accelerate/Slow が掛ける)

    // --- 向き補正(Homing 等) ---
    bool  hasSteering = false;                                       ///< 向き補正が要求されたか
    DirectX::SimpleMath::Vector3 targetDir = DirectX::SimpleMath::Vector3::Zero; ///< 目標方向(正規化想定)
    float steerLerp = 0.0f;                                          ///< 向き補間率 [0,1]（大きいほど急旋回）

    /// @brief 向き補正を要求する。より強い補間率を優先（last-winsではなく max を採る）。
    void RequestSteer(const DirectX::SimpleMath::Vector3& dir, float lerp)
    {
        hasSteering = true;
        targetDir = dir;
        if (lerp > steerLerp) steerLerp = lerp;
    }
};