#!/usr/bin/gjs
// Run in an isolated Fcitx session with the Chinese-only patch and schema.
const Gio = imports.gi.Gio;
const GLib = imports.gi.GLib;
const bus = Gio.bus_get_sync(Gio.BusType.SESSION, null);
const dest = 'org.fcitx.Fcitx5';
const iface = 'org.fcitx.Fcitx.InputContext1';
const loop = GLib.MainLoop.new(null, false);
let path;
let commits = [];
let failed = false;
function call(object, intf, method, sig = null, values = null) {
    return bus.call_sync(dest, object, intf, method,
        sig ? new GLib.Variant(sig, values) : null,
        null, Gio.DBusCallFlags.NONE, 5000, null).deep_unpack();
}
function context(method, sig = null, values = null) {
    return call(path, iface, method, sig, values);
}
function delay(ms) {
    return new Promise(resolve => GLib.timeout_add(GLib.PRIORITY_DEFAULT, ms,
        () => { resolve(); return GLib.SOURCE_REMOVE; }));
}
function require(value, message) {
    if (!value) throw new Error(message);
    print(`PASS: ${message}`);
}
function key(sym, state = 0, release = false) {
    return context('ProcessKeyEvent', '(uuubu)', [sym, 0, state, release, 0])[0];
}
function tap(sym, state = 0) {
    key(sym, state);
    key(sym, state, true);
}
async function chinese(label) {
    require(!call('/rime', 'org.fcitx.Fcitx.Rime1', 'IsAsciiMode')[0],
        `${label}: Chinese mode`);
    commits = [];
    for (const ch of 'nihao') tap(ch.charCodeAt(0));
    tap(32);
    await delay(100);
    require(commits.join('') === '你好', `${label}: nihao commits 你好 (got ${JSON.stringify(commits)})`);
}
async function run() {
    path = call('/org/freedesktop/portal/inputmethod',
        'org.fcitx.Fcitx.InputMethod1', 'CreateInputContext', '(a(ss))',
        [[['program', 'yiban-chinese-only-test']]])[0];
    bus.signal_subscribe(dest, iface, 'CommitString', path, null,
        Gio.DBusSignalFlags.NONE, (_a, _b, _c, _d, _e, args) => {
            commits.push(args.deep_unpack()[0]);
        });
    const caps = Math.pow(2, 39) + Math.pow(2, 6);
    context('SetSupportedCapability', '(t)', [caps]);
    context('SetCapability', '(t)', [caps]);
    context('FocusIn');
    call('/controller', 'org.fcitx.Fcitx.Controller1', 'SetCurrentIM', '(s)', ['rime']);
    for (let i = 0; i < 100; i++) {
        if (call('/rime', 'org.fcitx.Fcitx.Rime1', 'GetCurrentSchema')[0] === 'pinyin_simp') break;
        await delay(100);
    }
    require(call('/rime', 'org.fcitx.Fcitx.Rime1', 'GetCurrentSchema')[0] === 'pinyin_simp',
        'compact Simplified Chinese Pinyin selected');
    await chinese('initial');
    for (const [label, sym, state] of [
        ['left Shift', 0xffe1, 0], ['right Shift', 0xffe2, 0],
        ['Caps Lock', 0xffe5, 0], ['Eisu toggle', 0xff30, 0],
        ['Ctrl+Shift+2', 50, 5], ['Ctrl+Shift+at', 64, 5],
    ]) {
        tap(sym, state);
        await chinese(label);
    }
    commits = [];
    for (const ch of 'ni') tap(ch.charCodeAt(0));
    tap(0xffe1);
    for (const ch of 'hao') tap(ch.charCodeAt(0));
    tap(32);
    await delay(100);
    require(commits.join('') === '你好', 'Shift during composition keeps Chinese preedit');
    call('/rime', 'org.fcitx.Fcitx.Rime1', 'SetAsciiMode', '(b)', [true]);
    await chinese('external Latin request');
    context('FocusOut');
    context('FocusIn');
    await chinese('refocus');
    call('/controller', 'org.fcitx.Fcitx.Controller1', 'SetCurrentIM', '(s)', ['keyboard-us']);
    require(call('/controller', 'org.fcitx.Fcitx.Controller1', 'CurrentInputMethod')[0] === 'keyboard-us',
        'separate English keyboard remains available');
    require(!key(97), 'English keyboard passes Latin letters to application');
    call('/controller', 'org.fcitx.Fcitx.Controller1', 'SetCurrentIM', '(s)', ['rime']);
    await chinese('return from English keyboard');
}
run().catch(error => { printerr(error.message); printerr(error.stack); failed = true; })
    .finally(() => { if (path) context('DestroyIC'); loop.quit(); });
loop.run();
if (failed) imports.system.exit(1);
