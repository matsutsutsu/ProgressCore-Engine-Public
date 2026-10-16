#pragma once
// ===========================================================================
// 概要: ジャスト回避ラッシュ中の画面演出（周囲が暗くなるビネットと、放射ブラー）の
//       設定と状態を持つシングルトンコンポーネント。シーンに1つ置く。
//
// 責務:
//   - ビネットの強度・半径と、放射ブラーの強度・減衰を持つ
//   - フェードイン/アウトの時間を持つ（ビネットと放射ブラーで共有）
//   - active の間はピークへフェードイン、false でフェードアウトする現在値 weight を持つ
//
// 備考: rushActive を立てるのは PlayerRushSystem（ラッシュ中）。
//       weight を実時間でイーズしてビネットと放射ブラーへ反映するのは JustEvadeEffectSystem(R02)。
// ===========================================================================

struct JustEvadeEffectComponent
{
    // --- 調整値（保存する） ---
    float vignetteIntensity = 0.5f; // ピークの暗さ（0〜1）
    float vignetteRadius    = 0.2f; // ビネットの半径（小さいほど中心近くまで暗い）
    float radialStrength    = 0.6f; // 放射ブラーのピークの強さ（0〜1）
    float radialFalloff     = 3.4f; // 放射ブラーの減衰（大きいほど中心がくっきりし、縁だけが流れる）
    float fadeInTime        = 0.2f; // 0→ピークへ上がる時間（秒）
    float fadeOutTime       = 0.4f; // ピーク→0へ戻る時間（秒）。ラッシュの世界スロー復帰に合わせると自然

    // --- 実行時の状態（保存しない） ---
    bool  rushActive = false; // ラッシュ(PlayerRushSystem)が要求中。true の間ピークへ、false で 0 へ
    float weight     = 0.0f;  // 現在のフェード重み(0→1)。JustEvadeEffectSystem が実時間で動かす
};
