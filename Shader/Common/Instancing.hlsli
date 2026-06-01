#ifndef INSTANCING_HLSLI
#define INSTANCING_HLSLI

// 構造体は ShaderShared.h に移動（C++と共有）
#include "Engine/Graphics/Shader/ShaderShared.h"

// t11 にバインドされたインスタンスバッファ
StructuredBuffer<InstanceData> g_InstanceData : register(REG_SRV(SLOT_SRV_INSTANCE));

// インスタンスIDからワールド行列を取得する関数
float4x4 GetInstanceWorld(uint instanceID)
{
    return g_InstanceData[instanceID].World;
}

// インスタンスIDからカスタムパラメータを取得する関数
float4 GetInstanceCustomParams(uint instanceID)
{
    return g_InstanceData[instanceID].customParams;
}

#endif // INSTANCING_HLSLI