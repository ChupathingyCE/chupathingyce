package com.halo.decomp;

import java.io.IOException;
import java.io.StringReader;
import java.io.StringWriter;
import java.util.ArrayList;
import java.util.Properties;

/** Display-relative positions, visibility and action types for touch controls. */
final class TouchLayout {
    static final int LEFT = 16, FIRE_LEFT = 17;
    private static final float[][] DEFAULTS = {
        {856,447}, {913,377}, {794,377}, {850,312}, {915,239}, {802,239},
        {236,449}, {691,449}, {360,490}, {438,490}, {570,36}, {390,36},
        {100,237}, {100,335}, {51,286}, {149,286}, {115,440}, {255,239}
    };
    private static final float[] RADII = {
        36,32,34,32,39,35,32,32,27,29,28,28,25,25,25,25,64,39
    };
    /**
     * The layout for a small, wide screen (a 5.5 inch 960x480 Echo Show 5): the controls in
     * pixels of that screen, listed in the full layout's y of 540 (y * 1.125) so the same scaling
     * applies. The d-pad and the second Fire are hidden; the rest are larger where it matters
     * (the stick, Fire, Jump) and put in the lower corners, leaving the middle and the top
     * free for swipe aiming.
     */
    private static final float[][] COMPACT_DEFAULTS = {
        {790,456}, {900,467}, {690,473}, {900,191}, {880,315}, {770,321},
        {255,338}, {245,456}, {380,495}, {580,495}, {560,38}, {400,38},
        {100,237}, {100,335}, {51,286}, {149,286}, {110,411}, {255,239}
    };
    private static final float[] COMPACT_RADII = {
        42,38,36,34,54,40,34,34,30,30,28,28,25,25,25,25,62,39
    };
    private static final boolean[] COMPACT_SHOWN = {
        true,true,true,true,true,true,true,true,true,true,true,true,
        false,false,false,false,true,false
    };
    private final boolean compact;
    static final int BASE_COUNT = 18, MAX_CONTROLS = 64;
    private static final class Control {
        final int type;
        float x, y;
        boolean shown = true;
        float size = 1f;
        Control(int type, float x, float y) { this.type = type; this.x = x; this.y = y; }
    }
    private final ArrayList<Control> controls = new ArrayList<>();

    private float width = 960, height = 540;
    boolean rumbleEnabled = true, gyroscopeEnabled = false;
    static final float MIN_SIZE = 0.5f, MAX_SIZE = 2f;

    void bounds(float width, float height) {
        if (!Float.isFinite(width) || !Float.isFinite(height) || width <= 0 || height <= 0)
            throw new IllegalArgumentException("Invalid display dimensions");
        for (Control control : controls) {
            control.x *= width / this.width;
            control.y *= height / this.height;
        }
        this.width = width; this.height = height;
        for (int i = 0; i < size(); i++) move(i, x(i), y(i));
    }
    float savedX(int i) { return x(i) * 960 / width; }
    float savedY(int i) { return y(i) * 540 / height; }

    TouchLayout() { this(false); }
    TouchLayout(boolean compact) { this.compact = compact; resetDefaults(); }
    int size() { return controls.size(); }
    int type(int control) { return controls.get(control).type; }
    float x(int control) { return controls.get(control).x; }
    float y(int control) { return controls.get(control).y; }
    float radius(int control) { return (compact ? COMPACT_RADII : RADII)[type(control)] * controls.get(control).size; }
    float sizeScale(int control) { return controls.get(control).size; }
    void setSize(int control, float size) {
        if (!Float.isFinite(size) || size < MIN_SIZE || size > MAX_SIZE)
            throw new IllegalArgumentException("Button size must be between 50% and 200%");
        controls.get(control).size = size;
        move(control, x(control), y(control));
    }
    boolean shown(int control) { return controls.get(control).shown; }
    void setShown(int control, boolean shown) { controls.get(control).shown = shown; }

    void resetDefaults() {
        controls.clear();
        float[][] defaults = compact ? COMPACT_DEFAULTS : DEFAULTS;
        for (int i = 0; i < defaults.length; i++) {
            Control control = new Control(i, defaults[i][0]*width/960, defaults[i][1]*height/540);
            if (compact) control.shown = COMPACT_SHOWN[i];
            controls.add(control);
        }
        for (int i = 0; i < size(); i++) move(i, x(i), y(i));
    }

    int duplicate(int source) {
        if (size() >= MAX_CONTROLS || type(source) == LEFT) return -1;
        Control added = new Control(type(source), x(source)+radius(source)*2+12, y(source));
        added.size = sizeScale(source);
        controls.add(added);
        int index = size()-1;
        // If there is no space on the right, place the copy to the left instead.
        if (added.x > width-radius(source)) added.x = x(source)-radius(source)*2-12;
        move(index, added.x, added.y);
        return index;
    }

