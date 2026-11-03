#pragma once
// ===========================================================================
// 概要: 設定画面（視点の感度・BGM と効果音の音量）を開いて操作させ、設定をカメラと音声へ反映する System。
//
// 責務:
//   - 「設定」のボタンが押されたら、設定画面を上に重ねて開く
//   - 選んでいる行の値を左右の入力で変え、「戻る」・Esc・B で閉じて保存する
//   - 開いている間は、下の画面のボタンを操作できないようにする
//   - 設定の値を毎フレーム、カメラと音声へ反映する
//
// 備考: 全シーンで動かす。ポーズ中も操作できるよう、描画ステージに置く。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/UI/ECS/Components/UIButtonComponent.h"
#include "Engine/UI/ECS/Components/UIInteractableComponent.h"

#include <array>
#include <string>
#include <vector>

// 設定画面が開いているか（World リソース）
// PauseSystem はこれを見て、開いている間は Esc でポーズを切り替えない
struct SettingsScreenState
{
    bool open = false; // 設定画面を開いているか（閉じる入れ替えの途中も true）
};

class SettingsScreenSystem : public CCL::ECS::IfSystem<SettingsScreenSystem,
                                                       CCL::ECS::Read<UIInteractableComponent>,
                                                       CCL::ECS::Read<UIButtonComponent>>
{
public:
    SettingsScreenSystem() : IfSystem("SettingsScreenSystem") {}

    // UIInputSystem が決めた focus / clicked を同じフレームで読むため、その後に動かす
    std::vector<const char *> GetRunAfter() const override { return {"UIInputSystem"}; }

    void Initialize() override;     // SettingsScreenState を用意する
    void Update(float dt) override; // 設定を反映し、設定画面を開く・操作する・閉じる

private:
    // 設定の行。「戻る」は値を持たない最後の行
    enum Row
    {
        RowSensitivity,
        RowBgm,
        RowSfx,
        RowBack,
        RowCount
    };
    static constexpr int kValueRows = RowBack; // 値を持つ行の数

    // --- 反映と効果音 ---
    void ApplySettings();                       // 設定の値をカメラと音声へ反映する
    void PlaySound(const char *eventName) const; // 効果音を1回鳴らす（音声が無いシーンでは何もしない）

    // --- 開閉 ---
    void Open(CCL::ECS::EntityID opener); // 設定画面を作って開く。opener は開いたボタン
    void Close();                         // 設定画面を消して閉じ、保存する
    void UpdateFade(float dt);            // 重ねて入れ替える途中の不透明度を進め、描画リストへ書く

    /**
     * @brief 値を1段ずつ変える < か > の小さなボタンを作る。マウスで押すためのもので、フォーカスは受けない。
     * @param name  エンティティの名前。記号の文字は後ろに "Glyph" を付けた名前になる。
     * @param glyph ボタンに出す記号（"<" か ">"）。
     * @param x     中心の X（px、画面の左上から）。
     * @param y     中心の Y（px、画面の左上から）。
     * @return 作ったボタン。記号の文字と一緒に m_overlay へ足す。
     */
    CCL::ECS::EntityID SpawnArrow(CCL::ECS::EntityID canvas, const char *name, const char *glyph, float x, float y);

    // --- 値 ---
    void        Adjust(int row, int dir);  // 行の値を dir（-1 / +1）の向きに1段変える
    void        RefreshValues();           // 値の表示（ゲージ・文字）を今の設定に合わせる
    float       ValueRatio(int row) const; // 行のゲージの値（0〜1）
    std::string ValueText(int row) const;  // 行の値の文字（"1.00x"、"80" など）

    // --- 開閉の状態 ---
    bool m_open          = false; // 設定画面を開いているか
    bool m_closePending  = false; // 閉じる要求を受けたか
    bool m_closeByCancel = false; // Esc・B で閉じたか（「閉じる」の音を鳴らすか）

    // --- 重ねて入れ替える途中の状態 ---
    bool  m_fadeStarted         = false; // 設定画面が出そろい、入れ替えを始めたか
    bool  m_closing             = false; // 閉じる入れ替えの途中か（終わったら Close）
    bool  m_restoreBelowPending = false; // 次のフレームで、下の画面の不透明度を元に戻すか
    float m_fade                = 0.0f;  // 入れ替えの進み具合（0 = 下の画面、1 = 設定画面）

    // --- 作った部品 ---
    CCL::ECS::EntityID              m_opener = CCL::ECS::InvalidEntityID; // 開いたボタン（閉じたらここへ選択を戻す）
    std::vector<CCL::ECS::EntityID> m_overlay;                            // 作ったエンティティ（閉じるときにまとめて消す）
    std::vector<CCL::ECS::EntityID> m_disabled;                           // 開いている間に操作できなくした下の画面の部品

    std::array<CCL::ECS::EntityID, RowCount>   m_rows{};      // 行のボタン
    std::array<CCL::ECS::EntityID, kValueRows> m_dec{};       // 行の < ボタン
    std::array<CCL::ECS::EntityID, kValueRows> m_inc{};       // 行の > ボタン
    std::array<CCL::ECS::EntityID, kValueRows> m_gauge{};     // 行のゲージ
    std::array<CCL::ECS::EntityID, kValueRows> m_valueText{}; // 行の値の文字

    // --- 最後に音声へ反映した音量（負ならまだ反映していない） ---
    float m_appliedBgm = -1.0f; // BGM
    float m_appliedSfx = -1.0f; // 効果音
};
