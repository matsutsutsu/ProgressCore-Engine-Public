#pragma once
// ===========================================================================
// 概要: タイムラインのシグナル（TimelineSignalEvent）を受けて、ゲーム側の演出を起動する System。
//
// 責務:
//   - "Telop": 画面左下にテロップ（大きな名前と、その下の1行）を出し、時間が来たら消す
//   - "BGM"  : BGM の AudioEmitter の曲を差し替える（空なら止める）
//   - スキップで終わったら、出ているテロップを急いで消す
//
// シグナルの引数:
//   Telop: param = 名前（大きな文字）、param2 = 下の1行（空なら出さない）、value = 表示する秒数（0 なら 3 秒）
//   BGM  : param = 曲のイベント名（例: "bgm:/game"）。空なら止める
//
// 備考:
//   なぜゲーム側にあるか: テロップの見た目（ScreenHeadingUi の部品）も、どの音源が BGM かも、
//   ゲーム側の知識だから。タイムラインはシグナルの名前と引数を渡すだけで、意味を知らない。
//
//   シグナルはロジックステージ（TimelineSystem）で発行されるが、UI を作るのは描画ステージに
//   そろえたいので、受けたら溜めておき、自分の Update でまとめて処理する。
//   表示時間は Time::PresentationDt で数えるので、ポーズ中はテロップも止まる。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/ECS/Common/CCL_TypeInfo.h"
#include "Engine/Audio/Components/AudioEmitterComponent.h"
#include "Engine/UI/ECS/Components/UITextComponent.h"
#include "Engine/UI/ECS/Components/UIImageComponent.h"
#include "Engine/Timeline/TimelineEvents.h"

#include <unordered_set>
#include <vector>

class CinematicSignalSystem : public CCL::ECS::IfSystem<
                                  CinematicSignalSystem,
                                  CCL::ECS::Write<AudioEmitterComponent>>
{
public:
    CinematicSignalSystem()
        : IfSystem("CinematicSignalSystem")
    {
    }
    virtual ~CinematicSignalSystem() = default;

    void Initialize() override;
    void Update(float dt) override;

    // テロップの出現演出（UIRevealSystem）が書いた後に、消える側の不透明度を掛けたいので後ろに置く
    std::vector<const char *> GetRunAfter() const override { return {"UIRevealSystem"}; }

    // テロップの文字・画像の色を GetComponent で書く。ForEach のフィルタには載らないのでここで申告する
    std::vector<CCL::ECS::TypeID> GetWriteTypes() const override
    {
        auto types = IfSystem::GetWriteTypes();
        types.push_back(CCL::ECS::TypeInfo<UITextComponent>::ID());
        types.push_back(CCL::ECS::TypeInfo<UIImageComponent>::ID());
        return types;
    }

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
        std::vector<CCL::ECS::EntityID> entities; // 作った全部（消すとき用。入れ物を含む）
        std::vector<TelopPart>          parts;    // 色を持つ部品（フェード用）
        float                           remain = 0.0f; // 消え始めるまでの残り（秒）
        float                           fade   = 0.0f; // 消え始めてからの秒数
        bool                            fadeCaptured = false; // parts の baseAlpha を控えたか
    };

    // シグナル1つを処理する
    void HandleSignal(const TimelineSignalEvent &ev);

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

    /**
     * @brief BGM の AudioEmitter の曲を差し替える。
     * @param eventName 曲のイベント名。空なら止める。
     * @note 差し替えは名前を書き換えるだけ。止めて鳴らし直すのは AudioSystem（ハッシュの食い違いで検知する）。
     */
    void SwitchBgm(const std::string &eventName);

    std::vector<TimelineSignalEvent> m_pending;      // 受けたまま、まだ処理していないシグナル
    bool                             m_dismissTelops = false; // スキップで終わった。出ているテロップを急いで消す
    std::vector<Telop>               m_telops;       // 画面に出ているテロップ

    // BGM として扱う音源。止めると曲名が空になって見分けがつかなくなるので、一度見つけたら覚えておく
    std::unordered_set<CCL::ECS::EntityID> m_bgmEmitters;
};
