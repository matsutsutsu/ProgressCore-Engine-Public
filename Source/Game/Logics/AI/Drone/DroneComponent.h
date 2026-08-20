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
#include "Engine/ECS/Common/CCL_Types.h"
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



    // 第2形態用・新規フォーメーション
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

    // 波状縦割りの方向読み用（TsunamiWave=左→右, これ=右→左）
    TsunamiWave_RL, ///< 【波状縦割り・右→右左】右端から順に倒れてくる波

    /// 【小隊突撃】5機編隊を組み、先頭がプレイヤーを追って突っ込む。
    ///   全機で1つの形を作る他の技と違い、小隊ごとに独立して動く。
    SquadAssault,
};

// ドローン個体の具体的な物理ステート
enum class DroneState : uint8_t
{
    Idle,         ///< 【有機的追従】バネ制御でフワフワと目標に追従する待機・陣形維持状態
    MoveToTarget, ///< 【機械的移動】直線的に指定位置へ素早く整列する状態
    FireCharge,   ///< 【高速突撃】目標へ向かって突撃する攻撃状態（ジャスト回避の対象）
    Stunned,      ///< 【硬直】切り払いで吹き飛ばされ、制御を失って漂う状態（無害・通常AI停止）
    Returning,    ///< 【離脱】突撃を終えて上方へ抜ける状態（無害。この後 MoveToTarget で隊列へ）
};

/// @brief FireCharge 中に「どうやって動くか」。
///
/// bool（追従するか否か）では足りなくなったので enum にした。
/// 剛体追従とバネ追従は見た目がまったく別物で、混同すると
/// 「隊列が1枚の板に見える」という致命的な劣化になる。
enum class ChargeMode : uint8_t
{
    /// fireDirection へ直進する。方向は突入時にロックされる（弾幕・カミカゼ）。
    Straight,

    /// targetPosition へ毎フレーム貼り付ける。形が一切歪まない剛体。
    ///   縦割り・挟み込みのように「壁that形を保ったまま動く」技専用。
    RigidFollow,

    /// targetPosition へバネで寄る。速度に応じて後ろへ流れ、曲がると遅れて曲がる。
    ///   隊列を組んで飛ぶ技（小隊）はこちら。剛体にすると追従に見えない。
    SoftFollow,
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

    /// @brief 突撃を開始した地点（実行時専用・非シリアライズ）。
    ///        ここからの飛距離で「もう飛び切った」を個体が判断する。
    DirectX::SimpleMath::Vector3 chargeStartPos = DirectX::SimpleMath::Vector3::Zero;

    /// @brief 隊列追従（ChargeMode::SoftFollow）で先渡しする速度（実行時専用・非シリアライズ）。
    ///        バネだけで速度を作ると必ず「速度 ÷ バネ定数」ぶん遅れ、
    ///        速い突入ほど隊列が間延びして数珠が千切れて見える。
    ///        「1つ前の機体の速度」をここへ渡し、バネにはずれの補正だけをさせる。
    ///        小隊全体の速度を渡してはいけない。全機が同じ方向へ同じ速さで飛び、
    ///        先頭への追従が見えなくなる（各自がプレイヤーへ突っ込む絵になる）。
    ///        書き手は小隊陣形（Formation）。
    DirectX::SimpleMath::Vector3 followFeedForward = DirectX::SimpleMath::Vector3::Zero;

    // --- 4バイト型 (float) ---
    float stateTimer = 0.0f;
    float hoverTimeOffset = 0.0f;
    float moveSpeed = 30.0f;
    float orbitRadius = 8.0f;

    /// @brief 吹き飛ばし減衰後の「立て直し」時間（実行時専用・非シリアライズ）。
    ///        knockbackTimer が切れた瞬間にセットされ、この間は Stunned のまま
    ///        その場で姿勢を戻す。localIndex でばらつかせ、群れが一斉ではなく
    ///        バラバラに復帰するようにしている。
    float recoverTimer = 0.0f;

