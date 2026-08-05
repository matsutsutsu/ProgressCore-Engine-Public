#pragma once
#include <vector>
#include <memory>
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Platform/Core/Logger.h"

// =========================================================
// 1. 命令書の基本インターフェース
// =========================================================
class IEditorCommand {
public:
    virtual ~IEditorCommand() = default;
    virtual void Execute() = 0; // 実行 (Redo)
    virtual void Undo() = 0;    // 取り消し (Undo)
};

// =========================================================
// 2. 履歴マネージャー (どこからでも呼べるシングルトン)
// =========================================================
class EditorCommandHistory {
public:
    static EditorCommandHistory& Instance() {
        static EditorCommandHistory instance;
        return instance;
    }

    void ExecuteCommand(std::unique_ptr<IEditorCommand> cmd) {
        cmd->Execute();
        m_undoStack.push_back(std::move(cmd));
        m_redoStack.clear(); // 新しい操作をしたら未来は消える

        // 🔍 【足跡 4】スタックに積まれたことを確認
        CCL_LOG_INFO(LogCategory::Editor, "[CommandHistory] Command Executed. UndoStack Size: %zu", m_undoStack.size());
    }

    void Undo() {
        if (m_undoStack.empty()) {
            CCL_LOG_WARN(LogCategory::Editor, "[CommandHistory] Undo failed: Stack is empty.");
            return;
        }
        auto cmd = std::move(m_undoStack.back());
        m_undoStack.pop_back();

        cmd->Undo();
        m_redoStack.push_back(std::move(cmd));

        // 🔍 【足跡 5】Undoの成功を確認
        CCL_LOG_SUCCESS(LogCategory::Editor, "[CommandHistory] UNDO Performed. Remaining Undo: %zu, Redo: %zu", m_undoStack.size(), m_redoStack.size());
    }

    void Redo() {
        if (m_redoStack.empty()) {
            CCL_LOG_WARN(LogCategory::Editor, "[CommandHistory] Redo failed: Stack is empty.");
            return;
        }

        auto cmd = std::move(m_redoStack.back());
        m_redoStack.pop_back();

        cmd->Execute();
        m_undoStack.push_back(std::move(cmd));

        // 🔍 【足跡 6】Redoの成功を確認
        CCL_LOG_SUCCESS(LogCategory::Editor, "[CommandHistory] REDO Performed. Remaining Undo: %zu, Redo: %zu", m_undoStack.size(), m_redoStack.size());
    }

private:
    std::vector<std::unique_ptr<IEditorCommand>> m_undoStack;
    std::vector<std::unique_ptr<IEditorCommand>> m_redoStack;
};

// =========================================================
// 3. 汎用コマンド：任意のコンポーネント(T)の変更前後を保持して Undo/Redo する
// =========================================================
template <typename T>
class ChangeComponentCommand : public IEditorCommand {
private:
    CCL::ECS::World* m_world;
    CCL::ECS::EntityID m_entity;
    T m_oldData;
    T m_newData;

public:
    ChangeComponentCommand(CCL::ECS::World* world, CCL::ECS::EntityID entity, const T& oldData, const T& newData)
        : m_world(world), m_entity(entity), m_oldData(oldData), m_newData(newData) {
    }

    void Execute() override {
        if (auto* comp = m_world->GetComponent<T>(m_entity)) {
            *comp = m_newData;
        }
    }

    void Undo() override {
        if (auto* comp = m_world->GetComponent<T>(m_entity)) {
            *comp = m_oldData;
        }
    }
};