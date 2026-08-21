/// @file  DroneComponent.h
/// @brief ドローン1機ぶんの状態。ロジックを持たず、システム間の共有メモリとして機能する。
///
/// ── このファイルの読み方 ──────────────────────────────────
///
/// 【書き手は1フィールドにつき1システム】
///   読むのは誰でもよいが、書くのは1システムだけ。これが 1000 機を
///   ForEachParallel で並列に回せる根拠になっている。
///   コンポーネントはただの公開メモリなので、この規約をコンパイラは強制できない。
///   代わりに全フィールドへ「書き手」を明記する。ここを破ると即データ競合になる。
///
/// 【[RT] タグ】
///   実行時専用。セーブにもプレハブにも乗らない（ComponentMeta::Fields() に出さない）。
///
/// 【並び順】
///   意味のまとまりで並べる。SoA チャンクは DroneComponent を配列として
///   連続配置するので sizeof がそのまま帯域になるが、型サイズ順に手で並べ替える
///   運用は追加のたびに崩れる（実際に崩れていた）。末尾の static_assert で
///   測って見張る方式にしている。
#pragma once
#include <cstdint>
#include "SimpleMath.h"
#include "Engine/Serialization/Meta/ComponentMeta.h"
#include "Engine/ECS/Common/CCL_Types.h"
#include "Engine/Serialization/Core/EntityLink.h"


/// @brief ボスがドローン群へ出す「どの形を作れ」の指示。
/// @note  BossCommandComponent 側でもそのまま使う。
///        未実装の値は FormationRegistry が nullptr を返し、指定されても静止するだけ。
enum class DroneFormationType : uint8_t {
    Hidden,          ///< ボスに収納されている状態
    OrbitCircle,     ///< ボスを中心に円陣を描く
    HighOrbitSnipe,  ///< 【上空狙撃】上空待機からゆっくりと順番に狙撃し、一定距離で停止する
    DeathRing,       ///< プレイヤーの周囲を囲む「処刑の輪」
    AegisShield,     ///< ボスの前面に壁を作る「絶対防衛陣」
    HighOrbit,       ///< ボスの頭上高くで旋回
    LowOrbit,        ///< ボスと同じ高さで旋回
    SpreadLockOn,    ///< 大きく広がって静止（タメ）
    AllCharge,       ///< 現在のプレイヤー位置へ一斉突撃
    CloseGuard,      ///< ボスに密着してガードする
    BarrierBurst,    ///< 収縮からの全方位拡散爆発
    ChargeTunnel,    ///< 突進用の通路制限
    HoldPosition,    ///< 現在の空間位置に固定
    DirectionalCharge, ///< 全員で同じ方向へ一斉平行突撃
    CycloneBurst,    ///< 円陣から収縮・加速して平面放射

    // ---- 第2形態用 ----
    HighLowOrbit,        ///< 【双層の輪廻】偶数・奇数で高さが分かれる2段円陣
    CrossHelix,          ///< 【交差螺旋】2群に分かれて逆回転する弾幕陣形
    KamikazeSwarm,       ///< 【暴雨の号令】息もつかせぬ超連続カミカゼ突撃
    HighLowCycloneBurst, ///< 【双層の竜巻】上下2段でタイミングをずらした時間差放射

    // ---- 第2形態・128機用 ----
    SweepingBeam,    ///< 【絶望の秒針】超巨大な回転レーザー
    InfinityEight,   ///< 【輪廻の軌道】上空で8の字を描く
    SwarmRiver,      ///< 【暴虐の濁流】うねりながら押し寄せる波

    SideSlapWhip,    ///< 【側面薙ぎ払い】右側に縦列で並び、左へ一気に薙ぎ払う
    InfinitySnipe,   ///< 8の字を描きながら交差射撃
    GuillotineSlam,  ///< 【縦割り】頭上に巨大な刃を作り、正面に振り下ろす
    ClapSlam,        ///< 【両手挟み】左右に1列で展開し、正面でバチンと挟み込む

    TsunamiWave,                 ///< 【波状縦割り・左→右】真ん中から降りて波のように押し寄せる
    MegaClapSlam_UpperFirst,     ///< 128機マトリクス：上段が先に挟み込む
    MegaClapSlam_LowerFirst,     ///< 128機マトリクス：下段が先に挟み込む
    MegaShieldCharge_UpperFirst, ///< 128機マトリクス：上段が先に盾突撃
    MegaShieldCharge_LowerFirst, ///< 128機マトリクス：下段が先に盾突撃
    TsunamiWave_RL,              ///< 【波状縦割り・右→左】右端から順に倒れてくる波

    SquadAssault,    ///< 【小隊突撃】5機で一列縦隊を組み、先頭がプレイヤーを追う。
                     ///  全機で1つの形を作る他の技と違い、小隊ごとに独立して動く
};


