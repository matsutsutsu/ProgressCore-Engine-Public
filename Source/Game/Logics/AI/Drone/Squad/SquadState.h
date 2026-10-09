#pragma once
// ===========================================================================
// 概要: 小隊（5機編隊）の共有状態と調整値、それをまとめる World リソース。
//
// 責務:
//   - 小隊1個ぶんの状態（SquadState）: アンカー・段階・各席の前フレームの位置と速度
//   - 実行中に触る調整値（SquadTuning）: 隊列・追跡・反発・集合
//   - 全小隊の密配列（SquadRegistry）
//
// 備考:
//   書き手は SquadUpdateSystem（単スレッド）だけで、陣形・移動の並列ループからは読むだけ。
//
//   実体のリーダー機を持たない。先頭の個体をリーダーにすると、撃破された瞬間に小隊が
//   崩れ、追従機がリーダーの Transform を毎フレーム参照することになる。先頭の位置を
//   データ（アンカー: anchorPos）として持てば、先頭機が落ちても隊列は保たれる。
// ===========================================================================
#include <array>
#include <cstdint>
#include "SimpleMath.h"
#include "Game/Logics/AI/Drone/Components/DroneComponent.h"
#include "Game/Logics/AI/Drone/Components/DroneTuning.h" // kDroneGroundY

// 小隊が飛べる一番低い高さ。床の高さ＋2m。
// 追跡はプレイヤーの足元を目指すので、止めないと床を突き抜ける。アンカー（SquadUpdateSystem）と
// 各機の持ち場（Formations_Squad.h）の両方をここで止める。
inline constexpr float kSquadFloorY = kDroneGroundY + 2.0f;

// 小隊の段階。集合（読ませる）と追跡（当てる）を必ず分ける。
enum class SquadPhase : uint8_t
{
    Regroup, // 小隊突撃の指示が無い。アンカーは先頭の機に付いていくだけ
    Gather,  // ボスの頭上の輪へ集まり、着いたら下へ降りてくる。ここが予備動作＝これから来ると読ませる時間
    Chase,   // アンカーがプレイヤーを追い続ける（旋回上限つき）。攻撃の合図が落ちるまで続く
};

// 小隊1個ぶんの状態。書き手はすべて SquadUpdateSystem。
struct SquadState
{
    // --- アンカー（実体の無い先頭） ---

    DirectX::SimpleMath::Vector3 anchorPos      = DirectX::SimpleMath::Vector3::Zero;  // 位置。席0の機がここを追う
    DirectX::SimpleMath::Vector3 anchorForward  = DirectX::SimpleMath::Vector3::UnitZ; // 向き（正規化済み）
    DirectX::SimpleMath::Vector3 anchorVelocity = DirectX::SimpleMath::Vector3::Zero;  // 速度。席0の機へ先渡しする

    SquadPhase phase = SquadPhase::Regroup;

    // 集合中の下降の進み具合（0=上で集合中、1=降り切った）。上の集合点に着いた小隊から進め始める。
    float descend = 0.0f;

    // 追跡中の今の速さ [m/s]。曲がるときに減速し、向き直ったら加速するので、毎フレームの値を持ち越す。
    float chaseSpeed = 0.0f;

    // 追跡中の今の旋回の速さ [度/秒]。曲がり始めは0から少しずつ上がるので、毎フレームの値を持ち越す。
    float turnRateDeg = 0.0f;

    // このフレームの、他の小隊のアンカーから離れる向きの合計。追跡で狙う方向に混ぜる。
    // 長さは近さで決まる（1つの小隊と重なっていれば約1、半径の外なら0）。
    DirectX::SimpleMath::Vector3 separation = DirectX::SimpleMath::Vector3::Zero;

    // --- 各席の機体（前フレームの値） ---

    // 追従機はアンカーからの固定位置ではなく、自分の1つ前の機体のここを追う。
    // 1フレーム古い値をあえて使うことで1機ごとに遅れが積もり、アンカーが曲がると
    // 後ろの機ほど遅れて曲がる（隊列がしなる）。
    std::array<DirectX::SimpleMath::Vector3, 5> memberPos{};

    // 前の機体と同じ速度をまず出し、ずれだけをバネで直すのに使う。
    // 小隊全体の速度（anchorVelocity）を全機に配ってはいけない
    // （全機が同じ方向へ飛び、「先頭に追従」ではなく「各自が向かう」絵になる）。
    std::array<DirectX::SimpleMath::Vector3, 5> memberVel{};

    // 上の値が有効か。撃破されて欠けた席を追うと、隊列が原点へ吸い込まれる。
    std::array<bool, 5> memberValid{};

