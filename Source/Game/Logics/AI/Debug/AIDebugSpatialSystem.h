#pragma once
// ===========================================================================
// 概要: ボスの BT の間合いを、ステージ上に3Dで描くデバッグ用の System（エディタのみ）
//
// 責務:
//   - 読み込み済みの BT から距離条件の param を集め、ボスの足元にリングとして描く
//   - しきい値の間を地面の帯で塗り、今の間合いとボス→プレイヤーの線を描く
//   - ステージ端の条件（C_IsAtStageEdge）を、ステージの中心からのリングとして描く
//
// 備考: なぜ今この枝を選んだかを、戦いながら読めるようにするためのもの
//       半径は手で持たず毎フレーム BT から拾い直す。2か所で持つと、片方だけ直して表示が判定と食い違う
//       頭上のテキスト（AIDebugOverlaySystem）とは描き方もタイミングも違うので、別の System にしている
// ===========================================================================
#include <cstdint>
#include <vector>

#include <DirectXMath.h>

#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Game/Logics/AI/Debug/AIDebugRanges.h"
#include "Game/Logics/Character/Boss/Components/BossActionComponent.h"

class ShapeRenderer;

class AIDebugSpatialSystem : public CCL::ECS::IfSystem<
    AIDebugSpatialSystem,
    CCL::ECS::Read<TransformComponent>,
    CCL::ECS::Read<BossActionComponent>>
{
public:
    AIDebugSpatialSystem() : IfSystem("AIDebugSpatialSystem") {}

    void Update(float dt) override;
    void OnGui() override;

    // BehaviorTreeComponent はテンプレート引数に足さない。絞り込みになり、BT を持たない検証用のボスでリングが消える
    std::vector<CCL::ECS::TypeID> GetReadTypes() const override;

private:
    using Base = CCL::ECS::IfSystem<
        AIDebugSpatialSystem,
        CCL::ECS::Read<TransformComponent>,
        CCL::ECS::Read<BossActionComponent>>;

    // リングの集め方と色は AIDebugOverlaySystem（距離の数字の色）と共有する
    using RangeRing = AIDebugRanges::RangeRing;

    // 間合いの線を1本描く。m_ringHeight が 0 に近ければ地面の円、そうでなければ縦の壁にする
    // 地面の円はカメラが低いときや段差の下で潰れて読めないが、壁ならどの角度からでも境目が分かる
    void DrawRangeRing(ShapeRenderer *renderer, const DirectX::XMFLOAT3 &footCenter,
                       float radius, const DirectX::XMFLOAT4 &color) const;

    // DrawRangeRing の壁を半透明の面で塗る。線だけだと背景に紛れて読めない
    // 上へ行くほど薄くする。均一に塗ると本物の壁のようになり、戦闘が見えにくい
    void DrawRingWall(ShapeRenderer *renderer, const DirectX::XMFLOAT3 &footCenter,
                      float radius, const DirectX::XMFLOAT4 &color) const;

    // しきい値の間を地面の帯として塗る
    // 線も壁も面積が小さく、背景が明るいと見えにくい。面で塗ると、今どの帯にいるかが形で分かる
    void DrawGroundBands(ShapeRenderer *renderer, const DirectX::XMFLOAT3 &footCenter) const;

    // 地面にドーナツ形を1枚描く。rInner が 0 なら円板になる
    void DrawAnnulus(ShapeRenderer *renderer, const DirectX::XMFLOAT3 &center,
                     float rInner, float rOuter, const DirectX::XMFLOAT4 &color) const;

    bool m_enabled  = false;
    bool m_drawLine = false; // ボス→プレイヤーの照準線
    bool m_drawNow  = false; // 今の間合いを示すリング

    // しきい値の間を地面の帯で塗る。いちばん読みやすい表示なので既定で ON
    bool m_groundFill = true;

    // 一番内側の帯の濃さ。外側ほど薄くする。上げすぎると地面が見えず、立ち位置が分からない
    float m_fillAlpha = 0.45f;

    // リング全体を足元から持ち上げる高さ（m）。起伏や装飾に埋もれて塗りが欠けるときに上げる
    float m_heightOffset = 0.8f;

    // 間合いの壁の高さ（m）。0 なら地面の帯と輪郭だけ。真上から見るときなど、要る場面だけ上げる
    float m_ringHeight = 0.0f;

    // 壁の塗りの濃さ（足元での値）。壁を出したときだけ意味を持つ
    float m_wallAlpha = 0.5f;

    // ボス中心のリング。木が差し替わる経路（第2形態・AI 再読込）が複数あるので、毎フレーム作り直す
    std::vector<RangeRing> m_rings;

    // ステージ端のリング（C_IsAtStageEdge）。持つのは端からの幅で、中心が違うのでボスのリングとは分ける
    std::vector<float> m_edgeRings;

    // 最後に描いたときのステージの半径。GUI の一覧で「中心から何 m か」を出すのに使う
    float m_arenaRadius = 0.0f;
};
