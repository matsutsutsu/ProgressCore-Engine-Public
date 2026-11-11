#pragma once
// GPU パーティクルのエフェクト1つ。1 Entity が複数のエミッタを持ち、まとめて1アセットとして編集する
// 定数バッファは GPUParticleUpdateSystem が毎フレーム desc から組み立てる
#include "Engine/Graphics/Shader/Shared/ParticleShared.h"
#include "Engine/Graphics/Resource/ResourceManager.h"
#include "Engine/ECS/Common/CCL_Types.h"
#include <cstring>
#include <vector>
#include <d3d11.h>
#include <wrl/client.h>

// ===========================================================================
// ParticleEmitterDesc
//   エミッタの設定。シリアライズはこの構造体だけを見ればよい
//   - HLSL のレイアウト（CbParticleStatic）には依存しない
//   - 既定値のままで動くエミッタになる
//   - std::string や ComPtr を持たず、そのままコピーできる形にする
//   名前は camelCase。CbParticleStatic の snake_case と語幹を揃え、
//   BuildStaticCb()（GPUParticleUpdateSystem.cpp）で対応が分かるようにしている
// ===========================================================================
struct ParticleEmitterDesc
{
    // エミッタの表示名（「火花」「残り火」など）。描画には使わない
    // 1エフェクトが複数の層でできていると、モードと個数だけでは中身が分からないため付ける
    char name[32] = "";

    // ---- 発生形状 ----
    GpuData::float2 emitRadius = {0.0f, 0.5f};  // 発生半径の最小/最大（m）
    GpuData::float2 emitSize   = {0.1f, 0.05f}; // 粒子の大きさ（誕生時/消滅時, m）

    // 大きさのランダム量。0 で emitSize ちょうど、1 で 0〜emitSize の一様ランダム
    // 衝撃波のように毎回同じ大きさで出したいものは 0 にする
    float           sizeRandom   = 1.0f;

    // 誕生→消滅の大きさの変わり方。0: 線形 1: Ease-In（加速） 2: Ease-Out（減速） 3: Ease-InOut
    //   sizeEasePower は曲線の急さ（1 で線形と同じ、3〜5 で衝撃波らしい急な広がり）
    int   sizeEase      = 0;
    float sizeEasePower = 2.0f;
    GpuData::float2 coneAngle    = {0.0f, 0.3f}; // 放出角度の範囲（rad）
    GpuData::float2 emitSpeed    = {1.0f, 3.0f}; // 初速の範囲（m/秒）
    GpuData::float2 angularSpeed = {0.0f, 0.0f}; // 回転速度の範囲（rad/秒）
    GpuData::float2 lifespan     = {1.0f, 2.0f}; // 寿命の範囲（秒）
    GpuData::float2 spawnDelay   = {0.0f, 0.0f}; // 出現までの遅れの範囲（秒）
    GpuData::float2 fadeDuration = {0.2f, 0.3f}; // フェードイン/フェードアウトの時間（秒）

    // ---- 発生位置 ----
    GpuData::float3 localOffset  = {0, 0, 0};    // 発生位置のローカルなずれ（m）。線分モードでは始点（根本）
    float           localOffsetY = 0.0f;         // Y 方向の揺らぎ幅（m）。ディスクモード用
    GpuData::float2 emitStretch  = {1.0f, 1.0f}; // 発生範囲の倍率（x: X 軸, y: Z 軸）

    // ---- 発生形状（剣の刃に沿って撒く線分モード） ----
    int             emitShape = 0;                  // 0: ディスク 1: 線分（剣）
    GpuData::float3 lineEnd   = {0.0f, 1.0f, 0.0f}; // 線分の終点（刃先）。localOffset からのローカルなずれ（m）

    // ---- 色 ----
    GpuData::float4 color      = {1, 1, 1, 1}; // 手動モードの色

    // 発光の強さ。色（手動・ランプとも）に掛ける倍率で、1 で選んだ色そのまま
    //   描画先は HDR なので 1 を超えた分は明るく光り、ブルームがかかる
    //   トーンマップで白(1.0)も約 0.8 に圧縮されるので、目立たせたい粒子は 2〜5 程度に上げる
    float intensity = 1.0f;
    int             colorMode  = 0;            // 0: 手動 1: カラーランプ
    int             renderMode = 0;            // 0: 加算 1: 半透明

