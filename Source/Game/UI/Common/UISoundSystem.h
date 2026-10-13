#pragma once
// ===========================================================================
// 概要: メニューのボタンの操作に合わせて、効果音（選択の移動・決定・メニューが開く・閉じる）を鳴らす System。
//
// 責務:
//   - 選ばれているボタンが別のボタンへ移ったら「移動」を鳴らす
//   - ボタンが押されたら「決定」を鳴らす
//   - ボタンが1つも無い状態から現れたら「開く」、押されずに消えたら「閉じる」を鳴らす
//
// 備考: タイトル・ポーズ・リザルトのどの画面も、ボタンは MenuUiFactory::SpawnButton が作る。
//       画面ごとに鳴らす処理を書かず、ここでボタンの状態の変化だけを見て鳴らす。
//       音はイベント名で鳴らし、ファイルとの対応は Assets/Config/AudioConfigDXTK.json に書く。
//       ファイルが無いイベントは読み込みで飛ばされ、鳴らしても何も起きない（落ちない）。
//       描画ステージで走るのは、ポーズ中（ゲームの時間が止まっている間）もメニューは操作できるため。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/UI/ECS/Components/UIButtonComponent.h"
#include "Engine/UI/ECS/Components/UIInteractableComponent.h"

class UISoundSystem : public CCL::ECS::IfSystem<UISoundSystem,
                                                CCL::ECS::Read<UIInteractableComponent>,
                                                CCL::ECS::Read<UIButtonComponent>>
{
public:
    UISoundSystem() : IfSystem("UISoundSystem") {}

    // UIInputSystem が決めた hover / focus / clicked を同じフレームで読むため、その後に走らせる
    std::vector<const char *> GetRunAfter() const override
    {
        return {"UIInputSystem"};
    }

    void Update(float dt) override;

private:
    void Play(const char *eventName); // イベント名の効果音を1回鳴らす（音声が無いシーンでは何もしない）

    CCL::ECS::EntityID m_selected     = CCL::ECS::InvalidEntityID; // 前のフレームで選ばれていたボタン
    int                m_buttonCount  = 0;                         // 前のフレームのボタンの数（現れた・消えたを見る）
    bool               m_clickedLast  = false;                     // 直前に押されたか（押して消えたときに「閉じる」を重ねない）
};
