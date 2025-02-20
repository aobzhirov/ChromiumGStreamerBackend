// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.content.browser;

import android.Manifest.permission;
import android.content.pm.PackageManager;

import org.chromium.base.annotations.CalledByNative;
import org.chromium.base.annotations.JNINamespace;
import org.chromium.ui.base.WindowAndroid.PermissionCallback;

/**
 * Java counterpart of android DownloadController.
 *
 * Its a singleton class instantiated by the C++ DownloadController.
 */
@JNINamespace("content")
public class DownloadController {
    private static final String LOGTAG = "DownloadController";
    private static final DownloadController sInstance = new DownloadController();

    /**
     * Class for notifying the application that download has completed.
     */
    public interface DownloadNotificationService {
        /**
         * Notify the host application that a download is finished.
         * @param downloadInfo Information about the completed download.
         */
        void onDownloadCompleted(final DownloadInfo downloadInfo);

        /**
         * Notify the host application that a download is in progress.
         * @param downloadInfo Information about the in-progress download.
         */
        void onDownloadUpdated(final DownloadInfo downloadInfo);

        /**
         * Notify the host application that a download is cancelled.
         * @param downloadId Id of the download.
         */
        void onDownloadCancelled(final DownloadInfo downloadInfo);

        /**
         * Notify the host application that a download is interrupted.
         * @param downloadInfo Information about the completed download.
         * @param isAutoResumable Download can be auto resumed when network becomes available.
         */
        void onDownloadInterrupted(final DownloadInfo downloadInfo, boolean isAutoResumable);
    }

    private static DownloadNotificationService sDownloadNotificationService;

    @CalledByNative
    public static DownloadController getInstance() {
        return sInstance;
    }

    private DownloadController() {
        nativeInit();
    }

    private static ContentViewDownloadDelegate downloadDelegateFromView(ContentViewCore view) {
        return view.getDownloadDelegate();
    }

    public static void setDownloadNotificationService(DownloadNotificationService service) {
        sDownloadNotificationService = service;
    }

    /**
     * Notifies the download delegate of a new GET download and passes all the information
     * needed to download the file.
     *
     * The download delegate is expected to handle the download.
     */
    @CalledByNative
    private void newHttpGetDownload(ContentViewCore view, String url,
            String userAgent, String contentDisposition, String mimeType,
            String cookie, String referer, boolean hasUserGesture,
            String filename, long contentLength) {
        ContentViewDownloadDelegate downloadDelegate = downloadDelegateFromView(view);

        if (downloadDelegate == null) return;
        DownloadInfo downloadInfo = new DownloadInfo.Builder()
                .setUrl(url)
                .setUserAgent(userAgent)
                .setContentDisposition(contentDisposition)
                .setMimeType(mimeType)
                .setCookie(cookie)
                .setReferer(referer)
                .setHasUserGesture(hasUserGesture)
                .setFileName(filename)
                .setContentLength(contentLength)
                .setIsGETRequest(true)
                .build();
        downloadDelegate.requestHttpGetDownload(downloadInfo);
    }

    /**
     * Notifies the download delegate that a new download has started. This can
     * be either a POST download or a GET download with authentication.
     * @param view ContentViewCore associated with the download item.
     * @param filename File name of the downloaded file.
     * @param mimeType Mime of the downloaded item.
     */
    @CalledByNative
    private void onDownloadStarted(ContentViewCore view, String filename, String mimeType) {
        ContentViewDownloadDelegate downloadDelegate = downloadDelegateFromView(view);

        if (downloadDelegate != null) {
            downloadDelegate.onDownloadStarted(filename, mimeType);
        }
    }

    /**
     * Notifies the download delegate that a download completed and passes along info about the
     * download. This can be either a POST download or a GET download with authentication.
     * TODO(qinmin): Generate the notificationId in the java side, the native downloadId is going
     * to be deprecated.
     */
    @CalledByNative
    private void onDownloadCompleted(String url, String mimeType, String filename, String path,
            long contentLength, int notificationId, String downloadGuid, String originalUrl,
            String refererUrl, boolean hasUserGesture) {
        if (sDownloadNotificationService == null) return;
        DownloadInfo downloadInfo = new DownloadInfo.Builder()
                .setUrl(url)
                .setMimeType(mimeType)
                .setFileName(filename)
                .setFilePath(path)
                .setContentLength(contentLength)
                .setDescription(filename)
                .setNotificationId(notificationId)
                .setDownloadGuid(downloadGuid)
                .setOriginalUrl(originalUrl)
                .setReferer(refererUrl)
                .setHasUserGesture(hasUserGesture)
                .build();
        sDownloadNotificationService.onDownloadCompleted(downloadInfo);
    }

