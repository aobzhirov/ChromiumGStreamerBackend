// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.snackbar;

import android.os.Handler;
import android.view.View;
import android.view.View.OnClickListener;
import android.view.ViewGroup;

import org.chromium.base.VisibleForTesting;
import org.chromium.chrome.browser.device.DeviceClassManager;

/**
 * Manager for the snackbar showing at the bottom of activity. There should be only one
 * SnackbarManager and one snackbar in the activity.
 * <p/>
 * When action button is clicked, this manager will call {@link SnackbarController#onAction(Object)}
 * in corresponding listener, and show the next entry. Otherwise if no action is taken by user
 * during {@link #DEFAULT_SNACKBAR_DURATION_MS} milliseconds, it will call
 * {@link SnackbarController#onDismissNoAction(Object)}.
 */
public class SnackbarManager implements OnClickListener {

    /**
     * Interface that shows the ability to provide a snackbar manager. Activities implementing this
     * interface must call {@link SnackbarManager#onStart()} and {@link SnackbarManager#onStop()} in
     * corresponding lifecycle events.
     */
    public interface SnackbarManageable {
        /**
         * @return The snackbar manager that has a proper anchor view.
         */
        SnackbarManager getSnackbarManager();
    }

    /**
     * Controller that post entries to snackbar manager and interact with snackbar manager during
     * dismissal and action click event.
     */
    public interface SnackbarController {
        /**
         * Called when the user clicks the action button on the snackbar.
         * @param actionData Data object passed when showing this specific snackbar.
         */
        void onAction(Object actionData);

        /**
         * Called when the snackbar is dismissed by tiemout or UI enviroment change.
         * @param actionData Data object associated with the dismissed snackbar entry.
         */
        void onDismissNoAction(Object actionData);
    }

    /**
     * A class used to check if an {@link Object} meets certain criteria.
     */
    public interface ActionDataMatcher {
        /**
         * @return Whether the data stored in a {@link Snackbar} matches some criteria.
         */
        boolean match(Object data);
    }

    private static final int DEFAULT_SNACKBAR_DURATION_MS = 3000;
    private static final int ACCESSIBILITY_MODE_SNACKBAR_DURATION_MS = 6000;

    // Used instead of the constant so tests can override the value.
    private static int sSnackbarDurationMs = DEFAULT_SNACKBAR_DURATION_MS;
    private static int sAccessibilitySnackbarDurationMs = ACCESSIBILITY_MODE_SNACKBAR_DURATION_MS;

    private ViewGroup mParentView;
    private SnackbarView mView;
    private final Handler mUIThreadHandler;
    private SnackbarCollection mSnackbars = new SnackbarCollection();
    private boolean mActivityInForeground;
    private boolean mIsDisabledForTesting;
    private final Runnable mHideRunnable = new Runnable() {
        @Override
        public void run() {
            mSnackbars.removeCurrentDueToTimeout();
            updateView();
        }
    };

    /**
     * Constructs a SnackbarManager to show snackbars in the given window.
     * @param rootView The main view (e.g. android.R.id.content) of the embedding activity.
     */
    public SnackbarManager(ViewGroup rootView) {
        mParentView = rootView;
        mUIThreadHandler = new Handler();
    }

    /**
     * Notifies the snackbar manager that the activity is running in foreground now.
     */
    public void onStart() {
        mActivityInForeground = true;
    }

    /**
     * Notifies the snackbar manager that the activity has been pushed to background.
     */
    public void onStop() {
        mSnackbars.clear();
        updateView();
        mActivityInForeground = false;
    }

    /**
     * Shows a snackbar at the bottom of the screen, or above the keyboard if the keyboard is
     * visible.
     */
    public void showSnackbar(Snackbar snackbar) {
        if (!mActivityInForeground || mIsDisabledForTesting) return;
        mSnackbars.add(snackbar);
        updateView();
        mView.announceforAccessibility();
    }

    /**
     * Dismisses snackbars that are associated with the given {@link SnackbarController}.
     *
     * @param controller Only snackbars with this controller will be removed.
     */
    public void dismissSnackbars(SnackbarController controller) {
        if (mSnackbars.removeMatchingSnackbars(controller)) {
            updateView();
        }
    }

    /**
     * Dismisses snackbars that have a certain controller and action data.
     *
     * @param controller Only snackbars with this controller will be removed.
     * @param actionData Only snackbars whose action data is equal to actionData will be removed.
     */
    public void dismissSnackbars(SnackbarController controller, Object actionData) {
        if (mSnackbars.removeMatchingSnackbars(controller, actionData)) {
            updateView();
        }
    }

    /**
     * Dismisses snackbars that have action data that matches the given {@link ActionDataMatcher}.
     * @param controller Only snackbars created by this controller will be removed.
     * @param selector   The selector that selects a subset of snackbars.
     */
    public void dismissSnackbars(SnackbarController controller, ActionDataMatcher selector) {
        if (mSnackbars.removeMatchingSnackbars(controller, selector)) {
            updateView();
        }
    }

    /**
     * Handles click event for action button at end of snackbar.
     */
    @Override
    public void onClick(View v) {
        mSnackbars.removeCurrentDueToAction();
        updateView();
    }

    /**
     * @return Whether there is a snackbar on screen.
     */
    public boolean isShowing() {
        return mView != null && mView.isShowing();
    }

    /**
     * Updates the {@link SnackbarView} to reflect the value of mSnackbars.currentSnackbar(), which
     * may be null. This might show, change, or hide the view.
     */
    private void updateView() {
        if (!mActivityInForeground) return;
        Snackbar currentSnackbar = mSnackbars.getCurrent();
        if (currentSnackbar == null) {
            mUIThreadHandler.removeCallbacks(mHideRunnable);
            if (mView != null) {
                mView.dismiss();
                mView = null;
            }
        } else {
            boolean viewChanged = true;
            if (mView == null) {
                mView = new SnackbarView(mParentView, this, currentSnackbar);
                mView.show();
            } else {
                viewChanged = mView.update(currentSnackbar);
            }

            if (viewChanged) {
                int durationMs = getDuration(currentSnackbar);
                mUIThreadHandler.removeCallbacks(mHideRunnable);
                mUIThreadHandler.postDelayed(mHideRunnable, durationMs);
                mView.announceforAccessibility();
            }
        }

    }

    private int getDuration(Snackbar snackbar) {
        int durationMs = snackbar.getDuration();
        if (durationMs == 0) {
            durationMs = DeviceClassManager.isAccessibilityModeEnabled(mParentView.getContext())
                    ? sAccessibilitySnackbarDurationMs : sSnackbarDurationMs;
        }
        return durationMs;
    }

    /**
     * Disables the snackbar manager. This is only intented for testing purposes.
     */
    @VisibleForTesting
    public void disableForTesting() {
        mIsDisabledForTesting = true;
    }

    /**
     * Overrides the default snackbar duration with a custom value for testing.
     * @param durationMs The duration to use in ms.
     */
    @VisibleForTesting
    public static void setDurationForTesting(int durationMs) {
        sSnackbarDurationMs = durationMs;
        sAccessibilitySnackbarDurationMs = durationMs;
    }

    /**
     * @return The currently showing snackbar. For testing only.
     */
    @VisibleForTesting
    Snackbar getCurrentSnackbarForTesting() {
        return mSnackbars.getCurrent();
    }
}
