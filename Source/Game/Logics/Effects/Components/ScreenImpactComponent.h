#pragma once
// ===========================================================================
// ScreenImpactComponent.h
// -----------------------
// 役割:
//   「命中の瞬間」の画面演出（カメラシェイク＋放射ブラー/色収差バースト）の
//   グローバル状態を1つ保持するシングルトン設定コンポーネント。
//
// 責務:
//   - 演出のチューニング値（振幅・強さ・長さ）を保持。
//   - トリガ時にセットされる残り時間 timer と強さ倍率 power を保持。
//     ScreenImpactSystem が毎フレーム timer を減衰させ、強さを各演出へ流す。
//
// 位置付け:
//   シーンに singleton として1つ置く。DamageResolutionSystem が命中時に Trigger して
//   timer を立て、ScreenImpactSystem(R02) が camera と PostProcess 設定へ反映する。
//   ヒットストップ（時間停止）は別経路で TimeStateComponent 側が担当する。
//
// 設計方針:
//   - VFX を直接呼ばず「データで描画フェーズへ流す」方針に沿う（DamageResolutionSystem
//     の設計方針と同じ）。timer が 0 の間は何もしないので、他システムの PostProcess
//     利用を邪魔しない。
// ===========================================================================

/// @brief 命中演出（画面）のグローバル状態。シーンに singleton で1つ置く。
struct ScreenImpactComponent
{
    // --- チューニング（シリアライズ） ---
    float duration          = 0.22f;  ///< 1回のインパクトの長さ(秒)
    float shakeAmplitude    = 0.18f;  ///< カメラ揺れの最大振幅(m)
    float shakeFrequency    = 38.0f;  ///< 揺れの速さ(Hz目安)
    float radialStrength    = 0.35f;  ///< 放射ブラーのピーク強度
    float chromaticStrength = 0.012f; ///< 色収差のピーク強度(画面比)

    // --- ランタイム状態（非シリアライズ） ---
    float timer = 0.0f; ///< 残り時間(秒)。>0 の間だけ演出が出る。
    float power = 1.0f; ///< このインパクトの強さ倍率(0.3〜1.5想定)。トリガ時に設定。
    float seed  = 0.0f; ///< 揺れ位相のランダム種（連打で揺れが同じにならないように）。

    /// @brief 命中時に呼ぶ。強い衝撃を優先しつつ timer を満タンに戻す。
    /// @param p        強さ倍率（ダメージ由来など）。
    /// @param seedHint 位相をずらす任意の種（イベントID等）。
    void Trigger(float p, float seedHint)
    {
        timer = duration;      // 最新の命中で満タンに（連打で持続）
        power = p;
        seed  = seedHint;
    }
};
