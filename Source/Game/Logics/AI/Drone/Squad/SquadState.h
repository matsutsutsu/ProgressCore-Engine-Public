#pragma once
// ===========================================================================
// 概要: 小隊（5機編隊）の共有状態と調整値、それをまとめる World リソース。
//
// 責務:
//   - 小隊1個ぶんの状態（SquadState）: アンカー・段階・各席の前フレームの位置と速度
//   - 実行中に触る調整値（SquadTuning）: 隊列・追跡・集合
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
// 追跡はプレイヤーの足元を目指し、外れても飛び続けるので、止めないと床を突き抜ける。
// アンカー（SquadUpdateSystem）と各機の持ち場（Formations_Squad.h）の両方をここで止める。
inline constexpr float kSquadFloorY = kDroneGroundY + 2.0f;

// 小隊の段階。集合（読ませる）と追跡（当てる）を必ず分ける。
enum class SquadPhase : uint8_t
{
    Regroup, // 小隊突撃の指示が無い。アンカーはボス頭上の待機点を回る
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

    // アンカーの位置を一度でも決めたか。false の間は目標へ瞬間移動させる
    // （無いと初回だけワールド原点から飛んでくる）。
    bool initialized = false;

    SquadPhase phase = SquadPhase::Regroup;

    // 集合中の下降の進み具合（0=上で集合中、1=降り切った）。上の集合点に着いた小隊から進め始める。
    float descend = 0.0f;

    // 追跡中の今の速さ [m/s]。曲がるときに減速し、向き直ったら加速するので、毎フレームの値を持ち越す。
    float chaseSpeed = 0.0f;

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

    // 小隊ごとの速さのばらつき（上の倍率に ± で足す幅）。
    // 旋回半径は「速さ ÷ 旋回上限」なので、速さが違えば描く曲線も着く時刻も違い、全小隊が同じ線に重ならない。
    // 一番遅い小隊でも倍率が 1 を超える幅にする（下回ると走って逃げるプレイヤーに追いつけない）。
    float chaseSpeedSpread = 0.08f;

    // 狙う点をプレイヤーの左右へずらす最大量 [m]。全小隊が同じ点を追うと1か所に集まるため、
    // 小隊ごとに左右互い違いにずらして、包むように迫らせる。
    float aimSideOffset = 8.0f;

    // 上のずれを縮め始める距離と、0 にする距離 [m]。この間で線形に縮め、内側ではプレイヤー本人を狙う。
    //
    // 0 にする距離を必ず設ける。ずれを距離に比例させるだけだと、どの距離でも同じ角度だけずれた方向を
    // 狙い続けることになり、最後までプレイヤーの横をかすめて外れる。
    float aimFadeStartDist = 30.0f;
    float aimFadeEndDist   = 12.0f;

    // 曲がるときに落とす速さの下限（追跡の速さに対する割合）。狙う方向とのずれが 90 度以上でこの割合になる。
    //
    // 一定の速さのままだと、一度通り過ぎた相手が旋回の円（半径＝速さ÷旋回上限）の内側に入り、
    // どれだけ曲がっても機首を向けられずに周りを回り続ける。止まっている相手に当たらなくなる。
    // 減速すれば旋回半径が縮み、小さく U ターンして向き直れる。
    float turnSlowMinScale = 0.4f;

    // 速さを変えられる上限 [m/s²]（加速・減速とも）。
    // 無制限だと、至近距離で切り返された瞬間にも急ブレーキして小回りで追ってくるので、
    // 旋回上限で避けるという前提が崩れる。ブレーキが間に合うのは、通り過ぎた後の U ターンだけにする。
    float chaseAccel = 60.0f;

    // 追跡中に曲がれる角度の上限 [度/秒]。回避はこの制限だけで成り立たせる（終末ロックは持たない）。
    // 近づくほどプレイヤーを見る角度は速く変わるので、「横移動の速さ ÷ この値」より内側で鋭く切り返されると曲がりきれずに外れる。
    // 上げすぎると切り返しても曲がって当たり、ジャスト回避が成立しなくなる。
    float maxTurnRateDeg = 200.0f;

    // --- 集合 ---

    float gatherRadius    = 18.0f; // ボスの周りの輪の半径 [m]
    float gatherLowHeight = 5.0f;  // 降り切った後の高さ（ボス基準）[m]。追跡はここから始まる
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
