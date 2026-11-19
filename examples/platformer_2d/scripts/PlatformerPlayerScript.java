import dev.kyumist.Audio;
import dev.kyumist.Entity;
import dev.kyumist.Input;
import dev.kyumist.KyumistScript;

public class PlatformerPlayerScript extends KyumistScript {
    private boolean grounded = false;
    private float coyoteTimer = 0.0f;
    private float jumpCooldown = 0.0f;
    private int coinsCollected = 0;
    private final float[] vel = new float[3];
    private Entity hudEntity;

    @Override
    public void onCreate() {
        hudEntity = findEntity("HUD_Platformer");
        updateHud();
        logInfo("SkyHop 2D iniciado en entidad " + entity.getId());
    }

    @Override
    public void onUpdate(float dt) {
        entity.getVelocity(vel);
        float moveX = 0.0f;
        if (Input.isKeyDown(Input.KEY_A) || Input.isKeyDown(Input.KEY_LEFT))  moveX -= 5.6f;
        if (Input.isKeyDown(Input.KEY_D) || Input.isKeyDown(Input.KEY_RIGHT)) moveX += 5.6f;

        if (jumpCooldown > 0.0f) jumpCooldown -= dt;
        if (coyoteTimer > 0.0f)  coyoteTimer -= dt;
        if (Math.abs(vel[1]) < 0.25f) {
            coyoteTimer = 0.09f;
        }

        float velY = vel[1];
        boolean jumpPressed = Input.isKeyDown(Input.KEY_SPACE) || Input.isKeyDown(Input.KEY_W) || Input.isKeyDown(Input.KEY_UP);
        if (jumpPressed && (grounded || coyoteTimer > 0.0f) && jumpCooldown <= 0.0f) {
            velY = 8.4f;
            grounded = false;
            coyoteTimer = 0.0f;
            jumpCooldown = 0.22f;
            Audio.playSound("audio/bounce.wav", 0.5f);
        }

        entity.setVelocity(moveX, velY, 0.0f);

        if (entity.getY() < -7.5f) {
            entity.setPosition(-5.5f, -0.5f, 0.0f);
            entity.setVelocity(0.0f, 0.0f, 0.0f);
        }
    }

    @Override
    public void onCollisionEnter(Entity other) {
        String otherName = other.getName();
        if (otherName.startsWith("Plat") || otherName.equals("Suelo Base")) {
            entity.getVelocity(vel);
            if (vel[1] <= 0.5f) {
                grounded = true;
                coyoteTimer = 0.12f;
            }
        } else if (otherName.startsWith("Coin_")) {
            other.setPosition(0.0f, -60.0f, 0.0f);
            coinsCollected++;
            Audio.playSound("audio/bounce.wav", 0.95f);
            updateHud();
        }
    }

    private void updateHud() {
        if (hudEntity == null) {
            hudEntity = findEntity("HUD_Platformer");
        }
        if (hudEntity != null) {
            if (coinsCollected >= 3) {
                hudEntity.setUIText("SkyHop 2D | VICTORIA! Monedas: 3/3");
            } else {
                hudEntity.setUIText("SkyHop 2D | Monedas: " + coinsCollected + "/3 | A/D: Mover | Espacio/W: Saltar");
            }
        }
    }
}
