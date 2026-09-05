// 攻撃系の陣形クラス。Formations_Basic.h（周回・待機系）と対になる。
//
// 責務は3つ。予備動作の間に targetPosition で「形」を作る（プレイヤーはこの形から
// 回避方向を読む）、同時に fireDirection を仕込む、撃った後は狙いを凍結する
// （ホーミングさせない＝ジャスト回避で抜けられる）。
#pragma once
#include "Game/Logics/AI/Drone/Formation/IFormation.h"
#include "Game/Logics/AI/Drone/Formation/FormationRegistry.h"
#include "Game/Logics/AI/Drone/Formation/Formations_Basic.h" // 弾幕が頭上リングを借りる
#include <cmath>
#include <algorithm>

namespace CCL::AI::Formation::Attack
{
// 縦割り（GuillotineSlam）── 要求回避＝横回避。
// ボス前方に縦の刃を立て、下端を軸に橋が倒れるように振り下ろす。
// θ=0 が直立（読ませる形）、θ=90° で前方へ水平。下端は θ に依らず不動＝回転軸。
struct GuillotineSlamFormation : public IFormation
{
    void Calculate(DroneComponent &drone, const FormationContext &ctx) override
    {
        using namespace DirectX::SimpleMath;

        // 陣形自体が動く技なので、突撃中も targetPosition を更新して剛体追従させる。
        drone.chargeMode = ChargeMode::RigidFollow;

        // playerPos を直接見ないこと。AimDirection() は「撃つと決めた瞬間」の向きを
        // 返すので、倒れている最中にプレイヤーが横へ動いても刃は追わない。
        const Vector3 dir = ctx.AimDirection();

        Vector3 right = Vector3::UnitY.Cross(dir);
        if (right.LengthSquared() < 0.001f) right = Vector3::UnitX;
        right.Normalize();

        // localIndex を (col, row) の格子へ割り付けて壁を作る。
        const int total = (drone.totalDrones > 0) ? drone.totalDrones : 1;
        const int cols  = (std::max)(1, static_cast<int>(std::floor(std::sqrt(static_cast<float>(total)))));
        const int col   = drone.localIndex % cols;
        const int row   = drone.localIndex / cols;

        const float spacing      = 2.0f; // 刃の目の細かさ
        const float hingeHeight  = 2.0f; // 回転軸（下端）の高さ
        const float hingeForward = 5.0f; // ボスからプレイヤー側へ離す距離

        if (drone.wantsAttack)
            drone.attackProgress = (std::min)(1.0f, drone.attackProgress + ctx.dt * kSwingSpeed);
        else
            drone.attackProgress = 0.0f;

        const float eased = drone.attackProgress * drone.attackProgress; // easeIn（倒れが加速）
        const float theta = eased * kThetaMax;                            // 0 → kThetaMax
        const float c = std::cos(theta);
        const float s = std::sin(theta);

        const float height = row * spacing;                       // 下端(row0)=0＝軸
        const float side   = (col - (cols - 1) * 0.5f) * spacing; // 幅方向は中央揃え

        // 回転軸はボスの真上ではなくプレイヤー側へ出す。真上に置くと壁がボスを跨いで
        // 生え、予備動作の形がボスと重なって読めない。
        const Vector3 hingeBase =
            ctx.bossPos + Vector3::UnitY * hingeHeight + dir * hingeForward;

        // 高さベクトルを下端軸まわりに θ 回転（up → dir へ倒す）
        const Vector3 rotatedUp = Vector3::UnitY * (height * c) + dir * (height * s);

        drone.targetPosition = hingeBase + right * side + rotatedUp;

        // 向き：倒れていく接線方向（-up*sin + dir*cos）を向かせる
        Vector3 look = Vector3::UnitY * (-s) + dir * c;
        if (look.LengthSquared() < 0.001f) look = dir;
        look.Normalize();
        drone.fireDirection       = look;
        drone.targetLookDirection = look;
    }

