#pragma once
// ===========================================================================
// TrailSystem.h
// -------------
// 役割:
//   TrailComponent を持つエンティティの刃2端点を記録し、過去区間から
//   加算リボン(三角形リスト)を生成して TrailRenderer に積む。
//
// 責務:
//   - renderWorldMatrix でローカル端点をワールドへ変換（描画補間に追従）。
//   - 新区間の追記（移動量ゲート付き）。刻む頻度は TrailRecordSystem が発行する
//     sampleRequested に従う＝60Hz固定。区間の老化と破棄も同システムの担当。
//   - 尾ほど薄くフェードする頂点カラーを付けてリボンを構築・提出。
//   - distortion>0 のときは同じ形状を歪みレイヤにも積む（生成は1回だけ）。
//   - useZigZag のときは断面の中心を折れ線状にずらす（稲妻表現）。
//
// 位置付け:
//   R08_Main（メイン描画ステージ）で走り、同フレームの TrailPass が描画する。
//   ロジックには一切干渉しない純粋な見た目システム（Read Transform / Write Trail）。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"

#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Graphics/Components/Trail/TrailComponent.h"
#include "Engine/Graphics/Renderer/TrailRenderer.h"

#include <vector>

// 武器トレイルの区間記録とリボン生成を担うシステム。
class TrailSystem
    : public CCL::ECS::IfSystem<TrailSystem,
          CCL::ECS::Read<TransformComponent>,
          CCL::ECS::Write<TrailComponent>>
{
public:
    TrailSystem() : IfSystem("TrailSystem") {}

    void Update(float dt) override;

private:
    // 補間後の1断面。中心と「中心→先端」の半ベクトルで表す。
    //
    // 形状(板/十字/筒)はこの断面列から作られる。断面の作り方を1箇所に
    // まとめることで、形状を増やしても補間側を触らずに済む。
    struct Ring
    {
        DirectX::XMFLOAT3 center = {};
        DirectX::XMFLOAT3 half   = {};   // 中心→先端（幅カーブ適用済み）
        float             age    = 0.0f; // フェード用の経過秒
        float             taper  = 1.0f; // 尾ほど細くする係数（thickness 指定時に使う）

        // この断面の出どころ（元区間の通し番号と、区間内の位置0〜1）。
        //
        // ジグザグが「どの折れ点の上にいるか」を、フレームごとに変わる
        // 配列 index ではなく区間そのものから決めるために持ち回る。
        uint32_t segStamp = 0;
        float    segT     = 0.0f;
    };

    // 同じ位置に置く「加算レイヤ用」と「歪みレイヤ用」の頂点ペア。
    //
    // 歪みは加算と完全に同じ形状を使う。形状生成を2回走らせないよう、
    // 1頂点作るたびに両レイヤの色をここで確定させる。
    struct VertexPair
    {
        TrailRenderer::Vertex additive;
        TrailRenderer::Vertex distort;
    };

    // 三角形の積み先。distort が nullptr なら歪みレイヤを作らない。
    struct EmitSink
    {
        std::vector<TrailRenderer::Vertex> *additive = nullptr;
        std::vector<TrailRenderer::Vertex> *distort  = nullptr;

        void PushTriangle(const VertexPair &a, const VertexPair &b, const VertexPair &c) const;
    };

    // 区間リングを CatmullRom で補間し、連続した断面列に展開する。
    void BuildRings(const TrailComponent &trail, std::vector<Ring> &outRings) const;

    // 断面列から、shape に応じた三角形リストを積む。
    void EmitRibbon(const TrailComponent &trail, const std::vector<Ring> &rings,
                    const EmitSink &sink) const;
    void EmitCross(const TrailComponent &trail, const std::vector<Ring> &rings,
                   const EmitSink &sink) const;
    void EmitTube(const TrailComponent &trail, const std::vector<Ring> &rings,
                  const DirectX::XMFLOAT3 &cameraEye, const EmitSink &sink) const;

    // 断面 i の進行方向（末尾は直前の向きを流用）。
    DirectX::XMVECTOR RingTangent(const std::vector<Ring> &rings, size_t i) const;

    // 進行方向に垂直な断面内の正規直交基底を作る。
    //
    // half が進行方向と平行だと基底が潰れるため、その場合は適当な直交軸へ逃がす。
    void RingBasis(DirectX::FXMVECTOR tangent, const DirectX::XMFLOAT3 &half,
                   DirectX::XMVECTOR &outU, DirectX::XMVECTOR &outV) const;

    // Cross / Tube が使う半径。thickness=0 なら半ベクトル長を採用。
    float RingRadius(const TrailComponent &trail, const Ring &ring) const;

    /**
     * @brief 尾ほど薄くフェードする加算色と、対になる歪みパラメータを作る。
     * @param vCoord テクスチャ V 座標（幅方向。Ribbon/Cross は根本0/先端1、Tube は円周位置）。
     *              U（長さ方向）は age から算出し、tiling/scroll を掛けて uv を組む。
     */
    VertexPair MakeVertex(const TrailComponent &trail,
                          const DirectX::XMFLOAT3 &pos,
                          float age, float intensity, float vCoord) const;

    // 折れ点1つ分のずらしベクトル。断面内の基底 u,v 上で決める。
    DirectX::XMVECTOR ZigZagOffset(const TrailComponent &trail, uint32_t node,
                                   DirectX::FXMVECTOR u, DirectX::FXMVECTOR v) const;

    // 滑らかな断面列を、稲妻状の折れ線へずらす（中心のみ動かす）。
    void ApplyZigZag(const TrailComponent &trail, std::vector<Ring> &rings) const;
};
