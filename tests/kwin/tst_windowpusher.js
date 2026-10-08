// Runs data/kwin/windowpusher.js against a mocked KWin scripting API.
//
// The script normally lives inside KWin, where there is no way to test it
// automatically. Everything it touches (workspace, windows, QTimer,
// callDBus) is small enough to fake, so each test builds a fake KWin, plays
// the dock's side of the long-poll, and checks where the windows end up.
//
// Usage: node tst_windowpusher.js path/to/windowpusher.js

'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');

const scriptPath = process.argv[2];
if (!scriptPath) {
    console.error('usage: node tst_windowpusher.js <windowpusher.js>');
    process.exit(2);
}
const scriptSource = fs.readFileSync(scriptPath, 'utf8');

// ── Fake KWin ────────────────────────────────────────────────────────────────

class Signal {
    constructor() { this.slots = []; }
    connect(fn) { this.slots.push(fn); }
    disconnect(fn) {
        const i = this.slots.indexOf(fn);
        if (i === -1) throw new Error('not connected');
        this.slots.splice(i, 1);
    }
    emit(...args) { for (const fn of this.slots.slice()) fn(...args); }
}

class FakeTimer {
    constructor() {
        this.singleShot = false;
        this.interval = 0;
        this.active = false;
        this.timeout = new Signal();
    }
    start() { this.active = true; }
    stop() { this.active = false; }
    fire() { this.active = false; this.timeout.emit(); }
}

let nextId = 1;

class FakeWindow {
    constructor(kwin, props) {
        this._kwin = kwin;
        Object.assign(this, {
            internalId: `{window-${nextId++}}`,
            resourceClass: 'org.kde.kate',
            resourceName: 'kate',
            normalWindow: true,
            dialog: false,
            fullScreen: false,
            minimized: false,
            popupWindow: false,
            moveable: true,
            move: false,
            resize: false,
            deleted: false,
            maximizeMode: 0,
            tile: null,
            onAllDesktops: false,
            desktops: [kwin.desktops[0]],
            activities: [],
            minSize: { width: 0, height: 0 },
            output: kwin.screens[0],
        });
        const geometry = props.geometry;
        delete props.geometry;
        Object.assign(this, props);
        this._geometry = { ...geometry };
        this.geometryWrites = [];
        for (const name of ['interactiveMoveResizeStarted', 'interactiveMoveResizeFinished',
                            'maximizedChanged', 'fullScreenChanged', 'minimizedChanged',
                            'outputChanged', 'desktopsChanged', 'tileChanged', 'closed',
                            'frameGeometryChanged']) {
            this[name] = new Signal();
        }
    }
    get frameGeometry() { return { ...this._geometry }; }
    // Like KWin with a Wayland client: a pure move applies on the spot; a
    // resize is a configure request the app answers later, and the answer
    // places the window where that request said — even if the geometry was
    // set again (at the old size) in the meantime.
    set frameGeometry(rect) {
        const r = { x: rect.x, y: rect.y, width: rect.width, height: rect.height };
        this.geometryWrites.push({ ...r });
        const resize = r.width !== this._geometry.width || r.height !== this._geometry.height;
        if (resize && this._kwin.asyncResize) {
            this.pendingConfigure = r;
            return;
        }
        this._geometry = r;
        if (resize)
            this._kwin.unanswered.push(this);
        if (this.onWrite)
            this.onWrite();
        this.frameGeometryChanged.emit();
    }
    // The app answers its pending resize request (async mode).
    answer() {
        assert.ok(this.pendingConfigure, 'nothing to answer');
        this._geometry = this.pendingConfigure;
        this.pendingConfigure = null;
        this.frameGeometryChanged.emit();
    }
    // What the user (or a shortcut) does, as opposed to the script.
    userMoveTo(x, y) {
        this.interactiveMoveResizeStarted.emit();
        this._geometry = { ...this._geometry, x, y };
    }
    // A complete drag: start, move, let go.
    userDrop(x, y) {
        this.userMoveTo(x, y);
        this.frameGeometryChanged.emit();
        this.interactiveMoveResizeFinished.emit();
    }
    // A geometry change nobody in particular asked for: KWin re-fitting a
    // maximized window, an app resizing itself.
    changeBehindOurBack(rect) {
        this._geometry = { ...rect };
        this.frameGeometryChanged.emit();
    }
}

