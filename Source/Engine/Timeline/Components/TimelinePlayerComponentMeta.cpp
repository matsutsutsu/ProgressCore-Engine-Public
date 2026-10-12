// TimelinePlayerComponent のシリアライズ/インスペクタ定義。

#include "Engine/Timeline/Components/TimelinePlayerComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"

#include <string>

#ifdef ENABLE_EDITOR
#include <imgui.h>
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#include "Engine/Platform/Core/Dialog.h"
#endif

template <>
struct ComponentMeta<TimelinePlayerComponent>
{
    CCL_META_USING_THIS(TimelinePlayerComponent)

    static constexpr const char *displayName       = "Timeline Player";
    static constexpr const char *category          = CCL::Categories::Camera;
    static constexpr int         sortPriority      = 370;
    static constexpr bool        hiddenInInspector = false;
    static constexpr bool        hasCustomGui      = true;
    static constexpr bool        isSerializable    = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(TimelinePlayerComponent &comp, unsigned long long /*entityID*/ = 0,
                          void * /*world*/ = nullptr)
    {
        // 処理フロー:
        // 1. タイムライン資産の指定と読み直し
        // 2. 再生設定
        // 3. カメラ優先度
        // 4. 実行時状態の表示
        // 5. テスト再生とスクラブ

        namespace mi = ComponentMetaImGui;
        bool changed = false;

        // 資産を差し替えたら読み直させる
        auto resetAsset = [&comp]() {
            comp.asset.reset();
            comp.hasLoadError = false;
            comp.activeShot   = -1;
            comp.hasLastPose  = false;
        };

        // ---------------------------------------------------------
        // 1. タイムライン資産
        // ---------------------------------------------------------
        ImGui::SeparatorText(u8"タイムライン資産");
        if (mi::InputText(u8"ファイル", comp.assetPath))
        {
            resetAsset();
            changed = true;
        }
        if (ImGui::Button(u8"参照..."))
        {
            if (auto pathOpt = FileDialog::OpenPreset("Timeline"))
            {
                comp.assetPath = FileDialog::ConvertToRelativePath(*pathOpt);
                resetAsset();
                changed = true;
            }
        }
        ImGui::SameLine();
        // JSON を手で直したあとに、シーンを読み直さず反映させる
        if (ImGui::Button(u8"読み直す")) resetAsset();

        if (comp.hasLoadError)
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), u8"読み込みに失敗（ログを参照）");
        else if (comp.asset)
            ImGui::TextDisabled(u8"長さ %.2f 秒 / カメラ %zu / SE %zu / エフェクト %zu / アニメ %zu / シグナル %zu",
                                comp.asset->duration, comp.asset->cameraShots.size(), comp.asset->sounds.size(),
                                comp.asset->effects.size(), comp.asset->animations.size(),
                                comp.asset->signals.size());

        // ---------------------------------------------------------
        // 2. 再生設定
        // ---------------------------------------------------------
        ImGui::SeparatorText(u8"再生");
        changed |= mi::DragFloat(u8"開始待ち(秒)", &comp.startDelay, 0.05f, 0.0f, 10.0f);
        changed |= mi::Checkbox(u8"シーン開始で自動再生 (playOnStart)", &comp.playOnStart);
        changed |= mi::InputText(u8"きっかけ名 (trigger)", comp.trigger,
                                 u8"ゲーム側の再生要求がこの名前を指したときに再生する（例: BossDefeated）。空なら名前では選ばれない。");
        changed |= mi::Checkbox(u8"スキップ許可 (allowSkip)", &comp.allowSkip);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(u8"攻撃/ジャンプでスキップ。展示会では ON 推奨。");

        // ---------------------------------------------------------
        // 3. カメラ優先度
        // ---------------------------------------------------------
        ImGui::SeparatorText(u8"カメラ優先度");
        changed |= mi::DragInt(u8"再生中 (activePriority)", &comp.activePriority, 1, -1000, 1000);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(u8"プレイヤー追従カメラ(200)より高くすること。");
        changed |= mi::DragInt(u8"待機中 (idlePriority)", &comp.idlePriority, 1, -1000, 1000);

        // ---------------------------------------------------------
        // 4. 実行時状態
        // ---------------------------------------------------------
        ImGui::SeparatorText(u8"状態 (実行時)");
        const int stateIdx = static_cast<int>(comp.state);
        ImGui::Text(u8"state=%s  time=%.2f  shot=%d", kTimelinePlayStateNames[stateIdx], comp.time,
                    comp.activeShot);

        // 発火済みの数を出す。
        // SE やエフェクトが出ないとき「発火していないのか / 発火したが見えないのか」を分けるため。
        ImGui::Text(u8"処理済み（スキップで捨てた分を含む）: SE %zu / エフェクト %zu / シグナル %zu", comp.nextSound,
                    comp.nextEffect, comp.nextSignal);

        // 握ったまま残ると、そのキャラはステートマシンに戻らず固まる。
        // 再生が終わったら 0 に戻るかを見られるように出す。
        std::size_t held = 0;
        for (const auto e : comp.animHeld)
            held += (e != CCL::ECS::InvalidEntityID) ? 1u : 0u;
        ImGui::Text(u8"アニメを握っている数: %zu", held);
        ImGui::TextDisabled(u8"スクラブ中は SE・エフェクト・アニメ・シグナルを出さない（最初から再生で確認）");

        // 役割の結び付きを出す。
        // カメラが動かないとき「役割が来ていないのか」を最初に疑えるようにするため。
        if (comp.bindings.empty())
            ImGui::TextDisabled(u8"役割: まだ何も結び付いていない");
        for (const auto &[role, id] : comp.bindings)
            ImGui::Text(u8"役割 %s → Entity %llu", role.c_str(), static_cast<unsigned long long>(id));

        // ---------------------------------------------------------
        // 5. テスト再生とスクラブ
        // ---------------------------------------------------------
        if (ImGui::Button(u8"最初から再生", ImVec2(140, 0)))
        {
            comp.RequestPlay();
            changed = true;
        }

        const float duration = comp.asset ? comp.asset->duration : 0.0f;
        if (duration > 0.0f)
        {
            ImGui::SameLine();
            // 再生位置をスライダーで直接動かして絵を確認する（プレビュー用スクラブ）。

            // previewing を立てないと、再生していない＝カメラを譲る、と判定されて絵が見えない。
            if (ImGui::SliderFloat(u8"スクラブ", &comp.time, 0.0f, duration, "%.2fs"))
            {
                comp.previewing = true;
                comp.prevTime   = comp.time;
                changed         = true;
            }
        }
        if (comp.previewing)
        {
            ImGui::SameLine();
            if (ImGui::Button(u8"プレビュー終了"))
            {
                comp.previewing = false;
                comp.state      = TimelinePlayState::Finished; // 待機優先度へ落としてプレイヤーカメラへ戻す
                changed         = true;
            }
        }

        return changed;
    }
#else
    static bool CustomGui(TimelinePlayerComponent &, unsigned long long, void *) { return false; }
#endif

    static const std::vector<FieldDescriptor> &Fields()
    {
        // asset 以下の実行時状態はシリアライズしない。
        static const std::vector<FieldDescriptor> fields = {
            CFIELD_ASSET(assetPath, "タイムライン資産", "Asset", "Timeline", nullptr)

            CFIELD_FLOAT_RANGED(startDelay, "開始待ち", 0.05f, 0.0f, 10.0f, "Playback")
            CFIELD(playOnStart, "シーン開始で自動再生", "Playback")
            CFIELD(trigger, "再生のきっかけ名", "Playback")
            CFIELD(allowSkip, "スキップ許可", "Playback")

            CFIELD_INT_RANGED(activePriority, "再生中の優先度", -1000, 1000, "Priority")
            CFIELD_INT_RANGED(idlePriority, "待機中の優先度", -1000, 1000, "Priority")};
        return fields;
    }
};

REGISTER_COMPONENT(TimelinePlayerComponent, "TimelinePlayer")
REGISTER_COMPONENT_EDITOR_GUI(TimelinePlayerComponent)
