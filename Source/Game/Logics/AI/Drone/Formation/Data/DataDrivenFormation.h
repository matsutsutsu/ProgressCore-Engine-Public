#pragma once
// ===========================================================================
// 概要: FormationAsset（JSON）を解釈して持ち場を出す陣形クラスと、その登録処理
//
// 責務:
//   - (1)割付 (2)形 (3)動き (4)配置 の4段を1回だけ実装する
//   - Assets/Formations/*.json を読み、各アセットが名乗る枠へ登録する
//   - エディタの手送り（予告の経過・進行度の固定）を受ける
//
// 備考: 技ごとの違いは FormationAsset の数値に入るので、技が増えてもこのファイルは増えない
//       計算は陣形ローカル座標（x = Right / y = Up / z = Forward）で行い、最後に一度だけワールドへ移す
//       C++ 実装のある枠を名乗った JSON は弾く。同じ技の定義が2か所にあると、片方の更新に気づけない
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

        // エディタがモデルのパスを変えていたら、モデルの形を取り直す
        void BeginFrame(CCL::ECS::World &world) override { EnsureShape(); }

        // 1機分の持ち場を出す。(1)割付 → (2)形 → (3)動き → (4)配置 の順に、下の private 関数を呼ぶ
        void Calculate(DroneComponent &drone, const FormationContext &ctx) override;

        const char *DisplayName() const override { return m_asset.name.c_str(); }

        FormationCombat GetCombat() const override { return m_asset.output.combat; }
        ChargeMode      GetChargeMode() const override { return m_asset.output.chargeMode; }
        bool            UsesShiverTelegraph() const override { return m_asset.output.useShiverTelegraph; }

        // 形が動き切ってから kHitGraceSeconds 経ったら閉じる
        // 動き切った形は戻すまで残るので、開けたままだと吹き飛んだ先で何度も当たる
        bool IsHitWindowOver(const DroneComponent &drone) const override { return drone.attackProgress >= HitWindowEnd(); }

        bool TryGetTiming(TechniqueTiming &out) const override
        {
            out = {m_asset.telegraphSeconds, m_asset.recoverySeconds, m_asset.restSeconds};
            return true;
        }

        // 予告の経過秒数と攻撃の進行度を外から固定する（エディタの手送り）
        // 陣形が attackProgress を毎フレーム書くので、DroneComponent へ書いても上書きされる
        // そこで陣形の側に固定値を持たせる
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

        // 1グループ分の席の格子の大きさを求める。割付とエディタの表示が同じ式を使う
        // totalDrones が 0 以下なら1機として扱う
        static void GridSize(const FormationAsset &asset, int totalDrones, int &outCols, int &outRows);

        // 席ごとの進み t（0〜1）を、始まりから終わりのどこまで進んだか（0〜1）に変換する
        // キーのないカーブは easeIn（t*t）として扱う。エディタのグラフも使う
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

        // --- 進行度 ---

        // 形が動き切ってからも当たる時間 [秒]
        // 動き切った瞬間に閉じると、挟み込みが閉じる中心線（＝狙ったプレイヤーの位置）だけ当たらない
        static constexpr float kHitGraceSeconds = 0.1f;

        // 判定を閉じる進行度。1 を超えた分が kHitGraceSeconds にあたる
        float HitWindowEnd() const { return 1.0f + kHitGraceSeconds / m_asset.attackSeconds; }

        // 攻撃の進行度を進めて drone.attackProgress に書き、形に使う値（0〜1）を返す
        // drone.attackProgress は判定を閉じる所（HitWindowEnd）まで 1 を超えて進む
        // wantsAttack が下りたら 0 に戻る。エディタが時間を固定している間は、その値を使う
        float AdvanceProgress(DroneComponent &drone, const FormationContext &ctx) const;

        // --- (1) 割付 ---

        // 機体の通し番号（localIndex / totalDrones）から席を決める
        Seat AssignSeat(const DroneComponent &drone) const;

        // --- (2) 形 ---

        // 形が Model のとき、モデルの表面の点を用意する。パスが変わったときだけ取り直す
        // エディタはアセットを直接書き換えて知らせないので、BeginFrame で毎フレーム見比べる
        void EnsureShape();

        // 席を、動かす前の「待機の形」のローカル座標にする
        // 形が Model なら、localIndex でモデル表面の点を選ぶ
        Vector3 MakeLocal(const Seat &seat, const DroneComponent &drone) const;

        // --- (3) 動き ---

        // 動きの層を1枚、席の位置（local）と機首（look）に足し込む
        // p は攻撃の進行度（0〜1）。震えの層は p が 0 の間（予告中）だけ効く
        void ApplyLayer(const FormationLayer &layer, const Seat &seat, float p,
                        const FormationContext &ctx, const DroneComponent &drone,
                        Vector3 &local, Vector3 &look) const;

        // 攻撃の進行度 p における、この層の変換量を返す
        // 層の窓の中で席の順番だけ開始をずらし、from から to へ緩急つきで進める
        // 単位は Rotate なら度、Translate なら m、Shiver なら振れ幅の m
        float LayerValue(const FormationLayer &layer, const Seat &seat, float p) const;

        // この席が何番目に動くかを 0〜1 で返す（0 が最初）。波も先攻後攻もこの値で表す
        // phaseSource が None なら全席 0 で、同時に動く
        float SeatPhaseT(const FormationLayer &layer, const Seat &seat) const;

        // mirror の層が変換量に掛ける符号。sideGroups が2なら side0 が -1、side1 が +1
        // sideGroups が1以下なら常に +1
        float SideSign(const Seat &seat) const;

        // この機体が予告のどこにいるかを、予告の長さを 1 とした割合で返す
        // 上限で止めないので、1 を超えたら予告は終わっている
        float TelegraphT(const DroneComponent &drone) const;

        // --- (4) 配置と機首 ---

        // 狙う向きから、陣形ローカルの Right / Forward をワールドで表す（水平・正規化済み）
        static Basis MakeBasis(const FormationContext &ctx);

        // ローカル座標をワールドへ移す（ボスの位置 + origin のずれ + 基底）
        // clampToGround なら床より下へは行かない
        Vector3 ToWorld(const Vector3 &local, const Basis &basis, const Vector3 &bossPos) const;

        // 機首と突撃方向を、output.look の決め方で出す。戻り値は正規化済み
        Vector3 ResolveLook(const Vector3 &look, const Seat &seat, const Basis &basis,
                            const Vector3 &world, const FormationContext &ctx) const;

        // --- 状態 ---

        FormationAsset m_asset; // この陣形の定義

        static constexpr int kMaxShapePoints = 1024; // 取り出す点の上限。超えた機は先頭の点から使い回す

        std::vector<Vector3> m_shapePoints;        // モデル表面の点（モデル空間・最遠点の順）
        std::string          m_shapeModelPath;     // m_shapePoints を取り出したモデル
        bool                 m_shapeTried = false; // 取り出しを試したか。読めなかったモデルを毎フレーム読み直さない

        // --- エディタの手送り（負なら固定しない）---
        float m_telegraphOverride = -1.0f; // 予告の経過 [秒]
        float m_progressOverride  = -1.0f; // 攻撃の進行度 0〜1
    };

    // Assets/Formations/*.json を読み、各アセットが名乗る枠（JSON の "slot"）へ登録する
    // ファイル名を枠にしないのは、技の名前を変えた瞬間に保存済みの BT が別の技を指すため
    // C++ 陣形の登録より後に呼ぶ。先に呼ぶと、C++ 実装のある枠を名乗った JSON を弾けない
    void RegisterAll(FormationRegistry &registry);

}
