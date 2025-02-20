/*
 * Copyright (C) 2009 280 North Inc. All Rights Reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * @constructor
 * @extends {WebInspector.DataGridNode}
 * @param {!ProfilerAgent.CPUProfileNode} profileNode
 * @param {!WebInspector.TopDownProfileDataGridTree} owningTree
 * @param {boolean} hasChildren
 */
WebInspector.ProfileDataGridNode = function(profileNode, owningTree, hasChildren)
{
    this.profileNode = profileNode;

    WebInspector.DataGridNode.call(this, null, hasChildren);

    this.tree = owningTree;

    this.childrenByCallUID = {};
    this.lastComparator = null;

    this.callUID = profileNode.callUID;
    this.selfTime = profileNode.selfTime;
    this.totalTime = profileNode.totalTime;
    this.functionName = WebInspector.beautifyFunctionName(profileNode.functionName);
    this._deoptReason = (!profileNode.deoptReason || profileNode.deoptReason === "no reason") ? "" : profileNode.deoptReason;
    this.url = profileNode.url;
}

WebInspector.ProfileDataGridNode.prototype = {
    /**
     * @override
     * @param {string} columnId
     * @return {!Element}
     */
    createCell: function(columnId)
    {
        /**
         * @param {number} value
         * @param {number} percent
         * @return {!Element}
         */
        function createValueCell(value, percent)
        {
            var cell = createElementWithClass("td", "numeric-column");
            var div = cell.createChild("div", "profile-multiple-values");
            div.createChild("span").textContent = WebInspector.UIString("%.1f\u2009ms", value);
            div.createChild("span", "percent-column").textContent = percent >= 0 ? WebInspector.UIString("%.2f\u2009%%", percent) : "";
            return cell;
        }

        var cell;
        var isIdleNode = this.profileNode === this.tree.profileView.profile.idleNode;
        switch (columnId) {
        case "self":
            cell = createValueCell(this.selfTime, isIdleNode ? -1 : this.selfPercent);
            cell.classList.toggle("highlight", this._searchMatchedSelfColumn);
            break;

        case "total":
            cell = createValueCell(this.totalTime, isIdleNode ? -1 : this.totalPercent);
            cell.classList.toggle("highlight", this._searchMatchedTotalColumn);
            break;

        case "function":
            cell = this.createTD(columnId);
            cell.classList.toggle("highlight", this._searchMatchedFunctionColumn);
            if (this._deoptReason) {
                cell.classList.add("not-optimized");
                cell.createChild("span", "profile-warn-marker").title = WebInspector.UIString("Not optimized: %s", this._deoptReason);
            }
            cell.createTextChild(this.functionName);
            if (this.profileNode.scriptId === "0")
                break;
            var target = this.tree.profileView.target();
            var callFrame = /** @type {!RuntimeAgent.CallFrame} */ (this.profileNode);
            var urlElement = this.tree.profileView._linkifier.linkifyConsoleCallFrame(target, callFrame, "profile-node-file");
            urlElement.style.maxWidth = "75%";
            cell.appendChild(urlElement);
            break;

        default:
            cell = WebInspector.DataGridNode.prototype.createCell.call(this, columnId);
            break;
        }
        return cell;
    },

    select: function(supressSelectedEvent)
    {
        WebInspector.DataGridNode.prototype.select.call(this, supressSelectedEvent);
        this.tree.profileView._dataGridNodeSelected(this);
    },

    deselect: function(supressDeselectedEvent)
    {
        WebInspector.DataGridNode.prototype.deselect.call(this, supressDeselectedEvent);
        this.tree.profileView._dataGridNodeDeselected(this);
    },

    /**
     * @param {function(!T, !T)} comparator
     * @param {boolean} force
     * @template T
     */
    sort: function(comparator, force)
    {
        var gridNodeGroups = [[this]];

        for (var gridNodeGroupIndex = 0; gridNodeGroupIndex < gridNodeGroups.length; ++gridNodeGroupIndex) {
            var gridNodes = gridNodeGroups[gridNodeGroupIndex];
            var count = gridNodes.length;

            for (var index = 0; index < count; ++index) {
                var gridNode = gridNodes[index];

                // If the grid node is collapsed, then don't sort children (save operation for later).
                // If the grid node has the same sorting as previously, then there is no point in sorting it again.
                if (!force && (!gridNode.expanded || gridNode.lastComparator === comparator)) {
                    if (gridNode.children.length)
                        gridNode.shouldRefreshChildren = true;
                    continue;
                }

                gridNode.lastComparator = comparator;

                var children = gridNode.children;
                var childCount = children.length;

                if (childCount) {
                    children.sort(comparator);

                    for (var childIndex = 0; childIndex < childCount; ++childIndex)
                        children[childIndex].recalculateSiblings(childIndex);

                    gridNodeGroups.push(children);
                }
            }
        }
    },

    /**
     * @override
     * @param {!WebInspector.DataGridNode} profileDataGridNode
     * @param {number} index
     */
    insertChild: function(profileDataGridNode, index)
    {
        WebInspector.DataGridNode.prototype.insertChild.call(this, profileDataGridNode, index);

        this.childrenByCallUID[profileDataGridNode.callUID] = /** @type {!WebInspector.ProfileDataGridNode} */ (profileDataGridNode);
    },

    /**
     * @override
     * @param {!WebInspector.DataGridNode} profileDataGridNode
     */
    removeChild: function(profileDataGridNode)
    {
        WebInspector.DataGridNode.prototype.removeChild.call(this, profileDataGridNode);

        delete this.childrenByCallUID[/** @type {!WebInspector.ProfileDataGridNode} */ (profileDataGridNode).callUID];
    },

    removeChildren: function()
    {
        WebInspector.DataGridNode.prototype.removeChildren.call(this);

        this.childrenByCallUID = {};
    },

    /**
     * @param {!WebInspector.ProfileDataGridNode} node
     * @return {?WebInspector.ProfileDataGridNode}
     */
    findChild: function(node)
    {
        if (!node)
            return null;
        return this.childrenByCallUID[node.callUID];
    },

    get selfPercent()
    {
        return this.selfTime / this.tree.totalTime * 100.0;
    },

    get totalPercent()
    {
        return this.totalTime / this.tree.totalTime * 100.0;
    },

    populate: function()
    {
        WebInspector.ProfileDataGridNode.populate(this);
    },

    /**
     * @protected
     */
    populateChildren: function()
    {
    },

    // When focusing and collapsing we modify lots of nodes in the tree.
    // This allows us to restore them all to their original state when we revert.

    save: function()
    {
        if (this._savedChildren)
            return;

        this._savedSelfTime = this.selfTime;
        this._savedTotalTime = this.totalTime;

        this._savedChildren = this.children.slice();
    },

    /**
     * When focusing and collapsing we modify lots of nodes in the tree.
     * This allows us to restore them all to their original state when we revert.
     * @protected
     */
    restore: function()
    {
        if (!this._savedChildren)
            return;

        this.selfTime = this._savedSelfTime;
        this.totalTime = this._savedTotalTime;

        this.removeChildren();

        var children = this._savedChildren;
        var count = children.length;

        for (var index = 0; index < count; ++index) {
            children[index].restore();
            this.appendChild(children[index]);
        }
    },

    /**
     * @param {!WebInspector.ProfileDataGridNode} child
     * @param {boolean} shouldAbsorb
     */
    merge: function(child, shouldAbsorb)
    {
        WebInspector.ProfileDataGridNode.merge(this, child, shouldAbsorb);
    },

    __proto__: WebInspector.DataGridNode.prototype
}

