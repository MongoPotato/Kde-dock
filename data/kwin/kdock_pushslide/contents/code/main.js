// kdock_pushslide — KWin effect that makes the windows KDock pushes aside
// slide instead of jump (issue #12, phase 2).
//
// The window pusher script (data/kwin/windowpusher.js) still moves each
// window exactly once per reveal or hide, by setting its real geometry.
// This effect only changes how that one move LOOKS: the moment the geometry
// changes, it paints the window back where it was and slides that offset
// to zero over the dock's own slide duration. That is a GPU translation of
// the window's existing contents: the application doesn't redraw, and KWin
// repaints only the strip the window sweeps across.
//
// Effects can't talk D-Bus, so the effect doesn't know which windows the
// script is about to move. What it gets is a heads-up: right before KDock
// publishes a new state to the script, it asks KWin to reconfigure this
// effect (org.kde.KWin /Effects reconfigureEffect). D-Bus keeps messages
// from one sender in order, so that arrives before the script moves
// anything. Only then does the effect start listening, briefly, for
// geometry changes. Otherwise it is completely idle: no per-window
// connections, nothing that runs while you drag windows around.
//
// Only pure moves slide. A window the script had to shrink changes size,
// and its application has to draw the new size anyway, so it just snaps.

var pushSlide = {
    // main.qml slides the dock in and out over 220 ms with OutCubic easing.
    // The windows match it rather than Plasma's animation speed, so they
    // stay level with the dock.
    duration: 220,

    // How long after the heads-up a geometry change still counts as one of
    // ours. The script normally moves its windows within a few
    // milliseconds; anything later is somebody else's move.
    listenMs: 300,

    listening: [],     // windows whose frameGeometryChanged we're connected to
    listenUntil: 0,

    // Ease-out cubic, the same curve the animation itself uses: how far an
    // animation started at `start` has got by `now`, from 0 to 1.
    eased: function (start, now) {
        var t = Math.min(1, Math.max(0, (now - start) / pushSlide.duration));
        return 1 - Math.pow(1 - t, 3);
    },

    isCandidate: function (w) {
        return (w.normalWindow || w.dialog) && w.visible && !w.minimized
            && !w.fullScreen && w.onCurrentDesktop;
    },

    stopListening: function () {
        for (var i = 0; i < pushSlide.listening.length; i++) {
            try {
                pushSlide.listening[i].windowFrameGeometryChanged.disconnect(
                    pushSlide.onGeometryChanged);
            } catch (e) {
                // The window is already gone.
            }
        }
        pushSlide.listening = [];
    },

    // The heads-up from KDock: the script is about to move windows.
    startListening: function () {
        pushSlide.stopListening();
        pushSlide.listenUntil = Date.now() + pushSlide.listenMs;
        var windows = effects.stackingOrder;
        for (var i = 0; i < windows.length; i++) {
            var w = windows[i];
            if (!pushSlide.isCandidate(w) || !w.windowFrameGeometryChanged)
                continue;
            w.windowFrameGeometryChanged.connect(pushSlide.onGeometryChanged);
            pushSlide.listening.push(w);
        }
    },

    onGeometryChanged: function (w, oldGeometry) {
        // Effects have no timers, so the listening window closes on the
        // first change that arrives after it has expired.
        if (Date.now() > pushSlide.listenUntil) {
            pushSlide.stopListening();
            return;
        }
        if (w.move || w.resize)        // the user is dragging it
            return;
        var now = w.geometry;
        if (Math.abs(now.width - oldGeometry.width) >= 1
            || Math.abs(now.height - oldGeometry.height) >= 1)
            return;
        var dx = oldGeometry.x - now.x;
        var dy = oldGeometry.y - now.y;
        if (Math.abs(dx) < 1 && Math.abs(dy) < 1)
            return;
        pushSlide.slide(w, dx, dy);
    },

    // Paint `w` offset by (dx, dy) from where it really is, then slide that
    // offset to zero.
    slide: function (w, dx, dy) {
        var now = Date.now();
        var running = w.kdockPushSlide;
        if (running) {
            // Reversed mid-slide (the dock started hiding before it was all
            // the way in): carry on from where the window is on screen now,
            // not from where it was headed.
            var left = 1 - pushSlide.eased(running.start, now);
            dx += running.dx * left;
            dy += running.dy * left;
            cancel(running.animation);
        }
        w.kdockPushSlide = {
            start: now,
            dx: dx,
            dy: dy,
            animation: animate({
                window: w,
                duration: pushSlide.duration,
                curve: QEasingCurve.OutCubic,
                keepAlive: false,
                type: Effect.Translation,
                from: { value1: dx, value2: dy },
                to: { value1: 0, value2: 0 },
            }),
        };
    },

    onAnimationEnded: function (w, animationId) {
        var running = w.kdockPushSlide;
        if (running && running.animation.indexOf(animationId) !== -1)
            w.kdockPushSlide = null;
    },

    init: function () {
        effect.configChanged.connect(pushSlide.startListening);
        effect.animationEnded.connect(pushSlide.onAnimationEnded);
        effects.windowDeleted.connect(function (w) {
            var i = pushSlide.listening.indexOf(w);
            if (i !== -1)
                pushSlide.listening.splice(i, 1);
        });
    },
};

pushSlide.init();
