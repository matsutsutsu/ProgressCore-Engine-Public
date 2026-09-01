/**
 * 小隊（5機編隊）の共有状態。ドローン個体ではなく「小隊」の側に持たせるデータ。
 *
 * なぜ実体のリーダー機を持たないのか（設計の核）:
 *   先頭のドローン個体をリーダーにすると、
 *     ・撃破された瞬間に小隊が崩壊する
 *     ・追従機がリーダーの Transform を毎フレーム参照する＝ランダムアクセス＋更新順依存
 *   になる。そこで「先頭の位置」を純粋なデータ（anchor）として持ち、
 *   slot 0 の機体をそこに座らせる。見た目は「先頭機に4機が追従」のままで、
 *   先頭機が落ちても aliveCount が減るだけで隊列は生き続ける。
 *
 * 並列実行での約束:
 *   このデータを書くのは SquadUpdateSystem（単スレッド）だけ。
 *   DroneFormationSystem / DroneMovementSystem の並列ループからは読むだけにする。
 *   （並列ループから同じ小隊へ書くとデータ競合になる）
 */
#pragma once
#include <array>
#include <cstdint>
#include "SimpleMath.h"

/// ステージ床のワールドY。小隊の高度制限の基準。
/// @note  頭を動かす側（SquadUpdateSystem）と持ち場を出す側（SquadAssaultFormation）の
///        両方が使うので、どちらかのクラス内ではなくここへ置く。
///        今のステージは平面1枚なので定数で足りる。起伏のあるステージを作るなら
///        ここをレイキャストへ差し替える（使う側の式は変わらない）。
inline constexpr float kSquadGroundY = 0.0f;

/// 小隊の行動フェーズ。
/// @note  「予備動作を読ませてから当てる」という企画の骨格に合わせて、
///        接近（読ませる）と突入（当てる）を必ず分ける。
enum class SquadPhase : uint8_t
{
    Regroup,  ///< ボス頭上の待機点で編隊を組み直す（命令が来ていない間）
    Approach, ///< プレイヤーの側面へ回り込む。ここが予備動作＝回避方向を読ませる時間
    Strike,   ///< 先頭がプレイヤーを追って突っ込む（旋回上限つきホーミング）
};

/// 小隊1個ぶんの状態。
struct SquadState
{
    /// 先頭機の目標位置＝小隊の「頭」。追従機はここからのオフセットで並ぶ。
    DirectX::SimpleMath::Vector3 anchorPos = DirectX::SimpleMath::Vector3::Zero;

    /// 進行方向。編隊オフセットを回す基底になる（機首の向き）。
    DirectX::SimpleMath::Vector3 anchorForward = DirectX::SimpleMath::Vector3::UnitZ;

    /// 現在の速度。急に瞬間移動させないためにバネで寄せる対象。
    DirectX::SimpleMath::Vector3 anchorVelocity = DirectX::SimpleMath::Vector3::Zero;

    /// フェーズ滞在タイマー（秒）。突入時間の管理に使う。
    float phaseTimer = 0.0f;

    /// 現在のフェーズ。書き手は SquadUpdateSystem のみ。
    SquadPhase phase = SquadPhase::Regroup;

    /// 各スロットの機体の「前フレームの実位置」。
    /// @note  追従機は頭からの固定オフセットではなく、自分の前の機体のここを追う。
    ///        1フレーム古い値をあえてそのまま使う。鎖の1リンクごとに遅れが積もり、
    ///        頭が曲がると後ろほど遅れて曲がる＝隊列がしなる。
    ///        書き手は SquadUpdateSystem（毎フレーム全機を走査して更新）。
    std::array<DirectX::SimpleMath::Vector3, 5> memberPos{};

