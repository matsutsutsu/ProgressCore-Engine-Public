#pragma once
// ===========================================================================
// 概要: 全アセットのハンドル型と、テクスチャの実体の型をまとめたヘッダー
//
// 責務:
//   - TextureAsset（SRV とサイズなどの情報）を定義する
//   - 各リソースの Handle<T> の別名を定義する
//
// 備考: ハンドルだけが要るファイル（コンポーネントなど）は、ResourceManager.h ではなく
//       これを include する。実体の型は前方宣言だけにして、循環 include とビルド時間の増加を防ぐ
// ===========================================================================
#include <d3d11.h>
#include <wrl/client.h>
#include <string>
#include "ResourcePool.h"

class ModelResource;
class Model;
struct MaterialData;
struct GPUParticleBufferSet;
class RenderTarget;
struct AnimSequence;

// テクスチャの実体と、その情報
struct TextureAsset
{
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
    uint32_t                                         width    = 0; // 幅（px）
    uint32_t                                         height   = 0; // 高さ（px）
    size_t                                           byteSize = 0; // VRAM 使用量の見積もり（バイト。ミップ込み）
    std::string                                      debugPath;    // 読み込み元のパス（メモリ分析ウィンドウの表示用）

    TextureAsset() = default;

    // ファイルを通さずに作った SRV を登録するとき用（ResourceManager::RegisterTexture）
    TextureAsset(Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> inSrv)
        : srv(inSrv), width(0), height(0), byteSize(0), debugPath("Registered_UnknownSource")
    {
    }
};

// ============================================================================
// ハンドル型
// ============================================================================
using TextureResource      = TextureAsset;
using TextureHandle        = CCL::Handle<TextureResource>;
using ModelHandle          = CCL::Handle<ModelResource>;
using ModelInstanceHandle  = CCL::Handle<Model>;
using MaterialHandle       = CCL::Handle<MaterialData>;
using ParticleBufferHandle = CCL::Handle<GPUParticleBufferSet>;
using RenderTargetHandle   = CCL::Handle<RenderTarget>;
using AnimSequenceHandle   = CCL::Handle<AnimSequence>;
