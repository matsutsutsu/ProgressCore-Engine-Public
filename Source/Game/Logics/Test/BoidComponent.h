#pragma once
#include <SimpleMath.h>

/**
 * @brief Boidsシミュレーション用のコンポーネント
 * @note DODの観点から、群れ全体の共通パラメータ（重みや半径）はSystem側で一元管理し、
 * 個々のエンティティは「現在の速度ベクトル」のみを保持します。
 */
struct BoidComponent
{
    DirectX::SimpleMath::Vector3 velocity = {0.0f, 0.0f, 0.0f};
};