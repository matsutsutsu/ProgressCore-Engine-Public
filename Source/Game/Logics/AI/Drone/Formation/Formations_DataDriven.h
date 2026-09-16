// FormationAsset を解釈して座標を出す唯一の陣形クラス。
//
// 責務: ①割付 ②形 ③動き ④配置 の4段を1回だけ実装する。技ごとの違いはすべて
//       FormationAsset の数値に入っているので、技が増えてもこのファイルは増えない。
//
// 計算はすべて「陣形ローカル座標」で行い、最後に一度だけワールドへ移す。
//   ローカルの x = Right / y = Up / z = Forward。この3軸は正規直交で、
//   Right = Up × Forward が X = Y × Z と同じ関係なので、ワールドの単位軸まわりの
//   回転がそのまま陣形ローカルの回転になる。おかげでレイヤーごとに基底を
//   掛け直さずに済み、最後の1回だけ Right/Up/Forward を展開すればよい。
//
// 既存の C++ 陣形（Formations_Attack.h）との関係:
//   同じ技を両方に持たせない。JSON が名乗れるのは kDataFormationSlotFirst〜Last の
//   予約枠だけで、C++ 実装のある枠は登録時に弾く。同じ技の定義が2箇所に並ぶと、
//   片方だけ更新されたときに気づけないため。
#pragma once
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "Engine/Platform/Core/FileSystem.h"
#include "Engine/Platform/Core/Logger.h"
#include "Game/Logics/AI/Drone/Formation/FormationAsset.h"
#include "Game/Logics/AI/Drone/Formation/FormationAssetIO.h"
#include "Game/Logics/AI/Drone/Formation/FormationRegistry.h"
#include "Game/Logics/AI/Drone/Formation/IFormation.h"

namespace CCL::AI::Formation::Data
{

    // ① 割付の出力。1機が座る「席」。
    //   colCount / rowCount を持ち回るのは、中央揃えと波の位相がどちらも
    //   「全体の中で自分が何番目か」を必要とするため。
    struct Seat
    {
        int side     = 0; // 左右対称の何番目か。mirror が符号を取る
        int tier     = 0; // 段の何番目か。PhaseSource::Group がずらす単位
        int col      = 0;
        int row      = 0;
        int colCount = 1;
        int rowCount = 1;
    };

    class DataDrivenFormation : public IFormation
    {
    public:
        void                  SetAsset(const FormationAsset &asset) { m_asset = asset; }
        const FormationAsset &GetAsset() const { return m_asset; }

        /// エディタが実機の陣形を直接編集するための口。コピーを持たせると
        /// 「エディタでは変わったのに画面は変わらない」が起きる。
        FormationAsset &GetMutableAsset() { return m_asset; }

        void Calculate(DroneComponent &drone, const FormationContext &ctx) override;

        /// データ陣形は必ず Broadcast。個体が自分で攻撃を終わらせる技（弾幕・小隊）は
        /// フィールドへ状態をラッチするので、そもそもこの形式では書けない。
        ChargeLifecycle GetLifecycle() const override { return ChargeLifecycle::Broadcast; }

        FormationCombat GetCombat() const override { return m_asset.output.combat; }
        bool            UsesShiverTelegraph() const override { return m_asset.output.useShiverTelegraph; }

        /// 進行度を外から固定する（エディタの手送り）。負で通常どおり dt で進む。
        ///
        /// ランタイムには存在しない状態をここへ置いているのは、「陣形が attackProgress を
        /// 毎フレーム書く」という規約を破らずに止める方法が他に無いため。外から
        /// DroneComponent へ書いても、次のフレームに陣形が上書きしてしまう。
        void  SetProgressOverride(float progress) { m_progressOverride = progress; }
        void  ClearProgressOverride() { m_progressOverride = -1.0f; }
        float GetProgressOverride() const { return m_progressOverride; }

        /// 席の格子の大きさ。エディタが「何列×何行になるか」を出すのに使う。
        /// 割付と同じ式をエディタ側で書き直すと、片方だけ直したときに嘘の数字が出る。
        static void GridSize(const FormationAsset &asset, int totalDrones, int &outCols, int &outRows);

    private:
        using Vector3 = DirectX::SimpleMath::Vector3;

        /// ローカル空間での軸ベクトル。Right/Up/Forward がそのまま X/Y/Z に対応する。
        static Vector3 AxisVector(AssetAxis axis);

        Seat    AssignSeat(const DroneComponent &drone) const;
        Vector3 MakeLocal(const Seat &seat) const;

        /// この席が「何番目に動くか」を 0〜1 で返す。波も先攻後攻もこの1つに集約される。
        float SeatPhaseT(const FormationLayer &layer, const Seat &seat) const;

