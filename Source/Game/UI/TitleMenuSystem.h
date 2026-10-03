#pragma once
// ===========================================================================
// 概要: タイトル画面のメニューを作り、ボタンでシーンを切り替える System。
//
// 責務:
//   - タイトルの文字・START / QUIT ボタン・操作のヒントを作る
//   - START ならゲームへの切り替えを頼み、QUIT なら終了の要求を立てる
//
// 備考: 文字は画面の左に寄せ、右と上は 3D の背景（ボスと群れ）のために空ける。
//       全体登録（REGISTER_*）はせず、TitleScene が登録する（ゲーム本編でメニューが出ないように）。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/UI/ECS/Components/UIButtonComponent.h"

class TitleMenuSystem
    : public CCL::ECS::IfSystem<TitleMenuSystem, CCL::ECS::Read<UIButtonComponent>>
{
public:
    TitleMenuSystem() : IfSystem("TitleMenuSystem") {}

    void Initialize() override; // ボタンのイベントを購読するだけ
    void Update(float dt) override;

private:
    /**
     * @brief タイトルの文字とボタンを作る。
     *
     * @note Initialize ではなく最初の Update で呼ぶ。
     *       System の Initialize は登録時（TitleScene::Initialize の中）に走り、その後で背景のレベルを読むと、
     *       LoadScene が PersistentTag の無いエンティティを全部消してメニューも消えるため。
     */
    void SpawnMenu();

    bool m_menuSpawned   = false; // メニューを作ったか（1回だけ作るため）
    bool m_transitioning = false; // 切り替えを頼んだ後か（連打や何度も呼ばれるのを無視するため）
};
