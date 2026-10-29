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
//   技ごとの違いはすべて FormationAsset の数値に入っているので、技が増えてもこのファイルは増えない。
//
//   計算はすべて陣形ローカル座標（x = Right / y = Up / z = Forward）で行い、最後に一度だけワールドへ移す。
//
//   C++ 実装のある枠を名乗った JSON は登録時に弾く。同じ技の定義が2か所にあると、片方だけ更新されても気づけない。
// ===========================================================================
#include <string>
#include <vector>

#include "Game/Logics/AI/Drone/Formation/Data/FormationAsset.h"
#include "Game/Logics/AI/Drone/Formation/IFormation.h"

namespace CCL::AI::Formation { class FormationRegistry; }

namespace CCL::AI::Formation::Data
{

    // (1) 割付の出力。1機が入る「席」
    struct Seat
    {
        int side     = 0; // 左右対称の何番目か。mirror が符号を取る
        int tier     = 0; // 段の何番目か。PhaseSource::Group がずらす単位
        int col      = 0; // グループ内の列
        int row      = 0; // グループ内の行
        int colCount = 1; // グループの列数（中央揃えと波の位相に使う）
        int rowCount = 1; // グループの行数
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

        // エディタが実行中の陣形を直接編集するための入口（コピーだと画面に反映されない）
        FormationAsset &GetMutableAsset() { return m_asset; }

        // 1機分の持ち場を出す。(1)割付 → (2)形 → (3)動き → (4)配置 の順に、下の private 関数を呼ぶ
        void Calculate(DroneComponent &drone, const FormationContext &ctx) override;

        const char *DisplayName() const override { return m_asset.name.c_str(); }

        FormationCombat GetCombat() const override { return m_asset.output.combat; }
        bool            UsesShiverTelegraph() const override { return m_asset.output.useShiverTelegraph; }

        bool TryGetTiming(TechniqueTiming &out) const override
        {
            out = {m_asset.telegraphSeconds, m_asset.recoverySeconds, m_asset.restSeconds};
            return true;
        }

        /**
         * @brief 予告の経過秒数と攻撃の進行度を外から固定する（エディタの手送り）。
         *
         * 陣形が attackProgress を毎フレーム書くので、外から DroneComponent へ書いても上書きされる。
         * そこで陣形の側に固定値を持たせる。
         */
        void SetPreview(float telegraphSeconds, float progress)
        {
            m_telegraphOverride = telegraphSeconds;
            m_progressOverride  = progress;
        }
        // エディタが固定した時間を解き、ゲームの時間で進む状態へ戻す
        void ClearPreview()
        {
            m_telegraphOverride = -1.0f;
            m_progressOverride  = -1.0f;
        }

        /**
         * @brief 1グループ分の席の格子の大きさを求める。割付とエディタの表示が同じ式を使う。
         * @param asset       格子の決め方（assign）を持つ陣形アセット。
         * @param totalDrones 陣形に参加する機数。0以下は1機として扱う。
         * @param outCols     1グループの列数を受け取る。
         * @param outRows     1グループの行数を受け取る。
         */
        static void GridSize(const FormationAsset &asset, int totalDrones, int &outCols, int &outRows);

        /**
         * @brief 席ごとの進み t を、始まり→終わりのどこまで進んだかへ変換する（緩急）。エディタのグラフも使う。
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

        // (4) 配置で使う、陣形ローカルの軸をワールドで表したもの。Up は常にワールドの真上
        struct Basis
        {
            Vector3 right;   // ローカルの x
            Vector3 forward; // ローカルの z
        };

        // ---- 進行度 ----

        /**
         * @brief 攻撃の進行度を進めて drone.attackProgress に書き、その値を返す。
         * @param drone 進める機体。wantsAttack が立っている間だけ進み、下りたら 0 に戻る。
         * @param ctx   陣形の計算に渡された情報（dt を使う）。
         * @return 攻撃の進行度（0〜1）。エディタが時間を固定している間は、その値。
         */
        float AdvanceProgress(DroneComponent &drone, const FormationContext &ctx) const;

        // ---- (1) 割付 ----

        /**
         * @brief 機体の通し番号から席（左右・段・列・行）を決める（割付）。
         * @param drone 席を決める機体。localIndex と totalDrones を使う。
         * @return 決めた席。
         */
        Seat AssignSeat(const DroneComponent &drone) const;

        // ---- (2) 形 ----