// One 1920x1080 screen by default; a Plasma panel would shrink `area`.
function makeKWin(options = {}) {
    const screens = options.screens || [
        { name: 'eDP-1', geometry: { x: 0, y: 0, width: 1920, height: 1080 } },
    ];
    const kwin = {
        screens,
        desktops: [{ id: 'desktop-1' }, { id: 'desktop-2' }],
        windows: [],
        dbusCalls: [],
        timers: [],
        areas: options.areas || {},
        logs: [],
        // Resizes are answered by the app before the next dock state
        // arrives, unless asyncResize: then only when a test says so.
        asyncResize: !!options.asyncResize,
        unanswered: [],
    };
    kwin.currentDesktop = kwin.desktops[0];

    const workspace = {
        get screens() { return kwin.screens; },
        get currentDesktop() { return kwin.currentDesktop; },
        currentActivity: 'activity-1',
        windowList: () => kwin.windows.slice(),
        windowAdded: new Signal(),
        currentDesktopChanged: new Signal(),
        clientArea: (option, w) => {
            assert.equal(option, 2, 'expected KWin.MaximizeArea');
            return { ...(kwin.areas[w.output.name] || w.output.geometry) };
        },
    };
    kwin.workspace = workspace;

    const context = {
        workspace,
        KWin: { MaximizeArea: 2 },
        QTimer: function () {
            const t = new FakeTimer();
            kwin.timers.push(t);
            return t;
        },
        callDBus: (service, path, iface, method, ...args) => {
            const callback = typeof args[args.length - 1] === 'function' ? args.pop() : null;
            kwin.dbusCalls.push({ service, path, iface, method, args, callback });
        },
        console: { log: (...a) => kwin.logs.push(a.join(' ')) },
    };
    kwin.addWindow = (props) => {
        const w = new FakeWindow(kwin, props);
        kwin.windows.push(w);
        return w;
    };
    kwin.openWindow = (props) => {
        const w = kwin.addWindow(props);
        workspace.windowAdded.emit(w);
        return w;
    };
    kwin.closeWindow = (w) => {
        kwin.windows.splice(kwin.windows.indexOf(w), 1);
        w.deleted = true;
        w.closed.emit();
    };
    kwin.start = () => vm.runInNewContext(scriptSource, context, { filename: scriptPath });

    // The dock's side of the long-poll.
    let serial = 0;
    kwin.pendingPoll = () => {
        const polls = kwin.dbusCalls.filter((c) => c.method === 'pollState' && !c.answered);
        return polls[polls.length - 1];
    };
    kwin.send = (state) => {
        for (const w of kwin.unanswered.splice(0))
            w.frameGeometryChanged.emit();
        const poll = kwin.pendingPoll();
        assert.ok(poll, 'the script should be polling');
        poll.answered = true;
        poll.callback(JSON.stringify({
            serial: ++serial, enabled: true, revealed: true,
            output: 'eDP-1', edge: 'bottom', thickness: 80, ...state,
        }));
    };
    kwin.acks = () => kwin.dbusCalls.filter((c) => c.method === 'acknowledge');
    return kwin;
}

const at = (x, y, width, height) => ({ x, y, width, height });

// ── Tests ───────────────────────────────────────────────────────────────────

const tests = [];
const test = (name, fn) => tests.push({ name, fn });

test('polls the dock on start-up', () => {
    const kwin = makeKWin();
    kwin.start();
    const poll = kwin.pendingPoll();
    assert.equal(poll.service, 'org.kde.kdock');
    assert.equal(poll.path, '/WindowPusher');
    assert.equal(poll.iface, 'org.kde.kdock.WindowPusher');
    assert.deepEqual(poll.args, ['']);
});

test('moves an overlapping window up by exactly the overlap, and back', () => {
    const kwin = makeKWin();
    const w = kwin.addWindow({ geometry: at(100, 500, 800, 530) });   // bottom 1030
    kwin.start();

    kwin.send({ revealed: true });     // dock band starts at 1000
    assert.deepEqual(w.frameGeometry, at(100, 470, 800, 530));

    kwin.send({ revealed: false });
    assert.deepEqual(w.frameGeometry, at(100, 500, 800, 530));
});

test('leaves windows that do not reach the dock alone', () => {
    const kwin = makeKWin();
    const w = kwin.addWindow({ geometry: at(100, 100, 800, 900) });   // bottom exactly 1000
    kwin.start();
    kwin.send({ revealed: true });
    kwin.send({ revealed: false });
    assert.equal(w.geometryWrites.length, 0);
});

