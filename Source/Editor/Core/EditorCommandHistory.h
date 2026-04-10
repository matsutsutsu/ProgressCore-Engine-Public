#pragma once
#include <vector>
#include <memory>
#include "ECS/Core/CCL_World.h"

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
        _undoStack.push_back(std::move(cmd));
        _redoStack.clear(); // 新しい操作をしたら未来は消える
    }

    void Undo() {
        if (_undoStack.empty()) return;
        auto cmd = std::move(_undoStack.back());
        _undoStack.pop_back();

        cmd->Undo();
        _redoStack.push_back(std::move(cmd));
    }

    void Redo() {
        if (_redoStack.empty()) return;
        auto cmd = std::move(_redoStack.back());
        _redoStack.pop_back();

        cmd->Execute();
        _undoStack.push_back(std::move(cmd));
    }

private:
    std::vector<std::unique_ptr<IEditorCommand>> _undoStack;
    std::vector<std::unique_ptr<IEditorCommand>> _redoStack;
};

// =========================================================
// ★ 3. 究極の汎用コマンド：あらゆるコンポーネント(T)の変更を記憶する
// =========================================================
template <typename T>
class ChangeComponentCommand : public IEditorCommand {
private:
    CCL::ECS::Core::World* _world;
    CCL::ECS::EntityID _entity;
    T _oldData;
    T _newData;

public:
    ChangeComponentCommand(CCL::ECS::Core::World* world, CCL::ECS::EntityID entity, const T& oldData, const T& newData)
        : _world(world), _entity(entity), _oldData(oldData), _newData(newData) {
    }

    void Execute() override {
        if (auto* comp = _world->GetComponent<T>(_entity)) {
            *comp = _newData;
        }
    }

    void Undo() override {
        if (auto* comp = _world->GetComponent<T>(_entity)) {
            *comp = _oldData;
        }
    }
};