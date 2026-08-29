/// @file  Formations_DataDriven.h
/// @brief FormationSpec（JSON）を解釈して targetPosition を作る陣形。
///        Formations_Attack.h の各クラスが「1クラス=1つの技」なのに対し、
///        こちらは「1インスタンス=1つのアセット」になる。
///
/// 既存設計を壊さない理由:
///   IFormation の派生が1つ増えるだけ。FormationRegistry も
///   DroneFormationSystem のループも従来どおりで、C++ の陣形クラスと同居できる。
///   同じ DroneFormationType に両方を登録しておき、実行中に切り替えて
///   見た目が変わらないかを確かめられる（＝データ化が正しいかの検証手段）。
///
/// 対象外（意図的）:
///   個体が状態をラッチする技（KamikazeSwarm の個別帰投、SquadAssault の追従）は
///   attackProgress の関数として書けないので C++ に残す。FormationSpec.h の
///   ファイルヘッダに線引きの理由を書いてある。
#pragma once
#include <cmath>

#include "Game/Logics/AI/Drone/Formation/FormationSpec.h"
#include "Game/Logics/AI/Drone/Formation/IFormation.h"

namespace CCL::AI::Formation
{

/// @brief FormationSpec を毎フレーム評価する陣形。
///
/// 仮想呼び出しは「陣形の種類ごとに1回」のまま（DroneFormationSystem がループを回す）
/// なので、データ化してもドローン1機ごとの間接呼び出しは増えない。
struct DataDrivenFormation : public IFormation
{
    FormationSpec spec;

    /// @brief 元アセットのパス。エディタからの保存・再読み込みに使う。
    std::string assetPath;

    /// @brief localIndex から割り当てた席。
    ///
    /// エディタが色分けと機数の内訳表示に使うので公開している。
    /// 同じ割付をエディタ側で書き直すと、片方だけ直したときに絵と実機がずれる。
    struct Seat
    {
        int side = 0; ///< 符号グループ（左右）
        int tier = 0; ///< 時間差グループ（上下段）
        int col  = 0;
        int row  = 0;
        int cols = 1; ///< 中央揃えと列単位の時間差に使う、そのグループ内の列数
    };

