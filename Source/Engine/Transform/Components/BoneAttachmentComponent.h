#pragma once
// ===========================================================================
// 概要: 親 Entity の特定ボーンに追従させるためのコンポーネント BoneAttachmentComponent。
//
// 責務:
//   - 追従先のボーン名と、解決済みのボーン番号を保持する
//
// 備考: 親（ModelComponent を持つ Entity）の指定ボーンに、この Entity の
//       ローカル変換をオフセットとして掛けた位置へ追従する。
//       追従の計算は BoneAttachmentSystem（当たり判定用）と
//       BoneAttachmentRenderSystem（描画用）が行う。
// ===========================================================================
#include <string>

struct BoneAttachmentComponent
{
    std::string boneName        = ""; // "mixamorig:RightHand" 等
    int         cachedBoneIndex = -1; // 初回解決後にキャッシュする。-1 = 未解決
};