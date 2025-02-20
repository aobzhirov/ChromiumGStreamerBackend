// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.tabmodel;

import android.content.Context;
import android.test.suitebuilder.annotation.SmallTest;
import android.util.SparseArray;

import org.chromium.base.ThreadUtils;
import org.chromium.chrome.browser.TabState;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tabmodel.TabCreatorManager.TabCreator;
import org.chromium.chrome.browser.tabmodel.TabModel.TabLaunchType;
import org.chromium.chrome.browser.tabmodel.TabModel.TabSelectionType;
import org.chromium.chrome.browser.tabmodel.TabPersistentStore.TabPersistentStoreObserver;
import org.chromium.chrome.browser.tabmodel.TestTabModelDirectory.TabModelMetaDataInfo;
import org.chromium.chrome.test.util.browser.tabmodel.MockTabModelSelector;
import org.chromium.content.browser.test.NativeLibraryTestBase;
import org.chromium.content.browser.test.util.CallbackHelper;
import org.chromium.content_public.browser.LoadUrlParams;
import org.chromium.content_public.browser.WebContents;

import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.Callable;

/** Tests for the TabPersistentStore. */
public class TabPersistentStoreTest extends NativeLibraryTestBase {
    private static final int SELECTOR_INDEX = 0;

    private static class TabRestoredDetails {
        public int index;
        public int id;
        public String url;
        public boolean isStandardActiveIndex;
        public boolean isIncognitoActiveIndex;

        /** Store information about a Tab that's been restored. */
        TabRestoredDetails(int index, int id, String url,
                boolean isStandardActiveIndex, boolean isIncognitoActiveIndex) {
            this.index = index;
            this.id = id;
            this.url = url;
            this.isStandardActiveIndex = isStandardActiveIndex;
            this.isIncognitoActiveIndex = isIncognitoActiveIndex;
        }
    }

    private static class MockTabCreator extends TabCreator {
        public final SparseArray<TabState> created;
        public final CallbackHelper callback;

        private final boolean mIsIncognito;
        private final TabModelSelector mSelector;

        public int idOfFirstCreatedTab = Tab.INVALID_TAB_ID;

        public MockTabCreator(boolean incognito, TabModelSelector selector) {
            created = new SparseArray<TabState>();
            callback = new CallbackHelper();
            mIsIncognito = incognito;
            mSelector = selector;
        }

        @Override
        public boolean createsTabsAsynchronously() {
            return false;
        }

        @Override
        public Tab createNewTab(
                LoadUrlParams loadUrlParams, TabModel.TabLaunchType type, Tab parent) {
            Tab tab = Tab.createTabForLazyLoad(null, mIsIncognito, null,
                    TabLaunchType.FROM_LINK, Tab.INVALID_TAB_ID, loadUrlParams);
            mSelector.getModel(mIsIncognito).addTab(tab, TabModel.INVALID_TAB_INDEX, type);
            storeTabInfo(null, tab.getId());
            return tab;
        }

        @Override
        public Tab createFrozenTab(TabState state, int id, int index) {
            Tab tab = Tab.createFrozenTabFromState(
                    id, null, state.isIncognito(), null, state.parentId, state);
            mSelector.getModel(mIsIncognito).addTab(tab, index, TabLaunchType.FROM_RESTORE);
            storeTabInfo(state, id);
            return tab;
        }

        @Override
        public boolean createTabWithWebContents(Tab parent, WebContents webContents, int parentId,
                TabLaunchType type, String url) {
            return false;
        }

        @Override
        public Tab launchUrl(String url, TabModel.TabLaunchType type) {
            return null;
        }

        private void storeTabInfo(TabState state, int id) {
            if (created.size() == 0) idOfFirstCreatedTab = id;
            created.put(id, state);
            callback.notifyCalled();
        }
    }

    private static class MockTabCreatorManager implements TabCreatorManager {
        private MockTabCreator mRegularCreator;
        private MockTabCreator mIncognitoCreator;

        public MockTabCreatorManager(TabModelSelector selector) {
            mRegularCreator = new MockTabCreator(false, selector);
            mIncognitoCreator = new MockTabCreator(true, selector);
        }

