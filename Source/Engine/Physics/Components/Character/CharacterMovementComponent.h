#pragma once
// ===========================================================================
// 概要: キャラクターをどう動かしてほしいかを、動かす側（JoltCharacterUpdateSystem）へ渡す窓口
//       プレイヤーもボスも、移動はすべてここを通す
//
// 備考: 依頼が重なったときは、上の段ほど優先する
//         段 | 依頼                       | 効くもの           | ノックバック
//         1  | 移動の予約 / use3DVelocity | 水平も上下も丸ごと | 足さない
//         2  | isRootMotionActive         | 水平               | 足す
//         3  | overrideSpeed              | 水平の速さ         | 足す
//         4  | （依頼なし）walkSpeed      | 水平の速さ         | 足す
//       speedMultiplier は 3・4、moveIntensity は 4 にだけ掛かる。1 の間はジャンプと重力も効かない
//       決まった軌道の技は MoveTo / Hold / MoveInDirection で予約し、終わるまで物理が運ぶ
//       overrideSpeed / use3DVelocity / jumpImpulse は1フレーム限り。効かせたいフレームに毎回書く
// ===========================================================================
#include <SimpleMath.h>
#include <cstdint>
#include <optional>

// MoveTo の進み方
enum class MoveEase : uint8_t
{
    Linear,  // 等速
    EaseOut, // 速く動き始めて、終点で止まる
    EaseIn,  // ゆっくり動き始めて、終点で一番速くなる
};

// MoveTo の細かい指定。指定しなければ、等速で浮かない
struct MoveToOptions
{
    float                height             = 0.0f;             // 途中で浮く高さ [m]。中間で最大、両端で 0
    MoveEase             ease               = MoveEase::Linear; // 進み方
    std::optional<float> verticalStartSpeed = std::nullopt;     // 縦の始めの速さ [m/s]（+ が上）。指定すると、高さだけ等加速度で動く
                                                                //   始めにこの速さで出て、時間ちょうどに終点の高さへ着く（重力で投げた動き）
                                                                //   指定しなければ、高さも ease で進む
    float                maxSpeed           = 0.0f;             // 速さの上限 [m/s]。0 で上限なし。引っかかった遅れを一気に取り戻させたくないときだけ付ける
};

// 予約の種類
enum class MoveReservationKind : uint8_t
{
    ToPoint,     // MoveTo。終点まで運ぶ（位置で動かす）
    Hold,        // Hold。その場に留める
    InDirection, // MoveInDirection。決めた向きへ走る（速度で動かす。壁に沿って滑る）
};

// 予約した移動1つ分の状態
// 書くのは CharacterMovementComponent の予約の関数と JoltCharacterUpdateSystem だけ
struct CharacterMoveReservation
{
    MoveReservationKind          kind       = MoveReservationKind::ToPoint;
    DirectX::SimpleMath::Vector3 start      = {0, 0, 0}; // 始点。予約後の最初の物理更新で取る
    DirectX::SimpleMath::Vector3 target     = {0, 0, 0}; // 終点（ワールド）
    DirectX::SimpleMath::Vector3 direction  = {0, 0, 0}; // 走る向き（InDirection）
    float                        startSpeed = 0.0f;      // 走り始めの速さ [m/s]（InDirection）
    float                        endSpeed   = 0.0f;      // 走り終わりの速さ [m/s]（InDirection）
    float                        duration   = 0.0f;      // かける時間 [秒]
    float                        elapsed    = 0.0f;      // 経過 [秒]
    MoveToOptions                options;
    bool                         active   = false; // 運んでいる最中か
    bool                         started  = false; // 始点を取ったか
    bool                         stopNext = false; // 次の物理更新で速度を 0 にする（終点で滑らない）
    float                        leftover = 0.0f;  // 終わったときに余った時間 [秒]。続けて予約した次へ引き継ぐ
};

