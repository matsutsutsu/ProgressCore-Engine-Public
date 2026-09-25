#pragma once
// ===========================================================================
// 概要: 刃の2端点を記録し、過去の断面から加算合成の三角形リストを作って
//       TrailRenderer に積む System（R08_Main）。
//
// 責務:
//   - ローカルの2端点を renderWorldMatrix でワールドへ変換する（描画の補間に合わせる）
//   - sampleRequested が立っていて、先端が minDistance 以上動いたときだけ新しい断面を刻む
//   - 断面を補間して断面列にし、shape に応じた形（板・十字・筒）の三角形を作る
//   - distortion > 0 なら同じ形を歪みレイヤにも積む（形は1回だけ作る）
//   - useZigZag なら断面の中心を折れ線状にずらす
//
// 備考: 描画は同じフレームの TrailPass が行う。ロジックには影響しない見た目だけの System。
//       刻む頻度と断面の老化・破棄は TrailRecordSystem（ロジック, 60Hz 固定）が受け持つ。
//       位置の基準が renderWorldMatrix なのは、ロジックの worldMatrix ではなく画面に見えている
//       位置に軌跡を合わせるため（ModelRenderSystem と同じ）。
//       速く振ったときだけ尾が出るよう、先端が minDistance 以上動いたときだけ刻む。
//       止まっているときは刻まず、残っている断面が lifeTime で消える。
//       リングは固定長で、満杯なら一番古い断面を上書きする（ヒープを使わない）。
//       断面の補間（BuildRings）と形の生成（Emit*）を分けているのは、板1枚だと面が視線と
//       平行になると線につぶれるので、同じ断面列から十字や筒も作れるようにするため。
//       ジグザグは形を作る前に断面列の中心だけをずらすので、どの形でも同じ稲妻の芯線になる。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"

#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Graphics/Components/Trail/TrailComponent.h"
#include "Engine/Graphics/Renderer/TrailRenderer.h"

#include <vector>

class TrailSystem
    : public CCL::ECS::IfSystem<TrailSystem,
                                CCL::ECS::Read<TransformComponent>,
                                CCL::ECS::Write<TrailComponent>>
{
public:
    TrailSystem() : IfSystem("TrailSystem") {}

    void Update(float dt) override;

private:
    // 補間した後の1断面。中心と「中心→先端」の半分のベクトルで表す。
    // 形（板・十字・筒）はこの断面列から作る。断面の作り方を1か所にまとめておけば、
    // 形を増やしても補間の側を触らずに済む。
    struct Ring
    {
        DirectX::XMFLOAT3 center = {};   // 断面の中心（ワールド, m）
        DirectX::XMFLOAT3 half   = {};   // 中心→先端のベクトル（幅カーブを掛けた後, m）
        float             age    = 0.0f; // フェードに使う経過時間（秒）
        float             taper  = 1.0f; // 尾ほど細くする係数（thickness を指定したときに使う）

        // この断面の元（元の区間の通し番号と、区間の中の位置 0〜1）。
        // ジグザグの「どの折れ点の上にいるか」を、フレームごとに変わる配列の添字ではなく
        // 区間そのものから決めるために持つ。
        uint32_t segStamp = 0;
        float    segT     = 0.0f;
    };

    // 同じ位置に置く、加算レイヤ用と歪みレイヤ用の頂点の組。
    // 歪みは加算と全く同じ形を使う。形の生成を2回しないよう、1頂点作るたびに両方の値をここで決める。
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

        void PushTriangle(const VertexPair &a, const VertexPair &b, const VertexPair &c) const;
    };

    // リングの区間を Catmull-Rom で補間し、連続した断面列に広げる（useInterpolation=false なら補間しない）
    void BuildRings(const TrailComponent &trail, std::vector<Ring> &outRings) const;

    // 断面列から、shape に応じた三角形リストを積む
    void EmitRibbon(const TrailComponent &trail, const std::vector<Ring> &rings,
                    const EmitSink &sink) const;
    void EmitCross(const TrailComponent &trail, const std::vector<Ring> &rings,
                   const EmitSink &sink) const;
    void EmitTube(const TrailComponent &trail, const std::vector<Ring> &rings,
                  const DirectX::XMFLOAT3 &cameraEye, const EmitSink &sink) const;

    // 断面 i の進行方向（末尾は1つ前の向きを使う）
    DirectX::XMVECTOR RingTangent(const std::vector<Ring> &rings, size_t i) const;

    // 進行方向に垂直な、断面内の正規直交基底を作る。
    // half が進行方向と平行だと基底がつぶれるので、その場合は適当な直交軸を使う。
    void RingBasis(DirectX::FXMVECTOR tangent, const DirectX::XMFLOAT3 &half,
                   DirectX::XMVECTOR &outU, DirectX::XMVECTOR &outV) const;

    // Cross / Tube の半径（m）。thickness=0 なら half の長さを使う
    float RingRadius(const TrailComponent &trail, const Ring &ring) const;

    /**
     * @brief 尾ほど薄くなる加算色と、対になる歪みのパラメータを作る。
     *
     * U（長さ方向）は age から求め、tiling と scroll を掛けて UV にする。
     *
     * @param vCoord テクスチャの V 座標（幅方向。Ribbon/Cross は根本 0・先端 1、Tube は円周上の位置）
     */
    VertexPair MakeVertex(const TrailComponent    &trail,
                          const DirectX::XMFLOAT3 &pos,
                          float age, float intensity, float vCoord) const;

    // 折れ点1つ分のずらすベクトル。断面内の基底 u, v の上で決める
    DirectX::XMVECTOR ZigZagOffset(const TrailComponent &trail, uint32_t node,
                                   DirectX::FXMVECTOR u, DirectX::FXMVECTOR v) const;

    // 滑らかな断面列を、稲妻のような折れ線にずらす（中心だけを動かす）
    void ApplyZigZag(const TrailComponent &trail, std::vector<Ring> &rings) const;
};
