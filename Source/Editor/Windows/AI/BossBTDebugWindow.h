#pragma once
// ===========================================================================
// 概要: ボスの BT を止めたり、好きな枝を強制実行したりするデバッグ用ウィンドウ。
//
// 責務:
//   - ボスの BT の評価を止める・再開する
//   - BT を見やすいツリーで並べ、選んだ枝だけを繰り返し実行させる
//   - 今の状態（通常・停止中・強制実行中）を表示する
//
// 備考: 書き換えるのは BehaviorTreeComponent の debugPaused / debugForcedNodeId だけ。
//       選んだ枝から評価する処理は BehaviorTreeSystem 側にある（ApplyDebugForcing）。
// ===========================================================================
#include "Editor/Core/EditorWindow.h"
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeData.h" // BTAsset, BTNodeID

#include <string>

struct BehaviorTreeComponent;
struct BossCommandComponent;

class BossBTDebugWindow : public EditorWindow
{
public:
    BossBTDebugWindow() : EditorWindow("Boss BT Debugger", false) {}

protected:
    void DrawContents(EditorContext &context) override;

private:
    // シーン内のボス。見つからなければ全部 nullptr
    struct BossHandles
    {
        BehaviorTreeComponent *bt  = nullptr; // ボスの BT（所有しない）
        BossCommandComponent  *cmd = nullptr; // ボスへの行動要求（所有しない）
    };

    // 最初に見つかったボスを返す（1対1の戦闘前提）
    BossHandles FindBoss(EditorContext &context) const;

    // 止める・戻すの操作と、今の状態の表示
    void DrawStatus(const BossHandles &boss);

    // 1ノード分の行を描き、子へ降りる。inheritedName は省略した Decorator の名前
    void DrawNode(const BTAsset &asset, BTNodeID nodeId, const BossHandles &boss,
                  const std::string &inheritedName, int depth);

    // 行に出す文字列（名前・行動名・param）
    std::string MakeLabel(const BTAsset &asset, BTNodeID nodeId, const std::string &inheritedName) const;

    // 枝の中で最初に見つかる行動の表示名。無ければ nullptr
    const char *FirstActionName(const BTAsset &asset, BTNodeID nodeId) const;

    // 強制実行する枝を切り替える。-1 で通常の評価へ戻す
    void SetForced(const BossHandles &boss, int nodeId);

    bool m_showConditions = false; // ツリーに条件の行も出すか
};
