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
    /// （近接枝の上限と接近枝の下限が同じ値＝そこが間合いの境目）。
    enum RangeKind : uint8_t
    {
        kInside  = 1 << 0, // C_DistanceLessThan    … この内側で成立
        kOutside = 1 << 1, // C_DistanceGreaterThan … この外側で成立
    };

    struct RangeRing
    {
        float   radius = 0.0f;
        uint8_t kinds  = 0;   // RangeKind のビット和
    };

    /// 木を走査して距離条件の param を集める。半径ごとに1本へまとめる。
    void CollectRings(const BTAsset &asset);

    /// 間合いの線を1本描く。footCenter は「足元の高さの中心」。
    ///   地面に貼った円は、カメラが低いときも段差の下に相手が居るときも潰れて読めない。
    ///   縦に立ち上げて壁にすると、どの角度からでも「ここが境目」と分かる。
    void DrawRangeRing(ShapeRenderer *renderer, const DirectX::XMFLOAT3 &footCenter,
                       float radius, const DirectX::XMFLOAT4 &color) const;

    bool m_enabled  = true;
    bool m_drawLine = true;   // ボス→プレイヤーの照準線
    bool m_drawNow  = true;   // 今の間合いを示すリング

    /// 間合いの壁の高さ [m]。0 にすると地面に貼った円へ戻る。
    ///   既定はボスの背丈より少し高いくらい。低いと段差に埋もれ、高すぎると
    ///   縦線が画面を横切って戦闘が見えなくなる。
    float m_ringHeight = 6.0f;

    /// ボス中心のリング。毎フレーム作り直す（67ノード程度なので走査は無視できる）。
    ///   キャッシュせず作り直すのは、木が差し替わる経路が複数あるため。第2形態への
    ///   移行でも、インスペクタの「AI再読込」でも、黙って追従してほしい。
    std::vector<RangeRing> m_rings;

    /// 原点中心のリング（C_IsAtStageEdge）。中心が違うのでボス中心とは分けて持つ。
    std::vector<float> m_edgeRings;
};
