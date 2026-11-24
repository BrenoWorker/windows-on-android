package com.brenoworker.windroid;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.util.SparseArray;
import android.util.SparseIntArray;
import android.view.HapticFeedbackConstants;
import android.view.MotionEvent;
import android.view.View;
import org.json.JSONArray;
import org.json.JSONObject;
import java.util.ArrayList;
import java.util.List;

final class GamePad extends View {

    interface Listener {
        void onKey(String key, boolean down);

        void onButton(String id, boolean down);

        void onToggleControls();

        void onExitFullscreen();
    }

    private static final class Button {
        String id;
        String label;
        String key;
        float cx;
        float cy;
        float r;
        boolean down;
    }

    private static final int UP = 1;
    private static final int DOWN = 2;
    private static final int LEFT = 4;
    private static final int RIGHT = 8;
    private static final String[] DIR_IDS = {"up", "down", "left", "right"};
    private static final Object DPAD = new Object();
    private static final Object TOGGLE = new Object();
    private static final Object EXIT = new Object();

    private final Listener listener;
    private final float dp;
    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint line = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint label = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Path path = new Path();
    private final List<Button> buttons = new ArrayList<>();
    private final SparseArray<Object> owners = new SparseArray<>();
    private final SparseIntArray dirs = new SparseIntArray();
    private final RectF toggleRect = new RectF();
    private final RectF exitRect = new RectF();
    private final String[] dirKeys = {"ArrowUp", "ArrowDown", "ArrowLeft", "ArrowRight"};
    private boolean dpad = true;
    private boolean controls;
    private boolean fullscreen;
    private boolean togglePressed;
    private boolean exitPressed;
    private int held;
    private float opacity = 0.55f;
    private float dcx;
    private float dcy;
    private float dr;

    GamePad(Context context, Listener listener) {
        super(context);
        this.listener = listener;
        dp = context.getResources().getDisplayMetrics().density;
        line.setStyle(Paint.Style.STROKE);
        line.setStrokeWidth(1.5f * dp);
        label.setTextAlign(Paint.Align.CENTER);
        label.setFakeBoldText(true);
        setDefaults();
        setVisibility(GONE);
    }

    private void setDefaults() {
        dpad = true;
        dirKeys[0] = "ArrowUp";
        dirKeys[1] = "ArrowDown";
        dirKeys[2] = "ArrowLeft";
        dirKeys[3] = "ArrowRight";
        opacity = 0.55f;
        buttons.clear();
        addButton("a", "A", " ");
        addButton("b", "B", "Enter");
    }

    private void addButton(String id, String text, String key) {
        Button b = new Button();
        b.id = id;
        b.label = text;
        b.key = key;
        buttons.add(b);
    }

    String gamepadKey(int button) {
        if (button >= 10 && button <= 13) return dirKeys[button - 10];
        if (button <= 5) return button < buttons.size() ? buttons.get(button).key : "";
        if (button == 8) return "Enter";
        if (button == 9) return "Escape";
        return "";
    }

    String gamepadId(int button) {
        if (button >= 10 && button <= 13) return DIR_IDS[button - 10];
        if (button <= 5 && button < buttons.size()) return buttons.get(button).id;
        String[] names = {"a", "b", "x", "y", "l1", "r1", "l2", "r2", "start", "select", "up", "down", "left", "right", "l3", "r3"};
        return button >= 0 && button < names.length ? names[button] : "pad" + button;
    }

    void setState(boolean showControls, boolean isFullscreen) {
        if (controls && !showControls) releaseAll();
        controls = showControls;
        fullscreen = isFullscreen;
        if (!fullscreen) {
            togglePressed = false;
            exitPressed = false;
        }
        setVisibility(controls || fullscreen ? VISIBLE : GONE);
        invalidate();
    }

