#pragma once
// 画面右上の操作説明UI。何の行を出すかを決め、UI実体を1回だけ組む。
//
// 設計方針:
//   - 「どのアクションを・どんな名前で出すか」だけを InputPromptConfig.json が持つ。
//     どのキーが割り当てられているかは InputConfig.json が唯一の情報源。
//   - アイコンの張り替えは InputIconSystem の担当。本Systemは InputIconComponent を
//     付けるところまでで手を引く（表現＝アイコン解決 / 供給＝画面構成 の分離）。
//     この分離のおかげで、メニューの決定表示など他の画面が同じ解決経路に乗れる。
//   - 生成は「プレイヤーが居る」と分かってから1回だけ。持ち替えでエンティティを
//     作り直さない（作り直すとレイアウトが1フレーム飛ぶ）。
//     Initialize で組まないのは、タイトル画面も全システムを登録するため。
//     プレイヤーの操作説明なので、プレイヤーが居ない画面には出してはいけない。
//   - 見た目の調整値はメンバに持ち、OnGui から動かす。生成と配置計算を分けてあるので、
//     値を変えてもエンティティは作り直さず矩形だけ書き換わる。
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
    // 設定JSONの1行ぶん。
    struct ConfigRow
    {
        std::string action; // アクション名。空なら iconName 側の固定アイコンを使う
        std::string icon;   // "Move" / "Camera" などの固定アイコン名。空なら action を使う
        std::string label;  // 画面に出す行名（ASCII のみ）
    };

    // 操作説明の行を実際に組む。プレイヤーが現れた最初のフレームに1回だけ走る。
    void BuildRows();

    // InputPromptConfig.json を読む。失敗時は既定の並びを返す。
    void LoadConfig(std::vector<ConfigRow> &outRows) const;

    // index 行目のアイコンと行名の矩形を、調整値から計算する。
    //   生成時と OnGui からの再配置で同じ式を使うため、配置の計算はここだけに置く。
    void ComputeRow(size_t index, UITransformComponent &outIcon,
                    UITransformComponent &outLabel) const;

    // 現在の調整値を、生成済みの全行へ反映する（OnGui から呼ぶ）。
    void ApplyLayout();

    bool m_built = false; // 生成済みか（1回だけ組むためのラッチ）

    std::vector<CCL::ECS::EntityID> m_iconIDs;  // 各行のアイコン
    std::vector<CCL::ECS::EntityID> m_labelIDs; // 各行の行名

    // --- 調整用 ---
    float m_iconSize    = 59.0f;  // アイコンの一辺(px)
    float m_rowHeight   = 59.0f;  // 行の高さ(px)。iconSize より小さいと行が重なる
    float m_marginRight = 28.0f;  // 画面右端からの余白(px)
    float m_marginTop   = 12.0f;  // 画面上端からの余白(px)。ボスHPゲージの下に置く
    float m_labelWidth  = 108.0f; // 行名の幅(px)
    float m_labelGap    = 10.0f;  // アイコンと行名の間隔(px)
    float m_textSize    = 28.0f;  // 行名の文字高(px)
    float m_alpha       = 0.7f;   // 不透明度。常時表示なので控えめにする
};
