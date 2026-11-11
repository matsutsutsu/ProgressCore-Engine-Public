#pragma once
// ===========================================================================
// 概要: 刃の2端点を記録し、過去の断面から加算合成の三角形を作って TrailRenderer に積む System（R08_Main）
//
// 責務:
//   - ローカルの2端点を renderWorldMatrix でワールドへ移す（画面に見えている位置に合わせる）
//   - sampleRequested が立ち、先端が minDistance 以上動いたときだけ新しい断面を刻む
//   - 断面を補間して、shape に応じた形（板・十字・筒）の三角形を作る
//   - distortion > 0 なら、同じ形を歪みのレイヤにも積む
//
// 備考: 刻む頻度と断面の老化は TrailRecordSystem（ロジック、60Hz 固定）が受け持つ
//       リングは固定長で、満杯なら一番古い断面を上書きする。間引くと補間の制御点が入れ替わり、尾の形が跳ねる
//       補間（BuildRings）と形の生成（Emit*）を分けて、板が線につぶれる角度でも十字や筒を作れるようにしている
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
    // 補間した後の1断面。中心と「中心→先端」の半分のベクトルで表す
    // 形（板・十字・筒）はこの断面列から作る。断面の作り方を1か所にまとめておけば、
    // 形を増やしても補間の側を触らずに済む
    struct Ring
    {
        DirectX::XMFLOAT3 center = {};   // 断面の中心（ワールド, m）
        DirectX::XMFLOAT3 half   = {};   // 中心→先端のベクトル（幅カーブを掛けた後, m）
        float             age    = 0.0f; // フェードに使う経過時間（秒）
        float             taper  = 1.0f; // 尾ほど細くする係数（thickness を指定したときに使う）
    };

    // 同じ位置に置く、加算レイヤ用と歪みレイヤ用の頂点の組
    // 歪みは加算と全く同じ形を使う。形の生成を2回しないよう、1頂点作るたびに両方の値をここで決める
    struct VertexPair
    {
        TrailRenderer::Vertex additive;
        TrailRenderer::Vertex distort;
    };

    // 三角形の積み先。distort が nullptr なら歪みレイヤを作らない
    struct EmitSink
    {
        std::vector<TrailRenderer::Vertex> *additive = nullptr; // 加算レイヤの頂点（所有しない）
        std::vector<TrailRenderer::Vertex> *distort  = nullptr; // 歪みレイヤの頂点（所有しない）

        void PushTriangle(const VertexPair &a, const VertexPair &b, const VertexPair &c) const; // 加算レイヤへ、distort があれば歪みレイヤへも三角形を1つ積む
    };

    // Tube の断面ごとの正規直交フレーム
    struct TubeFrame
    {
        DirectX::XMVECTOR u;      // 断面内の基底の1本目（板の向き＝根本→先端が基準）
        DirectX::XMVECTOR v;      // 断面内の基底の2本目
        float             radius; // 筒の半径（m）
    };

    // ============================================================
    // 断面の記録
    // ============================================================

    // 新しい断面を1つリングの末尾に刻む。満杯なら一番古い断面を上書きする
    void RecordSegment(TrailComponent &trail, const DirectX::XMFLOAT3 &base, const DirectX::XMFLOAT3 &tip) const;

    // ============================================================
    // 断面列の生成
    // ============================================================

    // リングの区間を Catmull-Rom で補間し、連続した断面列に広げる（useInterpolation=false なら補間しない）
    void BuildRings(const TrailComponent &trail, std::vector<Ring> &outRings) const;

    // 根本と先端の2点から、幅カーブを掛けた断面を1つ作る。invLife は 1 / lifeTime（lifeTime が 0 なら 0）
    Ring MakeRing(const TrailComponent &trail, const DirectX::XMFLOAT3 &base, const DirectX::XMFLOAT3 &tip,
                  float age, float invLife) const;

    // Catmull-Rom 曲線上の点を返す。p1〜p2 が区間で、t が 0 で p1、1 で p2
    DirectX::XMFLOAT3 CatmullRom(const DirectX::XMFLOAT3 &p0, const DirectX::XMFLOAT3 &p1,
                                 const DirectX::XMFLOAT3 &p2, const DirectX::XMFLOAT3 &p3, float t) const;

    // ============================================================
    // 形状ごとの三角形生成
    // ============================================================

    // 断面列から、shape に応じた三角形リストを積む
    void EmitRibbon(const TrailComponent &trail, const std::vector<Ring> &rings,
                    const EmitSink &sink) const;
    void EmitCross(const TrailComponent &trail, const std::vector<Ring> &rings,
                   const EmitSink &sink) const;
    void EmitTube(const TrailComponent &trail, const std::vector<Ring> &rings,
                  const DirectX::XMFLOAT3 &cameraEye, const EmitSink &sink);

    // 断面 i の進行方向（末尾は1つ前の向きを使う）
    DirectX::XMVECTOR RingTangent(const std::vector<Ring> &rings, size_t i) const;

    // 進行方向に垂直な、断面内の正規直交基底を作る
    // half が進行方向と平行だと基底がつぶれるので、その場合はほかの直交軸を使う
    void RingBasis(DirectX::FXMVECTOR tangent, const DirectX::XMFLOAT3 &half,
                   DirectX::XMVECTOR &outU, DirectX::XMVECTOR &outV) const;

    // Cross / Tube の半径（m）。thickness=0 なら half の長さを使う
    float RingRadius(const TrailComponent &trail, const Ring &ring) const;

    // 尾ほど薄くなる加算色と、対になる歪みの頂点を作る。U は age から求め、tiling と scroll を掛ける
    // intensity は Tube の輪郭強調に使う明るさの倍率（ほかは 1）。vCoord は幅方向のテクスチャ座標
    VertexPair MakeVertex(const TrailComponent    &trail,
                          const DirectX::XMFLOAT3 &pos,
                          float age, float intensity, float vCoord) const;

    // ============================================================
    // 作業バッファ
    // ============================================================

    // トレイル間・フレーム間で使い回して、毎フレームのアロケーションをなくす
    // ForEach はシングルスレッド版なので、メンバに持っても競合しない
    std::vector<TrailRenderer::Vertex> m_vertices;        // 加算レイヤの頂点
    std::vector<TrailRenderer::Vertex> m_distortVertices; // 歪みレイヤの頂点
    std::vector<Ring>                  m_rings;           // 補間後の断面列
    std::vector<TubeFrame>             m_tubeFrames;      // Tube の断面ごとのフレーム
};
