#include "panels/ScriptEditorPanel.hpp"

#include "core/Log.hpp"
#include "scene/Scene.hpp"
#include "scripting/ScriptEngine.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace Kyumist {

namespace {

struct EditorCallbackContext {
    ScriptEditorPanel* panel = nullptr;
    OpenScriptDocument* doc = nullptr;
};

bool IsIdentStart(char c) {
    const unsigned char uc = static_cast<unsigned char>(c);
    return std::isalpha(uc) != 0 || c == '_';
}

bool IsIdentChar(char c) {
    const unsigned char uc = static_cast<unsigned char>(c);
    return std::isalnum(uc) != 0 || c == '_';
}

bool IsJavaKeyword(std::string_view word) {
    static const std::unordered_set<std::string_view> keywords{
        "abstract", "assert", "boolean", "break", "byte", "case", "catch", "char",
        "class", "const", "continue", "default", "do", "double", "else", "enum",
        "extends", "final", "finally", "float", "for", "if", "implements", "import",
        "instanceof", "int", "interface", "long", "native", "new", "package", "private",
        "protected", "public", "return", "short", "static", "super", "switch",
        "synchronized", "this", "throw", "throws", "transient", "try", "void",
        "volatile", "while", "true", "false", "null", "var"
    };
    return keywords.find(word) != keywords.end();
}

bool IsKyumistType(std::string_view word) {
    static const std::unordered_set<std::string_view> types{
        "KyumistScript", "Entity", "Input", "Audio", "NativeBridge", "ScriptHost",
        "entity", "String", "Math", "System", "Override", "dev", "kyumist"
    };
    return types.find(word) != types.end();
}

bool IsAllCapsConstant(std::string_view word) {
    if (word.size() < 2) {
        return false;
    }
    bool hasAlpha = false;
    for (char c : word) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (std::isalpha(uc)) {
            if (!std::isupper(uc)) {
                return false;
            }
            hasAlpha = true;
        } else if (!std::isdigit(uc) && c != '_') {
            return false;
        }
    }
    return hasAlpha;
}

std::string SanitizeClassName(const std::string& raw) {
    std::string out;
    for (char c : raw) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
            out.push_back(c);
        }
    }
    if (!out.empty() && std::isdigit(static_cast<unsigned char>(out.front()))) {
        out.insert(out.begin(), 'S');
    }
    return out;
}

std::string ToLowerAscii(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

int MatchScore(std::string_view label, std::string_view prefixLower) {
    if (prefixLower.empty()) {
        return 10;
    }
    const std::string labelLower = ToLowerAscii(label);
    if (labelLower == prefixLower) {
        return 100;
    }
    if (labelLower.rfind(prefixLower, 0) == 0) {
        return 80 - static_cast<int>(label.size() - prefixLower.size()) / 4;
    }
    const size_t subPos = labelLower.find(prefixLower);
    if (subPos != std::string::npos) {
        return 40 - static_cast<int>(subPos);
    }
    return -1;
}

int CountLeadingIndentWidth(const char* buf, int lineStart, int lineEnd) {
    int indent = 0;
    for (int i = lineStart; i < lineEnd; ++i) {
        if (buf[i] == ' ') {
            ++indent;
        } else if (buf[i] == '\t') {
            indent += 4;
        } else {
            break;
        }
    }
    return indent;
}

void ComputeNetBraceDelta(const char* buf, int len, int& outOpenCount, int& outCloseCount) {
    outOpenCount = 0;
    outCloseCount = 0;
    bool inLineComment = false;
    bool inBlockComment = false;
    bool inString = false;
    bool inChar = false;

    for (int i = 0; i < len; ++i) {
        const char c = buf[i];
        const char next = (i + 1 < len) ? buf[i + 1] : '\0';

        if (inLineComment) {
            if (c == '\n') {
                inLineComment = false;
            }
            continue;
        }
        if (inBlockComment) {
            if (c == '*' && next == '/') {
                inBlockComment = false;
                ++i;
            }
            continue;
        }
        if (inString) {
            if (c == '\\' && i + 1 < len) {
                ++i;
            } else if (c == '"') {
                inString = false;
            }
            continue;
        }
        if (inChar) {
            if (c == '\\' && i + 1 < len) {
                ++i;
            } else if (c == '\'') {
                inChar = false;
            }
            continue;
        }

        if (c == '/' && next == '/') {
            inLineComment = true;
            ++i;
            continue;
        }
        if (c == '/' && next == '*') {
            inBlockComment = true;
            ++i;
            continue;
        }
        if (c == '"') {
            inString = true;
            continue;
        }
        if (c == '\'') {
            inChar = true;
            continue;
        }

        if (c == '{') {
            ++outOpenCount;
        } else if (c == '}') {
            ++outCloseCount;
        }
    }
}

int FindMatchingOpenBraceIndent(const char* buf, int closeBracePos) {
    int depth = 1;
    bool inString = false;
    bool inChar = false;

    for (int i = closeBracePos - 1; i >= 0; --i) {
        const char c = buf[i];
        const bool escaped = (i > 0 && buf[i - 1] == '\\');
        if (!escaped && c == '"' && !inChar) {
            inString = !inString;
            continue;
        }
        if (!escaped && c == '\'' && !inString) {
            inChar = !inChar;
            continue;
        }
        if (inString || inChar) {
            continue;
        }

        if (c == '}') {
            ++depth;
        } else if (c == '{') {
            --depth;
            if (depth == 0) {
                int lineStart = i;
                while (lineStart > 0 && buf[lineStart - 1] != '\n') {
                    --lineStart;
                }
                return CountLeadingIndentWidth(buf, lineStart, i);
            }
        }
    }
    return -1;
}

std::string GetCurrentLineIndent(const std::string& content, int cursorPos) {
    const int clamped = std::clamp(cursorPos, 0, static_cast<int>(content.size()));
    int lineStart = clamped;
    while (lineStart > 0 && content[static_cast<size_t>(lineStart - 1)] != '\n') {
        --lineStart;
    }
    int i = lineStart;
    std::string indent;
    while (i < clamped) {
        const char ch = content[static_cast<size_t>(i)];
        if (ch == ' ') {
            indent.push_back(' ');
        } else if (ch == '\t') {
            indent.append("    ");
        } else {
            break;
        }
        ++i;
    }
    return indent;
}

void DrawColoredJavaLine(
    ImDrawList* drawList,
    ImFont* font,
    float fontSize,
    ImVec2 linePos,
    std::string_view line
) {
    size_t i = 0;
    const size_t n = line.size();
    float x = linePos.x;

    auto emitSpan = [&](std::string_view span, ImU32 col) {
        if (span.empty()) {
            return;
        }
        const char* begin = span.data();
        const char* end = span.data() + span.size();
        drawList->AddText(font, fontSize, ImVec2(x, linePos.y), col, begin, end);
        x += font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, begin, end).x;
    };

    while (i < n) {
        if (i + 1 < n && line[i] == '/' && line[i + 1] == '/') {
            emitSpan(line.substr(i), IM_COL32(115, 155, 122, 255));
            break;
        }
        if (line[i] == '"') {
            size_t j = i + 1;
            while (j < n && line[j] != '"') {
                if (line[j] == '\\' && j + 1 < n) {
                    ++j;
                }
                ++j;
            }
            if (j < n) {
                ++j;
            }
            emitSpan(line.substr(i, j - i), IM_COL32(242, 184, 107, 255));
            i = j;
            continue;
        }
        if (line[i] == '\'') {
            size_t j = i + 1;
            while (j < n && line[j] != '\'') {
                if (line[j] == '\\' && j + 1 < n) {
                    ++j;
                }
                ++j;
            }
            if (j < n) {
                ++j;
            }
            emitSpan(line.substr(i, j - i), IM_COL32(242, 184, 107, 255));
            i = j;
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(line[i]))) {
            size_t j = i;
            while (j < n && (std::isalnum(static_cast<unsigned char>(line[j])) || line[j] == '.')) {
                ++j;
            }
            emitSpan(line.substr(i, j - i), IM_COL32(235, 143, 199, 255));
            i = j;
            continue;
        }
        if (IsIdentStart(line[i]) || line[i] == '@') {
            size_t j = i + 1;
            while (j < n && IsIdentChar(line[j])) {
                ++j;
            }
            const std::string_view word = line.substr(i, j - i);

            size_t look = j;
            while (look < n && line[look] == ' ') {
                ++look;
            }
            const bool isCall = (look < n && line[look] == '(');

            ImU32 col = IM_COL32(226, 232, 244, 255);
            if (line[i] == '@') {
                col = IM_COL32(245, 204, 90, 255);
            } else if (IsJavaKeyword(word)) {
                col = IM_COL32(196, 140, 250, 255);
            } else if (IsKyumistType(word)) {
                col = IM_COL32(90, 208, 250, 255);
            } else if (IsAllCapsConstant(word)) {
                col = IM_COL32(110, 230, 195, 255);
            } else if (isCall) {
                col = IM_COL32(240, 222, 138, 255);
            }
            emitSpan(word, col);
            i = j;
            continue;
        }

        size_t j = i + 1;
        while (j < n &&
               !IsIdentStart(line[j]) &&
               !std::isdigit(static_cast<unsigned char>(line[j])) &&
               line[j] != '"' && line[j] != '\'' && line[j] != '@' &&
               !(j + 1 < n && line[j] == '/' && line[j + 1] == '/')) {
            ++j;
        }
        emitSpan(line.substr(i, j - i), IM_COL32(175, 188, 210, 255));
        i = j;
    }
}