    // ---- 物理 ----
    float gravity = 0.0f; // Y 方向の加速度（m/秒^2）。負の値で下へ落ちる

    // 速度の上限（m/秒）。0 以下で無制限。速い火花を作るときは上げる
    float           maxSpeed        = 10.0f;
    float           velocityStretch = 0.0f;         // 速度方向に引き伸ばす量（0 でなし）
    float           noiseScale      = 1.0f;         // ノイズの細かさ
    float           noiseStrength   = 0.0f;         // ノイズで動きを乱す強さ（0 でなし）
    GpuData::float2 uvScrollSpeed   = {0.0f, 0.0f}; // テクスチャ UV のスクロール速度（UV/秒）。スプライトシートが1枚のときだけ効く

    // ---- エミッタへの追従 ----
    // 生まれた後の粒子を、エミッタが動いた分のこの割合だけ一緒に動かす（0〜1）
    //   0 は今までどおりその場に残る（命中の火花など）。1 は完全に付いていく（体にまとうオーラなど）
    //   1 だと体に貼り付いて見えやすいので、少し尾を引かせたいときは 0.7〜0.9 にする
    //   付いていくのは位置の移動だけで、エミッタの回転には付いていかない
    float followRatio = 0.0f;

    // ---- 見た目 ----
    GpuData::float2 scale      = {1.0f, 1.0f}; // emitSize に掛ける倍率（x: 誕生時, y: 消滅時）。生成時に大きさへ焼き込む
    GpuData::uint2  spriteGrid = {1, 1};       // スプライトシートの分割数（列, 行）
    int             spriteMode = 0;            // スプライトの選び方。0: ランダム 1: 寿命に合わせて進める

    // 四角形の向き。0: ビルボード（カメラを向く） 1: ワールド水平（地面と平行に寝る）
    //   2: エミッタの向きの平面（水平面をエミッタの回転で傾ける）
    //   1・2 は空間に固定されるので、2段ジャンプの衝撃波リングのような「寝た円」が作れる
    //   1・2 では速度方向への引き伸ばし（velocityStretch）は効かない
    int orientMode = 0;

    // ---- 放出設定 ----
    int maxCount     = 1000; // 粒子の最大数。バッファの大きさもこれで決まる
    int particleType = 1;    // 0: Point 1: Quad
    int emitMode     = 0;    // 0: 継続 1: バースト
    int burstCount   = 0;    // バースト1回で出す数

    // ---- Force Field（引力・斥力・渦・抵抗。すべて 0 なら無効） ----
    GpuData::float3 attractorPos      = {0, 0, 0}; // 引力の中心（m）。attractorLocal が true ならエミッタ相対
    float           attractorStrength = 0.0f;      // 正で引力、負で斥力。中心での加速度（m/秒^2）
    float           attractorRadius   = 0.0f;      // 影響半径（m）。0 で無限、正なら端に向かって滑らかに弱まる
    float           vortexStrength    = 0.0f;      // 中心軸まわりの渦の強さ（0 でなし。引力と一緒に使う）
    float           drag              = 0.0f;      // 速度の減衰率（1/秒。0 でなし）
    bool            attractorLocal    = true;      // true: エミッタ相対 false: ワールド座標

    // ---- アセット参照（コピーできる形を保つため固定長配列） ----
    char texturePath[128]   = {}; // 粒子のテクスチャ
    char colorRampPath[128] = {}; // colorMode=1 で使うカラーランプ

    ParticleEmitterDesc()
    {
        std::memset(texturePath, 0, sizeof(texturePath));
        std::memset(colorRampPath, 0, sizeof(colorRampPath));
    }
};

// ===========================================================================
// ParticleEmitter
//   エミッタ1つ分。設定（desc）と、そのエミッタだけの実行時状態を持つ
//   粒子バッファはエミッタごとに持つ。エフェクト全体で1本のバッファを区切って使えば
//   Dispatch を1回にまとめられるが、その場合もアセット形式（desc の配列）は変えずに済む
// ===========================================================================
struct ParticleEmitter
{
    ParticleEmitterDesc desc; // 設定（編集・保存する値）

