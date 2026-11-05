#pragma once
// ===========================================================================
// 概要: ドローン1機ぶんの状態（DroneComponent）と、そのフィールドの型
//
// 備考: 1つのフィールドを同じステージで書く System は1つだけにする
//       1000機を ForEachParallel で回せるのはこの規則があるからで、破るとデータ競合になる
//       [RT] は実行時専用で保存しない。群れ全体で1つの値は DroneTuning.h に置く
// ===========================================================================
#include <array>
#include <cstdint>
#include <cstddef>
#include "SimpleMath.h"
#include "Engine/Serialization/Meta/ComponentMeta.h"
#include "Engine/ECS/Common/CCL_Types.h"
#include "Engine/Serialization/Core/EntityLink.h"
#include "Game/Logics/AI/Drone/Components/DroneFormationType.h"

// ドローン個体の物理ステート。書き手は DroneBrainSystem のみ
enum class DroneState : uint8_t
{
    Idle,         // 待機: 持ち場をバネで保つ
    MoveToTarget, // 移動: 持ち場へ素早く整列する
    FireCharge,   // 突撃: 当たり判定を持ちうるのはこの状態だけ
    Returning,    // 離脱: 突撃後に上へ抜ける。当たらない
};

// FireCharge 中の移動方式。書き手は攻撃陣形
enum class ChargeMode : uint8_t
{
    Straight,    // fireDirection へ直進（弾幕）
    RigidFollow, // targetPosition に毎フレーム一致させる（データ陣形）
    SoftFollow,  // targetPosition へバネで寄る（小隊）
};

// ホバー揺れの位相の周期 [秒]。この位相に掛ける係数は整数にする（2π で折り返しても波形が途切れないように）
inline constexpr float kHoverPhasePeriod = 6.28318530718f;

// ドローン1機ぶんの状態
struct DroneComponent
{
    // ---- 所属と番号 ── 書き手=DroneManagerSystem ----

    CCL::Serialization::EntityLink ownerBossId; // 所属ボス

    uint16_t localIndex  = 0;    // [RT] 群れ全体の通し番号
    uint16_t totalDrones = 1;    // [RT] 群れの総数。0除算を避けるため既定1
    uint8_t  squadId     = 0xFF; // 所属小隊。0xFF は無所属
    uint8_t  slotInSquad = 0;    // 小隊内の位置。0=先頭機

    // ---- ボスからの命令 ── 書き手=DroneCommandSystem ----

    DroneFormationType currentFormation = DroneFormationType::None; // [RT] 今どの形を作るか
    bool               wantsAttack      = false;                    // [RT] 突撃中か。自分で終える技では DroneBrainSystem が下ろす

    // ---- 陣形が書く目標 ── 書き手=陣形（IFormation 派生）----

    DirectX::SimpleMath::Vector3 targetPosition      = DirectX::SimpleMath::Vector3::Zero;  // [RT] 持ち場の座標
    DirectX::SimpleMath::Vector3 targetLookDirection = DirectX::SimpleMath::Vector3::UnitZ; // 機首を向けたい方向
    DirectX::SimpleMath::Vector3 fireDirection       = DirectX::SimpleMath::Vector3::Zero;  // 直進の突撃の方向

    float attackProgress = 0.0f; // [RT] 陣形攻撃の進行度 0→1

    bool               shivers            = false;                    // [RT] 予告の間に震えるか（震えを使う技か）
    DroneFormationType telegraphFormation = DroneFormationType::None; // [RT] telegraphTime をどの陣形で数えているか
    float              telegraphTime      = 0.0f;                     // 予告の経過時間 [秒]。0 より大きい間が予告中

    ChargeMode chargeMode = ChargeMode::Straight; // [RT] FireCharge 中の移動方式
    float      fireDelay  = 0.0f;                 // 発射待ちの残り [秒]。弾幕だけが使う

    DirectX::SimpleMath::Vector3 followFeedForward = DirectX::SimpleMath::Vector3::Zero; // 先渡しの速度（持ち場そのものが動く速度）
    float                        springScale       = 1.0f;                               // バネの強さの倍率。小隊の陣形が後ろの席ほど小さくする

    // ---- 個体の判断 ── 書き手=DroneBrainSystem ----

    DroneState currentState   = DroneState::Idle; // [RT] 今の物理ステート
    bool       hasReachedSlot = false;            // 持ち場へ一度でも着いたか

    float chargeWindup   = 0.0f; // [RT] 直進の突撃のタメの残り [秒]
    float breakawayTimer = 0.0f; // [RT] 突撃後の離脱の残り [秒]
    float chargeTimer    = 0.0f; // [RT] 突撃開始からの経過 [秒]。飛距離で終われないときの保険

    DirectX::SimpleMath::Vector3 chargeStartPos = DirectX::SimpleMath::Vector3::Zero; // [RT] 突撃開始地点。飛距離の基準

    // ---- 移動の実体 ── 書き手=DroneMovementSystem ----

    DirectX::SimpleMath::Vector3 currentVelocity = DirectX::SimpleMath::Vector3::Zero; // [RT] 現在速度
    float                        hoverTimeOffset = 0.0f;                               // ホバー揺れの位相 [秒]。生成時に機体ごとにばらす
    bool                         rigidLocked     = false;                              // [RT] 剛体追従で持ち場に追いついたか。突撃以外では下ろす

    // ---- 個体パラメータ（インスペクタで調整）----

    float moveSpeed   = 30.0f; // 基準速度 [m/s]。各ステートがこれに倍率を掛けて上限にする
    float orbitRadius = 8.0f;  // 低空円陣の半径 [m]
};

// 1000機ぶんが連続配置されるので、増やすほど帯域を食う
static_assert(sizeof(DroneComponent) <= 160,
              "DroneComponent が肥大化している。フィールドを追加する前に見直すこと");
