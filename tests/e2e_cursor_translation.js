#!/usr/bin/gjs
// Run on an isolated Fcitx D-Bus session with translator/daemon.py --fake.
const Gio = imports.gi.Gio;
const GLib = imports.gi.GLib;
const bus = Gio.bus_get_sync(Gio.BusType.SESSION, null);
const destination = 'org.fcitx.Fcitx5';
const iface = 'org.fcitx.Fcitx.InputContext1';
const loop = GLib.MainLoop.new(null, false);
let contextPath;
let aux = '';
let failed = false;
function call(method, signature = null, values = null) {
    return bus.call_sync(destination, contextPath, iface, method,
        signature ? new GLib.Variant(signature, values) : null,
        null, Gio.DBusCallFlags.NONE, 5000, null);
}
function delay(ms) {
    return new Promise(resolve => GLib.timeout_add(GLib.PRIORITY_DEFAULT, ms,
        () => { resolve(); return GLib.SOURCE_REMOVE; }));
}
function snapshot(text, caret, anchor = caret) {
    call('SetSurroundingText', '(suu)', [text, caret, anchor]);
}
function key(sym) {
    call('ProcessKeyEvent', '(uuubu)', [sym, 0, 0, false, 0]);
}
function require(value, message) {
    if (!value) throw new Error(`${message}; AuxDown=${aux}`);
    print(`PASS: ${message}`);
}
async function expectTranslation(text) {
    for (let i = 0; i < 100 && aux !== `EN: Translation: ${text}`; i++)
        await delay(20);
    require(aux === `EN: Translation: ${text}`, `translation follows ${text}`);
}
async function run() {
    const created = bus.call_sync(destination, '/org/freedesktop/portal/inputmethod',
        'org.fcitx.Fcitx.InputMethod1', 'CreateInputContext',
        new GLib.Variant('(a(ss))', [[['program', 'bilingual-cursor-test']]]),
        null, Gio.DBusCallFlags.NONE, 5000, null).deep_unpack();
    contextPath = created[0];
    bus.signal_subscribe(destination, iface, 'UpdateClientSideUI', contextPath,
        null, Gio.DBusSignalFlags.NONE, (_c, _s, _p, _i, _n, parameters) => {
            aux = parameters.deep_unpack()[3].map(piece => piece[0]).join('');
        });
    const capabilities = Math.pow(2, 39) + Math.pow(2, 6);
    call('SetSupportedCapability', '(t)', [capabilities]);
    call('SetCapability', '(t)', [capabilities]);
    call('FocusIn');
    bus.call_sync(destination, '/controller', 'org.fcitx.Fcitx.Controller1',
        'SetCurrentIM', new GLib.Variant('(s)', ['rime']), null,
        Gio.DBusCallFlags.NONE, 5000, null);
    snapshot('我喜欢苹果', 5);
    if (GLib.getenv('YIBAN_TEST_DISABLED') === '1') {
        await delay(700);
        require(!aux.includes('EN: '), 'Enabled=False disables sentence translation');
        return;
    }
    await expectTranslation('我喜欢苹果');
    key(32);
    await delay(60);
    require(!aux.includes('EN: '), 'Space hides immediately');
    snapshot('我喜欢苹果 ', 6);
    await delay(350);
    require(!aux.includes('EN: '), 'late reply does not reopen blank sentence');
    snapshot('我喜欢苹果 ', 2);
    await delay(40);
    require(aux === 'EN: Translation: 我喜欢苹果', 'return restores cached sentence');
    snapshot('我很喜欢苹果 ', 2);
    await expectTranslation('我很喜欢苹果');
    snapshot('我很喜欢苹果 ', 7);
    await delay(40);
    require(!aux.includes('EN: '), 'moving into blank suffix hides translation');
    snapshot('我很喜欢苹果 新句子', 10);
    await expectTranslation('新句子');
    snapshot('我很喜欢苹果 新句子', 1);
    await expectTranslation('我很喜欢苹果');
    snapshot('我很喜欢苹果 新句子', 1, 3);
    await delay(40);
    require(!aux.includes('EN: '), 'selection hides translation');
    snapshot('甲。乙', 1);
    await expectTranslation('甲');
    key(0xffff); // Delete
    snapshot('甲乙', 1);
    await expectTranslation('甲乙');
    key(0xff53); // Right: hide until authoritative cursor update
    await delay(40);
    require(!aux.includes('EN: '), 'navigation immediately hides old sentence');
    snapshot('甲乙', 2);
    await expectTranslation('甲乙');
    snapshot('', 0);
    for (const ch of 'nihao') key(ch.charCodeAt(0));
    key(49); // Select with 1: keep the unit active.
    await expectTranslation('你好');
    snapshot('你好', 2);
    key(32);
    await delay(350);
    require(!aux.includes('EN: '), 'Space after a real Rime commit hides the hint');
    snapshot('你好 ', 3);
    for (const ch of 'shijie') key(ch.charCodeAt(0));
    key(49);
    await expectTranslation('世界');
    snapshot('你好 世界', 5);
    snapshot('你好 世界', 1);
    await expectTranslation('你好');
    snapshot('', 0);
    for (const ch of 'nihao') key(ch.charCodeAt(0));
    key(32); // Selecting the candidate with Space also finishes the unit.
    await delay(350);
    require(!aux.includes('EN: '), 'Space candidate selection stays hidden');
    snapshot('你好', 2);
    await delay(40);
    require(!aux.includes('EN: '), 'commit acknowledgement cannot reopen the finished unit');
    snapshot('你好', 1);
    await expectTranslation('你好');
    call('FocusOut');
    await delay(40);
    require(!aux.includes('EN: '), 'focus out clears translation');
}
run().catch(error => { printerr(error.message); printerr(error.stack); failed = true; }).finally(() => {
    if (contextPath) call('DestroyIC');
    loop.quit();
});
loop.run();
if (failed) imports.system.exit(1);
