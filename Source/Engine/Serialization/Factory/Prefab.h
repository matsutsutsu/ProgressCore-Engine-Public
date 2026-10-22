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
//   - 生成はその場で確定する（中で World::ScrutinyAndApply を呼ぶ）。呼び出し側は戻り値の Entity に
//     すぐ GetComponent できる。そのかわり、そのとき積まれている World 全体の予約も一緒に確定する。
//     System の処理対象の一覧には、次の境目（ロジック・描画の更新の頭）で入る（CCL_World.h の備考）。
//   - 位置・回転を指定する版は、指定を PatchComponent の予約で書く。値が入るのは次の確定のとき。
// ===========================================================================
#include "Engine/ECS/Common/CCL_Types.h"
#include "Engine/ECS/Core/CCL_World.h"
#include <DirectXMath.h>
#include <string>

namespace Prefab
{

    using namespace CCL::ECS;

    // プレハブを生成し、ルートの EntityID を返す。失敗したら InvalidEntityID。
    // 呼ぶとその場で World 全体の予約が確定する（ファイル冒頭の備考）
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

    // 読み込んだプレハブの JSON のキャッシュを捨てる（次の SpawnPrefab で読み直す）
    void ClearPrefabCache();

} // namespace Prefab