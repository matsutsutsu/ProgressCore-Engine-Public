#pragma once
// ===========================================================================
// 概要: FormationAsset（JSON）を解釈して持ち場を出す陣形クラスと、その登録処理。
//
// 責務:
//   - (1)割付 (2)形 (3)動き (4)配置 の4段を1回だけ実装する
//   - Assets/Formations/*.json を読み、各アセットが名乗る枠へ登録する
//   - エディタの手送り（予告の経過・進行度の固定）を受ける
//
// 備考:
//   技ごとの違いはすべて FormationAsset の数値に入っているので、技が増えてもこの
//   ファイルは増えない。
//
//   計算はすべて陣形ローカル座標で行い、最後に一度だけワールドへ移す。
//   ローカルの x = Right / y = Up / z = Forward。この3軸は正規直交で、
//   Right = Up × Forward が X = Y × Z と同じ関係なので、ワールドの単位軸まわりの
//   回転がそのまま陣形ローカルの回転になる。そのためレイヤーごとに基底を
//   掛け直さずに済み、最後の1回だけ Right/Up/Forward を展開すればよい。
//
//   C++ で書いた陣形（Formations_*.h）と同じ技を両方に持たせない。C++ の登録が先に
//   走り、C++ 実装のある枠を名乗った JSON は登録時に弾く。同じ技の定義が2箇所に
//   並ぶと、片方だけ更新されたときに気づけないため。
// ===========================================================================
#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

#include "Engine/Platform/Core/FileSystem.h"
#include "Engine/Platform/Core/Logger.h"
#include "Game/Logics/AI/Drone/Components/DroneTuning.h" // 床の高さ（clampToGround）
#include "Game/Logics/AI/Drone/Formation/FormationAsset.h"
#include "Game/Logics/AI/Drone/Formation/FormationAssetIO.h"
#include "Game/Logics/AI/Drone/Formation/FormationRegistry.h"
#include "Game/Logics/AI/Drone/Formation/IFormation.h"
#include "Game/Logics/AI/Drone/Formation/ShapeSampler.h"

namespace CCL::AI::Formation::Data
{

    // (1) 割付の出力。1機が入る「席」。
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
        void SetAsset(const FormationAsset &asset)
        {
            m_asset = asset;
            EnsureShape(); // 重い点の取り出しを、戦闘中ではなく登録時に済ませる
        }
        const FormationAsset &GetAsset() const { return m_asset; }

        // エディタが実行中の陣形を直接編集するための入口。コピーを持たせると
        // 「エディタでは変わったのに画面は変わらない」が起きる。
        FormationAsset &GetMutableAsset() { return m_asset; }

        void Calculate(DroneComponent &drone, const FormationContext &ctx) override;

        // データ陣形は必ず Broadcast。個体が自分で攻撃を終わらせる技（弾幕）は
        // 機体ごとに状態を保持するので、この形式では書けない。
        ChargeLifecycle GetLifecycle() const override { return ChargeLifecycle::Broadcast; }

        // 技の名前はアセットが持つ。エディタで名前を変えれば BTエディタの表示も変わる。
        const char *DisplayName() const override { return m_asset.name.c_str(); }

        FormationCombat GetCombat() const override { return m_asset.output.combat; }
        bool            UsesShiverTelegraph() const override { return m_asset.output.useShiverTelegraph; }

        /**
         * @brief 予告の経過秒数と攻撃の進行度を外から固定する（エディタの手送り）。
         *
         * ゲーム中には使わない状態をここへ置いているのは、「陣形が attackProgress を
         * 毎フレーム書く」という規約を破らずに止める方法が他に無いため。外から
         * DroneComponent へ書いても、次のフレームに陣形が上書きしてしまう。
         */
        void SetPreview(float telegraphSeconds, float progress)
        {
            m_telegraphOverride = telegraphSeconds;
            m_progressOverride  = progress;
        }
        void ClearPreview()
        {
            m_telegraphOverride = -1.0f;
            m_progressOverride  = -1.0f;
        }

        /**
         * @brief 1グループ分の席の格子の大きさを求める。エディタが「何列×何行になるか」を出すのに使う。
         *
         * 割付と同じ式をエディタ側で書き直すと、片方だけ直したときに誤った数字が出る。
         *
         * @param asset       格子の決め方（assign）を持つ陣形アセット。
         * @param totalDrones 陣形に参加する機数。0以下は1機として扱う。
         * @param outCols     1グループの列数を受け取る。
         * @param outRows     1グループの行数を受け取る。
         */
        static void GridSize(const FormationAsset &asset, int totalDrones, int &outCols, int &outRows);