        @Override
        public MockTabCreator getTabCreator(boolean incognito) {
            return incognito ? mIncognitoCreator : mRegularCreator;
        }
    }

    /**
     * Used when testing interactions of TabPersistentStore with real {@link TabModelImpl}s.
     */
    static class TestTabModelSelector extends TabModelSelectorBase
            implements TabModelDelegate {
        final TabPersistentStore mTabPersistentStore;
        final MockTabPersistentStoreObserver mTabPersistentStoreObserver;
        private final MockTabCreatorManager mTabCreatorManager;
        private final TabModelOrderController mTabModelOrderController;

        public TestTabModelSelector(Context context) throws Exception {
            mTabCreatorManager = new MockTabCreatorManager(this);
            mTabPersistentStoreObserver = new MockTabPersistentStoreObserver();
            mTabPersistentStore = new TabPersistentStore(
                    this, 0, context, mTabCreatorManager, mTabPersistentStoreObserver);
            mTabModelOrderController = new TabModelOrderController(this);

            Callable<TabModelImpl> callable = new Callable<TabModelImpl>() {
                @Override
                public TabModelImpl call() {
                    return new TabModelImpl(false,
                            mTabCreatorManager.getTabCreator(false),
                            mTabCreatorManager.getTabCreator(true),
                            null, mTabModelOrderController, null, mTabPersistentStore,
                            TestTabModelSelector.this, true);
                }
            };
            TabModelImpl regularTabModel = ThreadUtils.runOnUiThreadBlocking(callable);
            TabModel incognitoTabModel = new OffTheRecordTabModel(
                    new OffTheRecordTabModelImplCreator(mTabCreatorManager.getTabCreator(false),
                            mTabCreatorManager.getTabCreator(true),
                            null, mTabModelOrderController, null, mTabPersistentStore, this));
            initialize(false, regularTabModel, incognitoTabModel);
        }

        @Override
        public Tab openNewTab(
                LoadUrlParams loadUrlParams, TabLaunchType type, Tab parent, boolean incognito) {
            return mTabCreatorManager.getTabCreator(incognito).createNewTab(
                    loadUrlParams, type, parent);
        }

        @Override
        public void requestToShowTab(Tab tab, TabSelectionType type) {
        }

        @Override
        public boolean closeAllTabsRequest(boolean incognito) {
            TabModel model = getModel(incognito);
            while (model.getCount() > 0) {
                Tab tabToClose = model.getTabAt(0);
                model.closeTab(tabToClose, false, false, true);
            }
            return true;
        }

        @Override
        public boolean isInOverviewMode() {
            return false;
        }

        @Override
        public boolean isSessionRestoreInProgress() {
            return false;
        }
    }

    static class MockTabPersistentStoreObserver implements TabPersistentStoreObserver {
        public final CallbackHelper initializedCallback = new CallbackHelper();
        public final CallbackHelper detailsReadCallback = new CallbackHelper();
        public final CallbackHelper stateLoadedCallback = new CallbackHelper();
        public final CallbackHelper listWrittenCallback = new CallbackHelper();
        public final ArrayList<TabRestoredDetails> details = new ArrayList<TabRestoredDetails>();

        public int mTabCountAtStartup = -1;

        @Override
        public void onInitialized(int tabCountAtStartup) {
            mTabCountAtStartup = tabCountAtStartup;
            initializedCallback.notifyCalled();
        }

        @Override
        public void onDetailsRead(int index, int id, String url,
                boolean isStandardActiveIndex, boolean isIncognitoActiveIndex) {
            details.add(new TabRestoredDetails(
                    index, id, url, isStandardActiveIndex, isIncognitoActiveIndex));
            detailsReadCallback.notifyCalled();
        }

        @Override
        public void onStateLoaded(Context context) {
            stateLoadedCallback.notifyCalled();
        }

        @Override
        public void onMetadataSavedAsynchronously() {
            listWrittenCallback.notifyCalled();
        }
    }

    /** Class for mocking out the directory containing all of the TabState files. */
    private TestTabModelDirectory mMockDirectory;

