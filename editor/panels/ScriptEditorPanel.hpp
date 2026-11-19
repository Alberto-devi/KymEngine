#pragma once

#include <imgui.h>

#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

namespace Kyumist {

class Scene;

struct ScriptUndoEntry {
    std::string content;
    int cursorPos = 0;
};

struct OpenScriptDocument {
    std::filesystem::path filePath;
    std::string fileName;
    std::string content;
    bool dirty = false;
    int cursorPos = 0;
    int cursorLine = 1;
    int cursorColumn = 1;
    ImGuiID lastEditorId = 0;
    std::vector<ScriptUndoEntry> undoStack;
    std::vector<ScriptUndoEntry> redoStack;
    std::string lastCommittedContent;
    int lastCommittedCursor = 0;
    double lastEditTimestamp = -10.0;
};

struct AutocompleteItem {
    std::string label;
    std::string insertText;
    std::string detail;
    std::string kind;
    int cursorBacktrack = 0;
};

class ScriptEditorPanel {
public:
    void OpenScript(const std::filesystem::path& filePath);
    bool CreateNewScriptFile(const std::filesystem::path& scriptsDir, const std::string& className);
    void OpenFirstAvailableScript();
    void CloseAll();

    void Render(Scene& scene, bool isPlaying, bool* pOpen);
    bool IsFocused() const { return m_IsWindowFocused; }

private:
    bool SaveDocument(OpenScriptDocument& doc);
    void SaveAndHotReload(Scene& scene, bool isPlaying);
    std::unordered_set<int> ParseErrorLinesForFile(const std::string& fileName) const;
    void RecordUndoSnapshot(
        OpenScriptDocument& doc,
        const std::string& beforeContent,
        int beforeCursor,
        bool forceDiscrete
    );
    void SyncDocumentCursorStats(OpenScriptDocument& doc);
    bool UndoDocument(OpenScriptDocument& doc);
    bool RedoDocument(OpenScriptDocument& doc);

    void UpdateAutocomplete(const OpenScriptDocument& doc, bool forceShow = false);
    void AcceptAutocomplete(ImGuiInputTextCallbackData* data, OpenScriptDocument& doc);
    static int StaticEditorCallback(ImGuiInputTextCallbackData* data);
    int HandleEditorCallback(ImGuiInputTextCallbackData* data, OpenScriptDocument& doc);
    void RenderSyntaxOverlayAndGutter(
        OpenScriptDocument& doc,
        const std::unordered_set<int>& errorLines,
        ImGuiID editorId,
        const ImVec2& gutterMin,
        const ImVec2& gutterMax
    );

    std::vector<OpenScriptDocument> m_Documents;
    int m_ActiveTabIndex = -1;
    bool m_IsWindowFocused = false;
    bool m_FocusRequested = false;
    bool m_FocusEditorRequested = false;
    char m_NewScriptName[64] = "NewScript";

    // Auto-indentacion & autocompletado
    bool m_PendingAutoIndent = false;
    bool m_PendingTabSpaces = false;
    bool m_PendingAcceptCompletion = false;
    bool m_PendingCloseDedent = false;
    int m_NavDelta = 0;
    int m_ClickedSuggestion = -1;
    int m_RestoreCursorPos = -1;

    bool m_AutocompleteOpen = false;
    int m_SelectedSuggestion = 0;
    int m_CompletionReplaceStart = 0;
    std::string m_CompletionPrefix;
    std::vector<AutocompleteItem> m_Suggestions;

    float m_PopupScreenX = 0.0f;
    float m_PopupScreenY = 0.0f;
    ImVec2 m_LastPopupMin{0.0f, 0.0f};
    ImVec2 m_LastPopupMax{0.0f, 0.0f};
};

} // namespace Kyumist

