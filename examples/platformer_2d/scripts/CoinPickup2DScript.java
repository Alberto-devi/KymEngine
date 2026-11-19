import dev.kyumist.KyumistScript;

public class CoinPickup2DScript extends KyumistScript {
    private float elapsed = 0.0f;
    private float baseX = 0.0f;
    private float baseY = 0.0f;

    @Override
    public void onCreate() {
        float[] pos = new float[3];
        entity.getPosition(pos);
        baseX = pos[0];
        baseY = pos[1];
    }

    @Override
    public void onUpdate(float dt) {
        elapsed += dt;
        if (entity.getY() > -20.0f) {
            entity.setPosition(baseX, baseY + (float) Math.sin(elapsed * 4.0) * 0.14f, 0.0f);
        }
    }
}
