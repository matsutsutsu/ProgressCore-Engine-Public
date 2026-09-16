// 小隊（5機編隊）の共有状態。書き手は SquadUpdateSystem（単スレッド）だけで、
// 陣形・移動の並列ループからは読むだけにする。
//
// 実体のリーダー機を持たないのが設計の核。先頭の個体をリーダーにすると撃破された
// 瞬間に小隊が崩壊し、追従機がリーダーの Transform を毎フレーム参照することになる。
// 「先頭の位置」を純粋なデータ(anchor)として持てば、先頭機が落ちても隊列は生きる。
#pragma once
#include <array>
#include <cstdint>
#include "SimpleMath.h"
#include "Game/Logics/AI/Drone/DroneComponent.h"

// 小隊の高度制限の基準。床の高さを変えたときに小隊だけ取り残されないよう共有する。
inline constexpr float kSquadGroundY = kDroneGroundY;

// 小隊の行動フェーズ。接近（読ませる）と突入（当てる）を必ず分ける。
enum class SquadPhase : uint8_t
{
    Regroup,  // ボス頭上の待機点で編隊を組み直す（命令が来ていない間）
    Approach, // プレイヤーの側面へ回り込む。ここが予備動作＝回避方向を読ませる時間
    Strike,   // 先頭がプレイヤーを追って突っ込む（旋回上限つきホーミング）
};

// 小隊1個ぶんの状態。書き手はすべて SquadUpdateSystem。
struct SquadState
{
    // 先頭機の目標位置＝小隊の「頭」。追従機はここからのオフセットで並ぶ。
    DirectX::SimpleMath::Vector3 anchorPos = DirectX::SimpleMath::Vector3::Zero;

    // 進行方向。編隊オフセットを回す基底になる（機首の向き）。
    DirectX::SimpleMath::Vector3 anchorForward = DirectX::SimpleMath::Vector3::UnitZ;

    // 現在の速度。急に瞬間移動させないためにバネで寄せる対象。
    DirectX::SimpleMath::Vector3 anchorVelocity = DirectX::SimpleMath::Vector3::Zero;

    // フェーズ滞在タイマー（秒）。突入時間の管理に使う。
    float phaseTimer = 0.0f;

    SquadPhase phase = SquadPhase::Regroup;

    // 各スロットの機体の「前フレームの実位置」。追従機は頭からの固定オフセットでなく
    // 自分の前の機体のここを追う。1フレーム古い値をあえて使うことで、鎖の1リンクごとに
    // 遅れが積もり、頭が曲がると後ろほど遅れて曲がる＝隊列がしなる。
    std::array<DirectX::SimpleMath::Vector3, 5> memberPos{};

    // 各スロットの機体の「前フレームの実速度」。前の機体と同じ速度をまず出し、
    // 差分だけをバネで直す。小隊全体の速度(anchorVelocity)を全機に配ってはいけない
    // （全機が同じ方向へ飛び、「先頭に追従」ではなく「各自が向かう」絵になる）。
    std::array<DirectX::SimpleMath::Vector3, 5> memberVel{};

    // 上の値が有効か。撃破されて欠けた席を追うと隊列が原点へ吸い込まれる。
    std::array<bool, 5> memberValid{};

    uint8_t aliveCount = 0; // 生存機数。0 の小隊は計算をスキップ

    // 頭の位置を一度でも決めたか。false の間は目標へ瞬間移動させる
    // （無いと初回だけワールド原点から飛んでくる）。
    bool initialized = false;
};

// 小隊の見た目を決める調整値。「どれくらいしなるか」は動かして目で見るしかないので、
// constexpr にせず実行時に触れるようにする。
struct SquadTuning
{
    float chainGap = 2.5f; // 前の機体との間隔 [m]。小さいほど蛇が詰まって見える

    float followGain     = 10.0f; // 間隔のずれを直す強さ。小さいほど胴体が伸び縮みする
    float slotLagPerSlot = 0.06f; // 後ろの席ほどバネを弱める割合。大きいほど尾が遅れて曲がる

    float strikeSpeed      = 42.0f;  // 突入速度 [m/s]
    float strikeDuration   = 1.6f;   // 突入の継続時間 [秒]
    float approachMinTime  = 1.6f;   // 次の突入までに接近へ留まる最低時間 [秒]
    float maxTurnRateDeg   = 100.0f; // 突入中に曲がれる角度の上限 [度/秒]
    float terminalLockDist = 8.0f;   // この距離まで詰めたら追尾をやめ直線に固定 [m]

    float approachRadius = 26.0f; // プレイヤーからどれだけ離れて構えるか [m]
    float approachHeight = 9.0f;  // 接近時の高度（プレイヤー基準）[m]

    // 床からの最低高度 [m]。突入は地面の高さへ直進し通り過ぎても走り続けるので必ず
    // 床を突き抜ける。物理で押し返さず、頭の高度を先に止めて隊列ごと床を這わせる。
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
