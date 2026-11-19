#include "physics/PhysicsSystem.hpp"

#include "audio/AudioEngine.hpp"
#include "scene/Components.hpp"
#include "scene/Scene.hpp"
#include "scripting/ScriptEngine.hpp"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace Kyumist {

namespace {

struct CollisionManifold {
    bool hit = false;
    glm::vec3 normal{0.0f, 1.0f, 0.0f}; // points from A to B
    float penetration = 0.0f;
};

glm::vec3 EffectiveBoxExtents(const RigidBodyComponent& rb) {
    if (rb.shape == ColliderShape::Box2D) {
        return {rb.halfExtents.x, rb.halfExtents.y, 0.5f};
    }
    return rb.halfExtents;
}

CollisionManifold TestSphereSphere(
    const glm::vec3& posA, float rA,
    const glm::vec3& posB, float rB
) {
    CollisionManifold m;
    const glm::vec3 delta = posB - posA;
    const float distSq = glm::dot(delta, delta);
    const float radiusSum = rA + rB;
    if (distSq >= radiusSum * radiusSum) {
        return m;
    }

    const float dist = std::sqrt(distSq);
    m.hit = true;
    m.normal = (dist > 1e-5f) ? (delta / dist) : glm::vec3(0.0f, 1.0f, 0.0f);
    m.penetration = radiusSum - dist;
    return m;
}

CollisionManifold TestBoxBox(
    const glm::vec3& posA, const glm::vec3& extA,
    const glm::vec3& posB, const glm::vec3& extB,
    bool is2D
) {
    CollisionManifold m;
    const glm::vec3 delta = posB - posA;
    const float ox = (extA.x + extB.x) - std::abs(delta.x);
    const float oy = (extA.y + extB.y) - std::abs(delta.y);
    const float oz = is2D ? 1e6f : ((extA.z + extB.z) - std::abs(delta.z));

    if (ox <= 0.0f || oy <= 0.0f || oz <= 0.0f) {
        return m;
    }

    m.hit = true;
    if (ox <= oy && ox <= oz) {
        m.penetration = ox;
        m.normal = {(delta.x >= 0.0f) ? 1.0f : -1.0f, 0.0f, 0.0f};
    } else if (oy <= ox && oy <= oz) {
        m.penetration = oy;
        m.normal = {0.0f, (delta.y >= 0.0f) ? 1.0f : -1.0f, 0.0f};
    } else {
        m.penetration = oz;
        m.normal = {0.0f, 0.0f, (delta.z >= 0.0f) ? 1.0f : -1.0f};
    }
    return m;
}

CollisionManifold TestSphereBox(
    const glm::vec3& spherePos, float radius,
    const glm::vec3& boxPos, const glm::vec3& boxExt
) {
    CollisionManifold m;
    const glm::vec3 minB = boxPos - boxExt;
    const glm::vec3 maxB = boxPos + boxExt;
    const glm::vec3 closest = glm::clamp(spherePos, minB, maxB);
    const glm::vec3 delta = closest - spherePos;
    const float distSq = glm::dot(delta, delta);

    if (distSq > radius * radius) {
        return m;
    }

    m.hit = true;
    const float dist = std::sqrt(distSq);
    if (dist > 1e-5f) {
        m.normal = delta / dist;
        m.penetration = radius - dist;
    } else {
        const glm::vec3 local = spherePos - boxPos;
        const float px = boxExt.x - std::abs(local.x);
        const float py = boxExt.y - std::abs(local.y);
        const float pz = boxExt.z - std::abs(local.z);
        if (px <= py && px <= pz) {
            m.normal = {(local.x >= 0.0f) ? -1.0f : 1.0f, 0.0f, 0.0f};
            m.penetration = radius + px;
        } else if (py <= px && py <= pz) {
            m.normal = {0.0f, (local.y >= 0.0f) ? -1.0f : 1.0f, 0.0f};
            m.penetration = radius + py;
        } else {
            m.normal = {0.0f, 0.0f, (local.z >= 0.0f) ? -1.0f : 1.0f};
            m.penetration = radius + pz;
        }
    }
    return m;
}

CollisionManifold DetectCollision(
    const glm::vec3& posA, const RigidBodyComponent& rbA,
    const glm::vec3& posB, const RigidBodyComponent& rbB
) {
    const bool aSphere = (rbA.shape == ColliderShape::Sphere3D);
    const bool bSphere = (rbB.shape == ColliderShape::Sphere3D);

    if (aSphere && bSphere) {
        return TestSphereSphere(posA, rbA.radius, posB, rbB.radius);
    }
    if (aSphere && !bSphere) {
        return TestSphereBox(posA, rbA.radius, posB, EffectiveBoxExtents(rbB));
    }
    if (!aSphere && bSphere) {
        CollisionManifold m = TestSphereBox(posB, rbB.radius, posA, EffectiveBoxExtents(rbA));
        m.normal = -m.normal;
        return m;
    }

    const bool both2D = (rbA.shape == ColliderShape::Box2D && rbB.shape == ColliderShape::Box2D);
    return TestBoxBox(posA, EffectiveBoxExtents(rbA), posB, EffectiveBoxExtents(rbB), both2D);
}

void TriggerBounceAudio(entt::registry& reg, entt::entity entity) {
    if (const auto* audio = reg.try_get<AudioSourceComponent>(entity)) {
        if (audio->playOnBounce && !audio->soundPath.empty()) {
            AudioEngine::PlaySound(audio->soundPath, audio->volume);
        }
    }
}

bool IntersectRaySphere(
    const glm::vec3& origin,
    const glm::vec3& dir,
    const glm::vec3& center,
    float radius,
    float maxDist,
    float& outT,
    glm::vec3& outNormal
) {
    const glm::vec3 oc = origin - center;
    const float b = glm::dot(oc, dir);
    const float c = glm::dot(oc, oc) - radius * radius;
    const float disc = b * b - c;
    if (disc < 0.0f) {
        return false;
    }
    const float sqrtDisc = std::sqrt(disc);
    float t = -b - sqrtDisc;
    if (t < 0.0f) {
        t = -b + sqrtDisc;
    }
    if (t < 0.0f || t > maxDist) {
        return false;
    }
    outT = t;
    const glm::vec3 hitPt = origin + dir * t;
    const glm::vec3 n = hitPt - center;
    outNormal = glm::length(n) > 1e-5f ? glm::normalize(n) : glm::vec3(0.0f, 1.0f, 0.0f);
    return true;
}

bool IntersectRayAABB(
    const glm::vec3& origin,
    const glm::vec3& dir,
    const glm::vec3& center,
    const glm::vec3& halfExtents,
    float maxDist,
    float& outT,
    glm::vec3& outNormal
) {
    const glm::vec3 bmin = center - halfExtents;
    const glm::vec3 bmax = center + halfExtents;
    float tMin = 0.0f;
    float tMax = maxDist;
    glm::vec3 hitNormal(0.0f, 1.0f, 0.0f);

    for (int axis = 0; axis < 3; ++axis) {
        if (std::abs(dir[axis]) < 1e-6f) {
            if (origin[axis] < bmin[axis] || origin[axis] > bmax[axis]) {
                return false;
            }
        } else {
            const float invD = 1.0f / dir[axis];
            float t1 = (bmin[axis] - origin[axis]) * invD;
            float t2 = (bmax[axis] - origin[axis]) * invD;
            glm::vec3 n(0.0f);
            n[axis] = (invD < 0.0f) ? 1.0f : -1.0f;
            if (t1 > t2) {
                std::swap(t1, t2);
                n = -n;
            }
            if (t1 > tMin) {
                tMin = t1;
                hitNormal = n;
            }
            if (t2 < tMax) {
                tMax = t2;
            }
            if (tMin > tMax) {
                return false;
            }
        }
    }

    if (tMin < 0.0f || tMin > maxDist) {
        return false;
    }
    outT = tMin;
    outNormal = hitNormal;
    return true;
}

} // namespace

