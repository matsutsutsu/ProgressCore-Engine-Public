#pragma once
// ===========================================================================
// 概要: 画面右上に操作の説明（アイコンと行名）を出す System。
//
// 責務:
//   - InputPromptConfig.json から、出すアクションと行名を読む
//   - プレイヤーが現れた最初のフレームに、アイコンと行名の UI エンティティを1回だけ作る
//   - アイコンが隠れている行は行名も隠す
//   - 大きさ・位置の調整値を持ち、OnGui で変えたら配置だけ書き直す
//
// 備考: どのキーが割り当てられているかは InputConfig.json だけが持ち、アイコンの画像を決めるのは
//       InputIconSystem。この System は InputIconComponent を付けるところまで行う。
//       Initialize で作らないのは、タイトル画面も全 System を登録するため。プレイヤーの操作説明なので、
//       プレイヤーが居ない画面には出さない。デバイスを持ち替えてもエンティティは作り直さない
//       （作り直すと配置が1フレーム崩れる）。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"

#include <string>
#include <vector>

struct UITransformComponent;

class InputPromptSystem : public CCL::ECS::IfSystem<InputPromptSystem>
{
public:
    InputPromptSystem()
        : IfSystem("InputPromptSystem")
    {
    }

    void Update(float dt) override;
    void OnGui() override;

private:
    // 設定の JSON の1行ぶん
    struct ConfigRow
    {
        std::string action; // アクション名。空なら icon の固定アイコンを使う
        std::string icon;   // 固定アイコンの名前（"Move" / "Camera"）。action が空のときだけ使う
        std::string label;  // 画面に出す行名（ASCII のみ）
    };

    // 操作の説明の行を作る。プレイヤーが現れた最初のフレームに1回だけ呼ぶ
    void BuildRows();

    // InputPromptConfig.json を読む。読めなければ既定の並びを返す
    void LoadConfig(std::vector<ConfigRow> &outRows) const;

    // index 行目のアイコンと行名の矩形を調整値から求める。
    // 生成時と OnGui での並べ直しで同じ式を使うため、配置の計算はここだけに置く
    void ComputeRow(size_t index, UITransformComponent &outIcon,
                    UITransformComponent &outLabel) const;

    // 今の調整値を、作ってある全部の行に反映する（OnGui から呼ぶ）
    void ApplyLayout();

    bool m_built = false; // 行を作ったか（1回だけ作るため）

    std::vector<CCL::ECS::EntityID> m_iconIDs;  // 各行のアイコン
    std::vector<CCL::ECS::EntityID> m_labelIDs; // 各行の行名

    // --- 調整値 ---
    float m_iconSize    = 59.0f;  // アイコンの一辺（px）
    float m_rowHeight   = 59.0f;  // 行の高さ（px）。m_iconSize より小さいと行が重なる
    float m_marginRight = 28.0f;  // 画面の右端からの余白（px）
    float m_marginTop   = 12.0f;  // 画面の上端からの余白（px）
    float m_labelWidth  = 108.0f; // 行名の幅（px）
    float m_labelGap    = 10.0f;  // アイコンと行名の間（px）
    float m_textSize    = 28.0f;  // 行名の文字の高さ（px）
    float m_alpha       = 0.7f;   // 不透明度。いつも出ているので控えめにする
};
