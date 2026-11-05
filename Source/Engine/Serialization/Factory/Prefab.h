#pragma once
// ===========================================================================
// 概要: プレハブ（JSON に保存した Entity の階層）をファイルパスから生成する関数群
//
// 責務:
//   - プレハブファイルから Entity の階層を生成し、ルートの EntityID を返す
//   - 位置・回転を指定して生成する
//   - 読み込んだ JSON をパスごとにキャッシュする
//   - 生成せずに JSON と中のテクスチャだけを先読みする（Preload）
//
// 備考: 生成するだけで、破棄は HierarchyCleanupSystem が DestroyTag から階層ごと消す
//       生成はその場で確定する（中で World::ScrutinyAndApply を呼ぶ）ので、戻り値にすぐ GetComponent できる
//       そのかわり、そのとき積まれている World 全体の予約も一緒に確定する
// ===========================================================================
#include "Engine/ECS/Common/CCL_Types.h"
#include "Engine/ECS/Core/CCL_World.h"
#include <DirectXMath.h>
#include <string>

namespace Prefab
{

    using namespace CCL::ECS;

    // プレハブを生成し、ルートの EntityID を返す。失敗したら InvalidEntityID
    // 呼ぶとその場で World 全体の予約が確定する（ファイル冒頭の備考）
    EntityID SpawnPrefab(World &world, const std::string &path);

    // 位置を指定して生成する。位置は予約で書くので、値が入るのは次の確定のとき
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

    // プレハブを生成せずに、JSON と中のテクスチャ（拡張子で拾う）だけを先に読む。プレハブがないか壊れていたら false
    // 初めて生成したフレームに、JSON のパースと画像のデコードがまとめて来てカクつくのを防ぐ。止まっても目立たないときに呼ぶ
    bool Preload(const std::string &path);

    // 読み込んだプレハブの JSON のキャッシュを捨てる（次の SpawnPrefab で読み直す）
    void ClearPrefabCache();

} // namespace Prefab