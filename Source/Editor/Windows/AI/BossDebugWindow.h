#pragma once
// ===========================================================================
// 概要: ボスを試すときの操作を1枚にまとめたデバッグ用ウィンドウ
//
// 責務:
//   - [AI]       BT の評価を止める・再開する。好きな枝を強制実行する
//   - [HP・形態] 形態と HP の状態を出し、HP を減らさない・全回復・とどめの手前・第2形態へ飛ぶを操作する
//   - [表示]     ボスのデバッグ描画の System（頭上の文字・地面の帯・ドローン・跳躍の軌道）の設定を出す
//
// 備考: 書き換えるのは次の値だけ
//         BehaviorTreeComponent の debugPaused / debugForcedNodeId
//         HealthComponent の currentHealth / debugNoDamage
//         BossPhaseComponent の debugSkipToPhase2（実際に形態を変えるのは BossPhaseSystem）
//       「第2形態へ（演出あり）」だけは、とどめと同じ FinishBlowEvent も出す
//       [表示] は各 System の OnGui をそのまま呼ぶ。設定の持ち主は System のまま
// ===========================================================================
#include "Editor/Core/EditorWindow.h"
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeData.h"
#include "Engine/ECS/Common/CCL_Types.h"

#include <string>

namespace CCL::ECS { class World; }
struct BehaviorTreeComponent;
struct BossCommandComponent;
struct BossPhaseComponent;
struct BossActionComponent;
struct HealthComponent;

class BossDebugWindow : public EditorWindow
{
public:
    BossDebugWindow() : EditorWindow("Boss Debugger", false) {}

protected:
    void DrawContents(EditorContext &context) override;

private:
    // シーン内のボス。見つからない Component は nullptr
    struct BossHandles
    {
        BehaviorTreeComponent *bt     = nullptr; // ボスの BT（所有しない）
        BossCommandComponent  *cmd    = nullptr; // ボスへの行動要求（所有しない）
        BossPhaseComponent    *phase  = nullptr; // 形態（所有しない）
        BossActionComponent   *action = nullptr; // 本体の動き（所有しない）
        HealthComponent       *health = nullptr; // HP（所有しない）

        CCL::ECS::World   *world = nullptr;                    // ボスのいるワールド（所有しない）
        CCL::ECS::EntityID id    = CCL::ECS::InvalidEntityID; // ボスのエンティティ
    };

    // 最初に見つかったボスを返す（1対1の戦闘前提）
    BossHandles FindBoss(EditorContext &context) const;

    // --- タブ ---

    // [AI] 止める・戻すの操作と、強制実行のツリー
    void DrawAiTab(const BossHandles &boss);
    // [HP・形態] 状態の表示と、HP・形態を動かすボタン
    void DrawHealthTab(const BossHandles &boss);
    // [表示] ボスのデバッグ描画の System の OnGui を並べる
    void DrawDisplayTab(EditorContext &context);

    // --- [AI] の部品 ---

    // 止める・戻すの操作と、今の状態の表示
    void DrawStatus(const BossHandles &boss);

    // 1ノード分の行を描き、子へ降りる。inheritedName は省略した Decorator の名前
    void DrawNode(const BTAsset &asset, BTNodeID nodeId, const BossHandles &boss,
                  const std::string &inheritedName, int depth);

    // 行に出す文字列（名前・行動名・param）
    std::string MakeLabel(const BTAsset &asset, BTNodeID nodeId, const std::string &inheritedName) const;

    // 枝の中で最初に見つかる行動の表示名。なければ nullptr
    const char *FirstActionName(const BTAsset &asset, BTNodeID nodeId) const;

    // 強制実行する枝を切り替える。-1 で通常の評価へ戻す
    void SetForced(const BossHandles &boss, int nodeId);

    bool m_showConditions = false; // ツリーに条件の行も出すか
};
