package com.halo.decomp;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;

/**
 * The launcher icon's "Import maps" shortcut (LauncherActivity adds it):
 * the system's file picker for Halo PC maps (MapImporter), then back to
 * where the player was. In the game, the touch controls' Options has the
 * same import.
 */
public class MapImportActivity extends Activity {
    private boolean picking;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        picking = savedInstanceState != null && savedInstanceState.getBoolean("picking");
        if (!picking) {
            picking = MapImporter.choose(this);
            if (!picking)
                finish();
        }
    }

    @Override
    protected void onSaveInstanceState(Bundle state) {
        super.onSaveInstanceState(state);
        state.putBoolean("picking", picking);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != MapImporter.REQUEST_CODE)
            return;
        MapImporter.handleResult(this, resultCode, data, this::finish);
    }
}