// 予約した移動（MoveTo / Hold）で、進み具合 u（0=始め, 1=終わり）のときにいるべき位置を返す
// 物理（JoltCharacterUpdateSystem）とデバッグ描画が同じ式を使う。2か所で持つと、描いた線と実際の動きが食い違う
inline DirectX::SimpleMath::Vector3 ReservationPlannedPosition(const CharacterMoveReservation &r, float u)
{
    // 進み方に合わせて、u を進んだ割合に直す
    // 1-(1-u)^2 は u=1 で傾き 0、u^2 は u=0 で傾き 0
    float eased = u;
    if (r.options.ease == MoveEase::EaseOut) eased = 1.0f - (1.0f - u) * (1.0f - u);
    if (r.options.ease == MoveEase::EaseIn) eased = u * u;

    DirectX::SimpleMath::Vector3 plannedPos = DirectX::SimpleMath::Vector3::Lerp(r.start, r.target, eased);

    // 縦の始めの速さがあれば、高さだけ等加速度で動かす
    // y = 始めの高さ + v*t + a*t^2/2。a は「時間ちょうどに終点の高さへ着く」ように決める
    if (r.options.verticalStartSpeed && r.duration > 0.0f)
    {
        const float startSpeed   = *r.options.verticalStartSpeed;
        const float elapsedTime  = u * r.duration;
        const float acceleration = 2.0f * ((r.target.y - r.start.y) - startSpeed * r.duration) / (r.duration * r.duration);
        plannedPos.y = r.start.y + startSpeed * elapsedTime + 0.5f * acceleration * elapsedTime * elapsedTime;
    }

    // 途中で浮かせる。4h*u(1-u) は両端で 0
    plannedPos.y += 4.0f * r.options.height * u * (1.0f - u);
    return plannedPos;
}

struct CharacterMovementComponent
{
    // --- 移動の予約 ---
    
    // 予約が終わった次のフレームに続けて予約すると、止まらずにつながる
    // 余った時間も引き継ぐので、段を何回つないでも技の長さがずれない
    CharacterMoveReservation reservation;

    // --- 移動の向き（入力・AI が毎フレーム書く）---

    DirectX::SimpleMath::Vector3 desiredVelocity = {0, 0, 0}; // 進みたい向き（正規化済み）。速さは持たない
    float                        moveIntensity   = 1.0f;      // 歩きの強さ 0〜1（スティックの傾き）

    // --- 1フレーム限りの依頼（物理が読んだら既定値へ戻す）---

    float                        overrideSpeed = -1.0f;              // 今だけこの速さ [m/s]。負で依頼なし
    bool                         use3DVelocity = false;              // velocity3D をそのまま速度に使う
    DirectX::SimpleMath::Vector3 velocity3D    = {0.0f, 0.0f, 0.0f}; // use3DVelocity 中の速度 [m/s]
    bool                         jumpImpulse   = false;              // このフレームに跳ぶと決まった

    // --- 持続する係数（毎フレーム書く。物理は戻さない）---

    float speedMultiplier = 1.0f; // 速度倍率。1 で等倍

    // --- ルートモーション（RootMotionSystem だけが書く）---

    bool                         isRootMotionActive = false;              // ルートモーション中か
    DirectX::SimpleMath::Vector3 rootMotionVelocity = {0.0f, 0.0f, 0.0f}; // アニメから求めた速度 [m/s]

    // --- 結果（JoltCharacterUpdateSystem だけが書く）---

    // 物理へ渡した水平速度 [m/s]。慣性込み、壁に当たる前の値
    DirectX::SimpleMath::Vector3 currentVelocity = {0, 0, 0};



    // target まで duration 秒かけて運ぶ予約をする
    // 始点は次の物理更新の位置。着いたら速度を 0 にして止まる
    void MoveTo(const DirectX::SimpleMath::Vector3 &target, float duration, const MoveToOptions &options = {})
    {
        BeginReservation(MoveReservationKind::ToPoint, duration);
        reservation.target  = target;
        reservation.options = options;
    }

    // その場に duration 秒留める予約をする（重力も効かない）
    // 空中で止まって狙う、頂点で浮くなどに使う
    void Hold(float duration) { BeginReservation(MoveReservationKind::Hold, duration); }

    // direction へ duration 秒走る予約をする。速さは speed から endSpeed へ直線で変える
    // 行き先の点を持たないので、壁に当たれば沿って滑りながら走り続ける
    void MoveInDirection(const DirectX::SimpleMath::Vector3 &direction, float speed, float duration, float endSpeed)
    {
        BeginReservation(MoveReservationKind::InDirection, duration);
        reservation.direction  = direction;
        reservation.startSpeed = speed;
        reservation.endSpeed   = endSpeed;
    }

    // 予約を取り消す。止めずに、それまでの速度の慣性を残す
    void CancelMove() { reservation = {}; }

    // 予約した移動の最中か。終わったら false
    bool IsMoving() const { return reservation.active; }

    // 新しい予約を始める。終わったばかりの予約の余った時間を引き継ぐ
    void BeginReservation(MoveReservationKind kind, float duration)
    {
        const float carry    = reservation.active ? 0.0f : reservation.leftover;
        reservation          = {};
        reservation.kind     = kind;
        reservation.duration = duration;
        reservation.elapsed  = carry;
        reservation.active   = true;
    }

};
