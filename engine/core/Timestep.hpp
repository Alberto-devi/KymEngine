#pragma once

namespace Kyumist {

class Timestep {
public:
    Timestep(float timeSeconds = 0.0f) : m_Time(timeSeconds) {}

    float GetSeconds() const { return m_Time; }
    float GetMilliseconds() const { return m_Time * 1000.0f; }

    operator float() const { return m_Time; }

private:
    float m_Time;
};

} // namespace Kyumist