        /**
         * @brief 形が Model のとき、モデルの表面の点を用意する。モデルのパスが変わったときだけ取り直す。
         *
         * エディタはアセットを直接書き換えるので、変更を知らせる入口が無い。そこで Calculate の頭で毎回パスを見比べる。
         *
         * @note Calculate から呼ぶので、機体のループが直列（ForEach）であることが前提。
         */
        void EnsureShape();

        /**
         * @brief 席を、動かす前の「待機の形」のローカル座標にする（形）。
         * @param seat  座標にする席。
         * @param drone 座標を求める機体。形が Model のときは localIndex でモデル表面の点を選ぶ。
         * @return ローカル座標（陣形の中心からの位置）。
         */
        Vector3 MakeLocal(const Seat &seat, const DroneComponent &drone) const;

        // ---- (3) 動き ----

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
         * @brief この席が「何番目に動くか」を 0〜1 で返す。波も先攻後攻もこの値で表す。
         * @param layer 並べ方（phaseSource・phaseReverse）を持つ動きの層。
         * @param seat  調べる席。
         * @return 0=最初に動く、1=最後に動く。phaseSource が None なら全席 0（同時）。
         */
        float SeatPhaseT(const FormationLayer &layer, const Seat &seat) const;

        /**
         * @brief mirror の層が変換量に掛ける符号を返す。
         * @param seat 調べる席。
         * @return 左右の端で -1 / +1 になる値（sideGroups=2 なら side0=-1 / side1=+1）。sideGroups が1以下なら常に +1。
         */
        float SideSign(const Seat &seat) const;

        /**
         * @brief この機体が予告の中のどこにいるかを返す。
         * @param drone 調べる機体。エディタが時間を固定している間は、その値を使う。
         * @return 予告の長さを 1 とした割合。上限で止めないので、1 を超えたら予告は終わっている。
         */
        float TelegraphT(const DroneComponent &drone) const;

        // ---- (4) 配置と機首 ----

        /**
         * @brief 狙う向きから、陣形ローカルの Right / Forward をワールドで表した基底を作る。
         * @param ctx 陣形の計算に渡された情報（狙う向きを使う）。
         * @return 水平・正規化済みの Right と Forward。
         */
        static Basis MakeBasis(const FormationContext &ctx);

        /**
         * @brief ローカル座標をワールド座標へ移す（ボスの位置 + origin のずれ + 基底）。
         * @param local   動きの層を当て終えたローカル座標。
         * @param basis   MakeBasis で作った基底。
         * @param bossPos 陣形の中心（ボスの位置）。
         * @return 持ち場のワールド座標。clampToGround なら床より下へは行かない。
         */
        Vector3 ToWorld(const Vector3 &local, const Basis &basis, const Vector3 &bossPos) const;

        /**
         * @brief 機首と突撃方向を、output.look の決め方で出す。
         * @param look  動きの層が回したローカルの Forward（Auto で使う）。
         * @param seat  機体の席（Inward で左右を見分ける）。
         * @param basis MakeBasis で作った基底。
         * @param world 持ち場のワールド座標（ToPlayer でプレイヤーへの向きを出す）。
         * @param ctx   陣形の計算に渡された情報（プレイヤーの位置を使う）。
         * @return 正規化済みの向き。
         */
        Vector3 ResolveLook(const Vector3 &look, const Seat &seat, const Basis &basis,
                            const Vector3 &world, const FormationContext &ctx) const;

        // ---- 状態 ----

        FormationAsset m_asset; // この陣形の定義

        static constexpr int kMaxShapePoints = 1024; // 取り出す点の上限。超えた機は先頭の点から使い回す

        std::vector<Vector3> m_shapePoints;        // モデル表面の点（モデル空間・最遠点の順）
        std::string          m_shapeModelPath;     // m_shapePoints を取り出したモデル
        bool                 m_shapeTried = false; // 取り出しを試したか。読めなかったモデルを毎フレーム読み直さない

        // --- エディタの手送り（負なら固定しない）---
        float m_telegraphOverride = -1.0f; // 予告の経過 [秒]
        float m_progressOverride  = -1.0f; // 攻撃の進行度 0〜1
    };

    /**
     * @brief Assets/Formations/*.json を読み、各アセットが名乗る枠（JSON の "slot"）へ登録する。
     *
     * ファイル名を枠にしないのは、技の名前を変えた瞬間に保存済みの BT が別の技を指すため。
     *
     * @param registry 登録先（World の Resource）。
     * @note C++ 陣形を登録した後に呼ぶ。先に呼ぶと、C++ 実装のある枠を名乗った JSON を弾けない。
     */
    void RegisterAll(FormationRegistry &registry);

} // namespace CCL::AI::Formation::Data
