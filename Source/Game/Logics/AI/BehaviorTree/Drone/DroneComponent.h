/**
 * @file DroneComponent.h
 * @brief ドローンの物理状態・フォーメーション状態を管理するコンポーネント
 * * @note 【アーキテクチャ上の注意】
 * このコンポーネントはPOD（Plain Old Data）であり、自身のAIロジックを持たない。
 * 振付師（FormationSystem）が目標座標を書き込み、
 * 実行者（MovementSystem）がそれに向かって物理移動を行うための「共有メモリ」として機能する。
 */
#pragma once
#include <cstdint>
#include "SimpleMath.h"
#include "Engine/Serialization/Meta/ComponentMeta.h"
#include "ECS/Common/CCL_Common.h"

 // ボスからドローン群への指示（※BossCommandComponent側でもこれを利用する）
enum class DroneFormationType : uint8_t {
    Hidden,          ///< ボスに収納されている状態
    OrbitCircle,     ///< ボスを中心に円陣を描く
    SequentialAttack ///< 順番にプレイヤーへ突撃する
};

// ドローン個体の具体的な物理ステート
enum class DroneState : uint8_t {
    Idle,         ///< 停止中
    MoveToTarget, ///< 目標座標へ移動中（追従）
    LockOn,       ///< 攻撃前のタメ・照準中
    FireCharge    ///< 目標へ向かって突撃中
};

/**
 * @struct DroneComponent
 * @brief ドローン1体ごとの状態と目標座標を保持する構造体
 */
struct DroneComponent {
    CCL::ECS::EntityID ownerBossId = CCL::ECS::InvalidEntityID; ///< 親であるボスのEntityID
    uint16_t localIndex = 0;                ///< 自分が編隊の何番目か (0, 1, 2...)
    uint16_t totalDrones = 1;               ///< 編隊の総数

    DroneFormationType currentFormation = DroneFormationType::Hidden;
    DroneState currentState = DroneState::Idle;
    float stateTimer = 0.0f;                ///< 現在の状態の継続時間

    // 振付師が書き込む目標座標
    DirectX::SimpleMath::Vector3 targetPosition = DirectX::SimpleMath::Vector3::Zero;

    // 個体パラメータ
    float moveSpeed = 30.0f;
    float orbitRadius = 8.0f; ///< 展開する円の半径
};
