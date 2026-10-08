#pragma once
// ===========================================================================
// 概要: 非同期ロード用の「つなぎ」シーン SceneLoading。
//
// 責務:
//   - 対象シーンの重い Initialize() を、SceneManager のロード専用 JobSystem で裏読みする
//   - 完了を毎フレーム覗き、終わったら SceneManager::RequestTransition で本命へ切り替える
//   - ロード中の表示を、暗転の幕の上に描く（左上の見出し・行き先・流れるログ・ヒント、中央右の◆の輪、右下の照準リング）
//
// 備考: 自身の Initialize は軽い（共通初期化のみで System は0個）。
//       毎フレームの並列（Execute → WaitForCounter で待つ）と違い、ここは
//       「投げっぱなし + 毎フレーム覗く（fire-and-poll）」でメインスレッドを止めない。
//       対象シーンは SetReady() 済みで渡るので、切替時に再 Initialize されない。
//       裏読みの間、SceneManager は画面を黒の幕で覆ったまま保持する。表示はその幕の上（RenderOverFade）に描き、
//       裏読みがすぐ終わるときは出さない（一瞬だけ出ると、かえってちらついて見える）。
//       裏読みがすぐ終わっても、ログが最後まで流れるよう kMinShowTime 秒は表示する。
//       アプリを起動して最初の読み込みだけは、ローディングの代わりにエンジンのロゴ（スプラッシュ）を出す。
//       起動時の読み込みの待ち時間をそのままロゴの表示に使うので、起動が遅くならない。
// ===========================================================================
#include "Engine/Scene/BaseScene.h"

#include "Engine/ECS/Jobs/CCL_JobSystem.h" // JobCounter（ロードジョブの寿命管理）

#include <array>
#include <atomic>
#include <memory>
#include <string>

class TextRenderer; // 見出し・ログなどの文字表示用（unique_ptr なのでデストラクタは .cpp に置く）
class Sprite;       // ◆・線などの単色矩形用

// ログの行数（SceneLoading.cpp の kLogLines と数を合わせる）
inline constexpr int kLogLineCount = 9;

class SceneLoading : public BaseScene
{
private:
    // 裏読みする本命シーン。切替を頼んだ後は SceneManager が所有する
    Scene *m_nextScene = nullptr;

    // 裏読み完了フラグ（ワーカーが書き、メインが Update で覗く）
    std::atomic<bool> m_isFinished{false};

    // ロードジョブの寿命管理カウンタ。Finalize/dtor で WaitForCounter して
    // 「ジョブ完了前に this が壊れる」use-after-free を防ぐ（join 相当）。
    CCL::ECS::JobCounter m_loadCounter;

    // ローディング中の文字表示（メインスレッド専用・遅延生成）
    std::unique_ptr<TextRenderer> m_text;

    // ◆を描く白スプライト（色を掛けて単色矩形を描く・遅延生成）
    std::unique_ptr<Sprite> m_white;

    // 表示用の進捗 0..1（実ロードは進捗を取れないので、時間で伸ばして見せる）。照準リングの目盛りとログの進み具合になる
    float m_progress = 0.0f;

    // 表示の濃さ 0..1。出すときは上げ、裏読みが終わって完了の演出を見せたら下げる
    float m_visibility = 0.0f;

    // 進捗が 100% に届いてからの秒数。完了の演出（照準リングが深紅に染まる・READY）の時計
    float m_outroTime = 0.0f;

    std::string                       m_destName;     // 行き先のシーン名（見出しの下に出す）
    int                               m_tipIndex = 0; // 出すヒントの番号（ロードのたびに次へ進める）
    std::array<float, kLogLineCount>  m_logStart{};   // ログの各行が出た時刻（秒）。負ならまだ出ていない

    // 最低待機時間（秒）。ロードが一瞬で終わった場合に黒を一拍だけ置くための最小の間
    static constexpr float kMinLoadTime = 0.15f;

    // 表示を出し始めるまでの時間（秒）。これより早く裏読みが終わったら、表示を出さずに切り替える
    static constexpr float kShowDelay = 0.2f;

    // 表示が現れる・消える速さ（1/秒。4 なら 0.25 秒）
    static constexpr float kFadeSpeed = 4.0f;

    // 進捗が 100% に届いてから、消し始めるまでの時間（秒）。完了の演出を見せる間
    static constexpr float kOutroHold = 0.5f;

    // 見せかけの進捗が 90% へ近づく時定数（秒）。ロードが長いほど、残りの目盛りがゆっくり点く
    static constexpr float kProgressTau = 1.5f;

    // 最低表示時間（秒）。裏読みがすぐ終わっても、ログが最後まで流れるまでは表示を続ける
    static constexpr float kMinShowTime = 4.0f;

    // スプラッシュ（起動して最初の読み込み）の最低表示時間（秒）。ロゴを読める長さにする
    static constexpr float kSplashTime = 2.5f;

    bool m_splash = false; // この読み込みでスプラッシュを出すか（製品ビルドで、アプリを起動して最初の読み込みだけ true）
    bool m_quick  = false; // 最低表示時間を待たずに抜けるか（開発ビルドの起動時。すぐデバッグに入れるように）

    // 描画。alpha は表示の濃さ（出入りのフェード込み）
    void DrawBackdrop(float alpha); // 背景（色の付いた地・◆の輪・周辺減光・粒子ノイズ）
    void DrawScreen(float alpha);   // 前景（方眼・見出し・行き先・ログ・ヒント・照準リング）
    void DrawSplash(float alpha);   // スプラッシュ（POWERED BY PROGRESSCORE ENGINE）

    // 中心 (cx, cy)・幅 w・高さ h・回転 angle（度）の単色の四角形を描く
    void DrawQuad(float cx, float cy, float w, float h, float angle, float r, float g, float b, float a);

public:
    SceneLoading(Scene *nextScene);
    virtual ~SceneLoading() override;

    void Initialize() override;
    void Finalize() override;
    void Update(float elapsedTime) override;

    // 暗転の幕の上に描く（SceneManager::Render が幕の後に呼ぶ）
    void RenderOverFade() override;
    void OnDrawImGui() override;

    CCL::ECS::World         *GetWorld() override { return m_worldPtr; }
    CCL::ECS::SystemManager *GetSystemManager() override { return m_ecsContext->GetSystemManager(); }

    float time = 0.0f; // このシーンに入ってからの経過時間（秒）
};
