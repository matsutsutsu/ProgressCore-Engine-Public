/**
 * 攻撃系の陣形クラス（予備動作フェーズで「形」を作り、fireDirection を仕込む）。
 *        Formations_Basic.h（周回・待機系）と対に、攻撃技をここへ1クラスずつ追加する。
 *
 * 攻撃陣形の責務:
 *   - 予備動作フェーズ（currentState != FireCharge）中に targetPosition で「形」を作る。
 *     プレイヤーはこの形から回避方向を読み取る。これがゲームの中心的な体験にあたる。
 *   - 同時に fireDirection（突撃方向）を仕込んでおく。FireCharge へ移った瞬間、
 *     DroneMovementSystem が fireDirection に沿って「タメ→リリース」を行う。
 *   - FireCharge 中は座標・方向を凍結し、コミットした直進を保証する
 *     （ホーミングさせない＝ジャスト回避で抜けられる）。
 */
#pragma once
#include "Game/Logics/AI/Drone/Formation/IFormation.h"
#include "Game/Logics/AI/Drone/Formation/FormationRegistry.h"
#include <cmath>
#include <algorithm>

namespace CCL::AI::Formation::Attack
{
// ====================================================================
// 縦割り（GuillotineSlam）
//   ボス頭上に「縦の刃（壁）」を立て、下端を回転軸に、橋が倒れるように
//   プレイヤー側へ振り下ろす。要求回避＝横回避。
//
// 動きの核心＝ヒンジ回転（剛体）:
//   各機の "刃ローカル座標" は固定（下端からの高さ height と、幅方向 side）。
//   刃全体を下端の軸まわりに角度 θ 回転させる。
//     θ=0   : 直立（up 方向）        ← 予備動作（読ませる形）
//     θ=90° : 前方へ水平（dir 方向）  ← 倒れきった状態
//   回転後の高さベクトル = up*height*cos(θ) + dir*height*sin(θ)
//   → 下端(height=0)は θ に依らず不動＝軸。上段ほど大きく前へ倒れる。
//
//   θ は attackProgress(0→1) から求める。倒れは重力的に加速させたいので easeIn。
//   移動は ChargeMode::RigidFollow で「targetPosition を剛体追従」させる
//   （fireDirection 直進ではなく、壁が形を保ったまま回る）。
// ====================================================================
struct GuillotineSlamFormation : public IFormation
{
    void Calculate(DroneComponent &drone, const FormationContext &ctx) override
    {
        using namespace DirectX::SimpleMath;

        // この攻撃は「陣形自体が動く」ので、突撃中も targetPosition を更新し続ける。
        // 移動側にはそれを剛体追従させる（直進ではなく回転）。
        drone.chargeMode = ChargeMode::RigidFollow;

        // --- ボス→プレイヤーの水平方向と、それに直交する「刃の幅」方向 ---
        Vector3 dir = ctx.playerPos - ctx.bossPos;
        dir.y = 0.0f;
        if (dir.LengthSquared() < 0.001f) dir = Vector3::UnitZ;
        dir.Normalize();

        Vector3 right = Vector3::UnitY.Cross(dir);
        if (right.LengthSquared() < 0.001f) right = Vector3::UnitX;
        right.Normalize();

        // --- localIndex を (col, row) の格子へ割り付けて「壁」を作る ---
        const int total = (drone.totalDrones > 0) ? drone.totalDrones : 1;
        const int cols  = (std::max)(1, static_cast<int>(std::floor(std::sqrt(static_cast<float>(total)))));
        const int col   = drone.localIndex % cols;
        const int row   = drone.localIndex / cols;

        const float spacing      = 2.0f; // 刃の目の細かさ
        const float hingeHeight  = 2.0f; // 回転軸（下端）の高さ
        const float hingeForward = 5.0f; // ボスからプレイヤー側へ離す距離（押し出し回避＋間合い）

        // --- 攻撃の進行度 θ を更新する ---
        //   wantsAttack 中だけ進め、非攻撃時は 0 に戻して直立を保つ。
        if (drone.wantsAttack)
            drone.attackProgress = (std::min)(1.0f, drone.attackProgress + ctx.dt * kSwingSpeed);
        else
            drone.attackProgress = 0.0f;

        const float eased = drone.attackProgress * drone.attackProgress; // easeIn（倒れが加速）
        const float theta = eased * kThetaMax;                            // 0 → kThetaMax
        const float c = std::cos(theta);
        const float s = std::sin(theta);

        // --- 刃ローカル：下端からの高さ height と、幅方向オフセット side ---
        const float height = row * spacing;                       // 下端(row0)=0＝軸
        const float side   = (col - (cols - 1) * 0.5f) * spacing; // 幅方向は中央揃え

        // 回転軸（下端の中心）：ボス頭上のやや低い位置
        const Vector3 hingeBase = ctx.bossPos + Vector3::UnitY * hingeHeight;

        // 高さベクトルを下端軸まわりに θ 回転（up → dir へ倒す）
        const Vector3 rotatedUp = Vector3::UnitY * (height * c) + dir * (height * s);

        drone.targetPosition = hingeBase + right * side + rotatedUp;

        // 地面へのめり込み防止：水平を越えて振り下ろすと刃先が地面下へ潜るため、
        // ボス足元(地面)から kGroundClearance より下へは行かせない（刃が地面で止まる）。
        const float floorY = ctx.bossPos.y + kGroundClearance;
        if (drone.targetPosition.y < floorY) drone.targetPosition.y = floorY;

        // 向き：倒れていく接線方向（-up*sin + dir*cos）を向かせる
        Vector3 look = Vector3::UnitY * (-s) + dir * c;
        if (look.LengthSquared() < 0.001f) look = dir;
        look.Normalize();
        drone.fireDirection       = look;
        drone.targetLookDirection = look;
    }