    static constexpr float kSwingSpeed = 2.5f; // 倒れ切りまで ≈0.4s
    static constexpr float kThetaMax   = 1.9f; // ≈109°。水平を少し越えて叩きつける
};

// 挟み込み1段（ClapSlam）── 要求回避＝ジャンプ。
// ボス前方へ左右2本の腕（低い壁）を張り出し、真横から前方中央へ振り込んで挟む。
// 配置は「回転させた腕方向 × 距離」で出す。奥行きをボス→プレイヤー軸に沿って
// 伸ばすと、機数が増えたとき壁が前後に長い回廊になってしまう。
struct ClapSlamFormation : public IFormation
{
    void Calculate(DroneComponent &drone, const FormationContext &ctx) override
    {
        using namespace DirectX::SimpleMath;

        drone.chargeMode = ChargeMode::RigidFollow; // 腕が形を保ったまま閉じる（剛体スライド）

        const Vector3 centerDir = ctx.AimDirection();  // 腕はこの線上へ閉じる

        // 左右どちらの腕か → 腕内番号 k を (距離セグメント × 高さ) へ割り付ける。
        const bool isRight = (drone.localIndex % 2 == 0);
        const int  k       = drone.localIndex / 2;
        const int  hRow    = k % kHeightRows;         // 高さ（低い壁＝ジャンプで越える）
        const int  seg     = k / kHeightRows;         // 腕の先端方向（前方へ伸びる）

        const float distance = kBaseDistance + seg  * kDistanceStep;
        const float height   = kBaseHeight   + hRow * kHeightSpacing;

        // 腕の角度を「真横(±kStartAngle)→前方中央(±kEndAngle)」へ閉じる。
        if (drone.wantsAttack)
            drone.attackProgress = (std::min)(1.0f, drone.attackProgress + ctx.dt * kClapSpeed);
        else
            drone.attackProgress = 0.0f;

        const float eased = drone.attackProgress * drone.attackProgress; // easeIn（バチンと閉じる）
        const float sign  = isRight ? 1.0f : -1.0f;
        const float angle = sign * (kStartAngle + (kEndAngle - kStartAngle) * eased);

        const Quaternion swing  = Quaternion::CreateFromAxisAngle(Vector3::UnitY, angle);
        const Vector3    armDir  = Vector3::Transform(centerDir, swing);

        drone.targetPosition = ctx.bossPos + armDir * distance + Vector3::UnitY * height;

        Vector3 look = ctx.playerPos - drone.targetPosition;  // 内向き
        look.y = 0.0f;
        if (look.LengthSquared() < 0.001f) look = centerDir;
        look.Normalize();
        drone.fireDirection       = look;
        drone.targetLookDirection = look;
    }

    static constexpr float kClapSpeed     = 3.0f;   // 閉じ切り ≈0.33s
    static constexpr int   kHeightRows    = 2;      // 低い壁（ジャンプで越えられる）
    static constexpr float kBaseDistance  = 3.0f;   // 最内の腕のボスからの距離
    static constexpr float kDistanceStep  = 1.2f;   // 腕が前方へ伸びる間隔
    static constexpr float kBaseHeight    = 0.8f;   // 壁の下端（地面付近）
    static constexpr float kHeightSpacing = 1.5f;   // 高さ段の間隔（低め）
    static constexpr float kStartAngle    = 1.5f;   // 開始角(rad)≈86°（ほぼ真横に開く）
    static constexpr float kEndAngle      = 0.08f;  // 閉じ切り角(rad)≈5°（前方中央でほぼ接触）
};

// 弾幕（KamikazeSwarm）── 要求回避＝ジャスト回避。
// 頭上の輪を回したまま1機ずつ中心へ降り、そこからプレイヤーへ高速で撃ち出す。
//   輪の上で待つ → 自分の番の kGatherTime 秒前に中心へ寄る（これが予告）
//   → fireDelay が 0 で発射（以降は直進）
//
// 置き技（縦割り・挟み込み）と違い、発射の瞬間まで狙い続ける「1発ずつ今のあなたを
// 狙う」技。撃ち出した後は直進するので避ける余地はそこで担保される。
struct KamikazeSwarmFormation : public IFormation
{
    // 撃ち出したら各機が独立して飛ぶので、終了判断は個体に持たせる（帰投がバラバラになる）。
    ChargeLifecycle GetLifecycle() const override { return ChargeLifecycle::Individual; }

    // 中心へ集まる移動そのものが予告。足すと予告が二重になってどちらを読むか濁る。
    bool UsesShiverTelegraph() const override { return false; }

