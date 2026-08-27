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
#include <cstdint>

/// @brief 断面の作り方。
/// @details Ribbon は板1枚なので、面が視線と平行になった瞬間に線へ潰れる。
///          剣の軌跡は「振る面を見せる」ものなのでそれで正しいが、足元の尾のように
///          どこから見ても体積が欲しい用途では Cross / Tube を使う。
enum class TrailShape : uint8_t
{
    Ribbon = 0, ///< 根本→先端の線分を掃いた板1枚（従来どおり。剣向き）
    Cross  = 1, ///< 板を進行方向まわりに複製して十字に（安い・真横でも消えない）
    Tube   = 2, ///< 断面を円にして押し出した筒（足元の尾など、体積が欲しいとき）
};

/// @brief CFIELD_ENUM_U8 用の表示名テーブル（enum の並びと一致させること）。
static constexpr const char *TrailShapeNames[] = {
    "Ribbon (板1枚)",
    "Cross (十字)",
    "Tube (筒)",
};
static constexpr int TrailShapeCount = 3;

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

    /// @brief Catmull-Rom補間の切り替えフラグ
    bool useInterpolation = true;

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

    // --- 断面形状（config。既定は従来と同じ板1枚） ---
    uint8_t shape = static_cast<uint8_t>(TrailShape::Ribbon); ///< 断面の作り方

    /// @brief Cross / Tube の半径。0 なら「根本→先端の長さの半分」を使う。
    /// @note  足元に付ける場合は baseOffset/tipOffset をほぼ同じ点にして、
    ///        ここで太さを決めるほうが扱いやすい。
    float thickness = 0.0f;

    int tubeSides = 6; ///< Tube の円周分割数（3〜16）。増やすほど滑らかで重い。

    /// @brief Tube のシルエット強調。1で「輪郭だけ光る筒」、0で「均一な塊」。
    /// @note  加算合成は全面が同じ明るさだと のっぺりした塊に見えるため、
    ///        視線に対して横を向く面ほど明るくして丸みを出す。
    float rimPower = 0.7f;

    // --- 歪み（屈折）レイヤ（config） ---
    /// @brief 背景をずらす最大量(ピクセル)。0 で歪みレイヤ自体を作らない。
    /// @note  加算レイヤと全く同じ形状を、SceneColorGrabPass が確保済みの
    ///        シーンカラーを屈折させて描く2枚目として重ねる。
    ///        ずらし方向は面の法線をビュー空間へ落とした xy なので、
    ///        面がカメラを向くほど 0 に近づき、輪郭で最も強く歪む。
    float distortion = 0.0f;

    /// @brief 屈折の色ズレ(0〜1)。RGB のずらし量に差をつけてプリズム感を出す。
    float distortionChroma = 0.0f;

    /// @brief 陽炎ゆらぎ(0〜1)。グローバルノイズでずらし方向を乱す。
    /// @note  板(Ribbon)が正面を向いていると法線由来の歪みはほぼ 0 になる。
    ///        剣の軌跡のように「面を見せる」使い方で歪ませたいときはここを上げる。
    float distortionNoise = 0.0f;

    // --- ダッシュ等、一時的にTrailをONにするための設定 ---
    float burstDuration = 0.25f; ///< 一時的にTrailを出す時間

    // --- runtime ---
    float burstTimer = 0.0f; ///< 一時Trail表示の残り時間

    // --- ジグザグ形状 ---
    bool     useZigZag       = false; ///< トレイルをランダムに折れ曲げる
    float    zigZagAmplitude = 0.08f; ///< 折れ曲がり量
    int      zigZagFrequency = 2;     ///< 1区間あたりの折れ点数
    uint32_t zigZagSeed      = 1337;  ///< 決定論的乱数シード


    /// @brief 一時的にTrailを出現させるための関数
    /// @param duration：トレイルを出す秒数
    void ActivateFor(float duration)
    {
        burstTimer = duration;
        emitting   = true;
    }

    /// @brief 一時Trail出現タイマーの更新
    void UpdateBurstTimer(float dt)
    {
        if (burstTimer <= 0.0f)
            return;

        burstTimer -= dt;

        if (burstTimer <= 0.0f)
        {
            burstTimer = 0.0f;
            emitting   = false;
        }
    }
};
