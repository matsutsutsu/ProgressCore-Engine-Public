#include "DataDrivenFormation.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

#include "Engine/Platform/Core/FileSystem.h"
#include "Engine/Platform/Core/Logger.h"
#include "Game/Logics/AI/Drone/Components/DroneTuning.h" // 床の高さ（clampToGround）
#include "Game/Logics/AI/Drone/Formation/Data/FormationAssetIO.h"
#include "Game/Logics/AI/Drone/Formation/Data/ShapeSampler.h"
#include "Game/Logics/AI/Drone/Formation/FormationRegistry.h"

namespace CCL::AI::Formation::Data
{
    using DirectX::SimpleMath::Vector3;

    namespace
    {
        // 陣形ローカル空間での軸ベクトル。Right/Up/Forward がそのまま X/Y/Z に対応する。
        // Right = Up × Forward は X = Y × Z と同じ関係なので、単位軸まわりの回転がそのまま陣形ローカルの回転になる
        Vector3 AxisVector(AssetAxis axis)
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
    } // namespace

    // ============================================================
    // 全体の流れ
    // ============================================================

    void DataDrivenFormation::Calculate(DroneComponent &drone, const FormationContext &ctx)
    {
        // 処理フロー:
        // 1. 移動方式をアセットから写し、モデルの形を用意する
        // 2. 攻撃の進行度を進める
        // 3. 席を決め、待機の形を作る（(1)割付・(2)形）
        // 4. 動きの層を上から順に当てる（(3)動き）
        // 5. ワールドへ移す（(4)配置）
        // 6. 機首と突撃方向を決める

        drone.chargeMode = m_asset.output.chargeMode;

        // 形がモデル参照の場合にドローンの配置位置を計算する
        EnsureShape();

        // 攻撃の進行度を進める
        const float p = AdvanceProgress(drone, ctx);

        // ドローンの番号をもとに配置する席を決める
        const Seat seat  = AssignSeat(drone);

        // 席をもとに、陣形ローカルの座標と向きを決める
        Vector3    local = MakeLocal(seat, drone);
        Vector3    look  = Vector3::UnitZ; // ローカルの Forward。Rotate が同じだけ回す

        // 動きの層を上から順に当てる
        for (const FormationLayer &layer : m_asset.layers)
            ApplyLayer(layer, seat, p, ctx, drone, local, look);

        const Basis   basis = MakeBasis(ctx);

        // 陣形ローカルの座標をワールドへ変換する
        const Vector3 world = ToWorld(local, basis, ctx.bossPos);
        drone.targetPosition = world;

        // 陣形ローカルの向きをワールドへ変換する
        const Vector3 lookWorld   = ResolveLook(look, seat, basis, world, ctx);
        drone.fireDirection       = lookWorld;
        drone.targetLookDirection = lookWorld;
    }

    // ============================================================
    // 進行度
    // ============================================================

    float DataDrivenFormation::AdvanceProgress(DroneComponent &drone, const FormationContext &ctx) const
    {
        // 撃つ合図が来るまで 0 に保ち、予備動作の形で待つ（プレイヤーはこの形から回避方向を読む）
        if (m_progressOverride >= 0.0f)
            drone.attackProgress = (std::min)(1.0f, m_progressOverride); // エディタの手送り
        else if (drone.wantsAttack)
            drone.attackProgress = (std::min)(1.0f, drone.attackProgress + ctx.dt / m_asset.attackSeconds);
        else
            drone.attackProgress = 0.0f;

        return drone.attackProgress;
    }

    // ============================================================
    // (1) 割付: 通し番号 → 席
    // ============================================================

    namespace
    {
        // 割付グループの数え方。GridSize と AssignSeat で共有する
        struct GroupCounts
        {
            int sideCount  = 1; // 左右グループの数
            int groupCount = 1; // グループの総数（左右 × 上下）
            int perGroup   = 1; // 1グループあたりの機数（端数は切り捨て）
        };

        // FormationAssign と機数から、左右グループ数・総グループ数・1グループあたりの機数を返す
        GroupCounts CountGroups(const FormationAssign &as, int totalDrones)
        {
            const int total = (totalDrones > 0) ? totalDrones : 1;

            GroupCounts g;
            g.sideCount  = (std::max)(1, as.sideGroups);
            g.groupCount = (std::max)(1, g.sideCount * (std::max)(1, as.tierGroups));
            g.perGroup   = (std::max)(1, total / g.groupCount);
            return g;
        }
    } // namespace

