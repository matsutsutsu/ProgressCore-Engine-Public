#pragma once
// ===========================================================================
// TrailComponent.h
// ----------------
// 役割:
//   剣など「振る武器」の軌跡（ソードトレイル）を残すためのデータ。
//   武器（または刃）エンティティに載せ、TrailSystem がワールド空間の
//   2端点(根本/先端)を毎フレーム記録して加算リボンを生成する。
//
// 責務:
//   - 刃のローカル2端点(baseOffset/tipOffset)と見た目パラメータの保持。
//   - 過去フレームの断面(Segment)をヒープ確保なしの固定長リングで保持する。
//     区間は age を持ち、lifeTime を超えたら最古側から捨てられる（尾が伸縮）。
//
// 設計方針:
//   - 全て POD。ランタイムのリングバッファは固定長配列（他の Combat 系と同様に
//     ヒープ確保を避ける）。config フィールドのみシリアライズし、リングは保存しない。
//   - 静止時に軌跡が溜まって汚れないよう、先端が minDistance 以上動いたときだけ
//     新区間を刻む（＝速く振ったときだけ尾が出る、剣らしい挙動）。
//   - emitting は手動運用が既定。animControlled=true にすると、TrailAnimationSyncSystem
//     が祖先(Animator)の Trail 区間イベントを見て emitting を毎フレーム上書きする。
// ===========================================================================
#include "Engine/ECS/Common/CCL_Types.h"
#include <DirectXMath.h>

/// @brief 武器の軌跡(加算リボン)を残すためのコンポーネント。
struct TrailComponent
{
    // --- config（シリアライズ対象。ローカル空間・向きは所有エンティティ基準） ---
    DirectX::XMFLOAT3 baseOffset = {0.0f, 0.0f, 0.0f}; ///< 刃の根本（ローカル）
    DirectX::XMFLOAT3 tipOffset  = {0.0f, 1.0f, 0.0f}; ///< 刃の先端（ローカル）
    DirectX::XMFLOAT4 color      = {0.6f, 0.8f, 1.0f, 1.0f}; ///< 加算色rgb + 基準アルファa

    float lifeTime    = 0.18f; ///< 1区間が消えるまでの秒数（尾の長さ）
    float minDistance = 0.02f; ///< 先端がこれ以上動いたら新区間を刻む（静止時の汚れ防止）
    bool  emitting    = true;  ///< 発生マスタースイッチ。animControlled=false のとき手動運用。

    /// @brief true: 祖先(Animator)の Trail 区間イベントで emitting を自動駆動する。
    ///        false: emitting は手動（インスペクタ/常時ON）。既定は手動。
    bool animControlled = false;

    // --- アニメ連動のランタイムキャッシュ（非シリアライズ） ---
    /// @brief animControlled 時に解決した「Animator を持つ祖先」。0/Invalid は未解決。
    ///        TrailAnimationSyncSystem が親を辿って一度だけ解決しキャッシュする。
    CCL::ECS::EntityID ownerEntity = CCL::ECS::InvalidEntityID;

    // --- runtime ring buffer（非シリアライズ・POD固定長） ---
    /// @brief 1フレーム分の刃の断面（根本-先端の線分）と経過時間。
    struct Segment
    {
        DirectX::XMFLOAT3 base = {}; ///< 記録時の根本ワールド座標
        DirectX::XMFLOAT3 tip  = {}; ///< 記録時の先端ワールド座標
        float             age  = 0.0f; ///< 記録からの経過秒。lifeTime 超で破棄。
    };

    static constexpr int MAX_SEGMENTS = 24; ///< リングの容量（尾の最大分割数）

    Segment segments[MAX_SEGMENTS] = {}; ///< 断面リング
    int     head  = 0; ///< 最古区間の index
    int     count = 0; ///< 有効区間数

    float width           = 1.0f; ///< トレイル全体の基本幅。1.0で元の太さ。
    float widthCurvePower = 1.5f; ///< 尾に向かって細くなる強さ。大きいほど急激に細くなる。
};
