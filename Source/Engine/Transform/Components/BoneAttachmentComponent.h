#pragma once
#include <string>

/**
 * @brief 親エンティティの特定ボーンに追従させるためのコンポーネント。
 * @note 親（ModelComponent 保有エンティティ）の指定ボーンに、本エンティティの
 *       ローカル変換をオフセットとして適用した位置へ追従する。
 */
struct BoneAttachmentComponent
{
    std::string boneName        = ""; // "mixamorig:RightHand" 等
    int         cachedBoneIndex = -1; // 初回解決後にキャッシュ。-1=未解決
};