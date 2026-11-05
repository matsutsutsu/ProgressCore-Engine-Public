#pragma once
// ===========================================================================
// 概要: タイトル画面のメニューを作り、ボタンでシーンを切り替える System
//
// 責務:
//   - タイトルロゴ・ボタン・操作のヒントを作る
//   - ロゴを、斬撃の光が走った跡に現れるように出す
//   - ボタンに合わせて、ゲーム・チュートリアルへの切り替えか、アプリの終了を頼む
//
// 備考: 「設定」のボタンは SettingsScreenSystem が受ける
//       全体登録はせず、TitleScene が登録する
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/UI/ECS/Components/UIButtonComponent.h"

class TitleMenuSystem : public CCL::ECS::IfSystem<
    TitleMenuSystem,
    CCL::ECS::Read<UIButtonComponent>>
{
public:
    TitleMenuSystem() : IfSystem("TitleMenuSystem") {}

    void Initialize() override; // GameFlowState を用意し、ボタンのイベントを購読する
    void Update(float dt) override;

private:
    // タイトルの文字とボタンを作る（最初の Update で1回だけ）
    void SpawnMenu();

    // タイトルロゴ（本体・白く光る写し・斬撃の光）を作る
    void SpawnLogo(CCL::ECS::EntityID canvas);

    // ロゴの登場演出を進める（斬撃の光が走った跡にロゴが現れ、走り終えたら白く光る）
    void AnimateLogo(float dt);

    bool m_menuSpawned   = false; // メニューを作ったか
    bool m_transitioning = false; // シーンの切り替えか終了を頼んだ後か

    CCL::ECS::EntityID m_logoID      = CCL::ECS::InvalidEntityID; // ロゴ本体（UIImage）
    CCL::ECS::EntityID m_logoFlashID = CCL::ECS::InvalidEntityID; // ロゴを白く光らせる写し（加算合成）
    CCL::ECS::EntityID m_slashID     = CCL::ECS::InvalidEntityID; // ロゴを切り開く斬撃の光（加算合成）
    float              m_logoTime    = 0.0f;                      // メニューを作ってからの秒数
};