        /// 進行度 p におけるこの層の変換量。角度[度] または 距離[m]。
        float LayerValue(const FormationLayer &layer, const Seat &seat, float p) const;

        /// mirror が掛ける符号。sideGroups=2 なら side0=-1 / side1=+1。
        float SideSign(const Seat &seat) const;

        void ApplyLayer(const FormationLayer &layer, const Seat &seat, float p,
                        const FormationContext &ctx, const DroneComponent &drone,
                        Vector3 &local, Vector3 &look) const;

        FormationAsset m_asset;

        /// エディタが進行度を握っている間だけ 0〜1。負なら通常どおり dt で進む。
        float m_progressOverride = -1.0f;
    };

    // ---------------------------------------------------------------------------

    inline DataDrivenFormation::Vector3 DataDrivenFormation::AxisVector(AssetAxis axis)
    {
        switch (axis)
        {
        case AssetAxis::Up:
            return Vector3::UnitY;
        case AssetAxis::Forward:
            return Vector3::UnitZ;
        default:
            return Vector3::UnitX; // Right
        }
    }

    // 席の格子の大きさ。割付とエディタ表示で同じ式を使うため切り出してある。
    inline void DataDrivenFormation::GridSize(
        const FormationAsset &asset, int totalDrones, int &outCols, int &outRows)
    {
        const FormationAssign &as = asset.assign;

        const int total      = (totalDrones > 0) ? totalDrones : 1;
        const int sideCount  = (std::max)(1, as.sideGroups);
        const int groupCount = (std::max)(1, sideCount * (std::max)(1, as.tierGroups));
        const int perGroup   = (std::max)(1, total / groupCount);

        int rows = as.rows;
        int cols = 1;
        if (rows <= 0)
        {
            // rows 未指定なら、機数の平方根から正方に近い格子を組む。
            cols = (std::max)(1, static_cast<int>(std::floor(std::sqrt(static_cast<float>(perGroup)))));
            rows = (std::max)(1, (perGroup + cols - 1) / cols);
        }
        else
        {
            cols = (std::max)(1, (perGroup + rows - 1) / rows);
        }

        outCols = cols;
        outRows = rows;
    }

    // ① localIndex → 席
    inline Seat DataDrivenFormation::AssignSeat(const DroneComponent &drone) const
    {
        const FormationAssign &as = m_asset.assign;

        const int total      = (drone.totalDrones > 0) ? drone.totalDrones : 1;
        const int sideCount  = (std::max)(1, as.sideGroups);
        const int groupCount = (std::max)(1, sideCount * (std::max)(1, as.tierGroups));
        const int perGroup   = (std::max)(1, total / groupCount);
        const int index      = static_cast<int>(drone.localIndex);

        // グループ番号と、グループ内の通し番号。
        int group = 0;
        int k     = index;
        if (as.split == AssetSplit::Interleaved)
        {
            group = index % groupCount;
            k     = index / groupCount;
        }
        else
        {
            // 割り切れない余りは最後のグループへ寄せる。ここで clamp しないと
            // 存在しないグループ番号が出て、左右の符号や段の高さが飛ぶ。
            group = (std::min)(index / perGroup, groupCount - 1);
            k     = index - group * perGroup;
        }

        Seat seat;
        seat.side = group % sideCount; // 左右が先に変わる（挟み込み2段の 下左/下右/上左/上右）
        seat.tier = group / sideCount;

        int cols = 1, rows = 1;
        GridSize(m_asset, total, cols, rows);

        if (as.fillOrder == AssetFillOrder::ColumnFirst)
        {
            seat.col = k % cols; // 横に敷き詰めてから上へ
            seat.row = k / cols;
        }
        else
        {
            seat.col = k / rows; // 1列を縦に埋めてから隣の列へ
            seat.row = k % rows;
        }

        seat.colCount = cols;
        seat.rowCount = rows;
        return seat;
    }

    // ② 席 → ローカル座標（動かす前の「待機の形」）
    inline DataDrivenFormation::Vector3 DataDrivenFormation::MakeLocal(const Seat &seat) const
    {
        const FormationLayout &lo = m_asset.layout;

        const float a = lo.centerColumns
                            ? (seat.col - (seat.colCount - 1) * 0.5f) * lo.spacingA
                            : lo.baseA + seat.col * lo.spacingA;

        const float b = lo.baseB + seat.row * lo.spacingB + seat.tier * lo.tierStepB;

        return AxisVector(lo.columnAxis) * a + Vector3::UnitY * b;
    }

