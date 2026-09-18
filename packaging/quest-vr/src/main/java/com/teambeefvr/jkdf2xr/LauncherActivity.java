package com.teambeefvr.jkdf2xr;

import android.app.Activity;
import android.app.ActivityManager;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.res.AssetManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.Settings;
import android.util.Log;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.Manifest;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileOutputStream;
import java.io.FileReader;
import java.io.FileWriter;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.List;

/**
 * Launcher Activity that checks for storage permission before starting the VR app.
 * This is needed because SDL2 initialization happens in onCreate() and we need
 * to ensure permission is granted before that.
 */
public class LauncherActivity extends Activity {

    private static final String TAG = "JKDF2XR";
    private static final int REQUEST_MANAGE_ALL_FILES = 2296;
    private static final int REQUEST_STORAGE_PERMISSION = 2297;
    private static final String GAME_FOLDER = "/sdcard/JKDF2XR";
    private static final String MOTS_FOLDER = GAME_FOLDER + "/mots";
    private static final String VR_WEAPON_OFFSETS_FILE = "jkdf2xr_vr_weapons.json";
    private static final String COMMANDLINE_FILE = "commandline.txt";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        Log.v(TAG, "LauncherActivity::onCreate()");
        super.onCreate(savedInstanceState);

        // Keep screen on during VR
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        checkPermissionAndLaunch();
    }

    private void checkPermissionAndLaunch() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            // Android 11+ uses MANAGE_EXTERNAL_STORAGE
            if (!Environment.isExternalStorageManager()) {
                Log.v(TAG, "Requesting MANAGE_EXTERNAL_STORAGE permission (Android 11+)...");
                Intent intent = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION);
                Uri uri = Uri.fromParts("package", getPackageName(), null);
                intent.setData(uri);
                startActivityForResult(intent, REQUEST_MANAGE_ALL_FILES);
            } else {
                Log.v(TAG, "Storage permission granted, launching VR activity...");
                launchVRActivity();
            }
        } else {
            // Android 10 and below use legacy storage permissions
            if (checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE) != PackageManager.PERMISSION_GRANTED) {
                Log.v(TAG, "Requesting WRITE_EXTERNAL_STORAGE permission (Android 10)...");
                requestPermissions(new String[]{
                    Manifest.permission.READ_EXTERNAL_STORAGE,
                    Manifest.permission.WRITE_EXTERNAL_STORAGE
                }, REQUEST_STORAGE_PERMISSION);
            } else {
                Log.v(TAG, "Storage permission granted, launching VR activity...");
                launchVRActivity();
            }
        }
    }

    private void launchVRActivity() {
        // Create game folder and copy assets if needed
        copyAssetsIfNeeded();

        // Only worth asking if both games are actually installed. This activity is deliberately
        // flat (no VR categories in the manifest) so the headset composites the dialog - with
        // them the compositor takes over and the dialog draws but stays invisible.
        if (hasMotsAssets()) {
            showGameChooser();
        } else {
            startGame(false);
        }
    }

    /**
     * Built as the activity's own content view rather than an AlertDialog: a dialog lives in a
     * separate window, and the headset's volumetric window only composites the task's main
     * window, so a dialog draws but is never visible.
     */
    private void showGameChooser() {
        Log.v(TAG, "MotS assets present, showing game chooser");

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setGravity(Gravity.CENTER);
        root.setBackgroundColor(0xFF101014);
        root.setPadding(80, 80, 80, 80);

        TextView title = new TextView(this);
        title.setText("JKDF2-XR");
        title.setTextColor(0xFFFFFFFF);
        title.setTextSize(TypedValue.COMPLEX_UNIT_SP, 34);
        title.setGravity(Gravity.CENTER);
        root.addView(title);

        TextView prompt = new TextView(this);
        prompt.setText("Which game would you like to play?");
        prompt.setTextColor(0xFFB0B0B8);
        prompt.setTextSize(TypedValue.COMPLEX_UNIT_SP, 20);
        prompt.setGravity(Gravity.CENTER);
        prompt.setPadding(0, 24, 0, 56);
        root.addView(prompt);

        root.addView(makeGameButton("Dark Forces II", false));
        root.addView(makeGameButton("Mysteries of the Sith", true));

        setContentView(root);
    }

    private Button makeGameButton(String label, final boolean bMots) {
        Button button = new Button(this);
        button.setText(label);
        button.setTextSize(TypedValue.COMPLEX_UNIT_SP, 24);
        button.setAllCaps(false);

        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(900, 140);
        lp.setMargins(0, 16, 0, 16);
        lp.gravity = Gravity.CENTER_HORIZONTAL;
        button.setLayoutParams(lp);

        button.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                startGame(bMots);
            }
        });
        return button;
    }

    private void startGame(boolean bMots) {
        setMotsFlag(bMots);
        startVRActivity();
    }

    private void startVRActivity() {
        // The engine keeps its configuration in globals initialised when the .so loads, and
        // Main_bMotsCompat is one of them. Relaunching only restarts the activity - the process
        // and its loaded library survive - so a previous MotS run would leave that flag set and
        // every subsequent launch would be MotS regardless of the choice. Kill the VR process
        // (it is its own :vr_process) so the library reloads and the globals start clean.
        killVRProcess();

        Intent intent = new Intent(this, VRActivity.class);
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
        startActivity(intent);
        finish();
    }

    private void killVRProcess() {
        ActivityManager manager = (ActivityManager)getSystemService(ACTIVITY_SERVICE);
        if (manager == null) return;

        List<ActivityManager.RunningAppProcessInfo> running = manager.getRunningAppProcesses();
        if (running == null) return;

        String vrProcessName = getPackageName() + ":vr_process";
        for (ActivityManager.RunningAppProcessInfo info : running) {
            if (vrProcessName.equals(info.processName)) {
                Log.v(TAG, "Killing stale VR process pid " + info.pid + " so engine globals reset");
                android.os.Process.killProcess(info.pid);
            }
        }
    }

    /**
     * The engine reads its arguments from commandline.txt, so the chooser drives the game by
     * managing the -motsCompat token in there. Anything else the user put in that file is
     * preserved - only that one token is added or removed.
     */
    private void setMotsFlag(boolean bMots) {
        File cmdFile = new File(GAME_FOLDER + "/" + COMMANDLINE_FILE);

        String existing = "";
        if (cmdFile.isFile()) {
            BufferedReader reader = null;
            try {
                reader = new BufferedReader(new FileReader(cmdFile));
                String line = reader.readLine();
                if (line != null) existing = line;
            } catch (IOException e) {
                Log.e(TAG, "Failed to read " + COMMANDLINE_FILE, e);
            } finally {
                try { if (reader != null) reader.close(); } catch (IOException ignored) {}
            }
        }

        StringBuilder args = new StringBuilder();
        for (String token : existing.trim().split("\\s+")) {
            if (token.isEmpty()) continue;
            if (token.equalsIgnoreCase("-motsCompat") || token.equalsIgnoreCase("/motsCompat")) continue;
            if (args.length() > 0) args.append(' ');
            args.append(token);
        }
        if (bMots) {
            if (args.length() > 0) args.append(' ');
            args.append("-motsCompat");
        }

        FileWriter writer = null;
        try {
            writer = new FileWriter(cmdFile, false);
            writer.write(args.toString());
            writer.write("\n");
            Log.v(TAG, COMMANDLINE_FILE + " -> '" + args.toString() + "'");
        } catch (IOException e) {
            Log.e(TAG, "Failed to write " + COMMANDLINE_FILE, e);
        } finally {
            try { if (writer != null) writer.close(); } catch (IOException ignored) {}
        }
    }

    /**
     * MotS is installed only if its two big archives are actually there - the mots folder itself
     * is created unconditionally above, so its presence proves nothing. Matched case-insensitively
     * because the engine reaches these through casepath() and users push them in either case.
     */
    private boolean hasMotsAssets() {
        File motsFolder = new File(MOTS_FOLDER);
        File episode = findChildIgnoreCase(motsFolder, "Episode");
        File resource = findChildIgnoreCase(motsFolder, "Resource");

        return findChildIgnoreCase(episode, "JKM.GOO") != null
            && findChildIgnoreCase(resource, "JKMRES.GOO") != null;
    }

    private static File findChildIgnoreCase(File parent, String name) {
        if (parent == null || !parent.isDirectory()) return null;

        File[] children = parent.listFiles();
        if (children == null) return null;

        for (File child : children) {
            if (child.getName().equalsIgnoreCase(name)) return child;
        }
        return null;
    }

    private void copyAssetsIfNeeded() {
        File gameFolder = new File(GAME_FOLDER);

        // Create the game folder if it doesn't exist
        if (!gameFolder.exists()) {
            Log.v(TAG, "Creating game folder: " + GAME_FOLDER);
            if (!gameFolder.mkdirs()) {
                Log.e(TAG, "Failed to create game folder");
                return;
            }
        }

        // Copy asset folders if they don't already exist
        copyAssetFolderIfNeeded("shaders", GAME_FOLDER + "/shaders");
        copyAssetFolderIfNeeded("resource", GAME_FOLDER + "/resource");
        copyAssetFolderIfNeeded("episode", GAME_FOLDER + "/episode");
        copyAssetFile(VR_WEAPON_OFFSETS_FILE, GAME_FOLDER + "/" + VR_WEAPON_OFFSETS_FILE);

        // Mysteries of the Sith runs out of a subfolder (-motsCompat in commandline.txt chdirs
        // into it), so it needs its own copy of the offsets. The file covers both games' bins.
        File motsFolder = new File(MOTS_FOLDER);
        if (!motsFolder.exists()) {
            motsFolder.mkdirs();
        }
        if (motsFolder.exists()) {
            // Altered: this used to copy only when the file was absent, so the MotS offsets were
            // frozen at whatever the first install wrote and no later APK could correct them -
            // the DF2 copy above has always been refreshed every launch. The bundled file is the
            // golden source for both games, so keep them consistent and refresh this one too.
            copyAssetFile(VR_WEAPON_OFFSETS_FILE, MOTS_FOLDER + "/" + VR_WEAPON_OFFSETS_FILE);
        }
    }

    private void copyAssetFolderIfNeeded(String assetFolder, String destPath) {
        File destFolder = new File(destPath);
        if (!destFolder.exists()) {
            Log.v(TAG, "Copying " + assetFolder + " to: " + destPath);
            copyAssetFolder(assetFolder, destPath);
        } else {
            Log.v(TAG, assetFolder + " folder already exists, skipping copy");
        }
    }

    private void copyAssetFolder(String assetFolder, String destPath) {
        AssetManager assetManager = getAssets();
        try {
            String[] files = assetManager.list(assetFolder);
            if (files == null || files.length == 0) {
                Log.e(TAG, "No files found in asset folder: " + assetFolder);
                return;
            }

            // Create destination folder
            File destDir = new File(destPath);
            if (!destDir.exists()) {
                destDir.mkdirs();
            }

            for (String filename : files) {
                String assetPath = assetFolder + "/" + filename;
                String destFilePath = destPath + "/" + filename;
                copyAssetFile(assetPath, destFilePath);
            }
            Log.v(TAG, "Copied " + files.length + " files from " + assetFolder);
        } catch (IOException e) {
            Log.e(TAG, "Failed to copy asset folder: " + assetFolder, e);
        }
    }

    private void copyAssetFile(String assetPath, String destPath) {
        AssetManager assetManager = getAssets();
        InputStream in = null;
        OutputStream out = null;
        try {
            in = assetManager.open(assetPath);
            out = new FileOutputStream(destPath);

            byte[] buffer = new byte[4096];
            int read;
            while ((read = in.read(buffer)) != -1) {
                out.write(buffer, 0, read);
            }
            Log.v(TAG, "Copied: " + assetPath + " -> " + destPath);
        } catch (IOException e) {
            Log.e(TAG, "Failed to copy asset: " + assetPath, e);
        } finally {
            try {
                if (in != null) in.close();
                if (out != null) out.close();
            } catch (IOException e) {
                // Ignore close errors
            }
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQUEST_MANAGE_ALL_FILES) {
            // Android 11+ MANAGE_EXTERNAL_STORAGE permission result
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                boolean granted = Environment.isExternalStorageManager();
                Log.v(TAG, "Returned from permission screen, permission granted: " + granted);
                if (granted) {
                    launchVRActivity();
                } else {
                    Log.v(TAG, "Permission not granted, exiting...");
                    finishAffinity();
                }
            }
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == REQUEST_STORAGE_PERMISSION) {
            // Android 10 and below storage permission result
            if (grantResults.length > 0 && grantResults[0] == PackageManager.PERMISSION_GRANTED) {
                Log.v(TAG, "Storage permission granted, launching VR activity...");
                launchVRActivity();
            } else {
                Log.v(TAG, "Storage permission denied, exiting...");
                finishAffinity();
            }
        }
    }
}