ImU32 KindBadgeColor(std::string_view kind) {
    if (kind == "Method") {
        return IM_COL32(240, 212, 110, 255);
    }
    if (kind == "Keyword") {
        return IM_COL32(196, 140, 250, 255);
    }
    if (kind == "Class") {
        return IM_COL32(90, 208, 250, 255);
    }
    if (kind == "Const") {
        return IM_COL32(110, 230, 195, 255);
    }
    if (kind == "Snippet") {
        return IM_COL32(104, 219, 148, 255);
    }
    return IM_COL32(180, 192, 215, 255);
}

} // namespace

void ScriptEditorPanel::OpenScript(const std::filesystem::path& filePath) {
    std::error_code ec;
    const std::filesystem::path absPath = std::filesystem::weakly_canonical(filePath, ec);
    const std::filesystem::path target = ec ? filePath : absPath;

    for (size_t i = 0; i < m_Documents.size(); ++i) {
        if (m_Documents[i].filePath == target) {
            m_ActiveTabIndex = static_cast<int>(i);
            m_FocusRequested = true;
            m_AutocompleteOpen = false;
            return;
        }
    }

    std::ifstream in(target);
    if (!in.is_open()) {
        KM_ERROR("No se pudo abrir el script '{}'.", target.string());
        return;
    }

    std::ostringstream ss;
    ss << in.rdbuf();

    OpenScriptDocument doc;
    doc.filePath = target;
    doc.fileName = target.filename().string();
    doc.content = ss.str();
    doc.content.reserve(std::max<size_t>(4096, doc.content.size() * 2));
    doc.lastCommittedContent = doc.content;
    doc.lastCommittedCursor = 0;
    doc.lastEditTimestamp = -10.0;
    doc.dirty = false;

    m_Documents.push_back(std::move(doc));
    m_ActiveTabIndex = static_cast<int>(m_Documents.size() - 1);
    m_FocusRequested = true;
    m_AutocompleteOpen = false;
}

bool ScriptEditorPanel::CreateNewScriptFile(const std::filesystem::path& scriptsDir, const std::string& rawName) {
    const std::string className = SanitizeClassName(rawName);
    if (className.empty()) {
        KM_WARN("Nombre de script invalido.");
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(scriptsDir, ec);
    const std::filesystem::path filePath = scriptsDir / (className + ".java");

    if (!std::filesystem::exists(filePath, ec)) {
        std::ofstream out(filePath);
        if (!out.is_open()) {
            KM_ERROR("No se pudo crear '{}'.", filePath.string());
            return false;
        }
        out << "import dev.kyumist.Audio;\n"
            << "import dev.kyumist.Input;\n"
            << "import dev.kyumist.KyumistScript;\n\n"
            << "public class " << className << " extends KyumistScript {\n"
            << "    @Override\n"
            << "    public void onCreate() {\n"
            << "        logInfo(\"" << className << " iniciado en entidad \" + entity.getId());\n"
            << "    }\n\n"
            << "    @Override\n"
            << "    public void onUpdate(float dt) {\n"
            << "        // Escribe aqui la logica por frame\n"
            << "    }\n\n"
            << "    @Override\n"
            << "    public void onDestroy() {\n"
            << "        logInfo(\"" << className << " detenido.\");\n"
            << "    }\n"
            << "}\n";
    }

    OpenScript(filePath);
    return true;
}

void ScriptEditorPanel::OpenFirstAvailableScript() {
    const std::filesystem::path& dir = ScriptEngine::GetScriptsDirectory();
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec)) {
        return;
    }
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".java") {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    if (!files.empty()) {
        OpenScript(files.front());
    }
}

void ScriptEditorPanel::CloseAll() {
    m_Documents.clear();
    m_ActiveTabIndex = -1;
    m_IsWindowFocused = false;
    m_AutocompleteOpen = false;
    m_Suggestions.clear();
}

void ScriptEditorPanel::SyncDocumentCursorStats(OpenScriptDocument& doc) {
    doc.cursorPos = std::clamp(doc.cursorPos, 0, static_cast<int>(doc.content.size()));

    int line = 1;
    int col = 1;
    for (int i = 0; i < doc.cursorPos; ++i) {
        const unsigned char uc = static_cast<unsigned char>(doc.content[static_cast<size_t>(i)]);
        if (uc == '\n') {
            ++line;
            col = 1;
        } else if ((uc & 0xC0) != 0x80) {
            ++col;
        }
    }
    doc.cursorLine = line;
    doc.cursorColumn = col;
}

void ScriptEditorPanel::RecordUndoSnapshot(
    OpenScriptDocument& doc,
    const std::string& beforeContent,
    int beforeCursor,
    bool forceDiscrete
) {
    if (beforeContent == doc.content) {
        return;
    }

    const double now = ImGui::GetTime();
    const int sizeDelta = static_cast<int>(doc.content.size()) - static_cast<int>(beforeContent.size());
    const bool continuousSingleInsert =
        (sizeDelta == 1) &&
        (doc.cursorPos == beforeCursor + 1) &&
        !forceDiscrete &&
        !doc.undoStack.empty() &&
        ((now - doc.lastEditTimestamp) < 0.45);

    if (!continuousSingleInsert) {
        doc.undoStack.push_back(ScriptUndoEntry{beforeContent, beforeCursor});
        if (doc.undoStack.size() > 256) {
            doc.undoStack.erase(doc.undoStack.begin());
        }
    }

    doc.redoStack.clear();
    doc.lastEditTimestamp = now;
}

bool ScriptEditorPanel::UndoDocument(OpenScriptDocument& doc) {
    if (doc.undoStack.empty()) {
        return false;
    }

    doc.redoStack.push_back(ScriptUndoEntry{doc.content, doc.cursorPos});
    ScriptUndoEntry entry = std::move(doc.undoStack.back());
    doc.undoStack.pop_back();

    doc.content = std::move(entry.content);
    if (doc.content.capacity() < doc.content.size() + 1024) {
        doc.content.reserve(std::max<size_t>(4096, doc.content.size() + 2048));
    }
    doc.cursorPos = std::clamp(entry.cursorPos, 0, static_cast<int>(doc.content.size()));
    doc.lastCommittedContent = doc.content;
    doc.lastCommittedCursor = doc.cursorPos;
    doc.lastEditTimestamp = -10.0;
    doc.dirty = true;
    m_AutocompleteOpen = false;
    m_RestoreCursorPos = doc.cursorPos;
    SyncDocumentCursorStats(doc);

    if (doc.lastEditorId != 0) {
        if (ImGuiInputTextState* state = ImGui::GetInputTextState(doc.lastEditorId)) {
            state->ReloadUserBuf = true;
            state->ReloadSelectionStart = doc.cursorPos;
            state->ReloadSelectionEnd = doc.cursorPos;
        }
    }
    return true;
}

bool ScriptEditorPanel::RedoDocument(OpenScriptDocument& doc) {
    if (doc.redoStack.empty()) {
        return false;
    }

    doc.undoStack.push_back(ScriptUndoEntry{doc.content, doc.cursorPos});
    ScriptUndoEntry entry = std::move(doc.redoStack.back());
    doc.redoStack.pop_back();

    doc.content = std::move(entry.content);
    if (doc.content.capacity() < doc.content.size() + 1024) {
        doc.content.reserve(std::max<size_t>(4096, doc.content.size() + 2048));
    }
    doc.cursorPos = std::clamp(entry.cursorPos, 0, static_cast<int>(doc.content.size()));
    doc.lastCommittedContent = doc.content;
    doc.lastCommittedCursor = doc.cursorPos;
    doc.lastEditTimestamp = -10.0;
    doc.dirty = true;
    m_AutocompleteOpen = false;
    m_RestoreCursorPos = doc.cursorPos;
    SyncDocumentCursorStats(doc);

    if (doc.lastEditorId != 0) {
        if (ImGuiInputTextState* state = ImGui::GetInputTextState(doc.lastEditorId)) {
            state->ReloadUserBuf = true;
            state->ReloadSelectionStart = doc.cursorPos;
            state->ReloadSelectionEnd = doc.cursorPos;
        }
    }
    return true;
}

