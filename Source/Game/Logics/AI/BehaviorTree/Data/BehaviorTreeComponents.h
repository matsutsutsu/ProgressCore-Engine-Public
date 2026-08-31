/**
 * ECSエンティティが保持するAIの状態管理コンポーネント
 *
 * 役割：
 *   各エンティティ固有の「現在の思考位置」や「判断材料（Blackboard）」を管理する。
 *
 * 設計思想：
 *   「思考（Data）」と「状態（Component）」を分離。
 *   コンポーネントには実行時のみ必要な最小限の状態のみを持たせ、
 *   巨大な木構造自体はアセットとして全Entityで共有する（BTAssetCache）。
 *
 * ここに置いてよいものの境界:
 *   どの敵でも意味を持つものだけ。特定の敵にしか無い概念（ボスの第2形態、
 *   ドローンの陣形など）は、その敵のコンポーネントへ置く。
 *   例: 第2形態のBTパスとHP閾値 → BossPhaseComponent
 *       ボスの行動要求・陣形要求     → BossCommandComponent
 *   この線引きが崩れると、敵の種類を足すたびにこのファイルが太る。
 */
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "SimpleMath.h"
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeData.h"
#include "Engine/ECS/Common/CCL_Types.h"

 // AIの判断材料を格納するブラックボード（Blackboard）
struct AIBlackboard {
    // --- ターゲット ---
    /// このAIが狙っている相手。書き手は知覚システム（BossPerceptionSystem）。
    ///
    /// なぜ座標だけでなくIDを持つか:
    ///   以前は BattleContext.playerPos を全AIへ直接配っており、「誰を狙うか」という
    ///   概念がコード上に存在しなかった（1体しか敵がいなかったので成立していた）。
    ///   雑魚を群れで出すと、個体ごとに狙う相手が変わる。IDを持たせておけば、
    ///   行動側は「ターゲットのHP」「ターゲットの向き」など座標以外も引ける。
    CCL::ECS::EntityID targetEntity = CCL::ECS::InvalidEntityID;

    DirectX::SimpleMath::Vector3 targetPosition;     // ターゲットの座標
    float distanceToTarget = 0.0f;                   // ターゲットとの距離

    // --- 自分のステータス ---
    float healthPercentage = 100.0f;                 // 自分の残りHP（0〜100）

    // --- ダメージ蓄積・回避システム ---
    float accumulatedDamage    = 0.0f;               // 現在溜まっているダメージ量
    float evadeDamageThreshold = 20.0f;              // いくつダメージを受けたら逃げるかの閾値
};

// AIの内部状態とブラックボード（直列化対象）
// 例えば、どのノードを実行中か、どのアセットを使うかなどの情報を持つ
struct BehaviorTreeComponent {
    // --- 巨大なオブジェクト群 (ポインタ/コンテナ系) ---
    /// 木の実体。BTAssetCache が同じパスに対して1つだけ作り、全Entityで共有する。
    std::shared_ptr<const BTAsset> sharedAsset;

    /// 読み込むBTのパス。空なら「BTを持たない」＝評価しない。
    std::string assetPath;
    std::string loadedAssetPath = "";
    std::string lastFrameTreeLog = "";

    std::vector<BTDebugState> debugNodeStates;
    std::vector<float> nodeTimers;
    std::vector<int> runningNodes;

    // --- 構造体 ---
    AIBlackboard blackboard;

    // --- プリミティブ型 (4バイト) ---
    AssetID assetId = 0;

    // --- デバッグ専用（実行時のみ・非シリアライズ）---
    /// この BTNodeID の枝「だけ」を評価する。-1 で通常どおりルートから評価する。
    ///        狙った技をその場で出して確認するためのもので、木そのものは書き換えない。
    ///        書き手はデバッグUI（BehaviorTreeSystem::OnGui）。
    int debugForcedNodeId = -1;

    /// 上を適用済みかの記録。切り替わった瞬間に実行状態をリセットするために使う。
    ///        書き手は BehaviorTreeSystem。
    int debugAppliedForcedNodeId = -1;

    /// 木の評価を止める。BossCommandComponent が上書きされなくなるので、
    ///        エディタから書いた指示（陣形など）がそのまま残る。
    ///
    /// なぜ要るか:
    ///   陣形やアニメを詰めている間もボスは技を選び続けるので、調整中の指示が
    ///   次のフレームに別の技で塗り潰される。assetPath を空にすれば止まるが
    ///   （BossPhaseSystem の第2形態移行がその手）、パスを失うので調整用途には使えない。
    ///
    /// @note 止めても currentActionId は保持される。移動中に止めるとボスが歩き続けるので、
    ///       止める側が併せて 0 を書く（＝BossActionSystem が Idle になる）。
    ///       書き手はデバッグUI（BehaviorTreeSystem::OnGui / FormationEditorWindow）。
    bool debugPaused = false;

    // --- プリミティブ型 (2バイト) ---
    BTNodeID runningNodeId = 0;
    BTNodeID previousRunningNodeId = 0xFFFF;
    ActionID lastLogActionId = 0xFFFF;
};
