// 敵AIの意思決定を戦場そのものへ3D描画するデバッグシステム。
// 「なぜ今この枝を選んだか」を戦いながら読めるよう、BTの距離しきい値を地面のリング
// として出し、あわせてボス→プレイヤーの照準線を引く。
//
// リングの半径は手で持たない。読み込み済みのBT（BTAsset）から距離条件を毎フレーム
// 拾い直すので、エディタで間合いを変えれば可視化もそのまま追従する。数字を2か所で
// 管理すると、必ず片方だけ直って「見えている線が嘘をつく」状態になる。
//
// AIDebugOverlaySystem（頭上の ImGui テキスト）とは描画機構もタイミングも違うので、
// 専用のレンダーシステムとして独立させてある。
#pragma once
#include <cstdint>
#include <vector>

#include <DirectXMath.h>

#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Game/Logics/Character/Boss/Components/BossActionComponent.h"

struct BTAsset;
class ShapeRenderer;

class AIDebugSpatialSystem : public CCL::ECS::IfSystem<
                                 AIDebugSpatialSystem,
                                 CCL::ECS::Read<TransformComponent>,
                                 CCL::ECS::Read<BossActionComponent>>
{
public:
    AIDebugSpatialSystem() : IfSystem("AIDebugSpatialSystem") {}

    /// ShapeRenderer への描画発行（毎フレーム）。
    void Update(float dt) override;
    void OnGui() override;

    /// Update が BehaviorTreeComponent を GetComponent で読む。
    ///   テンプレート引数には足さない（ForEach の絞り込みも兼ねるので、BTを持たない
    ///   検証用のボスでリングが黙って消える）。申告だけをここで足す。
    std::vector<CCL::ECS::TypeID> GetReadTypes() const override;

private:
    using Base = CCL::ECS::IfSystem<
        AIDebugSpatialSystem,
        CCL::ECS::Read<TransformComponent>,
        CCL::ECS::Read<BossActionComponent>>;

    /// どちら向きの条件がこの半径を使っているか。同じ半径を両方が使うこともある
    /// （近接枝の上限と接近枝の下限が同じ値＝そこが間合いの境目）
    enum RangeKind : uint8_t
    {
        kInside  = 1 << 0, // C_DistanceLessThan    … この内側で成立
        kOutside = 1 << 1, // C_DistanceGreaterThan … この外側で成立
    };

    struct RangeRing
    {
        float   radius = 0.0f;
        uint8_t kinds  = 0; // RangeKind のビット和
    };

    /// 木を走査して距離条件の param を集める。半径ごとに1本へまとめる。
    void CollectRings(const BTAsset &asset);

    /// 間合いの線を1本描く。footCenter は「足元の高さの中心」。
    ///   地面に貼った円は、カメラが低いときも段差の下に相手が居るときも潰れて読めない。
    ///   縦に立ち上げて壁にすると、どの角度からでも「ここが境目」と分かる。
    void DrawRangeRing(ShapeRenderer *renderer, const DirectX::XMFLOAT3 &footCenter,
                       float radius, const DirectX::XMFLOAT4 &color) const;

    /// 上の壁を半透明の面で塗る。線だけだと背景に溶けて読めないため。
    ///   上へ行くほど薄くする。均一に塗ると本当に壁になってしまい、肝心の戦闘が見えない。
    void DrawRingWall(ShapeRenderer *renderer, const DirectX::XMFLOAT3 &footCenter,
                      float radius, const DirectX::XMFLOAT4 &color) const;

    /// しきい値としきい値の間を地面の帯として塗る。
    ///   線も壁も視線に対して面積を持たないので、背景が明るいと結局は消える。
    ///   面で塗れば「今どの帯にいるか」が形として出るので、線を数えずに読める。
    void DrawGroundBands(ShapeRenderer *renderer, const DirectX::XMFLOAT3 &footCenter) const;

    /// 地面のドーナツを1枚。rInner が 0 なら円板になる。
    void DrawAnnulus(ShapeRenderer *renderer, const DirectX::XMFLOAT3 &center,
                     float rInner, float rOuter, const DirectX::XMFLOAT4 &color) const;

    bool m_enabled  = true;
    bool m_drawLine = true; // ボス→プレイヤーの照準線
    bool m_drawNow  = true; // 今の間合いを示すリング

    /// しきい値の間を地面の帯で塗る。読みやすさはこれが本命。
    bool m_groundFill = true;

    /// 一番内側の帯の濃さ。外側の帯ほど薄くする。
    ///   上げすぎると地面のテクスチャが消えて、どこに立っているか分からなくなる。
    float m_fillAlpha = 0.45f;

    /// リング全体を足元から持ち上げる高さ [m]。
    ///   段差・起伏・地面の装飾に埋もれて塗りが欠けるときはここを上げる。塗りと輪郭の
    ///   相対関係は保ったまま平行移動するので、上げても半径の読み方は変わらない。
    float m_heightOffset = 0.8f;

    /// 間合いの壁の高さ [m]。0 で地面の帯と輪郭だけになる。
    ///   地面の帯で読めるので既定は 0。真上から見るときなど、壁が要る場面だけ上げる。
    float m_ringHeight = 0.0f;

    /// 壁の塗りの濃さ（足元での値）。壁を出したときだけ意味を持つ。
    float m_wallAlpha = 0.5f;

    /// ボス中心のリング。毎フレーム作り直す（67ノード程度なので走査は無視できる）。
    ///   キャッシュせず作り直すのは、木が差し替わる経路が複数あるため。第2形態への
    ///   移行でも、インスペクタの「AI再読込」でも、黙って追従してほしい。
    std::vector<RangeRing> m_rings;

    /// 原点中心のリング（C_IsAtStageEdge）。中心が違うのでボス中心とは分けて持つ。
    std::vector<float> m_edgeRings;
};
