#pragma once
// ===========================================================================
// 概要: 距離フォグ・高さフォグ・ノイズ・空の地平線フォグ・リムライトの設定を
//       まとめて持つシングルトンコンポーネント。シーンに1つ置く。
//
// 責務:
//   - フォグとリムライトのパラメータを持つ
//   - ノイズテクスチャのパスと、読み込んだハンドルを持つ
//
// 備考: EnvironmentSystem が毎フレーム CbFog にまとめて GPU へ送る。
//       コンポーネントが無いか enabled=false なら、フォグは完全に無効になる。
//       ノイズテクスチャはパスが変わるとハンドルを無効にし、EnvironmentSystem が次に読み込む。
// ===========================================================================
#include <DirectXMath.h>
#include "Engine/Graphics/Resource/ResourceTypes.h"

struct FogComponent
{
    DirectX::XMFLOAT4 color{0.7f, 0.7f, 0.8f, 1.0f}; // フォグの色。a がフォグ全体（地平線フォグを含む）の強さ

    // --- 距離フォグ ---
    float start = 10.0f; // フォグがかかり始める距離（m）。center からの距離で測る
    float end   = 50.0f; // フォグが最も濃くなる距離（m）

    // --- 高さフォグ ---
    float heightStart   = 0.0f; // 高さフォグが薄くなり始める高さ（m）。これより下は最大の濃さ
    float heightDensity = 0.5f; // 高さに対する濃さの減り方（大きいほど上で急に薄くなる）

    // --- ノイズフォグ ---
    char          noiseTexturePath[256] = {}; // ノイズテクスチャのパス
    TextureHandle noiseTextureHandle;         // 読み込んだノイズテクスチャ（実行時のみ。保存しない）

    float             noiseScale    = 0.1f;    // ワールド XZ 座標から UV への倍率（小さいほど模様が大きい）
    float             noiseStrength = 0.0f;    // ノイズでフォグを削る強さ（0 で無効）
    DirectX::XMFLOAT2 noiseSpeed{0.1f, 0.05f}; // ノイズ UV のずらし量。シェーダーは時間を掛けていないので、今は流れずに固定でずれる

    // --- スカイボックスの地平線フォグ ---
    // 空をフォグと同じ色に溶かして、地面との境目を消す。強さは color.a で決まる。
    float skyFogHeight   = 0.05f; // フォグが始まる視線の高さ（視線方向の y 成分）。下げるほど空が多く残る
    float skyFogSoftness = 0.35f; // フェードの幅。大きいほどゆるやかに空へ抜ける

    // --- リムライト ---
    // PBR 系シェーダーが、輪郭を光らせて背景から浮かせるのに使う。
    DirectX::XMFLOAT3 rimColor{1.0f, 1.0f, 1.0f}; // リムの色
    float             rimPower    = 3.0f;         // 輪郭の鋭さ（大きいほど細い）
    float             rimStrength = 0.0f;         // リムの強さ（0 で無効）

    bool enabled = true; // false ならフォグとリムライトを送らない

    // 距離フォグの基準点（ワールド座標, m）。今はどこからも書き換えず、インスペクタにも
    // 出していないので原点のまま。そのため距離フォグはカメラではなく原点からの距離で決まる。
    DirectX::XMFLOAT4 center{0.0f, 0.0f, 0.0f, 1.0f};
};
