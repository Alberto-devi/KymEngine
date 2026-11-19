import dev.kyumist.Audio;
import dev.kyumist.Entity;
import dev.kyumist.Input;
import dev.kyumist.KyumistScript;

public class HybridHeroScript extends KyumistScript {
    private boolean grounded = false;
    private float jumpCooldown = 0.0f;
    private float hitCooldown = 0.0f;
    private int runesCollected = 0;
    private int lives = 3;
    private final float[] vel = new float[3];
    private Entity hudEntity;

    @Override
    public void onCreate() {
        hudEntity = findEntity("HUD_Hybrid");
        updateHud();
        logInfo("Aether Guardian (Hibrido 3D+2D) iniciado en entidad " + entity.getId());
    }

    @Override
    public void onUpdate(float dt) {
        entity.getVelocity(vel);
        float vx = 0.0f;
        float vz = 0.0f;
        if (Input.isKeyDown(Input.KEY_A) || Input.isKeyDown(Input.KEY_LEFT))  vx -= 4.8f;
        if (Input.isKeyDown(Input.KEY_D) || Input.isKeyDown(Input.KEY_RIGHT)) vx += 4.8f;
        if (Input.isKeyDown(Input.KEY_W) || Input.isKeyDown(Input.KEY_UP))    vz -= 4.8f;
        if (Input.isKeyDown(Input.KEY_S) || Input.isKeyDown(Input.KEY_DOWN))  vz += 4.8f;

        if (jumpCooldown > 0.0f) {
            jumpCooldown -= dt;
        }
        if (hitCooldown > 0.0f) {
            hitCooldown -= dt;
            if (hitCooldown <= 0.0f) {
                entity.setColor(0.98f, 0.48f, 0.34f, 1.0f);
            }
        }

        float vy = vel[1];
        if (Input.isKeyDown(Input.KEY_SPACE) && (grounded || Math.abs(vy) < 0.25f) && jumpCooldown <= 0.0f) {
            vy = 6.8f;
            grounded = false;
            jumpCooldown = 0.25f;
            Audio.playSound("audio/bounce.wav", 0.5f);
        }

        entity.setVelocity(vx, vy, vz);

        if (entity.getY() < -5.0f) {
            entity.setPosition(0.0f, 1.5f, 3.6f);
            entity.setVelocity(0.0f, 0.0f, 0.0f);
        }
    }

    @Override
    public void onCollisionEnter(Entity other) {
        String otherName = other.getName();
        if (otherName.equals("Suelo Santuario") || otherName.equals("Altar Central") || otherName.equals("Totem Cristal")) {
            grounded = true;
        } else if (otherName.startsWith("Runa_2D_")) {
            other.setPosition(0.0f, -60.0f, 0.0f);
            runesCollected++;
            Audio.playSound("audio/bounce.wav", 0.95f);
            updateHud();
        } else if (otherName.startsWith("Orbe_Guardian_") && hitCooldown <= 0.0f) {
            lives--;
            hitCooldown = 1.2f;
            entity.setColor(1.0f, 0.16f, 0.16f, 1.0f);
            Audio.playSound("audio/bounce.wav", 1.0f);
            if (lives <= 0) {
                lives = 3;
                runesCollected = 0;
                resetRunes();
            }
            entity.setPosition(0.0f, 1.1f, 3.8f);
            entity.setVelocity(0.0f, 0.0f, 0.0f);
            updateHud();
        }
    }

    private void resetRunes() {
        Entity r1 = findEntity("Runa_2D_1"); if (r1 != null) r1.setPosition(-4.0f, 0.85f, 1.2f);
        Entity r2 = findEntity("Runa_2D_2"); if (r2 != null) r2.setPosition(4.0f, 0.85f, 1.2f);
        Entity r3 = findEntity("Runa_2D_3"); if (r3 != null) r3.setPosition(0.0f, 1.30f, 0.0f);
    }

    private void updateHud() {
        if (hudEntity == null) {
            hudEntity = findEntity("HUD_Hybrid");
        }
        if (hudEntity != null) {
            if (runesCollected >= 3) {
                hudEntity.setUIText("Aether Guardian | SANTUARIO ACTIVADO! Vidas: " + lives + " | Runas: 3/3");
            } else {
                hudEntity.setUIText("Aether Guardian (3D+2D) | Vidas: " + lives + " | Runas 2D: " + runesCollected + "/3 | WASD: Mover | Espacio: Saltar");
            }
        }
    }
}