bool ScriptEditorPanel::SaveDocument(OpenScriptDocument& doc) {
    std::ofstream out(doc.filePath, std::ios::trunc);
    if (!out.is_open()) {
        KM_ERROR("Error al guardar '{}'.", doc.filePath.string());
        return false;
    }
    out << doc.content;
    doc.dirty = false;
    return true;
}

void ScriptEditorPanel::SaveAndHotReload(Scene& scene, bool isPlaying) {
    for (auto& doc : m_Documents) {
        if (doc.dirty) {
            SaveDocument(doc);
        }
    }
    ScriptEngine::CompileAndReloadScripts(&scene, isPlaying);
}

std::unordered_set<int> ScriptEditorPanel::ParseErrorLinesForFile(const std::string& fileName) const {
    std::unordered_set<int> lines;
    const std::string& output = ScriptEngine::GetLastCompileOutput();
    if (ScriptEngine::DidLastCompileSucceed() || output.empty()) {
        return lines;
    }

    std::istringstream iss(output);
    std::string line;
    while (std::getline(iss, line)) {
        const size_t pos = line.find(fileName + ":");
        if (pos == std::string::npos) {
            continue;
        }
        const size_t colon1 = pos + fileName.size() + 1;
        const size_t colon2 = line.find(':', colon1);
        if (colon2 != std::string::npos) {
            try {
                const int lineNum = std::stoi(line.substr(colon1, colon2 - colon1));
                if (lineNum > 0) {
                    lines.insert(lineNum);
                }
            } catch (const std::exception&) {
            }
        }
    }
    return lines;
}

