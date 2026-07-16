// TransformInterpolationSystem.h
#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Transform/Components/TransformComponent.h"

/// @file TransformInterpolationSystem.h
/// @brief 固定ステップ物理と可変フレーム描画の間を補間し、描画専用ワールド行列を確定するシステム。
///        Render Stage 先頭(R00)で実行し、カメラ・カリング・描画がこの補間結果を参照する。

/**
 * @brief prevPosition/position を alpha で線形補間し renderWorldMatrix を生成する。
 * @note Fix Your Timestep パターンの描画側後処理。alpha は RenderInterpStateComponent から取得。
 *       ロジック権威の worldMatrix には一切触れないため、物理・当たり判定への副作用はゼロ。
 * @warning R01_Camera より前に実行すること（カメラが補間後位置を追従できるようにするため）。
 */
class TransformInterpolationSystem : public CCL::ECS::IfSystem<TransformInterpolationSystem,
                                         CCL::ECS::Write<TransformComponent>>
{
  public:
    TransformInterpolationSystem()
        : IfSystem("TransformInterpolationSystem")
    {
    }
    virtual ~TransformInterpolationSystem() = default;

    void Update(float dt) override;

private:
    /// @brief 非ルート解決フェーズで使う、depth順の描画用ワールド行列の一時バッファ。
    /// @note TransformUpdateSystemの_cache.WorldAt/StoreWorldとは別領域。
    ///       Logic権威データ（cache内部の物理worldMatrix）を汚さないために独立させている。
    std::vector<DirectX::XMFLOAT4X4> _renderWorldDense;
};