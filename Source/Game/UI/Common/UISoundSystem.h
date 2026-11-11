#pragma once
// ===========================================================================
// 概要: メニューのボタンの操作に合わせて効果音（移動・決定・開く・閉じる）を鳴らす System
//
// 責務:
//   - 選ばれているボタンが移ったら「移動」、押されたら「決定」を鳴らす
//   - ボタンが現れたら「開く」、押されずに消えたら「閉じる」を鳴らす
//
// 備考: 画面ごとには鳴らさず、全画面のボタンの状態の変化だけを見て鳴らす
//       ポーズ中もメニューは操作できるので、描画ステージで動かす
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/UI/ECS/Components/UIButtonComponent.h"
#include "Engine/UI/ECS/Components/UIInteractableComponent.h"

class UISoundSystem : public CCL::ECS::IfSystem<
    UISoundSystem,
    CCL::ECS::Read<UIInteractableComponent>,
    CCL::ECS::Read<UIButtonComponent>>
{
public:
    UISoundSystem() : IfSystem("UISoundSystem") {}

    // UIInputSystem が決めた hover / focus / clicked を同じフレームで読むため、その後に動かす
    std::vector<const char *> GetRunAfter() const override { return {"UIInputSystem"}; }

    void Update(float dt) override;

private:
    // イベント名の効果音を1回鳴らす（音声がないシーンでは何もしない）
    void Play(const char *eventName);

    CCL::ECS::EntityID m_selected    = CCL::ECS::InvalidEntityID; // 前のフレームで選ばれていたボタン
    int                m_buttonCount = 0;                         // 前のフレームのボタンの数
    bool               m_clickedLast = false;                     // 最後にメニューが開いてから、ボタンが押されたか
};
