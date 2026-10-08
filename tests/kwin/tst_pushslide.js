// Runs data/kwin/kdock_pushslide/contents/code/main.js against a mocked
// KWin effects API: checks when the effect listens, which geometry changes
// it animates, and what offsets it animates from.
//
// Usage: node tst_pushslide.js path/to/main.js

'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');

const scriptPath = process.argv[2];
if (!scriptPath) {
    console.error('usage: node tst_pushslide.js <main.js>');
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

class FakeWindow {
    constructor(props) {
        Object.assign(this, {
            normalWindow: true, dialog: false, visible: true, minimized: false,
            fullScreen: false, onCurrentDesktop: true, move: false, resize: false,
        });
        Object.assign(this, props);
        this.geometry = { ...props.geometry };
        this.windowFrameGeometryChanged = new Signal();
    }
    // What the window pusher script does: a real geometry change.
    setGeometry(rect) {
        const old = this.geometry;
        this.geometry = { ...rect };
        this.windowFrameGeometryChanged.emit(this, old);
    }
    moveBy(dx, dy) {
        this.setGeometry({ ...this.geometry, x: this.geometry.x + dx, y: this.geometry.y + dy });
    }
}

function makeKWin() {
    const kwin = { windows: [], animations: [], cancelled: [], now: 1000, nextId: 1 };
    const effect = { configChanged: new Signal(), animationEnded: new Signal() };
    const effects = {
        get stackingOrder() { return kwin.windows.slice(); },
        windowDeleted: new Signal(),
    };
    const context = {
        effect,
        effects,
        Effect: { Translation: 7 },
        QEasingCurve: { OutCubic: 7 },
        animate: (options) => {
            const id = kwin.nextId++;
            kwin.animations.push({ id, ...options });
            return [id];
        },
        cancel: (ids) => { kwin.cancelled.push(...ids); return true; },
        Date: { now: () => kwin.now },
        Math,
    };
    kwin.add = (props) => {
        const w = new FakeWindow(props);
        kwin.windows.push(w);
        return w;
    };
    kwin.headsUp = () => effect.configChanged.emit();
    kwin.endAnimation = (w, id) => effect.animationEnded.emit(w, id);
    kwin.deleteWindow = (w) => {
        kwin.windows.splice(kwin.windows.indexOf(w), 1);
        effects.windowDeleted.emit(w);
    };
    kwin.start = () => vm.runInNewContext(scriptSource, context, { filename: scriptPath });
    kwin.last = () => kwin.animations[kwin.animations.length - 1];
    return kwin;
}

const at = (x, y, width, height) => ({ x, y, width, height });
const close = (a, b) => Math.abs(a - b) < 1e-6;
// Objects made inside the sandbox have its Object prototype, not ours.
const plain = (o) => JSON.parse(JSON.stringify(o));

// ── Tests ───────────────────────────────────────────────────────────────────

const tests = [];
const test = (name, fn) => tests.push({ name, fn });

test('idle: no connections at all until the heads-up', () => {
    const kwin = makeKWin();
    const w = kwin.add({ geometry: at(100, 500, 800, 560) });
    kwin.start();
    assert.equal(w.windowFrameGeometryChanged.slots.length, 0);
    w.moveBy(0, -60);
    assert.equal(kwin.animations.length, 0);
});

test('slides a pushed window from where it was', () => {
    const kwin = makeKWin();
    const w = kwin.add({ geometry: at(100, 500, 800, 560) });
    kwin.start();
    kwin.headsUp();
    w.moveBy(0, -60);
    const a = kwin.last();
    assert.equal(a.window, w);
    assert.equal(a.type, 7);
    assert.equal(a.duration, 220);
    assert.equal(a.curve, 7);
    assert.equal(a.keepAlive, false);
    assert.deepEqual(plain(a.from), { value1: 0, value2: 60 });
    assert.deepEqual(plain(a.to), { value1: 0, value2: 0 });
});

test('slides sideways for a left or right dock', () => {
    const kwin = makeKWin();
    const w = kwin.add({ geometry: at(20, 100, 800, 600) });
    kwin.start();
    kwin.headsUp();
    w.moveBy(60, 0);
    assert.deepEqual(plain(kwin.last().from), { value1: -60, value2: 0 });
});

test('stops listening once the window has passed', () => {
    const kwin = makeKWin();
    const w = kwin.add({ geometry: at(100, 500, 800, 560) });
    kwin.start();
    kwin.headsUp();
    kwin.now += 301;
    w.moveBy(0, -60);              // too late: not ours
    assert.equal(kwin.animations.length, 0);
    assert.equal(w.windowFrameGeometryChanged.slots.length, 0, 'disconnected');
});

test('only listens to windows that can be pushed', () => {
    const kwin = makeKWin();
    const g = at(100, 500, 800, 560);
    const ignored = [
        kwin.add({ geometry: g, fullScreen: true }),
        kwin.add({ geometry: g, minimized: true }),
        kwin.add({ geometry: g, visible: false }),
        kwin.add({ geometry: g, onCurrentDesktop: false }),
        kwin.add({ geometry: g, normalWindow: false }),     // panel, notification, ...
    ];
    const dialog = kwin.add({ geometry: g, normalWindow: false, dialog: true });
    kwin.start();
    kwin.headsUp();
    for (const w of ignored)
        assert.equal(w.windowFrameGeometryChanged.slots.length, 0);
    assert.equal(dialog.windowFrameGeometryChanged.slots.length, 1);
});

test('resizes snap: the app has to redraw anyway', () => {
    const kwin = makeKWin();
    const w = kwin.add({ geometry: at(0, 0, 1920, 1080) });
    kwin.start();
    kwin.headsUp();
    w.setGeometry(at(0, 0, 1920, 1000));     // shrunk, top edge kept
    w.setGeometry(at(0, 40, 1920, 960));     // moved and shrunk
    w.setGeometry(at(20, 40, 1900, 960));
    assert.equal(kwin.animations.length, 0);
});

test('a window the user is dragging is left alone', () => {
    const kwin = makeKWin();
    const w = kwin.add({ geometry: at(100, 500, 800, 560), move: true });
    kwin.start();
    kwin.headsUp();
    w.moveBy(0, -60);
    assert.equal(kwin.animations.length, 0);
});

test('a second heads-up does not double the connections', () => {
    const kwin = makeKWin();
    const w = kwin.add({ geometry: at(100, 500, 800, 560) });
    kwin.start();
    kwin.headsUp();
    kwin.headsUp();
    assert.equal(w.windowFrameGeometryChanged.slots.length, 1);
});

test('reversed mid-slide: carries on from where the window is on screen', () => {
    const kwin = makeKWin();
    const w = kwin.add({ geometry: at(100, 500, 800, 560) });
    kwin.start();
    kwin.headsUp();
    w.moveBy(-20, -60);                    // push: slides from (+20, +60) to 0
    const first = kwin.last();

    kwin.now += 110;                       // half the time in
    kwin.headsUp();
    w.moveBy(20, 60);                      // restore before the push finished
    const second = kwin.last();

    assert.deepEqual(kwin.cancelled, [first.id]);
    // OutCubic at t = 0.5 has covered 87.5 % of the way, so the window is
    // painted 7.5 px below its pushed position, i.e. 52.5 px above where it
    // now really is.
    assert.ok(close(second.from.value2, -60 + 60 * 0.125), JSON.stringify(second.from));
    assert.ok(close(second.from.value1, -20 + 20 * 0.125), JSON.stringify(second.from));
});

test('finished animations are forgotten', () => {
    const kwin = makeKWin();
    const w = kwin.add({ geometry: at(100, 500, 800, 560) });
    kwin.start();
    kwin.headsUp();
    w.moveBy(0, -60);
    kwin.endAnimation(w, kwin.last().id);
    assert.equal(w.kdockPushSlide, null);

    // The next slide starts fresh rather than adding a stale remainder.
    kwin.now += 50;
    kwin.headsUp();
    w.moveBy(0, 60);
    assert.deepEqual(plain(kwin.last().from), { value1: 0, value2: -60 });
    assert.deepEqual(kwin.cancelled, []);
});

test('deleted windows are dropped from the listening list', () => {
    const kwin = makeKWin();
    const w = kwin.add({ geometry: at(100, 500, 800, 560) });
    const other = kwin.add({ geometry: at(100, 500, 800, 560) });
    kwin.start();
    kwin.headsUp();
    kwin.deleteWindow(w);
    kwin.headsUp();                        // must not try to disconnect `w`
    assert.equal(other.windowFrameGeometryChanged.slots.length, 1);
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