    /// @brief 突撃を開始してからの経過時間（実行時専用・非シリアライズ）。
    ///        飛距離判定が成立しない場合（壁際・プレイヤーが遠すぎる）の保険。
    float chargeTimer = 0.0f;

    // --- 2バイト型 (uint16_t) ---
    uint16_t localIndex = 0;
    uint16_t totalDrones = 1;

    // --- 1バイト型 (enum/bool) ---
    DroneFormationType currentFormation = DroneFormationType::Hidden;
    DroneState currentState = DroneState::Idle;

    /// @brief 突撃1回ぶんの進行フェーズ（実行時専用・非シリアライズ）。
    ///        書き手は DroneCommandSystem（開始・中断）と DroneBrainSystem（終了判断）。
    DroneAttackPhase attackPhase = DroneAttackPhase::None;

    /// @brief 所属小隊。SquadRegistry::kNoSquad(0xFF) は無所属。
    ///        書き手は DroneManagerSystem のみ。一度割り当てたら維持する
    ///        （撃破のたびに振り直すと小隊がシャッフルされてしまう）。
    ///        128機を使う大技は無所属のまま localIndex/totalDrones で動くので、
    ///        小隊制と既存の陣形はそのまま共存する。
    uint8_t squadId = 0xFF;

    /// @brief 小隊内の位置。0 が先頭機で、大きいほど後方につく。
    ///        編隊オフセットの参照と、追従の鈍さ（後ろほど遅れる）に使う。
    uint8_t slotInSquad = 0;

    /// @brief 陣形スロットへの初回到着が完了したか（DroneBrainSystem のラッチ）。
    ///        true になると Idle に固定され、回転する targetPosition との
    ///        距離では MoveToTarget に戻らない（点滅防止）。
    bool hasReachedSlot = false;

    /// @brief ボスから「今攻撃せよ」の命令が来ているか（実行時専用・非シリアライズ）。
    ///        DroneCommandSystem が毎フレーム BossCommandComponent.droneAttackActive を
    ///        ここへ配布し、DroneBrainSystem が読む。
    ///        true → FireCharge へ遷移する。
    bool wantsAttack = false;

    /// @brief 陣形攻撃の進行度（0.0→1.0、実行時専用・非シリアライズ）。
    ///        ヒンジ回転（縦割りが橋のように倒れる）など「時間で形が動く」攻撃の時間軸。
    ///        攻撃陣形（Formation）が wantsAttack 中に進め、非攻撃時は 0 に戻す。
    float attackProgress = 0.0f;

    /// @brief FireCharge 中の移動モード（実行時専用・非シリアライズ）。
    ///        攻撃陣形が毎フレーム設定し、DroneBrainSystem が攻撃開始時に Straight へ戻す。
    ChargeMode chargeMode = ChargeMode::Straight;

    /// @brief dashモードの「発射待ち時間」（実行時専用・非シリアライズ）。
    ///        弾幕(KamikazeSwarm)のように順番に撃つ攻撃で、各機の発射順を作る。
    ///        FireCharge に入っても fireDelay>0 の間はその場で待機し、0になってから
    ///        タメ→リリースへ進む。攻撃陣形が forming 中に localIndex から設定し、
    ///        OrbitCircle(中立)で 0 にリセットする。
    float fireDelay = 0.0f;

    /// @brief 切り払い(CLEAVE)で吹き飛ばされている残り時間（実行時専用・非シリアライズ）。
    ///        >0 の間は通常AI(移動)を止めて knockbackVelocity で漂い、攻撃/被弾判定も失う
    ///        （押した先で自分に当たる理不尽を防ぎ、切り開いた道を通れる保証にする）。
    ///        0 になると通常AIが復帰し、バネ制御で隊列へ戻る。
    float knockbackTimer = 0.0f;

    /// @brief 吹き飛ばし中の速度（進行方向へ一律。数フレームで減衰しつつ漂う）。
    DirectX::SimpleMath::Vector3 knockbackVelocity = DirectX::SimpleMath::Vector3::Zero;
};