    void configure(String json) {
        releaseAll();
        setDefaults();
        if (json != null && !json.isEmpty()) {
            try {
                JSONObject o = new JSONObject(json);
                Object d = o.opt("dpad");
                if (d instanceof Boolean) dpad = (Boolean) d;
                if ("wasd".equals(d)) {
                    dirKeys[0] = "w";
                    dirKeys[1] = "s";
                    dirKeys[2] = "a";
                    dirKeys[3] = "d";
                }
                if (d instanceof JSONObject) {
                    JSONObject k = (JSONObject) d;
                    for (int i = 0; i < 4; i++) dirKeys[i] = k.optString(DIR_IDS[i], dirKeys[i]);
                }
                if (o.has("opacity")) opacity = (float) Math.max(0.15, Math.min(1.0, o.optDouble("opacity", opacity)));
                JSONArray list = o.optJSONArray("buttons");
                if (list != null) {
                    buttons.clear();
                    for (int i = 0; i < list.length() && buttons.size() < 6; i++) {
                        JSONObject b = list.optJSONObject(i);
                        if (b == null) continue;
                        String id = b.optString("id", "b" + i);
                        addButton(id, b.optString("label", id.toUpperCase()), b.optString("key", ""));
                    }
                }
            } catch (Exception ignored) {
                setDefaults();
            }
        }
        place(getWidth(), getHeight());
        invalidate();
    }

    @Override
    protected void onSizeChanged(int w, int h, int ow, int oh) {
        place(w, h);
    }

    private void place(int w, int h) {
        if (w <= 0 || h <= 0) return;
        float m = 22 * dp;
        float base = Math.min(w, h);
        dr = Math.max(48 * dp, Math.min(80 * dp, base * 0.17f));
        dcx = m + dr;
        dcy = h - m - dr;
        float ax = w - m - dr;
        float ay = h - m - dr;
        float br = Math.max(24 * dp, dr * 0.42f);
        int n = buttons.size();
        for (int i = 0; i < n; i++) {
            Button b = buttons.get(i);
            b.r = br;
            float ox;
            float oy;
            if (n == 1) {
                ox = 0;
                oy = 0;
            } else if (n == 2) {
                ox = i == 0 ? -0.62f : 0.62f;
                oy = i == 0 ? 0.45f : -0.45f;
            } else if (n <= 4) {
                double a = Math.PI / 2 - i * (2 * Math.PI / 4);
                ox = (float) Math.cos(a) * 0.72f;
                oy = (float) Math.sin(a) * 0.72f;
            } else {
                double a = Math.PI / 2 - i * (2 * Math.PI / n);
                ox = (float) Math.cos(a) * 0.8f;
                oy = (float) Math.sin(a) * 0.8f;
            }
            b.cx = ax + ox * dr * 1.15f;
            b.cy = ay + oy * dr * 1.15f;
        }
        float s = 38 * dp;
        float top = 10 * dp;
        exitRect.set(w - 10 * dp - s, top, w - 10 * dp, top + s);
        toggleRect.set(exitRect.left - 8 * dp - s, top, exitRect.left - 8 * dp, top + s);
    }

    @Override
    protected void onDraw(Canvas c) {
        int a = (int) (255 * opacity);
        if (controls && dpad) drawDpad(c, a);
        if (controls) for (Button b : buttons) drawButton(c, b, a);
        if (fullscreen) {
            drawChip(c, toggleRect, togglePressed);
            drawChip(c, exitRect, exitPressed);
            float u = toggleRect.width() / 16f;
            line.setColor(controls ? 0xFF60CDFF : 0xFFFFFFFF);
            RectF body = new RectF(toggleRect.left + 3 * u, toggleRect.top + 5 * u, toggleRect.right - 3 * u, toggleRect.bottom - 5 * u);
            c.drawRoundRect(body, 2.5f * u, 2.5f * u, line);
            c.drawLine(toggleRect.left + 5 * u, toggleRect.centerY(), toggleRect.left + 7.6f * u, toggleRect.centerY(), line);
            c.drawLine(toggleRect.left + 6.3f * u, toggleRect.centerY() - 1.3f * u, toggleRect.left + 6.3f * u, toggleRect.centerY() + 1.3f * u, line);
            c.drawCircle(toggleRect.right - 5.5f * u, toggleRect.centerY(), 0.8f * u, line);
            line.setColor(0xFFFFFFFF);
            float e = exitRect.width() * 0.22f;
            c.drawLine(exitRect.centerX() - e, exitRect.centerY() - e, exitRect.centerX() + e, exitRect.centerY() + e, line);
            c.drawLine(exitRect.centerX() + e, exitRect.centerY() - e, exitRect.centerX() - e, exitRect.centerY() + e, line);
        }
    }