    /// 各スロットの機体の「前フレームの実速度」。
    /// @note  追従機はこれを2つの用途で使う。
    ///          ① 並ぶ向き   … 前の機体の進行方向の真後ろに付く
    ///          ② 速度の継承 … 前の機体と同じ速度をまず出し、差分だけをバネで直す
    ///        小隊全体の速度（anchorVelocity）を全機に配ってはいけない。
    ///        それをやると全機が同じ方向へ同じ速さで飛ぶ＝「先頭ではなくプレイヤーに
    ///        各自が向かっている」ようにしか見えなくなる（追従が消える）。
    ///        書き手は SquadUpdateSystem。
    std::array<DirectX::SimpleMath::Vector3, 5> memberVel{};

    /// 上の値が有効か（その席の機体が生存しているか）。
    ///        撃破されて欠けた席を追ってしまうと、隊列が原点へ吸い込まれる。
    std::array<bool, 5> memberValid{};

    /// 生存機数。0 になった小隊は計算をスキップする。
    uint8_t aliveCount = 0;

    /// 頭の位置を一度でも決めたか。
    /// @note  false の間は目標へ瞬間移動させる。これが無いと、初回だけ
    ///        ワールド原点から目標へ飛んでくる不自然な動きが見える。
    bool initialized = false;
};

/// 小隊の見た目を決める調整値。
///
/// なぜ実行時に変えられるようにするか:
///   「隊列がどれくらいしなるか」は数字を見ても決まらず、動かして目で見るしかない。
///   constexpr のままだと1回の調整に毎回ビルドが要る。
///   BTのWait秒数をエディタ側で完結させているのと同じ理由でここも実行時に触れるようにする。
struct SquadTuning
{
    // --- 隊列の形 ---
    float chainGap = 2.5f;   ///< 前の機体との間隔 [m]。小さいほど蛇が詰まって見える

    // --- しなり ---
    float followGain     = 10.0f; ///< 間隔のずれを直す強さ。小さいほど胴体が伸び縮みする
    float slotLagPerSlot = 0.06f; ///< 1つ後ろの席ごとにバネを弱める割合。大きいほど尾が遅れて曲がる

    // --- 突入 ---
    float strikeSpeed      = 42.0f;  ///< 突入速度 [m/s]
    float strikeDuration   = 1.6f;   ///< 突入の継続時間 [秒]
    float approachMinTime  = 1.6f;   ///< 次の突入までに接近へ留まる最低時間 [秒]
    float maxTurnRateDeg   = 100.0f; ///< 突入中に曲がれる角度の上限 [度/秒]
    float terminalLockDist = 8.0f;   ///< この距離まで詰めたら追尾をやめ直線に固定 [m]

    // --- 接近 ---
    float approachRadius = 26.0f; ///< プレイヤーからどれだけ離れて構えるか [m]
    float approachHeight =  9.0f; ///< 接近時の高度（プレイヤー基準）[m]

    // --- 地面 ---
    /// 床（ワールド Y=0）からの最低高度 [m]。ここより下へは沈めない。
    ///
    /// なぜ要るか:
    ///   突入はプレイヤー（ほぼ地面の高さ）へ向かって直進し、通り過ぎても
    ///   strikeDuration のあいだ走り続ける。つまり必ず床を突き抜ける。
    ///   物理で押し返すのではなく、頭の高度を先に止めて隊列ごと床を這わせる。
    float minAltitude = 2.0f;
};

/// 全小隊の状態をまとめた World リソース。
/// @note  密配列。小隊数は数十なので、ドローン側からの squads[squadId] 参照は
///        実質 L1 に載る。エンティティIDからの検索を挟まないのが要点。
struct SquadRegistry
{
    /// 小隊の最大数。
    static constexpr uint8_t kMaxSquads = 32;

    /// 1小隊の編成人数（先頭1機＋追従4機）。
    static constexpr uint8_t kSquadSize = 5;

    /// 無所属を表す squadId。128機を使う大技はこちらのまま動く。
    static constexpr uint8_t kNoSquad = 0xFF;

    std::array<SquadState, kMaxSquads> squads{};

    /// 全小隊で共有する調整値（ImGui から実行中に変更できる）。
    SquadTuning tuning{};
};
