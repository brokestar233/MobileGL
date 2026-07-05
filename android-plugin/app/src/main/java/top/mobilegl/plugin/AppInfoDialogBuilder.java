package top.mobilegl.plugin;

import android.content.Context;
import android.content.pm.PackageManager;
import android.view.LayoutInflater;
import android.view.View;
import android.widget.TextView;

import androidx.annotation.NonNull;

import com.google.android.material.dialog.MaterialAlertDialogBuilder;

public final class AppInfoDialogBuilder extends MaterialAlertDialogBuilder {
    public AppInfoDialogBuilder(@NonNull Context context) {
        super(context);

        View view = LayoutInflater.from(context).inflate(R.layout.dialog_app_info, null);
        ((TextView) view.findViewById(R.id.info_version)).setText(resolveVersionName(context));

        setTitle(R.string.dialog_info);
        setView(view);
        setPositiveButton(android.R.string.ok, null);
    }

    private static String resolveVersionName(Context context) {
        try {
            return context.getPackageManager()
                    .getPackageInfo(context.getPackageName(), 0)
                    .versionName;
        } catch (PackageManager.NameNotFoundException ignored) {
            return "dev";
        }
    }
}
