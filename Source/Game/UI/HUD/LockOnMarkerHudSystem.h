#pragma once
// ===========================================================================
// 概要: ロックオン中の敵の体の中心に、小さな照準（2本の弧の輪と中心の◆）を描く System。
//
// 責務:
//   - プレイヤーの lockOnTargetID を読み、対象の体の中心を画面へ投影する
//   - 小さな輪（左右に切れ目のある2本の弧。ゆっくり回る）と中心の◆を描く
//   - ロックした瞬間に、開いた輪を白く光らせながら縮め、深紅に落ち着かせる
//   - 解除したら、少し広がりながら消す。カメラの後ろ・画面の外にいる間は隠す
//
// 備考:
//   ボスの姿を隠さないよう、画面上の大きさは距離に関係なく一定で小さくする。
//
//   弧は細い四角形を円に沿って並べて描く。UI のエンティティは作らず、
//   収集の帯（R10b_UICollect）で UIDrawList へ直接積む（他の収集 System と同じ帯）。
//   カメラの行列はそれより前の R01b_CameraBrain で決まるので、同じフレームのカメラに合う。
//
//   見た目と動きの調整値は LockOnMarkerStyleComponent。PlayerHud プレハブの子 LockOnMarker に付いていて、
//   インスペクタで調整し、インスペクタのプレハブ保存で残す。見つからないシーンでは既定値で描く。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/UI/ECS/UIDrawList.h"
#include "Game/Logics/Character/Player/Core/PlayerComponent.h"
#include "Game/UI/HUD/LockOnMarkerStyleComponent.h"

#include <DirectXMath.h>

class LockOnMarkerHudSystem : public CCL::ECS::IfSystem<LockOnMarkerHudSystem,
                                                        CCL::ECS::Read<PlayerComponent>,
                                                        CCL::ECS::Read<TransformComponent>>
{
public:
    LockOnMarkerHudSystem() : IfSystem("LockOnMarkerHudSystem") {}

    // 描画リストは System をまたいで共有する1本の配列で、スケジューラはリソースの共有を見ない。
    // 同じ帯の収集 System（画像 → 文字 → ゲージの順につないである）の後ろに並べ、同時に積まないようにする
    std::vector<const char *> GetRunAfter() const override
    {
        return {"UIProgressBarCollectSystem"};
    }

    void Update(float dt) override; // ロックオンの状態に合わせて、照準を描画リストへ積む

private:
    /**
     * @brief ロックオン対象の体の中心（足元と頭の中点）を画面へ投影する。
     * @param target ロックオン対象。
     * @param center 画面座標（px、画面の左上が原点）を受け取る。false のときは書かない。
     * @return 描ける位置なら true。カメラの後ろ・画面の外・カメラが無いときは false。
     */
    bool ProjectTarget(CCL::ECS::EntityID target, DirectX::XMFLOAT2 &center) const;

    /**
     * @brief 線を1本、描画リストへ積む。下に一回り太い暗い縁取りも積む。
     * @param list  積む先。
     * @param x     線の中心の X（px、画面の左上が原点）。
     * @param y     線の中心の Y（px）。
     * @param len   線の長さ（px）。
     * @param thick 線の太さ（px）。
     * @param angle 中心まわりの回転（度）。
     * @param color 線の色。縁取りの濃さもこの α に合わせる。
     */
    void PushStroke(UIDrawList &list, float x, float y, float len, float thick, float angle,
                    const DirectX::XMFLOAT4 &color) const;

    // 調整値を返す。PlayerHud の子 LockOnMarker が無ければ既定値
    const LockOnMarkerStyleComponent &GetStyle();

    CCL::ECS::EntityID m_styleID  = CCL::ECS::InvalidEntityID; // 調整値を持つエンティティ（見つけたら持っておく）
    CCL::ECS::EntityID m_target   = CCL::ECS::InvalidEntityID; // 最後に狙った対象
    float              m_lockAge  = 0.0f;                      // 今の対象をロックしてからの秒数
    float              m_presence = 0.0f;                      // 表示の濃さ（0〜1）
    float              m_spin     = 0.0f;                      // 弧の回転角（ラジアン）
    DirectX::XMFLOAT2  m_center{};                             // 最後に描いた中心（px）。解除後に消えていく間もここに描く

    static constexpr int kSortOrder = 90; // 描画順。他の HUD（HP・切り払い）より奥
};