void PhysicsSystem::OnRuntimeStart(Scene* scene) {
    if (!scene) {
        return;
    }
    const auto& reg = scene->GetRegistry();
    for (auto entity : reg.view<AudioSourceComponent>()) {
        const auto& audio = reg.get<AudioSourceComponent>(entity);
        if (audio.playOnStart && !audio.soundPath.empty()) {
            AudioEngine::PlaySound(audio.soundPath, audio.volume);
        }
    }
}

void PhysicsSystem::Step(Scene* scene, Timestep ts) {
    if (!scene) {
        return;
    }
    const float dt = std::min(ts.GetSeconds(), 0.05f);
    if (dt <= 0.0f) {
        return;
    }

    auto& reg = scene->GetRegistry();

    // Advance 2D spritesheet animations
    for (auto entity : reg.view<SpriteRendererComponent>()) {
        auto& spr = reg.get<SpriteRendererComponent>(entity);
        const int32_t totalFrames = std::max(1, spr.hFrames) * std::max(1, spr.vFrames);
        if (spr.animFps > 0.0f && totalFrames > 1) {
            spr.animTimer += dt;
            const float frameDur = 1.0f / spr.animFps;
            while (spr.animTimer >= frameDur) {
                spr.animTimer -= frameDur;
                spr.frameIndex = (spr.frameIndex + 1) % totalFrames;
            }
        }
    }

    std::vector<entt::entity> bodies;
    for (auto entity : reg.view<TransformComponent, RigidBodyComponent>()) {
        bodies.push_back(entity);
    }

    constexpr glm::vec3 gravity{0.0f, -9.81f, 0.0f};
    for (entt::entity entity : bodies) {
        auto& tr = reg.get<TransformComponent>(entity);
        auto& rb = reg.get<RigidBodyComponent>(entity);
        if (rb.bodyType == RigidBodyType::Dynamic) {
            if (rb.useGravity) {
                rb.velocity += gravity * dt;
            }
            tr.translation += rb.velocity * dt;
        }
        if (rb.bodyType == RigidBodyType::Dynamic || rb.bodyType == RigidBodyType::Kinematic) {
            tr.rotationDegrees += rb.angularVelocity * dt;
        }
    }

    for (size_t i = 0; i < bodies.size(); ++i) {
        for (size_t j = i + 1; j < bodies.size(); ++j) {
            const entt::entity entA = bodies[i];
            const entt::entity entB = bodies[j];
            if (!scene->IsValid(entA) || !scene->IsValid(entB)) {
                continue;
            }

            auto& trA = reg.get<TransformComponent>(entA);
            auto& rbA = reg.get<RigidBodyComponent>(entA);
            auto& trB = reg.get<TransformComponent>(entB);
            auto& rbB = reg.get<RigidBodyComponent>(entB);

            const bool isTriggerPair = rbA.isTrigger || rbB.isTrigger;
            const bool bothStatic = (rbA.bodyType == RigidBodyType::Static && rbB.bodyType == RigidBodyType::Static);
            if (bothStatic) {
                continue;
            }

            const float invMassA = (rbA.bodyType == RigidBodyType::Dynamic && rbA.mass > 0.0f) ? (1.0f / rbA.mass) : 0.0f;
            const float invMassB = (rbB.bodyType == RigidBodyType::Dynamic && rbB.mass > 0.0f) ? (1.0f / rbB.mass) : 0.0f;
            const float totalInvMass = invMassA + invMassB;

            const glm::vec3 worldPosA = glm::vec3(scene->GetWorldTransform(entA)[3]);
            const glm::vec3 worldPosB = glm::vec3(scene->GetWorldTransform(entB)[3]);

            const CollisionManifold manifold = DetectCollision(worldPosA, rbA, worldPosB, rbB);
            if (!manifold.hit || manifold.penetration <= 0.0f) {
                continue;
            }

            ScriptEngine::NotifyCollision(scene, entA, entB);
            ScriptEngine::NotifyCollision(scene, entB, entA);

            if (!scene->IsValid(entA) || !scene->IsValid(entB)) {
                continue;
            }
            if (isTriggerPair) {
                continue;
            }

            const float pushDist = std::max(manifold.penetration - 0.0005f, 0.0f);
            if (totalInvMass <= 0.0f) {
                // Separate Kinematic bodies from Static obstacles (or other Kinematic bodies)
                if (rbA.bodyType == RigidBodyType::Kinematic && rbB.bodyType == RigidBodyType::Static) {
                    trA.translation -= manifold.normal * pushDist;
                } else if (rbB.bodyType == RigidBodyType::Kinematic && rbA.bodyType == RigidBodyType::Static) {
                    trB.translation += manifold.normal * pushDist;
                } else if (rbA.bodyType == RigidBodyType::Kinematic && rbB.bodyType == RigidBodyType::Kinematic) {
                    trA.translation -= manifold.normal * (pushDist * 0.5f);
                    trB.translation += manifold.normal * (pushDist * 0.5f);
                }
                continue;
            }

            const glm::vec3 correction = manifold.normal * (pushDist / totalInvMass);
            trA.translation -= correction * invMassA;
            trB.translation += correction * invMassB;

            const glm::vec3 relVel = rbB.velocity - rbA.velocity;
            const float velAlongNormal = glm::dot(relVel, manifold.normal);
            if (velAlongNormal >= 0.0f) {
                continue;
            }

            const float restitution = std::min(rbA.restitution, rbB.restitution);
            const float jImpulse = -(1.0f + restitution) * velAlongNormal / totalInvMass;
            const glm::vec3 impulse = jImpulse * manifold.normal;

            rbA.velocity -= impulse * invMassA;
            rbB.velocity += impulse * invMassB;

            glm::vec3 tangent = relVel - velAlongNormal * manifold.normal;
            const float tangentLen = glm::length(tangent);
            if (tangentLen > 1e-4f) {
                tangent /= tangentLen;
                const float jt = -glm::dot(relVel, tangent) / totalInvMass;
                const float mu = std::sqrt(std::max(0.0f, rbA.friction * rbB.friction));
                const float clampedJt = std::clamp(jt, -jImpulse * mu, jImpulse * mu);
                const glm::vec3 frictionImpulse = clampedJt * tangent;
                rbA.velocity -= frictionImpulse * invMassA;
                rbB.velocity += frictionImpulse * invMassB;
            }

            if (std::abs(velAlongNormal) > 1.2f) {
                TriggerBounceAudio(reg, entA);
                TriggerBounceAudio(reg, entB);
            }
        }
    }
}