    // --- 調整値（グレーボックスで実測して詰める）---
    static constexpr float kSwingSpeed = 2.5f;               // attackProgress の増加速度（1/秒）→ 倒れ切りまで ≈0.4s
    static constexpr float kThetaMax   = 1.9f;               // 最大回転角（rad）≈109°（水平を少し越えて叩きつける）
    static constexpr float kGroundClearance = 0.5f;          // 地面クランプ高さ（ドローン半径ぶん浮かせて埋没を防ぐ）
};

// ====================================================================
// 挟み込み1段（ClapSlam）
//   ボスの「前方」に左右2本の腕（低い壁）を張り出し、真横に開いた状態から
//   前方中央（プレイヤー方向の軸）へ振り込んでバチンと挟む。壁は低いので
//   ジャンプで越えて避ける。要求回避＝ジャンプ。
//
// 設計変更（前作ソースを参照して調整）:
//   旧実装は壁の奥行きをボス→プレイヤー軸(dir)に沿って伸ばしていたため、
//   機数が増えると壁がボスの「前後」に長い回廊になり、ボスを取り巻いて見えた。
//   本実装は前作方式に合わせ、各機を「プレイヤー方向を軸に回転させた腕方向 armDir」
//   × 距離 で配置する。腕は常にボスの前方に張り出し、角度を 横→中央 へ閉じる。
//   前後には伸ばさない。
//
// 動きの核心＝左右の腕が前方中央へ振れて閉じる（剛体スライド）:
//   localIndex 偶数＝右腕／奇数＝左腕。腕内の番号 k を (距離セグメント × 高さ) に割付。
//   進行度 attackProgress(0→1) で腕の角度を kStartAngle(真横)→kEndAngle(前方中央) へ。
//   ChargeMode::RigidFollow で targetPosition を剛体追従させる。
// ====================================================================
struct ClapSlamFormation : public IFormation
{
    void Calculate(DroneComponent &drone, const FormationContext &ctx) override
    {
        using namespace DirectX::SimpleMath;

        drone.chargeMode = ChargeMode::RigidFollow; // 腕が形を保ったまま閉じる（剛体スライド）

        // ボス→プレイヤーの水平方向＝挟み込みの中心軸。腕はこの線上へ閉じる。
        Vector3 centerDir = ctx.playerPos - ctx.bossPos;
        centerDir.y = 0.0f;
        if (centerDir.LengthSquared() < 0.001f) centerDir = Vector3::UnitZ;
        centerDir.Normalize();

        // --- 左右どちらの腕か → 腕内番号 k を (距離セグメント × 高さ) へ割り付ける ---
        const bool isRight = (drone.localIndex % 2 == 0);
        const int  k       = drone.localIndex / 2;
        const int  hRow    = k % kHeightRows;         // 高さ（低い壁＝ジャンプで越える）
        const int  seg     = k / kHeightRows;         // 腕の先端方向（前方へ伸びる）

        const float distance = kBaseDistance + seg  * kDistanceStep;
        const float height   = kBaseHeight   + hRow * kHeightSpacing;

        // --- 進行度で腕の角度を「真横(±kStartAngle)→前方中央(±kEndAngle)」へ閉じる ---
        if (drone.wantsAttack)
            drone.attackProgress = (std::min)(1.0f, drone.attackProgress + ctx.dt * kClapSpeed);
        else
            drone.attackProgress = 0.0f;

        const float eased = drone.attackProgress * drone.attackProgress; // easeIn（バチンと閉じる）
        const float sign  = isRight ? 1.0f : -1.0f;
        const float angle = sign * (kStartAngle + (kEndAngle - kStartAngle) * eased);

        // centerDir を Y 軸まわりに angle 回転＝この機の「腕方向」。常にボス前方に張り出す。
        const Quaternion swing  = Quaternion::CreateFromAxisAngle(Vector3::UnitY, angle);
        const Vector3    armDir  = Vector3::Transform(centerDir, swing);

        drone.targetPosition = ctx.bossPos + armDir * distance + Vector3::UnitY * height;

        // 向き：中心軸（相手方向）を向かせる＝内向き
        Vector3 look = ctx.playerPos - drone.targetPosition;
        look.y = 0.0f;
        if (look.LengthSquared() < 0.001f) look = centerDir;
        look.Normalize();
        drone.fireDirection       = look;
        drone.targetLookDirection = look;
    }

