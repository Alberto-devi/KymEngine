import dev.kyumist.KyumistScript;

public class MovingPlatform2DScript extends KyumistScript {
    private float elapsed = 0.0f;
    private float baseX = 0.0f;
    private float baseY = 0.8f;

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
        float offsetX = (float) Math.sin(elapsed * 1.4) * 1.6f;
        float offsetY = (float) Math.cos(elapsed * 1.4) * 0.45f;
        entity.setPosition(baseX + offsetX, baseY + offsetY, 0.0f);
    }
}
