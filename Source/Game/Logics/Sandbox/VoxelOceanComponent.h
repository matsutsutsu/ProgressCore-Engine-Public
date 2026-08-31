#pragma once

/**
 * @brief ボクセルオーシャン用のコンポーネント
 * @note DODの観点から、波のパラメータはSystem側で一元管理するため、
 * このコンポーネントは「海の一部である」という目印（タグ）としてのみ機能する。
 */
struct VoxelOceanComponent
{
    // データなし。Archetypeの識別用タグとして利用する。
    // ※エンジン仕様で空構造体がエラーになる場合は、ダミーで bool isActive = true; などを入れる。
};