#pragma once
// 描画用モデル（ModelComponent）の頂点から、地形用の MeshShape を作らせる目印
// ModelComponent と JoltRigidbodyComponent を一緒に付ける。Transform のスケールも形状に入る
struct JoltMeshColliderComponent
{
    bool isEnabled = true; // false なら形状を作らず、デバッグ表示もしない
};
