#pragma once
// アクションの型は BTActions_Boss.cpp の無名 namespace に置き、ここには登録の関数だけを出す
// 型をヘッダに出すと BT を組む側から技を直接 new できてしまい、「技はレジストリから引く」を守れない

namespace CCL::AI::BT
{
    // ボス用の行動をすべて BTActionRegistry へ登録する。起動時に1回だけ呼ぶ
    void RegisterBossActions();
} 