    @Override
    public void setUp() throws Exception {
        super.setUp();
        loadNativeLibraryAndInitBrowserProcess();
        mMockDirectory = new TestTabModelDirectory(getInstrumentation().getTargetContext(),
                "TabPersistentStoreTest", Integer.toString(SELECTOR_INDEX));
        TabPersistentStore.setBaseStateDirectory(mMockDirectory.getBaseDirectory());
    }

    @Override
    public void tearDown() throws Exception {
        mMockDirectory.tearDown();
        super.tearDown();
    }

    @SmallTest
    public void testBasic() throws Exception {
        TabModelMetaDataInfo info = TestTabModelDirectory.TAB_MODEL_METADATA_V4;
        int numExpectedTabs = info.contents.length;

        mMockDirectory.writeTabModelFiles(info, true);

        // Set up the TabPersistentStore.
        Context context = getInstrumentation().getTargetContext();
        MockTabModelSelector mockSelector = new MockTabModelSelector(0, 0, null);
        MockTabCreatorManager mockManager = new MockTabCreatorManager(mockSelector);
        MockTabCreator regularCreator = mockManager.getTabCreator(false);
        MockTabPersistentStoreObserver mockObserver = new MockTabPersistentStoreObserver();
        TabPersistentStore store =
                new TabPersistentStore(mockSelector, 0, context, mockManager, mockObserver);

        // Make sure the metadata file loads properly and in order.
        store.loadState();
        mockObserver.initializedCallback.waitForCallback(0, 1);
        assertEquals(numExpectedTabs, mockObserver.mTabCountAtStartup);

        mockObserver.detailsReadCallback.waitForCallback(0, numExpectedTabs);
        assertEquals(numExpectedTabs, mockObserver.details.size());
        for (int i = 0; i < numExpectedTabs; i++) {
            TabRestoredDetails details = mockObserver.details.get(i);
            assertEquals(i, details.index);
            assertEquals(info.contents[i].tabId, details.id);
            assertEquals(info.contents[i].url, details.url);
            assertEquals(details.id == info.selectedTabId, details.isStandardActiveIndex);
            assertEquals(false, details.isIncognitoActiveIndex);
        }

        // Restore the TabStates.  The first Tab added should be the most recently selected tab.
        store.restoreTabs(true);
        regularCreator.callback.waitForCallback(0, 1);
        assertEquals(TestTabModelDirectory.TAB_MODEL_METADATA_V4.selectedTabId,
                regularCreator.idOfFirstCreatedTab);

        // Confirm that all the TabStates were read from storage (i.e. non-null).
        mockObserver.stateLoadedCallback.waitForCallback(0, 1);
        for (int i = 0; i < info.contents.length; i++) {
            int tabId = info.contents[i].tabId;
            assertNotNull(regularCreator.created.get(tabId));
        }
    }

