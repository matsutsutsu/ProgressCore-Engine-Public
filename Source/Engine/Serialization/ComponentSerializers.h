#pragma once


// ============================================================================
// システム初期化用
// ============================================================================
namespace ComponentSerializers {
    // エンジンの起動時に呼ばれ、全コンポーネントのシリアライザとGUIを一括登録する関数
    void RegisterAll();
}


// ============================================================================
// モジュールごとのメタ登録関数群 (Static Auto-Registration 代替)
// 各機能の Meta.cpp の末尾で実装されています。
// ============================================================================
void RegisterCoreMeta();        // Name, などエンジンの根幹
void RegisterTransformMeta();   // Transform などエンジンの根幹
void RegisterGamePlayMeta();    // Player, Enemy, Health, Trigger などゲームロジック
void RegisterPhysicsMeta();     // JoltCollider, JoltRigidbody, CollisionLayer など
void RegisterGraphicsMeta();    // Model, StaticMesh, Material など描画の基礎
void RegisterLightingMeta();    // Directional, Point, Spot ライト
void RegisterEnvironmentMeta(); // Fog, Bloom, ToneMap, ShadowMap などの環境・ポストプロセス
void RegisterCameraMeta();      // VirtualCamera, CameraBrain などのカメラシステム
void RegisterParticleMeta();    // GPUParticle, PooledParticle などエフェクト系
void RegisterAnimationMeta();   // Animator などのアニメーション系
void RegisterScriptingMeta();   // LuaScript などのスクリプト系
void RegisterUtilsMeta();       // Timer, Primitive, Dissolve などの便利ツール