#pragma once
// ===========================================================================
// 概要: チュートリアルで教える項目の表（kTutorialSteps）。
//
// 責務:
//   - 項目ごとに、属する章・ヒントのアイコンと文言・達成の条件と必要な量・時間切れを持つ
//   - 並び順で教える順番を決める
//   - 章ごとに、ボスを戦わせるかを決める
//
// 備考: 教える中身を変えるときは、この表だけを書き換える。
//       行は指定初期化子（.text = ...）で書き、使わない列は省いて既定値のままにする。
//       達成の条件の判定（何をしたら達成か）は TutorialSystem が TutorialGoal ごとに持つ。
//       条件の種類を増やしたら、TutorialSystem::Update の switch にも足すこと。
// ===========================================================================
#include "Game/UI/Input/InputIconComponent.h" // ヒントのアイコンの種類

#include <cstdint>
#include <iterator> // std::size

// 章。並び順がそのまま教える順番になる
enum class TutorialChapter : uint8_t
{
    Move,  // 章1 動かす（移動・視点・ダッシュ・ジャンプ）
    Slash, // 章2 斬る（ロックオン・攻撃・コンボ・ダッシュ攻撃）
    Read,  // 章3 読んで避けて斬り込む（陣形を読む・ジャスト回避・ラッシュ）
};

// 表示名（デバッグ表示用）。名前の二重管理を避けるため enum の隣に置く
inline constexpr const char *kTutorialChapterNames[] = {
    "章1 動かす",
    "章2 斬る",
    "章3 読んで避けて斬り込む",
};

// この章でボスを戦わせるか。章1・2は攻撃を出さずに待たせ、章3だけ手加減の BT で攻めさせる。
// 操作を覚えている最中に被弾すると、何を教わっているのか分からなくなるため
inline constexpr bool IsBossFighting(TutorialChapter chapter) { return chapter == TutorialChapter::Read; }

// 達成の条件の種類。項目ごとに、何をしたら次へ進むかを表す。amount の意味は種類ごとに違う
enum class TutorialGoal : uint8_t
{
    Wait,      // 条件なし。時間切れ（timeout）だけで進む
    Move,      // 水平に amount m 動く（ダッシュで動いた分も数える）
    Look,      // カメラを左右に合わせて amount 度回す（往復しても足していく）
    Dash,      // 回避ダッシュを amount 回出す
    Jump,      // amount 段目のジャンプを跳ぶ（1 = 普通のジャンプ、2 = 2段ジャンプ）
    LockOn,    // 敵をロックオンする
    AttackHit, // プレイヤーの攻撃を amount 回当てる（0 なら1回）
    AnimState, // プレイヤーのアニメが animState のステートに入る（コンボの2段目・ダッシュ攻撃など、どの攻撃が出たかを見る）
    JustEvade, // ジャスト回避を成立させる。陣形が組まれ始めてからヒントを出し、突撃の直前に補助スローを掛ける
};

// 教える項目1つぶん
struct TutorialStepDef
{
    TutorialChapter chapter    = TutorialChapter::Move;  // 属する章
    const char     *name       = "";                     // 名前（デバッグ表示用）
    InputIconKind   icon       = InputIconKind::Action;  // ヒントのアイコンの種類。移動と視点は固定のアイコン（Move / Camera）
    const char     *action     = nullptr;                // icon が Action のときに出すアクション名（InputConfig.json の Name）
    const char     *text       = "";                     // ヒントの1行（UTF-8）。空ならヒントを出さない
    const char     *assistText = nullptr;                // 補助スロー中に差し替える1行。nullptr なら差し替えない
    TutorialGoal    goal       = TutorialGoal::Wait;     // 達成の条件
    float           amount     = 0.0f;                   // 達成に必要な量（意味は TutorialGoal を参照）
    const char     *animState  = nullptr;                // goal が AnimState のとき、入れば達成になるステート名（プレイヤーのアニメグラフの名前）
    float           timeout    = 0.0f;                   // 達成しなくても、この秒数（実時間）で次へ進む。0 なら達成するまで待つ
};