test('re-polls with the serial it has and acknowledges each state', () => {
    const kwin = makeKWin();
    kwin.start();
    kwin.send({ revealed: true });
    assert.deepEqual(kwin.pendingPoll().args, ['1']);
    assert.equal(kwin.acks().length, 1);
    assert.equal(kwin.acks()[0].args[0], '1');

    // The same serial again (a keep-alive) is not a new state.
    const poll = kwin.pendingPoll();
    poll.answered = true;
    poll.callback(JSON.stringify({ serial: 1, enabled: true, revealed: true,
                                   output: 'eDP-1', edge: 'bottom', thickness: 80 }));
    assert.equal(kwin.acks().length, 1);
    assert.deepEqual(kwin.pendingPoll().args, ['1']);
});

test('shrinks a window that has no room to move', () => {
    const kwin = makeKWin({ areas: { 'eDP-1': at(0, 40, 1920, 1040) } });  // top panel
    const w = kwin.addWindow({ geometry: at(0, 40, 1920, 1040) });
    kwin.start();
    kwin.send({ revealed: true });
    assert.deepEqual(w.frameGeometry, at(0, 40, 1920, 960));
    kwin.send({ revealed: false });
    assert.deepEqual(w.frameGeometry, at(0, 40, 1920, 1040));
});

test('moves as far as it can, then shrinks the rest', () => {
    const kwin = makeKWin();
    const w = kwin.addWindow({ geometry: at(0, 30, 600, 1000) });   // bottom 1030
    kwin.start();
    kwin.send({ revealed: true });
    assert.deepEqual(w.frameGeometry, at(0, 0, 600, 1000));
    // 30 px up uses all the room above; nothing left to shrink.
    const tall = kwin.addWindow({ geometry: at(700, 20, 600, 1050) });  // bottom 1070
    kwin.send({ revealed: false });
    kwin.send({ revealed: true });
    assert.deepEqual(tall.frameGeometry, at(700, 0, 600, 1000));
});

test('respects the minimum size', () => {
    const kwin = makeKWin();
    const w = kwin.addWindow({ geometry: at(0, 0, 600, 1080), minSize: { width: 0, height: 1040 } });
    kwin.start();
    kwin.send({ revealed: true });
    assert.deepEqual(w.frameGeometry, at(0, 0, 600, 1040));
});

test('maximized windows shrink and keep their top edge', () => {
    const kwin = makeKWin();
    const w = kwin.addWindow({ geometry: at(0, 0, 1920, 1080), maximizeMode: 3 });
    kwin.start();
    kwin.send({ revealed: true });
    assert.deepEqual(w.frameGeometry, at(0, 0, 1920, 1000));
    kwin.send({ revealed: false });
    assert.deepEqual(w.frameGeometry, at(0, 0, 1920, 1080));
});

test('tiled windows shrink rather than ride up over their neighbour', () => {
    const kwin = makeKWin();
    const w = kwin.addWindow({ geometry: at(0, 540, 960, 540), tile: { id: 'bottom-left' } });
    kwin.start();
    kwin.send({ revealed: true });
    assert.deepEqual(w.frameGeometry, at(0, 540, 960, 460));
});

test('never touches fullscreen, minimized, popup, own or other-output windows', () => {
    const kwin = makeKWin({
        screens: [
            { name: 'eDP-1', geometry: at(0, 0, 1920, 1080) },
            { name: 'DP-1', geometry: at(1920, 0, 2560, 1440) },
        ],
    });
    const g = at(100, 500, 800, 560);
    const untouched = [
        kwin.addWindow({ geometry: g, fullScreen: true }),
        kwin.addWindow({ geometry: g, minimized: true }),
        kwin.addWindow({ geometry: g, popupWindow: true }),
        kwin.addWindow({ geometry: g, normalWindow: false }),     // e.g. a notification
        kwin.addWindow({ geometry: g, moveable: false }),
        kwin.addWindow({ geometry: g, resourceClass: 'kdock', resourceName: 'kdock' }),
        kwin.addWindow({ geometry: at(2000, 900, 800, 560), output: kwin.screens[1] }),
        kwin.addWindow({ geometry: g, desktops: [kwin.desktops[1]] }),
        kwin.addWindow({ geometry: g, activities: ['activity-2'] }),
        kwin.addWindow({ geometry: g, move: true }),               // being dragged
    ];
    const dialog = kwin.addWindow({ geometry: g, normalWindow: false, dialog: true });
    kwin.start();
    kwin.send({ revealed: true });
    for (const w of untouched)
        assert.equal(w.geometryWrites.length, 0, JSON.stringify(w.resourceClass));
    assert.equal(dialog.frameGeometry.y, 440);
});

