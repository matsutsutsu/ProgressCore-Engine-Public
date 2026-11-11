#pragma once
// 音を鳴らす要求のイベント。発行側は IAudioAPI を知らずに効果音を鳴らせる
// AudioSystem が購読し、IAudioAPI::PlayOneShot3D へ渡す
#include <cstdint>
#include <DirectXMath.h>

// 指定位置で撃ちっぱなしの 3D 効果音（銃声・足音・爆発など）を鳴らす
struct PlaySoundEvent
{
    uint32_t          eventHash;
    DirectX::XMFLOAT3 position;
};