    void Calculate(DroneComponent &drone, const FormationContext &ctx) override
    {
        using namespace DirectX::SimpleMath;

        drone.chargeMode = spec.chargeMode;

        // --- 基底：ボス→プレイヤーの水平方向と、その右手直交 ---
        //     全ての陣形がこの2本を基準に組み立てられる（C++ 版でも同じ7行が並んでいた）。
        Vector3 fwd = ctx.playerPos - ctx.bossPos;
        fwd.y       = 0.0f;
        if (fwd.LengthSquared() < kEpsilon) fwd = Vector3::UnitZ;
        fwd.Normalize();

        Vector3 right = Vector3::UnitY.Cross(fwd);
        if (right.LengthSquared() < kEpsilon) right = Vector3::UnitX;
        right.Normalize();

        AdvanceProgress(drone, ctx.dt);

        const Seat  seat   = AssignSeat(drone);
        const float phaseT = PhaseT(seat);
        const float local  = LocalProgress(drone.attackProgress, phaseT);
        const float amount = EaseAmount(local);

        // --- 席から「列方向の量 a」「行方向（高さ）の量 b」を出す ---
        const float a = spec.shape.centerColumns
                          ? (seat.col - (seat.cols - 1) * 0.5f) * spec.shape.spacingA
                          : spec.shape.baseA + seat.col * spec.shape.spacingA;

        const float b = spec.shape.baseB
                      + seat.tier * spec.shape.tierStepB
                      + seat.row * spec.shape.spacingB;

        const Vector3 origin = ctx.bossPos
                             + fwd * spec.shape.originForward
                             + Vector3::UnitY * spec.shape.originUp
                             + right * spec.shape.originRight;

        const float sign = MirrorSign(seat.side);

        Vector3 pos;
        Vector3 look;

        if (spec.shape.type == SpecShape::Arm)
        {
            // 腕：列は「腕の向き」への距離。YawSweep がその向きを Y 軸まわりに振る。
            const float angle = (spec.transform.type == SpecTransform::YawSweep)
                                  ? sign * std::lerp(spec.transform.from, spec.transform.to, amount) * kDegToRad
                                  : 0.0f;

            const Quaternion swing  = Quaternion::CreateFromAxisAngle(Vector3::UnitY, angle);
            const Vector3    armDir = Vector3::Transform(fwd, swing);

            pos  = origin + armDir * a + Vector3::UnitY * b;
            look = armDir;
        }
        else
        {
            const Vector3 colAxis = AxisVector(spec.shape.columnAxis, right, fwd);

            if (spec.transform.type == SpecTransform::Hinge)
            {
                // ヒンジ：高さベクトルを下端まわりに up→forward へ倒す。
                //   b=0 の席（最下段）は角度に依らず動かない＝そこが回転軸になる。
                const float theta = std::lerp(spec.transform.from, spec.transform.to, amount) * kDegToRad;
                const float c     = std::cos(theta);
                const float s     = std::sin(theta);

                pos  = origin + colAxis * a + Vector3::UnitY * (b * c) + fwd * (b * s);
                look = Vector3::UnitY * (-s) + fwd * c;
            }
            else if (spec.transform.type == SpecTransform::Translate)
            {
                const float   d        = sign * std::lerp(spec.transform.from, spec.transform.to, amount);
                const Vector3 moveAxis = AxisVector(spec.transform.axis, right, fwd);

                pos  = origin + colAxis * a + Vector3::UnitY * b + moveAxis * d;
                look = moveAxis * (-sign);
            }
            else
            {
                pos  = origin + colAxis * a + Vector3::UnitY * b;
                look = fwd;
            }
        }

        // --- 予備動作の見せ：先に動く側だけを震わせる ---
        //     「どちらが先か」をプレイヤーへ読ませるための情報表示なので、
        //     形が動き出したら（進行度>0）消す。
        if (spec.tell.enabled && drone.attackProgress <= 0.0f && phaseT <= spec.tell.leadThreshold)
        {
            const float shiver = std::sin(ctx.time * spec.tell.frequency
                                          + static_cast<float>(drone.localIndex))
                               * spec.tell.amplitude;
            pos += AxisVector(spec.tell.axis, right, fwd) * shiver;
        }

        // --- 地面へのめり込み防止 ---
        //     水平を越えて振り下ろす形は、そのままだと刃先が地面下へ潜る。
        if (spec.clampToGround)
        {
            const float floorY = ctx.bossPos.y + spec.groundClearance;
            if (pos.y < floorY) pos.y = floorY;
        }

        drone.targetPosition = pos;

        if (spec.look == SpecLook::ToPlayer)
        {
            Vector3 toPlayer = ctx.playerPos - pos;
            toPlayer.y       = 0.0f;
            look             = (toPlayer.LengthSquared() > kEpsilon) ? toPlayer : fwd;
        }

        if (look.LengthSquared() < kEpsilon) look = fwd;
        look.Normalize();
        drone.fireDirection       = look;
        drone.targetLookDirection = look;
    }

    // GetLifecycle は既定の Broadcast のまま。
    //   Individual（各機が飛距離で自分から切り上げる）はこの形式の対象外なので、
    //   ここで返せてしまうと「データにできない技をデータで書けたように見える」。

