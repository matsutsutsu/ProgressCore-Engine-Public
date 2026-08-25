#pragma once
#include "Editor/Core/EditorWindow.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemManager.h"
#include <cstdint>

#ifdef ENABLE_EDITOR
#include <imgui.h>
#endif

// =========================================================
// ECS デバッガ
//   固有の価値がある2機能に絞る:
//     - Memory & Chunks : アーキタイプ別チャンク構成とメモリの可視化・インスペクタ
//     - Execution Graph : TaskScheduler が解決した並列実行バッチの可視化
//   ※ システムの処理負荷計測は Tracy と System Manager ウィンドウに集約したため、
//      当ウィンドウからは Profiler / Pending Ops を撤去した。
// =========================================================
class ECSDebugWindow : public EditorWindow {
public:
    ECSDebugWindow();

protected:
    void DrawContents(EditorContext& context) override;

private:
    // 各タブの描画処理
    void DrawMemoryTab(CCL::ECS::World* world);
    void DrawExecutionGraphTab(CCL::ECS::SystemManager* systemManager);

    // 型IDから名前を取得するヘルパー
    const char* GetFriendlyTypeName(uint32_t id);
};
