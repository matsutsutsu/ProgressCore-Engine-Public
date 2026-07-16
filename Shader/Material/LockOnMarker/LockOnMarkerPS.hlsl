// Shader/Material/LockOnMarker/LockOnMarkerPS.hlsl
// Category : unlit
// PSO      : blend=transparency, depth=none
//            (shaders.json: { "name":"LockOnMarker", "blend":"transparency", "depth":"none" })
// Params   : markerColor(f4 rgb+a) / radius / thickness / rotationSpeed / segments
// Textures : なし(プロシージャルなリングを描画)
// Use      : ロックオン照準リング。敵位置にカメラ向きの板ポリを置いてこれを貼る。
#include "Shader/Common/Common.hlsli"
#include "Shader/Common/MaterialEffect.hlsli"

cbuffer CbMesh : register(REG_CB(SLOT_CB_MATERIAL))
{
    float4 markerColor;
    float  radius;
    float  thickness;
    float  rotationSpeed;
    float  segments;
};

float4 main(VS_OUT pin) : SV_TARGET
{
    float2 d = pin.texcoord - 0.5f;
    float  dist = length(d);

    float ring = 1.0f - smoothstep(0.0f, thickness, abs(dist - radius));

    float seg = 1.0f;
    if (segments > 0.5f)
    {
        float angle = atan2(d.y, d.x) + GetTime() * rotationSpeed;
        float a01 = frac(angle / 6.2831853f);
        seg = step(0.5f, frac(a01 * segments));
    }

    float alpha = ring * seg * markerColor.a;
    return float4(markerColor.rgb, alpha);
}