RaycastHit PhysicsSystem::Raycast(
    const Scene* scene,
    const glm::vec3& origin,
    const glm::vec3& direction,
    float maxDistance
) {
    RaycastHit best;
    if (!scene || maxDistance <= 0.0f) {
        return best;
    }

    const float dirLen = glm::length(direction);
    if (dirLen < 1e-6f) {
        return best;
    }
    const glm::vec3 dir = direction / dirLen;

    const auto& reg = scene->GetRegistry();
    float closestT = maxDistance;

    auto view = reg.view<TransformComponent, RigidBodyComponent>();
    for (auto entity : view) {
        const auto& rb = view.get<RigidBodyComponent>(entity);
        if (rb.isTrigger) {
            continue;
        }
        const glm::vec3 center = glm::vec3(scene->GetWorldTransform(entity)[3]);
        float hitT = 0.0f;
        glm::vec3 hitN(0.0f, 1.0f, 0.0f);
        bool hit = false;

        if (rb.shape == ColliderShape::Sphere3D) {
            hit = IntersectRaySphere(origin, dir, center, rb.radius, closestT, hitT, hitN);
        } else {
            hit = IntersectRayAABB(origin, dir, center, EffectiveBoxExtents(rb), closestT, hitT, hitN);
        }

        if (hit && hitT < closestT) {
            closestT = hitT;
            best.hit = true;
            best.entity = entity;
            best.distance = hitT;
            best.point = origin + dir * hitT;
            best.normal = hitN;
        }
    }

    return best;
}

} // namespace Kyumist
