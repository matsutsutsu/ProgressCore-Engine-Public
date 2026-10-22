#pragma once
// ===========================================================================
// 概要: ムービーのシグナル（CinematicSignalEvent）を受けて、ゲーム側の演出を出す System。
//
// 責務:
//   - "Telop": 画面左下にテロップ（大きな名前と、その下の1行）を出し、時間が来たら消す
//   - スキップで終わったら、出ているテロップを急いで消す
//   - 知らない名前のシグナルは、警告ログを出して捨てる
//
// シグナルの引数:
//   Telop: param = 名前（大きな文字）、param2 = 下の1行（空なら出さない）、value = 表示する秒数（0 なら 3 秒）
//
// 備考:
//   なぜゲーム側にあるか: テロップの見た目（ScreenHeadingUi の部品）はゲーム側の知識だから。
//   エンジンはシグナルの名前と引数を渡すだけで、意味を知らない。
//
//   シグナルはロジックの段（CinematicTrackSystem）から届くが、UI を作るのは描画の段にそろえたいので、
//   受けたら溜めておき、自分の Update でまとめて処理する。
//   表示時間は Time::PresentationDt で数えるので、ポーズ中はテロップも止まる。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Cinematic/CinematicEvents.h"
#include "Engine/UI/ECS/Components/UITextComponent.h"
#include "Engine/UI/ECS/Components/UIImageComponent.h"

#include <string>
#include <vector>

class CinematicSignalSystem : public CCL::ECS::IfSystem<CinematicSignalSystem,
                                                        CCL::ECS::Write<UITextComponent>,
                                                        CCL::ECS::Write<UIImageComponent>>
{
public:
    CinematicSignalSystem()
        : IfSystem("CinematicSignalSystem")
    {
    }

    // シグナルと終了の知らせの購読を登録する
    void Initialize() override;

    void Update(float dt) override;

    // テロップの出現演出（UIRevealSystem）が書いた後に、消える側の不透明度を掛けたいので後ろに置く
    std::vector<const char *> GetRunAfter() const override { return {"UIRevealSystem"}; }

private:
    // テロップを作る1つの部品と、消し始める前の不透明度（フェードの基準）
    struct TelopPart
    {
        CCL::ECS::EntityID id        = CCL::ECS::InvalidEntityID;
        float              baseAlpha = 1.0f;
    };

    // 画面に出ている1枚のテロップ
    struct Telop
    {
        std::vector<CCL::ECS::EntityID> entities;             // 作った全部（消すとき用。入れ物を含む）
        std::vector<TelopPart>          parts;                // 色を持つ部品（フェード用）
        float                           remain       = 0.0f;  // 消え始めるまでの残り（秒）
        float                           fade         = 0.0f;  // 消え始めてからの秒数
        bool                            fadeCaptured = false; // parts の baseAlpha を控えたか
    };

    // シグナル1つを処理する
    void HandleSignal(const CinematicSignalEvent &ev);

    /**
     * @brief テロップを出す。
     * @param title    大きな文字（名前）。
     * @param subtitle 下の1行。空なら出さない。
     * @param seconds  消え始めるまでの秒数。
     */
    void SpawnTelop(const std::string &title, const std::string &subtitle, float seconds);

    // 出ているテロップの時間を進め、フェードさせ、消えきったものを破棄する
    void UpdateTelops(float dt);

    // 部品の不透明度を、控えた値 × k にする（文字・画像のどちらでも）
    void ApplyPartAlpha(const TelopPart &part, float k) const;

    // 部品の今の不透明度を返す（フェードの基準を控えるため）
    float ReadPartAlpha(CCL::ECS::EntityID id) const;

    std::vector<CinematicSignalEvent> m_pending;              // 受けたまま、まだ処理していないシグナル
    bool                              m_dismissTelops = false; // スキップで終わった。出ているテロップを急いで消す
    std::vector<Telop>                m_telops;               // 画面に出ているテロップ
};
