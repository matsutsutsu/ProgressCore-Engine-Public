// 攻撃系の陣形クラス（予備動作フェーズで「形」を作り、fireDirection を仕込む）。
//        Formations_Basic.h（周回・待機系）と対に、攻撃技をここへ1クラスずつ追加する。
//
// 攻撃陣形の責務:
//   - 予備動作フェーズ（currentState != FireCharge）中に targetPosition で「形」を作る。
//     プレイヤーはこの形から回避方向を読み取る。これがゲームの中心的な体験にあたる。
//   - 同時に fireDirection（突撃方向）を仕込んでおく。FireCharge へ移った瞬間、
//     DroneMovementSystem が fireDirection に沿って「タメ→リリース」を行う。
//   - FireCharge 中は座標・方向を凍結し、コミットした直進を保証する
//     （ホーミングさせない＝ジャスト回避で抜けられる）。
#pragma once
#include "Game/Logics/AI/Drone/Formation/IFormation.h"
#include "Game/Logics/AI/Drone/Formation/FormationRegistry.h"
#include "Game/Logics/AI/Drone/Formation/Formations_Basic.h" // 弾幕が頭上リングを借りる
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

        // --- 狙う向きと、それに直交する「刃の幅」方向 ---
        //   playerPos を直接見ないこと。AimDirection() は「撃つと決めた瞬間」の
        //   向きを返すので、倒れている最中にプレイヤーが横へ動いても刃は追わない。
        const Vector3 dir = ctx.AimDirection();

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

        // 回転軸（下端の中心）：ボス頭上のやや低い位置を、プレイヤー側へ hingeForward だけ出す。
        //
        // @note hingeForward は宣言だけされていて式に入っていなかった。そのため刃の回転軸が
        //       ボスの真上に来ており、壁がボスを跨いで生え、倒れ始めがボスの足元から
        //       始まっていた（＝予備動作の「形」がボスと重なって読めない）。
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

    // --- 調整値（グレーボックスで実測して詰める）---
    static constexpr float kSwingSpeed = 2.5f;               // attackProgress の増加速度（1/秒）→ 倒れ切りまで ≈0.4s
    static constexpr float kThetaMax   = 1.9f;               // 最大回転角（rad）≈109°（水平を少し越えて叩きつける）
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

        // 挟み込みの中心軸。腕はこの線上へ閉じる。
        //   閉じ始めてからは追わない（AimDirection が撃つ瞬間の向きを返す）。
        const Vector3 centerDir = ctx.AimDirection();

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
//   頭上の輪を回したまま、1機ずつ輪の中心へ降りてきて、そこからプレイヤーへ
//   高速で撃ち出す。プレイヤーはジャスト回避で凌ぐ。要求回避＝ジャスト回避。
//
// 1発ぶんの流れ（すべて自分の fireDelay を時計にしている）:
//   ┌ 輪の上で待つ（回転し続ける）
//   ├ 自分の番の kGatherTime 秒前：輪から中心＋やや下へ寄っていく ← これが予告
//   └ fireDelay が 0：撃つ方向へ加速して発射（以降は直進）
//
// なぜ震えないのか（UsesShiverTelegraph = false）:
//   縦割りのように「形は完成していて、あとは振り下ろすだけ」の技は止まって
//   見えるので、震えが唯一の予告になる。こちらは中心へ集まる移動そのものが
//   予告なので、震えを足すと予告が二重になって、どちらを読めばよいのか濁る。
//
// なぜ狙いを固定しないのか（ctx.AimDirection を使わない）:
//   縦割り・挟み込みは「撃つと決めた瞬間の向きへコミットする置き技」で、
//   固定しないと横へ回り込んでも刃が追ってきてしまう。
//   弾幕は逆に「1発ずつ今のあなたを狙って撃つ」技にしたい。だから各機は
//   発射する瞬間まで ctx.playerPos を毎フレーム見る。撃ち出した後だけは
//   直進（＝ホーミングしない）なので、避ける余地はそこで担保される。
//
// 「順番に」の作り方:
//   forming 中に localIndex から fireDelay を割り振る（先頭ほど早く撃つ）。
//   全機が同時に FireCharge へ入っても、ProcessFireCharge が fireDelay の間は
//   陣形の指示に従わせるため、結果として順次発射になる。
//   fireDelay = (localIndex / total) * kFireWindow で、機数に依らず常に
//   kFireWindow 秒で撃ち切る（1000機でも一定時間で発射）。
//   ＝ kFireWindow を伸ばすほど発射レートが下がる。
// ====================================================================
struct KamikazeSwarmFormation : public IFormation
{
    // 撃ち出したら各機が独立して飛ぶ技なので、終了判断は個体に持たせる。
    // （飛び切った機から順に離脱→隊列へ戻る＝「帰投がバラバラ」になる）
    ChargeLifecycle GetLifecycle() const override { return ChargeLifecycle::Individual; }

