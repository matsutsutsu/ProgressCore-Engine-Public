#pragma once
// ===========================================================================
// 概要: 刃の断面を刻み、トレイルの三角形を作って TrailRenderer に積む System
//
// 責務:
//   - 印が立ち、先端が minDistance 以上動いたら断面を刻む
//   - 断面を補間して、shape に応じた形（板・筒）の三角形を作る
//   - distortion > 0 なら、同じ形を歪みの層にも積む
//
// 備考: R08_Main で走る。刻む頻度と断面の老化は、
//       ロジックの TrailRecordSystem が受け持つ
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"

#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Graphics/Components/Trail/TrailComponent.h"
#include "Engine/Graphics/Renderer/TrailRenderer.h"

#include <vector>

class TrailSystem : public CCL::ECS::IfSystem<
    TrailSystem,
    CCL::ECS::Read<TransformComponent>,
    CCL::ECS::Write<TrailComponent>>
{
public:
    TrailSystem() : IfSystem("TrailSystem") {}

    void Update(float dt) override;

private:
    // 補間した後の1断面。形（板・筒）はこの列から作るので、
    // 形を増やしても補間の側を触らずに済む
    struct Ring
    {
        DirectX::XMFLOAT3 center = {};   // 断面の中心（ワールド, m）
        DirectX::XMFLOAT3 half   = {};   // 中心から先端へのベクトル（m）
        float             age    = 0.0f; // 記録してからの時間（秒）
        float             taper  = 1.0f; // 尾ほど細くする係数
    };

    // 三角形の頂点の形だけ。色は PaintPoints で層ごとに塗る
    struct TrailPoint
    {
        DirectX::XMFLOAT3 pos = {};   // ワールド座標（m）
        float             age = 0.0f; // 記録してからの時間（秒）
        float             v   = 0.0f; // テクスチャの V（板は幅、筒は円周の位置）
    };

    // 筒の1断面の軸と半径
    struct TubeFrame
    {
        DirectX::XMVECTOR u;      // 断面の中の1本目の軸
        DirectX::XMVECTOR v;      // 断面の中の2本目の軸
        float             radius; // 筒の半径（m）
    };

    // --- 断面の記録 ---

    // リングの末尾に断面を1つ刻む。満杯なら一番古い断面を上書きする
    void RecordSegment(TrailComponent &trail, const DirectX::XMFLOAT3 &base, const DirectX::XMFLOAT3 &tip) const;

    // --- 断面列の生成 ---

    // リングの断面を補間して、断面の列に広げる
    // useInterpolation が false なら、記録した断面をそのまま並べる
    void BuildRings(const TrailComponent &trail, std::vector<Ring> &outRings) const;

    // 根本と先端から、幅カーブを掛けた断面を1つ作る
    // invLife は 1 / lifeTime（lifeTime が 0 なら 0）
    Ring MakeRing(const TrailComponent &trail, const DirectX::XMFLOAT3 &base, const DirectX::XMFLOAT3 &tip,
                  float age, float invLife) const;

    // Catmull-Rom 曲線の上の点を返す。t が 0 で p1、1 で p2
    DirectX::XMFLOAT3 CatmullRom(const DirectX::XMFLOAT3 &p0, const DirectX::XMFLOAT3 &p1,
                                 const DirectX::XMFLOAT3 &p2, const DirectX::XMFLOAT3 &p3, float t) const;

    // --- 形ごとの三角形の生成 ---

    // 断面の列から、板または筒の三角形を outPoints に3点ずつ積む
    void EmitRibbon(const std::vector<Ring> &rings, std::vector<TrailPoint> &outPoints) const;
    void EmitTube(const TrailComponent &trail, const std::vector<Ring> &rings,
                  std::vector<TrailPoint> &outPoints);

    // 断面 i の進行方向。末尾は1つ前の向きを使う
    DirectX::XMVECTOR RingTangent(const std::vector<Ring> &rings, size_t i) const;

    // 進行方向に垂直な、断面の中の2本の軸を作る
    void RingBasis(DirectX::FXMVECTOR tangent, const DirectX::XMFLOAT3 &half,
                   DirectX::XMVECTOR &outU, DirectX::XMVECTOR &outV) const;

    // 筒の半径（m）
    float RingRadius(const TrailComponent &trail, const Ring &ring) const;

    // --- 色を塗る ---

    // 点に色を塗って、加算の層の頂点にする
    // distortion > 0 なら、歪みの層の頂点も作る
    void PaintPoints(const TrailComponent &trail, const std::vector<TrailPoint> &points);

    // --- 作業バッファ ---

    // トレイルとフレームをまたいで使い回し、毎フレームの確保をなくす
    // ForEach は1スレッドで回るので、メンバに持っても競合しない
    std::vector<TrailRenderer::Vertex> m_vertices;        // 加算の層の頂点
    std::vector<TrailRenderer::Vertex> m_distortVertices; // 歪みの層の頂点
    std::vector<Ring>                  m_rings;           // 補間した後の断面の列
    std::vector<TrailPoint>            m_points;          // 形だけの三角形の頂点
    std::vector<TubeFrame>             m_tubeFrames;      // 筒の断面ごとの軸と半径
};