    int add(int type) {
        if (type < 0 || type >= BASE_COUNT) return -1;
        for (int i = 0; i < size(); i++) {
            if (type(i) == type && !shown(i)) { setShown(i, true); return i; }
        }
        return duplicate(type);
    }

    String exportConfiguration(float sensitivity) {
        if (!validSensitivity(sensitivity)) throw new IllegalArgumentException("Invalid sensitivity");
        Properties values = new Properties();
        values.setProperty("format", "halo-touch-layout");
        values.setProperty("version", "2");
        values.setProperty("rumble", Boolean.toString(rumbleEnabled));
        values.setProperty("gyroscope", Boolean.toString(gyroscopeEnabled));
        values.setProperty("count", Integer.toString(size()));
        values.setProperty("sensitivity", Float.toString(sensitivity));
        for (int i = 0; i < size(); i++) {
            String key = "control."+i+".";
            values.setProperty(key+"type", Integer.toString(type(i)));
            values.setProperty(key+"x", Float.toString(savedX(i)));
            values.setProperty(key+"y", Float.toString(savedY(i)));
            values.setProperty(key+"visible", Boolean.toString(shown(i)));
            values.setProperty(key+"size", Float.toString(sizeScale(i)));
        }
        StringWriter text = new StringWriter();
        try { values.store(text, "Halo Android touch layout"); }
        catch (IOException e) { throw new IllegalStateException(e); }
        return text.toString();
    }

    static final class Configuration {
        final TouchLayout layout;
        final float sensitivity;
        Configuration(TouchLayout layout, float sensitivity) {
            this.layout = layout; this.sensitivity = sensitivity;
        }
    }

    private static boolean validSensitivity(float value) {
        return Float.isFinite(value) && value >= 0.25f && value <= 4f;
    }

    static Configuration importConfiguration(String text) { return importConfiguration(text, false); }

    static Configuration importConfiguration(String text, boolean compact) {
        if (text == null || text.length() > 65536) throw new IllegalArgumentException("Invalid layout file");
        Properties values = new Properties();
        try {
            values.load(new StringReader(text));
            String version = values.getProperty("version");
            if (!"halo-touch-layout".equals(values.getProperty("format")) ||
                    !("1".equals(version) || "2".equals(version)))
                throw new IllegalArgumentException("Unsupported layout file");
            int count = Integer.parseInt(values.getProperty("count"));
            float sensitivity = Float.parseFloat(values.getProperty("sensitivity"));
            if (count < BASE_COUNT || count > MAX_CONTROLS || !validSensitivity(sensitivity))
                throw new IllegalArgumentException("Invalid layout settings");
            TouchLayout layout = new TouchLayout(compact);
            layout.controls.clear();
            if ("2".equals(version)) {
                layout.rumbleEnabled = readBoolean(values, "rumble");
                layout.gyroscopeEnabled = readBoolean(values, "gyroscope");
            }
            for (int i = 0; i < count; i++) {
                String key = "control."+i+".";
                int type = Integer.parseInt(values.getProperty(key+"type"));
                float x = Float.parseFloat(values.getProperty(key+"x"));
                float y = Float.parseFloat(values.getProperty(key+"y"));
                String shown = values.getProperty(key+"visible");
                float size = "2".equals(version) ? Float.parseFloat(values.getProperty(key+"size")) : 1f;
                if (type < 0 || type >= BASE_COUNT || (i < BASE_COUNT && type != i) ||
                        (i >= BASE_COUNT && type == LEFT) || !Float.isFinite(x) || !Float.isFinite(y) ||
                        x < 0 || x > 960 || y < 0 || y > 540 ||
                        !("true".equals(shown) || "false".equals(shown)) ||
                        !Float.isFinite(size) || size < MIN_SIZE || size > MAX_SIZE)
                    throw new IllegalArgumentException("Invalid control in layout file");
                Control control = new Control(type, x, y);
                control.shown = Boolean.parseBoolean(shown);
                control.size = size;
                layout.controls.add(control);
            }
            return new Configuration(layout, sensitivity);
        } catch (IOException | NullPointerException e) {
            throw new IllegalArgumentException("Incomplete layout file", e);
        }
    }

    private static boolean readBoolean(Properties values, String key) {
        String value = values.getProperty(key);
        if (!("true".equals(value) || "false".equals(value)))
            throw new IllegalArgumentException("Invalid "+key+" option");
        return Boolean.parseBoolean(value);
    }

    void move(int control, float x, float y) {
        if (!Float.isFinite(x) || !Float.isFinite(y)) return;
        float radius = radius(control);
        controls.get(control).x = Math.max(radius, Math.min(width-radius, x));
        controls.get(control).y = Math.max(radius, Math.min(height-radius, y));
    }

    void restore(int control, float x, float y) {
        if (Float.isFinite(x) && Float.isFinite(y) && x >= 0
                && x <= 960 && y >= 0 && y <= 540) {
            controls.get(control).x = x;
            controls.get(control).y = y;
        }
    }
}