    void Calculate(DroneComponent &drone, const FormationContext &ctx) override
    {
        using namespace DirectX::SimpleMath;

        // 陣形が手を引くのは「飛び出した後」だけ。順番待ちも中央でのタメもまだ撃って
        // いないので狙いを更新し続ける（stateTimer を見落とすと着弾の 0.5 秒以上前の
        // 座標を撃つことになり、歩いているだけで当たらなくなる）。
        const bool launched = (drone.currentState == DroneState::FireCharge) &&
                              (drone.fireDelay <= 0.0f) && (drone.stateTimer <= 0.0f);
        if (launched) return;

        drone.chargeMode = ChargeMode::Straight;

        const int total = (drone.totalDrones > 0) ? drone.totalDrones : 1;

        // 待機位置は頭上円陣と同じ輪。回転位相まで共有するので、BT が
        // 頭上円陣 → 弾幕 と切り替えてもドローンは動かない。
        const Vector3 slot = ctx.bossPos + Basic::HighRing::SlotOffset(
                                               drone.localIndex, drone.totalDrones, ctx.time);

        // 発射地点は輪の中心の少し下。
        const Vector3 muzzle =
            ctx.bossPos + Vector3::UnitY * (Basic::HighRing::kHeight - kGatherDrop);

        // 自分の番が近づくほど中心へ寄る。二乗で寄せるのは、序盤をほぼ動かさず
        // 「直前にすっと吸い込まれる」形にするため（線形だといつ撃つのか読めない）。
        float gather = 0.0f;
        if (drone.currentState == DroneState::FireCharge && drone.fireDelay <= 0.0f)
        {
            // 中央でのタメ中は持ち場を発射地点のままにする。輪へ戻すと下の飛翔時間が
            // 「輪から撃つ」前提になり、実際の発射地点とずれた先読みになる。
            gather = 1.0f;
        }
        else if (drone.fireDelay > 0.0f && drone.fireDelay < kGatherTime)
        {
            const float t = 1.0f - (drone.fireDelay / kGatherTime);
            gather        = t * t;
        }

        drone.targetPosition = Vector3::Lerp(slot, muzzle, gather);

        // 偏差射撃。今いる場所を撃つと飛翔時間ぶん必ず後ろへ外れるので、
        // 狙う先 ＝ 今の位置 ＋ 速度 × (タメの残り ＋ 距離÷射出速度) を解く。
        // 距離が狙う先に依存する循環なので、1回だけ計算し直して精度を上げる。
        const float launchSpeed = drone.moveSpeed * kDroneChargeSpeedScale;
        const float windupLeft  = (drone.stateTimer > 0.0f) ? drone.stateTimer : 0.0f;

        // 足元(playerPos)ではなく体の中心を狙う（足元は地面なので身長ぶん下に刺さる）。
        Vector3 aimPoint = ctx.playerCenter;
        if (launchSpeed > 0.001f)
        {
            for (int i = 0; i < 2; ++i)
            {
                const float flight =
                    (aimPoint - drone.targetPosition).Length() / launchSpeed;
                aimPoint = ctx.playerCenter +
                           ctx.playerVelocity * ((windupLeft + flight) * kLeadFactor);
            }
        }

        Vector3 toAim = aimPoint - drone.targetPosition;
        if (toAim.LengthSquared() > 0.001f) toAim.Normalize();
        else                                toAim = Vector3::UnitZ;

        drone.fireDirection       = toAim;
        drone.targetLookDirection = toAim;

        // 発射順（先頭ほど早い）。撃つ合図が出た後に書き直すと順番がリセットされ続けて
        // 永久に撃てなくなるので、まだ攻撃に入っていない間だけ割り振る。
        if (drone.currentState != DroneState::FireCharge)
        {
            drone.fireDelay =
                (static_cast<float>(drone.localIndex) / static_cast<float>(total)) * kFireWindow;
        }
    }

    static constexpr float kFireWindow = 8.0f; // 全機を撃ち切る秒数。伸ばすほど発射レートが下がる
    static constexpr float kGatherTime = 0.6f; // 発射の何秒前から中心へ寄り始めるか
    static constexpr float kGatherDrop = 6.0f; // 輪の高さから何m下を発射地点にするか

