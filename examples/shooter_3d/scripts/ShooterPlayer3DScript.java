import dev.kyumist.Audio;
import dev.kyumist.Entity;
import dev.kyumist.Input;
import dev.kyumist.KyumistScript;

public class ShooterPlayer3DScript extends KyumistScript {
    private float yawDeg = 0.0f;
    private float fireCooldown = 0.0f;
    private float laserTimer = 0.0f;
    private float hitCooldown = 0.0f;
    private int kills = 0;
    private int lives = 3;
    private Entity laserEntity;
    private Entity hudEntity;
    private final float[] pos = new float[3];

    @Override
    public void onCreate() {
        float[] rot = new float[3];
        entity.getRotation(rot);
        yawDeg = rot[1];
        laserEntity = findEntity("LaserBeam");
        hudEntity = findEntity("HUD_Shooter");
        updateHud();
        logInfo("Vortex Arena 3D iniciado en entidad " + entity.getId());
    }

    @Override
    public void onUpdate(float dt) {
        if (Input.isKeyDown(Input.KEY_Q) || Input.isKeyDown(Input.KEY_LEFT)) {
            yawDeg += 130.0f * dt;
        }
        if (Input.isKeyDown(Input.KEY_E) || Input.isKeyDown(Input.KEY_RIGHT)) {
            yawDeg -= 130.0f * dt;
        }
        entity.setRotation(0.0f, yawDeg, 0.0f);

        double rad = Math.toRadians(yawDeg);
        float fwdX = (float) -Math.sin(rad);
        float fwdZ = (float) -Math.cos(rad);
        float rightX = (float) Math.cos(rad);
        float rightZ = (float) -Math.sin(rad);

        float moveFwd = 0.0f;
        float moveRight = 0.0f;
        if (Input.isKeyDown(Input.KEY_W) || Input.isKeyDown(Input.KEY_UP))   moveFwd += 1.0f;
        if (Input.isKeyDown(Input.KEY_S) || Input.isKeyDown(Input.KEY_DOWN)) moveFwd -= 1.0f;
        if (Input.isKeyDown(Input.KEY_D)) moveRight += 1.0f;
        if (Input.isKeyDown(Input.KEY_A)) moveRight -= 1.0f;

        entity.getPosition(pos);
        float speed = 6.2f * dt;
        float nx = clamp(pos[0] + (fwdX * moveFwd + rightX * moveRight) * speed, -7.8f, 7.8f);
        float nz = clamp(pos[2] + (fwdZ * moveFwd + rightZ * moveRight) * speed, -7.8f, 7.8f);
        entity.setPosition(nx, pos[1], nz);

        if (fireCooldown > 0.0f) {
            fireCooldown -= dt;
        }
        if (hitCooldown > 0.0f) {
            hitCooldown -= dt;
            if (hitCooldown <= 0.0f) {
                entity.setColor(0.20f, 0.72f, 0.96f, 1.0f);
            }
        }
        if (laserTimer > 0.0f) {
            laserTimer -= dt;
            if (laserTimer <= 0.0f && laserEntity != null) {
                laserEntity.setPosition(0.0f, -50.0f, 0.0f);
            }
        }

        boolean shootPressed = Input.isKeyDown(Input.KEY_SPACE) || Input.isMouseButtonDown(Input.MOUSE_LEFT);
        if (shootPressed && fireCooldown <= 0.0f) {
            fireCooldown = 0.24f;
            shootLaser(nx, pos[1] + 0.25f, nz, fwdX, fwdZ);
        }
    }

    @Override
    public void onCollisionEnter(Entity other) {
        String otherName = other.getName();
        if (otherName.startsWith("Dron_") && hitCooldown <= 0.0f) {
            lives--;
            hitCooldown = 1.2f;
            entity.setColor(1.0f, 0.18f, 0.18f, 1.0f);
            Audio.playSound("audio/bounce.wav", 1.0f);
            other.setPosition(-5.2f, 0.9f, -5.8f);
            if (lives <= 0) {
                lives = 3;
                kills = 0;
                entity.setPosition(0.0f, 0.55f, 4.5f);
            }
            updateHud();
        }
    }

    private void shootLaser(float px, float py, float pz, float fwdX, float fwdZ) {
        Audio.playSound("audio/bounce.wav", 0.65f);
        if (laserEntity == null) {
            laserEntity = findEntity("LaserBeam");
        }
        if (laserEntity != null) {
            laserEntity.setRotation(0.0f, yawDeg, 0.0f);
            laserEntity.setPosition(px + fwdX * 3.0f, py, pz + fwdZ * 3.0f);
            laserTimer = 0.07f;
        }

        Entity hit = raycast(px + fwdX * 0.9f, py, pz + fwdZ * 0.9f, fwdX, 0.0f, fwdZ, 26.0f);
        if (hit != null && hit.getName().startsWith("Dron_")) {
            kills++;
            Audio.playSound("audio/bounce.wav", 1.0f);
            float angle = kills * 1.9f;
            float rx = (float) Math.cos(angle) * 5.6f;
            float rz = -Math.abs((float) Math.sin(angle) * 5.2f) - 1.5f;
            hit.setPosition(rx, 0.9f, rz);
            updateHud();
        }
    }

    private void updateHud() {
        if (hudEntity == null) {
            hudEntity = findEntity("HUD_Shooter");
        }
        if (hudEntity != null) {
            hudEntity.setUIText("Vortex Arena 3D | Vidas: " + lives + " | Bajas: " + kills + " | WASD: Mover | Q/E: Girar | Espacio/Clic: Disparar");
        }
    }

    private static float clamp(float val, float minV, float maxV) {
        return Math.max(minV, Math.min(maxV, val));
    }
}
