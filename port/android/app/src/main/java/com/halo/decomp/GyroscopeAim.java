package com.halo.decomp;

/**
 * The phone's gyroscope in the display's axes, for the native filter
 * (port/linux/src/gyro_aim.c through host_touch.c's nativeGyro: the bias
 * learnt while still, smoothing and a deadzone, which add the samples up).
 * A sample that starts over (the first, after a rotation or a gap, or a
 * timestamp out of order) tells the filter to start over too.
 */
final class GyroscopeAim {
    private long previousTime;
    private int previousRotation = -1;
    /** whether the last sample started over */
    boolean restart;

    void reset() { previousTime = 0; previousRotation = -1; }

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
        restart = previousTime == 0 || rotation != previousRotation || dt <= 0 || dt > 0.1f;
        previousTime = time; previousRotation = rotation;
        screen[0] = screenX; screen[1] = screenY; screen[2] = z;
        return true;
    }
}
