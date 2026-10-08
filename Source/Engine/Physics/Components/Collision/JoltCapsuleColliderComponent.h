#pragma once
// ===========================================================================
// 概要: カプセル形のコライダー。剛体の形状、またはキャラクター（CharacterVirtual）の形状になる。
//
// 責務:
//   - 円柱部分の半分の高さと半径、Entity の原点からのローカルなずれを持つ
//
// 備考: 全体の高さ = halfHeight * 2 + radius * 2。
//       キャラクターに使う場合、JoltCharacterSetupSystem は localOffset/回転を使わず、
//       足元が原点になるよう自前でずらす。
// ===========================================================================
#include <DirectXMath.h>

struct JoltCapsuleColliderComponent
{
    float halfHeight = 0.5f; // 円柱部分の半分の高さ（m）
    float radius     = 0.5f; // 半径（m）

    DirectX::XMFLOAT3 localOffset        = {0.0f, 0.0f, 0.0f}; // Entity の原点からの位置のずれ（m）
    DirectX::XMFLOAT3 localRotationEuler = {0.0f, 0.0f, 0.0f}; // コライダー単体のローカル回転（度）。横向きのカプセルに使う

    bool isDirty = false; // 形状を作り直すか。インスペクタで編集すると自動で立つ
};

