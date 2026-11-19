import dev.kyumist.Entity;
import dev.kyumist.KyumistScript;

public class EnemyDrone3DScript extends KyumistScript {
    private float elapsed = 0.0f;
    private float speed = 1.85f;
    private final float[] pos = new float[3];
    private final float[] targetPos = new float[3];
    private Entity player;

    @Override
    public void onCreate() {
        player = findEntity("Jugador");
        String name = entity.getName();
        if (name.endsWith("2")) {
            speed = 2.25f;
        } else if (name.endsWith("3")) {
            speed = 2.60f;
        }
    }

    @Override
    public void onUpdate(float dt) {
        elapsed += dt;
        entity.getPosition(pos);
        if (player == null) {
            player = findEntity("Jugador");
        }
        float nx = pos[0];
        float nz = pos[2];
        if (player != null) {
            player.getPosition(targetPos);
            float dx = targetPos[0] - pos[0];
            float dz = targetPos[2] - pos[2];
            float len = (float) Math.sqrt(dx * dx + dz * dz);
            if (len > 0.05f) {
                nx += (dx / len) * speed * dt;
                nz += (dz / len) * speed * dt;
            }
        }
        float ny = 0.72f + (float) Math.sin(elapsed * 3.4) * 0.14f;
        entity.setPosition(nx, ny, nz);
    }
}
