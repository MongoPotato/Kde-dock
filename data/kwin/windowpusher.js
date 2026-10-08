// windowpusher.js — runs INSIDE KWin, loaded by WindowPusher over
// org.kde.kwin.Scripting. Moves windows out of the auto-hidden dock's way
// while it is revealed, and puts them back when it hides (issue #12).
//
// A Wayland client cannot move other clients' windows; only the compositor
// can, so this half of the feature has to live in KWin.
//
// How it learns the dock's state: the dock slides inside a surface whose
// size never changes (see LayerShellWindow::setRevealed), so there is
// nothing to observe from here. KWin scripts can call D-Bus but cannot
// serve it, so this script long-polls instead: pollState() on the dock does
// not answer until the state differs from the serial passed in, or until a
// keep-alive interval has passed. Idle cost is one round trip per keep-alive.
//
// What it does with that state:
//   revealed → every eligible window overlapping the dock's band is moved
//              away from the dock by exactly the overlap. If there isn't
//              room it is shrunk instead. Maximised and tiled windows are
//              only ever shrunk, keeping their far edge where it is.
//   hidden   → each pushed window goes back to where it was, unless the user
//              has moved or resized it in the meantime, in which case it is
//              left alone.
// Geometry is committed once per reveal/hide, never animated frame by frame.
// Making that one move slide is the kdock_pushslide effect's job (see
// data/kwin/kdock_pushslide); this script doesn't know or care whether it
// is loaded.

var SERVICE   = 'org.kde.kdock';
var PATH      = '/WindowPusher';
var INTERFACE = 'org.kde.kdock.WindowPusher';

// No reply for this long means the dock is gone (crashed, killed) or the
// reply was lost: put every window back and keep asking. The dock answers
// every keep-alive interval (10 s) even when nothing changed, so a live dock
// never trips this.
var WATCHDOG_MS = 15000;

// Our own windows (dock, menus, settings panel) are never pushed.
var OWN_RESOURCE_CLASS = 'kdock';

var dockState = null;   // last state from the dock, or null when unknown
var knownSerial = '';   // serial of dockState, echoed back on every poll
var pollGeneration = 0; // a late reply to an abandoned poll is ignored
var pushed = {};        // internalId → push record, see pushWindow()
var applying = false;   // true while we ourselves change a window's state

var watchdog = new QTimer();
watchdog.singleShot = true;
watchdog.interval = WATCHDOG_MS;
watchdog.timeout.connect(function () {
    applyState(null);
    // Ask for the current state outright rather than waiting for a change:
    // the dock may well still be there and revealed.
    knownSerial = '';
    poll();
});

// ── Geometry helpers ─────────────────────────────────────────────────────

function rectOf(r) {
    return { x: r.x, y: r.y, width: r.width, height: r.height };
}

function sameRect(a, b) {
    return Math.abs(a.x - b.x) < 1 && Math.abs(a.y - b.y) < 1
        && Math.abs(a.width - b.width) < 1 && Math.abs(a.height - b.height) < 1;
}

function samePosition(a, b) {
    return Math.abs(a.x - b.x) < 1 && Math.abs(a.y - b.y) < 1;
}

function intersects(a, b) {
    return a.x < b.x + b.width && b.x < a.x + a.width
        && a.y < b.y + b.height && b.y < a.y + a.height;
}

function findOutput(name) {
    var screens = workspace.screens || [];
    for (var i = 0; i < screens.length; i++) {
        if (screens[i].name === name)
            return screens[i];
    }
    // Qt and KWin agree on connector names, but if they ever don't, a single
    // screen is still unambiguous.
    return screens.length === 1 ? screens[0] : null;
}

// The dock's output, and the band the revealed dock covers on it in global
// compositor coordinates. Null when the output can't be found.
function dockPlacement(state) {
    var output = findOutput(state.output);
    if (!output)
        return null;
    var g = output.geometry;
    var t = state.thickness;
    var rect;
    switch (state.edge) {
    case 'top':   rect = { x: g.x, y: g.y, width: g.width, height: t }; break;
    case 'left':  rect = { x: g.x, y: g.y, width: t, height: g.height }; break;
    case 'right': rect = { x: g.x + g.width - t, y: g.y, width: t, height: g.height }; break;
    default:      rect = { x: g.x, y: g.y + g.height - t, width: g.width, height: t }; break;
    }
    return { output: output, rect: rect };
}

