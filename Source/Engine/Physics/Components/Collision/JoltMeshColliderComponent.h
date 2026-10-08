#pragma once
// ===========================================================================
// 概要: 描画用モデル（ModelComponent）の頂点から地形用の MeshShape を作らせる目印。
//
// 責務:
//   - メッシュ判定を作るかどうかの切り替えを持つ
//
// 備考: ModelComponent と JoltRigidbodyComponent を一緒に付けておくこと。
//       Transform のスケールも形状に反映される（JoltBodySetupSystem）。
// ===========================================================================
struct JoltMeshColliderComponent
{
    bool isEnabled = true; // false なら形状を作らず、デバッグ表示もしない
};