void ScriptEditorPanel::UpdateAutocomplete(const OpenScriptDocument& doc, bool forceShow) {
    m_Suggestions.clear();
    m_SelectedSuggestion = 0;

    const int cursor = std::clamp(doc.cursorPos, 0, static_cast<int>(doc.content.size()));
    int lineStart = cursor;
    while (lineStart > 0 && doc.content[static_cast<size_t>(lineStart - 1)] != '\n') {
        --lineStart;
    }

    // No abrir autocompletado dentro de comentarios de linea o cadenas
    bool inString = false;
    bool inChar = false;
    for (int i = lineStart; i < cursor; ++i) {
        const char ch = doc.content[static_cast<size_t>(i)];
        const char next = (i + 1 < cursor) ? doc.content[static_cast<size_t>(i + 1)] : '\0';
        if (!inString && !inChar && ch == '/' && next == '/') {
            m_AutocompleteOpen = false;
            return;
        }
        if (ch == '"' && !inChar && (i == lineStart || doc.content[static_cast<size_t>(i - 1)] != '\\')) {
            inString = !inString;
        } else if (ch == '\'' && !inString && (i == lineStart || doc.content[static_cast<size_t>(i - 1)] != '\\')) {
            inChar = !inChar;
        }
    }
    if (inString || inChar) {
        m_AutocompleteOpen = false;
        return;
    }

    int wordStart = cursor;
    while (wordStart > lineStart) {
        const char ch = doc.content[static_cast<size_t>(wordStart - 1)];
        if (IsIdentChar(ch) || ch == '@') {
            --wordStart;
        } else {
            break;
        }
    }

    m_CompletionReplaceStart = wordStart;
    m_CompletionPrefix = doc.content.substr(
        static_cast<size_t>(wordStart),
        static_cast<size_t>(cursor - wordStart)
    );

    const bool hasDot = (wordStart > lineStart && doc.content[static_cast<size_t>(wordStart - 1)] == '.');
    std::string receiver;
    if (hasDot) {
        int recvEnd = wordStart - 1;
        int recvStart = recvEnd;
        while (recvStart > lineStart && IsIdentChar(doc.content[static_cast<size_t>(recvStart - 1)])) {
            --recvStart;
        }
        receiver = doc.content.substr(
            static_cast<size_t>(recvStart),
            static_cast<size_t>(recvEnd - recvStart)
        );
    }

    std::vector<AutocompleteItem> pool;
    const std::string lineIndent = GetCurrentLineIndent(doc.content, cursor);

    if (hasDot) {
        if (receiver == "entity") {
            pool = {
                {"getId()", "getId()", "long Entity.getId()", "Method", 0},
                {"getName()", "getName()", "String Entity.getName()", "Method", 0},
                {"setName(String name)", "setName(\"\")", "void Entity.setName(String)", "Method", 2},
                {"getX()", "getX()", "float Entity.getX()", "Method", 0},
                {"getY()", "getY()", "float Entity.getY()", "Method", 0},
                {"getZ()", "getZ()", "float Entity.getZ()", "Method", 0},
                {"getPosition(float[] outXYZ)", "getPosition()", "void Entity.getPosition(float[])", "Method", 1},
                {"setPosition(x, y, z)", "setPosition(0.0f, 0.0f, 0.0f)", "void Entity.setPosition(float, float, float)", "Method", 0},
                {"translate(dx, dy, dz)", "translate(0.0f, 0.0f, 0.0f)", "void Entity.translate(float, float, float)", "Method", 0},
                {"getRotation(float[] outXYZ)", "getRotation()", "void Entity.getRotation(float[])", "Method", 1},
                {"setRotation(rx, ry, rz)", "setRotation(0.0f, 0.0f, 0.0f)", "void Entity.setRotation(float, float, float)", "Method", 0},
                {"rotate(drx, dry, drz)", "rotate(0.0f, 45.0f * dt, 0.0f)", "void Entity.rotate(float, float, float)", "Method", 0},
                {"getScale(float[] outXYZ)", "getScale()", "void Entity.getScale(float[])", "Method", 1},
                {"setScale(sx, sy, sz)", "setScale(1.0f, 1.0f, 1.0f)", "void Entity.setScale(float, float, float)", "Method", 0},
                {"setColor(r, g, b, a)", "setColor(1.0f, 1.0f, 1.0f, 1.0f)", "void Entity.setColor(float, float, float, float)", "Method", 0},
                {"setUIText(String text)", "setUIText(\"\")", "void Entity.setUIText(String)", "Method", 2},
                {"getVelocity(float[] outXYZ)", "getVelocity()", "void Entity.getVelocity(float[])", "Method", 1},
                {"setVelocity(vx, vy, vz)", "setVelocity(0.0f, 0.0f, 0.0f)", "void Entity.setVelocity(float, float, float)", "Method", 0},
                {"getAngularVelocity(float[] outXYZ)", "getAngularVelocity()", "void Entity.getAngularVelocity(float[])", "Method", 1},
                {"setAngularVelocity(wx, wy, wz)", "setAngularVelocity(0.0f, 45.0f, 0.0f)", "void Entity.setAngularVelocity(float, float, float)", "Method", 0},
                {"applyImpulse(ix, iy, iz)", "applyImpulse(0.0f, 5.0f, 0.0f)", "void Entity.applyImpulse(float, float, float)", "Method", 0},
                {"destroy()", "destroy()", "void Entity.destroy()", "Method", 0}
            };
        } else if (receiver == "Input") {
            pool = {
                {"isKeyDown(int keyCode)", "isKeyDown(Input.KEY_W)", "boolean Input.isKeyDown(int)", "Method", 0},
                {"isMouseButtonDown(int button)", "isMouseButtonDown(Input.MOUSE_LEFT)", "boolean Input.isMouseButtonDown(int)", "Method", 0},
                {"getMouseX()", "getMouseX()", "float Input.getMouseX()", "Method", 0},
                {"getMouseY()", "getMouseY()", "float Input.getMouseY()", "Method", 0},
                {"MOUSE_LEFT", "MOUSE_LEFT", "Boton izquierdo raton (0)", "Const", 0},
                {"MOUSE_RIGHT", "MOUSE_RIGHT", "Boton derecho raton (1)", "Const", 0},
                {"MOUSE_MIDDLE", "MOUSE_MIDDLE", "Boton central raton (2)", "Const", 0},
                {"KEY_W", "KEY_W", "Tecla W (87)", "Const", 0},
                {"KEY_A", "KEY_A", "Tecla A (65)", "Const", 0},
                {"KEY_S", "KEY_S", "Tecla S (83)", "Const", 0},
                {"KEY_D", "KEY_D", "Tecla D (68)", "Const", 0},
                {"KEY_Q", "KEY_Q", "Tecla Q (81)", "Const", 0},
                {"KEY_E", "KEY_E", "Tecla E (69)", "Const", 0},
                {"KEY_SPACE", "KEY_SPACE", "Barra espaciadora (32)", "Const", 0},
                {"KEY_UP", "KEY_UP", "Flecha Arriba (265)", "Const", 0},
                {"KEY_DOWN", "KEY_DOWN", "Flecha Abajo (264)", "Const", 0},
                {"KEY_LEFT", "KEY_LEFT", "Flecha Izquierda (263)", "Const", 0},
                {"KEY_RIGHT", "KEY_RIGHT", "Flecha Derecha (262)", "Const", 0}
            };
        } else if (receiver == "Audio") {
            pool = {
                {"playSound(String path)", "playSound(\"audio/bounce.wav\")", "void Audio.playSound(String)", "Method", 0},
                {"playSound(String path, float volume)", "playSound(\"audio/bounce.wav\", 1.0f)", "void Audio.playSound(String, float)", "Method", 0}
            };
        } else if (receiver == "Math") {
            pool = {
                {"sin(double a)", "sin(0.0)", "double Math.sin(double)", "Method", 1},
                {"cos(double a)", "cos(0.0)", "double Math.cos(double)", "Method", 1},
                {"tan(double a)", "tan(0.0)", "double Math.tan(double)", "Method", 1},
                {"abs(float a)", "abs()", "float Math.abs(float)", "Method", 1},
                {"min(float a, float b)", "min(, )", "float Math.min(float, float)", "Method", 3},
                {"max(float a, float b)", "max(, )", "float Math.max(float, float)", "Method", 3},
                {"sqrt(double a)", "sqrt()", "double Math.sqrt(double)", "Method", 1},
                {"PI", "PI", "double Math.PI", "Const", 0}
            };
        }
    } else if (!m_CompletionPrefix.empty() || forceShow) {
        const std::string innerIndent = lineIndent + "    ";
        pool = {
            // Snippets estructurales con sangria automatica
            {
                "for (bucle)",
                "for (int i = 0; i < 10; i++) {\n" + innerIndent + "\n" + lineIndent + "}",
                "Bucle for con sangria automatica",
                "Snippet",
                static_cast<int>(lineIndent.size()) + 2
            },
            {
                "if (condicion)",
                "if () {\n" + innerIndent + "\n" + lineIndent + "}",
                "Bloque condicional if { ... }",
                "Snippet",
                static_cast<int>(innerIndent.size() + lineIndent.size()) + 6
            },
            {
                "else { ... }",
                "else {\n" + innerIndent + "\n" + lineIndent + "}",
                "Bloque else { ... }",
                "Snippet",
                static_cast<int>(lineIndent.size()) + 2
            },
            {
                "while (bucle)",
                "while () {\n" + innerIndent + "\n" + lineIndent + "}",
                "Bucle while { ... }",
                "Snippet",
                static_cast<int>(innerIndent.size() + lineIndent.size()) + 6
            },
            {
                "onCreate()",
                "@Override\n" + lineIndent + "public void onCreate() {\n" + innerIndent + "\n" + lineIndent + "}",
                "Ciclo de vida KyumistScript",
                "Snippet",
                static_cast<int>(lineIndent.size()) + 2
            },
            {
                "onUpdate(float dt)",
                "@Override\n" + lineIndent + "public void onUpdate(float dt) {\n" + innerIndent + "\n" + lineIndent + "}",
                "Logica por frame de KyumistScript",
                "Snippet",
                static_cast<int>(lineIndent.size()) + 2
            },
            {
                "onCollisionEnter(Entity other)",
                "@Override\n" + lineIndent + "public void onCollisionEnter(Entity other) {\n" + innerIndent + "\n" + lineIndent + "}",
                "Callback de colision / trigger",
                "Snippet",
                static_cast<int>(lineIndent.size()) + 2
            },
            {
                "onDestroy()",
                "@Override\n" + lineIndent + "public void onDestroy() {\n" + innerIndent + "\n" + lineIndent + "}",
                "Limpieza al detener escena",
                "Snippet",
                static_cast<int>(lineIndent.size()) + 2
            },

            // API Kyumist
            {"entity", "entity", "Entity actual del script", "Class", 0},
            {"Input", "Input", "dev.kyumist.Input", "Class", 0},
            {"Audio", "Audio", "dev.kyumist.Audio", "Class", 0},
            {"KyumistScript", "KyumistScript", "Clase base de scripts Kyumist", "Class", 0},
            {"findEntity(String name)", "findEntity(\"\")", "Entity KyumistScript.findEntity(String)", "Method", 2},
            {"spawnCube(name, x, y, z)", "spawnCube(\"Cube\", 0.0f, 3.0f, 0.0f)", "Entity KyumistScript.spawnCube(...)", "Method", 0},
            {"raycast(ox, oy, oz, dx, dy, dz, dist)", "raycast(0.0f, 1.0f, 0.0f, 0.0f, 0.0f, -1.0f, 20.0f)", "Entity KyumistScript.raycast(...)", "Method", 0},
            {"logInfo(String)", "logInfo(\"\");", "void KyumistScript.logInfo(String)", "Method", 3},
            {"logWarn(String)", "logWarn(\"\");", "void KyumistScript.logWarn(String)", "Method", 3},
            {"logError(String)", "logError(\"\");", "void KyumistScript.logError(String)", "Method", 3},

            // Palabras clave Java frecuentes
            {"@Override", "@Override", "Anotacion de sobreescritura", "Keyword", 0},
            {"public", "public", "Modificador publico", "Keyword", 0},
            {"private", "private", "Modificador privado", "Keyword", 0},
            {"protected", "protected", "Modificador protegido", "Keyword", 0},
            {"void", "void", "Sin valor de retorno", "Keyword", 0},
            {"float", "float", "Numero decimal 32-bit", "Keyword", 0},
            {"int", "int", "Entero 32-bit", "Keyword", 0},
            {"double", "double", "Numero decimal 64-bit", "Keyword", 0},
            {"boolean", "boolean", "Valor logico true/false", "Keyword", 0},
            {"String", "String", "Cadena de texto", "Class", 0},
            {"Math", "Math", "java.lang.Math", "Class", 0},
            {"return", "return ", "Retornar de funcion", "Keyword", 0},
            {"new", "new ", "Instanciar objeto", "Keyword", 0},
            {"true", "true", "Literal booleano verdadero", "Keyword", 0},
            {"false", "false", "Literal booleano falso", "Keyword", 0},
            {"class", "class ", "Declaracion de clase", "Keyword", 0},
            {"extends", "extends ", "Herencia de clase", "Keyword", 0},
            {"import", "import dev.kyumist.;", "Importar paquete Kyumist", "Keyword", 1},
            {"System.out.println", "System.out.println();", "Salida estandar", "Method", 2}
        };

        // Identificadores definidos en el propio script actual
        std::set<std::string> seenLabels;
        for (const auto& item : pool) {
            seenLabels.insert(item.label);
        }

        const size_t n = doc.content.size();
        for (size_t i = 0; i < n;) {
            if (IsIdentStart(doc.content[i])) {
                size_t j = i + 1;
                while (j < n && IsIdentChar(doc.content[j])) {
                    ++j;
                }
                const std::string ident = doc.content.substr(i, j - i);
                if (ident.size() >= 2 && ident != m_CompletionPrefix && seenLabels.insert(ident).second) {
                    pool.push_back({ident, ident, "Simbolo del script actual", "Var", 0});
                }
                i = j;
            } else {
                ++i;
            }
        }
    }

    const std::string prefixLower = ToLowerAscii(m_CompletionPrefix);
    struct ScoredItem {
        int score;
        AutocompleteItem item;
    };
    std::vector<ScoredItem> matches;

    for (auto& item : pool) {
        const int sc = MatchScore(item.label, prefixLower);
        if (sc > 0) {
            matches.push_back({sc, std::move(item)});
        }
    }

    std::sort(matches.begin(), matches.end(), [](const ScoredItem& a, const ScoredItem& b) {
        if (a.score != b.score) {
            return a.score > b.score;
        }
        return a.item.label < b.item.label;
    });

    const size_t maxSuggestions = std::min<size_t>(matches.size(), 10);
    for (size_t i = 0; i < maxSuggestions; ++i) {
        m_Suggestions.push_back(std::move(matches[i].item));
    }

    m_AutocompleteOpen = !m_Suggestions.empty();
}

void ScriptEditorPanel::AcceptAutocomplete(ImGuiInputTextCallbackData* data, OpenScriptDocument& doc) {
    if (!m_AutocompleteOpen || m_Suggestions.empty()) {
        return;
    }
    const int idx = std::clamp(m_SelectedSuggestion, 0, static_cast<int>(m_Suggestions.size()) - 1);
    const AutocompleteItem& chosen = m_Suggestions[static_cast<size_t>(idx)];

    const int replaceStart = std::clamp(m_CompletionReplaceStart, 0, data->BufTextLen);
    const int currentCursor = std::clamp(data->CursorPos, replaceStart, data->BufTextLen);
    const int deleteLen = currentCursor - replaceStart;

    if (deleteLen > 0) {
        data->DeleteChars(replaceStart, deleteLen);
    }
    data->InsertChars(replaceStart, chosen.insertText.c_str());

    const int endPos = replaceStart + static_cast<int>(chosen.insertText.size());
    data->CursorPos = std::clamp(endPos - chosen.cursorBacktrack, 0, data->BufTextLen);
    data->SelectionStart = data->SelectionEnd = data->CursorPos;

    doc.content.assign(data->Buf, static_cast<size_t>(data->BufTextLen));
    doc.cursorPos = data->CursorPos;
    doc.dirty = true;

    m_AutocompleteOpen = false;
    m_Suggestions.clear();
}