    void DataDrivenFormation::GridSize(const FormationAsset &asset, int totalDrones, int &outCols, int &outRows)
    {
        const FormationAssign &as       = asset.assign;
        const int              perGroup = CountGroups(as, totalDrones).perGroup;

        int rows = as.rows;
        int cols = 1;
        if (rows <= 0)
        {
            // rows 未指定なら、正方に近い格子を組む
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

    Seat DataDrivenFormation::AssignSeat(const DroneComponent &drone) const
    {
        // 処理フロー:
        // 1. 通し番号からグループとグループ内の番号を出す
        // 2. グループを左右と段に分ける
        // 3. グループ内の番号を格子の列と行にする

        const FormationAssign &as    = m_asset.assign;
        const GroupCounts      g     = CountGroups(as, drone.totalDrones);
        const int              index = static_cast<int>(drone.localIndex);

        int group = 0;     // グループ番号
        int k     = index; // グループ内の通し番号
        if (as.split == AssetSplit::Interleaved)
        {
            group = index % g.groupCount;
            k     = index / g.groupCount;
        }
        else
        {
            // 割り切れない余りは最後のグループへ寄せる（存在しないグループ番号を出さない）
            group = (std::min)(index / g.perGroup, g.groupCount - 1);
            k     = index - group * g.perGroup;
        }

        Seat seat;
        seat.side = group % g.sideCount; // 左右が先に変わる（挟み込み2段の 下左/下右/上左/上右）
        seat.tier = group / g.sideCount;

        int cols = 1, rows = 1;
        GridSize(m_asset, drone.totalDrones, cols, rows);

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

    // ============================================================
    // (2) 形: 席 → ローカル座標（動かす前の「待機の形」）
    // ============================================================

    void DataDrivenFormation::EnsureShape()
    {
        const FormationLayout &lo = m_asset.layout;
        if (lo.shape != AssetShape::Model) return;
        if (m_shapeTried && m_shapeModelPath == lo.modelPath) return;

        m_shapeTried     = true;
        m_shapeModelPath = lo.modelPath;
        m_shapePoints    = SampleModelSurface(lo.modelPath, kMaxShapePoints);

        CCL_LOG_INFO(LogCategory::AI, "[Formation] %s: モデルの形 %s から %zu 点",
                     m_asset.name.c_str(), lo.modelPath.c_str(), m_shapePoints.size());
    }

    Vector3 DataDrivenFormation::MakeLocal(const Seat &seat, const DroneComponent &drone) const
    {
        const FormationLayout &lo = m_asset.layout;

        // --- モデルの形 ---

        // 最遠点の順に並んだ点へ通し番号の順に置く。機数が減っても形が欠けず、粗くなるだけで済む。
        // 点が取れなかったときは下の格子へ戻す（全機が1点に重なると、何が壊れたか見た目で分からない）
        if (lo.shape == AssetShape::Model && !m_shapePoints.empty())
        {
            const Vector3 &p = m_shapePoints[drone.localIndex % m_shapePoints.size()];

            const auto q = DirectX::SimpleMath::Quaternion::CreateFromYawPitchRoll(
                DirectX::XMConvertToRadians(lo.modelYaw),
                DirectX::XMConvertToRadians(lo.modelPitch),
                DirectX::XMConvertToRadians(lo.modelRoll));

            return Vector3::Transform(p * lo.modelScale, q);
        }

        // --- 格子 ---

        const float a = lo.centerColumns
                            ? (seat.col - (seat.colCount - 1) * 0.5f) * lo.spacingA
                            : lo.baseA + seat.col * lo.spacingA;

        const float b = lo.baseB + seat.row * lo.spacingB + seat.tier * lo.tierStepB;

        return AxisVector(lo.columnAxis) * a + Vector3::UnitY * b;
    }

    // ============================================================
    // (3) 動き: 層を1枚ずつ当てる
    // ============================================================

    void DataDrivenFormation::ApplyLayer(const FormationLayer &layer, const Seat &seat, float p,
                                         const FormationContext &ctx, const DroneComponent &drone, Vector3 &local, Vector3 &look) const
    {
        if (layer.type == LayerType::Shiver)
        {
            // 震えだけは攻撃ではなく予告の時間の上に置く。撃つ合図で進行度が動き出したら消える
            if (p > 0.0f) return;

            const float u = TelegraphT(drone);
            if (u < layer.windowBegin || u > layer.windowEnd) return;

            // 先に動く側だけを震わせ、どちらが先に来るかの手がかりにする
            if (SeatPhaseT(layer, seat) > layer.leadThreshold) return;

            // 位相は localIndex でずらす。揃えると群れが1枚の板として揺れ、予告に見えない
            const float amplitude = LayerValue(layer, seat, u);
            const float offset    = std::sin(ctx.time * layer.frequency + static_cast<float>(drone.localIndex)) * amplitude;
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

            // 機首も同じだけ回す（倒れる刃が倒れる方向を向く）
            look = Vector3::Transform(look, q);
        }
        else
        {
            local += axis * value;
        }
    }

    float DataDrivenFormation::LayerValue(const FormationLayer &layer, const Seat &seat, float p) const
    {
        // 0除算になる値（窓幅0・duration 0以下）は FormationAssetIO::Validate が読み込み時に直す。
        // duration を窓幅までに抑えるのは、エディタで編集中の値でも保存後と同じ動きを見せるため
        const float width    = layer.windowEnd - layer.windowBegin;
        const float duration = (std::min)(layer.duration, width);
        const float start    = layer.windowBegin + SeatPhaseT(layer, seat) * (width - duration);
        const float t        = std::clamp((p - start) / duration, 0.0f, 1.0f);

        float value = layer.from + (layer.to - layer.from) * Ease(layer, t);
        if (layer.mirror) value *= SideSign(seat);
        return value;
    }

    float DataDrivenFormation::SeatPhaseT(const FormationLayer &layer, const Seat &seat) const
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

    float DataDrivenFormation::SideSign(const Seat &seat) const
    {
        const int n = m_asset.assign.sideGroups;
        if (n <= 1) return 1.0f;
        return static_cast<float>(seat.side) * (2.0f / (n - 1)) - 1.0f;
    }

    float DataDrivenFormation::TelegraphT(const DroneComponent &drone) const
    {
        const float seconds = (m_telegraphOverride >= 0.0f) ? m_telegraphOverride : drone.telegraphTime;
        return seconds / m_asset.telegraphSeconds; // 上限で止めない。帯の外＝予告の後は震えない
    }

    // ============================================================
    // (4) 配置: ローカル → ワールド
    // ============================================================

    DataDrivenFormation::Basis DataDrivenFormation::MakeBasis(const FormationContext &ctx)
    {
        Basis basis;
        basis.forward = ctx.AimDirection();
        basis.right   = Vector3::UnitY.Cross(basis.forward);
        if (basis.right.LengthSquared() < 0.001f) basis.right = Vector3::UnitX;
        basis.right.Normalize();
        return basis;
    }

    Vector3 DataDrivenFormation::ToWorld(const Vector3 &local, const Basis &basis, const Vector3 &bossPos) const
    {
        const FormationOrigin &origin = m_asset.origin;

        // origin は回転の軸の位置で、形の一部ではないので回転を受けない（ヒンジの高さを変えても倒れ方は変わらない）
        Vector3 world = bossPos + basis.right * (local.x + origin.right) + Vector3::UnitY * (local.y + origin.up) + basis.forward * (local.z + origin.forward);

        if (m_asset.output.clampToGround)
            world.y = (std::max)(world.y, kDroneGroundY + kDroneGroundClearance);

        return world;
    }

    // ============================================================
    // 機首と突撃方向
    // ============================================================

    Vector3 DataDrivenFormation::ResolveLook(const Vector3 &look, const Seat &seat, const Basis &basis,
                                             const Vector3 &world, const FormationContext &ctx) const
    {
        Vector3 lookWorld;
        switch (m_asset.output.look)
        {
        case LookMode::ToPlayer:
            lookWorld   = ctx.playerPos - world;
            lookWorld.y = 0.0f;
            break;

        // 左右対称の内側。mirror の符号の逆を向くので、2本の壁が必ず互いを向く。
        // 座標から中心線を引く方式だと、奥行きのある壁の先端ほど内を向き、壁が弧を描いて見える
        case LookMode::Inward:
            lookWorld = basis.right * -SideSign(seat);
            break;

        default: // Auto：回転レイヤーが回した Forward をそのまま使う
            lookWorld = basis.right * look.x + Vector3::UnitY * look.y + basis.forward * look.z;
            break;
        }

        if (lookWorld.LengthSquared() < 0.001f) lookWorld = basis.forward;
        lookWorld.Normalize();
        return lookWorld;
    }

    // ============================================================
    // 登録（DroneFormationSystem::Initialize から呼ぶ）
    // ============================================================

    void RegisterAll(FormationRegistry &registry)
    {
        // 処理フロー:
        // 1. Assets/Formations のファイルを一覧する
        // 2. 1つずつ読む
        // 3. 名乗った枠が使えるか確かめて登録する
        // 4. 空き枠をログへ出す

        // レジストリは World ごとに作り直されるので、シーンに入るたびに全ファイルを読み直す
        const std::vector<std::string> files = FileSystem::ListFiles("Assets/Formations", ".json");

        int loaded = 0;
        for (const std::string &path : files)
        {
            FormationAsset asset;
            if (!FormationAssetIO::Load(path, asset)) continue;

            // 欠番は BT から指す手段が無い
            if (asset.slot >= kDroneFormationSlotCount ||
                std::string_view(kDroneFormationNames[asset.slot]) == "(欠番)")
            {
                CCL_LOG_ERROR(LogCategory::AI, "[Formation] %s: slot=%u は陣形の枠として定義されていない",
                              path.c_str(), asset.slot);
                continue;
            }

            // 名前のある枠（縦割り等）も名乗れるが、先に登録した C++ 陣形か別の JSON が居れば弾く
            const auto type = static_cast<DroneFormationType>(asset.slot);
            if (registry.Has(type))
            {
                CCL_LOG_ERROR(LogCategory::AI,
                              "[Formation] %s: slot=%u は既に埋まっている（C++ 実装か、同じ枠を名乗る別のアセット）",
                              path.c_str(), asset.slot);
                continue;
            }

            registry.Register<DataDrivenFormation>(type)->SetAsset(asset);
            ++loaded;

            CCL_LOG_INFO(LogCategory::AI, "[Formation] 枠%u ← %s (%s) レイヤー%zu枚",
                         asset.slot, asset.name.c_str(), path.c_str(), asset.layers.size());
        }

        // 空き枠は BT の候補に出るが、実行すると静止する。選んだ人が原因に辿り着けるよう、起動時にログへ出す
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
