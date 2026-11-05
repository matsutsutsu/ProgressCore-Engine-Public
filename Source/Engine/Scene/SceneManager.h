#pragma once
// ===========================================================================
// 概要: 現在のシーンの保持と、シーンの切り替え・フェード遷移を統括するシングルトン SceneManager
//
// 責務:
//   - シーンの生成・切り替え（同期/非同期）と、現在のシーンの更新・描画の呼び出し
//   - フェードアウト → 切り替え → フェードインの遷移を1本の状態機械で回す
//   - シーンファイルの保存・読み込み（World の中身の入れ替え）
//   - シーン非同期ロード専用のスレッドプールの保持
// ===========================================================================
#include "Scene.h"
#include <string>
#include <memory>

#include "Engine/ECS/Jobs/CCL_JobSystem.h"

namespace CCL::ECS
{
    class World;
}

class Sprite; // フェード用の黒い全画面矩形

class SceneManager
{
private:
    // m_fadeQuad（unique_ptr<Sprite>）の破棄に Sprite の完全型が要るので、ここで定義する
    SceneManager();
    ~SceneManager();

public:
    static SceneManager &Instance()
    {
        static SceneManager instance;
        return instance;
    }

    // 最初のシーンの非同期ロードを始める
    void Initialize();

    void FixedUpdate(float fixedTime);

    // 予約された切り替えを実行してから、現在のシーンを更新する
    void Update(float elapsedTime);

    // 現在のシーンを描き、最前面にフェードを重ねる
    void Render();

    void DrawGUI();

    // 現在のシーンを Finalize して破棄する
    void Clear();

    // ============================================================
    // シーン切り替え（フェードなし）
    // ============================================================

    // 次の Update でシーンを切り替える
    void ChangeScene(const std::string &name);

    // 生成済みのシーンに切り替える（所有権を受け取る）。m_currentSceneName は更新しない
    void ChangeScene(Scene *scene);

    // ローディング画面を挟み、切り替え先の重い Initialize をワーカースレッドで走らせてから切り替える
    void ChangeSceneAsync(const std::string &name);

    // ============================================================
    // 画面遷移（フェード付き）
    // ============================================================
    //
    // フェードアウト、切り替え、フェードインを、SceneManager が1本の状態機械として回す
    // World は遷移で破棄されるので、フェードを ECS の System に置くと遷移をまたげない

    // フェードアウト、黒の間に切り替え、フェードインまでを行う。async なら SceneLoading を挟んで裏で読む
    // 遷移中なら false を返して無視する
    bool RequestTransition(const std::string &name, bool async = false);

    // 読み込み済みのシーンへフェードで切り替える（SceneLoading が裏読みを終えたときに使う）。遷移中なら false
    bool RequestTransition(Scene *readyScene);

    // 黒→表示のフェードインのみ（起動直後の初回シーンのポップイン隠し）
    void FadeInFromBlack(float dur = 1.0f);

    // フェード遷移中か
    bool IsTransitioning() const { return m_fade != FadeState::None; }

    // シーンロード専用のスレッドプール（フレーム並列用の共有プールとは別）
    // SceneLoading がここへ「対象シーンの Initialize」ジョブを投げる
    CCL::ECS::JobSystem &GetLoaderJobs() { return m_loaderJobs; }

    // 名前で切り替えた最後のシーン名（非同期なら最終的な遷移先の名前）
    const std::string &GetCurrentSceneName() const { return m_currentSceneName; }

    // ============================================================
    // シーンファイルの保存・読み込み
    // ============================================================

    // 未適用の構造変更を確定させてから、World の中身をファイルに書き出す
    void SaveScene(CCL::ECS::World *world, const std::string &filepath);

    // World の中身（PersistentTag 付きは残す）を破棄し、ファイルの内容で作り直す。読めなければ何も壊さずにやめる
    void LoadScene(CCL::ECS::World *world, const std::string &filepath);

    Scene *GetCurrentScene() const { return currentScene; }

    // シーンが切り替わるたびに増える通し番号
    //   「前回見たときから変わったか」を呼び出し側が1つの整数比較で判定できる
    //   World* の比較だと、新しい World がたまたま同じ番地に確保されたときに
    //   取りこぼす。エディタ側の Undo 履歴・選択の破棄がこれを見る
    uint32_t GetSceneSerial() const noexcept { return m_sceneSerial; }

private:
    // フェードの進行を1ステップ進める（黒の瞬間に予約された切替を発火する）
    void AdvanceFade(float dt);
    // 予約された切り替え（名前 or 実体）を今この場で実行する。黒の瞬間に呼ばれる
    void BeginPendingChange();
    // 画面切り替え（◆タイルワイプ）の効果音を鳴らす。暗転を始めるときに呼ぶ
    void PlayTransitionSound();
    // フェードを描画する（◆タイルワイプ。Render の末尾＝最前面で呼ぶ）
    void DrawFade();

    // フェードの状態機械
    //   Out（暗転）→ Hold（黒で少しタメる）→ In（明転）の3段
    //   Hold を挟むことで「黒に着いた瞬間に即バウンド」する軽さを消し、
    //   その裏で新シーンの初回フレームを描き切ってからフェードインできる
    enum class FadeState
    {
        None,
        Out,
        Hold,
        In
    };
    FadeState m_fade          = FadeState::None;
    float     m_fadeAlpha     = 0.0f;  // 0 = 透明 / 1 = 黒
    float     m_fadeOutDur    = 1.5f; // 暗転にかける秒数
    float     m_fadeInDur     = 1.5f; // 明転にかける秒数
    float     m_fadeHoldDur   = 0.15f; // 完全な黒を保持する秒数
    float     m_fadeHoldTimer = 0.0f;  // Hold に入ってからの経過秒数

    // 黒の瞬間に実行する「予約された切り替え」。名前指定と実体指定のどちらか
    bool        m_pendingValid = false;
    std::string m_pendingName;
    bool        m_pendingAsync = false;
    Scene      *m_pendingScene = nullptr; // 実体指定（SceneLoading からの本命切替）

    // 非同期ロードの「黒保持」中フラグ
    //   true の間はローディング画面を見せず、黒（alpha=1）のまま裏読み完了を待つ
    //   完了時に本命シーンへ差し替えて「1回だけ」フェードインする（暗→明→暗→明 の
    //   チラつきを避ける）。SceneLoading からの RequestTransition(Scene*) で解除
    bool m_asyncHold = false;

    std::unique_ptr<Sprite> m_fadeQuad; // フェードの◆・光の筋を描く白スプライト（遅延生成）

    Scene *currentScene = nullptr; // 現在のシーン（所有する）
    Scene *nextScene    = nullptr; // 次の Update で切り替えるシーン（所有する）

    std::string m_currentSceneName = "";

    uint32_t m_sceneSerial = 0; // シーン切り替えのたびに +1（GetSceneSerial 参照）

    // シーン非同期ロード専用のスレッドプール
    // フレーム並列用の共有プールと分けるのは、ロードのような長時間ジョブを
    // フレーム並列の WaitForCounter が手伝いで拾ってしまい、フレームが止まるのを防ぐため
    CCL::ECS::JobSystem m_loaderJobs;
};