    // --- 調整値（グレーボックスで実測して詰める）---
    static constexpr float kClapSpeed     = 3.0f;   // 閉じ速度（1/秒）→ 閉じ切り ≈0.33s
    static constexpr int   kHeightRows    = 2;      // 低い壁（ジャンプで越えられる）
    static constexpr float kBaseDistance  = 3.0f;   // 最内の腕のボスからの距離
    static constexpr float kDistanceStep  = 1.2f;   // 腕が前方へ伸びる間隔
    static constexpr float kBaseHeight    = 0.8f;   // 壁の下端（地面付近）
    static constexpr float kHeightSpacing = 1.5f;   // 高さ段の間隔（低め）
    static constexpr float kStartAngle    = 1.5f;   // 開始角(rad)≈86°（ほぼ真横に開く）
    static constexpr float kEndAngle      = 0.08f;  // 閉じ切り角(rad)≈5°（前方中央でほぼ接触）
};

// ====================================================================
// 弾幕（KamikazeSwarm）※初めての dashモード攻撃
//   ボス頭上にドローンを集め、プレイヤーへ向かって「順番に」高速で撃ち出す。
//   プレイヤーはジャスト回避で凌ぐ。要求回避＝ジャスト回避。
//
// 縦割り／挟み込みとの違い＝移動モードが逆:
//   これらは ChargeMode::Straight のまま＝各機が fireDirection へ独立に直進する
//   「弾（fire-and-forget）」。DroneMovementSystem の「タメ→リリース」がそのまま弾になる。
//
// 「順番に」の作り方:
//   forming 中に localIndex から fireDelay を割り振る（先頭ほど早く撃つ）。
//   全機が同時に FireCharge へ入っても、ProcessFireCharge が fireDelay の間その場で
//   待機させるため、結果として順次発射になる。
//   fireDelay = (localIndex / total) * kFireWindow で、機数に依らず常に kFireWindow 秒で
//   撃ち切る（1000機でも一定時間で発射）。
// ====================================================================
struct KamikazeSwarmFormation : public IFormation
{
    /// 撃ち出したら各機が独立して飛ぶ技なので、終了判断は個体に持たせる。
    /// （飛び切った機から順に離脱→隊列へ戻る＝「帰投がバラバラ」になる）
    ChargeLifecycle GetLifecycle() const override { return ChargeLifecycle::Individual; }

    void Calculate(DroneComponent &drone, const FormationContext &ctx) override
    {
        using namespace DirectX::SimpleMath;

        // 発射後は各機が独立して飛ぶので、撃ち出したら陣形は関与しない（凍結）。
        //   ※ Returning（離脱中）はあえて凍結しない。帰る先の待機リングを更新し続け、
        //     離脱が終わった機がそのまま隊列へ吸い込まれるようにするため。
        if (drone.currentState == DroneState::FireCharge)
            return;

        drone.chargeMode = ChargeMode::Straight; // dashモード（fireDirection直進）

        Vector3 dir = ctx.playerPos - ctx.bossPos;
        dir.y = 0.0f;
        if (dir.LengthSquared() < 0.001f) dir = Vector3::UnitZ;
        dir.Normalize();

        // --- forming：ボス頭上に「弾の供給リング」を作って待機させる ---
        const int   total = (drone.totalDrones > 0) ? drone.totalDrones : 1;
        const float angle = (2.0f * 3.14159265f) * (static_cast<float>(drone.localIndex) / static_cast<float>(total));
        const float ringR = 3.5f;
        const float ringH = 7.0f; // ボス頭上

        Vector3 pos = ctx.bossPos + Vector3::UnitY * ringH
                    + Vector3(std::cos(angle) * ringR, 0.0f, std::sin(angle) * ringR);
        drone.targetPosition = pos;

        // 発射方向：待機位置からプレイヤーへ（発射時にこの方向でロックされる）
        Vector3 toPlayer = ctx.playerPos - pos;
        if (toPlayer.LengthSquared() > 0.001f) toPlayer.Normalize();
        else                                   toPlayer = dir;
        drone.fireDirection       = toPlayer;
        drone.targetLookDirection = toPlayer;

        // 発射順（先頭ほど早い）。機数に依らず kFireWindow 秒で撃ち切る。
        drone.fireDelay = (static_cast<float>(drone.localIndex) / static_cast<float>(total)) * kFireWindow;
    }

