#pragma once

namespace ShaderResources {
    // HLSL側の RegisterDef.hlsli と一致させること！

    // -----------------------------------------------------------------
    // Constant Buffers (b)
    // -----------------------------------------------------------------
    // 通常描画用
    constexpr int CbMaterial = 0;
    constexpr int CbMesh     = 1;

    // ポストプロセス用 (CbMaterial, CbMesh と場所を共有する)
    constexpr int CbBloom   = 0;
    constexpr int CbToneMap = 1;

    // システム共通 (b8以降)
    constexpr int CbScene    = 8;
    constexpr int CbLight    = 9;
    constexpr int CbShadow   = 10;
    constexpr int CbFog      = 11;
    constexpr int CbSkeleton = 12;
    constexpr int CbParticle = 13;

    // -----------------------------------------------------------------
    // Shader Resources (t)
    // -----------------------------------------------------------------
    // マテリアル・パス固有
    constexpr int SrvMaterialTex0 = 0; // Diffuse
    constexpr int SrvMaterialTex1 = 1; // Normal
    constexpr int SrvMaterialTex2 = 2; // Specular
    constexpr int SrvParticleBuf  = 3; // Particle Buffer

    // シーンの深度バッファを受け取るスロット
    constexpr int SrvSceneDepth = 4;

    // ポストプロセス用
    constexpr int SrvBloomInput   = 0; // SrvMaterialTex0 と同じ
    constexpr int SrvBloomHighRes = 1; // SrvMaterialTex1 と同じ

    // フォグ用
    constexpr int SrvFogNoise = 9;

    // システム共通
    constexpr int SrvShadow   = 10;
    constexpr int SrvInstance = 11;
    constexpr int SrvBone     = 12;

    // ==========================================
    // グローバル環境ノイズ (ディゾルブ用など)
    // ==========================================
    constexpr int SrvGlobalNoise = 13;

    // -----------------------------------------------------------------
    // Unordered Access Views (u)
    // -----------------------------------------------------------------
    constexpr int UavParticleBuf = 0;

    // -----------------------------------------------------------------
    // Samplers (s)
    // -----------------------------------------------------------------
    constexpr int SmpDefault     = 0;
    constexpr int SmpLinear      = 1;
    constexpr int SmpAnisotropic = 2;
    constexpr int SmpShadow      = 10;

    // Bloom用サンプラー (通常はLinearを使うので1番を割り当て)
    constexpr int SmpBloom = 1;

} // namespace ShaderResources