    /// @brief localIndex を (グループ, 列, 行) の席へ割り付ける。
    Seat AssignSeat(const DroneComponent &drone) const
    {
        const int total  = (drone.totalDrones > 0) ? drone.totalDrones : 1;
        const int sides  = (std::max)(1, spec.assign.sideGroups);
        const int groups = (std::max)(1, sides * (std::max)(1, spec.assign.tierGroups));

        int group    = 0;
        int k        = drone.localIndex;
        int perGroup = total;

        if (groups > 1)
        {
            perGroup = (std::max)(1, total / groups);

            if (spec.assign.split == SpecSplit::Interleaved)
            {
                group = drone.localIndex % groups;
                k     = drone.localIndex / groups;
            }
            else
            {
                group = (std::min)(drone.localIndex / perGroup, groups - 1);
                k     = drone.localIndex - group * perGroup;
            }
        }

        Seat seat;
        seat.side = group % sides;
        seat.tier = group / sides;

        if (spec.assign.rows <= 0)
        {
            // 自動格子：正方に近い形へ敷き詰める。列が先に進む。
            seat.cols = (std::max)(1, static_cast<int>(std::floor(std::sqrt(static_cast<float>(perGroup)))));
            seat.col  = k % seat.cols;
            seat.row  = k / seat.cols;
        }
        else
        {
            // 行数固定：1列を縦に埋めてから隣の列へ。
            //
            // 端数（列を埋めきれない機体）の扱いが2通りある。どちらを使うかは
            // 「列が波の刻みになっているか」で決まる:
            //   波あり … 端数を独立した列にすると、その列だけ薄いのに波の1コマを
            //            占めてしまい、進みが不均等になる。最終列へ吸収させる。
            //   波なし … 刻みが無いので吸収する理由が無い。1機だけの列を許す
            //            （吸収すると同じ席に2機が重なる）。
            const int  rows         = spec.assign.rows;
            const int  fullCols     = (std::max)(1, perGroup / rows);
            const bool waveByColumn = (spec.timing.phaseSource == SpecPhaseSource::Column);

            if (waveByColumn)
            {
                seat.cols = fullCols;
                seat.col  = (std::min)(k / rows, fullCols - 1);
            }
            else
            {
                seat.cols = fullCols + ((perGroup % rows != 0) ? 1 : 0);
                seat.col  = k / rows;
            }
            seat.row = k % rows;
        }
        return seat;
    }

    /// @brief この席が波の「何番目」か（0=最初に動く, 1=最後）。
    float PhaseT(const Seat &seat) const
    {
        float t = 0.0f;
        switch (spec.timing.phaseSource)
        {
        case SpecPhaseSource::Column:
            t = (seat.cols <= 1) ? 0.0f : static_cast<float>(seat.col) / (seat.cols - 1);
            break;
        case SpecPhaseSource::Tier: {
            const int tiers = (std::max)(1, spec.assign.tierGroups);
            t = (tiers <= 1) ? 0.0f : static_cast<float>(seat.tier) / (tiers - 1);
            break;
        }
        default:
            return 0.0f;
        }
        return spec.timing.phaseReverse ? (1.0f - t) : t;
    }

  private:
    static constexpr float kEpsilon  = 0.001f;
    static constexpr float kDegToRad = 3.14159265358979f / 180.0f;

    /// @brief 進行度を進める。書き手が陣形であることは DroneComponent.h の規約どおり。
    void AdvanceProgress(DroneComponent &drone, float dt) const
    {
        if (drone.wantsAttack)
            drone.attackProgress = (std::min)(1.0f, drone.attackProgress + dt * spec.timing.speed);
        else
            drone.attackProgress = 0.0f;
    }

    /// @brief 全体の進行度から、この席ぶんの進行度を切り出す。
    ///
    /// 全体を phaseOverlap 幅の窓が滑っていく形にする。窓が狭いほど波が細く、
    /// 1.0 なら全席が同時に動く（時間差なし）。
    float LocalProgress(float progress, float phaseT) const
    {
        if (spec.timing.phaseSource == SpecPhaseSource::None)
            return std::clamp(progress, 0.0f, 1.0f);

        const float overlap = std::clamp(spec.timing.phaseOverlap, 0.01f, 1.0f);
        const float start   = phaseT * (1.0f - overlap);
        return std::clamp((progress - start) / overlap, 0.0f, 1.0f);
    }

    /// @brief 進行度 → 変換量。カーブ未設定なら既存陣形と同じ easeIn にする。
    float EaseAmount(float local) const
    {
        if (spec.timing.curve.empty()) return local * local;
        return spec.timing.curve.Evaluate(local);
    }

    float MirrorSign(int side) const
    {
        if (!spec.transform.mirrorBySide) return 1.0f;
        const bool isFirstSide = (side == 0);
        const bool positive    = spec.transform.mirrorFlip ? !isFirstSide : isFirstSide;
        return positive ? 1.0f : -1.0f;
    }

    static DirectX::SimpleMath::Vector3 AxisVector(SpecAxis axis,
        const DirectX::SimpleMath::Vector3                 &right,
        const DirectX::SimpleMath::Vector3                 &fwd)
    {
        switch (axis)
        {
        case SpecAxis::Right:   return right;
        case SpecAxis::Forward: return fwd;
        default:                return DirectX::SimpleMath::Vector3::UnitY;
        }
    }
};

} // namespace CCL::AI::Formation
