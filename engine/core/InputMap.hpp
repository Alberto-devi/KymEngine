#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

struct GLFWwindow;

namespace Kyumist {

enum class InputDeviceType {
    Key = 0,
    MouseButton = 1
};

struct InputBinding {
    InputDeviceType type = InputDeviceType::Key;
    int code = 0; // GLFW_KEY_* or GLFW_MOUSE_BUTTON_*
};

class InputMap {
public:
    static void InitDefaults();
    static bool LoadFromProject(const std::filesystem::path& projectDir);
    static bool SaveToProject(const std::filesystem::path& projectDir);

    static void Update(GLFWwindow* window);

    static void SetActionBindings(const std::string& actionName, std::vector<InputBinding> bindings);
    static void AddBinding(const std::string& actionName, InputBinding binding);
    static void RemoveAction(const std::string& actionName);
    static const std::unordered_map<std::string, std::vector<InputBinding>>& GetAllActions();

    static bool IsActionPressed(const std::string& actionName);
    static bool IsActionJustPressed(const std::string& actionName);
    static bool IsActionJustReleased(const std::string& actionName);
    static float GetAxis(const std::string& negativeAction, const std::string& positiveAction);

    static void ClearVirtualStates();
    static void SetVirtualActionState(const std::string& actionName, bool pressed);
    static void SetTouchAxis(float x, float y);
    static float GetTouchAxisX();
    static float GetTouchAxisY();

    static void SetMouseCaptured(GLFWwindow* window, bool captured);
    static bool IsMouseCaptured();

    static std::string BindingToString(const InputBinding& binding);
    static const std::vector<int>& GetListenKeyCandidates();
};

} // namespace Kyumist