int ScriptEditorPanel::StaticEditorCallback(ImGuiInputTextCallbackData* data) {
    auto* ctx = static_cast<EditorCallbackContext*>(data->UserData);
    if (!ctx || !ctx->doc) {
        return 0;
    }

    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        std::string* str = &ctx->doc->content;
        str->resize(static_cast<size_t>(data->BufTextLen));
        data->Buf = str->data();
        return 0;
    }

    if (ctx->panel) {
        return ctx->panel->HandleEditorCallback(data, *ctx->doc);
    }
    return 0;
}

int ScriptEditorPanel::HandleEditorCallback(ImGuiInputTextCallbackData* data, OpenScriptDocument& doc) {
    if (data->EventFlag == ImGuiInputTextFlags_CallbackCharFilter) {
        const ImWchar c = data->EventChar;

        if (m_AutocompleteOpen && !m_Suggestions.empty()) {
            if (c == '\t' || c == '\n' || c == '\r') {
                m_PendingAcceptCompletion = true;
                return 1;
            }
        }
        if (c == '\t') {
            m_PendingTabSpaces = true;
            return 1;
        }
        if (c == '\n') {
            m_PendingAutoIndent = true;
            return 0;
        }
        if (c == '}') {
            m_PendingCloseDedent = true;
            return 0;
        }
        return 0;
    }

    if (m_RestoreCursorPos >= 0) {
        data->CursorPos = std::clamp(m_RestoreCursorPos, 0, data->BufTextLen);
        data->SelectionStart = data->SelectionEnd = data->CursorPos;
        m_RestoreCursorPos = -1;
    }

    bool modifiedHere = false;

    if (m_NavDelta != 0 && m_AutocompleteOpen && !m_Suggestions.empty()) {
        data->CursorPos = std::clamp(doc.cursorPos, 0, data->BufTextLen);
        data->SelectionStart = data->SelectionEnd = data->CursorPos;
        const int count = static_cast<int>(m_Suggestions.size());
        m_SelectedSuggestion = (m_SelectedSuggestion + m_NavDelta + count * 4) % count;
        m_NavDelta = 0;
    }

    if (m_ClickedSuggestion >= 0 && m_AutocompleteOpen && !m_Suggestions.empty()) {
        data->CursorPos = std::clamp(doc.cursorPos, 0, data->BufTextLen);
        data->SelectionStart = data->SelectionEnd = data->CursorPos;
        m_SelectedSuggestion = std::clamp(m_ClickedSuggestion, 0, static_cast<int>(m_Suggestions.size()) - 1);
        m_ClickedSuggestion = -1;
        AcceptAutocomplete(data, doc);
        m_RestoreCursorPos = data->CursorPos;
        m_FocusEditorRequested = true;
        modifiedHere = true;
    } else if (m_PendingAcceptCompletion) {
        m_PendingAcceptCompletion = false;
        AcceptAutocomplete(data, doc);
        modifiedHere = true;
    }

    if (m_PendingTabSpaces) {
        m_PendingTabSpaces = false;
        if (data->HasSelection()) {
            const int selMin = std::min(data->SelectionStart, data->SelectionEnd);
            const int selMax = std::max(data->SelectionStart, data->SelectionEnd);
            data->DeleteChars(selMin, selMax - selMin);
        }
        int lineStart = data->CursorPos;
        while (lineStart > 0 && data->Buf[lineStart - 1] != '\n') {
            --lineStart;
        }
        const int col = data->CursorPos - lineStart;
        const int spaceCount = 4 - (col % 4);
        const std::string spaces(static_cast<size_t>(spaceCount), ' ');
        data->InsertChars(data->CursorPos, spaces.c_str());
        modifiedHere = true;
    }

    if (m_PendingAutoIndent) {
        m_PendingAutoIndent = false;
        if (data->CursorPos > 0 && data->Buf[data->CursorPos - 1] == '\n') {
            const int insertPos = data->CursorPos;
            const int prevLineEnd = insertPos - 1;
            int prevLineStart = prevLineEnd;
            while (prevLineStart > 0 && data->Buf[prevLineStart - 1] != '\n') {
                --prevLineStart;
            }

            const int baseIndent = CountLeadingIndentWidth(data->Buf, prevLineStart, prevLineEnd);
            int lastNonWs = prevLineEnd - 1;
            while (lastNonWs >= prevLineStart &&
                   (data->Buf[lastNonWs] == ' ' || data->Buf[lastNonWs] == '\t' || data->Buf[lastNonWs] == '\r')) {
                --lastNonWs;
            }

            if (lastNonWs >= prevLineStart && data->Buf[lastNonWs] == '{') {
                const int targetIndent = baseIndent + 4;
                int openBraces = 0;
                int closeBraces = 0;
                ComputeNetBraceDelta(data->Buf, data->BufTextLen, openBraces, closeBraces);

                const char nextChar = (insertPos < data->BufTextLen) ? data->Buf[insertPos] : '\0';
                if (nextChar == '}') {
                    const std::string insertion =
                        std::string(static_cast<size_t>(targetIndent), ' ') + "\n" +
                        std::string(static_cast<size_t>(baseIndent), ' ');
                    data->InsertChars(insertPos, insertion.c_str());
                    data->CursorPos = insertPos + targetIndent;
                    data->SelectionStart = data->SelectionEnd = data->CursorPos;
                } else if (openBraces > closeBraces) {
                    const std::string insertion =
                        std::string(static_cast<size_t>(targetIndent), ' ') + "\n" +
                        std::string(static_cast<size_t>(baseIndent), ' ') + "}";
                    data->InsertChars(insertPos, insertion.c_str());
                    data->CursorPos = insertPos + targetIndent;
                    data->SelectionStart = data->SelectionEnd = data->CursorPos;
                } else if (targetIndent > 0) {
                    const std::string spaces(static_cast<size_t>(targetIndent), ' ');
                    data->InsertChars(insertPos, spaces.c_str());
                }
            } else if (baseIndent > 0) {
                const std::string spaces(static_cast<size_t>(baseIndent), ' ');
                data->InsertChars(insertPos, spaces.c_str());
            }
            modifiedHere = true;
        }
    }

    if (m_PendingCloseDedent) {
        m_PendingCloseDedent = false;
        const int closePos = data->CursorPos - 1;
        if (closePos >= 0 && data->Buf[closePos] == '}') {
            int lineStart = closePos;
            while (lineStart > 0 && data->Buf[lineStart - 1] != '\n') {
                --lineStart;
            }
            bool onlySpacesBefore = true;
            for (int i = lineStart; i < closePos; ++i) {
                if (data->Buf[i] != ' ') {
                    onlySpacesBefore = false;
                    break;
                }
            }
            if (onlySpacesBefore) {
                const int leadingSpaces = closePos - lineStart;
                const int matchIndent = FindMatchingOpenBraceIndent(data->Buf, closePos);
                if (matchIndent >= 0 && leadingSpaces > matchIndent) {
                    data->DeleteChars(lineStart, leadingSpaces - matchIndent);
                    modifiedHere = true;
                } else if (matchIndent < 0 && leadingSpaces >= 4) {
                    data->DeleteChars(lineStart, 4);
                    modifiedHere = true;
                }
            }
        }
    }

    const bool textEdited = (data->EventFlag == ImGuiInputTextFlags_CallbackEdit) || modifiedHere;
    const int prevCursor = doc.cursorPos;

    if (textEdited) {
        const std::string beforeContent = doc.lastCommittedContent;
        const int beforeCursor = doc.lastCommittedCursor;
        doc.content.assign(data->Buf, static_cast<size_t>(data->BufTextLen));
        doc.cursorPos = std::clamp(data->CursorPos, 0, static_cast<int>(doc.content.size()));
        const char insertedChar = (doc.cursorPos > 0 && doc.cursorPos <= static_cast<int>(doc.content.size()))
            ? doc.content[static_cast<size_t>(doc.cursorPos - 1)]
            : '\0';
        const bool isBreakChar =
            (doc.content.size() > beforeContent.size()) &&
            (insertedChar == ' ' || insertedChar == '\n' || insertedChar == '\t' || insertedChar == '{' || insertedChar == '}');
        const bool forceDiscrete = modifiedHere ||
            (std::abs(static_cast<int>(doc.content.size()) - static_cast<int>(beforeContent.size())) != 1) ||
            isBreakChar;
        RecordUndoSnapshot(doc, beforeContent, beforeCursor, forceDiscrete);
        doc.lastCommittedContent = doc.content;
        doc.lastCommittedCursor = doc.cursorPos;
        doc.dirty = true;
    } else {
        doc.cursorPos = std::clamp(data->CursorPos, 0, static_cast<int>(doc.content.size()));
    }

    SyncDocumentCursorStats(doc);

    if (textEdited && !modifiedHere) {
        UpdateAutocomplete(doc);
    } else if (!textEdited && doc.cursorPos != prevCursor) {
        m_AutocompleteOpen = false;
    }

    return 0;
}