        /**
         * @brief 席ごとの進み t を、始まり→終わりのどこまで進んだかへ変換する（緩急）。
         *
         * エディタの緩急のグラフもこれを描く。
         *
         * @param layer 緩急のカーブを持つレイヤー。キーの無いカーブは easeIn（t*t）として扱う。
         * @param t     席ごとの進み（0〜1）。
         * @return 始まり→終わりのどこまで進んだか（0〜1）。
         */
        static float Ease(const FormationLayer &layer, float t)
        {
            return layer.curve.empty() ? (t * t) : layer.curve.Evaluate(t);
        }

    private:
        using Vector3 = DirectX::SimpleMath::Vector3;

        // ローカル空間での軸ベクトル。Right/Up/Forward がそのまま X/Y/Z に対応する。
        static Vector3 AxisVector(AssetAxis axis);

        Seat    AssignSeat(const DroneComponent &drone) const;
        Vector3 MakeLocal(const Seat &seat, const DroneComponent &drone) const;

        /**
         * @brief 形が Model のとき、モデルの表面の点を用意する。モデルが変わったときだけ取り直す。
         *
         * エディタはアセットを直接書き換える（GetMutableAsset）ので、変更を知らせる入口が無い。
         * そこで Calculate の頭で毎回パスを見比べる。
         *
         * @note Calculate から呼ぶので、DroneFormationSystem が機体のループを直列（ForEach）で回すことが前提。
         *       並列にするなら、ループの前に1回だけ呼ぶ形へ移すこと。
         */
        void EnsureShape();

        /**
         * @brief この席が「何番目に動くか」を 0〜1 で返す。波も先攻後攻もこの値で表す。
         * @param layer 並べ方（phaseSource・phaseReverse）を持つ動きの層。
         * @param seat  調べる席。
         * @return 0=最初に動く、1=最後に動く。phaseSource が None なら全席 0（同時）。
         */
        float SeatPhaseT(const FormationLayer &layer, const Seat &seat) const;

        /**
         * @brief 攻撃の進行度 p におけるこの層の変換量を返す。
         *
         * 層の窓（windowBegin〜windowEnd）の中で、席の順番（SeatPhaseT）だけ開始をずらし、from → to を緩急つきで進める。
         *
         * @param layer 動きの層。
         * @param seat  変換量を求める席。mirror の層では左右で符号が変わる。
         * @param p     攻撃の進行度（0〜1）。震えの層では予告の進み（TelegraphT）を渡す。
         * @return 変換量。Rotate なら角度（度）、Translate なら距離（m）、Shiver なら振れ幅（m）。
         */
        float LayerValue(const FormationLayer &layer, const Seat &seat, float p) const;

        /**
         * @brief mirror の層が変換量に掛ける符号を返す。
         * @param seat 調べる席。
         * @return 左右の端で -1 / +1 になる値（sideGroups=2 なら side0=-1 / side1=+1）。sideGroups が1以下なら常に +1。
         */
        float SideSign(const Seat &seat) const;

        /**
         * @brief 動きの層を1枚、席の位置と機首に当てる。
         * @param layer 当てる動きの層。
         * @param seat  当てる席。
         * @param p     攻撃の進行度（0〜1）。震えの層は p が 0 の間（予告中）だけ効く。
         * @param ctx   陣形の計算に渡された情報（時刻など）。
         * @param drone 当てる機体。震えの位相と予告の経過に使う。
         * @param local 席のローカル座標。層の変換を足し込む。
         * @param look  機首の向き（ローカル）。Rotate の層では同じだけ回す。
         */
        void ApplyLayer(const FormationLayer &layer, const Seat &seat, float p,
                        const FormationContext &ctx, const DroneComponent &drone,
                        Vector3 &local, Vector3 &look) const;

        FormationAsset m_asset;

        // 取り出せる点の上限。機数がこれを超えた分は先頭から使い回す（同じ点に重なる）。
        static constexpr int kMaxShapePoints = 1024;

