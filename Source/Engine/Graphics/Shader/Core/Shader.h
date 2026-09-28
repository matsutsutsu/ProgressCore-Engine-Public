#pragma once
// ===========================================================================
// 概要: 描画に使うシェーダーの基底クラス（Shader）と、メッシュごとにマテリアルを送る
//       シェーダーの基底クラス（IMaterialShader）。
//
// 責務:
//   - Begin / End でシェーダーとステートをバインドする・外す
//   - .cso のリフレクションから、定数バッファとテクスチャのスロットを求める
//   - マテリアルの値とテクスチャを、定数バッファとスロットに送る
//   - HLSL が更新されたら作り直す（CheckAndReload。対応している派生クラスだけ）
//
// 備考: 派生クラス
//         MaterialShader          マテリアル（shaders.json の materialShaders）。IMaterialShader
//         OutlineShader           輪郭線。IMaterialShader
//         ErrorShader             未登録のシェーダーの代わりに出すマゼンタ。IMaterialShader
//         ShadowMapShader         シャドウマップ（頂点シェーダーだけ）
//         NormalPrepassShader     NormalPrepass の法線
//         ParticleShader          パーティクルの描画（バッファは ParticleRenderer がバインド）
//         ParticleComputeShader   パーティクルの計算（Dispatch は GPUParticleUpdateSystem）
// ===========================================================================
#include "Engine/Graphics/RHI/RenderContext.h"
#include "Engine/Graphics/Resource/Model.h"
#include "Engine/Graphics/Shader/ShaderReflection.h"
#include "Engine/Graphics/Resource/MaterialData.h"

#include <d3d11.h>
#include <map>
#include <string>
#include <vector>
#include <filesystem>

class IMaterialShader;
class PipelineState;

class Shader
{
public:
    Shader() {}
    virtual ~Shader() {}

    /**
     * @brief シェーダーとステートをバインドする。
     * @param s renderState などを使う
     * @param ctx camera などを使う
     */
    virtual void Begin(ID3D11DeviceContext      *dc,
                       const RenderServices     &s,
                       const RenderFrameContext &ctx) = 0;

    // Begin でバインドしたものを外し、変えたステートを戻す
    virtual void End(ID3D11DeviceContext  *dc,
                     const RenderServices &s) = 0;

    // HLSL が更新されていたら作り直す（ShaderRegistry::UpdateHotReload から毎フレーム呼ばれる）
    virtual void CheckAndReload(ID3D11Device *device) {}

    // このシェーダーの PSO（ブレンド・深度・カリング）。nullptr なら各パスの既定の PSO を使う。
    // shaders.json で blend / cull / depth を指定した MaterialShader だけが返す
    virtual const PipelineState *GetPSO() const { return nullptr; }

    // IMaterialShader なら自身を、そうでなければ nullptr を返す（dynamic_cast を使わずに型を調べる）
    virtual IMaterialShader *AsMaterial() { return nullptr; }

    const ShaderReflect::ReflectionData                            &GetReflection() const { return m_reflection; }
    const std::map<std::string, ShaderReflect::AutoConstantBuffer> &GetConstantBuffers() const { return m_constantBuffers; }

    // bufferName の定数バッファの varName に value を書く（GPU に送るのは次の UpdateBuffer）
    template <typename T>
    bool SetConstantValue(const std::string &bufferName, const std::string &varName, const T &value)
    {
        auto it = m_constantBuffers.find(bufferName);
        if (it == m_constantBuffers.end()) return false;
        return it->second.SetValue(varName, value);
    }

protected:
    ShaderReflect::ReflectionData                            m_reflection;
    std::map<std::string, ShaderReflect::AutoConstantBuffer> m_constantBuffers;  // b0〜b7 の定数バッファ（名前 → バッファ）
    std::map<std::string, int>                               m_textureSlots;     // FetchTextureSlot で引いたもの
    std::map<std::string, int>                               m_samplerSlots;     // FetchSamplerSlot で引いたもの
    std::vector<int>                                         m_usedTextureSlots; // End で外すテクスチャのスロット
    std::filesystem::file_time_type                          m_lastWriteTime;

    // csoPath のリフレクションを読み、b0〜b7（SLOT_CB_SCENE 未満）の定数バッファを作る
    bool InitializeReflection(ID3D11Device *device, const std::string &csoPath);
    int  FetchTextureSlot(const std::string &name);
    int  FetchSamplerSlot(const std::string &name);
    // シェーダー・入力レイアウト・定数バッファをバインドする
    void ApplyShaderPipeline(ID3D11DeviceContext *dc, ID3D11VertexShader *vs, ID3D11PixelShader *ps, ID3D11InputLayout *layout);
    // 定数バッファ・テクスチャ・シェーダーのバインドを外す
    void UnbindResources(ID3D11DeviceContext *dc);

    /**
     * @brief mat の値を定数バッファに書いて送り、テクスチャをバインドする。
     * @param fallbackMat 上書きのマテリアルにテクスチャが無いスロットを埋めるマテリアル（モデル本来のもの。null 可）。
     *        上書きのマテリアルをシーンに保存すると、パスを持たないモデル埋め込みのテクスチャは消えるので、
     *        そのスロットはモデル本来のマテリアルのテクスチャで補う
     */
    void                            UpdateMaterialProperties(ID3D11DeviceContext *dc, const MaterialData &mat,
                                                             const MaterialData *fallbackMat = nullptr);
    std::filesystem::file_time_type GetLastWriteTime(const std::wstring &filepath);
    // リフレクションと、それから作った定数バッファ・スロットを全部消す（ホットリロードで作り直す前に呼ぶ）
    void ResetReflection();
};

// メッシュごとにマテリアルを送るシェーダー。OpaquePass / TransparentPass はこの型でないと描かない
class IMaterialShader : public Shader
{
public:
    /**
     * @brief メッシュのマテリアルを定数バッファとテクスチャに送る。
     * @param overrideMaterial nullptr なら mesh.material を使う。mesh.material は書き換えない
     */
    virtual void Update(ID3D11DeviceContext *dc,
                        const Model::Mesh   &mesh,
                        const MaterialData  *overrideMaterial = nullptr) = 0;

    IMaterialShader *AsMaterial() override { return this; }
};