    @SmallTest
    public void testInterruptedButStillRestoresAllTabs() throws Exception {
        TabModelMetaDataInfo info = TestTabModelDirectory.TAB_MODEL_METADATA_V4;
        int numExpectedTabs = info.contents.length;

        mMockDirectory.writeTabModelFiles(info, true);

        // Load up one TabPersistentStore, but don't load up the TabState files.  This prevents the
        // Tabs from being added to the TabModel.
        Context context = getInstrumentation().getTargetContext();
        MockTabModelSelector firstSelector = new MockTabModelSelector(0, 0, null);
        MockTabCreatorManager firstManager = new MockTabCreatorManager(firstSelector);
        MockTabPersistentStoreObserver firstObserver = new MockTabPersistentStoreObserver();
        final TabPersistentStore firstStore = new TabPersistentStore(
                firstSelector, 0, context, firstManager, firstObserver);
        firstStore.loadState();
        firstObserver.initializedCallback.waitForCallback(0, 1);
        assertEquals(numExpectedTabs, firstObserver.mTabCountAtStartup);
        firstObserver.detailsReadCallback.waitForCallback(0, numExpectedTabs);

        ThreadUtils.runOnUiThreadBlocking(new Runnable() {
            @Override
            public void run() {
                firstStore.saveState();
            }
        });

        // Prepare a second TabPersistentStore.
        MockTabModelSelector secondSelector = new MockTabModelSelector(0, 0, null);
        MockTabCreatorManager secondManager = new MockTabCreatorManager(secondSelector);
        MockTabCreator secondCreator = secondManager.getTabCreator(false);
        MockTabPersistentStoreObserver secondObserver = new MockTabPersistentStoreObserver();
        TabPersistentStore secondStore = new TabPersistentStore(
                secondSelector, 0, context, secondManager, secondObserver);

        // The second TabPersistentStore reads the file written by the first TabPersistentStore.
        // Make sure that all of the Tabs appear in the new one -- even though the new file was
        // written before the first TabPersistentStore loaded any TabState files and added them to
        // the TabModels.
        secondStore.loadState();
        secondObserver.initializedCallback.waitForCallback(0, 1);
        assertEquals(numExpectedTabs, secondObserver.mTabCountAtStartup);

        secondObserver.detailsReadCallback.waitForCallback(0, numExpectedTabs);
        assertEquals(numExpectedTabs, secondObserver.details.size());
        for (int i = 0; i < numExpectedTabs; i++) {
            TabRestoredDetails details = secondObserver.details.get(i);

            // Find the details for the current Tab ID.
            // TODO(dfalcantara): Revisit this bit when tab ordering is correctly preserved.
            TestTabModelDirectory.TabStateInfo currentInfo = null;
            for (int j = 0; j < numExpectedTabs && currentInfo == null; j++) {
                if (TestTabModelDirectory.TAB_MODEL_METADATA_V4.contents[j].tabId == details.id) {
                    currentInfo = TestTabModelDirectory.TAB_MODEL_METADATA_V4.contents[j];
                }
            }

            // TODO(dfalcantara): This won't be properly set until we have tab ordering preserved.
            // assertEquals(details.id == TestTabModelDirectory.TAB_MODEL_METADATA_V4_SELECTED_ID,
            //        details.isStandardActiveIndex);

            assertEquals(currentInfo.url, details.url);
            assertEquals(false, details.isIncognitoActiveIndex);
        }

        // Restore all of the TabStates.  Confirm that all the TabStates were read (i.e. non-null).
        secondStore.restoreTabs(true);
        secondObserver.stateLoadedCallback.waitForCallback(0, 1);
        for (int i = 0; i < numExpectedTabs; i++) {
            int tabId = TestTabModelDirectory.TAB_MODEL_METADATA_V4.contents[i].tabId;
            assertNotNull(secondCreator.created.get(tabId));
        }
    }

    @SmallTest
    public void testMissingTabStateButStillRestoresTab() throws Exception {
        TabModelMetaDataInfo info = TestTabModelDirectory.TAB_MODEL_METADATA_V5;
        int numExpectedTabs = info.contents.length;

        // Write out info for all but the third tab (arbitrarily chosen).
        mMockDirectory.writeTabModelFiles(info, false);
        for (int i = 0; i < info.contents.length; i++) {
            if (i != 2) mMockDirectory.writeTabStateFile(info.contents[i]);
        }

        // Initialize the classes.
        Context context = getInstrumentation().getTargetContext();
        MockTabModelSelector mockSelector = new MockTabModelSelector(0, 0, null);
        MockTabCreatorManager mockManager = new MockTabCreatorManager(mockSelector);
        MockTabPersistentStoreObserver mockObserver = new MockTabPersistentStoreObserver();
        TabPersistentStore store =
                new TabPersistentStore(mockSelector, 0, context, mockManager, mockObserver);

        // Make sure the metadata file loads properly and in order.
        store.loadState();
        mockObserver.initializedCallback.waitForCallback(0, 1);
        assertEquals(numExpectedTabs, mockObserver.mTabCountAtStartup);

        mockObserver.detailsReadCallback.waitForCallback(0, numExpectedTabs);
        assertEquals(numExpectedTabs, mockObserver.details.size());
        for (int i = 0; i < numExpectedTabs; i++) {
            TabRestoredDetails details = mockObserver.details.get(i);
            assertEquals(i, details.index);
            assertEquals(info.contents[i].tabId, details.id);
            assertEquals(info.contents[i].url, details.url);
            assertEquals(details.id == info.selectedTabId, details.isStandardActiveIndex);
            assertEquals(false, details.isIncognitoActiveIndex);
        }

        // Restore the TabStates, and confirm that the correct number of tabs is created even with
        // one missing.
        store.restoreTabs(true);
        mockObserver.stateLoadedCallback.waitForCallback(0, 1);
        assertEquals(numExpectedTabs, mockSelector.getModel(false).getCount());
        assertEquals(0, mockSelector.getModel(true).getCount());
    }