    // --- 調整値（グレーボックスで実測して詰める）---
    static constexpr float kFireWindow = 1.8f; // 全機を撃ち切るのにかける秒数（順次発射の総時間）
};

// ====================================================================
// 挟み込み2段（MegaClapSlam）※「形を読む」を最も強く要求する技
//   挟み込み1段の上下2段版。上下どちらの段が先に閉じるかを、予備動作の
//   「震え(shiver)」で読ませる。プレイヤーはジャンプのタイミングを読み替える:
//     下段が先 → 下段をジャンプで避け、着地してから上段をやり過ごす
//     上段が先 → 接地のままやり過ごし、後から来る下段をジャンプ
//
//   実装＝ClapSlam(左右の壁が寄る follow-target)の2段版:
//     4グループ =(下段/上段)×(左/右)。attackProgress を2フェーズに割り、
//     先段は 0.0→0.5、後段は 0.5→1.0 で gap を閉じる。
//     「先に閉じる段」だけ forming 中に左右へ震える＝どちらが先かの tell。
//   上下で先攻が違う2種を用意（UpperFirst / LowerFirst）。ボスはランダムに選ぶ想定。
// ====================================================================
struct MegaClapSlamBase : public IFormation
{
    /// 上段が先に閉じるか（派生で指定）。
    virtual bool UpperFirst() const = 0;

