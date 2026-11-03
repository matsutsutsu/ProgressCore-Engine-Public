#pragma once
// ===========================================================================
// 概要: チュートリアルの画面右上に、操作の説明（アイコンと行名）を出す System。
//
// 責務:
//   - プレイヤーが現れたら、InputPrompt プレハブ（調整値）を置き、行の UI を1回だけ作る
//   - 調整値から行を並べ、アイコンが隠れている行は行名も隠す
//
// 備考: 出す行と文言は InputPromptConfig.json、並べ方は InputPromptStyleComponent が持つ。
//       全体登録はせず、TutorialScene だけが登録する。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/UI/Input/InputPromptStyleComponent.h"

#include <string>
#include <vector>

struct UITransformComponent;

class InputPromptSystem : public CCL::ECS::IfSystem<InputPromptSystem>
{
public:
    InputPromptSystem() : IfSystem("InputPromptSystem") {}

    void Update(float dt) override;
    void OnGui() override; // 表示中の行数と、調整の手順の案内

private:
    // 設定の JSON の1行ぶん
    struct ConfigRow
    {
        std::string action; // アクション名。空なら icon の固定アイコンを使う
        std::string icon;   // 固定アイコンの名前（"Move" / "Camera"）。action が空のときだけ使う
        std::string label;  // 画面に出す行名（ASCII のみ）
    };

    // --- 行の組み立てと配置 ---
    // InputPrompt プレハブを置き、操作の説明の行を作る（プレイヤーが現れた最初のフレームに1回だけ）
    void Build();

    /**
     * @brief index 行目のアイコンと行名の矩形を、調整値から求める。
     * @param outIcon  アイコンの矩形を受け取る。
     * @param outLabel 行名の矩形を受け取る。
     */
    void ComputeRow(size_t index, const InputPromptStyleComponent &style,
                    UITransformComponent &outIcon, UITransformComponent &outLabel) const;

    // 調整値を、作ってある全部の行に反映する
    void ApplyLayout(const InputPromptStyleComponent &style);

    // 調整値を返す。InputPrompt プレハブが無ければ既定値
    const InputPromptStyleComponent &GetStyle() const;

    // --- 設定 ---
    // InputPromptConfig.json を読む。読めなければ既定の並びを返す
    void LoadConfig(std::vector<ConfigRow> &outRows) const;

    bool               m_built   = false;                     // 行を作ったか
    CCL::ECS::EntityID m_styleID = CCL::ECS::InvalidEntityID; // InputPrompt プレハブ（調整値）

    std::vector<CCL::ECS::EntityID> m_iconIDs;  // 各行のアイコン
    std::vector<CCL::ECS::EntityID> m_labelIDs; // 各行の行名
};