    @SmallTest
    public void testRestoresTabWithMissingTabStateWhileIgnoringIncognitoTab() throws Exception {
        TabModelMetaDataInfo info = TestTabModelDirectory.TAB_MODEL_METADATA_V5_WITH_INCOGNITO;
        int numExpectedTabs = info.contents.length;

        // Write out info for all but the third tab (arbitrarily chosen).
        mMockDirectory.writeTabModelFiles(info, false);
        for (int i = 0; i < info.contents.length; i++) {
            if (i != 2) mMockDirectory.writeTabStateFile(info.contents[i]);
        }

        // Initialize the classes.
        MockTabModelSelector mockSelector = new MockTabModelSelector(0, 0, null);
        MockTabCreatorManager mockManager = new MockTabCreatorManager(mockSelector);
        MockTabPersistentStoreObserver mockObserver = new MockTabPersistentStoreObserver();
        TabPersistentStore store = new TabPersistentStore(mockSelector, 0,
                getInstrumentation().getTargetContext(), mockManager, mockObserver);

        // Load the TabModel metadata.
        store.loadState();
        mockObserver.initializedCallback.waitForCallback(0, 1);
        assertEquals(numExpectedTabs, mockObserver.mTabCountAtStartup);
        mockObserver.detailsReadCallback.waitForCallback(0, numExpectedTabs);
        assertEquals(numExpectedTabs, mockObserver.details.size());

        // TODO(dfalcantara): Expand MockTabModel* to support Incognito Tab decryption.

        // Restore the TabStates, and confirm that the correct number of tabs is created even with
        // one missing.  No Incognito tabs should be created because the TabState is missing.
        store.restoreTabs(true);
        mockObserver.stateLoadedCallback.waitForCallback(0, 1);
        assertEquals(info.numRegularTabs, mockSelector.getModel(false).getCount());
        assertEquals(0, mockSelector.getModel(true).getCount());
    }

    /**
     * Tests that a real {@link TabModelImpl} will use the {@link TabPersistentStore} to write out
     * an updated metadata file when a closure is undone.
     */
    @SmallTest
    public void testUndoSingleTabClosureWritesTabListFile() throws Exception {
        TabModelMetaDataInfo info = TestTabModelDirectory.TAB_MODEL_METADATA_V5_NO_M18;
        mMockDirectory.writeTabModelFiles(info, true);

        // Start closing one tab, then undo it.  Make sure the tab list metadata is saved out.
        TestTabModelSelector selector = createAndRestoreRealTabModelImpls(info);
        MockTabPersistentStoreObserver mockObserver = selector.mTabPersistentStoreObserver;
        final TabModel regularModel = selector.getModel(false);
        int currentWrittenCallbackCount = mockObserver.listWrittenCallback.getCallCount();
        ThreadUtils.runOnUiThreadBlocking(new Runnable() {
            @Override
            public void run() {
                Tab tabToClose = regularModel.getTabAt(2);
                regularModel.closeTab(tabToClose, false, false, true);
                regularModel.cancelTabClosure(tabToClose.getId());
            }
        });
        mockObserver.listWrittenCallback.waitForCallback(currentWrittenCallbackCount, 1);
    }

