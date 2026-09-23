// ボス用BTアクションの登録口。
//
// アクションの実体（待機・移動・技…）は BTActions_Boss.cpp の無名namespaceにある。
// ここに出すのは登録関数だけ。技の定義をヘッダに置くと、BTを組む側から個々の技を
// 直接 new できてしまい、「技はレジストリ経由で引く」という境界が言葉だけになる。
#pragma once

namespace CCL::AI::BT
{
    /// ボス用の行動をすべて BTActionRegistry へ登録する。起動時に1回だけ呼ぶ。
    void RegisterBossActions();
} // namespace CCL::AI::BT