/// @brief ドローン個体の物理ステート。書き手は DroneBrainSystem のみ。
///
/// Stunned と Returning が FireCharge と別の値であること自体が仕様になっている。
///   当たり判定もジャスト回避も「FireCharge か」だけを見るので、
///   「吹き飛ばし中と帰り道は当たらない」に専用の判定コードが1行も要らない。
enum class DroneState : uint8_t
{
    Idle,         ///< 【有機的追従】バネ制御でフワフワと目標へ追従する。待機・陣形維持
    MoveToTarget, ///< 【機械的移動】直線的に持ち場へ素早く整列する
    FireCharge,   ///< 【高速突撃】目標へ突撃する。この状態だけが有害＝ジャスト回避の対象
    Stunned,      ///< 【硬直】切り払いで吹き飛ばされ制御を失って漂う。無害・通常AI停止
    Returning,    ///< 【離脱】突撃を終えて上方へ抜ける。無害。この後 MoveToTarget で隊列へ
};


/// @brief FireCharge 中の移動方式。書き手は攻撃陣形（Formation）。
///
/// bool（追従するか否か）では足りなくなったので enum にした。
/// 剛体追従とバネ追従は見た目がまったく別物で、混同すると
/// 「5機で飛んでも1枚の板が近づいてくるようにしか見えない」という致命的な劣化になる。
enum class ChargeMode : uint8_t
{
    Straight,    ///< fireDirection へ直進。方向は突入時にロックされる（弾幕・カミカゼ）
    RigidFollow, ///< targetPosition へ毎フレーム貼り付け＝形が一切歪まない（縦割り・挟み込み）
    SoftFollow,  ///< targetPosition へバネで寄る＝速いほど後ろへ流れ、曲がると遅れて曲がる（小隊）
};


/// @brief 突撃1回ぶんの寿命を「個体が」持つためのフェーズ。
///
/// なぜ必要か:
///   ボスの「今撃て」信号を毎フレーム全機へ配る方式（レベル）のままだと、
///   飛び切った機が「もう終わった」と判断しても次フレームに攻撃へ引き戻され、
///   個別行動が原理的に成立しない。信号は立ち上がり（エッジ）でだけ Charging を立て、
///   以降は各機が自分で Returning → None へ進める。
///   結果として「出撃は一斉、帰投はバラバラ」になる。
///
/// @note 陣形自体が動く攻撃（縦割り等）はこの寿命を使わない。
///       どちらを使うかは IFormation::GetLifecycle() が陣形ごとに宣言する。
enum class DroneAttackPhase : uint8_t
{
    None,      ///< 攻撃していない（陣形待機中／帰投完了後）
    Charging,  ///< 突撃中。飛距離か経過時間で各機が自分で切り上げる
    Returning, ///< 突き抜けた後の離脱中。当たり判定を持たない
};


/// @brief ドローン1機ぶんの状態。
struct DroneComponent
{
    // ================================================================
    // 所属と番号 ── 書き手=DroneManagerSystem
    // ================================================================

    CCL::Serialization::EntityLink ownerBossId; ///< 所属ボス。生成時に設定

    uint16_t localIndex  = 0;   ///< 群れ全体の通し番号。陣形が「何番目か」から座標を出す
    uint16_t totalDrones = 1;   ///< 群れの総数。localIndex と対で使う（0除算を避けるため既定1）

    /// 所属小隊。SquadRegistry::kNoSquad(0xFF) は無所属。
    ///
    /// 一度割り当てたら維持する。撃破のたびに振り直すと生き残った機の所属まで
    /// 入れ替わり、「同じ5機で組んでいる部隊」に見えなくなるため。
    /// 満杯で溢れた機は無所属のまま localIndex/totalDrones で動くので、
    /// 小隊制と既存の128機の大技はそのまま共存する。
    uint8_t squadId = 0xFF;

    uint8_t slotInSquad = 0;    ///< 小隊内の位置。0=先頭機。後ろほど追従を鈍らせる（SlotLag）

    // ================================================================
    // ボスからの命令 ── 書き手=DroneCommandSystem
    // ================================================================

    DroneFormationType currentFormation = DroneFormationType::Hidden; ///< 今どの形を作るか

    bool wantsAttack = false;   ///< [RT] 「今撃て」命令。true で判断層が FireCharge へ遷移させる

    /// [RT] 突撃1回ぶんの進行フェーズ。開始・中断=DroneCommandSystem／終了判断=DroneBrainSystem。
    ///      Broadcast 寿命の陣形では常に None（陣形が全機まとめて動き切るので個体は寿命を持たない）。
    DroneAttackPhase attackPhase = DroneAttackPhase::None;

    // ================================================================
    // 陣形が書く目標 ── 書き手=Formation（IFormation 派生）
    // ================================================================

    DirectX::SimpleMath::Vector3 targetPosition      = DirectX::SimpleMath::Vector3::Zero;  ///< 持ち場の座標
    DirectX::SimpleMath::Vector3 targetLookDirection = DirectX::SimpleMath::Vector3::UnitZ; ///< 機首を向けたい方向
    DirectX::SimpleMath::Vector3 fireDirection       = DirectX::SimpleMath::Vector3::Zero;  ///< 突撃方向。Straight 時にここへ直進する

    float attackProgress = 0.0f; ///< [RT] 陣形攻撃の進行度 0→1。縦割りが倒れる等「形が動く」技の時間軸

