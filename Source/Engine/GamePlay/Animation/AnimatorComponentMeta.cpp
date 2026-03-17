#include "ECS/Core/CCL_World.h" // Worldのキャストに必要
#include "Engine/GamePlay/Animation/AnimatorComponent.h"
#include "Engine/GamePlay/Graphics/Core/ModelComponent.h" // ModelComponentへのアクセスに必要

#include "Engine/Serialization/ComponentSerializers.h"
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/ComponentRegistry.h"
#include "Engine/Serialization/Meta/ComponentMeta.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#include "Engine/Serialization/Meta/ComponentMetaJson.h"

template <> struct ComponentMeta<AnimatorComponent> {
    static constexpr bool        registered   = true;
    static constexpr const char *displayName  = "Animator";
    static constexpr bool        hasCustomGui = true;

    // ★ 拡張されたシグネチャを使用
    static bool CustomGui(AnimatorComponent &anim, unsigned long long entityID, void *worldPtr)
    {
        bool changed = false;
        // void* から正規の World ポインタへキャスト
        auto *world = static_cast<CCL::ECS::Core::World *>(worldPtr);

        ImGui::DragFloat("Speed", &anim.playbackSpeed, 0.05f, -5.0f, 5.0f);
        ImGui::Checkbox("Loop", &anim.isLoop);
        ImGui::Separator();

        bool animFound = false;
        if (world) {
            // ★ 他のコンポーネント（ModelComponent）の情報を安全に取得
            auto *modelComp =
                world->GetComponent<ModelComponent>(static_cast<CCL::ECS::EntityID>(entityID));
            if (modelComp && modelComp->modelPtr) {
                auto *resource = modelComp->modelPtr->GetResource();
                if (resource) {
                    const auto &animations = resource->GetAnimations();
                    if (!animations.empty()) {
                        animFound               = true;
                        std::string currentName = (anim.currentAnimIndex >= 0 &&
                                                      anim.currentAnimIndex < animations.size())
                                                      ? animations[anim.currentAnimIndex].name
                                                      : "(Stop)";

                        if (ImGui::BeginCombo("Animation", currentName.c_str())) {
                            bool isStop = (anim.currentAnimIndex == -1);
                            if (ImGui::Selectable("(Stop)", isStop)) {
                                anim.Play(-1);
                                changed = true;
                            }
                            if (isStop) ImGui::SetItemDefaultFocus();

                            for (int i = 0; i < (int)animations.size(); ++i) {
                                bool isSelected = (anim.currentAnimIndex == i);
                                if (ImGui::Selectable(animations[i].name.c_str(), isSelected)) {
                                    anim.Play(i, anim.isLoop, anim.playbackSpeed);
                                    changed = true;
                                }
                                if (isSelected) ImGui::SetItemDefaultFocus();
                            }
                            ImGui::EndCombo();
                        }

                        // プログレスバー表示
                        if (anim.currentAnimIndex >= 0 && anim.currentAnimIndex < animations.size()) {
                            float duration = animations[anim.currentAnimIndex].secondsLength;
                            if (duration > 0.0f) {
                                float progress = anim.currentTimer / duration;
                                if (anim.isLoop)
                                    progress = fmod(progress, 1.0f);
                                else if (progress > 1.0f)
                                    progress = 1.0f;

                                char overlay[32];
                                sprintf_s(overlay, "%.2f / %.2f s", anim.currentTimer, duration);
                                ImGui::ProgressBar(progress, ImVec2(0.0f, 0.0f), overlay);
                            }
                        }
                    }
                }
            }
        }

        if (!animFound) {
            ImGui::TextDisabled("No Model or Animations found.");
            ImGui::InputInt("Anim Index (Raw)", &anim.currentAnimIndex);
        }

        if (ImGui::TreeNode("Debug Info")) {
            ImGui::Text("Timer: %.3f", anim.currentTimer);
            ImGui::Text("Finished: %s", anim.isFinished ? "True" : "False");
            if (anim.targetAnimIndex != -1) {
                ImGui::Separator();
                ImGui::TextColored(ImVec4(1, 1, 0, 1),
                    "Transitioning: %d -> %d",
                    anim.currentAnimIndex,
                    anim.targetAnimIndex);
                float t = (anim.transitionDuration > 0.0f)
                              ? (anim.transitionTimer / anim.transitionDuration)
                              : 0.0f;
                ImGui::ProgressBar(t, ImVec2(0.0f, 0.0f), "Blending");
            }
            ImGui::TreePop();
        }
        return changed;
    }

    static const std::vector<FieldDescriptor> &Fields()
    {
        static const std::vector<FieldDescriptor> fields = {
            META_FIELD_FLOAT(
                AnimatorComponent, playbackSpeed, "playbackSpeed", "Playback Speed", 0.05f, -5.0f, 5.0f, "Playback"),
            META_FIELD_BOOL(AnimatorComponent, isLoop, "isLoop", "Loop", "Playback"),
            META_FIELD_INT(
                AnimatorComponent, currentAnimIndex, "currentAnimIndex", "Current Anim Index", -1, 999, "State")};
        return fields;
    }
};

void RegisterAnimationMeta()
{
    auto &jsonReg = ComponentRegistry::Instance();
    ComponentMetaJson::RegisterMetaT<AnimatorComponent>(jsonReg, "Animator");

    auto &guiReg = ComponentGuiRegistry::Instance();
    ComponentMetaImGui::RegisterGuiMeta<AnimatorComponent>(guiReg);
}