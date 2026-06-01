#pragma once
#include "Engine/ECS/System/CCL_System.h"
#include "Engine/Graphics/RHI/RenderContext.h"  // RenderServices
#include <d3d11.h>
#include "Engine/Graphics/Shader/ShaderShared.h"
#include <vector>
#include <wrl.h>
#include <DirectXMath.h>

/**
 * @brief フォグ・IBL・グローバルノイズ等の「環境」リソースを GPU にバインドする舞台監督。
 *
 * @details
 *  【責務】
 *   - フォグ定数バッファ（CbFog）の管理・バインド
 *   - IBL テクスチャ（Irradiance / PrefilterMap / BRDF LUT）のバインド
 *   - グローバルノイズテクスチャのバインド
 *   - CbScene の「バインド」（BindAllGlobalConstantBuffers 経由）
 *
 *  【責務外・バグ修正】
 *   - CbScene の「内容更新」は Graphics::Update() が唯一の権限を持つ。
 *     EnvironmentSystem は UpdateSubresource で CbScene を上書きしない。
 *     （上書きすると Graphics::Update の正しい行列データが破壊される）
 */
class EnvironmentSystem : public CCL::ECS::SystemBase
{
public:
    EnvironmentSystem();
    virtual ~EnvironmentSystem() = default;

    std::vector<CCL::ECS::TypeID> GetReadTypes() const override;
    void Update(float dt) override;

    /**
     * @brief フォグ・IBL・ノイズを GPU にバインドし、CbScene も pipeline にバインドする。
     * @param dc デバイスコンテキスト。
     * @param s  永続サービス（renderState, sceneConstantBuffer 等を参照）。
     * @note  CbScene の内容更新（UpdateSubresource）は行わない。
     *        Graphics::Update() で更新済みのバッファをそのままバインドするだけ。
     */
    void BindGlobalResources(ID3D11DeviceContext* dc, const RenderServices& s);

private:

    Microsoft::WRL::ComPtr<ID3D11Buffer> _fogConstantBuffer;

    // グローバルテクスチャ
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> _globalNoiseSRV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> _irradianceMapSRV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> _prefilterMapSRV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> _brdfLutSRV;
};