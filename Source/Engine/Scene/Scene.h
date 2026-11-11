#pragma once
// ===========================================================================
// 概要: 全シーンの抽象基底クラス Scene
//
// 責務:
//   - シーンのライフサイクル（Initialize → Start → 更新/描画 → Finalize）の定義
//   - 初期化済みフラグ（非同期ロードでワーカースレッドから立てられる）
//   - エディタ・フレームワークへ World / SystemManager を渡す窓口
//
// 備考: 実際のシーンは BaseScene を継承して作る
// ===========================================================================
#include <memory>
#include <atomic>

namespace CCL::ECS
{
    class World;
    class SystemManager;
} // namespace CCL::ECS

class Scene
{
protected:
    // 状態管理は一番親のクラスが責任を持つ
    // 非同期ロード時はワーカースレッドが書き、メインスレッドが読むため atomic
    //    acquire/release で「Initialize が構築した World の中身」の可視性まで保証する
    //    （release で立てた ready を acquire で観測した後は、World へ安全にアクセスできる）
    std::atomic<bool> m_isReady{false};

public:
    Scene() {}
    virtual ~Scene() {}

    // 初期化が完了しているか。SceneLoading で裏読みした場合は、遷移前から true になっている
    bool IsReady() const { return m_isReady.load(std::memory_order_acquire); }

    // SceneManager（または裏読みのワーカースレッド）が「初期化完了」を通知する
    void SetReady() { m_isReady.store(true, std::memory_order_release); }

    // 重いデータのロード。非同期ロードではワーカースレッドで呼ばれる
    virtual void Initialize() = 0;

    // GPU のセットアップなど、Initialize の後にメインスレッドで1回だけ呼ばれる
    virtual void Start() {}

    virtual void Finalize() = 0;

    // 固定タイムステップの更新（物理・ロジック用）
    virtual void FixedUpdate(float fixedTime) {}

    // 毎フレームの更新（描画準備・UI）
    virtual void Update(float elapsedTime) = 0;

    virtual void Render() = 0;

    // 画面遷移の黒い幕（SceneManager::DrawFade）より手前に描くもの。既定では何も描かない
    //   幕はすべての描画の上に被さるので、暗転中にも見せたいもの（ローディングの表示）はここで描く
    virtual void RenderOverFade() {}

    // ImGui の描画
    virtual void DrawGUI() = 0;

    // エディタやフレームワークが World にアクセスするための窓口
    virtual CCL::ECS::World         *GetWorld()         = 0;
    virtual CCL::ECS::SystemManager *GetSystemManager() = 0;
};