    void Calculate(DroneComponent &drone, const FormationContext &ctx) override
    {
        using namespace DirectX::SimpleMath;

        drone.chargeMode = ChargeMode::RigidFollow; // 壁が形を保ってスライド（follow-target）

        Vector3 dir = ctx.playerPos - ctx.bossPos;
        dir.y = 0.0f;
        if (dir.LengthSquared() < 0.001f) dir = Vector3::UnitZ;
        dir.Normalize();
        Vector3 right = Vector3::UnitY.Cross(dir);
        if (right.LengthSquared() < 0.001f) right = Vector3::UnitX;
        right.Normalize();

        // --- 4グループへ割付：g=0下段左 / 1下段右 / 2上段左 / 3上段右 ---
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

        // --- 進行度を進める（2フェーズ共通の時間軸）---
        if (drone.wantsAttack)
            drone.attackProgress = (std::min)(1.0f, drone.attackProgress + ctx.dt * kSpeed);
        else
            drone.attackProgress = 0.0f;

        // この段が「先に閉じる段」か → 担当フェーズを決める
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

    // --- 調整値（グレーボックスで実測して詰める）---
    //   特に lowerBase/upperBase の高さは「接地で下段に当たり・ジャンプ頂点で上段に当たる」
    //     関係になるよう、プレイヤーのジャンプ高さと合わせて実測して決める。
    static constexpr float kSpeed      = 1.2f;  // 2フェーズ合計の速度 → 全閉まで ≈0.83s
    static constexpr float kStartGap   = 12.0f; // 開始の左右間隔
    static constexpr float kEndGap     = 1.2f;  // 閉じ切りの左右間隔
    static constexpr float kShiverFreq = 10.0f; // tell の震えの速さ（追従で消えない程度に低め）
    static constexpr float kShiverAmp  = 0.6f;  // tell の震えの幅
};

struct MegaClapUpperFirstFormation : public MegaClapSlamBase { bool UpperFirst() const override { return true;  } };
struct MegaClapLowerFirstFormation : public MegaClapSlamBase { bool UpperFirst() const override { return false; } };

// ====================================================================
// 横なぎ回転（SideSlapWhip）※ジャスト回避を誘う技
//   ボスの片側に「横長・縦に少し幅」の腕（ドローンの長方形配列）を作り、
//   ボス中心の縦軸まわりに回転して正面を横へ薙ぎ払う。
//   回転と逆に走れば無効化できてしまうため、払いにダッシュを合わせる
//   ＝ジャスト回避を誘う設計（回避成立で無敵通過＋接近）。
//
//   実装＝縦軸まわりの剛体回転（follow-target）:
//     各機は腕上の (半径 col, 高さ row) に固定。腕全体の角度 a を進行度で回す。
//       a = kStartAngle + kSweepAngle * easeIn(progress)   （右後方→前→左後方）
//     armDir(a) = dir*cos(a) + right*sin(a)   （dir=ボス→プレイヤー）
// ====================================================================
struct SideSlapWhipFormation : public IFormation
{
    void Calculate(DroneComponent &drone, const FormationContext &ctx) override
    {
        using namespace DirectX::SimpleMath;
        drone.chargeMode = ChargeMode::RigidFollow;

        Vector3 dir = ctx.playerPos - ctx.bossPos;
        dir.y = 0.0f;
        if (dir.LengthSquared() < 0.001f) dir = Vector3::UnitZ;
        dir.Normalize();
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

// ====================================================================
// 波状縦割り（TsunamiWave）※横方向の読み合い（挟み込み2段と対になる技）
//   縦割りの刃を横方向に複数並べ、波のように順次倒す。プレイヤーは波に合わせて
//   横へ走り続ける。どちら向きに波が来るかを予備動作の「震え」で読ませる。
//
//   実装＝複数刃のヒンジ倒れ（follow-target）を横位置で時間差発火:
//     bladeIndex(横位置)ごとに WaveT(0..1)を決め、fallStart = WaveT*(1-幅) で
//     倒れ始めをずらす。leading（WaveT小）の刃だけ forming 中に上下へ震える＝方向tell。
//   左→右(LtoR)と右→左(RtoL)の2種を用意（ボスがランダムに選ぶ想定）。
// ====================================================================
struct WaveGuillotineBase : public IFormation
{
    /// この刃の「波内位置」(0=最初に倒れる, 1=最後)。派生が向きを決める。
    virtual float WaveT(int bladeIndex, int numBlades) const = 0;

    void Calculate(DroneComponent &drone, const FormationContext &ctx) override
    {
        using namespace DirectX::SimpleMath;
        drone.chargeMode = ChargeMode::RigidFollow;

        Vector3 dir = ctx.playerPos - ctx.bossPos;
        dir.y = 0.0f;
        if (dir.LengthSquared() < 0.001f) dir = Vector3::UnitZ;
        dir.Normalize();
        Vector3 right = Vector3::UnitY.Cross(dir);
        if (right.LengthSquared() < 0.001f) right = Vector3::UnitX;
        right.Normalize();

        // 刃＝縦の列。localIndex を (bladeIndex 横位置, heightIdx 高さ) へ割付
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

        // 方向tell：最初に倒れる刃(waveT小)だけ forming 中に上下へ震える
        if (drone.attackProgress <= 0.0f && waveT < 0.18f)
        {
            const float sh = std::sin(ctx.time * 12.0f + static_cast<float>(drone.localIndex)) * 0.5f;
            pos += Vector3::UnitY * sh;
        }

        // 地面へのめり込み防止：水平を越えて倒れると刃先が地面下へ潜るため、
        // ボス足元(地面)から kGroundClearance より下へは行かせない。
        const float floorY = ctx.bossPos.y + kGroundClearance;
        if (pos.y < floorY) pos.y = floorY;

        drone.targetPosition = pos;
        Vector3 look = Vector3::UnitY * (-std::sin(theta)) + dir * std::cos(theta);
        if (look.LengthSquared() < 0.001f) look = dir;
        look.Normalize();
        drone.fireDirection       = look;
        drone.targetLookDirection = look;
    }

    static constexpr float kWaveSpeed = 1.0f;  // 波全体の進行速度
    static constexpr float kThetaMax  = 1.9f;  // 各刃の最大倒れ角
    static constexpr float kGroundClearance = 0.5f; // 地面クランプ高さ（埋没防止）
};
struct WaveGuillotineLtoR : public WaveGuillotineBase { float WaveT(int i, int n) const override { return (n <= 1) ? 0.0f : static_cast<float>(i) / (n - 1); } };
struct WaveGuillotineRtoL : public WaveGuillotineBase { float WaveT(int i, int n) const override { return (n <= 1) ? 0.0f : 1.0f - static_cast<float>(i) / (n - 1); } };

// ====================================================================
// 登録（DroneFormationSystem::Initialize から呼ぶ）
// ====================================================================
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
