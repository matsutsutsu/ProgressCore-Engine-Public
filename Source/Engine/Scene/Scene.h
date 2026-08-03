#pragma once
#include <memory>
#include <atomic>

namespace CCL::ECS { class World; }
namespace CCL::ECS { class SystemManager; }

// シーン基底
class Scene
{
  protected:
    // 状態管理は一番親のクラスが責任を持つ。
    // ★ 非同期ロード時はワーカースレッドが書き、メインスレッドが読むため atomic。
    //    acquire/release で「Initialize が構築した World の中身」の可視性まで保証する
    //    （release で立てた ready を acquire で観測した後は、World へ安全にアクセスできる）。
    std::atomic<bool> m_isReady{false};

public:
    Scene() {}
    virtual ~Scene() {}

    // 初期化が完了しているかどうかのフラグ
    // LoadingSceneで裏読みした場合は true を返すようにする
    bool IsReady() const { return m_isReady.load(std::memory_order_acquire); }

    // マネージャー（または裏読みスレッド）から「初期化完了」を通知するための関数
    void SetReady() { m_isReady.store(true, std::memory_order_release); }

    // 初期化
    virtual void Initialize() = 0;

    // GPUセットアップなど、Initialize後にメインスレッドで1回だけ呼ばれる処理（必要に応じてオーバーライド）
    virtual void Start() {} 

    // 終了化
    virtual void Finalize() = 0;

    // --- 固定タイムステップ更新（物理・ロジック用） ---
    // デフォルトでは何もしない（必要に応じてオーバーライド）
    virtual void FixedUpdate(float fixedTime) {}

    // 更新処理
    virtual void Update(float elapsedTime) = 0;

    // 描画処理
    virtual void Render() = 0;

    // GUI描画
    virtual void DrawGUI() = 0;

    // 外部（エディタやフレームワーク）が世界にアクセスするための窓口
    virtual CCL::ECS::World* GetWorld() = 0;
    virtual CCL::ECS::SystemManager* GetSystemManager() = 0;
};