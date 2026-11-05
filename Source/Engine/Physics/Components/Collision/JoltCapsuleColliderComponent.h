#pragma once
#include <DirectXMath.h>

// カプセル形のコライダー。剛体か、キャラクター（CharacterVirtual）の形状になる
// 全体の高さは halfHeight * 2 + radius * 2。キャラクターでは localOffset と回転を使わず、足元を原点にする
struct JoltCapsuleColliderComponent
{
    float halfHeight = 0.5f; // 円柱部分の半分の高さ（m）
    float radius     = 0.5f; // 半径（m）

    DirectX::XMFLOAT3 localOffset        = {0.0f, 0.0f, 0.0f}; // Entity の原点からの位置のずれ（m）
    DirectX::XMFLOAT3 localRotationEuler = {0.0f, 0.0f, 0.0f}; // コライダー単体のローカル回転（度）。横向きのカプセルに使う

    bool isDirty = false; // 形状を作り直すか。インスペクタで編集すると自動で立つ
};

