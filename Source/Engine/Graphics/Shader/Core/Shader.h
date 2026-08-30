#pragma once

#include "Engine/Graphics/RHI/RenderContext.h"
#include "Engine/Graphics/Resource/Model.h"
#include "Engine/Graphics/Shader/ShaderReflection.h"
#include "Engine/Graphics/Resource/MaterialData.h"

#include <d3d11.h>
#include <map>
#include <string>
#include <vector>
#include <filesystem>

class IMaterialShader; // 前方宣言（AsMaterial の戻り値型に使う）
class PipelineState;   // GetPSO の戻り値（ポインタなので前方宣言で足りる

// ============================================================================
// Shader : 全描画シェーダーの基底（パイプライン制御層）
// ----------------------------------------------------------------------------
//  役割:
//   1. HotReload   : HLSL が更新されたら再コンパイル（CheckAndReload）
//   2. Reflection  : 定数バッファ/テクスチャの要求スロットを自動解析（m_reflection）
//   3. PipelineCtrl: Begin / End で描画パイプラインの状態を管理
//
//  「メッシュ単位のマテリアル転送（Update）」が必要なものは、この基底ではなく
//  IMaterialShader を継承すること（MaterialShader のみ該当）。
//
//  派生クラスと種別:
//   MaterialShader        … マテリアルシェーダー（Update を持つ → IMaterialShader）
//   ShadowMapShader       … パイプラインのみ（Update 不要）
//   ParticleShader        … パイプラインのみ（バッファは ParticleRenderer が直接バインド）
//   ParticleComputeShader … パイプラインのみ（Dispatch はシステム側）
// ============================================================================
class Shader
{
public:
    Shader() {}
    virtual ~Shader() {}

    /**
      * @brief シェーダー・ステートのバインド。
      * @param dc  デバイスコンテキスト。
      * @param s   永続サービス（renderState, lightManager 等の参照に使う）。
      * @param ctx フレームコンテキスト（camera 等）。
      */
    virtual void Begin(ID3D11DeviceContext* dc,
        const RenderServices& s,
        const RenderFrameContext& ctx) = 0;

    /**
     * @brief シェーダーリソースの解除。
     * @param dc デバイスコンテキスト。
     * @param s  永続サービス（ステート復元等に使う）。
     */
    virtual void End(ID3D11DeviceContext* dc,
        const RenderServices& s) = 0;

    virtual void CheckAndReload(ID3D11Device* device) {}

     /**
     * @brief このシェーダー固有の PSO（ブレンド/深度/カリング）。
     * @return nullptr の場合、各パスのデフォルト PSO が使われる（後方互換）。
     *         MaterialShader が JSON 由来の PSO を返す。
     */
    virtual const PipelineState* GetPSO() const { return nullptr; }

    /**
     * @brief 安全なダウンキャスト（RTTI 不要）。
     *        マテリアル転送が可能なシェーダーのみ非 null を返す。
     *        IMaterialShader 側で override して this を返す。
     */
    virtual IMaterialShader* AsMaterial() { return nullptr; }

    // ================================================================
    //  共通機能
    // ================================================================

    const ShaderReflect::ReflectionData& GetReflection() const { return m_reflection; }
    const std::map<std::string, ShaderReflect::AutoConstantBuffer>& GetConstantBuffers() const { return m_constantBuffers; }

    // テンプレート関数はコンパイル時に展開されるため、ヘッダーに実装を残します
    template<typename T>
    bool SetConstantValue(const std::string& bufferName, const std::string& varName, const T& value)
    {
        auto it = m_constantBuffers.find(bufferName);
        if (it == m_constantBuffers.end()) return false;
        return it->second.SetValue(varName, value);
    }


protected:
    // --- 共通機能：データメンバ ---
    ShaderReflect::ReflectionData m_reflection;
    std::map<std::string, ShaderReflect::AutoConstantBuffer> m_constantBuffers;
    std::map<std::string, int> m_textureSlots;
    std::map<std::string, int> m_samplerSlots;
    std::vector<int> m_usedTextureSlots;
    std::filesystem::file_time_type m_lastWriteTime;

    // --- 共通機能：メソッド宣言（実装は .cpp へ） ---
    bool InitializeReflection(ID3D11Device* device, const std::string& csoPath);
    int FetchTextureSlot(const std::string& name);
    int FetchSamplerSlot(const std::string& name);
    void ApplyShaderPipeline(ID3D11DeviceContext* dc, ID3D11VertexShader* vs, ID3D11PixelShader* ps, ID3D11InputLayout* layout);
    void UnbindResources(ID3D11DeviceContext* dc);
    // fallbackMat: override 材質がテクスチャスロットを持たない場合の代替（通常はメッシュ本来の材質）。
    //   → override をシーン保存すると assetPath 無しのモデル埋め込みテクスチャは失われるため、
    //     そのスロットは fallbackMat（生きているモデル材質）のテクスチャで補う。null 可。
    void UpdateMaterialProperties(ID3D11DeviceContext* dc, const MaterialData& mat,
                                  const MaterialData* fallbackMat = nullptr);
    std::filesystem::file_time_type GetLastWriteTime(const std::wstring& filepath);
    // ホットリロード時に古い情報を全て消し去るための関数
    void ResetReflection();
};


// ============================================================================
// IMaterialShader : メッシュ単位のマテリアル転送を持つシェーダー
// ----------------------------------------------------------------------------
//  OpaquePass / TransparentPass はこの型を要求する。
//  Shader::AsMaterial() を override して this を返すことで、
//  RTTI なしで安全に Shader* → IMaterialShader* のダウンキャストができる。
// ============================================================================
class IMaterialShader : public Shader
{
public:
    /**
     * @brief メッシュごとのマテリアル・定数転送。
     * @param overrideMaterial nullptr なら mesh.material を使用する。
     *        mesh.material は書き換えない（アセット汚染防止）。
     */
    virtual void Update(ID3D11DeviceContext* dc,
        const Model::Mesh& mesh,
        const MaterialData* overrideMaterial = nullptr) = 0;

    IMaterialShader* AsMaterial() override { return this; }
};