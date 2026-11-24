package com.brenoworker.windroid;

import android.Manifest;
import android.annotation.SuppressLint;
import android.app.Activity;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothClass;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothManager;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.provider.Settings;
import android.webkit.MimeTypeMap;
import java.io.File;
import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;

@SuppressLint("MissingPermission")
final class BluetoothHub {

    private static final String ACTION_BATTERY = "android.bluetooth.device.action.BATTERY_LEVEL_CHANGED";
    private static final String EXTRA_BATTERY = "android.bluetooth.device.extra.BATTERY_LEVEL";
    private static final int REQUEST_PERMISSIONS = 4101;
    private static final int REQUEST_ENABLE = 4102;

    private static final class Found {
        String name;
        int rssi;
        int cls;
    }

    private final Activity activity;
    private final BluetoothAdapter adapter;
    private final Map<String, Found> found = new LinkedHashMap<>();
    private final Set<String> connected = ConcurrentHashMap.newKeySet();
    private final Map<String, Integer> battery = new ConcurrentHashMap<>();
    private volatile String message = "";
    private boolean registered;
    private SoundFx sounds;

    void setSounds(SoundFx s) {
        sounds = s;
    }

    private final BroadcastReceiver receiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            String action = intent.getAction();
            if (action == null) return;
            BluetoothDevice device = intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE);
            switch (action) {
                case BluetoothDevice.ACTION_FOUND:
                    if (device == null) return;
                    synchronized (found) {
                        Found f = found.get(device.getAddress());
                        if (f == null) f = new Found();
                        f.name = safeName(device);
                        f.rssi = intent.getShortExtra(BluetoothDevice.EXTRA_RSSI, Short.MIN_VALUE);
                        BluetoothClass c = intent.getParcelableExtra(BluetoothDevice.EXTRA_CLASS);
                        f.cls = c != null ? c.getDeviceClass() : classOf(device);
                        found.put(device.getAddress(), f);
                    }
                    break;
                case BluetoothDevice.ACTION_ACL_CONNECTED:
                    if (device != null && connected.add(device.getAddress()) && sounds != null) sounds.play("device-in");
                    break;
                case BluetoothDevice.ACTION_ACL_DISCONNECTED:
                    if (device != null) {
                        if (connected.remove(device.getAddress()) && sounds != null) sounds.play("device-out");
                        battery.remove(device.getAddress());
                    }
                    break;
                case BluetoothDevice.ACTION_BOND_STATE_CHANGED:
                    if (device == null) return;
                    int state = intent.getIntExtra(BluetoothDevice.EXTRA_BOND_STATE, BluetoothDevice.BOND_NONE);
                    int prev = intent.getIntExtra(BluetoothDevice.EXTRA_PREVIOUS_BOND_STATE, BluetoothDevice.BOND_NONE);
                    if (state == BluetoothDevice.BOND_BONDED) {
                        message = "Paired with " + safeName(device);
                        synchronized (found) {
                            found.remove(device.getAddress());
                        }
                    } else if (state == BluetoothDevice.BOND_NONE && prev == BluetoothDevice.BOND_BONDING) {
                        message = "Couldn't pair with " + safeName(device);
                    }
                    break;
                case ACTION_BATTERY:
                    if (device != null) {
                        int level = intent.getIntExtra(EXTRA_BATTERY, -1);
                        if (level >= 0) battery.put(device.getAddress(), level);
                    }
                    break;
                case BluetoothAdapter.ACTION_STATE_CHANGED:
                    int s = intent.getIntExtra(BluetoothAdapter.EXTRA_STATE, BluetoothAdapter.STATE_OFF);
                    if (s == BluetoothAdapter.STATE_OFF) {
                        connected.clear();
                        battery.clear();
                        synchronized (found) {
                            found.clear();
                        }
                    }
                    break;
                default:
                    break;
            }
        }
    };

    BluetoothHub(Activity activity) {
        this.activity = activity;
        BluetoothManager m = (BluetoothManager) activity.getSystemService(Context.BLUETOOTH_SERVICE);
        adapter = m != null ? m.getAdapter() : null;
    }

    void start() {
        if (adapter == null || registered) return;
        IntentFilter f = new IntentFilter();
        f.addAction(BluetoothDevice.ACTION_FOUND);
        f.addAction(BluetoothDevice.ACTION_ACL_CONNECTED);
        f.addAction(BluetoothDevice.ACTION_ACL_DISCONNECTED);
        f.addAction(BluetoothDevice.ACTION_BOND_STATE_CHANGED);
        f.addAction(BluetoothAdapter.ACTION_STATE_CHANGED);
        f.addAction(BluetoothAdapter.ACTION_DISCOVERY_FINISHED);
        f.addAction(ACTION_BATTERY);
        try {
            if (Build.VERSION.SDK_INT >= 33) activity.registerReceiver(receiver, f, Context.RECEIVER_EXPORTED);
            else activity.registerReceiver(receiver, f);
            registered = true;
        } catch (Throwable ignored) {
        }
        refreshConnected();
    }

    void stop() {
        if (!registered) return;
        try {
            activity.unregisterReceiver(receiver);
        } catch (Throwable ignored) {
        }
        registered = false;
    }

    private boolean modern() {
        return Build.VERSION.SDK_INT >= 31 && activity.getApplicationInfo().targetSdkVersion >= 31;
    }

    private String[] permissions() {
        if (modern())
            return new String[] {Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT};
        if (Build.VERSION.SDK_INT >= 29) return new String[] {Manifest.permission.ACCESS_FINE_LOCATION};
        return new String[] {Manifest.permission.ACCESS_COARSE_LOCATION};
    }

    boolean hasPermission() {
        if (Build.VERSION.SDK_INT < 23) return true;
        for (String p : permissions())
            if (activity.checkSelfPermission(p) != PackageManager.PERMISSION_GRANTED) return false;
        return true;
    }

    private boolean canConnect() {
        return !modern() || activity.checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_GRANTED;
    }

    void requestPermission() {
        if (Build.VERSION.SDK_INT >= 23 && !hasPermission()) activity.requestPermissions(permissions(), REQUEST_PERMISSIONS);
    }

    void onPermissionResult() {
        refreshConnected();
    }

    String state() {
        if (adapter == null) return "0\t0\t0\t0\t\t";
        boolean perm = hasPermission();
        boolean on = false;
        boolean scanning = false;
        String name = "";
        try {
            on = adapter.isEnabled();
            if (perm) scanning = adapter.isDiscovering();
            if (canConnect()) name = adapter.getName();
        } catch (Throwable ignored) {
        }
        String msg = message;
        message = "";
        return "1\t" + (on ? 1 : 0) + "\t" + (scanning ? 1 : 0) + "\t" + (perm ? 1 : 0) + "\t" + clean(name) + "\t" + clean(msg);
    }

    void setEnabled(boolean on) {
        if (adapter == null) return;
        if (!canConnect()) {
            requestPermission();
            return;
        }
        try {
            if (on) {
                if (adapter.isEnabled()) return;
                if (Build.VERSION.SDK_INT < 33 && adapter.enable()) return;
                activity.startActivityForResult(new Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE), REQUEST_ENABLE);
            } else {
                if (!adapter.isEnabled()) return;
                if (Build.VERSION.SDK_INT < 33 && adapter.disable()) return;
                openSystemSettings();
            }
        } catch (Throwable t) {
            openSystemSettings();
        }
    }

    void openSystemSettings() {
        try {
            activity.startActivity(new Intent(Settings.ACTION_BLUETOOTH_SETTINGS));
        } catch (Throwable ignored) {
        }
    }

    void scan(boolean on) {
        if (adapter == null) return;
        if (!hasPermission()) {
            requestPermission();
            return;
        }
        try {
            if (on) {
                synchronized (found) {
                    found.clear();
                }
                if (adapter.isDiscovering()) adapter.cancelDiscovery();
                if (!adapter.startDiscovery()) message = "Couldn't start searching for devices";
            } else {
                adapter.cancelDiscovery();
            }
        } catch (Throwable t) {
            message = "Couldn't start searching for devices";
        }
    }

    void pair(String address) {
        BluetoothDevice d = device(address);
        if (d == null) return;
        try {
            adapter.cancelDiscovery();
            if (!d.createBond()) message = "Couldn't pair with " + safeName(d);
        } catch (Throwable t) {
            message = "Couldn't pair with " + safeName(d);
        }
    }

    void unpair(String address) {
        BluetoothDevice d = device(address);
        if (d == null) return;
        try {
            Method m = d.getClass().getMethod("removeBond");
            Object r = m.invoke(d);
            if (r instanceof Boolean && (Boolean) r) {
                message = "Removed " + safeName(d);
                return;
            }
        } catch (Throwable ignored) {
        }
        message = "Remove " + safeName(d) + " from Android Bluetooth settings";
        openSystemSettings();
    }

    void rename(String name) {
        if (adapter == null || !canConnect() || name == null || name.trim().isEmpty()) return;
        try {
            adapter.setName(name.trim());
        } catch (Throwable ignored) {
        }
    }

    String devices() {
        StringBuilder sb = new StringBuilder();
        if (adapter == null || !canConnect()) return "";
        try {
            Set<BluetoothDevice> bonded = adapter.getBondedDevices();
            if (bonded != null) {
                for (BluetoothDevice d : bonded) {
                    String addr = d.getAddress();
                    boolean on = connected.contains(addr) || reflectConnected(d);
                    int bat = battery.containsKey(addr) ? battery.get(addr) : reflectBattery(d);
                    line(sb, addr, safeName(d), kind(classOf(d)), 1, on, bat, 0);
                }
            }
        } catch (Throwable ignored) {
        }
        List<Map.Entry<String, Found>> list;
        synchronized (found) {
            list = new ArrayList<>(found.entrySet());
        }
        for (Map.Entry<String, Found> e : list) {
            BluetoothDevice d = device(e.getKey());
            int bond = d != null && d.getBondState() == BluetoothDevice.BOND_BONDING ? 2 : 0;
            if (d != null && d.getBondState() == BluetoothDevice.BOND_BONDED) continue;
            Found f = e.getValue();
            line(sb, e.getKey(), f.name, kind(f.cls), bond, false, -1, f.rssi == Short.MIN_VALUE ? 0 : f.rssi);
        }
        return sb.toString();
    }

    String send(File file) {
        if (file == null || !file.isFile()) return "File not found";
        try {
            Uri uri = FilesProvider.uriFor(activity, file);
            String ext = MimeTypeMap.getFileExtensionFromUrl(file.getName());
            String type = ext == null ? null : MimeTypeMap.getSingleton().getMimeTypeFromExtension(ext.toLowerCase());
            Intent intent = new Intent(Intent.ACTION_SEND);
            intent.setType(type == null ? "application/octet-stream" : type);
            intent.putExtra(Intent.EXTRA_STREAM, uri);
            intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
            Intent bt = new Intent(intent);
            bt.setPackage("com.android.bluetooth");
            if (bt.resolveActivity(activity.getPackageManager()) != null) {
                activity.startActivity(bt);
            } else {
                activity.startActivity(Intent.createChooser(intent, "Send via Bluetooth"));
            }
            return "";
        } catch (Throwable t) {
            return "Couldn't share this file";
        }
    }

    private void refreshConnected() {
        if (adapter == null || !canConnect()) return;
        try {
            Set<BluetoothDevice> bonded = adapter.getBondedDevices();
            if (bonded == null) return;
            for (BluetoothDevice d : bonded)
                if (reflectConnected(d)) connected.add(d.getAddress());
        } catch (Throwable ignored) {
        }
    }

    private BluetoothDevice device(String address) {
        if (adapter == null || address == null || !BluetoothAdapter.checkBluetoothAddress(address)) return null;
        try {
            return adapter.getRemoteDevice(address);
        } catch (Throwable t) {
            return null;
        }
    }

    private static boolean reflectConnected(BluetoothDevice d) {
        try {
            Method m = d.getClass().getMethod("isConnected");
            Object r = m.invoke(d);
            return r instanceof Boolean && (Boolean) r;
        } catch (Throwable t) {
            return false;
        }
    }

    private static int reflectBattery(BluetoothDevice d) {
        try {
            Method m = d.getClass().getMethod("getBatteryLevel");
            Object r = m.invoke(d);
            if (r instanceof Integer && (Integer) r >= 0 && (Integer) r <= 100) return (Integer) r;
        } catch (Throwable ignored) {
        }
        return -1;
    }

    private static int classOf(BluetoothDevice d) {
        try {
            BluetoothClass c = d.getBluetoothClass();
            return c == null ? 0 : c.getDeviceClass();
        } catch (Throwable t) {
            return 0;
        }
    }

    private static String kind(int cls) {
        int major = cls & 0x1F00;
        switch (cls) {
            case BluetoothClass.Device.AUDIO_VIDEO_HEADPHONES:
            case BluetoothClass.Device.AUDIO_VIDEO_WEARABLE_HEADSET:
            case BluetoothClass.Device.AUDIO_VIDEO_HANDSFREE:
                return "headset";
            case BluetoothClass.Device.PERIPHERAL_KEYBOARD:
            case BluetoothClass.Device.PERIPHERAL_KEYBOARD_POINTING:
                return "keyboard";
            case BluetoothClass.Device.PERIPHERAL_POINTING:
                return "mouse";
            default:
                break;
        }
        if (major == 0x0500 && ((cls & 0x3C) == 0x08 || (cls & 0x3C) == 0x04)) return "gamepad";
        switch (major) {
            case BluetoothClass.Device.Major.AUDIO_VIDEO: return "audio";
            case BluetoothClass.Device.Major.PERIPHERAL: return "input";
            case BluetoothClass.Device.Major.PHONE: return "phone";
            case BluetoothClass.Device.Major.COMPUTER: return "computer";
            case BluetoothClass.Device.Major.WEARABLE: return "watch";
            case BluetoothClass.Device.Major.TOY: return "gamepad";
            default: return "other";
        }
    }

    private String safeName(BluetoothDevice d) {
        String n = null;
        try {
            if (canConnect()) n = d.getName();
        } catch (Throwable ignored) {
        }
        return n == null || n.isEmpty() ? d.getAddress() : n;
    }

    private static void line(StringBuilder sb, String addr, String name, String kind, int bond, boolean on, int bat, int rssi) {
        sb.append(addr).append('\t').append(clean(name)).append('\t').append(kind).append('\t').append(bond).append('\t')
                .append(on ? 1 : 0).append('\t').append(bat).append('\t').append(rssi).append('\n');
    }

    private static String clean(String s) {
        return s == null ? "" : s.replace('\t', ' ').replace('\n', ' ');
    }
}