    /**
     * Tests that a real {@link TabModelImpl} will use the {@link TabPersistentStore} to write out
     * valid a valid metadata file and the TabModel's associated TabStates after closing and
     * canceling the closure of all the tabs simultaneously.
     */
    @SmallTest
    public void testUndoCloseAllTabsWritesTabListFile() throws Exception {
        final TabModelMetaDataInfo info = TestTabModelDirectory.TAB_MODEL_METADATA_V5_NO_M18;
        mMockDirectory.writeTabModelFiles(info, true);

        for (int i = 0; i < 2; i++) {
            final TestTabModelSelector selector = createAndRestoreRealTabModelImpls(info);

            // Undoing tab closures one-by-one results in the first tab always being selected after
            // the initial restoration.
            if (i == 0) {
                assertEquals(info.selectedTabId, selector.getCurrentTab().getId());
            } else {
                assertEquals(info.contents[0].tabId, selector.getCurrentTab().getId());
            }

            ThreadUtils.runOnUiThreadBlocking(new Runnable() {
                @Override
                public void run() {
                    closeAllTabsThenUndo(selector, info);

                    // Synchronously save the data out to simulate minimizing Chrome.
                    selector.mTabPersistentStore.saveState();
                }
            });

            // Load up each TabState and confirm that values are still correct.
            for (int j = 0; j < info.numRegularTabs; j++) {
                TabState currentState = TabState.restoreTabState(
                        mMockDirectory.getDataDirectory(), info.contents[j].tabId);
                assertEquals(info.contents[j].title, currentState.getDisplayTitleFromState());
                assertEquals(info.contents[j].url, currentState.getVirtualUrlFromState());
            }
        }
    }

    private TestTabModelSelector createAndRestoreRealTabModelImpls(TabModelMetaDataInfo info)
            throws Exception {
        Context context = getInstrumentation().getTargetContext();
        TestTabModelSelector selector = new TestTabModelSelector(context);

        TabPersistentStore store = selector.mTabPersistentStore;
        MockTabPersistentStoreObserver mockObserver = selector.mTabPersistentStoreObserver;

        // Load up the TabModel metadata.
        int numExpectedTabs = info.numRegularTabs + info.numIncognitoTabs;
        store.loadState();
        mockObserver.initializedCallback.waitForCallback(0, 1);
        assertEquals(numExpectedTabs, mockObserver.mTabCountAtStartup);
        mockObserver.detailsReadCallback.waitForCallback(0, info.contents.length);
        assertEquals(numExpectedTabs, mockObserver.details.size());

        // Restore the TabStates, check that things were restored correctly, in the right tab order.
        store.restoreTabs(true);
        mockObserver.stateLoadedCallback.waitForCallback(0, 1);
        assertEquals(info.numRegularTabs, selector.getModel(false).getCount());
        assertEquals(info.numIncognitoTabs, selector.getModel(true).getCount());
        for (int i = 0; i < numExpectedTabs; i++) {
            assertEquals(info.contents[i].tabId, selector.getModel(false).getTabAt(i).getId());
        }

        return selector;
    }

    /**
     * Close all Tabs in the regular TabModel, then undo the operation to restore the Tabs.
     * This simulates how {@link StripLayoutHelper} and {@link UndoBarController} would close
     * all of a {@link TabModel}'s tabs on tablets, which is different from how the
     * {@link OverviewListLayout} would do it on phones.
     */
    private void closeAllTabsThenUndo(TabModelSelector selector, TabModelMetaDataInfo info) {
        // Close all the tabs, using an Observer to determine what is actually being closed.
        TabModel regularModel = selector.getModel(false);
        final List<Integer> closedTabIds = new ArrayList<Integer>();
        TabModelObserver closeObserver = new EmptyTabModelObserver() {
            @Override
            public void allTabsPendingClosure(List<Integer> tabIds) {
                for (Integer id : tabIds) closedTabIds.add(id);
            }
        };
        regularModel.addObserver(closeObserver);
        regularModel.closeAllTabs(false, false);
        assertEquals(info.numRegularTabs, closedTabIds.size());

        // Cancel closing each tab.
        for (Integer id : closedTabIds) regularModel.cancelTabClosure(id);
        assertEquals(info.numRegularTabs, regularModel.getCount());
    }
}