    inline float DataDrivenFormation::SeatPhaseT(const FormationLayer &layer, const Seat &seat) const
    {
        float t = 0.0f;
        switch (layer.phaseSource)
        {
        case PhaseSource::Column:
            t = (seat.colCount > 1) ? static_cast<float>(seat.col) / (seat.colCount - 1) : 0.0f;
            break;
        case PhaseSource::Row:
            t = (seat.rowCount > 1) ? static_cast<float>(seat.row) / (seat.rowCount - 1) : 0.0f;
            break;
        case PhaseSource::Group: {
            const int n = m_asset.assign.tierGroups;
            t           = (n > 1) ? static_cast<float>(seat.tier) / (n - 1) : 0.0f;
            break;
        }
        default:
            break; // None＝全席同時
        }

        if (layer.phaseReverse) t = 1.0f - t;
        return std::clamp(t, 0.0f, 1.0f);
    }

    inline float DataDrivenFormation::SideSign(const Seat &seat) const
    {
        const int n = m_asset.assign.sideGroups;
        if (n <= 1) return 1.0f;
        return static_cast<float>(seat.side) * (2.0f / (n - 1)) - 1.0f;
    }

    inline float DataDrivenFormation::LayerValue(
        const FormationLayer &layer, const Seat &seat, float p) const
    {
        // 窓幅と duration の正当性は FormationAssetIO::Validate が保証している
        // （0除算になる値は読み込み時に直され、どのファイルが悪いかがログに出る）。
        const float width = layer.windowEnd - layer.windowBegin;
        const float start = layer.windowBegin + SeatPhaseT(layer, seat) * (width - layer.duration);
        const float t     = std::clamp((p - start) / layer.duration, 0.0f, 1.0f);

        // キーの無いカーブは従来どおり easeIn。既存陣形と絵を揃えるための既定。
        const float eased = layer.curve.empty() ? (t * t) : layer.curve.Evaluate(t);

        float value = layer.from + (layer.to - layer.from) * eased;
        if (layer.mirror) value *= SideSign(seat);
        return value;
    }

    // ③ 動き：1枚ぶん
    inline void DataDrivenFormation::ApplyLayer(const FormationLayer &layer, const Seat &seat, float p,
                                                const FormationContext &ctx, const DroneComponent &drone, Vector3 &local, Vector3 &look) const
    {
        if (layer.type == LayerType::Shiver)
        {
            // 予告なので、進行度が動き出した時点で消える。震えるのは「先に動く側」だけで、
            // それが「どちらが先に来るか」をプレイヤーへ伝える唯一の手がかりになる。
            if (p > 0.0f) return;
            if (SeatPhaseT(layer, seat) > layer.leadThreshold) return;

            // 位相を localIndex でずらす。揃えると群れが1枚の板として揺れて予告に見えない。
            const float offset = std::sin(ctx.time * layer.frequency +
                                          static_cast<float>(drone.localIndex)) *
                                 layer.to;
            local += AxisVector(layer.axis) * offset;
            return;
        }

        const float   value = LayerValue(layer, seat, p);
        const Vector3 axis  = AxisVector(layer.axis);

        if (layer.type == LayerType::Rotate)
        {
            const auto q = DirectX::SimpleMath::Quaternion::CreateFromAxisAngle(
                axis, DirectX::XMConvertToRadians(value));

            local = Vector3::Transform(local, q);

            // 機首も同じだけ回す。倒れる刃が倒れる方向を向き、薙ぐ腕が薙ぐ方向を向く。
            look = Vector3::Transform(look, q);
        }
        else
        {
            local += axis * value;
        }
    }

