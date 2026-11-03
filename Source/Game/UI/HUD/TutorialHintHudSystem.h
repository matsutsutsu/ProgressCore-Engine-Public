#pragma once
// ===========================================================================
// 概要: チュートリアルのヒント（ボタンのアイコンと1行の説明）を画面の下中央に出す System。
//
// 責務:
//   - プレイヤーが現れたら、TutorialHint プレハブ（帯・ゲージ・アイコン・文・中央のボタン）を生成する
//   - 今の項目のアイコンと文言を出し、文の長さに合わせて帯の幅と部品の並びを決める
//   - 項目が変わったらフェードインし直す
//   - 帯の真下の細いゲージで、今の項目の進み具合を出す
//   - 回避の補助スローの「今だ」の間は、画面の中央に押すボタンを大きく出す
//
// 備考: 何をいつ出すかは TutorialSystem が TutorialState に書く。ここは見た目だけを持つ。
//       見た目はプレハブ（Assets/Prefabs/UI/TutorialHint.json）が持ち、インスペクタで調整して
//       インスペクタのプレハブ保存で残す。並べ方の寸法・切り替える色はルートの TutorialHintStyleComponent。
//       全体登録はせず、TutorialScene だけが登録する。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/Tutorial/TutorialState.h"
#include "Game/UI/HUD/TutorialHintStyleComponent.h"

#include <string>

class TutorialHintHudSystem : public CCL::ECS::IfSystem<TutorialHintHudSystem>
{
public:
    TutorialHintHudSystem() : IfSystem("TutorialHintHudSystem") {}

    // TutorialSystem が同じステージで書いた TutorialState を、同じフレームのうちに映す
    std::vector<const char *> GetRunAfter() const override { return {"TutorialSystem"}; }

    void Update(float dt) override;

private:
    // TutorialHint プレハブを生成し、部品の子を名前で引く
    void SpawnHud();

    // --- 更新 ---
    // アイコンと文言を書き、文の幅に合わせて並べ直す
    void ApplyHint(const TutorialStepDef &def, bool assist, const TutorialHintStyleComponent &style);
    void UpdateGauge(const TutorialState &state, const TutorialHintStyleComponent &style, float dt);
    void UpdateNowButton(const TutorialState &state, const TutorialHintStyleComponent &style, float dt);

    // x は帯の中心からの横位置（左端）、y は帯の中心からの縦位置（中心。下が正）
    void SetRect(CCL::ECS::EntityID id, float x, float y, float width, float height);
    void SetAlpha(CCL::ECS::EntityID id, float alpha);

    // 文字の幅を測る仕組みが UI に無いので、半角は高さの半分、全角は高さと同じ幅として数える
    static float EstimateWidth(const std::string &text, float pixelSize);

    CCL::ECS::EntityID m_rootID  = CCL::ECS::InvalidEntityID; // プレハブのルート（Canvas と TutorialHintStyle）
    bool               m_spawned = false;                     // 生成を試したか（失敗しても毎フレーム読み直さない）

    // --- 帯（TutorialHintRow の子） ---
    CCL::ECS::EntityID m_bandID  = CCL::ECS::InvalidEntityID; // 下地の黒帯
    CCL::ECS::EntityID m_barID   = CCL::ECS::InvalidEntityID; // 帯の真下のゲージの地
    CCL::ECS::EntityID m_fillID  = CCL::ECS::InvalidEntityID; // ゲージの中身
    CCL::ECS::EntityID m_iconID  = CCL::ECS::InvalidEntityID; // ボタンのアイコン
    CCL::ECS::EntityID m_plusID  = CCL::ECS::InvalidEntityID; // 同時押しの「+」
    CCL::ECS::EntityID m_icon2ID = CCL::ECS::InvalidEntityID; // 同時押しの2つ目のアイコン
    CCL::ECS::EntityID m_textID  = CCL::ECS::InvalidEntityID; // 説明の1行

    // --- 画面の中央 ---
    CCL::ECS::EntityID m_nowIconID = CCL::ECS::InvalidEntityID; // 「今だ」の間に大きく出すボタン

    int   m_lastStep   = -1;    // 最後に出した項目。変わったらフェードインし直す
    bool  m_hasSecond  = false; // 今のヒントが同時押し（アイコン2つ）か
    float m_bandWidth  = 0.0f;  // 帯の幅（px）
    float m_alpha      = 0.0f;  // 帯全体の不透明度
    float m_gaugeShown = 0.0f;  // 画面に出しているゲージの割合
    float m_nowAlpha   = 0.0f;  // 中央のボタンの不透明度
};
