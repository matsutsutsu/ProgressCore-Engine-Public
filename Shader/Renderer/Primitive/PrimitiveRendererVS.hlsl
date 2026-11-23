// ===========================================================================
// 概要: 単純な形（PrimitiveRenderer）の頂点シェーダー。インスタンスの行列で置き、カプセルは形を伸ばす
//
// 入力: 形のメッシュの頂点（位置・法線）とインスタンスの番号
// 出力: 画面の位置・色（customParams）・ワールド空間の法線
//
// 備考: カプセルのときは、C++ がワールド行列の使っていない欄（4列目の上3つ）に値を入れて渡す
//         world[0].w … カプセルなら 1
//         world[1].w … 半径
//         world[2].w … 円柱の部分の長さ
//       カプセルのメッシュは、長さ 1 の円柱の上下に半球を付けた形。上の半球・円柱・下の半球に分けて伸ばす
// ===========================================================================
#include "Shader/Renderer/Primitive/PrimitiveRenderer.hlsli"

VS_OUT main(VS_IN vin, uint instanceID : SV_InstanceID)
{
    // 処理フロー:
    // 1. 行列に入れたカプセルの値を取り出し、行列から消す
    // 2. カプセルなら、半径と長さに合わせて頂点を動かす
    // 3. ワールドと画面へ移す

    VS_OUT vout;

    float4x4 world = GetInstanceWorld(instanceID);
    float4 instanceColor = GetInstanceCustomParams(instanceID);

    // ---------------------------------------------------------
    // 1. カプセルの値
    // ---------------------------------------------------------

    bool isCapsule = (world[0].w > 0.5f);

    float radius = 1.0f;
    float cylinderHeight = 1.0f;

    if (isCapsule)
    {
        radius = max(world[1].w, 0.0001f);
        cylinderHeight = max(world[2].w, 0.0f);
    }

    // 行列として掛ける前に 0 へ戻す。残すと位置の w が変わり、変換が崩れる
    world[0].w = 0.0f;
    world[1].w = 0.0f;
    world[2].w = 0.0f;

    float3 vPos = vin.position.xyz;
    float3 vNormal = vin.normal.xyz;

    // ---------------------------------------------------------
    // 2. カプセルを伸ばす
    // ---------------------------------------------------------

    // カプセルの行列には大きさを入れていない（大きさはここで頂点に掛ける）
    // そのため、半径が 0 に近くても行列が潰れず、回転が壊れない
    if (isCapsule)
    {
        float halfHeight = cylinderHeight * 0.5f;

        if (vPos.y > 0.51f)
        {
            // 上の半球: 半径で縮め、円柱の上端へ移す
            vPos = (vPos - float3(0.0f, 0.5f, 0.0f)) * radius + float3(0.0f, halfHeight, 0.0f);
        }
        else if (vPos.y < -0.51f)
        {
            // 下の半球
            vPos = (vPos - float3(0.0f, -0.5f, 0.0f)) * radius + float3(0.0f, -halfHeight, 0.0f);
        }
        else
        {
            // 円柱: 横は半径、縦は長さに合わせる。法線は横向きのまま
            vPos.x *= radius;
            vPos.z *= radius;
            vPos.y = (vPos.y + 0.5f) * cylinderHeight - halfHeight;
            vNormal.y = 0.0f;
        }
    }

    // ---------------------------------------------------------
    // 3. ワールドと画面
    // ---------------------------------------------------------

    float4 worldPos = mul(float4(vPos, 1.0f), world);
    vout.position = mul(worldPos, viewProjection);

    // 法線。大きさ 0 の行列で長さが 0 になったら、上向きにしておく
    float3 wNormal = mul(float4(vNormal, 0.0f), world).xyz;
    float len = length(wNormal);
    if (len < 0.00001f)
    {
        vout.worldNormal = float3(0.0f, 1.0f, 0.0f);
    }
    else
    {
        vout.worldNormal = wNormal / len;
    }

    vout.color = instanceColor;

    return vout;
}