    inline void DataDrivenFormation::Calculate(DroneComponent &drone, const FormationContext &ctx)
    {
        using namespace DirectX::SimpleMath;

        drone.chargeMode = m_asset.output.chargeMode;

        // 撃つ合図が来るまで進行度を0に張り付け、予備動作の「形」で待つ。
        // プレイヤーはこの静止した形から回避方向を読む。
        if (m_progressOverride >= 0.0f)
            drone.attackProgress = (std::min)(1.0f, m_progressOverride); // エディタの手送り
        else if (drone.wantsAttack)
            drone.attackProgress = (std::min)(1.0f, drone.attackProgress + ctx.dt / m_asset.attackSeconds);
        else
            drone.attackProgress = 0.0f;

        const float p    = drone.attackProgress;
        const Seat  seat = AssignSeat(drone);

        Vector3 local = MakeLocal(seat);
        Vector3 look  = Vector3::UnitZ; // ローカルの Forward。Rotate が同じだけ回す

        for (const FormationLayer &layer : m_asset.layers)
            ApplyLayer(layer, seat, p, ctx, drone, local, look);

        // ④ ローカル → ワールド。
        //    AimDirection() は「撃つと決めた瞬間」に焼かれた向きを返すので、倒れている
        //    最中にプレイヤーが横へ動いても形は追わない（＝避けた者勝ちが成立する）。
        const Vector3 forward = ctx.AimDirection();
        Vector3       right   = Vector3::UnitY.Cross(forward);
        if (right.LengthSquared() < 0.001f) right = Vector3::UnitX;
        right.Normalize();

        // origin は回転を受けない。これが「軸の位置」であって形の一部ではないから
        // ＝ ヒンジの高さ(origin.up)を変えても倒れ方は変わらない、という分離になる。
        Vector3 world = ctx.bossPos + right * (local.x + m_asset.origin.right) + Vector3::UnitY * (local.y + m_asset.origin.up) + forward * (local.z + m_asset.origin.forward);

        if (m_asset.output.clampToGround)
            world.y = (std::max)(world.y, kDroneGroundY + kDroneGroundClearance);

        drone.targetPosition = world;

        Vector3 lookWorld;
        switch (m_asset.output.look)
        {
        case LookMode::ToPlayer:
            lookWorld   = ctx.playerPos - world;
            lookWorld.y = 0.0f;
            break;

        // 左右対称の内側。mirror が使う符号の逆を向くので、2本の壁が必ず互いを向く。
        // 座標から中心線を引く方式にしないのは、奥行きのある壁だと先端ほど内を向いて
        // 壁が弓なりに見えるため（挟み込み2段は前後に長い）。
        case LookMode::Inward:
            lookWorld = right * -SideSign(seat);
            break;

        default: // Auto：回転レイヤーが回した Forward をそのまま使う
            lookWorld = right * look.x + Vector3::UnitY * look.y + forward * look.z;
            break;
        }

        if (lookWorld.LengthSquared() < 0.001f) lookWorld = forward;
        lookWorld.Normalize();

        drone.fireDirection       = lookWorld;
        drone.targetLookDirection = lookWorld;
    }

    // ---------------------------------------------------------------------------
    // 登録（DroneFormationSystem::Initialize から呼ぶ）
    // ---------------------------------------------------------------------------

    /// Assets/Formations/*.json を読み、各アセットが名乗る予約枠へ登録する。
    ///
    /// ファイル名ではなく JSON 内の "slot" で枠を決める。ファイル名を枠にすると
    /// 技の名前を変えた瞬間に保存済みの BT が別の技を指す。
    inline void RegisterAll()
    {
        auto &registry = FormationRegistry::Instance();

        const std::vector<std::string> files = FileSystem::ListFiles("Assets/Formations", ".json");

        int loaded = 0;
        for (const std::string &path : files)
        {
            FormationAsset asset;
            if (!FormationAssetIO::Load(path, asset)) continue;

            if (asset.slot < kDataFormationSlotFirst || asset.slot > kDataFormationSlotLast)
            {
                CCL_LOG_ERROR(LogCategory::AI,
                              "[Formation] %s: slot=%u は予約枠(%u〜%u)の外。C++実装の枠を上書きしないため登録しない",
                              path.c_str(), asset.slot, kDataFormationSlotFirst, kDataFormationSlotLast);
                continue;
            }

            const auto type = static_cast<DroneFormationType>(asset.slot);
            if (registry.Has(type))
            {
                CCL_LOG_ERROR(LogCategory::AI,
                              "[Formation] %s: slot=%u は既に埋まっている。同じ枠を名乗るアセットが2つある",
                              path.c_str(), asset.slot);
                continue;
            }

            registry.Register<DataDrivenFormation>(type)->SetAsset(asset);
            ++loaded;

            CCL_LOG_INFO(LogCategory::AI, "[Formation] 枠%u ← %s (%s) レイヤー%zu枚",
                         asset.slot, asset.name.c_str(), path.c_str(), asset.layers.size());
        }

        // 空き枠は BT の候補には出るが実行すると静止する。どこが空いているかを
        // 起動時に必ず言っておかないと、選んだ本人が原因に辿り着けない。
        std::string empties;
        for (uint8_t s = kDataFormationSlotFirst; s <= kDataFormationSlotLast; ++s)
        {
            if (!registry.Has(static_cast<DroneFormationType>(s)))
                empties += (empties.empty() ? "" : ",") + std::to_string(s);
        }

        CCL_LOG_INFO(LogCategory::AI, "[Formation] データ陣形 %d件を登録。空き枠: %s",
                     loaded, empties.empty() ? "なし" : empties.c_str());
    }

} // namespace CCL::AI::Formation::Data
