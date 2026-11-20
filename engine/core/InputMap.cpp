#include "core/InputMap.hpp"

#include "core/Log.hpp"

#ifndef __ANDROID__
#include <GLFW/glfw3.h>
#endif
#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <unordered_set>

namespace Kyumist {

namespace {

struct InputMapState {
    std::unordered_map<std::string, std::vector<InputBinding>> actions;
    std::unordered_map<std::string, bool> currStates;
    std::unordered_map<std::string, bool> prevStates;
    std::unordered_set<std::string> virtualPressed;
    float touchAxisX = 0.0f;
    float touchAxisY = 0.0f;
    bool mouseCaptured = false;
    bool initialized = false;
};

InputMapState s_Input;

} // namespace

void InputMap::InitDefaults() {
    s_Input.actions.clear();
#ifndef __ANDROID__
    s_Input.actions["move_forward"] = {{InputDeviceType::Key, GLFW_KEY_W}, {InputDeviceType::Key, GLFW_KEY_UP}};
    s_Input.actions["move_back"] = {{InputDeviceType::Key, GLFW_KEY_S}, {InputDeviceType::Key, GLFW_KEY_DOWN}};
    s_Input.actions["move_left"] = {{InputDeviceType::Key, GLFW_KEY_A}, {InputDeviceType::Key, GLFW_KEY_LEFT}};
    s_Input.actions["move_right"] = {{InputDeviceType::Key, GLFW_KEY_D}, {InputDeviceType::Key, GLFW_KEY_RIGHT}};
    s_Input.actions["jump"] = {{InputDeviceType::Key, GLFW_KEY_SPACE}};
    s_Input.actions["action"] = {{InputDeviceType::Key, GLFW_KEY_E}, {InputDeviceType::MouseButton, GLFW_MOUSE_BUTTON_LEFT}};
    s_Input.actions["sprint"] = {{InputDeviceType::Key, GLFW_KEY_LEFT_SHIFT}};
    s_Input.actions["cancel"] = {{InputDeviceType::Key, GLFW_KEY_ESCAPE}};
#else
    s_Input.actions["move_forward"] = {};
    s_Input.actions["move_back"] = {};
    s_Input.actions["move_left"] = {};
    s_Input.actions["move_right"] = {};
    s_Input.actions["jump"] = {};
    s_Input.actions["action"] = {};
    s_Input.actions["sprint"] = {};
    s_Input.actions["cancel"] = {};
#endif
    s_Input.initialized = true;
}

bool InputMap::LoadFromProject(const std::filesystem::path& projectDir) {
    InitDefaults();
    std::error_code ec;
    const std::filesystem::path mapFile = projectDir / "input_map.json";
    if (!std::filesystem::exists(mapFile, ec)) {
        return false;
    }
    std::ifstream in(mapFile);
    if (!in.is_open()) {
        return false;
    }
    try {
        nlohmann::json root;
        in >> root;
        if (!root.contains("actions") || !root["actions"].is_object()) {
            return false;
        }
        s_Input.actions.clear();
        for (auto it = root["actions"].begin(); it != root["actions"].end(); ++it) {
            std::vector<InputBinding> list;
            if (it.value().is_array()) {
                for (const auto& item : it.value()) {
                    InputBinding b;
                    const std::string t = item.value("type", "key");
                    b.type = (t == "mouse") ? InputDeviceType::MouseButton : InputDeviceType::Key;
                    b.code = item.value("code", 0);
                    if (b.code != 0 || b.type == InputDeviceType::MouseButton) {
                        list.push_back(b);
                    }
                }
            }
            s_Input.actions[it.key()] = std::move(list);
        }
        return true;
    } catch (const std::exception& ex) {
        KM_CORE_WARN("Failed to load input_map.json: {}", ex.what());
        return false;
    }
}

bool InputMap::SaveToProject(const std::filesystem::path& projectDir) {
    if (!s_Input.initialized) {
        InitDefaults();
    }
    std::error_code ec;
    std::filesystem::create_directories(projectDir, ec);

    nlohmann::json root;
    nlohmann::json actionsObj = nlohmann::json::object();
    for (const auto& [name, bindings] : s_Input.actions) {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& b : bindings) {
            arr.push_back({
                {"type", b.type == InputDeviceType::MouseButton ? "mouse" : "key"},
                {"code", b.code}
            });
        }
        actionsObj[name] = std::move(arr);
    }
    root["actions"] = std::move(actionsObj);

