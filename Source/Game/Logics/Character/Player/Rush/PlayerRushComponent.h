#pragma once
#include <cstdint>
#include <SimpleMath.h>
#include "Engine/ECS/Common/CCL_Types.h"

// ラッシュのフェーズ。成立した直後から順に進む
// 世界のスローはとどめが当たるまでフェーズによらず一定。プレイヤーの時計は待機の間だけ遅くする
enum class RushPhase : uint8_t
{
    Locked = 0, // 回避離脱。減速しながら離れる。操作は受け付けない
    Ready,      // 待機。攻撃の入力だけ受け付ける。プレイヤーも遅くなり、回避の姿勢のまま止まって見える
    Attack,     // 突っ込む姿勢で間合いを詰めて、着地の斬りで1回斬る
    Flurry,     // 連撃。押すたびに斬り、決まった数の後にとどめ
};

// ジャスト回避の成立後の「ラッシュ」の設定値と状態。状態を書くのは PlayerRushSystem
struct PlayerRushComponent
{
    // --- 世界のスロー ---

    float worldScale    = 0.1f; // 世界の時間倍率（1 で等速）
    float playerScale   = 1.0f; // プレイヤーの時間倍率（1 で実時間）
    float slowRecover   = 0.4f; // 終了後、等速へ戻るまで（秒）
    float finishRecover = 0.4f; // とどめが当たってから等速へ戻るまで（秒）。外したときは終了後の戻りに使う

    // 待機の間は、プレイヤーの時間倍率を playerScale から readyPlayerScale へ絞る
    // 回避のアニメが終わって待機の棒立ちに戻らないよう、回避の姿勢のまま止めて見せる
    // 待機の長さ（readyDuration）は実時間で数えるので、絞っても判断の時間は伸びない
    float readyPlayerScale = 0.1f;  // 待機中のプレイヤーの時間倍率（1 で実時間）
    float readySlowInTime  = 0.15f; // 待機に入ってから readyPlayerScale まで絞りきる時間（秒）。始めは速く、終わりほどゆっくり寄せる

    // --- フェーズの長さ ---

    float lockedDuration = 0.25f; // 回避離脱の長さ（秒）
    float readyDuration  = 0.40f; // 待機の長さ（秒）
    float attackDuration = 0.35f; // 斬ってから連撃へ移るまで（秒）

    // 詰めの保険。壁に阻まれて届かないときに、ここで終える
    // 普通に詰めるときには効かない長さにする（closeInSpeed で startMaxDistance＋離脱の分を詰める時間より長く）
    float closeInMaxDuration = 2.0f; // （秒）

    // --- 始められる範囲 ---

    // ラッシュ時の、相手との距離がこの値より離れていたら、ラッシュを始めない（ジャスト回避だけになる）
    float startMaxDistance = 80.0f; // （m）

    // 範囲外でジャスト回避したときの短いスロー。世界だけ遅くし、プレイヤーは実時間で動ける
    // 終わった後は、ラッシュの後と同じく slowRecover かけて等速へ戻る
    // ダッシュ（約0.16秒）が終わった後も、遅い世界を自分だけ動ける時間がはっきり残る長さにする
    float evadeSlowScale    = 0.1f; // 世界の時間倍率（1 で等速）。ラッシュの worldScale に近いほど同じ手応えになる
    float evadeSlowDuration = 1.0f; // 長さ（秒）

    // 成立した直後は、プレイヤーも世界と一緒に遅くして手応えを出す
    // この秒数をかけて、世界と同じ遅さから実時間へ寄せる。最初ほどゆっくり離れる（0 で最初から実時間）
    float evadeSharedSlowTime = 0.3f; // （秒）

    // --- 回避離脱 ---

    float leapSpeed = 120.0f; // 離脱の初速（m/秒）

    // --- 間合い詰め ---

    // 相手の手前 strikeDistance の位置（斬る位置）へ、等速で寄せる。斬った後も strikeTrackTime の間は寄せ続ける
    // 出した瞬間の距離だけで決めると、逃げる相手や、少し手前で出した斬りが届かない
    float closeInSpeed        = 90.0f; // 寄せる速さ（m/秒）。加速なしの等速
    float closeInStopDistance = 3.0f;  // 斬る位置まで残りこの距離になったら斬りを出す（m）。斬りの振り始めの分だけ前にずらす
    float strikeTrackTime     = 0.22f; // 斬った後に寄せ続ける長さ（秒）。Rush_Dive_End の当たり判定の終わりに合わせる
    float strikeDistance      = 1.5f;  // 斬る位置の、相手からの距離（m）。これより近ければ寄せない

    // --- 連撃 ---

    int   rushStepCount     = 4;     // とどめの前に出す斬りの数
    float rushInputBuffer   = 0.5f;  // 押した攻撃を覚えておく時間（秒）
    float flurryMaxDuration = 10.0f; // 連撃の上限（秒）

    // --- 無敵 ---

    float invincibleRefresh = 0.1f; // ラッシュ中に保つ無敵の残り（秒）

    // --- 実行時の状態 ---

    bool      isActive      = false;             // ラッシュ中か
    bool      isRecovering  = false;             // 終了後、スローが戻るのを待っているか
    float     evadeSlowTimer = 0.0f;             // 範囲外の回避の短いスローの残り（秒）。0 なら出ていない
    float     slowWorldScale = 0.1f;             // 今かけている世界スローの倍率。ラッシュなら worldScale、範囲外の回避なら evadeSlowScale
    RushPhase phase         = RushPhase::Locked; // 今のフェーズ
    float     timer         = 0.0f;              // 今のフェーズの残り（秒）。回避離脱では使わない
    bool      attackFired   = false;             // Attack で斬りを出したか
    float     strikeTrackTimer = 0.0f;           // 斬った後の吸い付きの残り（秒）。0 なら寄せていない
    int       rushStep      = 0;                 // 連撃で入った斬りの数。詰めの斬りは含まない
    bool      finishEntered = false;             // とどめのアニメに入ったか
    bool      finishHit     = false;             // とどめが当たったか。当たってからは、プレイヤーも世界と一緒のスローで動く

    uint32_t lastAnimState = 0; // 前のステップのアニメのステート

    float evadeFaceYaw = 0.0f; // 回避離脱と待機の間に向く方（ラジアン、0 で +Z）

    DirectX::SimpleMath::Vector3 evadeDir      = {0, 0, 0};                 // 離脱の向き（正規化済み）
    DirectX::SimpleMath::Vector3 closeInDir    = {0, 0, 0};                 // 詰める向き（正規化済み）
    CCL::ECS::EntityID           closeInTarget = CCL::ECS::InvalidEntityID; // ラッシュの相手（避けた攻撃の持ち主）。開始時に決める。分からないときと死んだら無効
};



// 相手がラッシュを始められる範囲にいるか
// 詰めは水平に動くので、高さは見ずに水平距離で比べる。ラッシュの開始とロックオンの照準の色で同じ判定を使う
inline bool IsInRushStartRange(const PlayerRushComponent          &rush,
                               const DirectX::SimpleMath::Vector3 &playerPos,
                               const DirectX::SimpleMath::Vector3 &targetPos)
{
    const float dx = targetPos.x - playerPos.x;
    const float dz = targetPos.z - playerPos.z;
    return dx * dx + dz * dz <= rush.startMaxDistance * rush.startMaxDistance;
}