test('only pushes on the dock\'s output', () => {
    const kwin = makeKWin({
        screens: [
            { name: 'eDP-1', geometry: at(0, 0, 1920, 1080) },
            { name: 'DP-1', geometry: at(1920, 0, 2560, 1440) },
        ],
    });
    const laptop = kwin.addWindow({ geometry: at(100, 500, 800, 560) });
    const monitor = kwin.addWindow({ geometry: at(2000, 900, 800, 560), output: kwin.screens[1] });
    kwin.start();
    kwin.send({ revealed: true, output: 'DP-1' });
    assert.equal(laptop.geometryWrites.length, 0);
    assert.deepEqual(monitor.frameGeometry, at(2000, 800, 800, 560));
});

test('a window the user moved while the dock was up stays put', () => {
    const kwin = makeKWin();
    const w = kwin.addWindow({ geometry: at(100, 500, 800, 560) });
    kwin.start();
    kwin.send({ revealed: true });
    w.userMoveTo(300, 200);
    kwin.send({ revealed: false });
    assert.deepEqual(w.frameGeometry, at(300, 200, 800, 560));
});

test('a window moved by a shortcut (no drag) also stays put', () => {
    const kwin = makeKWin();
    const w = kwin.addWindow({ geometry: at(100, 500, 800, 560) });
    kwin.start();
    kwin.send({ revealed: true });
    w._geometry = { ...w._geometry, x: 0, y: 0 };
    kwin.send({ revealed: false });
    assert.deepEqual(w.frameGeometry, at(0, 0, 800, 560));
});

test('a window maximized while the dock was up is not restored', () => {
    const kwin = makeKWin();
    const w = kwin.addWindow({ geometry: at(100, 500, 800, 560) });
    kwin.start();
    kwin.send({ revealed: true });
    w.maximizeMode = 3;
    w.maximizedChanged.emit();
    kwin.send({ revealed: false });
    assert.equal(w.geometryWrites.length, 1);
});

test('state changes KWin makes because of our own write are not the user\'s', () => {
    const kwin = makeKWin();
    const w = kwin.addWindow({ geometry: at(0, 0, 1920, 1080), maximizeMode: 3 });
    // E.g. KWin dropping the maximized state when a script resizes the window.
    w.onWrite = () => w.maximizedChanged.emit();
    kwin.start();
    kwin.send({ revealed: true });
    kwin.send({ revealed: false });
    assert.deepEqual(w.frameGeometry, at(0, 0, 1920, 1080));
});

test('a window that resized itself keeps its new size when moved back', () => {
    const kwin = makeKWin();
    const w = kwin.addWindow({ geometry: at(100, 500, 800, 560) });
    kwin.start();
    kwin.send({ revealed: true });
    w._geometry = { ...w._geometry, width: 900, height: 500 };   // the app's own doing
    kwin.send({ revealed: false });
    assert.deepEqual(w.frameGeometry, at(100, 500, 900, 500));
});

test('a window closed while pushed is forgotten', () => {
    const kwin = makeKWin();
    const w = kwin.addWindow({ geometry: at(100, 500, 800, 560) });
    kwin.start();
    kwin.send({ revealed: true });
    kwin.closeWindow(w);
    assert.equal(w.closed.slots.length, 0, 'signals disconnected');
    kwin.send({ revealed: false });
    assert.equal(w.geometryWrites.length, 1);
});

test('windows opened while the dock is up make room too', () => {
    const kwin = makeKWin();
    kwin.start();
    kwin.send({ revealed: true });
    const w = kwin.openWindow({ geometry: at(100, 600, 800, 450) });
    assert.deepEqual(w.frameGeometry, at(100, 550, 800, 450));
    kwin.send({ revealed: false });
    assert.deepEqual(w.frameGeometry, at(100, 600, 800, 450));

    const later = kwin.openWindow({ geometry: at(100, 600, 800, 450) });
    assert.equal(later.geometryWrites.length, 0, 'not while hidden');
});

