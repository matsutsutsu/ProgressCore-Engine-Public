#pragma once
// ===========================================================================
// 概要: 音を鳴らす要求を EventBus で送るためのイベント定義。
//
// 責務:
//   - 発行側が IAudioAPI を知らずに効果音を鳴らせるようにする
//
// 備考: AudioSystem が購読し、IAudioAPI::PlayOneShot3D へ渡す。
// ===========================================================================
#include <cstdint>
#include <DirectXMath.h>

// 指定位置で撃ちっぱなしの 3D 効果音（銃声・足音・爆発など）を鳴らす
struct PlaySoundEvent
{
    uint32_t          eventHash;
    DirectX::XMFLOAT3 position;
};