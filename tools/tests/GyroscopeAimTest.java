package com.halo.decomp;

public final class GyroscopeAimTest {
    private static void check(boolean ok, String message) {
        if (!ok) throw new AssertionError(message);
    }
    private static void near(float value, float expected, String message) {
        check(Math.abs(value-expected) < 0.000001f, message);
    }
    public static void main(String[] args) {
        GyroscopeAim aim = new GyroscopeAim(); float[] screen = new float[3];
        check(aim.sample(1000000000L, 1, 0, 0.5f, 1, screen) && aim.restart == GyroscopeAim.RESTART_ALL, "First event starts the filter over");
        check(aim.sample(1020000000L, 1, 0, 0.5f, 1, screen) && aim.restart == GyroscopeAim.RESTART_NONE, "The next event goes on");
        near(screen[1], -1, "Landscape: the device's x is the display's y, reversed");
        near(screen[0], 0, "Pure device x does not reach the display's x");
        near(screen[2], 0.5f, "The axis out of the display is the same in any rotation");
        check(aim.sample(1040000000L, 1, 0, 0, 3, screen) && aim.restart == GyroscopeAim.RESTART_ALL, "Changing orientation starts everything over");
        near(screen[1], 1, "Reverse landscape reverses the device-axis mapping");
        check(aim.sample(2000000000L, 1, 0, 0, 3, screen) && aim.restart == GyroscopeAim.RESTART_TIME, "A resume gap starts the time over, keeping the bias");
        aim.sample(2020000000L, 0, 1, 0, 1, screen);
        near(screen[0], 1, "Landscape: the device's y is the display's x");
        aim.sample(2040000000L, 0, 1, 0, 3, screen);
        near(screen[0], -1, "Reverse landscape: the device's y is the display's x, reversed");
        aim.sample(3000000000L, 0.5f, -0.25f, 0, 0, screen);
        near(screen[0], 0.5f, "Portrait keeps the device's axes");
        near(screen[1], -0.25f, "Portrait keeps the device's axes");
        check(!aim.sample(3040000000L, Float.NaN, 1, 0, 0, screen), "Invalid sensor values are rejected");
        check(aim.sample(3060000000L, 1, 1, 0, 0, screen) && aim.restart == GyroscopeAim.RESTART_TIME, "After a rejected value the time starts over");
        aim.sample(4000000000L, 1, 1, 0, 0, screen);
        check(aim.sample(3990000000L, 1, 1, 0, 0, screen) && aim.restart == GyroscopeAim.RESTART_TIME, "Out-of-order timestamps start the time over");
        aim.reset();
        check(aim.sample(5000000000L, 1, 1, 0, 0, screen) && aim.restart == GyroscopeAim.RESTART_TIME, "A pause keeps the bias");
        System.out.println("Gyroscope orientation and lifecycle checks passed");
    }
}
