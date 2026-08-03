#include "ComponentGuiRegistry.h"
#include "Engine/ECS/Core/CCL_World.h" // Worldへのアクセスに必要

#ifdef ENABLE_EDITOR
#include <imgui.h>
#endif

#include "Engine/Graphics/RHI/GpuResourceUtils.h" // テクスチャロード用
#include "Engine/Graphics/RHI/Graphics.h"         // デバイス取得用
#include "Engine/Platform/Core/Dialog.h"              // ファイル選択ダイアログ用

#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#include "Engine/Graphics/Shader/Shared/ShaderShared.h"

// JSONとファイル入出力
#include <fstream>
#include <json.hpp>
#include <filesystem>
using json = nlohmann::json;


// ============================================================================
// シングルトンの実体を絶対にこの .cpp 1箇所だけで生成させる
// ============================================================================
ComponentGuiRegistry &ComponentGuiRegistry::Instance()
{
    static ComponentGuiRegistry s;
    return s;
}


// Draw関数にも EntityID と World* を追加して渡す
void ComponentGuiRegistry::Draw(
    TypeID id, void* data, EntityID entity, CCL::ECS::World* world)
{
    ResolveBindings(); // 描画する直前に、IDマップが未完成なら完成させる

    auto idIt = m_typeIdToIndex.find(id);
    if (idIt == m_typeIdToIndex.end()) {
#ifdef ENABLE_EDITOR
        ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "No GUI registered for this ID.");
#endif
        return;
    }
    auto it = m_registry.find(idIt->second);
    if (it != m_registry.end()) {
        it->second.guiFunc(data, entity, world);
    }
}