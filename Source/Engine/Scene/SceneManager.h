#pragma once

#include "Scene.h"
#include <string>
#include <memory>

#include "Engine/ECS/Jobs/CCL_JobSystem.h" // シーン非同期ロード用の汎用スレッドプール

// World クラスが存在することをコンパイラに教える
namespace CCL::ECS { class World; }

// フェード用の全画面矩形（黒オーバーレイ）。実体は .cpp で生成する。
class Sprite;

// シーンマネージャー
class SceneManager {
private:
    // unique_ptr<Sprite> を持つため、Sprite が完全型になる .cpp 側で定義する
    //（ヘッダでは Sprite は前方宣言のみ＝不完全型で ~unique_ptr が組めない）。
    SceneManager();
    ~SceneManager();

public:
    // 唯一のインスタンス取得
    static SceneManager& Instance() {
        static SceneManager instance;
        return instance;
    }

    // 初期化
    void Initialize();

    // 終了化
    //void Finalize();

    // --- 追加：固定更新 ---
    void FixedUpdate(float fixedTime);

    // 更新処理
    void Update(float elapsedTime);

    // 描画処理
    void Render();

    // GUI描画
    void DrawGUI();

    // シーンクリア
    void Clear();

    // シーン切り替え
    void ChangeScene(const std::string& name);

    // インスタンスを直接渡すオーバーロード
    void ChangeScene(Scene *scene);

    // 非同期シーン切り替え：ローディング画面を挟み、対象シーンの重い
    // Initialize() を裏（ワーカースレッド）で走らせてから遷移する。
    void ChangeSceneAsync(const std::string& name);

    // =====================================================================
    //  画面遷移（フェード）の統括
    //   シーン切替の所有者である SceneManager が「フェードアウト → 切替 →
    //   フェードイン」を1本の状態機械として回す。World は遷移で破棄されるため
    //   フェードを World の中（ECSシステム）に置くと遷移をまたげない。だから
    //   遷移をまたいで生き残るこの層が持つのが正しい。
    //
    //   呼び出し側（メニューのボタン等）は「行き先を頼む」だけでよく、
    //   フェードもロードも SceneLoading の存在も知らなくてよい（疎結合）。
    // =====================================================================

    // 遷移を要求する。フェードアウト→黒の瞬間に切替→フェードインを自動で行う。
    // async=true なら SceneLoading を挟んで裏読みする。
    // 既に遷移中なら false を返して無視する（二重遷移防止）。
    bool RequestTransition(const std::string& name, bool async = false);

    // 既に生成・ロード済みのシーン実体へフェード遷移する。
    // SceneLoading が「裏読み完了した本命シーン」へ切り替えるときに使う。
    bool RequestTransition(Scene* readyScene);

    // 黒→表示のフェードインのみ（起動直後の初回シーンのポップイン隠し）。
    void FadeInFromBlack(float dur = 0.65f);

    // 遷移フェード中かどうか。
    bool IsTransitioning() const { return m_fade != FadeState::None; }

    // シーンロード専用の汎用スレッドプール（各Worldの並列プールとは独立）。
    // SceneLoading がここへ「対象シーンの Initialize」ジョブを投げる。
    CCL::ECS::JobSystem& GetLoaderJobs() { return m_loaderJobs; }

    // 現在のシーン名を取得・設定
    const std::string& GetCurrentSceneName() const { return m_currentSceneName; }
    void SetCurrentSceneName(const std::string& name) { m_currentSceneName = name; }

    // --- ECSデータ駆動のシーン保存・読み込み ---
    void SaveScene(CCL::ECS::World* world, const std::string& filepath);
    void LoadScene(CCL::ECS::World* world, const std::string& filepath);

	Scene* GetCurrentScene() const { return dynamic_cast<Scene*>(currentScene); }


private:
    // フェードの進行を1ステップ進める（黒の瞬間に予約された切替を発火する）。
    void AdvanceFade(float dt);
    // 予約された切替（名前 or 実体）を今この場で実行する。黒の瞬間に呼ばれる。
    void BeginPendingChange();
    // フェードの黒矩形を描画する（Render の末尾＝最前面で呼ぶ）。
    void DrawFade();

    // --- フェード状態機械 ---
    //   Out（暗転）→ Hold（黒で少しタメる）→ In（明転）の3段。
    //   Hold を挟むことで「黒に着いた瞬間に即バウンド」する軽さを消し、
    //   その裏で新シーンの初回フレームを描き切ってからフェードインできる。
    enum class FadeState { None, Out, Hold, In };
    FadeState m_fade      = FadeState::None;
    float     m_fadeAlpha = 0.0f;  // 0=透明 / 1=黒
    float     m_fadeOutDur = 0.55f; // 暗転にかける秒数
    float     m_fadeInDur  = 0.65f; // 明転にかける秒数
    float     m_fadeHoldDur = 0.15f; // 完全な黒を保持する秒数
    float     m_fadeHoldTimer = 0.0f;

    // 黒の瞬間に実行する「予約された切替」。名前指定と実体指定のどちらか。
    bool        m_pendingValid    = false;
    std::string m_pendingName;
    bool        m_pendingAsync    = false;
    Scene*      m_pendingScene    = nullptr; // 実体指定（SceneLoading からの本命切替）

    // 非同期ロードの「黒保持」中フラグ。
    //   true の間はローディング画面を見せず、黒（alpha=1）のまま裏読み完了を待つ。
    //   完了時に本命シーンへ差し替えて“1回だけ”フェードインする（暗→明→暗→明 の
    //   チラつきを避ける）。SceneLoading からの RequestTransition(Scene*) で解除。
    bool m_asyncHold = false;

    std::unique_ptr<Sprite> m_fadeQuad; // 黒矩形（遅延生成）

    Scene* currentScene = nullptr; // 現在のシーン
    Scene* nextScene = nullptr;    // 次に遷移するシーン

    // 現在のシーン名を保持する変数
    std::string m_currentSceneName = "";

    // シーン非同期ロード専用のスレッドプール（毎フレーム並列とは別インスタンス）。
    // ※ 各Worldの JobSystem はそのWorld自身をロードできない（鶏卵問題）ため、
    //    ロードはWorldに依存しないここのプールで回す。
    CCL::ECS::JobSystem m_loaderJobs;
};