test('switching desktop while revealed pushes the newly visible windows', () => {
    const kwin = makeKWin();
    const other = kwin.addWindow({ geometry: at(100, 600, 800, 450), desktops: [kwin.desktops[1]] });
    kwin.start();
    kwin.send({ revealed: true });
    assert.equal(other.geometryWrites.length, 0);
    kwin.currentDesktop = kwin.desktops[1];
    kwin.workspace.currentDesktopChanged.emit();
    assert.deepEqual(other.frameGeometry, at(100, 550, 800, 450));
    kwin.send({ revealed: false });
    assert.deepEqual(other.frameGeometry, at(100, 600, 800, 450));
});

test('disabling the feature puts everything back', () => {
    const kwin = makeKWin();
    const w = kwin.addWindow({ geometry: at(100, 500, 800, 560) });
    kwin.start();
    kwin.send({ revealed: true });
    kwin.send({ enabled: false, revealed: true });
    assert.deepEqual(w.frameGeometry, at(100, 500, 800, 560));
    assert.match(kwin.acks().pop().args[1], /restored 1/);
});

test('if the dock goes silent, the watchdog puts everything back', () => {
    const kwin = makeKWin();
    const w = kwin.addWindow({ geometry: at(100, 500, 800, 560) });
    kwin.start();
    kwin.send({ revealed: true });
    const stale = kwin.pendingPoll();
    const watchdog = kwin.timers[0];
    assert.ok(watchdog.active);
    watchdog.fire();
    assert.deepEqual(w.frameGeometry, at(100, 500, 800, 560));

    // It asks again from scratch, and ignores the reply to the abandoned poll.
    assert.deepEqual(kwin.pendingPoll().args, ['']);
    stale.answered = true;
    stale.callback(JSON.stringify({ serial: 99, enabled: true, revealed: true,
                                    output: 'eDP-1', edge: 'bottom', thickness: 80 }));
    assert.deepEqual(w.frameGeometry, at(100, 500, 800, 560));
});

test('a dock that moved while revealed: restore, then push for the new place', () => {
    const kwin = makeKWin();
    const w = kwin.addWindow({ geometry: at(100, 500, 800, 560) });   // bottom 1060
    kwin.start();
    kwin.send({ revealed: true, thickness: 80 });
    assert.equal(w.frameGeometry.y, 440);
    kwin.send({ revealed: true, thickness: 120 });
    assert.equal(w.frameGeometry.y, 400);
    kwin.send({ revealed: false, thickness: 120 });
    assert.equal(w.frameGeometry.y, 500);
});

test('top edge: windows move down', () => {
    const kwin = makeKWin();
    const w = kwin.addWindow({ geometry: at(100, 50, 800, 500) });
    kwin.start();
    kwin.send({ edge: 'top' });
    assert.deepEqual(w.frameGeometry, at(100, 80, 800, 500));
    kwin.send({ edge: 'top', revealed: false });
    assert.deepEqual(w.frameGeometry, at(100, 50, 800, 500));
});

test('top edge: no room below means shrink', () => {
    const kwin = makeKWin();
    const w = kwin.addWindow({ geometry: at(0, 0, 800, 1080) });
    kwin.start();
    kwin.send({ edge: 'top' });
    assert.deepEqual(w.frameGeometry, at(0, 80, 800, 1000));
});

test('left edge: windows move right', () => {
    const kwin = makeKWin();
    const w = kwin.addWindow({ geometry: at(20, 100, 800, 600) });
    kwin.start();
    kwin.send({ edge: 'left' });
    assert.deepEqual(w.frameGeometry, at(80, 100, 800, 600));
});

test('right edge: windows move left', () => {
    const kwin = makeKWin();
    const w = kwin.addWindow({ geometry: at(1200, 100, 700, 600) });   // right 1900
    kwin.start();
    kwin.send({ edge: 'right' });
    assert.deepEqual(w.frameGeometry, at(1140, 100, 700, 600));
});

test('never pulls a window toward the dock', () => {
    const kwin = makeKWin({ areas: { 'eDP-1': at(0, 40, 1920, 1040) } });
    // Dragged half above the top panel by the user.
    const w = kwin.addWindow({ geometry: at(0, 10, 800, 1060) });   // bottom 1070
    kwin.start();
    kwin.send({ revealed: true });
    assert.deepEqual(w.frameGeometry, at(0, 10, 800, 990));
});

// ── Resizes the app hasn't answered yet ─────────────────────────────────────