    ChargeMode chargeMode = ChargeMode::Straight; ///< [RT] FireCharge 中の移動方式。突撃開始時に判断層が Straight へ戻す

    /// [RT] 発射待ち時間 [秒]。FireCharge に入っても >0 の間はその場で待ち、0 でタメ→リリースへ進む。
    ///      弾幕(KamikazeSwarm)が localIndex から各機へ割り振り「順番に飛ばす」を作る。
    float fireDelay = 0.0f;

    /// [RT] SoftFollow で先渡しする速度。バネにはずれの補正だけをさせる。
    ///
    /// なぜ先渡しが要るか:
    ///   バネだけで速度を作ると必ず「速度 ÷ バネ定数」ぶん遅れる。
    ///   40m/s級の突入では各リンクが数m開き、数珠が千切れて点が飛んで見える。
    ///
    /// 何を渡すか（ここを間違えると追従が消える）:
    ///   小隊では「1つ前の機体の速度」。小隊全体の速度を渡してはいけない。
    ///   突入中の小隊速度はプレイヤーへ向かうベクトルなので、それを全機へ配ると
    ///   全機が同じ方向へ同じ速さで飛び、バネの補正が完全に埋もれる。
    ///   結果「先頭に追従」ではなく「各自がプレイヤーへ突っ込む」絵になる。
    DirectX::SimpleMath::Vector3 followFeedForward = DirectX::SimpleMath::Vector3::Zero;

    // ================================================================
    // 個体の判断 ── 書き手=DroneBrainSystem
    // ================================================================

    DroneState currentState = DroneState::Idle; ///< 今の物理ステート。移動層はこれだけを見る

    /// 陣形スロットへ初回到着したかのラッチ。
    ///
    /// 命令は毎フレーム全機へ上書きされるのが原則なので、個体が自分で決めた判断は
    /// 放っておくと次フレームに消える。保持したい判断はこうしてフィールドへ焼く。
    /// このパターンが個体行動の土台で、attackPhase（個別帰投）と
    /// recoverTimer（硬直からの個別復帰）も同じ形をしている。
    ///
    /// true の間は、回転し続ける targetPosition との距離では MoveToTarget へ戻さない
    /// （毎フレーム到着判定をすると Idle と MoveToTarget が点滅する）。
    bool hasReachedSlot = false;

    float stateTimer   = 0.0f;  ///< [RT] 汎用の状態タイマー [秒]。突撃のタメ時間と離脱カーブの長さに使い回す
    float chargeTimer  = 0.0f;  ///< [RT] 突撃開始からの経過 [秒]。飛距離判定が成立しない時（壁際等）の保険
    float recoverTimer = 0.0f;  ///< [RT] 吹き飛ばし減衰後の立て直し [秒]。localIndex でばらつかせ復帰を揃えない

    DirectX::SimpleMath::Vector3 chargeStartPos = DirectX::SimpleMath::Vector3::Zero; ///< [RT] 突撃開始地点。ここからの飛距離で「飛び切った」を個体が判断する

    // ================================================================
    // 切り払い（CLEAVE）── 書き手=PlayerCleaveSystem（設定）
    //   減算は DroneBrainSystem、速度の減衰は DroneMovementSystem。
    // ================================================================

    /// [RT] 吹き飛ばされている残り時間 [秒]。>0 の間は Stunned となり通常AIを行わない。
    ///      この間は攻撃判定も被弾判定も失う。押した先で自分に当たる理不尽を防ぎ、
    ///      「切り開いた道は通れる」をルールとして保証するため。
    float knockbackTimer = 0.0f;

    DirectX::SimpleMath::Vector3 knockbackVelocity = DirectX::SimpleMath::Vector3::Zero; ///< [RT] 吹き飛ばし速度。数フレームで減衰しつつ漂う

    // ================================================================
    // 移動の実体 ── 書き手=DroneMovementSystem
    // ================================================================

    DirectX::SimpleMath::Vector3 currentVelocity = DirectX::SimpleMath::Vector3::Zero; ///< [RT] 現在速度。毎フレーム積分して座標を確定する

    float hoverTimeOffset = 0.0f; ///< [RT] ホバー揺れの位相 [秒]。個体ごとにずれるので群れが同期して揺れない

    // ================================================================
    // 個体パラメータ（インスペクタで調整）
    // ================================================================

    float moveSpeed   = 30.0f;  ///< 基準速度 [m/s]。各ステートがこれに倍率を掛けて上限にする
    float orbitRadius =  8.0f;  ///< 旋回半径 [m]。※現在どの陣形も参照していない（削除候補）
};

/// 1000機ぶんが連続配置されるので、増やすほど帯域を食う。
/// 型サイズ順に手で並べ替える運用は追加のたびに崩れるため、測って見張る方式にする。
/// 引っかかったら上限を上げる前に「本当にこのフィールドが要るか」を疑うこと。
static_assert(sizeof(DroneComponent) <= 160,
              "DroneComponent が肥大化している。フィールドを追加する前に見直すこと");
