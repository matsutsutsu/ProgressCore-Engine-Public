#pragma once
// ===========================================================================
// 概要: フォグ・IBL・グローバルノイズなど、シーン全体で共通の描画リソースを
//       GPU にバインドする System
//
// 責務:
//   - フォグ定数バッファ（CbFog）を作り、FogComponent から毎フレーム書き込んでバインドする
//   - IBL テクスチャ（IEM・PMREM・BRDF LUT）とグローバルノイズをバインドする
//   - SkyboxComponent のパスからスカイボックスと IBL を読み込む（なければ初期設定を使う）
//   - CbScene / CbSkeleton をパイプラインにバインドする
//
// 備考: CbScene の中身を書くのは Graphics::Update だけで、ここはバインドだけする
//       ここで書くと、Graphics::Update が書いた行列を古い値で上書きする
//       空と IBL の既定は .cpp の定数に置き、シーンごとに変えるときは SkyboxComponent を置く
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include <d3d11.h>
#include <vector>
#include <wrl.h>
#include <DirectXMath.h>

struct RenderServices;

class EnvironmentSystem : public CCL::ECS::SystemBase
{
public:
    EnvironmentSystem() : SystemBase("EnvironmentSystem") {}
    virtual ~EnvironmentSystem() = default;

    // テクスチャと定数バッファを用意する（登録直後に SystemManager が呼ぶ）
    void Initialize() override;

    std::vector<CCL::ECS::TypeID> GetReadTypes() const override;
    void                          Update(float dt) override;

    // フォグ・IBL・ノイズと CbScene を GPU にバインドする。CbScene の中身は書かない（書くのは Graphics::Update）
    void BindGlobalResources(ID3D11DeviceContext *dc, const RenderServices &s);

private:
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_fogConstantBuffer; // CbFog

    // 既定のテクスチャ。SkyboxComponent がないか、パスが空・読み込み失敗のときに使う
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_globalNoiseSRV;   // グローバルノイズ
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_irradianceMapSRV; // 既定の IEM
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_prefilterMapSRV;  // 既定の PMREM
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_brdfLutSRV;       // BRDF LUT（空に依存しないので差し替えない）
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_defaultSkyboxSRV; // 既定のスカイボックス背景
};
