// ============================================================================
// ファイル名: ResourceTypes.h
// 役割: 循環参照を防ぐための、全アセットの共有型・ハンドル定義ヘッダー
// ============================================================================
#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <string>
#include "ResourcePool.h" // Handle構造体のため

// --- 前方宣言 (インクルードしないことで爆速化と循環参照を防止) ---
class ModelResource;
class Model;
struct MaterialData;
struct GPUParticleBufferSet;
class RenderTarget;
class AnimSequence;

// ============================================================================
// [Data Structures]
// ============================================================================
// テクスチャの実体とメタデータを一つの構造体にカプセル化
struct TextureAsset {
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
    uint32_t width = 0;
    uint32_t height = 0;
    size_t byteSize = 0;
    std::string debugPath;

    TextureAsset() = default;
    TextureAsset(Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> inSrv)
        : srv(inSrv), width(0), height(0), byteSize(0), debugPath("Registered_UnknownSource") {
    }
};

// ============================================================================
// [Handle Definitions] (全システムで共有する型定義)
// ============================================================================
using TextureResource = TextureAsset;
using TextureHandle = CCL::Handle<TextureResource>;
using ModelHandle = CCL::Handle<ModelResource>;
using ModelInstanceHandle = CCL::Handle<Model>;
using MaterialHandle = CCL::Handle<MaterialData>;
using ParticleBufferHandle = CCL::Handle<GPUParticleBufferSet>;
using RenderTargetHandle = CCL::Handle<RenderTarget>;
using AnimSequenceHandle = CCL::Handle<AnimSequence>;