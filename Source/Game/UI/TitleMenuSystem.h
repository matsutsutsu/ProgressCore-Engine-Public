#pragma once
// タイトル画面のメニューUIを生成し、ボタンクリックでシーン遷移するシステム。
//
// 設計方針:
//   - タイトル文字＋START/QUIT ボタンを ECS UI として生成。文字は画面左に寄せ、
//     右半分は3D背景（群れが組む斬線）のために空ける。
//   - UIButtonClickedEvent を購読し、onClickAction のハッシュで遷移を分岐。
//   - グローバル登録（REGISTER_*_SYSTEM）はしない。TitleScene が手動登録する
//     （ゲーム本編でタイトルメニューが湧かないようにするため）。
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/UI/ECS/Components/UIButtonComponent.h"

class TitleMenuSystem
    : public CCL::ECS::IfSystem<TitleMenuSystem, CCL::ECS::Read<UIButtonComponent>>
{
public:
    TitleMenuSystem() : IfSystem("TitleMenuSystem") {}

    void Initialize() override; // イベント購読のみ
    void Update(float dt) override;

private:
    /// タイトル文字とボタンを生成する。
    ///
    /// なぜ Initialize ではなく最初の Update か:
    ///   システムの Initialize は登録時（= TitleScene::Initialize の中）に走る。その後で
    ///   3D背景レベルを読むと、LoadScene が PersistentTag 以外を全消去するのでUIごと消える。
    ///   レベル読み込みが終わったあと＝最初の Update まで待つ。
    void SpawnMenu();

    bool m_menuSpawned   = false; // 生成済みか（1回だけ生成するためのラッチ）
    bool m_transitioning = false; // 遷移予約後か（連打・多重発火対策）
};
