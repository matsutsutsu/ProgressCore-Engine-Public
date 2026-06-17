#pragma once
#include "Engine/ECS/System/CCL_System.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Physics/Components/Character/JoltCharacterHandleComponent.h"
#include "Engine/Physics/Components/Character/JoltCharacterConfigComponent.h"
#include "Engine/Physics/Components/Character/CharacterMovementInputComponent.h"

// ===================================================================================
// ファイル名: JoltCharacterUpdateSystem.h
// 役割:
// 仮想キャラクター（CharacterVirtual）の移動・壁ずり・段差登り・重力・ジャンプを
// 計算する専用システム。
//
// 【アーキテクチャ仕様】
// - 実行フェーズ: PrePhysics (★ JoltStepSystem が走る前に終わらせるのが公式の絶対ルール)
// - 対象: CharacterVirtual / Input / Config をすべて持つ Entity。
//
// 【接地判定の唯一の真実】
// - 接地状態は CharacterVirtual::GetGroundState() を権威とする。
//   Facade のレイキャスト接地（剛体対象）はキャラクター移動には使わない。
//
// 【使い方・ルール】
// - 「意思（desiredVelocity / jumpRequested）」を受け取り、地形との干渉を計算する。
// - 計算結果の「位置(Position)」だけを Transform に書き戻す。回転(Rotation)は同期しない。
// - isTeleported フラグが立っている場合は、計算前にワープ処理を優先して行う。
//
// 【並列化に関する重大な注意】
// - 本システムは ForEachWithID（シングルスレッド）で実行している。
//   ループ内で BodyInterface（非ロック版）へ書き込みを行うため、
//   ForEachWithIDParallel へ安易に置き換えると並列書き込みでクラッシュする。
// ===================================================================================

class JoltCharacterUpdateSystem
    : public CCL::ECS::IfSystem<JoltCharacterUpdateSystem,
    CCL::ECS::Write<TransformComponent>,
    CCL::ECS::Read<JoltCharacterHandleComponent>,
    CCL::ECS::Read<CharacterMovementInputComponent>,
    CCL::ECS::Read<JoltCharacterConfigComponent>> {
public:
    JoltCharacterUpdateSystem() : IfSystem("JoltCharacterUpdateSystem") {}
    virtual ~JoltCharacterUpdateSystem() = default;

    void Update(float dt) override;
};