    private void drawChip(Canvas c, RectF r, boolean pressed) {
        fill.setColor(pressed ? 0xCC3A3A3A : 0x99202020);
        c.drawRoundRect(r, r.height() / 2, r.height() / 2, fill);
    }

    private void drawDpad(Canvas c, int a) {
        fill.setColor((a / 2 << 24) | 0x101010);
        c.drawCircle(dcx, dcy, dr, fill);
        line.setColor((a << 24) | 0xFFFFFF);
        c.drawCircle(dcx, dcy, dr, line);
        float arm = dr * 0.62f;
        float t = dr * 0.2f;
        for (int i = 0; i < 4; i++) {
            int bit = 1 << i;
            float dx = i == 2 ? -1 : (i == 3 ? 1 : 0);
            float dy = i == 0 ? -1 : (i == 1 ? 1 : 0);
            float px = dcx + dx * arm;
            float py = dcy + dy * arm;
            boolean on = (held & bit) != 0;
            if (on) {
                fill.setColor((Math.min(255, a + 60) << 24) | 0x60CDFF);
                c.drawCircle(px, py, dr * 0.3f, fill);
            }
            path.reset();
            path.moveTo(px + dx * t, py + dy * t);
            path.lineTo(px - dx * t * 0.6f + dy * t, py - dy * t * 0.6f + dx * t);
            path.lineTo(px - dx * t * 0.6f - dy * t, py - dy * t * 0.6f - dx * t);
            path.close();
            fill.setColor((Math.min(255, a + 70) << 24) | 0xFFFFFF);
            c.drawPath(path, fill);
        }
    }

    private void drawButton(Canvas c, Button b, int a) {
        fill.setColor(b.down ? ((Math.min(255, a + 60) << 24) | 0x60CDFF) : ((a / 2 << 24) | 0x101010));
        c.drawCircle(b.cx, b.cy, b.r, fill);
        line.setColor((a << 24) | 0xFFFFFF);
        c.drawCircle(b.cx, b.cy, b.r, line);
        String text = b.label.length() > 5 ? b.label.substring(0, 5) : b.label;
        label.setTextSize(b.r * (text.length() <= 1 ? 0.8f : (text.length() <= 3 ? 0.55f : 0.4f)));
        label.setColor((Math.min(255, a + 90) << 24) | 0xFFFFFF);
        c.drawText(text, b.cx, b.cy - (label.descent() + label.ascent()) / 2, label);
    }

    private Object hit(float x, float y) {
        if (fullscreen) {
            if (toggleRect.contains(x, y)) return TOGGLE;
            if (exitRect.contains(x, y)) return EXIT;
        }
        if (!controls) return null;
        for (Button b : buttons) {
            float dx = x - b.cx;
            float dy = y - b.cy;
            if (dx * dx + dy * dy <= b.r * b.r * 1.6f) return b;
        }
        if (dpad) {
            float dx = x - dcx;
            float dy = y - dcy;
            if (dx * dx + dy * dy <= dr * dr * 1.5f) return DPAD;
        }
        return null;
    }