    // 中心へ集まる移動そのものが予告なので、震えは出さない（上のコメント参照）。
    bool UsesShiverTelegraph() const override { return false; }

    void Calculate(DroneComponent &drone, const FormationContext &ctx) override
    {
        using namespace DirectX::SimpleMath;

        // --- 陣形が手を引くのは「飛び出した後」だけ ---
        //
        //   順番待ち(fireDelay>0)も、中央でのタメ(stateTimer>0)も、まだ撃っていない。
        //   その間は陣形が動かし続け、狙いも更新し続ける。
        //
        //   stateTimer まで見るのが要点:
        //     以前は fireDelay が 0 になった時点で手を引いていた。そこから
        //     タメ 0.4 秒＋飛行があるので、着弾の 0.5 秒以上前の座標を撃っていた。
        //     プレイヤーの歩行速度は 40m/s 級なので 20m 以上後ろへ外れる
        //     ＝普通に歩いているだけで当たらない、という状態だった。
        //
        //   ※ Returning（離脱中）はあえて凍結しない。帰る先の待機リングを
        //     更新し続け、離脱が終わった機がそのまま隊列へ吸い込まれるようにするため。
        const bool launched = (drone.currentState == DroneState::FireCharge) &&
                              (drone.fireDelay <= 0.0f) && (drone.stateTimer <= 0.0f);
        if (launched) return;

        drone.chargeMode = ChargeMode::Straight; // dashモード（fireDirection直進）

        const int total = (drone.totalDrones > 0) ? drone.totalDrones : 1;

        // --- 待機位置：頭上円陣(HighOrbit)と同じ輪。回転位相まで共有する ---
        //   同じ式を通すので、BT が 頭上円陣 → 弾幕 と切り替えてもドローンは動かない。
        const Vector3 slot = ctx.bossPos + Basic::HighRing::SlotOffset(
                                               drone.localIndex, drone.totalDrones, ctx.time);

        // --- 発射地点：輪の中心の、少し下 ---
        const Vector3 muzzle =
            ctx.bossPos + Vector3::UnitY * (Basic::HighRing::kHeight - kGatherDrop);

        // --- 自分の番が近づくほど中心へ寄る ---
        //   fireDelay は残り時間なので、kGatherTime を切ってから寄り始める。
        //   二乗で寄せるのは、序盤をほぼ動かさず「直前にすっと吸い込まれる」
        //   形にするため。線形だと最初からじわじわ動いて、いつ撃つのか読めない。
        float gather = 0.0f;
        if (drone.currentState == DroneState::FireCharge && drone.fireDelay <= 0.0f)
        {
            // 中央でのタメ中。持ち場は発射地点のまま。
            //   ここを輪へ戻すと、下の飛翔時間の計算が「輪から撃つ」前提になり、
            //   実際の発射地点（中央）とずれた先読みになる。
            gather = 1.0f;
        }
        else if (drone.fireDelay > 0.0f && drone.fireDelay < kGatherTime)
        {
            const float t = 1.0f - (drone.fireDelay / kGatherTime);
            gather        = t * t;
        }

        drone.targetPosition = Vector3::Lerp(slot, muzzle, gather);

        // --- 狙い：着弾までにプレイヤーが動く先を読む（偏差射撃）---
        //
        //   今いる場所を撃つと必ず後ろへ外れる。飛翔時間があるからで、
        //   これは速度をいくら上げても消えない（速くすれば減るだけ）。
        //   狙う先 ＝ 今の位置 ＋ 速度 × 着弾までの時間。
        //
        //   着弾までの時間 ＝ 中央でのタメの残り ＋ 飛翔時間(距離÷射出速度)。
        //   距離は狙う先までなので、本当は自分自身を参照する式になる。
        //   1回だけ計算し直して精度を上げる（2回目以降はほとんど動かない）。
        //
        //   kLeadFactor で読みの強さを決める。
        //     0.0 … 先読みなし（今の実装＝ほぼ当たらない）
        //     1.0 … 完全な迎撃。等速で動き続ける限り必ず当たる
        //   撃った後は直進するので、飛んでくるのを見てから動きを変えれば外れる。
        //   ＝「歩いているだけでは避けられないが、避ければ避けられる」に寄せる。
        const float launchSpeed = drone.moveSpeed * kDroneChargeSpeedScale;
        const float windupLeft  = (drone.stateTimer > 0.0f) ? drone.stateTimer : 0.0f;

        //   狙うのは足元(playerPos)ではなく体の中心(playerCenter)。
        //   足元は地面なので、そこへ向けると身長ぶん下を撃って地面に刺さる。
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

        // --- 発射順（先頭ほど早い）---
        //   撃つ合図が出た後に書き直すと、待っている間ずっと順番がリセットされて
        //   永久に撃てなくなる。まだ攻撃に入っていない間だけ割り振る。
        if (drone.currentState != DroneState::FireCharge)
        {
            drone.fireDelay =
                (static_cast<float>(drone.localIndex) / static_cast<float>(total)) * kFireWindow;
        }
    }