// One-dimensional push along the axis that points away from the dock.
// Coordinates are arranged so the dock always sits at the HIGH end:
//   start, len  — the window's extent along the axis
//   dockStart   — where the dock band begins
//   areaStart   — the far limit the window may be pushed back to
//   minLen      — the window's minimum size along the axis
//   keepStart   — shrink only, never move (maximised / tiled windows)
// Returns the new { start, len }, or null when nothing has to change.
function solveAxis(start, len, dockStart, areaStart, minLen, keepStart) {
    var overlap = start + len - dockStart;
    if (overlap <= 0)
        return null;

    // Never pull a window TOWARD the dock — e.g. one the user dragged half
    // past the far edge of the work area.
    var newStart = keepStart ? start
                             : Math.min(start, Math.max(areaStart, start - overlap));
    var newLen = Math.min(len, dockStart - newStart);
    if (newLen < minLen)
        newLen = Math.min(len, minLen);
    if (newLen < 1)
        return null;

    newStart = Math.round(newStart);
    newLen = Math.round(newLen);
    if (Math.abs(newStart - start) < 1 && Math.abs(newLen - len) < 1)
        return null;
    return { start: newStart, len: newLen };
}

// Where window geometry `g` has to go so it clears `dock`, or null.
function pushedGeometry(g, dock, area, minSize, edge, keepStart) {
    var r;
    switch (edge) {
    case 'top':
        // Mirror the vertical axis so the dock is at the high end.
        r = solveAxis(-(g.y + g.height), g.height, -(dock.y + dock.height),
                      -(area.y + area.height), minSize.height, keepStart);
        return r && { x: g.x, y: -(r.start + r.len), width: g.width, height: r.len };
    case 'left':
        r = solveAxis(-(g.x + g.width), g.width, -(dock.x + dock.width),
                      -(area.x + area.width), minSize.width, keepStart);
        return r && { x: -(r.start + r.len), y: g.y, width: r.len, height: g.height };
    case 'right':
        r = solveAxis(g.x, g.width, dock.x, area.x, minSize.width, keepStart);
        return r && { x: r.start, y: g.y, width: r.len, height: g.height };
    default:
        r = solveAxis(g.y, g.height, dock.y, area.y, minSize.height, keepStart);
        return r && { x: g.x, y: r.start, width: g.width, height: r.len };
    }
}

// ── Which windows qualify ────────────────────────────────────────────────

function onCurrentDesktop(w) {
    if (w.onAllDesktops)
        return true;
    var current = workspace.currentDesktop;
    var desktops = w.desktops || [];
    for (var i = 0; i < desktops.length; i++) {
        if (desktops[i] === current || (current && desktops[i].id === current.id))
            return true;
    }
    return false;
}

function onCurrentActivity(w) {
    var activities = w.activities;
    if (!activities || activities.length === 0 || !workspace.currentActivity)
        return true;
    return activities.indexOf(workspace.currentActivity) !== -1;
}

function isOwnWindow(w) {
    return String(w.resourceClass).toLowerCase() === OWN_RESOURCE_CLASS
        || String(w.resourceName).toLowerCase() === OWN_RESOURCE_CLASS;
}

// Maximised (in any direction) or tiled: KWin keeps these fitted to the
// work area, so moving them makes no sense — they may only give up space.
function isSpecialState(w) {
    return (w.maximizeMode !== undefined && w.maximizeMode !== 0) || !!w.tile;
}

function isEligible(w, output) {
    if (!w || w.deleted)
        return false;
    if (!(w.normalWindow || w.dialog))
        return false;
    if (w.fullScreen || w.minimized || w.popupWindow || !w.moveable)
        return false;
    if (w.move || w.resize)     // the user is dragging it right now
        return false;
    if (isOwnWindow(w))
        return false;
    if (!w.output || w.output.name !== output.name)
        return false;
    return onCurrentDesktop(w) && onCurrentActivity(w);
}

// ── Push / restore ───────────────────────────────────────────────────────

function windowKey(w) {
    return String(w.internalId);
}

function setGeometry(w, rect) {
    applying = true;
    try {
        w.frameGeometry = rect;
    } finally {
        applying = false;
    }
}

function forget(key) {
    var record = pushed[key];
    if (!record)
        return;
    for (var i = 0; i < record.connections.length; i++) {
        try {
            record.connections[i].signal.disconnect(record.connections[i].slot);
        } catch (e) {
            // The window is already gone.
        }
    }
    delete pushed[key];
}

