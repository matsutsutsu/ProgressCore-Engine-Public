#include "JoltPhysicsComponentMeta.h"

// JSONシリアライズ自動登録用
#include "Engine/Serialization/ComponentRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaJson.h"

// GUI自動登録用（※プロジェクトの設計に依存します。後述の解説参照）
// #include "Editor/Inspector/ComponentGuiRegistry.h"
// #include "Engine/Serialization/Meta/ComponentMetaImGui.h"

namespace {
    // ⚙️ 配送センターの自動受付システム（比喩）
    // プログラム起動時に一度だけ動き、名簿に自動で書き込むための専用構造体
    struct JoltPhysicsAutoRegistrar {
        JoltPhysicsAutoRegistrar()
        {
            // ----------------------------------------------------------------
            // 1. JSONシリアライズシステムの自動登録
            // ----------------------------------------------------------------
            // ComponentRegistry は Instance() を持つシングルトンのため、
            // 起動時の静的初期化フェーズで安全に登録を流し込めます。
            auto &jsonRegistry = ComponentRegistry::Instance();

            ComponentMetaJson::RegisterMetaT<JoltBoxColliderComponent>(
                jsonRegistry, "JoltBoxCollider");
            ComponentMetaJson::RegisterMetaT<JoltSphereColliderComponent>(
                jsonRegistry, "JoltSphereCollider");
            ComponentMetaJson::RegisterMetaT<JoltRigidbodyComponent>(jsonRegistry, "JoltRigidbody");

            // ----------------------------------------------------------------
            // 2. ImGui (Inspector) システムの自動登録について
            // ----------------------------------------------------------------
            // もし ComponentGuiRegistry がシングルトン設計（Instance()を持つ）であれば、
            // 以下のコードのコメントアウトを外すだけでGUI側の登録も完全自動化されます。
            //
            // auto& guiRegistry = ComponentGuiRegistry::Instance();
            // ComponentMetaImGui::RegisterGuiMeta<JoltBoxColliderComponent>(guiRegistry);
            // ComponentMetaImGui::RegisterGuiMeta<JoltSphereColliderComponent>(guiRegistry);
            // ComponentMetaImGui::RegisterGuiMeta<JoltRigidbodyComponent>(guiRegistry);
        }
    };

    // グローバルスコープで静的インスタンスを生成
    // これにより、main() 関数が呼ばれる前にコンストラクタが走り、登録が完了する
    static JoltPhysicsAutoRegistrar s_joltPhysicsRegistrar;
} // namespace