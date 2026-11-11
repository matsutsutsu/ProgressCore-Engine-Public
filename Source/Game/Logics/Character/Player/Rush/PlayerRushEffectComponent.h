#pragma once

// ラッシュ中の画面演出（ビネットと放射ブラー）の設定と状態。プレイヤーが PlayerRushComponent と一緒に持つ
// weight は PlayerRushEffectSystem が、同じエンティティの PlayerRushComponent.isActive を見て動かす
struct PlayerRushEffectComponent
{
    // --- 調整値（保存する） ---

    float vignetteIntensity = 0.5f; // ビネットのピークの暗さ（0〜1）
    float vignetteRadius    = 0.2f; // ビネットの半径。小さいほど中心まで暗い
    float radialStrength    = 0.6f; // 放射ブラーのピークの強さ（0〜1）
    float radialFalloff     = 3.4f; // 放射ブラーの減衰。大きいほど縁だけが流れる
    float fadeInTime        = 0.2f; // ピークまで上がる時間（秒）
    float fadeOutTime       = 0.4f; // 0 まで戻る時間（秒）

    // --- 実行時の状態（保存しない） ---

    float weight = 0.0f; // 今のフェードの重み（0〜1）
};