// 教える項目の表。並び順がそのまま教える順番になる
inline constexpr TutorialStepDef kTutorialSteps[] = {
    // --- 章1 動かす ---

    // シーンに入ってすぐは明転の途中なので、少し待ってから始める。ボスもこの間は待たせる
    {.chapter = TutorialChapter::Move, .name = "WaitBattle", .goal = TutorialGoal::Wait, .timeout = 1.0f},

    {.chapter = TutorialChapter::Move,
     .name    = "Move",
     .icon    = InputIconKind::Move,
     .text    = "移動する",
     .goal    = TutorialGoal::Move,
     .amount  = 10.0f},

    {.chapter = TutorialChapter::Move,
     .name    = "Look",
     .icon    = InputIconKind::Camera,
     .text    = "視点を動かして、周りを見渡す",
     .goal    = TutorialGoal::Look,
     .amount  = 90.0f},

    // 2回出させて、一瞬で大きく動けることと、出した後も走り続けることを体で覚えてもらう
    {.chapter = TutorialChapter::Move,
     .name    = "Dash",
     .action  = "Dash",
     .text    = "回避ダッシュ。一瞬で大きく動ける",
     .goal    = TutorialGoal::Dash,
     .amount  = 2.0f},

    {.chapter = TutorialChapter::Move,
     .name    = "Jump",
     .action  = "Jump",
     .text    = "ジャンプ",
     .goal    = TutorialGoal::Jump,
     .amount  = 1.0f},

    {.chapter = TutorialChapter::Move,
     .name    = "DoubleJump",
     .action  = "Jump",
     .text    = "空中でもう一度押すと、2段ジャンプ",
     .goal    = TutorialGoal::Jump,
     .amount  = 2.0f},

    // --- 章2 斬る ---

    // 先にロックオンさせる。ボスを画面に捉え続けられるので、この後の攻撃で見失わない
    {.chapter = TutorialChapter::Slash,
     .name    = "LockOn",
     .action  = "LockOn",
     .text    = "ボスをロックオン。視点がボスを追い続ける",
     .goal    = TutorialGoal::LockOn},

    {.chapter = TutorialChapter::Slash,
     .name    = "Attack",
     .action  = "Attack",
     .text    = "ボスに近づいて、攻撃を3回当てる",
     .goal    = TutorialGoal::AttackHit,
     .amount  = 3.0f},

    // コンボは Attack_1 → Attack_2 の2段（アニメグラフの AttackCombo）。2段目に入れば続けて押せている
    {.chapter   = TutorialChapter::Slash,
     .name      = "Combo",
     .action    = "Attack",
     .text      = "続けて押すと、連続攻撃",
     .goal      = TutorialGoal::AnimState,
     .animState = "Attack_2"},

    // ダッシュ攻撃は、ダッシュ中か走っている間に攻撃すると出る（PlayerAnimParamSyncSystem の DashAttackReady）
    {.chapter   = TutorialChapter::Slash,
     .name      = "DashAttack",
     .action    = "Attack",
     .text      = "ダッシュ中か走っている間に攻撃すると、ダッシュ攻撃",
     .goal      = TutorialGoal::AnimState,
     .animState = "DashAttack"},

    // --- 章3 読んで避けて斬り込む ---

    // 回避: 形を見る前に「回避」と言っても、何を避けるのか分からないので、陣形が組まれ始めてからヒントを出す
    {.chapter    = TutorialChapter::Read,
     .name       = "ReadAndEvade",
     .action     = "Dash",
     .text       = "陣形の形を読み、突っ込んでくる瞬間に回避",
     .assistText = "今だ！ 回避",
     .goal       = TutorialGoal::JustEvade},

    // 攻撃: ジャスト回避の接近でボスの近くへ運ばれている間に促す。当てなくても時間で次へ進める
    // （ボスに背を向けて回避した、などで届かないことがあり、そこで止めると先を教えられない）
    {.chapter = TutorialChapter::Read,
     .name    = "Strike",
     .action  = "Attack",
     .text    = "ジャスト回避でボスの懐へ。そのまま攻撃",
     .goal    = TutorialGoal::AttackHit,
     .timeout = 5.0f},
};

// 項目の数。TutorialState.step がこれに達したら教え終えた
inline constexpr uint8_t kTutorialStepCount = static_cast<uint8_t>(std::size(kTutorialSteps));
