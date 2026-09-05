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
        ['program', 'bilingual-ime-e2e'],
    ]]), null, Gio.DBusCallFlags.NONE, 5000, null).deep_unpack();
const contextPath = created[0];
let translated = false;
let cleanedUp = false;
let phase = 0;
let firstTranslation = '';
let joinedTranslation = '';
let backspaceSent = false;
const commits = [];
const loop = GLib.MainLoop.new(null, false);

function cleanup() {
    if (cleanedUp)
        return;
    cleanedUp = true;
    call(contextPath, 'FocusOut');
    call(contextPath, 'DestroyIC');
}

bus.signal_subscribe(destination, contextInterface, 'CommitString', contextPath,
    null, Gio.DBusSignalFlags.NONE, (_connection, _sender, _path, _interface,
        _signal, parameters) => {
        const committed = parameters.deep_unpack()[0];
        commits.push(committed);
        print(`COMMIT ${committed}`);
    });

bus.signal_subscribe(destination, contextInterface, 'UpdateClientSideUI',
    contextPath, null, Gio.DBusSignalFlags.NONE,
    (_connection, _sender, _path, _interface, _signal, parameters) => {
        const fields = parameters.deep_unpack();
        const auxDown = fields[3].map(piece => piece[0]).join('');
        if (auxDown.includes('EN: ')) {
            print(`AUXDOWN ${auxDown}`);
            if (phase === 0) {
                firstTranslation = auxDown;
                phase = 1;
                GLib.timeout_add(GLib.PRIORITY_DEFAULT, 50, () => {
                    backspaceSent = true;
                    key(0xff08); // BackSpace reopens the sentence.
                    return GLib.SOURCE_REMOVE;
                });
            } else if (phase === 1 && backspaceSent) {
                print(`REOPENED ${auxDown}`);
                phase = 2;
                GLib.timeout_add(GLib.PRIORITY_DEFAULT, 50, () => {
                    for (const character of 'shijie')
                        key(character.charCodeAt(0));
                    key(32);
                    return GLib.SOURCE_REMOVE;
                });
            } else if (phase === 2 && commits.includes('世界') &&
                       auxDown !== firstTranslation) {
                joinedTranslation = auxDown;
                print(`JOINED ${auxDown}`);
                phase = 3;
                GLib.idle_add(GLib.PRIORITY_DEFAULT, () => {
                    call(contextPath, 'SetSurroundingText', '(suu)',
                        ['第一句 第二句', 4, 4]);
                    key(0xff08);
                    return GLib.SOURCE_REMOVE;
                });
            } else if (phase === 3 && auxDown !== joinedTranslation) {
                print(`MIDDLE_BOUNDARY_REMOVED ${auxDown}`);
                translated = true;
                cleanup();
                loop.quit();
            }
        }
    });

const capabilities = Math.pow(2, 39) + Math.pow(2, 6);
call(contextPath, 'SetSupportedCapability', '(t)', [capabilities]);
call(contextPath, 'SetCapability', '(t)', [capabilities]);
call(contextPath, 'FocusIn');
bus.call_sync(destination, '/controller', 'org.fcitx.Fcitx.Controller1',
    'SetCurrentIM', new GLib.Variant('(s)', ['rime']), null,
    Gio.DBusCallFlags.NONE, 5000, null);

// Rime is loaded lazily and can take a few seconds on a cold start.
GLib.usleep(4000000);

function key(keyval) {
    call(contextPath, 'ProcessKeyEvent', '(uuubu)', [keyval, 0, 0, false, 0]);
}

for (const character of 'nihao')
    key(character.charCodeAt(0));
key(32);

GLib.timeout_add(GLib.PRIORITY_DEFAULT, 20000, () => {
    cleanup();
    loop.quit();
    return GLib.SOURCE_REMOVE;
});
loop.run();

if (!translated) {
    printerr('No translation appeared after deleting a middle boundary');
    imports.system.exit(1);
}
if (!commits.includes('你好') || !commits.includes('世界')) {
    printerr(`Unexpected commits: ${commits.join(', ')}`);
    imports.system.exit(1);
}