    // --- 調整値（グレーボックスで実測して詰める）---
    static constexpr float kFireWindow = 8.0f; // 全機を撃ち切るのにかける秒数。伸ばすほど発射レートが下がる
    static constexpr float kGatherTime = 0.6f; // 発射の何秒前から中心へ寄り始めるか
    static constexpr float kGatherDrop = 6.0f; // 輪の高さから何m下を発射地点にするか

    // 先読みの強さ。0=今の位置を撃つ / 1=完全な迎撃。
    //   1.0 のままだと等速移動には必ず当たるが、そこが狙いでよい。
    //   狙いは発射の瞬間に凍るので、飛んできてから動きを変えれば抜けられる。
    //   「歩くだけで避けられる」を潰しつつ「避ければ避けられる」を残す値。
    static constexpr float kLeadFactor = 0.9f;
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
    // 上段が先に閉じるか（派生で指定）。
    virtual bool UpperFirst() const = 0;

    void Calculate(DroneComponent &drone, const FormationContext &ctx) override
    {
        using namespace DirectX::SimpleMath;

        drone.chargeMode = ChargeMode::RigidFollow; // 壁が形を保ってスライド（follow-target）

        const Vector3 dir = ctx.AimDirection();
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
    // この刃の「波内位置」(0=最初に倒れる, 1=最後)。派生が向きを決める。
    virtual float WaveT(int bladeIndex, int numBlades) const = 0;

    void Calculate(DroneComponent &drone, const FormationContext &ctx) override
    {
        using namespace DirectX::SimpleMath;
        drone.chargeMode = ChargeMode::RigidFollow;

        const Vector3 dir = ctx.AimDirection();
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
