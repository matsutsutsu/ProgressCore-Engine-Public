#pragma once
/// @file InputPromptSystem.h
/// @brief 画面右上の操作説明UI。何の行を出すかを決め、UI実体を1回だけ組む。
///
/// 設計方針:
///   - 「どのアクションを・どんな名前で出すか」だけを InputPromptConfig.json が持つ。
///     どのキーが割り当てられているかは InputConfig.json が唯一の情報源。
///   - アイコンの張り替えは InputIconSystem の担当。本Systemは InputIconComponent を
///     付けるところまでで手を引く（表現＝アイコン解決 / 供給＝画面構成 の分離）。
///     この分離のおかげで、メニューの決定表示など他の画面が同じ解決経路に乗れる。
///   - 生成は Initialize の1回だけ。持ち替えでエンティティを作り直さない
///     （作り直すとレイアウトが1フレーム飛ぶ）。
#include "Engine/ECS/System/CCL_IfSystem.h"

#include <string>
#include <vector>

class InputPromptSystem : public CCL::ECS::IfSystem<InputPromptSystem>
{
  public:
    InputPromptSystem()
        : IfSystem("InputPromptSystem")
    {
    }

    void Initialize() override;
    void Update(float dt) override;

  private:
    /// @brief 設定JSONの1行ぶん。
    struct ConfigRow
    {
        std::string action; ///< アクション名。空なら iconName 側の固定アイコンを使う
        std::string icon;   ///< "Move" / "Camera" などの固定アイコン名。空なら action を使う
        std::string label;  ///< 画面に出す行名（ASCII のみ）
    };

    /// @brief InputPromptConfig.json を読む。失敗時は既定の並びを返す。
    void LoadConfig(std::vector<ConfigRow> &outRows) const;

    std::vector<CCL::ECS::EntityID> m_iconIDs;  ///< 各行のアイコン
    std::vector<CCL::ECS::EntityID> m_labelIDs; ///< 各行の行名

    // --- 調整用 ---
    static constexpr float kIconSize    = 34.0f; ///< アイコンの一辺(px)
    static constexpr float kRowHeight   = 38.0f; ///< 行の高さ(px)
    static constexpr float kMarginRight = 28.0f; ///< 画面右端からの余白(px)
    static constexpr float kMarginTop   = 96.0f; ///< 画面上端からの余白(px)。ボスHPゲージの下に置く
    static constexpr float kLabelWidth  = 108.0f;///< 行名の幅(px)
    static constexpr float kTextSize    = 17.0f; ///< 行名の文字高(px)
    static constexpr float kAlpha       = 0.62f; ///< 不透明度。常時表示なので控えめにする
};
