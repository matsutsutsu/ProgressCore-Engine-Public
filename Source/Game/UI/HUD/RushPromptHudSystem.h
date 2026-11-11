#pragma once
// ===========================================================================
// 概要: ラッシュの待機中に、攻撃ボタンのアイコンと残り時間のリングを出す System
//
// 責務:
//   - 待機に入ったら、ラッシュの相手（避けた攻撃の持ち主）のそばに合図を出す
//   - 待機の残り時間に合わせてリングを減らす
//   - 押せたら広がりながら消し、押さずに終わったら薄れて消す
//
// 備考: UI のエンティティは作らず、UIDrawList へ直接積む
//       調整値とボタンの画像は、PlayerHud プレハブの子 RushPrompt から読む
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/UI/ECS/UIDrawList.h"
#include "Engine/UI/ECS/Components/UIImageComponent.h"
#include "Game/Logics/Character/Player/Rush/PlayerRushComponent.h"
#include "Game/UI/HUD/RushPromptStyleComponent.h"
#include "Game/UI/Input/InputIconComponent.h"

#include <cstdint>
#include <DirectXMath.h>
#include <string>

class RushPromptHudSystem : public CCL::ECS::IfSystem<
    RushPromptHudSystem,
    CCL::ECS::Read<PlayerRushComponent>,
    CCL::ECS::Read<TransformComponent>,
    CCL::ECS::Read<UIImageComponent>,
    CCL::ECS::Write<InputIconComponent>>
{
public:
    RushPromptHudSystem() : IfSystem("RushPromptHudSystem") {}

    // 描画リストは同じ帯の System と共有し、スケジューラはその共有を見ない
    // 積む System の鎖の最後に並べ、同時に積まないようにする
    std::vector<const char *> GetRunAfter() const override { return {"CinematicFrameSystem"}; }

    void Update(float dt) override;

private:
    // 合図の表示の状態
    enum class PromptState : std::uint8_t
    {
        Hidden,  // 出ていない
        Showing, // 待機中。残り時間を減らしながら出す
        Burst,   // 押せた。広がりながら消す
        FadeOut, // 押さずに終わった。薄れて消す
    };

    // --- 更新 ---

    // プレイヤーのエンティティを返す。戦闘の情報がないシーンでは InvalidEntityID
    CCL::ECS::EntityID FindPlayer() const;

    // 待機の出入りを見て、表示の状態を切り替える
    void SwitchState(const PlayerRushComponent *rush);

    // 濃さと残り時間を進める。消えきったら false
    bool AdvanceState(const PlayerRushComponent *rush, const RushPromptStyleComponent &style, float dt);

    // --- 位置 ---

    // 合図の中心を決める。ラッシュの相手がいなければ、画面の下寄り中央
    void UpdateCenter(const PlayerRushComponent *rush, const RushPromptStyleComponent &style);

    // 相手の体の中心（足元と頭の中点）を画面へ投影する（px、左上が原点）
    // LockOnMarkerHudSystem と同じ求め方にして、照準と位置をそろえる
    // カメラの後ろ・画面の外・カメラがないときは false を返し、center は書かない
    bool ProjectTarget(CCL::ECS::EntityID target, DirectX::XMFLOAT2 &center) const;

    // --- 描画 ---

    // 残り時間のリングを積む。scale は半径に掛ける倍率、alpha は全体の不透明度
    void PushRing(UIDrawList &list, const RushPromptStyleComponent &style, float scale, float alpha) const;

    // リングを作る短い線を1本積む。線は真上から時計回りに並ぶ
    // fill は線の長さの割合（0〜1）で、残り時間の境目の線だけ 1 未満になる
    void PushRingSegment(UIDrawList &list, float radius, int index, float fill, float thickness,
                         const DirectX::XMFLOAT4 &color, int sortOrder) const;

    // 攻撃ボタンのアイコンを、リングの中心に積む
    void PushIcon(UIDrawList &list, const RushPromptStyleComponent &style, float scale, float alpha) const;

    // --- 調整値と画像 ---

    // 調整値を返す。PlayerHud の子 RushPrompt がなければ既定値
    const RushPromptStyleComponent &GetStyle();

    // 攻撃ボタンの画像のパス。まだ決まっていなければ空
    const std::string &GetIconPath() const;

    // --- 状態 ---

    CCL::ECS::EntityID m_styleID   = CCL::ECS::InvalidEntityID; // 子 RushPrompt（見つけたら持っておく）
    PromptState        m_state     = PromptState::Hidden;       // 今の表示の状態
    float              m_age       = 0.0f;                      // 出てからの時間（秒）
    float              m_stateTime = 0.0f;                      // 今の状態に入ってからの時間（秒）
    float              m_presence  = 0.0f;                      // 表示の濃さ（0〜1）
    float              m_ratio     = 1.0f;                      // リングの残りの割合（0〜1）
    DirectX::XMFLOAT2  m_center{};                              // 合図の中心（px）

    static constexpr int kRingSegments = 48; // リングを作る線の数
    static constexpr int kSortOrder    = 95; // 描画順。ロックオンの照準より手前で、HUD の範囲に収める
};
