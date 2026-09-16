#pragma once
#include <DirectXMath.h>
#include "Engine/Graphics/Resource/ResourceTypes.h"

struct FogComponent
{
    DirectX::XMFLOAT4 color{0.7f, 0.7f, 0.8f, 1.0f};

    // --- 距離フォグ ---
    float start = 10.0f;
    float end   = 50.0f;

    // --- 高さフォグ ---
    float heightStart   = 0.0f; // 霧が発生する地面の高さ
    float heightDensity = 0.5f; // 高さによる霧の濃さ

    // --- ノイズフォグ  ---
    char noiseTexturePath[256] = {}; // テクスチャパス

    TextureHandle noiseTextureHandle;

    float             noiseScale    = 0.1f;    // ノイズの粗さ (UVスケール)
    float             noiseStrength = 0.0f;    // ノイズの影響度 (0.0=無効)
    DirectX::XMFLOAT2 noiseSpeed{0.1f, 0.05f}; // 流れる速度

    // --- スカイボックス地平線フォグ ---
    // 空(虚空)を距離フォグと同じ色に溶かして境目を消す。強度は color.a と連動。
    float skyFogHeight   = 0.05f; // フォグ開始の視線高(dir.y)。下げるほど空が広く残る
    float skyFogSoftness = 0.35f; // フェード幅。大きいほど緩やかに空へ抜ける

    // --- リムライト (Atmosphere Rim) ---
    // キャラクターやオブジェクトの輪郭を光らせ、背景との分離を良くします
    DirectX::XMFLOAT3 rimColor{1.0f, 1.0f, 1.0f}; // 発光色
    float             rimPower    = 3.0f;         // 輪郭の鋭さ (高いほど細い)
    float             rimStrength = 0.0f;         // 発光強度 (0.0で無効)

    bool              enabled = true;
    DirectX::XMFLOAT4 center{0.0f, 0.0f, 0.0f, 1.0f}; // プレイヤー中心座標
};