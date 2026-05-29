#pragma once
#include "Engine/ECS/System/CCL_System.h"
#include "Engine/Graphics/RHI/RenderContext.h"  // RenderServices（BindGlobalResources 引数）
#include <d3d11.h>
#include <vector>
#include <wrl.h>
#include <DirectXMath.h>

/**
 * @brief カメラ・ライト・フォグ・IBL 等、環境全体のセットアップを行う舞台監督システム。
 * @details
 *  BindGlobalResources の引数に RenderServices を追加したことで
 *  Graphics::Instance() への依存をレンダー時に除去した。
 *  コンストラクタでの device 取得のみ残存（ECS 構造上の制約）。
 */
class EnvironmentSystem : public CCL::ECS::SystemBase
{
public:
    EnvironmentSystem();
    virtual ~EnvironmentSystem() = default;

    std::vector<CCL::ECS::TypeID> GetReadTypes() const override;
    void Update(float dt) override;

    /**
     * @brief 描画ループの直前で呼び出し、各種リソースを GPU にバインドする。
     * @param dc  デバイスコンテキスト。
     * @param s   永続サービス（sceneConstantBuffer, renderState 等を参照する）。
     *            呼び出し元: Graphics::Instance().GetModelRenderer()->GetServices()
     */
    void BindGlobalResources(ID3D11DeviceContext* dc, const RenderServices& s);

private:
    struct CbSceneData {
        DirectX::XMFLOAT4X4 viewProjection;
        DirectX::XMFLOAT4   lightDirection;
        DirectX::XMFLOAT4   lightColor;
        DirectX::XMFLOAT4   cameraPosition;
        DirectX::XMFLOAT4X4 view;
        DirectX::XMFLOAT4X4 projection;
    };
    CbSceneData _sceneData = {};

    struct CbFogData {
        DirectX::XMFLOAT4 color;
        DirectX::XMFLOAT4 params;
        DirectX::XMFLOAT4 center;
        DirectX::XMFLOAT4 noiseParams;
        DirectX::XMFLOAT4 rimColor;
        DirectX::XMFLOAT4 rimParams;
    };

    Microsoft::WRL::ComPtr<ID3D11Buffer> _fogConstantBuffer;
    ID3D11ShaderResourceView* _currentFogNoiseSRV = nullptr;

    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> _globalNoiseSRV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> _irradianceMapSRV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> _prefilterMapSRV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> _brdfLutSRV;
};