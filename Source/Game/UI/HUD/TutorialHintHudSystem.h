#pragma once
// ===========================================================================
// 概要: チュートリアルのヒント（ボタンのアイコンと1行の説明）を画面の下中央に出す System。
//
// 責務:
//   - 今の項目のアイコンと文言を出し、項目が変わったらフェードインし直す
//   - 帯の真下の細いゲージで、今の項目の進み具合を出す
//   - 回避の補助スローの「今だ」の間は、画面の中央に押すボタンを大きく出す
//
// 備考: 何をいつ出すかは TutorialSystem が TutorialState に書く。ここは見た目だけを持つ。
//       全体登録はせず、TutorialScene だけが登録する。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/Tutorial/TutorialState.h"

#include "Engine/UI/ECS/Components/UITransformComponent.h"

#include <DirectXMath.h>
#include <string>

class TutorialHintHudSystem : public CCL::ECS::IfSystem<TutorialHintHudSystem>
{
public:
    TutorialHintHudSystem() : IfSystem("TutorialHintHudSystem") {}

    // TutorialSystem が同じステージで書いた TutorialState を、同じフレームのうちに映す
    std::vector<const char *> GetRunAfter() const override { return {"TutorialSystem"}; }

    void Update(float dt) override;

private:
    // --- 生成 ---
    void               Build();
    CCL::ECS::EntityID SpawnImage(CCL::ECS::EntityID canvas, const char *name, DirectX::XMFLOAT4 color, int sortOrder);
    CCL::ECS::EntityID SpawnIcon(CCL::ECS::EntityID canvas, const char *name, const UITransformComponent &t, int sortOrder);
    CCL::ECS::EntityID SpawnText(CCL::ECS::EntityID canvas, const char *name, const char *text);

    // 帯の中身の置き方。画面の下辺の中央を基準に、左端と縦の中央を合わせる。位置と大きさは ApplyHint が決める
    static UITransformComponent RowTransform();

    // --- 更新 ---
    void ApplyHint(const TutorialStepDef &def, bool assist); // アイコンと文言を書き、文の幅に合わせて並べ直す
    void UpdateGauge(const TutorialState &state, float dt);
    void UpdateNowButton(const TutorialState &state, float dt);

    // x は帯の中心からの横位置（左端）、y は帯の中心からの縦位置（中心。下が正）
    void SetRect(CCL::ECS::EntityID id, float x, float y, float width, float height);
    void SetAlpha(CCL::ECS::EntityID id, float alpha);

    // 文字の幅を測る仕組みが UI に無いので、半角は高さの半分、全角は高さと同じ幅として数える
    static float EstimateWidth(const std::string &text, float pixelSize);

    bool m_built = false;

    // --- 帯 ---
    CCL::ECS::EntityID m_bandID  = CCL::ECS::InvalidEntityID; // 下地の黒帯
    CCL::ECS::EntityID m_barID   = CCL::ECS::InvalidEntityID; // 帯の真下のゲージの地
    CCL::ECS::EntityID m_fillID  = CCL::ECS::InvalidEntityID; // ゲージの中身
    CCL::ECS::EntityID m_iconID  = CCL::ECS::InvalidEntityID; // ボタンのアイコン
    CCL::ECS::EntityID m_plusID  = CCL::ECS::InvalidEntityID; // 同時押しの「+」
    CCL::ECS::EntityID m_icon2ID = CCL::ECS::InvalidEntityID; // 同時押しの2つ目のアイコン
    CCL::ECS::EntityID m_textID  = CCL::ECS::InvalidEntityID; // 説明の1行

    // --- 画面の中央 ---
    CCL::ECS::EntityID m_nowIconID = CCL::ECS::InvalidEntityID; // 「今だ」の間に大きく出すボタン

    int   m_lastStep   = -1;    // 最後に書いた項目。変わったら書き換える
    bool  m_lastAssist = false; // 最後に書いたとき「今だ」だったか
    bool  m_hasSecond  = false; // 今のヒントが同時押し（アイコン2つ）か
    float m_bandWidth  = 0.0f;  // 帯の幅（px）
    float m_alpha      = 0.0f;  // 帯全体の不透明度
    float m_gaugeShown = 0.0f;  // 画面に出しているゲージの割合
    float m_nowAlpha   = 0.0f;  // 中央のボタンの不透明度
};
