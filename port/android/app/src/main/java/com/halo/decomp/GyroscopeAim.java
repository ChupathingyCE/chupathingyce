package com.halo.decomp;

/**
 * The phone's gyroscope in the display's axes, for the native filter
 * (port/linux/src/gyro_aim.c through host_touch.c's nativeGyro: the bias
 * learnt while still, smoothing and a deadzone, which add the samples up).
 * restart tells the filter what the last sample starts over: RESTART_TIME
 * after a gap or a pause (a timestamp out of order, the first after reset),
 * which keeps the bias it learnt; RESTART_ALL after the display rotated (or
 * the very first sample), whose axes are other ones now.
 */
final class GyroscopeAim {
    static final int RESTART_NONE = 0, RESTART_TIME = 1, RESTART_ALL = 2;
    private long previousTime;
    private int previousRotation = -1;
    /** what the last sample started over */
    int restart;

    /** a pause: the next sample starts the time over, the bias is kept */
    void reset() { previousTime = 0; }

    /** rates (radians a second, the sensor's axes) into screen[0..2]: the display's x, y and z */
    boolean sample(long time, float x, float y, float z, int rotation, float[] screen) {
        screen[0] = screen[1] = screen[2] = 0;
        if (!Float.isFinite(x) || !Float.isFinite(y) || !Float.isFinite(z) || rotation < 0 || rotation > 3) {
            reset(); return false;
        }
        float screenX = x, screenY = y;
        switch (rotation) {
            case 1: screenX = y; screenY = -x; break;
            case 2: screenX = -x; screenY = -y; break;
            case 3: screenX = -y; screenY = x; break;
        }
        float dt = (time-previousTime)*1e-9f;
        restart = rotation != previousRotation ? RESTART_ALL
            : previousTime == 0 || dt <= 0 || dt > 0.1f ? RESTART_TIME : RESTART_NONE;
        previousTime = time; previousRotation = rotation;
        screen[0] = screenX; screen[1] = screenY; screen[2] = z;
        return true;
    }
}
