#!/usr/bin/gjs

const Gio = imports.gi.Gio;
const GLib = imports.gi.GLib;

const bus = Gio.bus_get_sync(Gio.BusType.SESSION, null);
const destination = 'org.fcitx.Fcitx5';
const inputMethodPath = '/org/freedesktop/portal/inputmethod';
const inputMethodInterface = 'org.fcitx.Fcitx.InputMethod1';
const contextInterface = 'org.fcitx.Fcitx.InputContext1';

function call(path, method, signature = null, values = null) {
    const parameters = signature === null ? null : new GLib.Variant(signature, values);
    return bus.call_sync(destination, path, contextInterface, method, parameters,
        null, Gio.DBusCallFlags.NONE, 5000, null);
}

const created = bus.call_sync(destination, inputMethodPath, inputMethodInterface,
    'CreateInputContext', new GLib.Variant('(a(ss))', [[
        ['program', 'bilingual-ime-rime-ice-e2e'],
    ]]), null, Gio.DBusCallFlags.NONE, 5000, null).deep_unpack();
const contextPath = created[0];
const cases = [
    ['tishici', '提示词'],
    ['xianyanbao', '显眼包'],
    ['jushenzhineng', '具身智能'],
];
const loop = GLib.MainLoop.new(null, false);
let index = 0;
let failed = false;

function cleanup() {
    call(contextPath, 'FocusOut');
    call(contextPath, 'DestroyIC');
}

function key(keyval) {
    call(contextPath, 'ProcessKeyEvent', '(uuubu)', [keyval, 0, 0, false, 0]);
}

function typeNext() {
    if (index === cases.length) {
        cleanup();
        loop.quit();
        return GLib.SOURCE_REMOVE;
    }
    for (const character of cases[index][0])
        key(character.charCodeAt(0));
    key(32);
    return GLib.SOURCE_REMOVE;
}

bus.signal_subscribe(destination, contextInterface, 'CommitString', contextPath,
    null, Gio.DBusSignalFlags.NONE, (_connection, _sender, _path, _interface,
        _signal, parameters) => {
        const committed = parameters.deep_unpack()[0];
        const expected = cases[index][1];
        print(`COMMIT ${committed}`);
        if (committed !== expected) {
            printerr(`Expected ${expected}, got ${committed}`);
            failed = true;
            cleanup();
            loop.quit();
            return;
        }
        index++;
        GLib.timeout_add(GLib.PRIORITY_DEFAULT, 100, typeNext);
    });

const capabilities = Math.pow(2, 39) + Math.pow(2, 6);
call(contextPath, 'SetSupportedCapability', '(t)', [capabilities]);
call(contextPath, 'SetCapability', '(t)', [capabilities]);
call(contextPath, 'FocusIn');
bus.call_sync(destination, '/controller', 'org.fcitx.Fcitx.Controller1',
    'SetCurrentIM', new GLib.Variant('(s)', ['rime']), null,
    Gio.DBusCallFlags.NONE, 5000, null);

GLib.timeout_add(GLib.PRIORITY_DEFAULT, 1000, typeNext);
GLib.timeout_add(GLib.PRIORITY_DEFAULT, 15000, () => {
    printerr(`Timed out after ${index} successful cases`);
    failed = true;
    cleanup();
    loop.quit();
    return GLib.SOURCE_REMOVE;
});
loop.run();

if (failed || index !== cases.length)
    imports.system.exit(1);
