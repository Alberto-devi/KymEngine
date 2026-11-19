import dev.kyumist.KyumistScript;

public class GuardianOrb3DScript extends KyumistScript {
    private float angle = 0.0f;

    @Override
    public void onCreate() {
        String name = entity.getName();
        if (name.endsWith("2")) {
            angle = 2.094395f;
        } else if (name.endsWith("3")) {
            angle = 4.18879f;
        }
    }

    @Override
    public void onUpdate(float dt) {
        angle += 1.45f * dt;
        float ox = (float) Math.cos(angle) * 3.0f;
        float oz = -2.5f + (float) Math.sin(angle) * 3.0f;
        float oy = 0.88f + (float) Math.sin(angle * 2.0) * 0.12f;
        entity.setPosition(ox, oy, oz);
        entity.rotate(0.0f, 90.0f * dt, 0.0f);
    }
}
