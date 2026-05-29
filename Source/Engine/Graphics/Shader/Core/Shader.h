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


/**
 * @brief 全描画シェーダーの抽象基底クラス。
 *  各パスの呼び出し方：
 *  @code
 *    shader->Begin (dc, *_services, ctx);
 *    shader->Update(dc, mesh);
 *    shader->End  (dc, *_services);
 *  @endcode
 */
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
     * @brief メッシュごとのマテリアル・定数転送。
     * @param dc   デバイスコンテキスト。
     * @param mesh 描画対象メッシュ（マテリアルデータを含む）。
     */
    virtual void Update(ID3D11DeviceContext* dc,
        const Model::Mesh& mesh) = 0;

    /**
     * @brief シェーダーリソースの解除。
     * @param dc デバイスコンテキスト。
     * @param s  永続サービス（ステート復元等に使う）。
     */
    virtual void End(ID3D11DeviceContext* dc,
        const RenderServices& s) = 0;


    // ================================================================
    //  共通機能
    // ================================================================

    const ShaderReflect::ReflectionData& GetReflection() const { return _reflection; }
    const std::map<std::string, ShaderReflect::AutoConstantBuffer>& GetConstantBuffers() const { return _constantBuffers; }

    // テンプレート関数はコンパイル時に展開されるため、ヘッダーに実装を残します
    template<typename T>
    bool SetConstantValue(const std::string& bufferName, const std::string& varName, const T& value)
    {
        auto it = _constantBuffers.find(bufferName);
        if (it == _constantBuffers.end()) return false;
        return it->second.SetValue(varName, value);
    }

    virtual void CheckAndReload(ID3D11Device* device) {}

protected:
    // --- 共通機能：データメンバ ---
    ShaderReflect::ReflectionData _reflection;
    std::map<std::string, ShaderReflect::AutoConstantBuffer> _constantBuffers;
    std::map<std::string, int> _textureSlots;
    std::map<std::string, int> _samplerSlots;
    std::vector<int> _usedTextureSlots;
    std::filesystem::file_time_type _lastWriteTime;

    // --- 共通機能：メソッド宣言（実装は .cpp へ） ---
    bool InitializeReflection(ID3D11Device* device, const std::string& csoPath);
    int FetchTextureSlot(const std::string& name);
    int FetchSamplerSlot(const std::string& name);
    void ApplyShaderPipeline(ID3D11DeviceContext* dc, ID3D11VertexShader* vs, ID3D11PixelShader* ps, ID3D11InputLayout* layout);
    void UnbindResources(ID3D11DeviceContext* dc);
    void UpdateMaterialProperties(ID3D11DeviceContext* dc, const MaterialData& mat);
    std::filesystem::file_time_type GetLastWriteTime(const std::string& filepath);
    // ホットリロード時に古い情報を全て消し去るための関数
    void ResetReflection();
};