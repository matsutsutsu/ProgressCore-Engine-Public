#pragma once
// ===========================================================================
// 概要: 非同期ロードの間に表示する「つなぎ」のシーン SceneLoading
//       次のシーンを裏で読み込みながら、暗転の幕の上にローディング画面を描く
//
// 責務:
//   - 次のシーンの重い Initialize() を、SceneManager のロード専用 JobSystem で裏読みする
//   - 裏読みの完了を毎フレーム確かめ、終わったら SceneManager::RequestTransition で次のシーンへ切り替える
//   - ローディング画面（見出し・ログ・ヒント・進捗リング）を暗転の幕の上に描く
//   - アプリを起動して最初の読み込みでは、ローディング画面の代わりにエンジンのロゴ（スプラッシュ）を描く
//
// 備考: 裏読みは投げたままにし、毎フレーム完了を確かめる
//       WaitForCounter で待つと、読み込みの間メインスレッドが止まって画面を描けない
//       次のシーンは SetReady 済みで渡すので、切り替えたときに Initialize が二度走らない
// ===========================================================================
#include "Engine/Scene/BaseScene.h"

#include "Engine/ECS/Jobs/CCL_JobSystem.h"

#include <array>
#include <atomic>
#include <memory>
#include <string>

class TextRenderer; // 見出し・ログなどの文字表示（unique_ptr で持つので、デストラクタは .cpp に置く）
class Sprite;       // 線・◆・目盛りなどの単色の四角形

// ログの行数（SceneLoading.cpp の kLogLines と数を合わせる）
inline constexpr int kLogLineCount = 8;

class SceneLoading : public BaseScene
{
private:
    // --- 裏読み ---
    Scene               *m_nextScene = nullptr;  // 裏読みする次のシーン。切り替えを頼んだ後は SceneManager が所有する
    std::atomic<bool>    m_isFinished{false};    // 裏読みが終わったか（ワーカースレッドが書き、メインスレッドが読む）
    CCL::ECS::JobCounter m_loadCounter;          // ロードジョブの完了待ちに使うカウンタ

    // --- 描画の道具（メインスレッド専用・最初の描画で作る） ---
    std::unique_ptr<TextRenderer> m_text;  // 文字の描画
    std::unique_ptr<Sprite>       m_white; // 白いテクスチャ。色を掛けて単色の四角形を描く

    // --- 表示の状態 ---
    float                            m_progress   = 0.0f; // 見せかけの進捗（0〜1）。進捗リングとログの進み具合になる
    float                            m_visibility = 0.0f; // 表示の濃さ（0〜1）
    float                            m_outroTime  = 0.0f; // 進捗が 100% に届いてからの秒数。完了の演出の時計
    int                              m_tipIndex   = 0;    // 出すヒントの番号
    std::array<float, kLogLineCount> m_logStart{};        // ログの各行が出た時刻（秒）。負ならまだ出ていない

    bool m_splash = false; // この読み込みでスプラッシュを出すか（製品ビルドの、起動して最初の読み込みだけ true）
    bool m_quick  = false; // 最低表示時間を待たずに抜けるか（開発ビルドの起動時だけ true）

    // --- 時間の調整値（秒） ---
    static constexpr float kMinLoadTime = 0.15f; // 次のシーンへ切り替えるまでの最低の待ち時間
    static constexpr float kShowDelay   = 0.2f;  // 表示を出し始めるまでの待ち時間
    static constexpr float kFadeSpeed   = 4.0f;  // 表示が現れる・消える速さ（1/秒。4 なら 0.25 秒）
    static constexpr float kOutroHold   = 0.5f;  // 進捗が 100% に届いてから、消し始めるまでの時間
    static constexpr float kProgressTau = 1.5f;  // 読み込み中の見せかけの進捗が 90% へ近づく時定数
    static constexpr float kMinShowTime = 4.0f;  // ローディング画面の最低表示時間
    static constexpr float kSplashTime  = 2.5f;  // スプラッシュの最低表示時間

    // --- 描画（alpha は表示の濃さ。出入りのフェードを含む） ---
    void DrawBackdrop(float alpha); // 背景（色の付いた地・周辺減光・粒子ノイズ）
    void DrawScreen(float alpha);   // 前景（方眼・見出し・ログ・ヒント・進捗リング）
    void DrawSplash(float alpha);   // スプラッシュ（POWERED BY PROGRESSCORE ENGINE）

    // 中心を (cx, cy)（px、左上が原点で下が正）に置いた、単色の四角形を1枚描く。angle が 45 なら◆になる
    // 最初の描画で m_white を作った後に呼ぶ
    void DrawQuad(float cx, float cy, float w, float h, float angle, float r, float g, float b, float a);

public:
    // 裏読みする次のシーンを受け取る。Initialize() はまだ呼んでいないもので、切り替えを頼むまでこのシーンが持つ
    SceneLoading(Scene *nextScene);
    virtual ~SceneLoading() override;

    void Initialize() override;
    void Finalize() override;
    void Update(float elapsedTime) override;

    // 暗転の幕の上に描く（SceneManager::Render が幕を描いた後に呼ぶ）
    void RenderOverFade() override;
    void OnDrawImGui() override;

    CCL::ECS::World         *GetWorld() override { return m_worldPtr; }
    CCL::ECS::SystemManager *GetSystemManager() override { return m_ecsContext->GetSystemManager(); }

    float time = 0.0f; // このシーンに入ってからの経過時間（秒）
};
