#include "Shader/Renderer/Primitive/PrimitiveRenderer.hlsli"

VS_OUT main(VS_IN vin, uint instanceID : SV_InstanceID)
{
    VS_OUT vout;
    
    // 1. インスタンスバッファ(t11)から、自分の番号の「ワールド行列」と「色」を取り出す
    // ※C++側で customParams に color を入れて渡しています
    float4x4 world = GetInstanceWorld(instanceID);
    float4 instanceColor = GetInstanceCustomParams(instanceID);

    // 2. 座標変換 (ローカル座標 → ワールド座標 → 画面座標)
    // 1x1のサイズの頂点に、スケールが焼き込まれたワールド行列を掛ける
    float4 worldPos = mul(vin.position, world);
    
    // Scene.hlsli で定義されている viewProjection (CbScene) を掛ける
    vout.position = mul(worldPos, viewProjection);
    
    
    // =========================================================
    // ★防弾：法線のゼロ除算（NaN爆弾）を完全に防ぐ
    // =========================================================
    float3 wNormal = mul(float4(vin.normal, 0.0f), world).xyz;
    float len = length(wNormal);
    
    if (len < 0.00001f)
    {
        // スケールが0等で長さが消失した場合は、ダミーの法線を上書きしてNaNを回避する
        vout.worldNormal = float3(0.0f, 1.0f, 0.0f);
    }
    else
    {
        // 安全に正規化
        vout.worldNormal = wNormal / len;
    }
    
    vout.color = instanceColor;

    return vout;
}