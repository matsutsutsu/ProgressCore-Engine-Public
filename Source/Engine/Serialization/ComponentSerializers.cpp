#include "ComponentSerializers.h"

// ※もし PathToRelativeUtf8 などのパス変換ヘルパー関数がこのファイルにあるなら、
// それらは消さずにそのまま一番上に残しておいてください。

namespace ComponentSerializers {

    // ============================================================================
    // エンジン内の全コンポーネントの Meta 情報（JSON & GUI）を一括登録する
    // ============================================================================
    void RegisterAll()
    {

        // 1. コアシステム
        RegisterCoreMeta();
        RegisterTransformMeta(); // 呼び出しが漏れていたのを修正

        // 2. 物理演算
        RegisterPhysicsMeta();

        // 3. グラフィックス＆レンダリング
        RegisterGraphicsMeta();
        RegisterLightingMeta();
        RegisterEnvironmentMeta();
        RegisterParticleMeta();

        // 4. アニメーション＆カメラ
        RegisterAnimationMeta();
        RegisterCameraMeta();

        // 5. ゲームロジック＆スクリプト
        RegisterGamePlayMeta();
        //RegisterScriptingMeta();

        // 6. ユーティリティ・デバッグ
        RegisterUtilsMeta();
    }

} // namespace ComponentSerializers