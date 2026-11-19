import dev.kyumist.KyumistScript;

public class RotatorScript extends KyumistScript {
    @Override
    public void onCreate() {
        logInfo("RotatorScript iniciado en entidad " + entity.getId());
    }

    @Override
    public void onUpdate(float dt) {
        entity.rotate(20.0f * dt, 65.0f * dt, 0.0f);
    }

    @Override
    public void onDestroy() {
        logInfo("RotatorScript detenido en entidad " + entity.getId());
    }
}

