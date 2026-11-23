#pragma once
#include "Engine/ECS/Common/CCL_Types.h"
#include "Engine/Graphics/Resource/ResourceTypes.h"
#include <DirectXMath.h>
#include <cstdint>
#include <string>

// 断面の作り方。Ribbon は面が視線と平行になると線につぶれるので、
// どこから見ても太さが欲しいときは Tube を使う
enum class TrailShape : uint8_t
{
    Ribbon = 0, // 根本から先端までの線分を掃いた板1枚
    Tube   = 1, // 断面を円にして押し出した筒
};

// CFIELD_ENUM_U8 用の表示名（TrailShape の並びと一致させる）
static constexpr const char *TrailShapeNames[] = {
    "Ribbon (板1枚)",
    "Tube (筒)",
};
static constexpr int TrailShapeCount = 2;

// 剣や足元に付ける軌跡。TrailRecordSystem が時間を進め、
// TrailSystem が断面を刻んで描く
struct TrailComponent
{
    // --- 設定（保存する。座標は Entity のローカル空間） ---

    DirectX::XMFLOAT3 baseOffset = {0.0f, 0.0f, 0.0f};       // 刃の根本（m）
    DirectX::XMFLOAT3 tipOffset  = {0.0f, 1.0f, 0.0f};       // 刃の先端（m）
    DirectX::XMFLOAT4 color      = {0.6f, 0.8f, 1.0f, 1.0f}; // 加算する色と基準のアルファ

    std::string texturePath; // 貼るテクスチャのパス

    DirectX::XMFLOAT2 uvTiling = {1.0f, 1.0f}; // UV の繰り返し回数（長さ, 幅）
    DirectX::XMFLOAT2 uvScroll = {0.0f, 0.0f}; // UV の流れる速さ（UV/秒）

    float lifeTime    = 0.18f; // 断面が消えるまでの時間（秒）
    float minDistance = 0.02f; // 刻むのに要る先端の移動量（m）
    bool  emitting    = true;  // 断面を刻むか

    bool useInterpolation = true; // 断面の間を Catmull-Rom で補間するか

    bool animControlled = false; // アニメの Trail 区間で emitting を切り替えるか

    float width           = 1.0f; // 幅の倍率
    float widthCurvePower = 1.5f; // 尾に向かって細くなる強さ

    uint8_t shape = static_cast<uint8_t>(TrailShape::Ribbon); // 断面の作り方（TrailShape）

    float thickness = 0.0f; // 筒の半径（m）
    int   tubeSides = 6;    // 筒の円周の分割数（3〜16）

    float distortion = 0.0f; // 背景をずらす最大量（px）

    float burstDuration = 0.25f; // ActivateFor で出す時間（秒）

    // --- 実行時の状態（保存しない） ---

    TextureHandle     textureHandle;                  // texturePath から引いたテクスチャ
    DirectX::XMFLOAT2 uvScrollOffset = {0.0f, 0.0f}; // uvScroll を積んだ今のずれ

    CCL::ECS::EntityID ownerEntity = CCL::ECS::InvalidEntityID; // Animator を持つ祖先

    float timeScale = 1.0f; // 老化に使った時間の倍率

    // 記録した1つの断面（刃の根本と先端）
    struct Segment
    {
        DirectX::XMFLOAT3 base = {};   // 根本のワールド座標（m）
        DirectX::XMFLOAT3 tip  = {};   // 先端のワールド座標（m）
        float             age  = 0.0f; // 記録してからの時間（秒）
    };

    // リングの容量。スロー中は断面が長く残るので、
    // 等速で要る数よりずっと多く取ってある
    static constexpr int MAX_SEGMENTS = 256;

    Segment segments[MAX_SEGMENTS] = {}; // 断面のリングバッファ
    int     head                   = 0;  // 一番古い断面の添字
    int     count                  = 0;  // 有効な断面の数

    bool sampleRequested = false; // 1区間刻んでいい印（TrailSystem が降ろす）

    float burstTimer = 0.0f; // 一時トレイルの残り時間（秒）

    // duration 秒の間だけトレイルを出す。切れると emitting が false に戻る
    void ActivateFor(float duration)
    {
        burstTimer = duration;
        emitting   = true;
    }

    // 一時トレイルのタイマーを進め、切れたら emitting を落とす
    void UpdateBurstTimer(float dt)
    {
        if (burstTimer <= 0.0f) return;

        burstTimer -= dt;

        if (burstTimer <= 0.0f)
        {
            burstTimer = 0.0f;
            emitting   = false;
        }
    }
};