// Anything that changes a window's state behind our back means the user (or
// a shortcut, or KWin itself) has taken over: restoring the old geometry
// would undo their action.
function watchForTakeover(w, record) {
    var markTouched = function () {
        if (!applying)
            record.touched = true;
    };
    var signals = [w.interactiveMoveResizeStarted, w.maximizedChanged,
                   w.fullScreenChanged, w.outputChanged, w.tileChanged];
    for (var i = 0; i < signals.length; i++) {
        if (signals[i] && signals[i].connect) {
            signals[i].connect(markTouched);
            record.connections.push({ signal: signals[i], slot: markTouched });
        }
    }
    var onClosed = function () { forget(windowKey(w)); };
    if (w.closed && w.closed.connect) {
        w.closed.connect(onClosed);
        record.connections.push({ signal: w.closed, slot: onClosed });
    }
}

function pushWindow(w, state, placement) {
    var key = windowKey(w);
    if (pushed[key] || !isEligible(w, placement.output))
        return false;

    var g = rectOf(w.frameGeometry);
    var dock = placement.rect;
    if (!intersects(g, dock))
        return false;

    var area = rectOf(workspace.clientArea(KWin.MaximizeArea, w));
    var minSize = w.minSize || { width: 0, height: 0 };
    var target = pushedGeometry(g, dock, area, minSize, state.edge, isSpecialState(w));
    if (!target)
        return false;

    var record = {
        window: w,
        original: g,
        target: target,
        resized: Math.abs(target.width - g.width) >= 1
              || Math.abs(target.height - g.height) >= 1,
        touched: false,
        connections: [],
    };
    pushed[key] = record;
    watchForTakeover(w, record);
    setGeometry(w, target);
    return true;
}

function restoreWindow(key) {
    var record = pushed[key];
    var w = record.window;
    forget(key);
    if (!w || w.deleted || record.touched)
        return false;

    // Clients cannot move themselves on Wayland, so a window that is no
    // longer where we put it was moved by the user (or a keyboard shortcut).
    // Leave it there. A size change alone is the app's own business.
    var now = rectOf(w.frameGeometry);
    if (!samePosition(now, record.target))
        return false;

    var back = {
        x: record.original.x,
        y: record.original.y,
        width: record.resized ? record.original.width : now.width,
        height: record.resized ? record.original.height : now.height,
    };
    if (!sameRect(back, now))
        setGeometry(w, back);
    return true;
}

function pushAll(state) {
    var placement = dockPlacement(state);
    if (!placement)
        return 0;
    var count = 0;
    var windows = workspace.windowList();
    for (var i = 0; i < windows.length; i++) {
        if (pushWindow(windows[i], state, placement))
            ++count;
    }
    return count;
}

function restoreAll() {
    var count = 0;
    for (var key in pushed) {
        if (restoreWindow(key))
            ++count;
    }
    return count;
}

function isRevealed(state) {
    return !!(state && state.enabled && state.revealed);
}

function samePlacement(a, b) {
    return a && b && a.output === b.output && a.edge === b.edge
        && a.thickness === b.thickness;
}

// Returns a short summary for the dock's debug log.
function applyState(state) {
    var previous = dockState;
    dockState = state;

    if (!isRevealed(state))
        return 'restored ' + restoreAll();

    // The dock moved or changed size while up: start over from scratch.
    var restored = 0;
    if (isRevealed(previous) && !samePlacement(previous, state))
        restored = restoreAll();
    var count = pushAll(state);
    return 'pushed ' + count + (restored ? ', restored ' + restored : '');
}

// ── Talking to the dock ──────────────────────────────────────────────────

function poll() {
    var generation = ++pollGeneration;
    watchdog.start();
    callDBus(SERVICE, PATH, INTERFACE, 'pollState', knownSerial, function (json) {
        if (generation !== pollGeneration)
            return;
        var state = null;
        try {
            state = JSON.parse(json);
        } catch (e) {
            state = null;
        }
        if (state && String(state.serial) !== knownSerial) {
            knownSerial = String(state.serial);
            var summary = applyState(state);
            callDBus(SERVICE, PATH, INTERFACE, 'acknowledge', knownSerial, summary);
        }
        poll();
    });
}

// Windows that appear, or come into view, while the dock is up get the
// same treatment as the ones that were there when it revealed.
workspace.windowAdded.connect(function (w) {
    if (isRevealed(dockState)) {
        var placement = dockPlacement(dockState);
        if (placement)
            pushWindow(w, dockState, placement);
    }
});

workspace.currentDesktopChanged.connect(function () {
    if (isRevealed(dockState))
        pushAll(dockState);
});

poll();
