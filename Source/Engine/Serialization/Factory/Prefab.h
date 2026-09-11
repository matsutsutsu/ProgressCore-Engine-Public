#pragma once
#include "Engine/ECS/Common/CCL_Types.h"
#include "Engine/ECS/Core/CCL_World.h"
#include <DirectXMath.h>
#include <d3d11.h>
#include <string>
#include <unordered_map>

namespace Prefab {

    using namespace CCL::ECS;

    // 終了処理
    void Cleanup();

    // プレハブ JSON のパース結果キャッシュを捨てる。
    //   SpawnPrefab は同じプレハブの再生成が速くなるよう、検証済みの entities を
    //   パスごとに保持している。シーンを切り替えるときなど、持ち越したくない
    //   タイミングで呼ぶ（Cleanup からも呼ばれる）。
    void ClearPrefabCache();

    // 初期化 (デバイス保持など)
    void Initialize(ID3D11Device *device);

    // =========================================================================
    // 新システム: IDではなくファイルパスで生成する
    // =========================================================================

    // JSONからプレハブを生成 (座標指定なし)
    EntityID SpawnPrefab(World &world, const std::string &path);

    // 座標指定で生成
    EntityID SpawnPrefab(
        World &world, const std::string &path, const DirectX::XMFLOAT3 &position);

    // 座標・回転(Quaternion)指定で生成
    EntityID SpawnPrefab(World &world,
        const std::string            &path,
        const DirectX::XMFLOAT3      &position,
        const DirectX::XMFLOAT4      &rotation);

    // 座標・Y軸回転(度数法)指定で生成
    EntityID SpawnPrefab(World &world,
        const std::string            &path,
        const DirectX::XMFLOAT3      &position,
        float                         rotationY);

} // namespace CCL::Prefab