#pragma once
// ===========================================================================
// 概要: チュートリアルのヒント（ボタンのアイコンと1行の説明）を画面の下中央に出す System。
//
// 責務:
//   - TutorialState の項目から、出すアイコン（同時押しなら2つ）と文言を決める
//   - 文言が変わったら一度消してから出し直し、新しい指示だと気付けるようにする
//   - 下地の帯をゲージにして進み具合を出し、達成したら満たしたまま余韻を見せる
//   - 回避の補助スローの「今だ」の間は、画面の中央に押すボタンを大きく出す
//
// 備考: どの項目をいつ出すかは TutorialSystem が決める（TutorialState.showHint）。ここは見た目だけを持つ。
//
//       アイコンの画像は InputIconSystem が割り当てから決めるので、キーボードとパッドのどちらにも追従する。
//
//       全体登録はせず、TutorialScene だけが登録する。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/Tutorial/TutorialState.h"

#include <string>

class TutorialHintHudSystem : public CCL::ECS::IfSystem<TutorialHintHudSystem>
{
public:
    TutorialHintHudSystem() : IfSystem("TutorialHintHudSystem") {}

    // TutorialSystem が同じステージで書いた TutorialState を、同じフレームのうちに映す
    std::vector<const char *> GetRunAfter() const override { return {"TutorialSystem"}; }

    void Update(float dt) override;

private:
    // 1つのヒントの中身
    struct Hint
    {
        InputIconKind icon      = InputIconKind::Action; // アイコンの種類。移動と視点は固定のアイコン
        const char   *action    = nullptr;               // icon が Action のときにアイコンにするアクション名（InputConfig.json の Name）
        const char   *action2   = nullptr;               // 同時押しの2つ目のアクション名。nullptr なら1つだけ出す
        const char   *text      = "";                    // 説明の1行（UTF-8）
        bool          emphasize = false;                 // 強調するか（補助スローの「今だ」）
    };

    // UI エンティティを作る。プレイヤーが現れた最初のフレームに1回だけ呼ぶ
    void Build();

    /**
     * @brief 項目に対応するヒントを返す。
     * @param state いまの項目と、補助スローの「今だ」の段階か。
     * @return 出すヒント。ヒントの無い項目と教え終えた後は、文言が空のヒント。
     */
    Hint SelectHint(const TutorialState &state) const;

    /**
     * @brief ヒントの中身を UI へ書き、横幅を文言に合わせて並べ直す。
     * @param hint 書くヒント。
     */
    void ApplyHint(const Hint &hint);

    // 下地の帯の中身を、今の進み具合まで滑らかに伸ばす（毎フレーム）
    void UpdateGauge(const TutorialState &state, float dt);

    // 補助スローの「今だ」の間、画面の中央のボタンを脈打たせながら出す
    void UpdateNowButton(const TutorialState &state, float dt);

    /**
     * @brief 文字列の描画幅を見積もる（px）。
     *
     * 文字の幅を測る仕組みが UI に無いので、半角は高さの半分・それ以外は高さと同じ幅として数える。
     * 帯の幅と、アイコンと文の組を中央へ寄せるのに使う。少しずれても帯の余白に収まる。
     *
     * @param text      UTF-8 の文字列。
     * @param pixelSize 文字の高さ（px）。
     * @return 見積もった幅（px）。
     */
    static float EstimateWidth(const std::string &text, float pixelSize);

    bool m_built = false; // UI を作ったか

    // --- 下中央の帯 ---
    CCL::ECS::EntityID m_bandID  = CCL::ECS::InvalidEntityID; // 下地の帯（ゲージの地を兼ねる）
    CCL::ECS::EntityID m_fillID  = CCL::ECS::InvalidEntityID; // 帯の中身。進み具合だけ左から伸びる
    CCL::ECS::EntityID m_iconID  = CCL::ECS::InvalidEntityID; // ボタンのアイコン
    CCL::ECS::EntityID m_plusID  = CCL::ECS::InvalidEntityID; // 同時押しの「＋」
    CCL::ECS::EntityID m_icon2ID = CCL::ECS::InvalidEntityID; // 同時押しの2つ目のアイコン
    CCL::ECS::EntityID m_textID  = CCL::ECS::InvalidEntityID; // 説明の1行

    // --- 画面の中央 ---
    CCL::ECS::EntityID m_nowIconID = CCL::ECS::InvalidEntityID; // 「今だ」の間に大きく出すボタン

    std::string m_currentText;         // いま出している文言
    bool        m_hasSecond  = false;  // いまのヒントが同時押し（アイコン2つ）か
    float       m_alpha      = 0.0f;   // 帯全体の不透明度（フェード）
    float       m_pulse      = 0.0f;   // 強調のときの明滅の位相（秒）
    float       m_bandWidth  = 400.0f; // 帯の幅（px。ApplyHint が文言に合わせて決める）
    float       m_gaugeShown = 0.0f;   // 帯の中身の割合。進み具合へ滑らかに寄せる
    float       m_nowAlpha   = 0.0f;   // 中央のボタンの不透明度
};
