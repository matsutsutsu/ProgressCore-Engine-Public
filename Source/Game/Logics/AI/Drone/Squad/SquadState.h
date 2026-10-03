#pragma once
// ===========================================================================
// 概要: 小隊（5機編隊）の共有状態と調整値、それをまとめる World リソース。
//
// 責務:
//   - 小隊1個ぶんの状態（SquadState）: アンカー・フェーズ・各席の前フレームの位置と速度
//   - 見た目の調整値（SquadTuning）: 間隔・追従の強さ・追跡の速さなど
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

// 小隊の高度制限の基準。床の高さを変えたときに小隊だけ取り残されないよう共有する。
inline constexpr float kSquadGroundY = kDroneGroundY;

// 小隊の行動フェーズ。展開（読ませる）と追跡（当てる）を必ず分ける。
enum class SquadPhase : uint8_t
{
    Regroup,  // ボス頭上の待機点で編隊を組み直す（命令が来ていない間）
    Approach, // プレイヤーを囲む扇へ展開する。ここが予備動作＝どこから来るかを読ませる時間
    Chase,    // 先頭がプレイヤーを追い続ける（旋回上限つきホーミング）。攻撃の合図が落ちるまで続く
};

// 小隊1個ぶんの状態。書き手はすべて SquadUpdateSystem。
struct SquadState
{
    // 先頭機の目標位置（アンカー）。追従機はここを起点に並ぶ。
    DirectX::SimpleMath::Vector3 anchorPos = DirectX::SimpleMath::Vector3::Zero;

    // 進行方向。編隊オフセットを回す基底になる（機首の向き）。
    DirectX::SimpleMath::Vector3 anchorForward = DirectX::SimpleMath::Vector3::UnitZ;

    // 現在の速度。急に瞬間移動させないためにバネで寄せる対象。
    DirectX::SimpleMath::Vector3 anchorVelocity = DirectX::SimpleMath::Vector3::Zero;

    SquadPhase phase = SquadPhase::Regroup;

    // 各スロットの機体の「前フレームの実位置」。追従機はアンカーからの固定オフセットでなく
    // 自分の前の機体のここを追う。1フレーム古い値をあえて使うことで、1機ごとに遅れが
    // 積もり、アンカーが曲がると後ろの機ほど遅れて曲がる（隊列がしなる）。
    std::array<DirectX::SimpleMath::Vector3, 5> memberPos{};

    // 各スロットの機体の「前フレームの実速度」。前の機体と同じ速度をまず出し、
    // 差分だけをバネで直す。小隊全体の速度(anchorVelocity)を全機に配ってはいけない
    // （全機が同じ方向へ飛び、「先頭に追従」ではなく「各自が向かう」絵になる）。
    std::array<DirectX::SimpleMath::Vector3, 5> memberVel{};

    // 上の値が有効か。撃破されて欠けた席を追うと隊列が原点へ吸い込まれる。
    std::array<bool, 5> memberValid{};

    uint8_t aliveCount = 0; // 生存機数。0 の小隊は計算をスキップ

    // アンカーの位置を一度でも決めたか。false の間は目標へ瞬間移動させる
    // （無いと初回だけワールド原点から飛んでくる）。
    bool initialized = false;
};

// 小隊の見た目を決める調整値。「どれくらいしなるか」は動かして目で見るしかないので、
// constexpr にせず実行時に触れるようにする。
struct SquadTuning
{
    float chainGap = 2.5f; // 前の機体との間隔 [m]。小さいほど隊列が詰まって見える

    float followGain     = 10.0f; // 間隔のずれを直す強さ。小さいほど隊列が伸び縮みする
    float slotLagPerSlot = 0.06f; // 後ろの席ほどバネを弱める割合。大きいほど最後尾が遅れて曲がる

    // 追跡の速さ。プレイヤーの走行速度に掛ける倍率。
    // 走り続けても追いつかれ、一瞬だけ速いダッシュなら振り切れる値にする（1 を超え、ダッシュ÷走行 未満）。
    // 倍率で持つのは、プレイヤーの速度を調整したときに関係が崩れないようにするため。
    float chaseSpeedOverRun = 1.1f;

    // 追跡中に曲がれる角度の上限 [度/秒]。回避はこの制限だけで成り立たせる（終末ロックは持たない）。
    // 近づくほどプレイヤーを見る角度は速く変わるので、「横移動の速さ ÷ この値」より内側で鋭く切り返されると曲がりきれずに外れる。
    // 上げすぎると切り返しても曲がって当たり、ジャスト回避が成立しなくなる。
    float maxTurnRateDeg = 200.0f;

    float approachRadius = 26.0f; // プレイヤーからどれだけ離れて構えるか [m]
    float approachHeight = 9.0f;  // 展開時の高度（プレイヤー基準）[m]

    // 小隊を並べる扇の広さ [度]。プレイヤーから見てボスの方向を中心に広げる。
    // 360 で全周を囲む。全周だとカメラの後ろの小隊が見えないまま突っ込んでくるので、画面に収まる広さを既定にする。
    float approachArcDeg = 240.0f;

    // 隣り合う小隊の距離と高さを互い違いにずらす幅 [m]。同じ円周上に並ぶと画面上で重なって見えるため、
    // 手前・低い段と奥・高い段に分けて空間を埋める。
    float layerRadiusSpread = 6.0f;
    float layerHeightSpread = 5.0f;

    // 床からの最低高度 [m]。追跡はプレイヤーの足元（地面の高さ）を目指し、外れても飛び続けるので必ず
    // 床を突き抜ける。物理で押し返さず、アンカーの高度を先に止めて隊列ごと床の上を飛ばせる。
    float minAltitude = 2.0f;
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
