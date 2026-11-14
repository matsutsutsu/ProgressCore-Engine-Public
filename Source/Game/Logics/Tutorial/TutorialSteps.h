#pragma once
// ===========================================================================
// 概要: チュートリアルで教える項目の表。並び順が教える順番になる
//
// 備考: 行は指定初期化子（.text = ...）で書き、使わない列は省く
//       達成の量（距離・回数・時間切れ）は TutorialSystem.cpp が持つ。文に数を書いた項目は、その定数と合わせる
//       項目を増やしたら、TutorialGoal と TutorialSystem::EvaluateGoal の switch にも足す
// ===========================================================================
#include "Game/UI/Input/InputIconComponent.h"

#include <cstdint>
#include <iterator>

// 達成の条件の種類。表の行と1対1で、TutorialSystem::EvaluateGoal の switch で判定する
enum class TutorialGoal : uint8_t
{
    Wait,       // 明転が終わるまで待つ（時間で進む）
    Move,       // 移動する
    Look,       // 視点を回す
    Dash,       // 回避ダッシュを出す
    Jump,       // ジャンプする
    DoubleJump, // 2段ジャンプする
    LockOn,     // 敵をロックオンする
    Attack,       // 攻撃を当てる
    Combo,        // 攻撃の連打で、本線のコンボを最後の段まで出し切る
    ComboForward, // コンボ中にボスの方へ倒して攻撃（跳び斬り）、続けて押す（叩きつけ）
    ComboBack,    // コンボ中にボスと反対へ倒して攻撃（退き斬り）、続けて押す（踏み込み斬り）
    DashAttack,   // ダッシュ攻撃を出し、続けて押してコンボへつなぐ
    AirAttack,    // 空中で攻撃し、続けて押して急降下斬りを出す
    JustEvade,  // ジャスト回避を成立させる
    Strike,     // ジャスト回避の後に攻撃を当てる（当てなくても時間で進む）
    Rush,       // ラッシュの説明を読む（時間で進む。攻撃は求めない）
    FreePlay,   // 自由に練習する。終わらず、ポーズメニューからタイトルへ戻って抜ける
};

// 教える項目1つぶん
struct TutorialStepDef
{
    const char   *name        = "";                    // 名前（デバッグ表示用）
    InputIconKind icon        = InputIconKind::Action; // ヒントのアイコンの種類。移動と視点は固定のアイコン（Move / Camera）
    const char   *action      = nullptr;               // icon が Action のときに出すアクション名（InputConfig.json の Name）
    const char   *action2     = nullptr;               // 同時押しを教える2つ目のアクション名
    const char   *text        = "";                    // ヒントの1行（UTF-8）。空ならヒントを出さない
    const char   *assistText  = nullptr;               // 補助スローの「今だ」の段階で差し替える1行。nullptr なら差し替えない
    TutorialGoal  goal        = TutorialGoal::Wait;    // 達成の条件の種類
    bool          bossAttacks = false;                 // この項目の間、ボスに手加減の攻撃をさせるか
    float         holdTime    = 1.2f;                  // 達成してから次の項目へ進むまでの余韻（秒・実時間）
};

