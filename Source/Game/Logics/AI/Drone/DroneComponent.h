// ドローン1機ぶんの状態。ロジックを持たず、システム間の共有メモリとして機能する。
//
// ── このファイルの読み方 ──────────────────────────────────
//
// 【書き手は1フィールドにつき1システム】
//   読むのは誰でもよいが、書くのは1システムだけ。これが 1000 機を
//   ForEachParallel で並列に回せる根拠になっている。
//   コンポーネントはただの公開メモリなので、この規約をコンパイラは強制できない。
//   代わりに全フィールドへ「書き手」を明記する。ここを破ると即データ競合になる。
//
// 【[RT] タグ】
//   実行時専用。セーブにもプレハブにも乗らない（ComponentMeta::Fields() に出さない）。
//
// 【並び順】
//   意味のまとまりで並べる。SoA チャンクは DroneComponent を配列として
//   連続配置するので sizeof がそのまま帯域になるが、型サイズ順に手で並べ替える
//   運用は追加のたびに崩れる（実際に崩れていた）。末尾の static_assert で
//   測って見張る方式にしている。
#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
#include "SimpleMath.h"
#include "Engine/Serialization/Meta/ComponentMeta.h"
#include "Engine/ECS/Common/CCL_Types.h"
#include "Engine/Serialization/Core/EntityLink.h"


// 陣形の一覧。ここが唯一の定義で、enum も表示名もこの表から作る。
//
// 列は (識別子, 保存値, 表示名)。
//
// なぜ値を手で書くのか（＝並び順に頼らないのか）:
//   この数値はプレハブとシーンにそのまま保存されている（Muriel.json の
//   requestFormation、Drone.json の currentFormation）。並び順を保存値にすると、
//   途中の1行を消しただけで保存済みデータが別の陣形を指す。値を書いておけば
//   行の削除も並べ替えも安全になる。
//
// なぜ欠番があるのか:
//   構想だけで実装されなかった技（DeathRing / SweepingBeam など21件）を
//   2026-08 に削除した跡。欠番は埋めず、増やすときは 33 以降を使う。
//
// 追加するときは、ここに1行足したうえで FormationRegistry へ
// IFormation 実装を登録する。登録が無い型は指定しても静止するだけ。
#define DRONE_FORMATION_LIST(X)                             \
    X(Hidden,                   0, "収納（命令なし）")      \
    X(OrbitCircle,              1, "降下円陣")              \
    X(HighOrbit,                5, "頭上円陣")              \
    X(KamikazeSwarm,           17, "弾幕")                  \
    X(SideSlapWhip,            22, "横なぎ回転")            \
    X(GuillotineSlam,          24, "縦割り")                \
    X(ClapSlam,                25, "挟み込み1段")           \
    X(TsunamiWave,             26, "波状縦割り・左→右")     \
    X(MegaClapSlam_UpperFirst, 27, "挟み込み2段・上段先攻") \
    X(MegaClapSlam_LowerFirst, 28, "挟み込み2段・下段先攻") \
    X(TsunamiWave_RL,          31, "波状縦割り・右→左")     \
    X(SquadAssault,            32, "小隊突撃")

// ボスがドローン群へ出す「どの形を作れ」の指示。
//
// BossCommandComponent 側でもそのまま使う。
enum class DroneFormationType : uint8_t
{
#define CCL_DRONE_FORMATION_ENUM(Name, Value, Display) Name = Value,
    DRONE_FORMATION_LIST(CCL_DRONE_FORMATION_ENUM)
#undef CCL_DRONE_FORMATION_ENUM
};

// 保存値として使いうる番号の上限+1（欠番ぶんを含む）。
//
// インスペクタの列挙表示は「値をそのまま配列の添字にする」ため、
// 名前テーブルは詰めた長さではなくこの長さで作る必要がある。
inline constexpr size_t kDroneFormationSlotCount = 33;

// インスペクタの列挙表示用の陣形名。欠番は "(欠番)" で埋める。
//
// なぜ enum と同じファイルに置くか:
//   以前は DroneComponent と BossCommandComponent のメタ定義が同じ .cpp に
//   同居していて、この表もその .cpp のローカル変数だった。両者を別ファイルへ
//   分けたときに表が二重化しかけたので、出どころ（DRONE_FORMATION_LIST）の
//   隣へ移した。名前と値の対応を人間が保つ必要がなくなる。
static constexpr std::array<const char *, kDroneFormationSlotCount> MakeDroneFormationNames()
{
    std::array<const char *, kDroneFormationSlotCount> names{};
    for (auto &n : names) n = "(欠番)";
#define CCL_DRONE_FORMATION_NAME(Name, Value, Display) names[Value] = Display;
    DRONE_FORMATION_LIST(CCL_DRONE_FORMATION_NAME)
#undef CCL_DRONE_FORMATION_NAME
    return names;
}
inline constexpr auto kDroneFormationNames = MakeDroneFormationNames();

