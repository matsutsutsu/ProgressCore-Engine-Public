#pragma once
// ===========================================================================
// 概要: Esc かパッドの START でゲームを一時停止し、ポーズメニューを重ねて出す System。
//
// 責務:
//   - 一時停止を切り替え、GameFlowState.paused とゲームの時間を止める・戻す
//   - 一時停止したらメニューの UI を作り、解除したら消す
//   - 「ゲームに戻る」と「タイトルへ」のボタンを受ける
//   - 製品ビルドでは、アプリが裏に回ったら自動で一時停止する
//
// 備考: 一時停止中は描画だけが回るので、解除の入力を拾えるよう描画ステージに置く。
//       全体登録はせず、GameScene と EditorScene が登録する。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/UI/ECS/Components/UIButtonComponent.h"

#include <vector>

class PauseSystem
    : public CCL::ECS::IfSystem<PauseSystem, CCL::ECS::Read<UIButtonComponent>>
{
public:
    PauseSystem() : IfSystem("PauseSystem") {}

    void Initialize() override;     // GameFlowState を用意し、ボタンのイベントを購読する
    void Update(float dt) override; // 一時停止の切り替えの入力を見る

    // メニューの親子のリスト（TransformComponent）を繋ぐので申告する
    std::vector<CCL::ECS::TypeID> GetWriteTypes() const override;

private:
    void OpenPause();  // 一時停止してメニューを作る
    void ClosePause(); // 再開してメニューを消す

    /**
     * @brief 作ったメニューを、親の子のリストに繋ぐ（エディタの Hierarchy で木の形に見せるため）。
     * @return 繋ぎ終えたら true。生成がまだ反映されていなければ false（次のフレームでもう一度呼ぶ）。
     */
    bool LinkOverlay();

    bool                            m_paused        = false; // 一時停止中か
    bool                            m_transitioning = false; // タイトルへの切り替えを頼んだ後か
    bool                            m_linkPending   = false; // 作ったメニューをまだ親の子のリストに繋いでいないか
    std::vector<CCL::ECS::EntityID> m_overlay;               // 作ったメニューのエンティティ（解除するときに消す）
};
