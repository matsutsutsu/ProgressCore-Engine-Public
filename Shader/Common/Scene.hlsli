#ifndef SCENE_HLSLI
#define SCENE_HLSLI

// 定数バッファは ShaderShared.h に移動（C++と共有）
#include "Engine/Graphics/Shader/Shared/ShaderShared.h"

// ノイズテクスチャ 
Texture2D gFogNoiseMap : register(REG_SRV(SLOT_SRV_FOG_NOISE));
// サンプラは既存の LinearSampler (s0) を使います

#endif // SCENE_HLSLI