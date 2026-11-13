#pragma once
#include <SimpleMath.h>

#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Physics/Components/Character/CharacterMovementComponent.h"
#include "Engine/Physics/Components/Character/JoltCharacterConfigComponent.h"
#include "Engine/Physics/Components/Character/JoltCharacterHandleComponent.h"
#include "Game/Logics/Character/Boss/Components/BossActionComponent.h"

struct BattleContext;
struct AnimStateMachineComponent;

// ボス1体・1フレーム分の材料。BossActionSystem が作り、技の Enter・Update へまとめて渡す
// const でも、参照先（transform・movement）は書き換えられる
struct BossActionFrame
{
    TransformComponent                 &transform;        // 向きを書く
    CharacterMovementComponent         &movement;         // 移動速度を書く
    const JoltCharacterConfigComponent &joltConfig;       // 移動の速さの上限
    const JoltCharacterHandleComponent &joltHandle;       // 物理の位置と向き
    const BattleContext                &battle;           // ステージの中心・半径
    DirectX::SimpleMath::Vector3        playerPos;        // 相手の足元（ワールド）
    float                               dt;               // この個体の時計の dt [秒]
    const AnimStateMachineComponent    *animStateMachine; // アニメのステートの長さを引く。なければ nullptr
    bool                                raging;           // 怒りの連発中か（攻撃の技の硬直を縮める）
};

// 複数の技で使う処理
namespace BossActionCommon
{
    // 技の硬直。足を止めて recoveryTime 秒数え、終わったら actionFinished を立てる
    // 間合いを取る・後方ステップ・跳躍・突進の最後の段と、疲れの間ずっと呼ぶ
    void UpdateRecovery(BossActionComponent &bossAction, float recoveryTime, const BossActionFrame &frame);

    // 間合いを保つ向き（正規化しない）。横（moveLeft の側）＋ 距離を保つ前後 ＋ 壁から離れる内向き を足したもの
    DirectX::SimpleMath::Vector3 KeepDistanceVector(const DirectX::SimpleMath::Vector3 &bossPos,
                                                    const DirectX::SimpleMath::Vector3 &toPlayerDir, bool moveLeft,
                                                    const BossActionComponent &bossAction, const BossActionFrame &frame);

    // 動く向きを、体から見た向き（x = 右、y = 前）に直す。moveDir が水平で長さ 1 なら、結果も長さ 1
    DirectX::SimpleMath::Vector2 BodyLocalDirection(const DirectX::SimpleMath::Vector3 &moveDir,
                                                    const TransformComponent           &transform);
} // namespace BossActionCommon
