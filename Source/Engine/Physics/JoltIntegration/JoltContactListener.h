// Jolt物理空間とECS空間を繋ぐ、スレッドセーフな衝突イベントの蓄積バッファ（ポスト）
//
// Joltのワーカースレッドから非同期に呼ばれるため、内部状態の変更には必ずミューテックスによる保護が必要。
//
// 注意: このクラス内で直接ECSのWorldやコンポーネントにアクセスしてはならない（データ競合によるクラッシュを招く）。
//
// キャラクター側(JoltCharacterContactListener)もこのバッファに PushEvent() で相乗りし、
// 物理更新後の L05 で JoltCollisionEventSystem が一括配送する。
#pragma once
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <mutex>
#include <vector>
#include "Engine/Physics/Components/Collision/JoltCollisionEvent.h"

class JoltContactListener : public JPH::ContactListener {
public:
    /**
     * @brief 物理的な接触が新たに検出された瞬間にJoltのワーカースレッドから呼ばれるコールバック
     * @param inBody1 衝突した剛体1
     * @param inBody2 衝突した剛体2
     * @param inManifold 衝突の接触点や法線情報を持つ多様体
     * @param ioSettings 接触に対する反発係数や摩擦を上書きするための設定値
     * * @note 実行コンテキスト: Jolt Worker Thread (マルチスレッド)
     */
    virtual void OnContactAdded(const JPH::Body& inBody1, const JPH::Body& inBody2,
        const JPH::ContactManifold& inManifold,
        JPH::ContactSettings& ioSettings) override;

    /**
     * @brief 接触が継続しているフレームに呼ばれる（2フレーム目以降）。
     * @note  Added と全く同じ扱いにする。Jolt は同じボディ対の2回目以降を Added では
     *        なく Persisted に回すため(ContactListener.h の注記)、ここを実装しないと
     *        「重なり続けている間」の接触が全て消え、攻撃判定の窓より先に武器が相手へ
     *        入っているケース（突進攻撃など）が丸ごと不発になる。
     * @note  実行コンテキスト: Jolt Worker Thread (マルチスレッド)
     */
    virtual void OnContactPersisted(const JPH::Body& inBody1, const JPH::Body& inBody2,
        const JPH::ContactManifold& inManifold,
        JPH::ContactSettings& ioSettings) override;

    /**
     * @brief 接触が解消された時に呼ばれる。
     * @note  計測ログのみ。接触が正しく解放されているかの確認用で、判定には使わない。
     * @warning このコールバック中は Body へ一切アクセスできない(Jolt の仕様)。
     *          そのため EntityID は解決できず BodyID の index のみ記録する。
     */
    virtual void OnContactRemoved(const JPH::SubShapeIDPair& inSubShapePair) override;

    /**
     * @brief 外部（キャラクターリスナー等）から衝突イベントをスレッドセーフに追加する。
     * @note どのスレッドから呼んでも安全。剛体・キャラ双方のイベントをこの1つのバッファに集約する。
     */
    void PushEvent(const JoltCollisionEvent &ev);

    /**
     * @brief 蓄積された全ての衝突イベントを一括で取得し、内部バッファをクリアする
     * @return 蓄積されていた JoltCollisionEvent の配列
     * * @note 実行コンテキスト: ECS Main Thread (またはTask Thread)
     * @warning 内部でロックを取得するため、1フレームに1回だけ（バッチ処理として）呼び出すこと。
     */
    std::vector<JoltCollisionEvent> GetAndClearEvents();

private:
    /**
     * @brief Added / Persisted 共通の実体。接触1件をイベント化してバッファへ積む。
     * @param inPhase 計測ログ用の表示ラベル("Add" / "Persist")。判定には影響しない。
     */
    void EmitContactEvent(const JPH::Body& inBody1, const JPH::Body& inBody2,
        const JPH::ContactManifold& inManifold, const char* inPhase);

    std::mutex                      m_mutex;  // スレッド間競合を防ぐための排他ロック
    std::vector<JoltCollisionEvent> m_events; // 蓄積される衝突イベントのバッファ
};