void ScriptEditorPanel::RenderSyntaxOverlayAndGutter(
    OpenScriptDocument& doc,
    const std::unordered_set<int>& errorLines,
    ImGuiID editorId,
    const ImVec2& gutterMin,
    const ImVec2& gutterMax
) {
    ImGuiWindow* parentWindow = ImGui::GetCurrentWindow();
    ImGuiWindow* drawWindow = nullptr;
    for (int i = parentWindow->DC.ChildWindows.Size - 1; i >= 0; --i) {
        ImGuiWindow* child = parentWindow->DC.ChildWindows[i];
        if (child && child->ChildId == editorId) {
            drawWindow = child;
            break;
        }
    }
    if (!drawWindow) {
        return;
    }

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = ImGui::GetStyle();
    ImFont* font = ImGui::GetFont();
    const float fontSize = ImGui::GetFontSize();

    const bool isActive = (g.ActiveId == editorId);
    const float scrollX = isActive ? g.InputTextState.Scroll.x : 0.0f;
    const float scrollY = drawWindow->Scroll.y;

    const ImVec2 textOrigin(
        drawWindow->Pos.x + style.FramePadding.x - scrollX,
        drawWindow->Pos.y + style.FramePadding.y - scrollY
    );
    const float gutterTopY = drawWindow->Pos.y + style.FramePadding.y - scrollY;

    // Fondo y separador del Gutter de numeros de linea
    ImDrawList* gutterDraw = ImGui::GetWindowDrawList();
    gutterDraw->AddRectFilled(gutterMin, gutterMax, IM_COL32(13, 15, 20, 255));
    gutterDraw->AddLine(
        ImVec2(gutterMax.x - 1.0f, gutterMin.y),
        ImVec2(gutterMax.x - 1.0f, gutterMax.y),
        IM_COL32(42, 47, 60, 255)
    );
    gutterDraw->PushClipRect(gutterMin, gutterMax, true);

    ImDrawList* codeDraw = drawWindow->DrawList;
    codeDraw->PushClipRect(drawWindow->InnerClipRect.Min, drawWindow->InnerClipRect.Max, true);

    const float viewTop = drawWindow->InnerClipRect.Min.y - fontSize;
    const float viewBottom = drawWindow->InnerClipRect.Max.y + fontSize;

    const std::string& text = doc.content;
    const size_t n = text.size();
    size_t lineStart = 0;
    int lineNum = 1;

    while (lineStart <= n) {
        size_t lineEnd = text.find('\n', lineStart);
        const bool hasNewline = (lineEnd != std::string::npos);
        if (!hasNewline) {
            lineEnd = n;
        }
        size_t visibleEnd = lineEnd;
        if (visibleEnd > lineStart && text[visibleEnd - 1] == '\r') {
            --visibleEnd;
        }

        const float y = textOrigin.y + static_cast<float>(lineNum - 1) * fontSize;
        if (y >= viewTop && y <= viewBottom) {
            const bool isCursorLine = (lineNum == doc.cursorLine);
            const bool hasError = (errorLines.count(lineNum) > 0);

            if (hasError) {
                codeDraw->AddRectFilled(
                    ImVec2(drawWindow->InnerClipRect.Min.x, y),
                    ImVec2(drawWindow->InnerClipRect.Max.x, y + fontSize),
                    IM_COL32(235, 58, 64, 42)
                );
                codeDraw->AddLine(
                    ImVec2(drawWindow->InnerClipRect.Min.x, y + fontSize - 1.0f),
                    ImVec2(drawWindow->InnerClipRect.Max.x, y + fontSize - 1.0f),
                    IM_COL32(245, 76, 82, 185)
                );
                gutterDraw->AddRectFilled(
                    ImVec2(gutterMin.x, y),
                    ImVec2(gutterMax.x - 1.0f, y + fontSize),
                    IM_COL32(235, 58, 64, 52)
                );
            } else if (isCursorLine && isActive) {
                codeDraw->AddRectFilled(
                    ImVec2(drawWindow->InnerClipRect.Min.x, y),
                    ImVec2(drawWindow->InnerClipRect.Max.x, y + fontSize),
                    IM_COL32(92, 146, 240, 20)
                );
                gutterDraw->AddRectFilled(
                    ImVec2(gutterMin.x, y),
                    ImVec2(gutterMax.x - 1.0f, y + fontSize),
                    IM_COL32(92, 146, 240, 26)
                );
            }

            char numBuf[16];
            if (hasError) {
                std::snprintf(numBuf, sizeof(numBuf), "!%3d", lineNum);
            } else {
                std::snprintf(numBuf, sizeof(numBuf), "%4d", lineNum);
            }
            const ImU32 numCol = hasError
                ? IM_COL32(250, 100, 106, 255)
                : (isCursorLine ? IM_COL32(130, 215, 255, 255) : IM_COL32(105, 118, 142, 255));
            const float numY = gutterTopY + static_cast<float>(lineNum - 1) * fontSize;
            gutterDraw->AddText(font, fontSize, ImVec2(gutterMin.x + 6.0f, numY), numCol, numBuf);

            const std::string_view lineView(text.data() + lineStart, visibleEnd - lineStart);
            DrawColoredJavaLine(codeDraw, font, fontSize, ImVec2(textOrigin.x, y), lineView);
        }

        if (!hasNewline) {
            break;
        }
        lineStart = lineEnd + 1;
        ++lineNum;
    }

    // Cursor (caret) visible con coordenada exacta para posicionar el popup de autocompletado
    const int clampedCursor = std::clamp(doc.cursorPos, 0, static_cast<int>(n));
    int curLineStart = clampedCursor;
    while (curLineStart > 0 && text[static_cast<size_t>(curLineStart - 1)] != '\n') {
        --curLineStart;
    }
    const float cursorOffsetX = font->CalcTextSizeA(
        fontSize,
        FLT_MAX,
        0.0f,
        text.data() + curLineStart,
        text.data() + clampedCursor
    ).x;
    const float cursorScreenX = IM_TRUNC(textOrigin.x + cursorOffsetX);
    const float cursorScreenY = IM_TRUNC(textOrigin.y + static_cast<float>(doc.cursorLine - 1) * fontSize);

    m_PopupScreenX = cursorScreenX;
    m_PopupScreenY = cursorScreenY + fontSize + 4.0f;

    if (isActive) {
        const float anim = g.InputTextState.CursorAnim;
        const bool cursorVisible = (!g.IO.ConfigInputTextCursorBlink) || (anim <= 0.0f) || (ImFmod(anim, 1.20f) <= 0.80f);
        if (cursorVisible) {
            codeDraw->AddRectFilled(
                ImVec2(cursorScreenX, cursorScreenY + 1.0f),
                ImVec2(cursorScreenX + 2.0f, cursorScreenY + fontSize - 1.0f),
                IM_COL32(120, 225, 255, 255)
            );
        }
    }

    codeDraw->PopClipRect();
    gutterDraw->PopClipRect();
}

