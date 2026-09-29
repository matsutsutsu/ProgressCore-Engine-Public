#pragma once
// ===========================================================================
// 概要: ジャスト回避に成功した瞬間の画面演出（スロー中の色調・放射ブラー・色収差・ビネット）の
//       設定と残り時間を持つシングルトンコンポーネント。シーンに1つ置く。
//
// 責務:
//   - 演出の調整値（長さ・立ち上がりと戻りの時間・各ポストプロセスの最大の強さ・色のスタイル）を持つ
//   - Trigger() で立てる残り時間 timer と、切り払いの準備中にモノクロを保つ hold を持つ
//
// 備考: JustEvadeSystem が成功時に Trigger() し、JustEvadeEffectSystem（R02）が timer を実時間で減らして
//       ポストプロセスへ反映する。時間を遅くするのは別の経路（Time::HitStop）。
//       timer が 0 の間は何も書かないので、他の演出のポストプロセスを邪魔しない。
//       色のスタイルはインスペクタで見比べるため、実行中に3種から切り替えられる。
// ===========================================================================
#include <cstdint>

// スロー中の画面全体の色味
enum class JustEvadeStyle : uint8_t
{
    CoolBlue    = 0, // クールな青（バレットタイム）
    WitchPurple = 1, // 紫（ウィッチタイム / ベヨネッタ型）
    Monochrome  = 2, // ほぼ脱色 + 高コントラスト（硬派 / 隻狼型）
};

// CFIELD_ENUM_U8 に渡す表示名（enum の並びと一致させること）
static constexpr const char *JustEvadeStyleNames[] = {
    "Cool Blue (Bullet Time)",
    "Witch Purple",
    "Monochrome (Sekiro)",
};
static constexpr int JustEvadeStyleCount = 3;

struct JustEvadeEffectComponent
{
    // --- 調整値（保存する） ---
    uint8_t style       = static_cast<uint8_t>(JustEvadeStyle::CoolBlue); // JustEvadeStyle
    float   duration    = 0.5f;                                           // 演出全体の長さ（実時間の秒）。スローの長さと合わせる
    float   attackTime  = 0.03f;                                          // 0 から最大まで上がる時間（秒）
    float   releaseTime = 0.18f;                                          // 最大から 0 まで戻る時間（秒）
    float   spikeDecay  = 0.18f;                                          // 放射ブラーと色収差が最大から 0 になる時間（秒）

    float radialPeak    = 0.35f; // 放射ブラーの最大の強さ
    float chromaPeak    = 0.02f; // 色収差の最大の強さ（画面に対する割合）
    float gradeMixPeak  = 0.9f;  // 色調補正をかける割合の最大（0〜1）
    float vignettePeak  = 0.6f;  // ビネットの最大の暗さ
    float vignettePulse = 5.0f;  // ビネットが明滅する速さ（0 なら明滅しない）

    // --- 実行時の状態（保存しない） ---
    float timer = 0.0f; // 残り時間（秒）。0 より大きい間だけ演出を出す

    // true の間は timer に関係なくモノクロを保つ（PlayerCleaveSystem が切り払いの準備中に立てる）。
    // 放射ブラーと色収差は出さず、色を抜くだけにする。
    bool hold = false;

    // ジャスト回避の成功時に呼ぶ。timer を duration に戻して演出を始める
    void Trigger()
    {
        timer = duration;
    }
};