    try {
        std::ofstream out(projectDir / "input_map.json", std::ios::trunc);
        if (!out.is_open()) {
            return false;
        }
        out << root.dump(2) << '\n';
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

void InputMap::Update(GLFWwindow* window) {
    if (!s_Input.initialized) {
        InitDefaults();
    }
    s_Input.prevStates = s_Input.currStates;
    s_Input.currStates.clear();

    for (const auto& [name, bindings] : s_Input.actions) {
        bool pressed = (s_Input.virtualPressed.find(name) != s_Input.virtualPressed.end());
#ifndef __ANDROID__
        if (!pressed && window != nullptr) {
            for (const auto& b : bindings) {
                if (b.type == InputDeviceType::Key) {
                    if (glfwGetKey(window, b.code) == GLFW_PRESS) {
                        pressed = true;
                        break;
                    }
                } else {
                    if (glfwGetMouseButton(window, b.code) == GLFW_PRESS) {
                        pressed = true;
                        break;
                    }
                }
            }
        }
#else
        (void)window;
#endif
        s_Input.currStates[name] = pressed;
    }

    // Also include virtual-only actions not explicitly configured in actions map
    for (const auto& vName : s_Input.virtualPressed) {
        s_Input.currStates[vName] = true;
    }
}

void InputMap::SetActionBindings(const std::string& actionName, std::vector<InputBinding> bindings) {
    if (!s_Input.initialized) {
        InitDefaults();
    }
    s_Input.actions[actionName] = std::move(bindings);
}

void InputMap::AddBinding(const std::string& actionName, InputBinding binding) {
    if (!s_Input.initialized) {
        InitDefaults();
    }
    auto& vec = s_Input.actions[actionName];
    for (const auto& existing : vec) {
        if (existing.type == binding.type && existing.code == binding.code) {
            return;
        }
    }
    vec.push_back(binding);
}

void InputMap::RemoveAction(const std::string& actionName) {
    s_Input.actions.erase(actionName);
}

const std::unordered_map<std::string, std::vector<InputBinding>>& InputMap::GetAllActions() {
    if (!s_Input.initialized) {
        InitDefaults();
    }
    return s_Input.actions;
}

bool InputMap::IsActionPressed(const std::string& actionName) {
    const auto it = s_Input.currStates.find(actionName);
    return it != s_Input.currStates.end() && it->second;
}

bool InputMap::IsActionJustPressed(const std::string& actionName) {
    const auto currIt = s_Input.currStates.find(actionName);
    const bool curr = (currIt != s_Input.currStates.end() && currIt->second);
    const auto prevIt = s_Input.prevStates.find(actionName);
    const bool prev = (prevIt != s_Input.prevStates.end() && prevIt->second);
    return curr && !prev;
}

bool InputMap::IsActionJustReleased(const std::string& actionName) {
    const auto currIt = s_Input.currStates.find(actionName);
    const bool curr = (currIt != s_Input.currStates.end() && currIt->second);
    const auto prevIt = s_Input.prevStates.find(actionName);
    const bool prev = (prevIt != s_Input.prevStates.end() && prevIt->second);
    return !curr && prev;
}

float InputMap::GetAxis(const std::string& negativeAction, const std::string& positiveAction) {
    float val = 0.0f;
    if (IsActionPressed(negativeAction)) {
        val -= 1.0f;
    }
    if (IsActionPressed(positiveAction)) {
        val += 1.0f;
    }
    return val;
}

void InputMap::ClearVirtualStates() {
    s_Input.virtualPressed.clear();
    s_Input.touchAxisX = 0.0f;
    s_Input.touchAxisY = 0.0f;
}

void InputMap::SetVirtualActionState(const std::string& actionName, bool pressed) {
    if (pressed) {
        s_Input.virtualPressed.insert(actionName);
    } else {
        s_Input.virtualPressed.erase(actionName);
    }
}

void InputMap::SetTouchAxis(float x, float y) {
    s_Input.touchAxisX = std::clamp(x, -1.0f, 1.0f);
    s_Input.touchAxisY = std::clamp(y, -1.0f, 1.0f);
}

float InputMap::GetTouchAxisX() {
    return s_Input.touchAxisX;
}

float InputMap::GetTouchAxisY() {
    return s_Input.touchAxisY;
}

void InputMap::SetMouseCaptured(GLFWwindow* window, bool captured) {
    s_Input.mouseCaptured = captured;
#ifndef __ANDROID__
    if (window != nullptr) {
        glfwSetInputMode(window, GLFW_CURSOR, captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    }
#else
    (void)window;
#endif
}

bool InputMap::IsMouseCaptured() {
    return s_Input.mouseCaptured;
}

std::string InputMap::BindingToString(const InputBinding& binding) {
#ifndef __ANDROID__
    if (binding.type == InputDeviceType::MouseButton) {
        switch (binding.code) {
        case GLFW_MOUSE_BUTTON_LEFT: return "Mouse Left";
        case GLFW_MOUSE_BUTTON_RIGHT: return "Mouse Right";
        case GLFW_MOUSE_BUTTON_MIDDLE: return "Mouse Middle";
        default: return "Mouse " + std::to_string(binding.code);
        }
    }
    switch (binding.code) {
    case GLFW_KEY_SPACE: return "Space";
    case GLFW_KEY_ENTER: return "Enter";
    case GLFW_KEY_ESCAPE: return "Escape";
    case GLFW_KEY_TAB: return "Tab";
    case GLFW_KEY_BACKSPACE: return "Backspace";
    case GLFW_KEY_LEFT_SHIFT: return "Left Shift";
    case GLFW_KEY_RIGHT_SHIFT: return "Right Shift";
    case GLFW_KEY_LEFT_CONTROL: return "Left Ctrl";
    case GLFW_KEY_RIGHT_CONTROL: return "Right Ctrl";
    case GLFW_KEY_LEFT_ALT: return "Left Alt";
    case GLFW_KEY_UP: return "Up";
    case GLFW_KEY_DOWN: return "Down";
    case GLFW_KEY_LEFT: return "Left";
    case GLFW_KEY_RIGHT: return "Right";
    default: break;
    }
    if (binding.code >= GLFW_KEY_A && binding.code <= GLFW_KEY_Z) {
        return std::string(1, static_cast<char>('A' + (binding.code - GLFW_KEY_A)));
    }
    if (binding.code >= GLFW_KEY_0 && binding.code <= GLFW_KEY_9) {
        return std::string(1, static_cast<char>('0' + (binding.code - GLFW_KEY_0)));
    }
    if (binding.code >= GLFW_KEY_F1 && binding.code <= GLFW_KEY_F12) {
        return "F" + std::to_string(1 + (binding.code - GLFW_KEY_F1));
    }
    return "Key " + std::to_string(binding.code);
#else
    return "Code " + std::to_string(binding.code);
#endif
}

const std::vector<int>& InputMap::GetListenKeyCandidates() {
    static const std::vector<int> kKeys = {
#ifndef __ANDROID__
        GLFW_KEY_SPACE, GLFW_KEY_ENTER, GLFW_KEY_ESCAPE, GLFW_KEY_TAB, GLFW_KEY_BACKSPACE,
        GLFW_KEY_LEFT_SHIFT, GLFW_KEY_RIGHT_SHIFT, GLFW_KEY_LEFT_CONTROL, GLFW_KEY_RIGHT_CONTROL,
        GLFW_KEY_LEFT_ALT, GLFW_KEY_UP, GLFW_KEY_DOWN, GLFW_KEY_LEFT, GLFW_KEY_RIGHT,
        GLFW_KEY_A, GLFW_KEY_B, GLFW_KEY_C, GLFW_KEY_D, GLFW_KEY_E, GLFW_KEY_F, GLFW_KEY_G,
        GLFW_KEY_H, GLFW_KEY_I, GLFW_KEY_J, GLFW_KEY_K, GLFW_KEY_L, GLFW_KEY_M, GLFW_KEY_N,
        GLFW_KEY_O, GLFW_KEY_P, GLFW_KEY_Q, GLFW_KEY_R, GLFW_KEY_S, GLFW_KEY_T, GLFW_KEY_U,
        GLFW_KEY_V, GLFW_KEY_W, GLFW_KEY_X, GLFW_KEY_Y, GLFW_KEY_Z,
        GLFW_KEY_0, GLFW_KEY_1, GLFW_KEY_2, GLFW_KEY_3, GLFW_KEY_4,
        GLFW_KEY_5, GLFW_KEY_6, GLFW_KEY_7, GLFW_KEY_8, GLFW_KEY_9,
        GLFW_KEY_F1, GLFW_KEY_F2, GLFW_KEY_F3, GLFW_KEY_F4, GLFW_KEY_F5, GLFW_KEY_F6,
        GLFW_KEY_F7, GLFW_KEY_F8, GLFW_KEY_F9, GLFW_KEY_F10, GLFW_KEY_F11, GLFW_KEY_F12
#endif
    };
    return kKeys;
}

} // namespace Kyumist

