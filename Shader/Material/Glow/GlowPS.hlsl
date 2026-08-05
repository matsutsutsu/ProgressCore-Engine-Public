// Shader/Material/Glow/GlowPS.hlsl
//
// 加算発光(Glow)マテリアル用のピクセルシェーダー。
// ライティング計算を一切行わない Unlit。テクスチャ(輝度マスク)×色×強度を
// そのまま出力し、実際の「加算合成」は C++側の PSO (BlendState::Additive) が行う。
// このシェーダー自身は普通に色とアルファを計算するだけで良い
// （前回の particle_ps.hlsl と同じ設計方針：加算/半透明の切り替えは PSO の仕事、
//   シェーダーは色計算に徹する）。
// BlendState::Additive は SrcBlend=SRC_ALPHA なので、出力アルファが
//    そのまま光の強さの倍率になる点に注意（下記 main 内コメント参照）。
#include "Shader/Common/Common.hlsli"

// ==============================================================================
// 定数バッファ & リソース
//   変数名がそのまま MaterialData の nameHash キーになる
//     (SetColor("glowColor"_hash, ...) / SetScalar("intensity"_hash, ...) と対応)
// ==============================================================================
cbuffer CbMesh : register(REG_CB(SLOT_CB_MATERIAL))
{
    float4 glowColor;       // rgb: 発光色（aはこのシェーダーでは未使用。強さはintensityで調整）
    float  intensity;       // 発光の強さ（1.0が基準。2〜5程度で強く光らせる）
    float  useEmissiveMap;  // C++側から送られてくる「テクスチャの有無」フラグ
    float2 _pad;            // 16byte境界合わせ
};

Texture2D EmissiveMap : register(REG_SRV(SLOT_SRV_MAT_TEX0));
SamplerState LinearSampler : register(REG_SMP(SLOT_SMP_LINEAR));

float4 main(VS_OUT pin) : SV_TARGET
{
    // テクスチャが無ければ白（=グローカラーそのまま光る）として扱う
    float4 tex = useEmissiveMap > 0.5f
        ? EmissiveMap.Sample(LinearSampler, pin.texcoord)
        : float4(1.0f, 1.0f, 1.0f, 1.0f);

    float3 finalColor = glowColor.rgb * tex.rgb * intensity;

    // 重要: 加算合成(BlendState::Additive)は SrcBlend=SRC_ALPHA, DestBlend=ONE。
    //         つまり出力アルファがそのまま「光の強さの倍率」として効く（ONE,ONEではない）。
    //         テクスチャのアルファをそのまま返すと、輝度マスクとしてソフトな縁の
    //         フェードアウトが自然にでき、テクスチャ無し(useEmissiveMap=0)なら
    //         常に1.0＝フル強度で光る。
    return float4(finalColor, tex.a);
}
