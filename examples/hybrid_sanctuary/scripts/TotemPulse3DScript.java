import dev.kyumist.KyumistScript;

public class TotemPulse3DScript extends KyumistScript {
    private float elapsed = 0.0f;

    @Override
    public void onUpdate(float dt) {
        elapsed += dt;
        entity.rotate(0.0f, 55.0f * dt, 0.0f);
        entity.setPosition(0.0f, 1.45f + (float) Math.sin(elapsed * 2.2) * 0.16f, -2.5f);
    }
}