/**
 * @param {!WebInspector.ProfileDataGridNode|!WebInspector.ProfileDataGridTree} container
 * @param {!WebInspector.ProfileDataGridNode} child
 * @param {boolean} shouldAbsorb
 */
WebInspector.ProfileDataGridNode.merge = function(container, child, shouldAbsorb)
{
    container.selfTime += child.selfTime;

    if (!shouldAbsorb)
        container.totalTime += child.totalTime;

    var children = container.children.slice();

    container.removeChildren();

    var count = children.length;

    for (var index = 0; index < count; ++index) {
        if (!shouldAbsorb || children[index] !== child)
            container.appendChild(children[index]);
    }

    children = child.children.slice();
    count = children.length;

    for (var index = 0; index < count; ++index) {
        var orphanedChild = children[index];
        var existingChild = container.childrenByCallUID[orphanedChild.callUID];

        if (existingChild)
            existingChild.merge(orphanedChild, false);
        else
            container.appendChild(orphanedChild);
    }
}

/**
 * @param {!WebInspector.ProfileDataGridNode|!WebInspector.ProfileDataGridTree} container
 */
WebInspector.ProfileDataGridNode.populate = function(container)
{
    if (container._populated)
        return;
    container._populated = true;

    container.populateChildren();

    var currentComparator = container.tree.lastComparator;

    if (currentComparator)
        container.sort(currentComparator, true);
}