// 教える項目の表。並び順がそのまま教える順番になる
inline constexpr TutorialStepDef kTutorialSteps[] = {
    // --- 動かす ---

    // シーンに入ってすぐは明転の途中なので、少し待ってから始める。ボスもこの間は待たせる
    {.name = "WaitBattle",
     .goal = TutorialGoal::Wait,
     .holdTime = 0.0f},

    {.name = "Move",
     .icon = InputIconKind::Move,
     .text = "移動する",
     .goal = TutorialGoal::Move},

    {.name = "Look",
     .icon = InputIconKind::Camera,
     .text = "視点を動かして、周りを見渡す",
     .goal = TutorialGoal::Look},

    {.name   = "Dash",
     .action = "Dash",
     .text   = "ダッシュ。一瞬で大きく動き、そのまま走りに移る",
     .goal   = TutorialGoal::Dash},

    {.name   = "Jump",
     .action = "Jump",
     .text   = "ジャンプ",
     .goal   = TutorialGoal::Jump},

    {.name   = "DoubleJump",
     .action = "Jump",
     .text   = "空中でもう一度押すと、2段ジャンプ",
     .goal   = TutorialGoal::DoubleJump},

    // --- 斬る ---

    // 先にロックオンさせる。ボスを画面に捉え続けられるので、この後の攻撃で見失わない
    {.name   = "LockOn",
     .action = "LockOn",
     .text   = "ボスをロックオン。もう一度押すと解除（押すたびに切り替え）",
     .goal   = TutorialGoal::LockOn},

    {.name   = "Attack",
     .action = "Attack",
     .text   = "ボスに近づいて、攻撃",
     .goal   = TutorialGoal::Attack},

    // 本線は Atk_Combo1 → 2 → 乱れ斬り（3 を4回）→ 4 → 5 の8段
    {.name   = "Combo",
     .action = "Attack",
     .text   = "攻撃ボタンを連打すると、乱れ斬りを経て8段まで続くコンボになる",
     .goal   = TutorialGoal::Combo},

    // ここから2つは派生。本線のどの段からでも、押した瞬間のスティックの向きで分かれる
    // 1発目はまだコンボに入っていないので派生しない。文で「攻撃の後」と言っておく
    {.name    = "ComboForward",
     .icon    = InputIconKind::Move,
     .action2 = "Attack",
     .text    = "攻撃の後、ボスの方へ倒して攻撃で跳び斬り。続けて押すと叩きつけ",
     .goal    = TutorialGoal::ComboForward},

    {.name    = "ComboBack",
     .icon    = InputIconKind::Move,
     .action2 = "Attack",
     .text    = "攻撃の後、ボスと逆へ倒して攻撃で退き斬り。続けて押すと踏み込み斬り",
     .goal    = TutorialGoal::ComboBack},

    // 「ダッシュ ＋ 攻撃」と2つのボタンを並べて出す
    {.name    = "DashAttack",
     .action  = "Dash",
     .action2 = "Attack",
     .text    = "ダッシュ中か走りながら攻撃でダッシュ攻撃。続けて押すとコンボへ",
     .goal    = TutorialGoal::DashAttack},

    {.name    = "AirAttack",
     .action  = "Jump",
     .action2 = "Attack",
     .text    = "ジャンプ中に攻撃で空中斬り。続けて押すと急降下斬り",
     .goal    = TutorialGoal::AirAttack},

    // --- 読んで避けて斬り込む（ここからボスが攻撃してくる） ---

    // ボスに攻撃させるのは、攻撃を避ける練習からだけにする
    // 移動や攻撃を覚えている最中に被弾すると、何を教わっているのか分からなくなるため
    {.name        = "ReadAndEvade",
     .action      = "Dash",
     .text        = "敵の攻撃のタイミングに合わせてダッシュをするとジャスト回避ができる",
     .assistText  = "今だ！ ダッシュ！",
     .goal        = TutorialGoal::JustEvade,
     .bossAttacks = true,
     .holdTime    = 0.0f}, // 成立するとすぐラッシュに入るので、余韻を挟まない

    // ジャスト回避の接近でボスの近くへ運ばれている間に促す
    {.name        = "Strike",
     .action      = "Attack",
     .text        = "ジャスト回避でボスの懐へ。そのまま攻撃",
     .goal        = TutorialGoal::Strike,
     .bossAttacks = true},

    // 今のジャスト回避から攻撃までが「ラッシュ」だと名前を付けて説明する。体験した直後に言うと結び付けやすい
    // 読むだけの項目なので、ボスは待たせる（読んでいる間に被弾させない）
    {.name     = "Rush",
     .action   = "Attack",
     .text     = "ジャスト回避の後に攻撃ボタンを押すと、ラッシュで一気に斬り込める！",
     .goal     = TutorialGoal::Rush,
     .holdTime = 1.0f}, // 読む時間の間に帯が満ちるので、余韻は短くてよい

    // 最後は終わらせずに、好きなだけ練習させる。抜けるときはポーズメニューの「タイトルへ」
    // 抜け方を教えることで、ポーズメニューがあることも伝える
    {.name        = "FreePlay",
     .icon        = InputIconKind::Pause,
     .text        = "自由に練習しよう。ESC のメニューからタイトルへ戻れる",
     .goal        = TutorialGoal::FreePlay,
     .bossAttacks = true},
};

// 項目の数。TutorialState.step がこれに達したら教え終えた
inline constexpr uint8_t kTutorialStepCount = static_cast<uint8_t>(std::size(kTutorialSteps));
