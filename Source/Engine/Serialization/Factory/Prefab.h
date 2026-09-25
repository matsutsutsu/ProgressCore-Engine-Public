#pragma once
// ===========================================================================
// 概要: プレハブ（JSON に保存した Entity の階層）をファイルパスから生成する関数群。
//
// 責務:
//   - プレハブファイルから Entity の階層を生成し、ルートの EntityID を返す
//   - 位置・回転を指定して生成する
//   - 読み込んだ JSON をパスごとにキャッシュする
//
// 備考:
//   - 生成だけを行い、破棄には関わらない（DestroyTag を起点に HierarchyCleanupSystem が階層ごと消す）。
//   - キャッシュはアプリ終了まで持つ。エディタ構成ではファイルの更新時刻が変わったら読み直す。
// ===========================================================================
#include "Engine/ECS/Common/CCL_Types.h"
#include "Engine/ECS/Core/CCL_World.h"
#include <DirectXMath.h>
#include <string>

namespace Prefab
{

    using namespace CCL::ECS;

    // プレハブを生成し、ルートの EntityID を返す。失敗したら InvalidEntityID
    EntityID SpawnPrefab(World &world, const std::string &path);

    // 位置を指定して生成する
    EntityID SpawnPrefab(
        World &world, const std::string &path, const DirectX::XMFLOAT3 &position);

    // 位置と回転（クォータニオン）を指定して生成する
    EntityID SpawnPrefab(World                   &world,
                         const std::string       &path,
                         const DirectX::XMFLOAT3 &position,
                         const DirectX::XMFLOAT4 &rotation);

    // 位置と Y 軸回転（度）を指定して生成する
    EntityID SpawnPrefab(World                   &world,
                         const std::string       &path,
                         const DirectX::XMFLOAT3 &position,
                         float                    rotationY);

} // namespace Prefab