    uint8_t aliveCount = 0; // 生存機数。0 の小隊は計算をスキップ
};

// 実行中に触る調整値。「どれくらいしなるか」「どれくらい避けられるか」は動かして目で見るしかないので、
// constexpr にせず小隊の調整 UI（SquadUpdateSystem::OnGui）から変えられるようにする。
// それ以外の数値は、使う側の .cpp / .h に定数で置いてある。
struct SquadTuning
{
    // --- 隊列 ---

    float chainGap   = 2.5f;  // 前の機体との間隔 [m]。小さいほど隊列が詰まって見える
    float followGain = 10.0f; // 間隔のずれを直す強さ。小さいほど隊列が伸び縮みする

    // --- 追跡 ---

    // 追跡の速さ。プレイヤーの走行速度に掛ける倍率。
    // 走り続けても追いつかれ、一瞬だけ速いダッシュなら振り切れる値にする（1 を超え、ダッシュ÷走行 未満）。
    // 倍率で持つのは、プレイヤーの速度を調整したときに関係が崩れないようにするため。
    float chaseSpeedOverRun = 1.1f;

    // 追跡中に曲がれる角度の上限 [度/秒]。回避はこの制限だけで成り立たせる（終末ロックは持たない）。
    // 近づくほどプレイヤーを見る角度は速く変わるので、「横移動の速さ ÷ この値」より内側で鋭く切り返されると曲がりきれずに外れる。
    // 上げすぎると切り返しても曲がって当たり、ジャスト回避が成立しなくなる。
    float maxTurnRateDeg = 90.0f;

    // 曲がり始めに旋回の速さを上げていく速さ [度/秒²]。
    // 0 から上限まで一瞬で上がると、切り返しにすぐ反応してきびきび曲がり、避けにくい。
    // 上限 ÷ この値 が、曲がり始めてから全力で曲がるまでの秒数になる。
    float turnAccelDeg = 120.0f;

    // 曲がるときに落とす速さの下限（追跡の速さに対する割合）。狙う方向とのずれが 90 度以上でこの割合になる。
    //
    // 一定の速さのままだと、一度通り過ぎた相手が旋回の円（半径＝速さ÷旋回上限）の内側に入り、
    // どれだけ曲がっても機首を向けられずに周りを回り続ける。止まっている相手に当たらなくなる。
    // 減速すれば旋回半径が縮み、小さく U ターンして向き直れる。
    float turnSlowMinScale = 0.55f;

    // 速さを変える速さ [m/s²]。減速と再加速で分ける。
    // 大きいと速さが瞬時に切り替わり、曲がり角で急ブレーキしたように見える。
    // 小さいと、通り過ぎた後に勢いで流れてから、ゆっくり曲がって戻ってくる。
    float chaseBrake = 8.0f; // 曲がるときの減速
    float chaseAccel = 5.0f; // 向き直った後の再加速（追跡を始めたときの加速も兼ねる）

    // --- 小隊同士の反発 ---

    // 全小隊が同じ1点（プレイヤー）を同じ性能で追うと、軌跡が1本に重なって1つの塊に見える。
    // 近くの小隊から離れる向きを狙う方向に混ぜて、横に広がって迫らせる。
    // 位置を直接押さないのは、旋回上限を通らずに横へ動けてしまい、回避の前提が崩れるため。
    float separationRadius = 10.0f; // 反発し始める距離 [m]。小隊1本の長さ（間隔×4）より長くする
    float separationWeight = 0.3f;  // 狙う方向に混ぜる強さ。1 だと重なった相手から離れる向きとプレイヤーの向きが同じ重み

    // プレイヤーからこの距離より内側では、反発を近さに比例して弱め、プレイヤーの位置で0にする [m]。
    // 直前まで反発させると、止まっている相手にも当たらなくなる。
    float separationFadeDist = 15.0f;

    // --- 集合 ---

    float gatherRadius     = 18.0f; // ボスの周りの輪の半径 [m]
    float gatherLowHeight  = 5.0f;  // 降り切った後の高さ（ボス基準）[m]。追跡はここから始まる
};

// 全小隊の状態をまとめた World リソース。密配列にしてエンティティIDからの検索を挟まない。
struct SquadRegistry
{
    static constexpr uint8_t kMaxSquads = 32;
    static constexpr uint8_t kSquadSize = 5; // 先頭1機＋追従4機

    static constexpr uint8_t kNoSquad = 0xFF; // 無所属。大技はこちらのまま動く

    std::array<SquadState, kMaxSquads> squads{};
    SquadTuning                        tuning{};
};