// ドローン個体の物理ステート。書き手は DroneBrainSystem のみ。
//
// Stunned と Returning が FireCharge と別の値であること自体が仕様になっている。
//   当たり判定もジャスト回避も「FireCharge か」だけを見るので、
//   「吹き飛ばし中と帰り道は当たらない」に専用の判定コードが1行も要らない。
enum class DroneState : uint8_t
{
    Idle,         // 【有機的追従】バネ制御でフワフワと目標へ追従する。待機・陣形維持
    MoveToTarget, // 【機械的移動】直線的に持ち場へ素早く整列する
    FireCharge,   // 【高速突撃】目標へ突撃する。この状態だけが有害＝ジャスト回避の対象
    Stunned,      // 【硬直】切り払いで吹き飛ばされ制御を失って漂う。無害・通常AI停止
    Returning,    // 【離脱】突撃を終えて上方へ抜ける。無害。この後 MoveToTarget で隊列へ
};


// FireCharge 中の移動方式。書き手は攻撃陣形（Formation）。
//
// bool（追従するか否か）では足りなくなったので enum にした。
// 剛体追従とバネ追従は見た目がまったく別物で、混同すると
// 「5機で飛んでも1枚の板が近づいてくるようにしか見えない」という致命的な劣化になる。
enum class ChargeMode : uint8_t
{
    Straight,    // fireDirection へ直進。方向は突入時にロックされる（弾幕・カミカゼ）
    RigidFollow, // targetPosition へ毎フレーム貼り付け＝形が一切歪まない（縦割り・挟み込み）
    SoftFollow,  // targetPosition へバネで寄る＝速いほど後ろへ流れ、曲がると遅れて曲がる（小隊）
};


// 突撃1回ぶんの寿命を「個体が」持つためのフェーズ。
//
// なぜ必要か:
//   ボスの「今撃て」信号を毎フレーム全機へ配る方式（レベル）のままだと、
//   飛び切った機が「もう終わった」と判断しても次フレームに攻撃へ引き戻され、
//   個別行動が原理的に成立しない。信号は立ち上がり（エッジ）でだけ Charging を立て、
//   以降は各機が自分で Returning → None へ進める。
//   結果として「出撃は一斉、帰投はバラバラ」になる。
//
// 陣形自体が動く攻撃（縦割り等）はこの寿命を使わない。
// どちらを使うかは IFormation::GetLifecycle() が陣形ごとに宣言する。
enum class DroneAttackPhase : uint8_t
{
    None,      // 攻撃していない（陣形待機中／帰投完了後）
    Charging,  // 突撃中。飛距離か経過時間で各機が自分で切り上げる
    Returning, // 突き抜けた後の離脱中。当たり判定を持たない
};


// ホバー揺れの位相の周期 [秒]。
//
// hoverTimeOffset をこの値で巻き戻す。掛ける係数（DroneMovementSystem の
// kHoverFrequency）が整数なので、2π で折り返しても波形は途切れない。
//
// この位相を使う係数を足すときは整数のままにすること。
// （予告の震えはここを使わず windupTimer を見る。巻き戻らないので
//   周波数を小数にでき、スライダーで調整できる）
inline constexpr float kHoverPhasePeriod = 6.28318530718f;

// 予告の震えの調整値。World リソースとして1つだけ持つ。
//
// なぜコンポーネントではなくリソースか:
//   「撃つ前は震える」は機体ごとの個性ではなく、全機共通の見せ方の規則。
//   1000機分をコンポーネントに持たせると、全部同じ値なのにメモリと
//   プレハブの両方が膨らみ、しかも一括で変えられない。
//   小隊の SquadTuning と同じ手。
struct DroneShiverTuning
{
    float amplitude = 0.25f; // 溜め切ったときの振れ幅 [m]
    float frequency = 7.0f;  // 震えの速さ [Hz]。上げすぎるとノイズに見えて予告にならない
    float rampTime  = 1.2f;  // この秒数で最大まで溜まる
    float scatter   = 1.0f;  // 機体ごとの位相のばらけ具合 0＝全機同時 / 1＝バラバラ
};

// ステージ床のワールY と、そこから浮かせておく最低高度 [m]。
//
// 全ドローンの硬い床。陣形側のクランプは「刃が地面で止まる」という
// 形の話だが、こちらは形とは無関係に「どの状態でも地面へ潜らない」を保証する。
//   吹き飛ばし・帰投・バネ追従の行き過ぎは targetPosition を経由しないので、
//   陣形側でいくらクランプしても止まらない。
//
// 今のステージは平面1枚なので定数で足りる。起伏のあるステージを作るなら
// ここをレイキャストへ差し替える（使う側の式は変わらない）。
inline constexpr float kDroneGroundY        = 0.0f;
inline constexpr float kDroneGroundClearance = 0.5f; // 機体半径ぶん。埋没を防ぐ

