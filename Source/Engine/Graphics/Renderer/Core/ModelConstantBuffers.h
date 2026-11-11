#pragma once
// ===========================================================================
// 概要: モデルの描画で使う定数バッファ（CbSkeleton と CbScene）をバインドするクラス
//
// 責務:
//   - CbSkeleton（b12）を持ち、メッシュのボーン行列を書き込む
//   - CbScene（b8。Graphics が持つ）と CbSkeleton を頂点・ピクセルシェーダーにバインドする
//
// 備考: 描画（ドローコール）はしない。各 RenderPass が呼ぶ
// @code
//    // 描画パスから（スキンメッシュのボーン行列を送る）
//    modelConstants->UpdateSkeletonConstants(dc, mesh);
//
//    // フレームで共通の定数バッファをまとめてバインドする
//    modelConstants->BindAllGlobalConstantBuffers(dc);
// @endcode
// ===========================================================================
#include <wrl.h>
#include <d3d11.h>
#include <DirectXMath.h>
#include "Engine/Graphics/Resource/Model.h"
#include "Engine/Graphics/Shader/Shared/ShaderShared.h"

class ModelConstantBuffers
{
public:
    // CbSkeleton のバッファを作る
    explicit ModelConstantBuffers(ID3D11Device *device);
    ~ModelConstantBuffers() = default;

    ModelConstantBuffers(const ModelConstantBuffers &)            = delete;
    ModelConstantBuffers &operator=(const ModelConstantBuffers &) = delete;

    // Graphics が持つ CbScene のバッファを受け取る（Graphics の初期化で1回だけ呼ぶ）
    void SetSceneConstantBuffer(ID3D11Buffer *sceneCB) noexcept { m_sceneCB = sceneCB; }

    // CbScene と CbSkeleton をバインドする（EnvironmentSystem と NormalPrepass から呼ばれる）
    void BindAllGlobalConstantBuffers(ID3D11DeviceContext *dc);

    // メッシュのボーン行列を CbSkeleton に書き、頂点シェーダーにバインドする
    // ボーンがなければ、メッシュのノードのワールド行列を 0 番に入れる
    void UpdateSkeletonConstants(ID3D11DeviceContext *dc, const Model::Mesh &mesh);

private:
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_cbSkeletonBuffer;  // CbSkeleton（b12）
    ID3D11Buffer                        *m_sceneCB = nullptr; // CbScene（b8。Graphics が持つ。所有しない）

    struct CbSkeleton
    {
        DirectX::XMFLOAT4X4 boneTransforms[MAX_BONES]; // オフセット行列 × ボーンのワールド行列
    };
    CbSkeleton m_cbSkeletonCache = {}; // GPU に送る前の CPU 側の写し
};
