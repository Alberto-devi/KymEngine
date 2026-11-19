import dev.kyumist.Input;
import dev.kyumist.KyumistScript;

public class Oscillator2DScript extends KyumistScript {
    private float elapsed = 0.0f;
    private float baseX = 0.0f;
    private float baseY = 1.8f;
    private float baseZ = -1.0f;

    @Override
    public void onCreate() {
        float[] pos = new float[3];
        entity.getPosition(pos);
        baseX = pos[0];
        baseY = pos[1];
        baseZ = pos[2];
        logInfo("Oscillator2DScript iniciado en entidad " + entity.getId());
    }

    @Override
    public void onUpdate(float dt) {
        elapsed += dt;

        float moveSpeed = 2.5f * dt;
        if (Input.isKeyDown(Input.KEY_RIGHT)) baseX += moveSpeed;
        if (Input.isKeyDown(Input.KEY_LEFT))  baseX -= moveSpeed;
        if (Input.isKeyDown(Input.KEY_UP))    baseY += moveSpeed;
        if (Input.isKeyDown(Input.KEY_DOWN))  baseY -= moveSpeed;

        float offsetX = (float) Math.sin(elapsed * 2.5) * 0.65f;
        float offsetY = (float) Math.cos(elapsed * 3.5) * 0.25f;
        entity.setPosition(baseX + offsetX, baseY + offsetY, baseZ);
    }

    @Override
    public void onDestroy() {
        logInfo("Oscillator2DScript detenido en entidad " + entity.getId());
    }
}

