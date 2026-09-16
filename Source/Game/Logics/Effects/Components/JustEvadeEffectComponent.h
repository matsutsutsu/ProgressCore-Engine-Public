#pragma once
// ===========================================================================
// JustEvadeEffectComponent.h
// --------------------------
// 役割:
//   「ジャスト回避成功」の瞬間に走る画面演出（ウィッチタイム）のグローバル状態を
//   1つ保持するシングルトン設定コンポーネント。
//
// 責務:
//   - 演出のチューニング値（長さ・立ち上がり/戻り・各PPのピーク強度・色スタイル）を保持。
//   - Trigger() でセットされる残り時間 timer を保持。JustEvadeEffectSystem が毎フレーム
//     timer を実時間で減衰させ、色調(ColorGrading)/放射ブラー/色収差/ビネットへ流す。
//
// 位置付け:
//   シーンに singleton として1つ置く。JustEvadeSystem が成功時に Trigger() し、
//   JustEvadeEffectSystem(R02) が各PP設定へ反映する。時間スロー自体は別経路
//   （TimeContext.globalHitStopTimer / globalFreezeScale）が担当する。
//
// 設計方針:
//   - VFX を直接呼ばず「データで描画フェーズへ流す」方針（ScreenImpact と同じ）。
//   - timer が 0 の間は何もしないので、他システムの PostProcess 利用を邪魔しない。
//   - 色スタイルは3種をランタイムで切り替え可能（インスペクタで見比べる用）。
// ===========================================================================
#include <cstdint>

// ジャスト回避演出の色味スタイル（スロー中の画面全体の方向性）。
enum class JustEvadeStyle : uint8_t
{
    CoolBlue    = 0, // クールな青（バレットタイム）
    WitchPurple = 1, // 紫（ウィッチタイム / ベヨネッタ型）
    Monochrome  = 2, // ほぼ脱色 + 高コントラスト（硬派 / 隻狼型）
};

// CFIELD_ENUM_U8 用の表示名テーブル（enum の並びと一致させること）。
static constexpr const char *JustEvadeStyleNames[] = {
    "Cool Blue (Bullet Time)",
    "Witch Purple",
    "Monochrome (Sekiro)",
};
static constexpr int JustEvadeStyleCount = 3;

// ジャスト回避演出（画面）のグローバル状態。シーンに singleton で1つ置く。
struct JustEvadeEffectComponent
{
    // --- チューニング（シリアライズ） ---
    uint8_t style       = static_cast<uint8_t>(JustEvadeStyle::CoolBlue); // 色味スタイル
    float   duration    = 0.5f;                                           // 演出全体の長さ(実秒)。スロー窓(globalHitStopTimer)と合わせる。
    float   attackTime  = 0.03f;                                          // 立ち上がり時間(秒)。0→ピークへ一気に。
    float   releaseTime = 0.18f;                                          // 戻り時間(秒)。ピーク→0へスッと戻す。
    float   spikeDecay  = 0.18f;                                          // 放射ブラー/色収差スパイクの減衰時間(秒)。t=0最大→0。

    float radialPeak    = 0.35f; // 放射ブラーのピーク強度
    float chromaPeak    = 0.02f; // 色収差のピーク強度(画面比)
    float gradeMixPeak  = 0.9f;  // 色調グレーディングの適用量ピーク(0..1)
    float vignettePeak  = 0.6f;  // ビネットの暗さピーク
    float vignettePulse = 5.0f;  // ビネットの脈動速度(0で脈動なし)

    // --- ランタイム状態（非シリアライズ） ---
    float timer = 0.0f;  // 残り時間(秒)。>0 の間だけ演出が出る。
    bool  hold  = false; // true の間はモノクロを「サステイン保持」（切り払い準備の照準中など）。
                         // スパイク(放射ブラー/色収差)は出さず、静かな脱色だけを維持する。

    // ジャスト回避成功時に呼ぶ。timer を満タンに戻して演出を開始する。
    void Trigger()
    {
        timer = duration;
    }
};
