#pragma once
// ===========================================================================
// 概要: タイトル画面のメニューを作り、ボタンでシーンを切り替える System。
//
// 責務:
//   - タイトルロゴ・ボタン（はじめる / チュートリアル / 設定 / 終了）・操作のヒントを作る
//   - ロゴを、斬撃の光が走った跡に現れるように出す
//   - 「はじめる」ならゲームへ、「チュートリアル」ならチュートリアルへの切り替えを頼み、
//     「終了」ならアプリの終了の要求を立てる
//
// 備考:
//   文字は画面の左に寄せ、右と上は 3D の背景（ボスと群れ）のために空ける。
//
//   「設定」のボタンは SettingsScreenSystem が受けて、設定画面を開く。
//
//   全体登録（REGISTER_*）はせず、TitleScene が登録する。ゲーム本編でメニューが出ないようにする。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/UI/ECS/Components/UIButtonComponent.h"

class TitleMenuSystem
    : public CCL::ECS::IfSystem<TitleMenuSystem, CCL::ECS::Read<UIButtonComponent>>
{
public:
    TitleMenuSystem() : IfSystem("TitleMenuSystem") {}

    void Initialize() override; // GameFlowState を用意し、ボタンのイベントを購読する
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

    // タイトルロゴ（本体・白く光る写し・斬撃の光）を作る。SpawnMenu から呼ぶ
    void SpawnLogo(CCL::ECS::EntityID canvas);

    // ロゴの登場演出を進める。斬撃の光が左から右へ走り、その跡にロゴが現れ、走り終えたら全体が白く光る
    void AnimateLogo(float dt);

    bool m_menuSpawned   = false; // メニューを作ったか
    bool m_transitioning = false; // シーンの切り替えか終了を頼んだ後か

    CCL::ECS::EntityID m_logoID      = CCL::ECS::InvalidEntityID; // ロゴ本体（UIImage）
    CCL::ECS::EntityID m_logoFlashID = CCL::ECS::InvalidEntityID; // ロゴを白く光らせる写し（UIImage・加算合成）
    CCL::ECS::EntityID m_slashID     = CCL::ECS::InvalidEntityID; // ロゴを切り開く斬撃の光（UIImage・加算合成）
    float              m_logoTime    = 0.0f;                      // メニューを作ってからの秒数（ロゴの演出の時計）
};