    // 先読みの強さ。0=今の位置を撃つ / 1=完全な迎撃。狙いは発射の瞬間に凍るので、
    // 「歩くだけで避けられる」を潰しつつ「避ければ避けられる」を残す値にする。
    static constexpr float kLeadFactor = 0.9f;
};

// 挟み込み2段（MegaClapSlam）── 「形を読む」を最も強く要求する技。
// 1段版の上下2段版で、どちらの段が先に閉じるかを予備動作の震えで読ませる。
//   下段が先 → ジャンプで避け、着地してから上段をやり過ごす
//   上段が先 → 接地のままやり過ごし、後から来る下段をジャンプ
// 4グループ =(下段/上段)×(左/右)。先段は 0.0→0.5、後段は 0.5→1.0 で間隔を閉じる。
struct MegaClapSlamBase : public IFormation
{
    virtual bool UpperFirst() const = 0;

    void Calculate(DroneComponent &drone, const FormationContext &ctx) override
    {
        using namespace DirectX::SimpleMath;

        drone.chargeMode = ChargeMode::RigidFollow; // 壁が形を保ってスライド

        const Vector3 dir = ctx.AimDirection();
        Vector3 right = Vector3::UnitY.Cross(dir);
        if (right.LengthSquared() < 0.001f) right = Vector3::UnitX;
        right.Normalize();

        // 4グループへ割付：g=0下段左 / 1下段右 / 2上段左 / 3上段右。
        const int total = (drone.totalDrones > 0) ? drone.totalDrones : 1;
        const int per   = (std::max)(1, total / 4);
        int g = drone.localIndex / per; if (g > 3) g = 3;
        const bool isUpper = (g >= 2);
        const bool isLeft  = (g % 2 == 0);
        const int  k       = drone.localIndex - g * per; // グループ内 index

        const int   heightRows    = 2; // 各壁は低い（1段分の高さ）
        const int   colsPerWall   = (std::max)(1, (per + heightRows - 1) / heightRows);
        const int   col           = k / heightRows;
        const int   row           = k % heightRows;

        const float depthSpacing  = 2.5f;
        const float heightSpacing = 1.2f;
        const float clapForward   = 6.0f;
        const float lowerBase     = 0.4f; // 下段の下端（接地で当たる／ジャンプで越える）
        const float upperBase     = 2.8f; // 上段の下端（ジャンプ頂点で当たる／接地でくぐる）

        if (drone.wantsAttack)
            drone.attackProgress = (std::min)(1.0f, drone.attackProgress + ctx.dt * kSpeed);
        else
            drone.attackProgress = 0.0f;

        // この段が「先に閉じる段」か → 担当フェーズを決める。
        const bool  firstTier  = (isUpper == UpperFirst());
        const float phaseStart = firstTier ? 0.0f : 0.5f;
        const float closeP     = std::clamp((drone.attackProgress - phaseStart) / 0.5f, 0.0f, 1.0f);
        const float eased      = closeP * closeP;                    // easeIn（バチンと閉じる）
        const float gap        = kStartGap + (kEndGap - kStartGap) * eased;

        const float sideSign = isLeft ? -1.0f : 1.0f;
        const float depth    = (col - (colsPerWall - 1) * 0.5f) * depthSpacing;
        const float tierBase = isUpper ? upperBase : lowerBase;

        Vector3 pos = ctx.bossPos + dir * clapForward + Vector3::UnitY * tierBase
                    + right * (sideSign * gap)
                    + dir   * depth
                    + Vector3::UnitY * (row * heightSpacing);

        // 先に閉じる段だけ forming 中に左右へ震える＝「こっちが先」の tell
        if (drone.attackProgress <= 0.0f && firstTier)
        {
            const float sh = std::sin(ctx.time * kShiverFreq + static_cast<float>(drone.localIndex)) * kShiverAmp;
            pos += right * sh;
        }

        drone.targetPosition = pos;
        Vector3 look = right * (-sideSign);
        if (look.LengthSquared() < 0.001f) look = dir;
        look.Normalize();
        drone.fireDirection       = look;
        drone.targetLookDirection = look;
    }

