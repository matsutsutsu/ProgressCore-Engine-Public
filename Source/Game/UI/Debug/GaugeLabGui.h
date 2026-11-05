#pragma once
// HP ゲージの見た目を確かめるための、HUD System の OnGui に置く HP ボタン
// HealthComponent を直接書き換えるので、被弾モーションや無敵時間は通らない
#include "Game/Logics/Combat/Components/HealthComponent.h"

#include <algorithm>
#include <cstdio>
#include <imgui.h>

namespace CCL::GameUI
{
    /**
     * @brief HP をボタンで減らす・回復する欄
     * @param damageRatio 「ダメージ」ボタン1回で減らす割合（最大 HP に対する）
     * @note 0 にすると死亡の処理が走ってゲージが隠れるので、減らしても 1 は残す
     */
    inline void DrawHealthButtons(HealthComponent &hp, float damageRatio)
    {
        const float maxHp = std::max(hp.maxHealth, 1.0f);
        char        label[32];
        std::snprintf(label, sizeof(label), "ダメージ %d%%", static_cast<int>(damageRatio * 100.0f + 0.5f));

        if (ImGui::Button(label))
        {
            hp.currentHealth = std::max(1.0f, hp.currentHealth - maxHp * damageRatio);
        }
        ImGui::SameLine();
        if (ImGui::Button("回復 20%"))
        {
            hp.currentHealth = std::min(maxHp, hp.currentHealth + maxHp * 0.2f);
        }
        ImGui::SameLine();
        if (ImGui::Button("HP 20% に")) hp.currentHealth = maxHp * 0.2f;
        ImGui::SameLine();
        if (ImGui::Button("全快")) hp.currentHealth = maxHp;
    }
} // namespace CCL::GameUI
