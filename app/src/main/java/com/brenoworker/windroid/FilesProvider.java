package com.brenoworker.windroid;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.content.Context;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.OpenableColumns;
import android.webkit.MimeTypeMap;
import java.io.File;
import java.io.FileNotFoundException;
import java.io.IOException;

public final class FilesProvider extends ContentProvider {

    static String authority(Context c) {
        return c.getPackageName() + ".files";
    }

    static Uri uriFor(Context c, File f) {
        String base = c.getFilesDir().getAbsolutePath();
        String path = f.getAbsolutePath();
        String rel = path.startsWith(base + "/") ? path.substring(base.length() + 1) : path;
        return new Uri.Builder().scheme("content").authority(authority(c)).encodedPath("/" + Uri.encode(rel, "/")).build();
    }

    private File fileFor(Uri uri) throws FileNotFoundException {
        Context c = getContext();
        if (c == null) throw new FileNotFoundException();
        String rel = uri.getPath();
        if (rel == null) throw new FileNotFoundException();
        while (rel.startsWith("/")) rel = rel.substring(1);
        try {
            File base = c.getFilesDir().getCanonicalFile();
            File f = new File(base, rel).getCanonicalFile();
            if (!f.getPath().startsWith(base.getPath() + "/")) throw new FileNotFoundException();
            return f;
        } catch (IOException e) {
            throw new FileNotFoundException();
        }
    }

    @Override
    public boolean onCreate() {
        return true;
    }

    @Override
    public ParcelFileDescriptor openFile(Uri uri, String mode) throws FileNotFoundException {
        return ParcelFileDescriptor.open(fileFor(uri), ParcelFileDescriptor.MODE_READ_ONLY);
    }

    @Override
    public Cursor query(Uri uri, String[] projection, String selection, String[] selectionArgs, String sortOrder) {
        File f;
        try {
            f = fileFor(uri);
        } catch (FileNotFoundException e) {
            return null;
        }
        String[] cols = {OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE};
        MatrixCursor c = new MatrixCursor(cols, 1);
        c.addRow(new Object[] {f.getName(), f.length()});
        return c;
    }

    @Override
    public String getType(Uri uri) {
        String name = uri.getLastPathSegment();
        if (name == null) return "application/octet-stream";
        int dot = name.lastIndexOf('.');
        String type = dot < 0 ? null : MimeTypeMap.getSingleton().getMimeTypeFromExtension(name.substring(dot + 1).toLowerCase());
        return type == null ? "application/octet-stream" : type;
    }

    @Override
    public Uri insert(Uri uri, ContentValues values) {
        return null;
    }

    @Override
    public int delete(Uri uri, String selection, String[] selectionArgs) {
        return 0;
    }

    @Override
    public int update(Uri uri, ContentValues values, String selection, String[] selectionArgs) {
        return 0;
    }
}