    // lowerBase/upperBase の高さは「接地で下段に当たり・ジャンプ頂点で上段に当たる」
    // 関係になるよう、プレイヤーのジャンプ高さと合わせて実測して決める。
    static constexpr float kSpeed      = 1.2f;  // 全閉まで ≈0.83s
    static constexpr float kStartGap   = 12.0f; // 開始の左右間隔
    static constexpr float kEndGap     = 1.2f;  // 閉じ切りの左右間隔
    static constexpr float kShiverFreq = 10.0f; // tell の震えの速さ（追従で消えない程度に低め）
    static constexpr float kShiverAmp  = 0.6f;  // tell の震えの幅
};

struct MegaClapUpperFirstFormation : public MegaClapSlamBase { bool UpperFirst() const override { return true;  } };
struct MegaClapLowerFirstFormation : public MegaClapSlamBase { bool UpperFirst() const override { return false; } };

// 横なぎ回転（SideSlapWhip）── ジャスト回避を誘う技。
// ボスの片側に横長の腕を作り、縦軸まわりに回転して正面を薙ぎ払う。
// 回転と逆に走れば無効化できるので、払いにダッシュを合わせさせる狙い。
struct SideSlapWhipFormation : public IFormation
{
    void Calculate(DroneComponent &drone, const FormationContext &ctx) override
    {
        using namespace DirectX::SimpleMath;
        drone.chargeMode = ChargeMode::RigidFollow;

        const Vector3 dir = ctx.AimDirection();
        Vector3 right = Vector3::UnitY.Cross(dir);
        if (right.LengthSquared() < 0.001f) right = Vector3::UnitX;
        right.Normalize();

        // 腕：半径方向に長く(col)、高さ方向に少し幅(row)
        const int total = (drone.totalDrones > 0) ? drone.totalDrones : 1;
        const int rows  = 3;
        const int col   = drone.localIndex / rows; // 半径方向（横長）
        const int row   = drone.localIndex % rows; // 高さ（縦に少し幅）

        const float minRadius     = 4.0f;
        const float radialSpacing = 2.0f;
        const float baseHeight    = 1.5f;
        const float heightSpacing = 1.5f;

        if (drone.wantsAttack)
            drone.attackProgress = (std::min)(1.0f, drone.attackProgress + ctx.dt * kSweepSpeed);
        else
            drone.attackProgress = 0.0f;

        const float e = drone.attackProgress * drone.attackProgress; // easeIn（一気に薙ぐ）
        const float a = kStartAngle + kSweepAngle * e;

        const Vector3 armDir     = dir * std::cos(a) + right * std::sin(a);
        const float   radialDist = minRadius + col * radialSpacing;
        const float   height     = baseHeight + row * heightSpacing;

        drone.targetPosition      = ctx.bossPos + armDir * radialDist + Vector3::UnitY * height;
        drone.fireDirection       = armDir;
        drone.targetLookDirection = armDir;
    }

    static constexpr float kStartAngle = 1.75f;  // ≈+100°（右後方でタメ）
    static constexpr float kSweepAngle = -3.5f;  // ≈-200°（前を通過して左後方まで薙ぐ）
    static constexpr float kSweepSpeed = 1.6f;   // 進行度の速度（速い＝一気に薙ぐ）
};

// 波状縦割り（TsunamiWave）── 横方向の読み合い（挟み込み2段と対になる技）。
// 縦割りの刃を横に並べ、波のように順次倒す。どちら向きに波が来るかを予備動作の
// 震えで読ませ、プレイヤーは波に合わせて横へ走り続ける。
struct WaveGuillotineBase : public IFormation
{
    /// この刃の「波内位置」(0=最初に倒れる, 1=最後)。派生が波の向きを決める。
    virtual float WaveT(int bladeIndex, int numBlades) const = 0;