// 突撃（リリース）時に moveSpeed へ掛ける倍率。
//
// なぜコンポーネント側に置くか:
//   動かす側（DroneMovementSystem）だけでなく、狙いを決める側（弾幕の陣形）も
//   この値を要る。飛翔時間 ＝ 距離 ÷ 射出速度 を出さないと偏差射撃が組めない。
//   両方が同じ数字を見ていないと、狙った先と実際の着弾がずれる。
//   kHoverPhasePeriod をここへ置いてあるのと同じ理由。
inline constexpr float kDroneChargeSpeedScale = 5.5f;

// ドローン1機ぶんの状態。
struct DroneComponent
{
    // ================================================================
    // 所属と番号 ── 書き手=DroneManagerSystem
    // ================================================================

    CCL::Serialization::EntityLink ownerBossId; // 所属ボス。生成時に設定

    uint16_t localIndex  = 0;   // [RT] 群れ全体の通し番号。陣形が「何番目か」から座標を出す
    uint16_t totalDrones = 1;   // [RT] 群れの総数。localIndex と対で使う（0除算を避けるため既定1）

    // 所属小隊。SquadRegistry::kNoSquad(0xFF) は無所属。
    //
    // 一度割り当てたら維持する。撃破のたびに振り直すと生き残った機の所属まで
    // 入れ替わり、「同じ5機で組んでいる部隊」に見えなくなるため。
    // 満杯で溢れた機は無所属のまま localIndex/totalDrones で動くので、
    // 小隊制と既存の128機の大技はそのまま共存する。
    uint8_t squadId = 0xFF;

    uint8_t slotInSquad = 0;    // 小隊内の位置。0=先頭機。後ろほど追従を鈍らせる（SlotLag）

    // ================================================================
    // ボスからの命令 ── 書き手=DroneCommandSystem
    // ================================================================

    DroneFormationType currentFormation = DroneFormationType::Hidden; // [RT] 今どの形を作るか

    bool wantsAttack = false;   // [RT] 「今撃て」命令。true で判断層が FireCharge へ遷移させる

    // [RT] 突撃1回ぶんの進行フェーズ。開始・中断=DroneCommandSystem／終了判断=DroneBrainSystem。
    //      Broadcast 寿命の陣形では常に None（陣形が全機まとめて動き切るので個体は寿命を持たない）。
    DroneAttackPhase attackPhase = DroneAttackPhase::None;

    // ================================================================
    // 陣形が書く目標 ── 書き手=Formation（IFormation 派生）
    // ================================================================

    DirectX::SimpleMath::Vector3 targetPosition      = DirectX::SimpleMath::Vector3::Zero;  // [RT] 持ち場の座標
    DirectX::SimpleMath::Vector3 targetLookDirection = DirectX::SimpleMath::Vector3::UnitZ; // 機首を向けたい方向
    DirectX::SimpleMath::Vector3 fireDirection       = DirectX::SimpleMath::Vector3::Zero;  // 突撃方向。Straight 時にここへ直進する

    float attackProgress = 0.0f; // [RT] 陣形攻撃の進行度 0→1。縦割りが倒れる等「形が動く」技の時間軸

    // [RT] 予告の溜め時間 [秒]。書き手は DroneFormationSystem。
    //
    // 「持ち場に着いている（hasReachedSlot）が、まだ撃つ合図が来ていない」＝
    // 予備動作の最中だけ進み、それ以外では 0 に戻る。DroneMovementSystem が
    // この値から震えの振幅を出す（長く溜まるほど大きく震える）。
    //
    // なぜ振幅そのものではなく「溜まった秒数」を持つのか:
    //   BT の Wait(予告) を伸ばせば自動で長く溜まり、縮めれば即撃ちになる。
    //   予告の強さに専用の調整値を作らずに済み、調整先が予告の長さ1つに集まる。
    //
    // 機体ごとに持つのは hasReachedSlot が機体ごとに違うため。
    // 突撃から帰る途中の機は false なので、帰りながら震えることはない。
    // 隊列へ戻った機から順に震え始め、群れが「充電されていく」ように見える。
    float windupTimer = 0.0f;

    ChargeMode chargeMode = ChargeMode::Straight; // [RT] FireCharge 中の移動方式。突撃開始時に判断層が Straight へ戻す

    // [RT] 発射待ち時間 [秒]。FireCharge に入っても >0 の間はその場で待ち、0 でタメ→リリースへ進む。
    //      弾幕(KamikazeSwarm)が localIndex から各機へ割り振り「順番に飛ばす」を作る。
    float fireDelay = 0.0f;

