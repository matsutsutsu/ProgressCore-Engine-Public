#pragma once
#include <string>

// 親 Entity の指定ボーンに、この Entity のローカル変換をオフセットとして掛けて追従させる
// 追従の計算は BoneAttachmentSystem（当たり判定用）と BoneAttachmentRenderSystem（描画用）が行う
struct BoneAttachmentComponent
{
    std::string boneName        = ""; // 追従先のボーン名（親モデルのノード名）
    int         cachedBoneIndex = -1; // boneName から解決したノード番号（-1 = 未解決）
};