test('hidden before the app answered the shrink: restored once it does', () => {
    const kwin = makeKWin({ asyncResize: true });
    const w = kwin.addWindow({ geometry: at(0, 0, 1920, 1080), maximizeMode: 3 });
    kwin.start();
    kwin.send({ revealed: true });
    assert.deepEqual(w.pendingConfigure, at(0, 0, 1920, 1000));
    kwin.send({ revealed: false });            // the dock left already
    w.answer();                                // ... and now the app shrinks
    assert.deepEqual(w.frameGeometry, at(0, 0, 1920, 1000));
    // The restore went out after the answer, as a request of its own.
    assert.deepEqual(w.pendingConfigure, at(0, 0, 1920, 1080));
    w.answer();
    assert.deepEqual(w.frameGeometry, at(0, 0, 1920, 1080));
});

test('a window moved and shrunk is restored too when the answer comes late', () => {
    const kwin = makeKWin({ asyncResize: true });
    const w = kwin.addWindow({ geometry: at(0, 30, 600, 1050) });     // bottom 1080
    kwin.start();
    kwin.send({ revealed: true });
    kwin.send({ revealed: false });
    w.answer();
    w.answer();
    assert.deepEqual(w.frameGeometry, at(0, 30, 600, 1050));
});

test('revealed again before the answer: the stale restore is dropped', () => {
    const kwin = makeKWin({ asyncResize: true });
    const w = kwin.addWindow({ geometry: at(0, 0, 1920, 1080), maximizeMode: 3 });
    kwin.start();
    kwin.send({ revealed: true });
    kwin.send({ revealed: false });
    kwin.send({ revealed: true });
    w.answer();
    assert.deepEqual(w.frameGeometry, at(0, 0, 1920, 1000));
    assert.equal(w.pendingConfigure, null, 'no restore went out');
    kwin.timers.forEach((t) => t.active && t !== kwin.timers[0] && t.fire());
    assert.deepEqual(w.frameGeometry, at(0, 0, 1920, 1000));
});

test('an app that never answers does not keep a restore waiting forever', () => {
    const kwin = makeKWin({ asyncResize: true });
    const w = kwin.addWindow({ geometry: at(0, 0, 1920, 1080), maximizeMode: 3 });
    kwin.start();
    kwin.send({ revealed: true });
    kwin.send({ revealed: false });
    assert.equal(w.frameGeometryChanged.slots.length, 1, 'waiting for the answer');
    kwin.timers[kwin.timers.length - 1].fire();
    assert.equal(w.frameGeometryChanged.slots.length, 0, 'gave up waiting');
    // Still where it always was (the shrink never happened).
    assert.deepEqual(w.frameGeometry, at(0, 0, 1920, 1080));
});

test('a dock moved to another edge: windows go back, then make room there', () => {
    const kwin = makeKWin();
    const w = kwin.addWindow({ geometry: at(30, 500, 800, 560) });   // bottom 1060, left 30
    kwin.start();
    kwin.send({ edge: 'bottom' });
    assert.deepEqual(w.frameGeometry, at(30, 440, 800, 560));
    kwin.send({ edge: 'left' });
    // Back at its own height, and out of the left dock's way.
    assert.deepEqual(w.frameGeometry, at(80, 500, 800, 560));
    kwin.send({ edge: 'left', revealed: false });
    assert.deepEqual(w.frameGeometry, at(30, 500, 800, 560));
});

// ── Fixed dock: keep windows clear all the time ─────────────────────────────

const fixed = (state = {}) => ({ keepClear: true, revealed: true, ...state });
const effectCalls = (kwin) => kwin.dbusCalls.filter((c) => c.method === 'reconfigureEffect');

test('fixed dock: overlapping windows are pushed straight away', () => {
    const kwin = makeKWin();
    const w = kwin.addWindow({ geometry: at(100, 500, 800, 560) });
    kwin.start();
    kwin.send(fixed());
    assert.deepEqual(w.frameGeometry, at(100, 440, 800, 560));
});

test('fixed dock: a window dropped onto the dock is pushed off it', () => {
    const kwin = makeKWin();
    const w = kwin.addWindow({ geometry: at(100, 100, 800, 560) });
    kwin.start();
    kwin.send(fixed());
    w.userDrop(300, 700);                                  // bottom 1260
    assert.deepEqual(w.frameGeometry, at(300, 440, 800, 560));
});

