#pragma once
// ===========================================================================
// 概要: ドローン1機ぶんの状態（DroneComponent）と、そのフィールドの型。
//
// 責務:
//   - 判断層・陣形・移動層など、ドローン系 System 間で受け渡す状態を持つ
//   - 物理ステート（DroneState）・突撃の移動方式（ChargeMode）・攻撃フェーズを定義する
//
// 備考:
//   群れ全体で1つだけ持つ値（床の高さ・突撃速度倍率・予告の震え）は DroneTuning.h にある。
//
//   ロジックは持たない。読むのは誰でもよいが、書くのは「1フィールドにつき、
//   1ステージあたり1システム」に限る。1000機を ForEachParallel で回せるのはこの
//   規則があるからで、破るとデータ競合になる。
//
//   各区分の「書き手=…」は主な書き手を示す。ステージが違えば直列化されるので、
//   別ステージから書いているフィールドもある（stateTimer と fireDelay は判断層・陣形が
//   設定して移動層が減らす、など）。同じステージで2つの System が同じフィールドを
//   書かないこと。
//
//   [RT] は実行時専用（セーブにもプレハブにも保存しない）。
// ===========================================================================
#include <array>
#include <cstdint>
#include <cstddef>
#include "SimpleMath.h"
#include "Engine/Serialization/Meta/ComponentMeta.h"
#include "Engine/ECS/Common/CCL_Types.h"
#include "Engine/Serialization/Core/EntityLink.h"
#include "Game/Logics/AI/Drone/Components/DroneFormationType.h" // 陣形の種類（BT側と共有するので別ヘッダ）

// ドローン個体の物理ステート。書き手は DroneBrainSystem のみ。
//   Returning が FireCharge と別値であること自体が仕様。判定側は
//   「FireCharge か」だけを見るので、「帰り道は当たらない」に
//   専用コードが1行も要らない。
enum class DroneState : uint8_t
{
    Idle,         // 追従: ホバーの揺れを伴うバネ制御で目標へ寄る。待機・陣形維持
    MoveToTarget, // 移動: 持ち場へ直線的に素早く整列する
    FireCharge,   // 突撃: 目標へ突撃する。当たり判定とジャスト回避の対象になりうるのはこの状態だけ
    Returning,    // 離脱: 突撃を終えて上方へ抜ける。無害。この後 MoveToTarget で隊列へ戻る
};

// FireCharge 中の移動方式。書き手は攻撃陣形（Formation）。
enum class ChargeMode : uint8_t
{
    Straight,    // fireDirection へ直進。方向は突入時にロックされる（弾幕）
    RigidFollow, // targetPosition へ毎フレーム一致させる。形が歪まない（縦割り・挟み込み）
    SoftFollow,  // targetPosition へバネで寄る。速いほど後ろへ流れ、曲がると遅れて曲がる（小隊）
};

// 突撃1回ぶんの寿命を「個体が」持つためのフェーズ。
//
// 信号を毎フレーム全機へ配ると、飛び切った機が次フレームに攻撃へ引き戻されて個別行動が成立しない。
// そこで立ち上がりでだけ Charging を立て、以降は各機が自分で進める。出撃は一斉、帰投は機体ごとに異なる。
//
// 陣形自体が動く攻撃（縦割り等）はこれを使わない（IFormation::GetLifecycle）。
enum class DroneAttackPhase : uint8_t
{
    None,      // 攻撃していない（陣形待機中／帰投完了後）
    Charging,  // 突撃中。飛距離か経過時間で各機が自分で切り上げる
    Returning, // 突き抜けた後の離脱中。当たり判定を持たない
};

// ホバー揺れの位相の周期 [秒]。掛ける係数が整数なので 2π で折り返しても
// 波形が途切れない。この位相を使う係数を足すときは整数のままにすること。
inline constexpr float kHoverPhasePeriod = 6.28318530718f;

// ドローン1機ぶんの状態。
struct DroneComponent
{
    // ---- 所属と番号 ── 書き手=DroneManagerSystem ----

    CCL::Serialization::EntityLink ownerBossId; // 所属ボス。生成時に設定

    uint16_t localIndex  = 0; // [RT] 群れ全体の通し番号。陣形が「何番目か」から座標を出す
    uint16_t totalDrones = 1; // [RT] 群れの総数。0除算を避けるため既定1

    // 所属小隊。0xFF は無所属。
    // 一度割り当てたら維持する。撃破のたびに振り直すと、生き残った機の所属まで入れ替わり、
    // 同じ部隊で組んでいるように見えない。
    uint8_t squadId = 0xFF;

    uint8_t slotInSquad = 0; // 小隊内の位置。0=先頭機。小隊の陣形は後ろほど追従を弱める（springScale）

    // ---- ボスからの命令 ── 書き手=DroneCommandSystem ----

    DroneFormationType currentFormation = DroneFormationType::None; // [RT] 今どの形を作るか

    bool wantsAttack = false; // [RT] 「今撃て」命令。true で判断層が FireCharge へ遷移させる

