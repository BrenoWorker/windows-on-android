package com.brenoworker.windroid;

import android.content.Context;
import android.media.AudioAttributes;
import android.media.SoundPool;
import java.io.File;
import java.util.HashMap;
import java.util.Map;

final class SoundFx {

    private final File dir;
    private final SoundPool pool;
    private final Map<String, Integer> ids = new HashMap<>();
    private final Map<Integer, Float> pending = new HashMap<>();
    private final java.util.Set<Integer> ready = new java.util.HashSet<>();
    private volatile boolean enabled = true;
    private volatile float volume = 0.8f;

    SoundFx(Context context) {
        dir = new File(context.getFilesDir(), "sounds");
        AudioAttributes attrs = new AudioAttributes.Builder()
                .setUsage(AudioAttributes.USAGE_ASSISTANCE_SONIFICATION)
                .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
                .build();
        pool = new SoundPool.Builder().setMaxStreams(4).setAudioAttributes(attrs).build();
        pool.setOnLoadCompleteListener(new SoundPool.OnLoadCompleteListener() {
            @Override
            public void onLoadComplete(SoundPool p, int sampleId, int status) {
                synchronized (SoundFx.this) {
                    if (status != 0) return;
                    ready.add(sampleId);
                    Float v = pending.remove(sampleId);
                    if (v != null) p.play(sampleId, v, v, 1, 0, 1f);
                }
            }
        });
    }

    void configure(boolean on, float vol) {
        enabled = on;
        volume = Math.max(0f, Math.min(1f, vol));
    }

    synchronized void play(String name) {
        play(name, volume);
    }

    synchronized void play(String name, float vol) {
        if (!enabled || vol <= 0f) return;
        Integer id = ids.get(name);
        if (id == null) {
            File f = new File(dir, name + ".wav");
            if (!f.isFile()) return;
            id = pool.load(f.getPath(), 1);
            ids.put(name, id);
        }
        if (ready.contains(id)) pool.play(id, vol, vol, 1, 0, 1f);
        else pending.put(id, vol);
    }

    synchronized void reload() {
        for (Integer id : ids.values()) pool.unload(id);
        ids.clear();
        ready.clear();
        pending.clear();
    }

    void release() {
        pool.release();
    }
}