test('fixed dock: dropped windows slide when the effect is loaded', () => {
    const kwin = makeKWin();
    const w = kwin.addWindow({ geometry: at(100, 100, 800, 560) });
    kwin.start();
    kwin.send(fixed({ animated: true }));
    w.userDrop(300, 700);
    // The effect is told first, KWin to KWin; the move follows its reply.
    const call = effectCalls(kwin).pop();
    assert.equal(call.service, 'org.kde.KWin');
    assert.equal(call.path, '/Effects');
    assert.deepEqual(call.args, ['kdock_pushslide']);
    assert.equal(w.frameGeometry.y, 700, 'not yet');
    call.callback();
    assert.equal(w.frameGeometry.y, 440);
    // The fallback timer firing afterwards must not push twice.
    kwin.timers[kwin.timers.length - 1].fire();
    assert.equal(w.geometryWrites.length, 1);
});

test('fixed dock: a lost effect reply does not leave the window on the dock', () => {
    const kwin = makeKWin();
    const w = kwin.addWindow({ geometry: at(100, 100, 800, 560) });
    kwin.start();
    kwin.send(fixed({ animated: true }));
    w.userDrop(300, 700);
    kwin.timers[kwin.timers.length - 1].fire();             // no reply came
    assert.equal(w.frameGeometry.y, 440);
});

test('fixed dock: a window dropped clear of the dock is left alone', () => {
    const kwin = makeKWin();
    const w = kwin.addWindow({ geometry: at(100, 100, 800, 560) });
    kwin.start();
    kwin.send(fixed());
    w.userDrop(300, 200);
    assert.equal(w.geometryWrites.length, 0);
    assert.equal(effectCalls(kwin).length, 0);
});

test('fixed dock: a pushed window the user drags elsewhere stays there', () => {
    const kwin = makeKWin();
    const w = kwin.addWindow({ geometry: at(100, 500, 800, 560) });
    kwin.start();
    kwin.send(fixed());
    w.userDrop(0, 0);
    assert.deepEqual(w.frameGeometry, at(0, 0, 800, 560));
    assert.equal(w.geometryWrites.length, 1);
});

test('fixed dock: turning it off returns windows to where they were', () => {
    const kwin = makeKWin();
    const a = kwin.addWindow({ geometry: at(100, 500, 800, 560) });
    const b = kwin.addWindow({ geometry: at(900, 100, 800, 560) });
    kwin.start();
    kwin.send(fixed());
    b.userDrop(900, 600);       // the user put it there; we moved it up
    kwin.send(fixed({ enabled: false }));
    assert.deepEqual(a.frameGeometry, at(100, 500, 800, 560));
    assert.deepEqual(b.frameGeometry, at(900, 600, 800, 560));
});

test('fixed dock: an unminimized window that covers the dock is pushed', () => {
    const kwin = makeKWin();
    const w = kwin.addWindow({ geometry: at(100, 500, 800, 560), minimized: true });
    kwin.start();
    kwin.send(fixed());
    assert.equal(w.geometryWrites.length, 0);
    w.minimized = false;
    w.minimizedChanged.emit();
    assert.equal(w.frameGeometry.y, 440);
});

test('fixed dock: maximizing shrinks the window above the dock', () => {
    const kwin = makeKWin();
    const w = kwin.addWindow({ geometry: at(100, 100, 800, 560) });
    kwin.start();
    kwin.send(fixed());
    w.maximizeMode = 3;
    w._geometry = at(0, 0, 1920, 1080);
    w.maximizedChanged.emit();
    assert.deepEqual(w.frameGeometry, at(0, 0, 1920, 1000));
});

test('fixed dock: KWin re-fitting a maximized window over the dock is undone', () => {
    const kwin = makeKWin();
    const w = kwin.addWindow({ geometry: at(0, 0, 1920, 1080), maximizeMode: 3 });
    kwin.start();
    kwin.send(fixed());
    assert.equal(w.frameGeometry.height, 1000);
    w.changeBehindOurBack(at(0, 0, 1920, 1080));            // e.g. a panel changed
    assert.equal(w.frameGeometry.height, 1000);
    // And off again restores the real maximized geometry, not the re-fit.
    kwin.send(fixed({ enabled: false }));
    assert.deepEqual(w.frameGeometry, at(0, 0, 1920, 1080));
});

