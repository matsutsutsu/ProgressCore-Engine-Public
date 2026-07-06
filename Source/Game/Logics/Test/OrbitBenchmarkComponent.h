#pragma once

/**
 * @brief ECS限界テスト（ギャラクシー・スウォーム）用コンポーネント
 * @note DOD最適化のため、共通パラメータ(速度など)はSystemで管理し、個体固有の不変データのみを保持します。
 */
struct OrbitBenchmarkComponent
{
    float phaseOffset; // 個体ごとの初期角度（ズレ）
    float radius;      // 中心からの軌道半径
};