void ScriptEditorPanel::Render(Scene& scene, bool isPlaying, bool* pOpen) {
    if (m_FocusRequested) {
        ImGui::SetNextWindowFocus();
        m_FocusRequested = false;
    }

    if (!ImGui::Begin("Script Editor", pOpen)) {
        m_IsWindowFocused = false;
        ImGui::End();
        return;
    }

    const ImGuiIO& io = ImGui::GetIO();
    ImFont* monoFont = (io.Fonts->Fonts.Size > 1) ? io.Fonts->Fonts[1] : nullptr;

    m_IsWindowFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

    if (m_IsWindowFocused && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) {
        SaveAndHotReload(scene, isPlaying);
    }

    if (m_IsWindowFocused &&
        !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId) &&
        m_ActiveTabIndex >= 0 &&
        m_ActiveTabIndex < static_cast<int>(m_Documents.size())) {
        auto& activeDoc = m_Documents[static_cast<size_t>(m_ActiveTabIndex)];
        const bool ctrlOnly = io.KeyCtrl && !io.KeyAlt && !io.KeySuper;
        if (ctrlOnly && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, true)) {
            UndoDocument(activeDoc);
        } else if (ctrlOnly && ((io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, true)) ||
                                (!io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Y, true)))) {
            RedoDocument(activeDoc);
        }
    }

    // Top toolbar
    if (ImGui::Button("+ Nuevo Script")) {
        ImGui::OpenPopup("CreateJavaScriptPopup");
    }
    if (ImGui::BeginPopup("CreateJavaScriptPopup")) {
        ImGui::Text("Nombre de la clase Java:");
        ImGui::InputText("##NewScriptClass", m_NewScriptName, sizeof(m_NewScriptName));
        if (ImGui::Button("Crear Script")) {
            if (CreateNewScriptFile(ScriptEngine::GetScriptsDirectory(), m_NewScriptName)) {
                ScriptEngine::CompileAndReloadScripts(&scene, isPlaying);
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }

    ImGui::SameLine();
    const bool hasDirty = std::any_of(m_Documents.begin(), m_Documents.end(), [](const auto& d) { return d.dirty; });
    if (hasDirty) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.54f, 0.30f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.68f, 0.38f, 1.0f));
    }
    if (ImGui::Button("Guardar y Hot-Reload (Ctrl+S)")) {
        SaveAndHotReload(scene, isPlaying);
    }
    if (hasDirty) {
        ImGui::PopStyleColor(2);
    }

    ImGui::SameLine();
    OpenScriptDocument* activeToolbarDoc =
        (m_ActiveTabIndex >= 0 && m_ActiveTabIndex < static_cast<int>(m_Documents.size()))
            ? &m_Documents[static_cast<size_t>(m_ActiveTabIndex)]
            : nullptr;
    const bool canUndoDoc = activeToolbarDoc && !activeToolbarDoc->undoStack.empty();
    const bool canRedoDoc = activeToolbarDoc && !activeToolbarDoc->redoStack.empty();

    ImGui::BeginDisabled(!canUndoDoc);
    if (ImGui::Button("Deshacer##ScriptUndo")) {
        if (activeToolbarDoc && UndoDocument(*activeToolbarDoc)) {
            m_FocusEditorRequested = true;
        }
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("Deshacer codigo (Ctrl+Z)");
    }

    ImGui::SameLine();
    ImGui::BeginDisabled(!canRedoDoc);
    if (ImGui::Button("Rehacer##ScriptRedo")) {
        if (activeToolbarDoc && RedoDocument(*activeToolbarDoc)) {
            m_FocusEditorRequested = true;
        }
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("Rehacer codigo (Ctrl+Shift+Z)");
    }

    ImGui::SameLine();
    if (ImGui::Button("Insertar Snippet")) {
        ImGui::OpenPopup("SnippetsPopup");
    }
    if (ImGui::BeginPopup("SnippetsPopup")) {
        auto insertSnippet = [&](const std::string& code) {
            if (m_ActiveTabIndex >= 0 && m_ActiveTabIndex < static_cast<int>(m_Documents.size())) {
                auto& doc = m_Documents[static_cast<size_t>(m_ActiveTabIndex)];
                const std::string beforeContent = doc.content;
                const int beforeCursor = doc.cursorPos;
                const int pos = std::clamp(doc.cursorPos, 0, static_cast<int>(doc.content.size()));
                doc.content.insert(static_cast<size_t>(pos), code);
                doc.cursorPos = pos + static_cast<int>(code.size());
                RecordUndoSnapshot(doc, beforeContent, beforeCursor, true);
                doc.lastCommittedContent = doc.content;
                doc.lastCommittedCursor = doc.cursorPos;
                doc.lastEditTimestamp = -10.0;
                doc.dirty = true;
                m_RestoreCursorPos = doc.cursorPos;
                m_FocusEditorRequested = true;
                SyncDocumentCursorStats(doc);
                if (doc.lastEditorId != 0) {
                    if (ImGuiInputTextState* state = ImGui::GetInputTextState(doc.lastEditorId)) {
                        state->ReloadUserBuf = true;
                        state->ReloadSelectionStart = doc.cursorPos;
                        state->ReloadSelectionEnd = doc.cursorPos;
                    }
                }
            }
        };
        if (ImGui::MenuItem("Bucle for (int i = 0; i < 10; i++)")) {
            insertSnippet("for (int i = 0; i < 10; i++) {\n    \n}");
        }
        if (ImGui::MenuItem("Rotar Entidad (onUpdate)")) {
            insertSnippet("entity.rotate(0.0f, 45.0f * dt, 0.0f);");
        }
        if (ImGui::MenuItem("Movimiento con Input WASD")) {
            insertSnippet("if (Input.isKeyDown(Input.KEY_W)) {\n    entity.translate(0.0f, 0.0f, -3.0f * dt);\n}");
        }
        if (ImGui::MenuItem("Impulso Fisico")) {
            insertSnippet("entity.applyImpulse(0.0f, 5.0f, 0.0f);");
        }
        if (ImGui::MenuItem("Reproducir Sonido")) {
            insertSnippet("Audio.playSound(\"audio/bounce.wav\", 0.8f);");
        }
        ImGui::EndPopup();
    }

    ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 14.0f, ImGui::GetWindowWidth() - 165.0f));
    if (ScriptEngine::DidLastCompileSucceed()) {
        ImGui::TextColored(ImVec4(0.22f, 0.88f, 0.48f, 1.0f), "javac: OK");
    } else {
        ImGui::TextColored(ImVec4(0.96f, 0.36f, 0.38f, 1.0f), "javac: ERRORES");
    }

    ImGui::Separator();

    const float totalHeight = ImGui::GetContentRegionAvail().y;
    const float compilerPaneHeight = 96.0f;
    const float mainHeight = std::max(150.0f, totalHeight - compilerPaneHeight - 10.0f);

    // Left sidebar: available scripts in project
    if (ImGui::BeginChild("ScriptSidebar", ImVec2(185.0f, mainHeight), true)) {
        ImGui::TextDisabled("Scripts (.java)");
        ImGui::Separator();

        const std::filesystem::path& scriptsDir = ScriptEngine::GetScriptsDirectory();
        std::error_code ec;
        std::vector<std::filesystem::path> files;
        if (std::filesystem::exists(scriptsDir, ec)) {
            for (const auto& entry : std::filesystem::directory_iterator(scriptsDir, ec)) {
                if (entry.is_regular_file() && entry.path().extension() == ".java") {
                    files.push_back(entry.path());
                }
            }
        }
        std::sort(files.begin(), files.end());

        for (const auto& file : files) {
            const std::string name = file.filename().string();
            const bool isOpenActive =
                (m_ActiveTabIndex >= 0 &&
                 m_ActiveTabIndex < static_cast<int>(m_Documents.size()) &&
                 m_Documents[static_cast<size_t>(m_ActiveTabIndex)].filePath == std::filesystem::weakly_canonical(file, ec));

            ImGui::TextColored(ImVec4(0.98f, 0.45f, 0.09f, 1.0f), "{J}");
            ImGui::SameLine(0.0f, 5.0f);
            if (ImGui::Selectable(name.c_str(), isOpenActive)) {
                OpenScript(file);
            }
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    bool drawPopup = false;
    int popupDisplayCount = 0;

    // Unified single editable syntax-highlighted code editor
    if (ImGui::BeginChild("ScriptMainArea", ImVec2(0.0f, mainHeight), false)) {
        if (m_Documents.empty()) {
            ImGui::Spacing();
            ImGui::TextDisabled("Haz clic en un archivo .java a la izquierda o desde FileSystem (res://) para editarlo.");
        } else if (ImGui::BeginTabBar("OpenScriptsTabBar", ImGuiTabBarFlags_AutoSelectNewTabs)) {
            int docToClose = -1;
            for (size_t idx = 0; idx < m_Documents.size(); ++idx) {
                auto& doc = m_Documents[idx];
                bool openTab = true;
                ImGuiTabItemFlags tabFlags = doc.dirty ? ImGuiTabItemFlags_UnsavedDocument : ImGuiTabItemFlags_None;
                if (ImGui::BeginTabItem(doc.fileName.c_str(), &openTab, tabFlags)) {
                    m_ActiveTabIndex = static_cast<int>(idx);

                    const std::unordered_set<int> errorLines = ParseErrorLinesForFile(doc.fileName);
                    const float statusBarH = 22.0f;
                    const float areaAvailW = ImGui::GetContentRegionAvail().x;
                    const float editorH = std::max(80.0f, ImGui::GetContentRegionAvail().y - statusBarH - 4.0f);
                    const float gutterWidth = 48.0f;
                    const float editorW = std::max(120.0f, areaAvailW - gutterWidth);

                    // Atajos de teclado antes de InputTextMultiline cuando el autocompletado esta visible
                    const ImGuiID popupOwnerId = ImGui::GetID("##AutocompletePopupOwner");
                    if (m_AutocompleteOpen && !m_Suggestions.empty()) {
                        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true)) {
                            m_NavDelta = 1;
                            ImGui::SetKeyOwner(ImGuiKey_DownArrow, popupOwnerId, ImGuiInputFlags_LockThisFrame);
                        } else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true)) {
                            m_NavDelta = -1;
                            ImGui::SetKeyOwner(ImGuiKey_UpArrow, popupOwnerId, ImGuiInputFlags_LockThisFrame);
                        } else if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                            m_AutocompleteOpen = false;
                            ImGui::SetKeyOwner(ImGuiKey_Escape, popupOwnerId, ImGuiInputFlags_LockThisFrame);
                        } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                            const ImVec2 mp = io.MousePos;
                            if (mp.x >= m_LastPopupMin.x && mp.x <= m_LastPopupMax.x &&
                                mp.y >= m_LastPopupMin.y && mp.y <= m_LastPopupMax.y) {
                                const float rowH = 22.0f;
                                const int rowIdx = static_cast<int>((mp.y - (m_LastPopupMin.y + 4.0f)) / rowH);
                                const int visibleMax = std::min(static_cast<int>(m_Suggestions.size()), 8);
                                if (rowIdx >= 0 && rowIdx < visibleMax) {
                                    m_ClickedSuggestion = rowIdx;
                                }
                            } else {
                                m_AutocompleteOpen = false;
                            }
                        }
                    }

                    // Ctrl+Espacio para invocar autocompletado manualmente
                    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
                        io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
                        UpdateAutocomplete(doc, true);
                    }

                    if (monoFont) {
                        ImGui::PushFont(monoFont);
                    }

                    // 1) Gutter para numeros de linea y marcadores de error
                    ImGui::PushID(static_cast<int>(idx));
                    ImGui::InvisibleButton("##LineGutter", ImVec2(gutterWidth, editorH));
                    const ImVec2 gutterMin = ImGui::GetItemRectMin();
                    const ImVec2 gutterMax = ImGui::GetItemRectMax();

                    ImGui::SameLine(0.0f, 0.0f);

                    // 2) Area unica editable con texto base transparente + overlay a color
                    if (doc.content.capacity() < doc.content.size() + 1024) {
                        doc.content.reserve(doc.content.size() + 2048);
                    }

                    EditorCallbackContext cbCtx{this, &doc};

                    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.06f, 0.07f, 0.095f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
                    ImGui::PushStyleColor(ImGuiCol_TextSelectedBg, ImVec4(0.22f, 0.45f, 0.78f, 0.42f));

                    if (m_FocusEditorRequested) {
                        ImGui::SetKeyboardFocusHere();
                        m_FocusEditorRequested = false;
                    }

                    const ImGuiInputTextFlags editorFlags =
                        ImGuiInputTextFlags_CallbackResize |
                        ImGuiInputTextFlags_CallbackAlways |
                        ImGuiInputTextFlags_CallbackEdit |
                        ImGuiInputTextFlags_CallbackCharFilter |
                        ImGuiInputTextFlags_AllowTabInput |
                        ImGuiInputTextFlags_NoUndoRedo;

                    if (ImGui::InputTextMultiline(
                            "##CodeEditor",
                            doc.content.data(),
                            doc.content.capacity() + 1,
                            ImVec2(editorW, editorH),
                            editorFlags,
                            &ScriptEditorPanel::StaticEditorCallback,
                            &cbCtx)) {
                        doc.dirty = true;
                    }
                    const ImGuiID editorId = ImGui::GetItemID();
                    doc.lastEditorId = editorId;

                    ImGui::PopStyleColor(3);

                    RenderSyntaxOverlayAndGutter(doc, errorLines, editorId, gutterMin, gutterMax);

                    ImGui::PopID();

                    if (monoFont) {
                        ImGui::PopFont();
                    }

                    // Barra inferior de estado del editor
                    ImGui::TextDisabled(
                        "Ln %d, Col %d  |  Auto-tab: 4 espacios  |  Ctrl+Z / Ctrl+Shift+Z: Deshacer/Rehacer  |  Ctrl+S: Guardar",
                        doc.cursorLine,
                        doc.cursorColumn
                    );

                    if (m_AutocompleteOpen && !m_Suggestions.empty()) {
                        drawPopup = true;
                        popupDisplayCount = std::min(static_cast<int>(m_Suggestions.size()), 8);
                    }

                    ImGui::EndTabItem();
                }
                if (!openTab) {
                    docToClose = static_cast<int>(idx);
                }
            }

            if (docToClose >= 0) {
                m_Documents.erase(m_Documents.begin() + docToClose);
                if (m_Documents.empty()) {
                    m_ActiveTabIndex = -1;
                    m_AutocompleteOpen = false;
                } else {
                    m_ActiveTabIndex = std::clamp(m_ActiveTabIndex, 0, static_cast<int>(m_Documents.size()) - 1);
                }
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::EndChild();

    // Popup flotante de autocompletado
    if (drawPopup && popupDisplayCount > 0) {
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        const float popupW = 430.0f;
        const float rowH = 22.0f;
        const float popupH = static_cast<float>(popupDisplayCount) * rowH + 8.0f;

        float px = m_PopupScreenX;
        float py = m_PopupScreenY;
        const float maxX = vp->WorkPos.x + vp->WorkSize.x - popupW - 8.0f;
        const float maxY = vp->WorkPos.y + vp->WorkSize.y - popupH - 8.0f;
        if (px > maxX) {
            px = std::max(vp->WorkPos.x + 8.0f, maxX);
        }
        if (py > maxY) {
            py = std::max(vp->WorkPos.y + 8.0f, m_PopupScreenY - popupH - 22.0f);
        }

        m_LastPopupMin = ImVec2(px, py);
        m_LastPopupMax = ImVec2(px + popupW, py + popupH);

        ImDrawList* fg = ImGui::GetForegroundDrawList();
        fg->AddRectFilled(m_LastPopupMin, m_LastPopupMax, IM_COL32(20, 23, 31, 248), 6.0f);
        fg->AddRect(m_LastPopupMin, m_LastPopupMax, IM_COL32(72, 118, 198, 220), 6.0f, 0, 1.5f);
        fg->PushClipRect(m_LastPopupMin, m_LastPopupMax, true);

        const ImVec2 mousePos = io.MousePos;
        for (int i = 0; i < popupDisplayCount; ++i) {
            const AutocompleteItem& item = m_Suggestions[static_cast<size_t>(i)];
            const ImVec2 rowMin(px + 4.0f, py + 4.0f + static_cast<float>(i) * rowH);
            const ImVec2 rowMax(px + popupW - 4.0f, rowMin.y + rowH);

            const bool hovered =
                mousePos.x >= rowMin.x && mousePos.x <= rowMax.x &&
                mousePos.y >= rowMin.y && mousePos.y <= rowMax.y;
            if (hovered) {
                m_SelectedSuggestion = i;
            }
            const bool selected = (i == m_SelectedSuggestion);
            if (selected) {
                fg->AddRectFilled(rowMin, rowMax, IM_COL32(42, 86, 158, 220), 4.0f);
            }

            const ImU32 badgeCol = KindBadgeColor(item.kind);
            const std::string tag = "[" + item.kind + "]";
            fg->AddText(ImVec2(rowMin.x + 6.0f, rowMin.y + 3.0f), badgeCol, tag.c_str());

            fg->AddText(
                ImVec2(rowMin.x + 82.0f, rowMin.y + 3.0f),
                selected ? IM_COL32(255, 255, 255, 255) : IM_COL32(226, 232, 244, 255),
                item.label.c_str()
            );

            if (!item.detail.empty()) {
                const ImVec2 detailSize = ImGui::CalcTextSize(item.detail.c_str());
                const float detailX = std::max(rowMin.x + 235.0f, rowMax.x - detailSize.x - 8.0f);
                fg->AddText(
                    ImVec2(detailX, rowMin.y + 3.0f),
                    IM_COL32(138, 152, 178, 230),
                    item.detail.c_str()
                );
            }
        }

        fg->PopClipRect();
    } else {
        m_LastPopupMin = ImVec2(0.0f, 0.0f);
        m_LastPopupMax = ImVec2(0.0f, 0.0f);
    }

    // Compiler output strip
    if (ImGui::BeginChild("JavaCompilerOutput", ImVec2(0.0f, compilerPaneHeight), true,
                          ImGuiWindowFlags_HorizontalScrollbar)) {
        if (monoFont) {
            ImGui::PushFont(monoFont);
        }
        const std::string& output = ScriptEngine::GetLastCompileOutput();
        if (output.empty()) {
            ImGui::TextDisabled("Salida del compilador javac...");
        } else {
            const ImVec4 col = ScriptEngine::DidLastCompileSucceed()
                ? ImVec4(0.32f, 0.85f, 0.56f, 1.0f)
                : ImVec4(0.96f, 0.38f, 0.38f, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, col);
            ImGui::TextUnformatted(output.c_str());
            ImGui::PopStyleColor();
        }
        if (monoFont) {
            ImGui::PopFont();
        }
    }
    ImGui::EndChild();

    ImGui::End();
}

} // namespace Kyumist
