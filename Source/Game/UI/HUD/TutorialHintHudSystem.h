#pragma once
// ===========================================================================
// 概要: チュートリアルのヒント（ボタンのアイコンと1行の説明）を画面の下中央に出す System。
//
// 責務:
//   - TutorialState の項目から、出すアイコンと文言を決める
//   - 文言が変わったら一度消してから出し直し、新しい指示だと気付けるようにする
//   - 出す・消すをフェードで行う
//
// 備考: どの項目をいつ出すかは TutorialSystem が決める（TutorialState.showHint）。ここは見た目だけを持つ。
//       アイコンの画像は InputIconSystem が割り当てから決めるので、キーボードとパッドのどちらにも追従する。
//       全体登録はせず、TutorialScene だけが登録する。
//       UI はプレイヤーが現れた最初のフレームに1回だけ作る（レベルの読み込みより前に作ると、読み込みで消されるため）。
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
        const char *action    = nullptr; // アイコンにするアクション名（InputConfig.json の Name）
        const char *text      = "";      // 説明の1行（UTF-8）
        bool        emphasize = false;   // 強調するか（補助スローの「今！」）
    };

    // UI エンティティを作る。プレイヤーが現れた最初のフレームに1回だけ呼ぶ
    void Build();

    /**
     * @brief 項目に対応するヒントを返す。
     * @param state いまの項目と補助スロー中か。
     * @return 出すヒント。
     */
    Hint SelectHint(const TutorialState &state) const;

    /**
     * @brief ヒントの中身を UI へ書き、横幅を文言に合わせて並べ直す。
     * @param hint 書くヒント。
     */
    void ApplyHint(const Hint &hint);

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

    bool m_built = false;

    CCL::ECS::EntityID m_bandID = CCL::ECS::InvalidEntityID; // 下地の帯
    CCL::ECS::EntityID m_iconID = CCL::ECS::InvalidEntityID; // ボタンのアイコン
    CCL::ECS::EntityID m_textID = CCL::ECS::InvalidEntityID; // 説明の1行

    std::string m_currentText;    // いま出している文言（変わったら出し直す）
    float       m_alpha   = 0.0f; // 全体の不透明度（フェード）
    float       m_pulse   = 0.0f; // 強調のときの明滅の位相（秒）

    // --- 調整値 ---
    static constexpr float kBottom    = 190.0f; // 画面の下端から帯の中心まで（px）。左下のゲージと字幕の位置を避ける
    static constexpr float kBandH     = 64.0f;  // 帯の高さ（px）
    static constexpr float kPadX      = 28.0f;  // 帯の左右の余白（px）
    static constexpr float kIconSize  = 46.0f;  // アイコンの一辺（px）
    static constexpr float kIconGap   = 16.0f;  // アイコンと文の間（px）
    static constexpr float kTextSize  = 26.0f;  // 文字の高さ（px）
    static constexpr float kEmphSize  = 34.0f;  // 強調のときの文字の高さ（px）
    static constexpr float kFadeSpeed = 10.0f;  // フェードの速さ（Damp の speed）
};