    /**
     * Notifies the download delegate that a download completed and passes along info about the
     * download. This can be either a POST download or a GET download with authentication.
     */
    @CalledByNative
    private void onDownloadInterrupted(String url, String mimeType, String filename, String path,
            long contentLength, int notificationId, String downloadGuid, boolean isResumable,
            boolean isAutoResumable) {
        if (sDownloadNotificationService == null) return;
        DownloadInfo downloadInfo = new DownloadInfo.Builder()
                .setUrl(url)
                .setMimeType(mimeType)
                .setFileName(filename)
                .setFilePath(path)
                .setContentLength(contentLength)
                .setDescription(filename)
                .setNotificationId(notificationId)
                .setDownloadGuid(downloadGuid)
                .setIsResumable(isResumable)
                .build();
        sDownloadNotificationService.onDownloadInterrupted(downloadInfo, isAutoResumable);
    }

    /**
     * Called when a download was cancelled.
     * @param notificationId Notification Id of the download item.
     * @param downloadGuid GUID of the download item.
     * TODO(qinmin): Generate the notificationId in the java side, the native downloadId is going
     * to be deprecated.
     */
    @CalledByNative
    private void onDownloadCancelled(int notificationId, String downloadGuid) {
        if (sDownloadNotificationService == null) return;
        DownloadInfo downloadInfo = new DownloadInfo.Builder()
                .setNotificationId(notificationId)
                .setDownloadGuid(downloadGuid)
                .build();
        sDownloadNotificationService.onDownloadCancelled(downloadInfo);
    }

    /**
     * Notifies the download delegate about progress of a download. Downloads that use Chrome
     * network stack use custom notification to display the progress of downloads.
     * TODO(qinmin): Generate the notificationId in the java side, the native downloadId is going
     * to be deprecated.
     */
    @CalledByNative
    private void onDownloadUpdated(String url, String mimeType, String filename, String path,
            long contentLength, int notificationId, String downloadGuid, int percentCompleted,
            long timeRemainingInMs, boolean hasUserGesture, boolean isPaused, boolean isResumable) {
        if (sDownloadNotificationService == null) return;
        DownloadInfo downloadInfo = new DownloadInfo.Builder()
                .setUrl(url)
                .setMimeType(mimeType)
                .setFileName(filename)
                .setFilePath(path)
                .setContentLength(contentLength)
                .setDescription(filename)
                .setNotificationId(notificationId)
                .setDownloadGuid(downloadGuid)
                .setPercentCompleted(percentCompleted)
                .setTimeRemainingInMillis(timeRemainingInMs)
                .setHasUserGesture(hasUserGesture)
                .setIsPaused(isPaused)
                .setIsResumable(isResumable)
                .build();
        sDownloadNotificationService.onDownloadUpdated(downloadInfo);
    }

    /**
     * Notifies the download delegate that a dangerous download started.
     */
    @CalledByNative
    private void onDangerousDownload(ContentViewCore view, String filename, String downloadGuid) {
        ContentViewDownloadDelegate downloadDelegate = downloadDelegateFromView(view);
        if (downloadDelegate != null) {
            downloadDelegate.onDangerousDownload(filename, downloadGuid);
        }
    }

    /**
     * Returns whether file access is allowed.
     *
     * @param view The ContentViewCore to access file system.
     * @return true if allowed, or false otherwise.
     */
    @CalledByNative
    private boolean hasFileAccess(ContentViewCore view) {
        return view.getWindowAndroid().hasPermission(permission.WRITE_EXTERNAL_STORAGE);
    }

    /**
     * Called to prompt user with the file access permission.
     *
     * @param view The ContentViewCore to access file system.
     * @param callbackId The native callback function pointer.
     */
    @CalledByNative
    private void requestFileAccess(final ContentViewCore view, final long callbackId) {
        ContentViewDownloadDelegate downloadDelegate = downloadDelegateFromView(view);
        if (downloadDelegate != null) {
            downloadDelegate.requestFileAccess(callbackId);
        } else {
            PermissionCallback permissionCallback = new PermissionCallback() {
                @Override
                public void onRequestPermissionsResult(String[] permissions, int[] grantResults) {
                    onRequestFileAccessResult(callbackId, grantResults.length > 0
                            && grantResults[0] == PackageManager.PERMISSION_GRANTED);
                }
            };
            view.getWindowAndroid().requestPermissions(
                    new String[] {android.Manifest.permission.WRITE_EXTERNAL_STORAGE},
                    permissionCallback);
        }
    }

    /**
     * Notify the results of a file access request.
     * @param callbackId The ID of the callback.
     * @param granted Whether access was granted.
     */
    public void onRequestFileAccessResult(long callbackId, boolean granted) {
        nativeOnRequestFileAccessResult(callbackId, granted);
    }

    // native methods
    private native void nativeInit();
    private native void nativeOnRequestFileAccessResult(long callbackId, boolean granted);
}
