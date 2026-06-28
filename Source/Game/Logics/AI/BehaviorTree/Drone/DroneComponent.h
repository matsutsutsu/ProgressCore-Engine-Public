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
#include "Engine/ECS/Common/CCL_Common.h"
#include "Engine/Serialization/Core/EntityLink.h"

 // ボスからドローン群への指示（※BossCommandComponent側でもこれを利用する）
enum class DroneFormationType : uint8_t {
    Hidden,          ///< ボスに収納されている状態
    OrbitCircle,     ///< ボスを中心に円陣を描く
    HighOrbitSnipe,    ///< 【上空狙撃】上空待機からゆっくりと順番に狙撃し、一定距離で停止する
    DeathRing,       ///< プレイヤーの周囲を囲む「処刑の輪」
    AegisShield,     ///< ボスの前面に壁を作る「絶対防衛陣」
    HighOrbit,       ///< ボスの頭上高くで旋回
    LowOrbit,        ///< ボスと同じ高さで旋回
    SpreadLockOn,    ///< 大きく広がって静止（タメ）
    AllCharge,       ///< 現在のプレイヤー位置へ一斉突撃
	CloseGuard,       ///< ボスに密着してガードする
    BarrierBurst,     //  収縮からの全方位拡散爆発
    ChargeTunnel,      // 突進用の通路制限
    HoldPosition,     // 現在の空間位置に固定
    DirectionalCharge,// 全員で同じ方向へ一斉平行突撃
    CycloneBurst,       // 円陣から収縮・加速して平面放射



    // ★ 第2形態用・新規フォーメーション
    HighLowOrbit,      ///< 【双層の輪廻】偶数・奇数で高さが分かれる2段円陣
    CrossHelix,        ///< 【交差螺旋】2群に分かれて逆回転する弾幕陣形
    KamikazeSwarm,      ///< 【暴雨の号令】息もつかせぬ超連続カミカゼ突撃
    HighLowCycloneBurst, ///< 【双層の竜巻】上下2段でタイミングをずらした時間差放射

    // 第２形態　１２８機用
    SweepingBeam,      ///< 【絶望の秒針】超巨大な回転レーザー
    InfinityEight,     ///< 【輪廻の軌道】上空で8の字を描く
    SwarmRiver,         ///< 【暴虐の濁流】うねりながら押し寄せる波

    SideSlapWhip, ///< 【側面薙ぎ払い】右側に縦列で並び、左へ一気に薙ぎ払う
    InfinitySnipe, // 8の字を描きながら交差射撃
    GuillotineSlam, ///< 【縦割り】頭上に巨大な刃を作り、正面に振り下ろす
    ClapSlam, ///< 【両手挟み】左右に1列で展開し、正面でバチンと挟み込む

    TsunamiWave, // 真ん中から降りて波のように押し寄せる
    MegaClapSlam_UpperFirst, ///< 128機マトリクス：上段が先に挟み込む
    MegaClapSlam_LowerFirst, ///< 128機マトリクス：下段が先に挟み込む

    // 上下時間差盾突撃
    MegaShieldCharge_UpperFirst,
    MegaShieldCharge_LowerFirst,

};

// ドローン個体の具体的な物理ステート
enum class DroneState : uint8_t {
    Idle,         ///< 【有機的追従】バネ制御でフワフワと目標に追従する待機・陣形維持状態。
    MoveToTarget, ///< 【機械的移動】Lerpで直線的に指定位置へ素早く整列する状態。
    LockOn,       ///< 【タメ・静止】一切移動せずその場に留まるエネルギー充填・待機状態。
    FireCharge,   ///< 【高速突撃】目標座標へ向かって等速直線運動で突撃する攻撃状態。
    Hold,         ///< 【完全停止】その場で急ブレーキをかけて完全に固定される状態。 

    // シールドからの発射などに使う、浮き上がらない直線突撃
    HorizontalCharge,

    Shaking, // 震える状態
};

/**
 * @struct DroneComponent
 * @brief ドローン1体ごとの状態と目標座標を保持する構造体
 */
struct DroneComponent {
    // --- 8バイト型 ---
    CCL::Serialization::EntityLink ownerBossId;

    // --- 12バイト型 (Vector3) ---
    DirectX::SimpleMath::Vector3 targetPosition = DirectX::SimpleMath::Vector3::Zero;
    DirectX::SimpleMath::Vector3 fireDirection = DirectX::SimpleMath::Vector3::Zero;
    DirectX::SimpleMath::Vector3 currentVelocity = DirectX::SimpleMath::Vector3::Zero;

    // 脳が指示する「ドローンが向くべき方向（視線）」
    DirectX::SimpleMath::Vector3 targetLookDirection = DirectX::SimpleMath::Vector3::UnitZ;

    // 前回のフレームで加算した視覚的な揺れ（ノイズ）のオフセット
    DirectX::SimpleMath::Vector3 currentShakeOffset = DirectX::SimpleMath::Vector3::Zero;

    // --- 4バイト型 (float) ---
    float stateTimer = 0.0f;
    float hoverTimeOffset = 0.0f;
    float moveSpeed = 30.0f;
    float orbitRadius = 8.0f;

    // --- 2バイト型 (uint16_t) ---
    uint16_t localIndex = 0;
    uint16_t totalDrones = 1;

    // --- 1バイト型 (enum/bool) ---
    DroneFormationType currentFormation = DroneFormationType::Hidden;
    DroneState currentState = DroneState::Idle;
};