/**
 * @constructor
 * @implements {WebInspector.CPUProfileView.Searchable}
 * @param {!WebInspector.CPUProfileView} profileView
 * @param {!ProfilerAgent.CPUProfileNode} rootProfileNode
 */
WebInspector.ProfileDataGridTree = function(profileView, rootProfileNode)
{
    this.tree = this;
    this.children = [];

    this.profileView = profileView;

    var idleNode = profileView.profile.idleNode;
    this.totalTime = rootProfileNode.totalTime - (idleNode ? idleNode.totalTime : 0);
    this.lastComparator = null;

    this.childrenByCallUID = {};
}

WebInspector.ProfileDataGridTree.prototype = {
    get expanded()
    {
        return true;
    },

    appendChild: function(child)
    {
        this.insertChild(child, this.children.length);
    },

    insertChild: function(child, index)
    {
        this.children.splice(index, 0, child);
        this.childrenByCallUID[child.callUID] = child;
    },

    removeChildren: function()
    {
        this.children = [];
        this.childrenByCallUID = {};
    },

    populateChildren: function()
    {
    },

    findChild: WebInspector.ProfileDataGridNode.prototype.findChild,
    sort: WebInspector.ProfileDataGridNode.prototype.sort,

    /**
     * @protected
     */
    save: function()
    {
        if (this._savedChildren)
            return;

        this._savedTotalTime = this.totalTime;
        this._savedChildren = this.children.slice();
    },

    restore: function()
    {
        if (!this._savedChildren)
            return;

        this.children = this._savedChildren;
        this.totalTime = this._savedTotalTime;

        var children = this.children;
        var count = children.length;

        for (var index = 0; index < count; ++index)
            children[index].restore();

        this._savedChildren = null;
    },

    /**
     * @param {!WebInspector.SearchableView.SearchConfig} searchConfig
     * @return {?function(!WebInspector.ProfileDataGridNode):boolean}
     */
    _matchFunction: function(searchConfig)
    {
        var query = searchConfig.query.trim();
        if (!query.length)
            return null;

        var greaterThan = (query.startsWith(">"));
        var lessThan = (query.startsWith("<"));
        var equalTo = (query.startsWith("=") || ((greaterThan || lessThan) && query.indexOf("=") === 1));
        var percentUnits = (query.endsWith("%"));
        var millisecondsUnits = (query.length > 2 && query.endsWith("ms"));
        var secondsUnits = (!millisecondsUnits && query.endsWith("s"));

        var queryNumber = parseFloat(query);
        if (greaterThan || lessThan || equalTo) {
            if (equalTo && (greaterThan || lessThan))
                queryNumber = parseFloat(query.substring(2));
            else
                queryNumber = parseFloat(query.substring(1));
        }

        var queryNumberMilliseconds = (secondsUnits ? (queryNumber * 1000) : queryNumber);

        // Make equalTo implicitly true if it wasn't specified there is no other operator.
        if (!isNaN(queryNumber) && !(greaterThan || lessThan))
            equalTo = true;

        var matcher = createPlainTextSearchRegex(query, "i");

        function matchesQuery(/*ProfileDataGridNode*/ profileDataGridNode)
        {
            delete profileDataGridNode._searchMatchedSelfColumn;
            delete profileDataGridNode._searchMatchedTotalColumn;
            delete profileDataGridNode._searchMatchedFunctionColumn;

            if (percentUnits) {
                if (lessThan) {
                    if (profileDataGridNode.selfPercent < queryNumber)
                        profileDataGridNode._searchMatchedSelfColumn = true;
                    if (profileDataGridNode.totalPercent < queryNumber)
                        profileDataGridNode._searchMatchedTotalColumn = true;
                } else if (greaterThan) {
                    if (profileDataGridNode.selfPercent > queryNumber)
                        profileDataGridNode._searchMatchedSelfColumn = true;
                    if (profileDataGridNode.totalPercent > queryNumber)
                        profileDataGridNode._searchMatchedTotalColumn = true;
                }

                if (equalTo) {
                    if (profileDataGridNode.selfPercent == queryNumber)
                        profileDataGridNode._searchMatchedSelfColumn = true;
                    if (profileDataGridNode.totalPercent == queryNumber)
                        profileDataGridNode._searchMatchedTotalColumn = true;
                }
            } else if (millisecondsUnits || secondsUnits) {
                if (lessThan) {
                    if (profileDataGridNode.selfTime < queryNumberMilliseconds)
                        profileDataGridNode._searchMatchedSelfColumn = true;
                    if (profileDataGridNode.totalTime < queryNumberMilliseconds)
                        profileDataGridNode._searchMatchedTotalColumn = true;
                } else if (greaterThan) {
                    if (profileDataGridNode.selfTime > queryNumberMilliseconds)
                        profileDataGridNode._searchMatchedSelfColumn = true;
                    if (profileDataGridNode.totalTime > queryNumberMilliseconds)
                        profileDataGridNode._searchMatchedTotalColumn = true;
                }

                if (equalTo) {
                    if (profileDataGridNode.selfTime == queryNumberMilliseconds)
                        profileDataGridNode._searchMatchedSelfColumn = true;
                    if (profileDataGridNode.totalTime == queryNumberMilliseconds)
                        profileDataGridNode._searchMatchedTotalColumn = true;
                }
            }

            if (profileDataGridNode.functionName.match(matcher) || (profileDataGridNode.url && profileDataGridNode.url.match(matcher)))
                profileDataGridNode._searchMatchedFunctionColumn = true;

            if (profileDataGridNode._searchMatchedSelfColumn ||
                profileDataGridNode._searchMatchedTotalColumn ||
                profileDataGridNode._searchMatchedFunctionColumn) {
                profileDataGridNode.refresh();
                return true;
            }

            return false;
        }
        return matchesQuery;
    },

    /**
     * @override
     * @param {!WebInspector.SearchableView.SearchConfig} searchConfig
     * @param {boolean} shouldJump
     * @param {boolean=} jumpBackwards
     * @return {number}
     */
    performSearch: function(searchConfig, shouldJump, jumpBackwards)
    {
        this.searchCanceled();
        var matchesQuery = this._matchFunction(searchConfig);
        if (!matchesQuery)
            return 0;

        this._searchResults = [];
        for (var current = this.children[0]; current; current = current.traverseNextNode(false, null, false)) {
            if (matchesQuery(current))
                this._searchResults.push({ profileNode: current });
        }
        this._searchResultIndex = jumpBackwards ? 0 : this._searchResults.length - 1;
        return this._searchResults.length;
    },

    /**
     * @override
     */
    searchCanceled: function()
    {
        if (this._searchResults) {
            for (var i = 0; i < this._searchResults.length; ++i) {
                var profileNode = this._searchResults[i].profileNode;
                delete profileNode._searchMatchedSelfColumn;
                delete profileNode._searchMatchedTotalColumn;
                delete profileNode._searchMatchedFunctionColumn;
                profileNode.refresh();
            }
        }

        this._searchResults = [];
        this._searchResultIndex = -1;
    },

    /**
     * @override
     */
    jumpToNextSearchResult: function()
    {
        if (!this._searchResults || !this._searchResults.length)
            return;
        this._searchResultIndex = (this._searchResultIndex + 1) % this._searchResults.length;
        this._jumpToSearchResult(this._searchResultIndex);
    },

    /**
     * @override
     */
    jumpToPreviousSearchResult: function()
    {
        if (!this._searchResults || !this._searchResults.length)
            return;
        this._searchResultIndex = (this._searchResultIndex - 1 + this._searchResults.length) % this._searchResults.length;
        this._jumpToSearchResult(this._searchResultIndex);
    },

    /**
     * @override
     * @return {number}
     */
    currentSearchResultIndex: function()
    {
        return this._searchResultIndex;
    },

    _jumpToSearchResult: function(index)
    {
        var searchResult = this._searchResults[index];
        if (!searchResult)
            return;

        var profileNode = searchResult.profileNode;
        profileNode.revealAndSelect();
    }
}

WebInspector.ProfileDataGridTree.propertyComparators = [{}, {}];

/**
 * @param {string} property
 * @param {boolean} isAscending
 * @return {function(!Object.<string, *>, !Object.<string, *>)}
 */
WebInspector.ProfileDataGridTree.propertyComparator = function(property, isAscending)
{
    var comparator = WebInspector.ProfileDataGridTree.propertyComparators[(isAscending ? 1 : 0)][property];

    if (!comparator) {
        if (isAscending) {
            comparator = function(lhs, rhs)
            {
                if (lhs[property] < rhs[property])
                    return -1;

                if (lhs[property] > rhs[property])
                    return 1;

                return 0;
            };
        } else {
            comparator = function(lhs, rhs)
            {
                if (lhs[property] > rhs[property])
                    return -1;

                if (lhs[property] < rhs[property])
                    return 1;

                return 0;
            };
        }

        WebInspector.ProfileDataGridTree.propertyComparators[(isAscending ? 1 : 0)][property] = comparator;
    }

    return comparator;
}
