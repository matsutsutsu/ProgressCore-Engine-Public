#pragma once
// ===========================================================================
// 概要: ボスの BT の間合いを、ステージ上に3Dで描くデバッグ用の System（エディタのみ）。
//
// 責務:
//   - 読み込み済みの BT から距離条件の param を集め、ボスの足元にリングとして描く
//   - しきい値の間を地面の帯で塗り、今の間合いとボス→プレイヤーの線を描く
//   - ステージ端の条件（C_IsAtStageEdge）を、ステージの中心からのリングとして描く
//
// 備考:
//   なぜ今この枝を選んだかを、戦いながら読めるようにするためのもの。
//   リングの半径は手で持たず、毎フレーム BT から拾い直す。エディタで間合いを変えれば
//   表示もそのまま変わる。数字を2か所で管理すると、片方だけ直して表示が判定と食い違う。
//   AIDebugOverlaySystem（頭上の ImGui テキスト）とは描画の仕組みもタイミングも違うので、
//   別の Render ステージの System にしている。
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

    // ShapeRenderer へ描画を積む（毎フレーム）。
    void Update(float dt) override;
    void OnGui() override;

    /**
     * @brief テンプレート引数以外に読む型を申告する。
     *
     * Update が BehaviorTreeComponent を GetComponent で読むので、申告だけを足す。
     * テンプレート引数には足さない。ForEach の絞り込みにもなるので、BT を持たない検証用のボスでリングが何も言わずに消える。
     */
    std::vector<CCL::ECS::TypeID> GetReadTypes() const override;

private:
    using Base = CCL::ECS::IfSystem<
        AIDebugSpatialSystem,
        CCL::ECS::Read<TransformComponent>,
        CCL::ECS::Read<BossActionComponent>>;

    // リングの集め方と色は AIDebugOverlaySystem（距離の数字の色）と共有する。
    using RangeRing = AIDebugRanges::RangeRing;

    /**
     * @brief 間合いの線を1本描く。
     *
     * 地面の円はカメラが低いときや段差の下に相手が居るときに潰れて読めないが、壁にするとどの角度からでも境目が分かる。
     *
     * @param renderer   描画先。所有しない。
     * @param footCenter 足元の高さの中心（ワールド）。
     * @param radius     間合いの半径（m）。
     * @param color      線の色。
     * @note m_ringHeight が 0 に近ければ地面の円、そうでなければ縦に立ち上げた壁として描く。
     */
    void DrawRangeRing(ShapeRenderer *renderer, const DirectX::XMFLOAT3 &footCenter,
                       float radius, const DirectX::XMFLOAT4 &color) const;

    /**
     * @brief DrawRangeRing の壁を半透明の面で塗る。線だけだと背景に紛れて読めないため。
     *
     * 上へ行くほど薄くする。均一に塗ると本当の壁のようになり、戦闘が見えにくくなる。
     *
     * @param renderer   描画先。所有しない。
     * @param footCenter 足元の高さの中心（ワールド）。
     * @param radius     間合いの半径（m）。
     * @param color      面の色。
     */
    void DrawRingWall(ShapeRenderer *renderer, const DirectX::XMFLOAT3 &footCenter,
                      float radius, const DirectX::XMFLOAT4 &color) const;

    /**
     * @brief しきい値としきい値の間を地面の帯として塗る。
     *
     * 線も壁も面積が小さいので、背景が明るいと見えにくい。
     * 面で塗れば今どの帯にいるかが形で分かり、線を数えずに読める。
     *
     * @param renderer   描画先。所有しない。
     * @param footCenter 足元の高さの中心（ワールド）。
     */
    void DrawGroundBands(ShapeRenderer *renderer, const DirectX::XMFLOAT3 &footCenter) const;

    // 地面にドーナツ形を1枚描く。rInner が 0 なら円板になる。
    void DrawAnnulus(ShapeRenderer *renderer, const DirectX::XMFLOAT3 &center,
                     float rInner, float rOuter, const DirectX::XMFLOAT4 &color) const;

    #if 0
    bool m_enabled  = true;
    bool m_drawLine = true; // ボス→プレイヤーの照準線
    bool m_drawNow  = true; // 今の間合いを示すリング
    #else
    bool m_enabled  = false;
    bool m_drawLine = false; // ボス→プレイヤーの照準線
    bool m_drawNow  = false; // 今の間合いを示すリング
    #endif

    // しきい値の間を地面の帯で塗る。いちばん読みやすい表示なので既定で ON。
    bool m_groundFill = true;

    // 一番内側の帯の濃さ。外側の帯ほど薄くする。
    //   上げすぎると地面のテクスチャが見えず、どこに立っているか分からなくなる。
    float m_fillAlpha = 0.45f;

    // リング全体を足元から持ち上げる高さ（m）。
    //   段差・起伏・地面の装飾に埋もれて塗りが欠けるときはここを上げる。塗りと輪郭を
    //   そろって平行移動するので、上げても半径の読み方は変わらない。
    float m_heightOffset = 0.8f;

    // 間合いの壁の高さ（m）。0 なら地面の帯と輪郭だけになる。
    //   地面の帯で読めるので既定は 0。真上から見るときなど、壁が要る場面だけ上げる。
    float m_ringHeight = 0.0f;

    // 壁の塗りの濃さ（足元での値）。壁を出したときだけ意味を持つ。
    float m_wallAlpha = 0.5f;

    // ボス中心のリング。毎フレーム作り直す（ノードは数十個なので、走査のコストは無視できる）。
    //   キャッシュせず作り直すのは、木が差し替わる経路が複数あるため。第2形態への
    //   移行でも、インスペクタの「AI再読込」でも、何もしなくても表示が変わるようにする。
    std::vector<RangeRing> m_rings;

    // ステージ端のリング（C_IsAtStageEdge）。持つのは端からの幅で、半径ではない。
    //   中心が違うのでボス中心のリングとは分けて持つ。
    std::vector<float> m_edgeRings;

    // 最後に描いたときのステージの半径。GUI の一覧で「中心から何 m か」を出すのに使う。
    float m_arenaRadius = 0.0f;
};
