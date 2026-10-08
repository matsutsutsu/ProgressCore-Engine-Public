#pragma once
// ===========================================================================
// 概要: HP ゲージの演出を見比べるための、HUD System の OnGui 用の欄をまとめたヘッダー。
//
// 責務:
//   - ゲージの演出（グリッチ・光沢・外側の光・傾き・区切り）を調整する欄を描く
//   - HP をボタンで減らす・回復する欄を描く（戦わずに被弾・回復の演出を起こすため）
//
// 備考: PlayerHealthHudSystem と BossHealthHudSystem の OnGui から呼ぶ。2つの欄が同じ並びになるよう、
//       ここにまとめる。HP のボタンは HealthComponent を直接書き換えるので、ダメージの経路
//       （被弾モーション・無敵時間など）は通らない。見た目を確かめる専用。
// ===========================================================================
#include "Engine/UI/ECS/Components/UIProgressBarComponent.h"
#include "Game/Logics/Combat/Components/HealthComponent.h"

#include <algorithm>
#include <cstdio>
#include <imgui.h>

namespace CCL::GameUI
{
    // ゲージの演出を調整する欄
    inline void DrawGaugeEffectControls(UIProgressBarComponent &bar)
    {
        ImGui::SliderFloat("被弾のグリッチ", &bar.glitch, 0.0f, 1.0f);
        ImGui::SliderFloat("光沢", &bar.sheen, 0.0f, 0.5f);
        ImGui::SliderFloat("外側の光の強さ", &bar.auraColor.w, 0.0f, 1.0f);
        ImGui::SliderFloat("外側の光の広がり", &bar.auraWidth, 0.0f, 24.0f, "%.0f px");
        ImGui::SliderFloat("傾き", &bar.slant, 0.0f, 32.0f, "%.0f px");
        ImGui::SliderFloat("区切りの数", &bar.segments, 0.0f, 30.0f, "%.0f");
    }

    /**
     * @brief HP をボタンで減らす・回復する欄。
     * @param damageRatio 「ダメージ」ボタン1回で減らす割合（最大 HP に対する）
     * @note 0 にすると死亡の処理が走ってゲージが隠れるので、減らしても 1 は残す
     */
    inline void DrawHealthButtons(HealthComponent &hp, float damageRatio)
    {
        const float maxHp = std::max(hp.maxHealth, 1.0f);
        char        label[32];

        std::snprintf(label, sizeof(label), "ダメージ %d%%", static_cast<int>(damageRatio * 100.0f + 0.5f));
        if (ImGui::Button(label))
            hp.currentHealth = std::max(1.0f, hp.currentHealth - maxHp * damageRatio);
        ImGui::SameLine();
        if (ImGui::Button("回復 20%"))
            hp.currentHealth = std::min(maxHp, hp.currentHealth + maxHp * 0.2f);
        ImGui::SameLine();
        if (ImGui::Button("HP 20% に"))
            hp.currentHealth = maxHp * 0.2f;
        ImGui::SameLine();
        if (ImGui::Button("全快"))
            hp.currentHealth = maxHp;
    }
} // namespace CCL::GameUI
