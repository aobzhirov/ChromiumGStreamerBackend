// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.webapps;

import android.content.Context;
import android.content.SharedPreferences;
import android.os.AsyncTask;

import org.chromium.base.VisibleForTesting;
import org.chromium.base.annotations.CalledByNative;

import java.util.Collections;
import java.util.HashSet;
import java.util.Set;
import java.util.concurrent.TimeUnit;

/**
 * Keeps track of web apps which have created a SharedPreference file (through the used of the
 * WebappDataStorage class) which may need to be cleaned up in the future.
 *
 * It is NOT intended to be 100% accurate nor a comprehensive list of all installed web apps
 * because it is impossible to track when the user removes a web app from the Home screen and it
 * is similarily impossible to track pre-registry era web apps (this case is not a problem anyway
 * as these web apps have no external data to cleanup).
 */
public class WebappRegistry {

    static final String REGISTRY_FILE_NAME = "webapp_registry";
    static final String KEY_WEBAPP_SET = "webapp_set";
    static final String KEY_LAST_CLEANUP = "last_cleanup";

    /** Represents a period of 4 weeks in milliseconds */
    static final long FULL_CLEANUP_DURATION = TimeUnit.DAYS.toMillis(4L * 7L);

    /** Represents a period of 13 weeks in milliseconds */
    static final long WEBAPP_UNOPENED_CLEANUP_DURATION = TimeUnit.DAYS.toMillis(13L * 7L);

    /**
     * Called when a retrieval of the stored web apps occurs.
     */
    public interface FetchCallback {
        public void onWebappIdsRetrieved(Set<String> readObject);
    }

    /**
     * Registers the existence of a web app and creates the SharedPreference for it.
     * @param context  Context to open the registry with.
     * @param webappId The id of the web app to register.
     */
    public static void registerWebapp(final Context context, final String webappId,
            final String scope) {
        new AsyncTask<Void, Void, Void>() {
            @Override
            protected final Void doInBackground(Void... nothing) {
                SharedPreferences preferences = openSharedPreferences(context);
                Set<String> webapps = new HashSet<String>(getRegisteredWebappIds(preferences));
                boolean added = webapps.add(webappId);
                assert added;

                // Update the last used time, so we can guarantee that a web app which appears in
                // the registry will have a last used time != WebappDataStorage.LAST_USED_INVALID.
                WebappDataStorage storage = new WebappDataStorage(context, webappId);
                storage.setScope(scope);
                storage.updateLastUsedTime();
                preferences.edit().putStringSet(KEY_WEBAPP_SET, webapps).apply();
                return null;
            }
        }.execute();
    }

    /**
     * Asynchronously retrieves the list of web app IDs which this registry is aware of.
     * @param context  Context to open the registry with.
     * @param callback Called when the set has been retrieved. The set may be empty.
     */
    @VisibleForTesting
    public static void getRegisteredWebappIds(final Context context, final FetchCallback callback) {
        new AsyncTask<Void, Void, Set<String>>() {
            @Override
            protected final Set<String> doInBackground(Void... nothing) {
                return getRegisteredWebappIds(openSharedPreferences(context));
            }

            @Override
            protected final void onPostExecute(Set<String> result) {
                callback.onWebappIdsRetrieved(result);
            }
        }.execute();
    }

    /**
     * Deletes the data for all "old" web apps.
     * "Old" web apps have not been opened by the user in the last 3 months, or have had their last
     * used time set to 0 by the user clearing their history. Cleanup is run, at most, once a month.
     *
     * @param context     Context to open the registry with.
     * @param currentTime The current time which will be checked to decide if the task should be run
     *                    and if a web app should be cleaned up.
     */
    static void unregisterOldWebapps(final Context context, final long currentTime) {
        new AsyncTask<Void, Void, Void>() {
            @Override
            protected final Void doInBackground(Void... nothing) {
                SharedPreferences preferences = openSharedPreferences(context);
                long lastCleanup = preferences.getLong(KEY_LAST_CLEANUP, 0);
                if ((currentTime - lastCleanup) < FULL_CLEANUP_DURATION) return null;

                Set<String> currentWebapps = getRegisteredWebappIds(preferences);
                Set<String> retainedWebapps = new HashSet<String>(currentWebapps);
                for (String id : currentWebapps) {
                    long lastUsed = new WebappDataStorage(context, id).getLastUsedTime();
                    if ((currentTime - lastUsed) < WEBAPP_UNOPENED_CLEANUP_DURATION) continue;

                    WebappDataStorage.deleteDataForWebapp(context, id);
                    retainedWebapps.remove(id);
                }

                preferences.edit()
                        .putLong(KEY_LAST_CLEANUP, currentTime)
                        .putStringSet(KEY_WEBAPP_SET, retainedWebapps)
                        .apply();
                return null;
            }
        }.execute();
    }

    /**
     * Deletes the data of all web apps, as well as the registry tracking the web apps.
     */
    @VisibleForTesting
    static void unregisterAllWebapps(final Context context, final Runnable callback) {
        new AsyncTask<Void, Void, Void>() {
            @Override
            protected final Void doInBackground(Void... nothing) {
                SharedPreferences preferences = openSharedPreferences(context);
                for (String id : getRegisteredWebappIds(preferences)) {
                    WebappDataStorage.deleteDataForWebapp(context, id);
                }
                preferences.edit().clear().apply();
                return null;
            }

            @Override
            protected final void onPostExecute(Void nothing) {
                assert callback != null;
                callback.run();
            }
        }.execute();
    }

    @CalledByNative
    static void unregisterAllWebapps(Context context, final long callbackPointer) {
        unregisterAllWebapps(context, new Runnable() {
            @Override
            public void run() {
                nativeOnWebappsUnregistered(callbackPointer);
            }
        });
    }

    /**
     * Deletes the scope and sets the last used time to 0 for all web apps.
     */
    @VisibleForTesting
    static void clearWebappHistory(final Context context, final Runnable callback) {
        new AsyncTask<Void, Void, Void>() {
            @Override
            protected final Void doInBackground(Void... nothing) {
                SharedPreferences preferences = openSharedPreferences(context);
                for (String id : getRegisteredWebappIds(preferences)) {
                    WebappDataStorage.clearHistory(context, id);
                }
                return null;
            }

            @Override
            protected final void onPostExecute(Void nothing) {
                assert callback != null;
                callback.run();
            }
        }.execute();
    }

    @CalledByNative
    static void clearWebappHistory(Context context, final long callbackPointer) {
        clearWebappHistory(context, new Runnable() {
            @Override
            public void run() {
                nativeOnClearedWebappHistory(callbackPointer);
            }
        });
    }

    private static SharedPreferences openSharedPreferences(Context context) {
        return context.getSharedPreferences(REGISTRY_FILE_NAME, Context.MODE_PRIVATE);
    }

    private static Set<String> getRegisteredWebappIds(SharedPreferences preferences) {
        // Wrap with unmodifiableSet to ensure it's never modified. See crbug.com/568369.
        return Collections.unmodifiableSet(
                preferences.getStringSet(KEY_WEBAPP_SET, Collections.<String>emptySet()));
    }

    private WebappRegistry() {
    }

    private static native void nativeOnWebappsUnregistered(long callbackPointer);
    private static native void nativeOnClearedWebappHistory(long callbackPointer);
}