    private int dirMask(float x, float y) {
        float dx = x - dcx;
        float dy = y - dcy;
        if (dx * dx + dy * dy < dr * dr * 0.05f) return 0;
        int sector = (int) Math.round(Math.atan2(dy, dx) / (Math.PI / 4));
        sector = ((sector % 8) + 8) % 8;
        switch (sector) {
            case 0: return RIGHT;
            case 1: return RIGHT | DOWN;
            case 2: return DOWN;
            case 3: return DOWN | LEFT;
            case 4: return LEFT;
            case 5: return LEFT | UP;
            case 6: return UP;
            default: return UP | RIGHT;
        }
    }

    private void syncDirs() {
        int now = 0;
        for (int i = 0; i < dirs.size(); i++) now |= dirs.valueAt(i);
        int changed = now ^ held;
        if (changed == 0) return;
        for (int i = 0; i < 4; i++) {
            int bit = 1 << i;
            if ((changed & bit) == 0) continue;
            boolean down = (now & bit) != 0;
            if (dirKeys[i] != null && !dirKeys[i].isEmpty()) listener.onKey(dirKeys[i], down);
            listener.onButton(DIR_IDS[i], down);
        }
        if ((now & ~held) != 0) performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
        held = now;
        invalidate();
    }

    private void press(Button b, boolean down) {
        if (b.down == down) return;
        b.down = down;
        if (b.key != null && !b.key.isEmpty()) listener.onKey(b.key, down);
        listener.onButton(b.id, down);
        if (down) performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
        invalidate();
    }

    private void release(int pointer, float x, float y, boolean fire) {
        Object t = owners.get(pointer);
        owners.remove(pointer);
        if (t == null) return;
        if (t == DPAD) {
            dirs.delete(pointer);
            syncDirs();
        } else if (t instanceof Button) {
            if (!stillHeld((Button) t)) press((Button) t, false);
        } else if (t == TOGGLE) {
            togglePressed = false;
            invalidate();
            if (fire && toggleRect.contains(x, y)) listener.onToggleControls();
        } else if (t == EXIT) {
            exitPressed = false;
            invalidate();
            if (fire && exitRect.contains(x, y)) listener.onExitFullscreen();
        }
    }

    private boolean stillHeld(Button b) {
        for (int i = 0; i < owners.size(); i++) if (owners.valueAt(i) == b) return true;
        return false;
    }

    void releaseAll() {
        owners.clear();
        dirs.clear();
        syncDirs();
        for (Button b : buttons) press(b, false);
        togglePressed = false;
        exitPressed = false;
        invalidate();
    }

    @Override
    public boolean onTouchEvent(MotionEvent e) {
        int action = e.getActionMasked();
        int index = e.getActionIndex();
        switch (action) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN: {
                float x = e.getX(index);
                float y = e.getY(index);
                Object t = hit(x, y);
                if (t == null) return action != MotionEvent.ACTION_DOWN;
                int id = e.getPointerId(index);
                owners.put(id, t);
                if (t == DPAD) {
                    dirs.put(id, dirMask(x, y));
                    syncDirs();
                } else if (t instanceof Button) {
                    press((Button) t, true);
                } else if (t == TOGGLE) {
                    togglePressed = true;
                    invalidate();
                } else if (t == EXIT) {
                    exitPressed = true;
                    invalidate();
                }
                return true;
            }
            case MotionEvent.ACTION_MOVE: {
                boolean any = false;
                for (int i = 0; i < e.getPointerCount(); i++) {
                    int id = e.getPointerId(i);
                    if (owners.get(id) == DPAD) {
                        dirs.put(id, dirMask(e.getX(i), e.getY(i)));
                        any = true;
                    }
                }
                if (any) syncDirs();
                return true;
            }
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP:
                release(e.getPointerId(index), e.getX(index), e.getY(index), true);
                return true;
            case MotionEvent.ACTION_CANCEL:
                releaseAll();
                return true;
            default:
                return true;
        }
    }
}
