#ifndef INSTANCING_HLSLI
#define INSTANCING_HLSLI

// InstanceData は ShaderShared.h で C++ と共有している
#include "Engine/Graphics/Shader/Shared/ShaderShared.h"

// インスタンスごとの値。描画パスがバッチごとに詰め直して置く
StructuredBuffer<InstanceData> g_InstanceData : register(REG_SRV(SLOT_SRV_INSTANCE));

// instanceID 番目のインスタンスのワールド行列
float4x4 GetInstanceWorld(uint instanceID)
{
    return g_InstanceData[instanceID].World;
}

// instanceID 番目のインスタンスの汎用の値（使い方はマテリアルごとに決める）
float4 GetInstanceCustomParams(uint instanceID)
{
    return g_InstanceData[instanceID].customParams;
}

#endif // INSTANCING_HLSLI