    // 突撃1回ぶんの進行フェーズ。終了の判断（Charging→Returning）だけは DroneBrainSystem が書く。
    // Broadcast 寿命の陣形では常に None。
    DroneAttackPhase attackPhase = DroneAttackPhase::None;

    // ---- 陣形が書く目標 ── 書き手=Formation（IFormation 派生）----

    DirectX::SimpleMath::Vector3 targetPosition      = DirectX::SimpleMath::Vector3::Zero;  // [RT] 持ち場の座標
    DirectX::SimpleMath::Vector3 targetLookDirection = DirectX::SimpleMath::Vector3::UnitZ; // 機首を向けたい方向
    DirectX::SimpleMath::Vector3 fireDirection       = DirectX::SimpleMath::Vector3::Zero;  // 突撃方向。Straight 時にここへ直進する

    float attackProgress = 0.0f; // [RT] 陣形攻撃の進行度 0→1。「形が動く」技の時間軸

    // 予告の溜め時間 [秒]。持ち場に着いていて、まだ撃つ合図が来ていない間だけ進む。
    // 振幅ではなく秒数なので、BTの Wait(予告) を伸ばせば自動で長く震える。
    float windupTimer = 0.0f;

    // 予告の経過時間 [秒]。持ち場に着いていて、まだ撃つ合図が来ていない間だけ進む。
    // windupTimer と違って「撃つ前に震わせる」の設定に関係なく進む。
    // データ陣形の Shiver の帯はこれを時計にして、予告の中のどこで震えるかを決める。
    float telegraphTime = 0.0f;

    ChargeMode chargeMode = ChargeMode::Straight; // [RT] FireCharge 中の移動方式

    // 発射待ち時間 [秒]。>0 の間は撃たずに持ち場を追い続ける（移動層の発射待ち）。
    // 弾幕が localIndex から各機へ割り振り、「順番に飛ばす」を作る。
    float fireDelay = 0.0f;

    // 先渡しする速度（持ち場そのものが動いている速度）。移動層はこれにバネの補正を足して動く。
    // バネにはずれの補正だけをさせるので、持ち場が速く動いても置いていかれない。
    //
    // 書き手は陣形の種類で変わる。
    //   ボスに付いていく陣形 …DroneFormationSystem が「ボスの速度 × 乗せる割合」を入れる
    //   小隊                  … 陣形が「1つ前の機体の速度」で上書きする。小隊全体の速度を渡すと
    //                           全機が同じ方向へ同じ速さで飛び、「先頭に追従」ではなく「各自が突っ込む」絵になる
    DirectX::SimpleMath::Vector3 followFeedForward = DirectX::SimpleMath::Vector3::Zero;

    // 持ち場へ寄るバネの強さの倍率。移動層の接近・待機・バネ追従が、バネの強さに掛ける。
    // DroneFormationSystem が Calculate の前に 1 を入れ、小隊の陣形だけが「後ろの席ほど弱く」で上書きする
    // （後ろの機ほど遅れて曲がり、隊列が先頭から順にしなる）。
    // 陣形の外で動かす機（タイトルの群れ）は、動かす側（TitleSwarmSystem）が書く。
    float springScale = 1.0f;

    // ---- 個体の判断 ── 書き手=DroneBrainSystem ----

    DroneState currentState = DroneState::Idle; // [RT] 今の物理ステート。移動層はこれだけを見る

    // 陣形スロットへ初回到着したかのラッチ。
    // true の間は、回転し続ける targetPosition との距離では MoveToTarget へ戻さない。
    // 距離で戻すと、閾値をまたぐたびに MoveToTarget と Idle が点滅する。
    bool hasReachedSlot = false;

    float stateTimer   = 0.0f; // [RT] 汎用の状態タイマー [秒]。タメ時間と離脱カーブに使い回す
    float chargeTimer  = 0.0f; // [RT] 突撃開始からの経過 [秒]。飛距離判定が成立しない時の保険

    DirectX::SimpleMath::Vector3 chargeStartPos = DirectX::SimpleMath::Vector3::Zero; // [RT] 突撃開始地点。飛距離判定の基準

    // ---- 移動の実体 ── 書き手=DroneMovementSystem ----

    DirectX::SimpleMath::Vector3 currentVelocity = DirectX::SimpleMath::Vector3::Zero; // [RT] 現在速度

    // ホバー揺れの位相 [秒]。蒔くのは生成時、進めるのは移動層（ステージが違うので競合しない）。
    //
    // 0 のまま dt を足すと全機の位相が一致し、群れが1枚の板として揺れる。
    float hoverTimeOffset = 0.0f;

    // ---- 個体パラメータ（インスペクタで調整）----

    float moveSpeed   = 30.0f; // 基準速度 [m/s]。各ステートがこれに倍率を掛けて上限にする
    float orbitRadius = 8.0f;  // 旋回半径 [m]。低空円陣が円の半径として読む
};

// 1000機ぶんが連続配置されるので、増やすほど帯域を食う。
static_assert(sizeof(DroneComponent) <= 160,
              "DroneComponent が肥大化している。フィールドを追加する前に見直すこと");