        std::vector<Vector3> m_shapePoints;    // モデル表面の点（モデル空間・最遠点の順）
        std::string          m_shapeModelPath; // m_shapePoints を取り出したモデル
        bool                 m_shapeTried = false; // 読めなかったモデルを毎フレーム読み直さないため

        /**
         * @brief この機体が予告の中のどこにいるかを返す。
         * @param drone 調べる機体。エディタが時間を固定している間は、その値を使う。
         * @return 予告の長さを 1 とした割合。上限で止めないので、1 を超えたら予告は終わっている。
         */
        float TelegraphT(const DroneComponent &drone) const;

        // エディタが時間を固定している間だけ 0 以上。負なら通常どおりゲームの時間で進む。
        float m_telegraphOverride = -1.0f; // 予告の経過 [秒]
        float m_progressOverride  = -1.0f; // 攻撃の進行度 0〜1
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

    // (1) localIndex → 席
    inline Seat DataDrivenFormation::AssignSeat(const DroneComponent &drone) const
    {
        // 処理フロー:
        // 1. 通し番号からグループとグループ内の番号を出す
        // 2. グループを左右と段に分ける
        // 3. グループ内の番号を格子の列と行にする

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

    inline void DataDrivenFormation::EnsureShape()
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

    // (2) 席 → ローカル座標（動かす前の「待機の形」）
    inline DataDrivenFormation::Vector3 DataDrivenFormation::MakeLocal(const Seat &seat, const DroneComponent &drone) const
    {
        const FormationLayout &lo = m_asset.layout;

        // --- モデルの形 ---

        // 通し番号の順に、最遠点の順で並んだ点へ置く。
        // 機数が減っても先頭から使うので、形が欠けずに粗くなるだけで済む。
        //
        // 点が取れなかったときは下の格子へ戻す。全機を1点に重ねると、何が壊れたか見た目から分からない。
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

    inline float DataDrivenFormation::TelegraphT(const DroneComponent &drone) const
    {
        const float seconds = (m_telegraphOverride >= 0.0f) ? m_telegraphOverride : drone.telegraphTime;
        return seconds / m_asset.telegraphSeconds; // 上限で止めない。帯の外＝予告の後は震えない
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
        // 0除算になる値（窓幅0・duration 0以下）は FormationAssetIO::Validate が読み込み時に直す。

        // duration を窓幅までに抑えるのは、エディタで窓を縮めている最中の値でも、
        // 保存後（Validate が同じように詰める）と同じ動きをプレビューで見せるため。
        const float width    = layer.windowEnd - layer.windowBegin;
        const float duration = (std::min)(layer.duration, width);
        const float start    = layer.windowBegin + SeatPhaseT(layer, seat) * (width - duration);
        const float t        = std::clamp((p - start) / duration, 0.0f, 1.0f);

        float value = layer.from + (layer.to - layer.from) * Ease(layer, t);
        if (layer.mirror) value *= SideSign(seat);
        return value;
    }

    // (3) 動き: 1枚ぶん
    inline void DataDrivenFormation::ApplyLayer(const FormationLayer &layer, const Seat &seat, float p,
                                                const FormationContext &ctx, const DroneComponent &drone, Vector3 &local, Vector3 &look) const
    {
        if (layer.type == LayerType::Shiver)
        {
            // 震えだけは攻撃ではなく「予告」の時間の上に置く。撃つ合図で進行度が動き出したら消える。

            // 予告の間は向きが固定されず、触っても当たらない。
            // 震えを攻撃の中に置くと、その両方が変わってしまう。
            if (p > 0.0f) return;

            const float u = TelegraphT(drone);
            if (u < layer.windowBegin || u > layer.windowEnd) return;

            // leadThreshold で「先に動く側」だけを震わせると、どちらが先に来るかの手がかりになる。
            if (SeatPhaseT(layer, seat) > layer.leadThreshold) return;

            // 振れ幅は他の動きと同じく「始まり → 終わり」を緩急つきで進める。
            // 既定の p*p なら、撃つ直前ほど強く震える。

            // 位相は localIndex でずらす。揃えると群れが1枚の板として揺れ、予告に見えない。
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

        // 処理フロー:
        // 1. 移動方式をアセットから写し、モデルの形を用意する
        // 2. 攻撃の進行度を進める
        // 3. 席を決め、待機の形を作る（割付・形）
        // 4. 動きの層を上から順に当てる（動き）
        // 5. ワールドへ移す（配置）
        // 6. 機首と突撃方向を決める

        drone.chargeMode = m_asset.output.chargeMode;

        // もしモデルの形を使うなら表面の点を取り出すための関数
        EnsureShape();

        // --- 進行度 ---

        // 撃つ合図が来るまで進行度を 0 に保ち、予備動作の「形」で待つ。
        // プレイヤーはこの静止した形から回避方向を読む。
        if (m_progressOverride >= 0.0f)
            drone.attackProgress = (std::min)(1.0f, m_progressOverride); // エディタの手送り
        else if (drone.wantsAttack)
            drone.attackProgress = (std::min)(1.0f, drone.attackProgress + ctx.dt / m_asset.attackSeconds);
        else
            drone.attackProgress = 0.0f;

        const float p = drone.attackProgress;

        // --- 席と待機の形 ---

        const Seat seat = AssignSeat(drone);

        Vector3 local = MakeLocal(seat, drone);
        Vector3 look  = Vector3::UnitZ; // ローカルの Forward。Rotate が同じだけ回す

        // --- 動きの層 ---

        for (const FormationLayer &layer : m_asset.layers)
            ApplyLayer(layer, seat, p, ctx, drone, local, look);

        // --- ワールドへ ---

        // AimDirection() は「撃つと決めた瞬間」に固定した向きを返す。
        // 倒れている最中にプレイヤーが横へ動いても形は追わないので、避ければ当たらない。
        const Vector3 forward = ctx.AimDirection();
        Vector3       right   = Vector3::UnitY.Cross(forward);
        if (right.LengthSquared() < 0.001f) right = Vector3::UnitX;
        right.Normalize();

        // origin は回転を受けない。origin は「軸の位置」であって形の一部ではない。
        // そのため、ヒンジの高さ（origin.up）を変えても倒れ方は変わらない。
        Vector3 world = ctx.bossPos + right * (local.x + m_asset.origin.right) + Vector3::UnitY * (local.y + m_asset.origin.up) + forward * (local.z + m_asset.origin.forward);

        if (m_asset.output.clampToGround)
            world.y = (std::max)(world.y, kDroneGroundY + kDroneGroundClearance);

        drone.targetPosition = world;

        // --- 機首と突撃方向 ---

        Vector3 lookWorld;
        switch (m_asset.output.look)
        {
        case LookMode::ToPlayer:
            lookWorld   = ctx.playerPos - world;
            lookWorld.y = 0.0f;
            break;

        // 左右対称の内側。mirror が使う符号の逆を向くので、2本の壁が必ず互いを向く。
        //
        // 座標から中心線を引く方式にはしない。
        // 奥行きのある壁だと先端ほど内を向いて、壁が弧を描いて見えるため（挟み込み2段は前後に長い）。
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

    /**
     * @brief Assets/Formations/*.json を読み、各アセットが名乗る枠へ登録する。
     *
     * ファイル名ではなく JSON 内の "slot" で枠を決める。
     * ファイル名を枠にすると、技の名前を変えた瞬間に保存済みの BT が別の技を指す。
     */
    inline void RegisterAll()
    {
        // 処理フロー:
        // 1. Assets/Formations のファイルを一覧する
        // 2. 1つずつ読み、名乗った枠が使えるか確かめて登録する
        // 3. 空き枠をログへ出す

        auto &registry = FormationRegistry::Instance();

        const std::vector<std::string> files = FileSystem::ListFiles("Assets/Formations", ".json");

        int loaded = 0;
        for (const std::string &path : files)
        {
            FormationAsset asset;
            if (!FormationAssetIO::Load(path, asset)) continue;

            // 欠番は BT から指す手段が無いので、登録しても誰にも呼ばれない。
            if (asset.slot >= kDroneFormationSlotCount ||
                std::string_view(kDroneFormationNames[asset.slot]) == "(欠番)")
            {
                CCL_LOG_ERROR(LogCategory::AI, "[Formation] %s: slot=%u は陣形の枠として定義されていない",
                              path.c_str(), asset.slot);
                continue;
            }

            // 先に登録した C++ 陣形か、同じ枠を名乗る別の JSON が居る。
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

        // 空き枠は BT の候補には出るが、実行すると静止する。
        // どこが空いているかを起動時に必ずログへ出しておかないと、選んだ人が原因に辿り着けない。
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
