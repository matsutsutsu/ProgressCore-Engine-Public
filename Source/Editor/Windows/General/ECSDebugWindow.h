#pragma once
// ===========================================================================
// 概要: ECS の内部状態を見るデバッグウィンドウ
//
// 責務:
//   - Memory & Chunks: アーキタイプ別のチャンク構成と使用状況、エンティティごとの値と生のメモリ
//   - Execution Graph: TaskScheduler が決めた並列実行のバッチ
//
// 備考: System ごとの処理時間は Tracy と System Manager ウィンドウで見る
// ===========================================================================
#include "Editor/Core/EditorWindow.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemManager.h"
#include <cstdint>

#ifdef ENABLE_EDITOR
#include <imgui.h>
#endif

class ECSDebugWindow : public EditorWindow
{
public:
    ECSDebugWindow();

protected:
    void DrawContents(EditorContext &context) override;

private:
    // 各タブの描画処理
    void DrawMemoryTab(CCL::ECS::World *world);
    void DrawExecutionGraphTab(CCL::ECS::SystemManager *systemManager);

    // 型 ID から表示名を返す。未登録なら "Unknown (ID:n)"（内部の static バッファを返すので、すぐ使うこと）
    const char *GetFriendlyTypeName(uint32_t id);
};
