#pragma once

// マテリアルのアルファの扱い方。glTF の alphaMode と同じ3種類
enum class AlphaMode
{
    Opaque, // 不透明
    Mask,   // alphaCutoff 未満を捨てる（切り抜き）
    Blend   // アルファブレンド
};
