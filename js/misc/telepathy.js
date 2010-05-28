/* -*- mode: js2; js2-basic-offset: 4; indent-tabs-mode: nil -*- */

const DBus = imports.dbus;
const Tp = imports.gi.TelepathyGLib;

// D-Bus utils; should eventually move to gjs.
// https://bugzilla.gnome.org/show_bug.cgi?id=610859

function makeProxyClass(iface) {
    let constructor = function() { this._init.apply(this, arguments); };

    constructor.prototype._init = function(bus, name, path) {
        bus.proxifyObject(this, name, path);
    };

    DBus.proxifyPrototype(constructor.prototype, iface);
    return constructor;
}

// Telepathy D-Bus interface definitions. Note that most of these are
// incomplete, and only cover the methods/properties/signals that
// we're currently using.

const ChannelTextIface = {
    name: Tp.IFACE_CHANNEL_TYPE_TEXT,
    methods: [
        { name: 'ListPendingMessages',
          inSignature: 'b',
          outSignature: 'a(uuuuus)'
        },
        { name: 'Send',
          inSignature: 'us',
          outSignature: ''
        }
    ],
    signals: [
        { name: 'Received',
          inSignature: 'uuuuus' }
    ]
};
let ChannelText = makeProxyClass(ChannelTextIface);

const ChannelDispatcherIface = {
    name: Tp.IFACE_CHANNEL_DISPATCHER,
    methods: [
        { name: 'EnsureChannel',
          inSignature: 'oa{sv}xs',
          outSignature: 'o' }
    ]
};
let ChannelDispatcher = makeProxyClass(ChannelDispatcherIface);

const ChannelRequestIface = {
    name: Tp.IFACE_CHANNEL_REQUEST,
    methods: [
        { name: 'Proceed',
          inSignature: '',
          outSignature: '' }
    ],
    signals: [
        { name: 'Failed',
          signature: 'ss' },
        { name: 'Succeeded',
          signature: '' }
    ]
};
let ChannelRequest = makeProxyClass(ChannelRequestIface);