    void Calculate(DroneComponent &drone, const FormationContext &ctx) override
    {
        using namespace DirectX::SimpleMath;
        drone.chargeMode = ChargeMode::RigidFollow;

        const Vector3 dir = ctx.AimDirection();
        Vector3 right = Vector3::UnitY.Cross(dir);
        if (right.LengthSquared() < 0.001f) right = Vector3::UnitX;
        right.Normalize();

        // 刃＝縦の列。localIndex を (bladeIndex 横位置, heightIdx 高さ) へ割付。
        const int total       = (drone.totalDrones > 0) ? drone.totalDrones : 1;
        const int bladeHeight = 3;
        const int numBlades   = (std::max)(1, total / bladeHeight);
        int bladeIndex = drone.localIndex / bladeHeight;
        if (bladeIndex >= numBlades) bladeIndex = numBlades - 1;
        const int heightIdx = drone.localIndex % bladeHeight;

        const float bladeSpacing  = 2.5f; // 刃と刃の横間隔
        const float heightSpacing = 2.0f; // 刃の高さ方向
        const float hingeHeight   = 2.0f; // 回転軸の高さ
        const float forward       = 5.0f; // ボス前方へのオフセット

        if (drone.wantsAttack)
            drone.attackProgress = (std::min)(1.0f, drone.attackProgress + ctx.dt * kWaveSpeed);
        else
            drone.attackProgress = 0.0f;

        const float waveT       = WaveT(bladeIndex, numBlades);
        const float fallDurFrac = 0.35f;                                // 1枚の刃が倒れるのにかける進行度の割合
        const float fallStart   = waveT * (1.0f - fallDurFrac);         // 波として倒れ始めをずらす
        const float fallP       = std::clamp((drone.attackProgress - fallStart) / fallDurFrac, 0.0f, 1.0f);
        const float theta       = (fallP * fallP) * kThetaMax;          // easeIn で倒れる

        const float bladeX = (bladeIndex - (numBlades - 1) * 0.5f) * bladeSpacing;
        const float height = heightIdx * heightSpacing;

        const Vector3 hingeBase = ctx.bossPos + dir * forward + right * bladeX + Vector3::UnitY * hingeHeight;
        const Vector3 rotatedUp = Vector3::UnitY * (height * std::cos(theta)) + dir * (height * std::sin(theta));
        Vector3       pos       = hingeBase + rotatedUp;

        // 方向tell：最初に倒れる刃だけ forming 中に上下へ震える。
        if (drone.attackProgress <= 0.0f && waveT < 0.18f)
        {
            const float sh = std::sin(ctx.time * 12.0f + static_cast<float>(drone.localIndex)) * 0.5f;
            pos += Vector3::UnitY * sh;
        }

        drone.targetPosition = pos;
        Vector3 look = Vector3::UnitY * (-std::sin(theta)) + dir * std::cos(theta);
        if (look.LengthSquared() < 0.001f) look = dir;
        look.Normalize();
        drone.fireDirection       = look;
        drone.targetLookDirection = look;
    }

    static constexpr float kWaveSpeed = 1.0f;  // 波全体の進行速度
    static constexpr float kThetaMax  = 1.9f;  // 各刃の最大倒れ角
};
struct WaveGuillotineLtoR : public WaveGuillotineBase { float WaveT(int i, int n) const override { return (n <= 1) ? 0.0f : static_cast<float>(i) / (n - 1); } };
struct WaveGuillotineRtoL : public WaveGuillotineBase { float WaveT(int i, int n) const override { return (n <= 1) ? 0.0f : 1.0f - static_cast<float>(i) / (n - 1); } };

// 登録（DroneFormationSystem::Initialize から呼ぶ）。
inline void RegisterAll()
{
    auto &reg = FormationRegistry::Instance();
    reg.Register<GuillotineSlamFormation>(DroneFormationType::GuillotineSlam);
    reg.Register<ClapSlamFormation>(DroneFormationType::ClapSlam);
    reg.Register<KamikazeSwarmFormation>(DroneFormationType::KamikazeSwarm);
    reg.Register<MegaClapUpperFirstFormation>(DroneFormationType::MegaClapSlam_UpperFirst);
    reg.Register<MegaClapLowerFirstFormation>(DroneFormationType::MegaClapSlam_LowerFirst);
    reg.Register<SideSlapWhipFormation>(DroneFormationType::SideSlapWhip);
    reg.Register<WaveGuillotineLtoR>(DroneFormationType::TsunamiWave);     // 左→右
    reg.Register<WaveGuillotineRtoL>(DroneFormationType::TsunamiWave_RL);  // 右→左
}

} // namespace CCL::AI::Formation::Attack