    // ---- 実行時状態（保存しない） ----
    TextureHandle        texture;       // desc.texturePath から読み込んだテクスチャ
    TextureHandle        colorRamp;     // desc.colorRampPath から読み込んだカラーランプ
    ParticleBufferHandle computeBuffer; // 粒子バッファ（ResourceManager のプールが持つ）

    bool  isInitialized = false; // false なら UpdateSystem がバッファを作り直す
    bool  needsGpuInit  = true;  // true なら InitCS を走らせる
    bool  staticDirty   = true;  // true なら staticCB を送り直す（desc を変えたとき）
    float burstTrigger  = 0.0f;  // TriggerBurst() で立ち、UpdateSystem が消費する

    // このエミッタの乱数の種。GPU へ instance_seed として渡る
    // 0 は未割り当てで、UpdateSystem が最初の更新で決めて以後は変えない
    // 同じプレハブから出した VFX でも、個体ごとに粒子の配置が変わる
    // 保存すると全インスタンスが同じ種になるので保存しない
    GpuData::uint instanceSeed = 0;

    // 発生のオン/オフ。GPU へ emit_enabled として渡る
    // false の間は継続モードの粒子を新しく出さないだけで、出ている粒子は寿命まで残る
    // アニメ連動（GPUParticleComponent::animControlled）のときは ParticleAnimationSyncSystem が書き換える
    bool emitting = true;

    // インスペクタでの表示オン/オフ。false の間は描画コマンドを積まない
    // どの層がどれかを確かめるためのプレビュー用なので保存しない
    // （保存すると、消したまま出荷して層が欠けても気付けない）
    // シミュレーションは止めないので、戻すと続きから見える
    bool previewVisible = true;
};

// ===========================================================================
// GPUParticleComponent
//   エフェクトを作るエミッタの配列と、位置・時間・アニメ連動など
//   エフェクト全体で共通の状態を持つ
// ===========================================================================
struct GPUParticleComponent
{
    std::vector<ParticleEmitter> emitters{ParticleEmitter{}}; // エミッタの配列。常に1個以上（エディタで増減する）

    // ---- エフェクト全体で共通の実行時状態 ----
    float time = 0.0f; // 経過時間の合計（秒）。全エミッタで共有する

    // true なら、祖先の Animator の Particle 区間イベントで emitting を切り替える
    // false なら emitting は手動（既定。常にオン）。このフラグだけは保存する
    bool animControlled = false;

    // animControlled のときに見つけた、Animator を持つ祖先（保存しない）
    // ParticleAnimationSyncSystem が親をたどって一度だけ探し、ここに持つ。InvalidEntityID は未解決
    CCL::ECS::EntityID ownerEntity = CCL::ECS::InvalidEntityID;

    // ---- サブフレーム補間用の前フレームの姿勢（実行時のみ。保存しない） ----
    // 剣を速く振っても、生成位置が1フレームごとの飛び飛びにならず軌道上に並ぶよう、
    // UpdateSystem が前フレームのワールド姿勢を持ち、生成時に補間する
    // エミッタはすべて同じ Entity に乗っているので、全エミッタで共通
    GpuData::float3 prevWorldPos     = {0.0f, 0.0f, 0.0f};
    GpuData::float4 prevWorldRot     = {0.0f, 0.0f, 0.0f, 1.0f};
    bool            hasPrevTransform = false; // false の初回は今の姿勢を入れる（補間で尾を引かせない）

    GPUParticleComponent() = default;

    // 全エミッタのバーストを1フレームだけ起こす
    void TriggerBurst()
    {
        for (auto &e : emitters)
            e.burstTrigger = 1.0f;
    }

    // desc を変えた後に呼ぶ。次のフレームで staticCB を送り直す
    void MarkDescChanged()
    {
        for (auto &e : emitters)
            e.staticDirty = true;
    }

    // 全エミッタの発生をまとめてオン/オフする（アニメ連動用）
    void SetEmitting(bool on)
    {
        for (auto &e : emitters)
            e.emitting = on;
    }
};
