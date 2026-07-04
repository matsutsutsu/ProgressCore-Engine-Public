#ifndef SCENE_HLSLI
#define SCENE_HLSLI

// 定数バッファは ShaderShared.h に移動（C++と共有）
#include "Engine/Graphics/Shader/Shared/ShaderShared.h"

// ノイズテクスチャ（フォグ用。FogComponent未設定時はnullバインドされる点に注意）
Texture2D gFogNoiseMap : register(REG_SRV(SLOT_SRV_FOG_NOISE));

// グローバルノイズテクスチャ(t13)。
// EnvironmentSystem のコンストラクタで Assets/Textures/Shader/Noise.png を
// 読み込み、毎フレーム無条件にバインドする（FogComponentの有無に依存しない）。
Texture2D gGlobalNoiseMap : register(REG_SRV(SLOT_SRV_GLOBAL_NOISE));
// サンプラは既存の LinearSampler (s0) を使います

#endif // SCENE_HLSLI
