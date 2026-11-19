#ifndef SCENE_HLSLI
#define SCENE_HLSLI

// CbScene などの cbuffer は ShaderShared.h で C++ と共有している
#include "Engine/Graphics/Shader/Shared/ShaderShared.h"

// どのシェーダーからも読める共通のノイズ（R だけ使う）
// EnvironmentSystem が起動時に読み込み、毎フレーム置く。フォグの設定がないシーンでも置かれている
Texture2D gGlobalNoiseMap : register(REG_SRV(SLOT_SRV_GLOBAL_NOISE));

#endif // SCENE_HLSLI