test('fixed dock: an app growing over the dock is pushed again, and restored to its own place', () => {
    const kwin = makeKWin();
    const w = kwin.addWindow({ geometry: at(100, 500, 800, 560) });
    kwin.start();
    kwin.send(fixed());
    assert.equal(w.frameGeometry.y, 440);
    w.changeBehindOurBack(at(100, 440, 800, 700));         // grew by itself
    assert.deepEqual(w.frameGeometry, at(100, 300, 800, 700));
    kwin.send(fixed({ enabled: false }));
    // Back to where it was before we ever moved it, at the size it chose.
    assert.deepEqual(w.frameGeometry, at(100, 500, 800, 700));
});

test('fixed dock: a pushed window found back on the dock is pushed again', () => {
    const kwin = makeKWin();
    const w = kwin.addWindow({ geometry: at(100, 500, 800, 560) });
    kwin.start();
    kwin.send(fixed());
    w._geometry = at(100, 600, 800, 560);                  // no signal for it
    w.minimizedChanged.emit();                             // but something happens
    assert.equal(w.frameGeometry.y, 440);
});

test('fixed dock: an app that insists on covering the dock is given up on', () => {
    const kwin = makeKWin();
    const w = kwin.addWindow({ geometry: at(0, 0, 800, 1080) });
    // The app answers every resize request by going back to full height,
    // after the fact, as a real client would.
    let replies = 0;
    w.onWrite = () => { replies++; };
    kwin.start();
    kwin.send(fixed());
    for (let guard = 0; replies > 0 && guard < 20; guard++) {
        replies--;
        w.changeBehindOurBack({ ...w.frameGeometry, height: 1080 });
    }
    assert.equal(w.geometryWrites.length, 4);                // first push + 3 retries
});

test('fixed dock: no geometry watching for windows that were never pushed', () => {
    const kwin = makeKWin();
    const w = kwin.addWindow({ geometry: at(100, 100, 800, 560) });
    kwin.start();
    kwin.send(fixed());
    assert.equal(w.frameGeometryChanged.slots.length, 0);
    assert.equal(w.interactiveMoveResizeFinished.slots.length, 1);
});

test('fixed dock: windows opened later are watched too', () => {
    const kwin = makeKWin();
    kwin.start();
    kwin.send(fixed());
    const w = kwin.openWindow({ geometry: at(100, 100, 800, 560) });
    w.userDrop(100, 800);
    assert.equal(w.frameGeometry.y, 440);
});

test('switching the dock to auto-hide stops watching and restores', () => {
    const kwin = makeKWin();
    const w = kwin.addWindow({ geometry: at(100, 500, 800, 560) });
    kwin.start();
    kwin.send(fixed());
    kwin.send({ keepClear: false, revealed: false });
    assert.deepEqual(w.frameGeometry, at(100, 500, 800, 560));
    assert.equal(w.interactiveMoveResizeFinished.slots.length, 0);
    w.userDrop(100, 700);
    assert.equal(w.frameGeometry.y, 700);
});

test('auto-hide: a pushed window that drifts back is not chased', () => {
    const kwin = makeKWin();
    const w = kwin.addWindow({ geometry: at(0, 0, 1920, 1080), maximizeMode: 3 });
    kwin.start();
    kwin.send({ revealed: true });
    w.changeBehindOurBack(at(0, 0, 1920, 1080));
    assert.equal(w.geometryWrites.length, 1);
});

test('unknown output on a multi-screen setup does nothing', () => {
    const kwin = makeKWin({
        screens: [
            { name: 'eDP-1', geometry: at(0, 0, 1920, 1080) },
            { name: 'DP-1', geometry: at(1920, 0, 2560, 1440) },
        ],
    });
    const w = kwin.addWindow({ geometry: at(100, 500, 800, 560) });
    kwin.start();
    kwin.send({ revealed: true, output: 'HDMI-9' });
    assert.equal(w.geometryWrites.length, 0);
});

test('unknown output with a single screen uses that screen', () => {
    const kwin = makeKWin();
    const w = kwin.addWindow({ geometry: at(100, 500, 800, 560) });
    kwin.start();
    kwin.send({ revealed: true, output: '' });
    assert.equal(w.frameGeometry.y, 440);
});

// ── Runner ──────────────────────────────────────────────────────────────────

let failed = 0;
for (const { name, fn } of tests) {
    try {
        fn();
        console.log(`PASS   : ${name}`);
    } catch (e) {
        failed++;
        console.log(`FAIL!  : ${name}\n${e.stack}`);
    }
}
console.log(`Totals: ${tests.length - failed} passed, ${failed} failed`);
process.exit(failed ? 1 : 0);