    // [RT] SoftFollow で先渡しする速度。バネにはずれの補正だけをさせる。
    //
    // なぜ先渡しが要るか:
    //   バネだけで速度を作ると必ず「速度 ÷ バネ定数」ぶん遅れる。
    //   40m/s級の突入では各リンクが数m開き、数珠が千切れて点が飛んで見える。
    //
    // 何を渡すか（ここを間違えると追従が消える）:
    //   小隊では「1つ前の機体の速度」。小隊全体の速度を渡してはいけない。
    //   突入中の小隊速度はプレイヤーへ向かうベクトルなので、それを全機へ配ると
    //   全機が同じ方向へ同じ速さで飛び、バネの補正が完全に埋もれる。
    //   結果「先頭に追従」ではなく「各自がプレイヤーへ突っ込む」絵になる。
    DirectX::SimpleMath::Vector3 followFeedForward = DirectX::SimpleMath::Vector3::Zero;

    // ================================================================
    // 個体の判断 ── 書き手=DroneBrainSystem
    // ================================================================

    DroneState currentState = DroneState::Idle; // [RT] 今の物理ステート。移動層はこれだけを見る

    // 陣形スロットへ初回到着したかのラッチ。
    //
    // 命令は毎フレーム全機へ上書きされるのが原則なので、個体が自分で決めた判断は
    // 放っておくと次フレームに消える。保持したい判断はこうしてフィールドへ焼く。
    // このパターンが個体行動の土台で、attackPhase（個別帰投）と
    // recoverTimer（硬直からの個別復帰）も同じ形をしている。
    //
    // true の間は、回転し続ける targetPosition との距離では MoveToTarget へ戻さない
    // （毎フレーム到着判定をすると Idle と MoveToTarget が点滅する）。
    bool hasReachedSlot = false;  // [RT]

    float stateTimer   = 0.0f;  // [RT] 汎用の状態タイマー [秒]。突撃のタメ時間と離脱カーブの長さに使い回す
    float chargeTimer  = 0.0f;  // [RT] 突撃開始からの経過 [秒]。飛距離判定が成立しない時（壁際等）の保険
    float recoverTimer = 0.0f;  // [RT] 吹き飛ばし減衰後の立て直し [秒]。localIndex でばらつかせ復帰を揃えない

    DirectX::SimpleMath::Vector3 chargeStartPos = DirectX::SimpleMath::Vector3::Zero; // [RT] 突撃開始地点。ここからの飛距離で「飛び切った」を個体が判断する

    // ================================================================
    // 切り払い（CLEAVE）── 書き手=PlayerCleaveSystem（設定）
    //   減算は DroneBrainSystem、速度の減衰は DroneMovementSystem。
    // ================================================================

    // [RT] 吹き飛ばされている残り時間 [秒]。>0 の間は Stunned となり通常AIを行わない。
    //      この間は攻撃判定も被弾判定も失う。押した先で自分に当たる理不尽を防ぎ、
    //      「切り開いた道は通れる」をルールとして保証するため。
    float knockbackTimer = 0.0f;

    DirectX::SimpleMath::Vector3 knockbackVelocity = DirectX::SimpleMath::Vector3::Zero; // [RT] 吹き飛ばし速度。数フレームで減衰しつつ漂う

    // ================================================================
    // 移動の実体 ── 書き手=DroneMovementSystem
    // ================================================================

    DirectX::SimpleMath::Vector3 currentVelocity = DirectX::SimpleMath::Vector3::Zero; // [RT] 現在速度。毎フレーム積分して座標を確定する

    // [RT] ホバー揺れと予告の震えの位相 [秒]。
    //
    // 例外的に書き手が2つある。蒔くのは DroneManagerSystem::SpawnDrones（生成時に1回）、
    // 進めるのは DroneMovementSystem（毎フレーム）。生成と毎フレームで
    // ステージが違うので競合しない。
    // 0 のまま dt を足すだけだと全機の位相が一致し、群れが1枚の板として揺れる。
    float hoverTimeOffset = 0.0f;

    // ================================================================
    // 個体パラメータ（インスペクタで調整）
    // ================================================================

    float moveSpeed   = 30.0f;  // 基準速度 [m/s]。各ステートがこれに倍率を掛けて上限にする
    float orbitRadius =  8.0f;  // 旋回半径 [m]。降下円陣(OrbitCircleFormation)が円の半径として読む
};

// 1000機ぶんが連続配置されるので、増やすほど帯域を食う。
// 型サイズ順に手で並べ替える運用は追加のたびに崩れるため、測って見張る方式にする。
// 引っかかったら上限を上げる前に「本当にこのフィールドが要るか」を疑うこと。
static_assert(sizeof(DroneComponent) <= 160,
              "DroneComponent が肥大化している。フィールドを追加する前に見直すこと");
