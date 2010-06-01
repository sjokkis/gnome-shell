/* -*- mode: js2; js2-basic-offset: 4; indent-tabs-mode: nil -*- */

const Clutter = imports.gi.Clutter;
const DBus = imports.dbus;
const GLib = imports.gi.GLib;
const Lang = imports.lang;
const Pango = imports.gi.Pango;
const Shell = imports.gi.Shell;
const Signals = imports.signals;
const St = imports.gi.St;
const Gettext = imports.gettext.domain('gnome-shell');
const _ = Gettext.gettext;
const Tp = imports.gi.TelepathyGLib;

const Main = imports.ui.main;
const MessageTray = imports.ui.messageTray;
const Telepathy = imports.misc.telepathy;
const Tweener = imports.ui.tweener;

let channelDispatcher = null;
let missedCalls = null;

let contact_features = [Tp.ContactFeature.ALIAS, 
                        Tp.ContactFeature.AVATAR_DATA, 
                        Tp.ContactFeature.PRESENCE]
let contact_features_len = contact_features.length

// See Notification.appendMessage
const SCROLLBACK_RECENT_TIME = 15 * 60; // 15 minutes
const SCROLLBACK_RECENT_LENGTH = 20;
const SCROLLBACK_IDLE_LENGTH = 5;

// Fade time for entries in the missed calls notification
const CALL_FADE_TIME = 0.5;

// This is GNOME Shell's implementation of the Telepathy 'Client'
// interface. Specifically, the shell is a Telepathy 'Observer', which
// lets us see messages even if they belong to another app (eg,
// Empathy).

function Client() {
    this._init();
};

Client.prototype = {
    _init : function() {
        this._sources = {};

        channelDispatcher = new Telepathy.ChannelDispatcher(DBus.session,
                                                            Tp.CHANNEL_DISPATCHER_BUS_NAME,
                                                            Tp.CHANNEL_DISPATCHER_OBJECT_PATH);

        // Set up a SimpleObserver, which will call ObserveChannels whenever a
        // channel matching its filters is detected.
        // The second argument, recover, means ObserveChannels will be run 
        // for any existing channel as well.
        let dbus = Tp.DBusDaemon.dup();
        this.observer = Tp.SimpleObserver.new(dbus, true, "GnomeShell", true,
                Lang.bind(this, this.ObserveChannels));

        // Single-user text-based chats
        this.observer.add_observer_filter({
            'org.freedesktop.Telepathy.Channel.ChannelType': Tp.IFACE_CHANNEL_TYPE_TEXT,
            'org.freedesktop.Telepathy.Channel.TargetHandleType': Tp.HandleType.CONTACT,
        });

        // Single-user audio/video chats
        this.observer.add_observer_filter({
            'org.freedesktop.Telepathy.Channel.ChannelType': Tp.IFACE_CHANNEL_TYPE_STREAMED_MEDIA,
            'org.freedesktop.Telepathy.Channel.TargetHandleType': Tp.HandleType.CONTACT,
        });

        try {
            this.observer.register();
        } catch (e) {
            throw new Error("Couldn't register SimpleObserver. Error: \n" + e);
        }
    },

    ObserveChannels: function(observer, account, conn, channels,
                              dispatch_op, requests, context, user_data) {
        let connPath = conn.get_object_path();
        let connName = conn.get_bus_name();

        for (let i in channels) {
            let chan = channels[i];
            let [targetHandle, targetHandleType] = chan.get_handle();

            if (this._sources[chan.get_object_path()]) {
                continue;
            }

            Shell.Global.get_tp_contacts(
                    conn,
                    1, [targetHandle],
                    contact_features_len, contact_features,
                    Lang.bind(this, 
                        function(connection, contacts, failed) {
                            let contact = contacts[0];
                            let chanType = chan.get_channel_type();
                            let source = null;

                            switch(chanType)
                            {
                            case Tp.IFACE_CHANNEL_TYPE_TEXT:
                                source = new TextSource(account, conn, chan, contact);
                                break;
                            case Tp.IFACE_CHANNEL_TYPE_STREAMED_MEDIA:
                                source = new AVSource(account, conn, chan, contact);
                                source._missedId = source.connect('missed', Lang.bind(this, this._callMissed));
                                break;
                            default:
                                return;
                            }

                            source._destroyId = source.connect('destroy', Lang.bind(this, this._removeSource));
                            this._sources[chan.get_object_path()] = source;
                        }), null);
        }
 
        // Allow dbus method to return
        context.accept();
    },

    _callMissed: function(source) {
        // create source if necessary
        if (!missedCalls) {
            missedCalls = new MissedCallsSource();
            missedCalls._destroyId = missedCalls.connect('destroy', Lang.bind(this, this._removeMissedCalls));
        }

        missedCalls.addCall(source.contact);
    },

    _removeMissedCalls: function() {
        missedCalls = null;
    },

    _removeSource: function(source) {
        delete this._sources[source.object_path];
    },

    get Interfaces() {
        return [ Tp.IFACE_CLIENT_OBSERVER ]
    },
};


function Source(account, conn, chan, contact) {
    this._init(account, conn, chan, contact);
};

Source.prototype = {
    __proto__: MessageTray.Source.prototype,

    _init: function(account, conn, chan, contact) {
        MessageTray.Source.prototype._init.call(this, chan.get_object_path());

        this._account = account;
        this._conn = conn;
        this._chan = chan;
        this._contact = contact;
        this._notification = null;
        this._title = contact.get_alias();

        chan._invalidatedId = chan.connect('invalidated', Lang.bind(this, this._removeChannel));
        contact._presenceChangedId = contact.connect('presence-changed', Lang.bind(this, this._presenceChanged));

        // create notification and set title
        this._presenceChanged(contact, contact.get_presence_type(),
                contact.get_presence_status(),
                contact.get_presence_message());
    },

    createIcon: function(size) {
        let iconBox = new St.Bin({ style_class: 'avatar-box' });
        iconBox._size = size;
        let textureCache = St.TextureCache.get_default();

        let file = this._contact.get_avatar_file();
        if (file) {
            let uri = file.get_uri();
            iconBox.child = textureCache.load_uri_sync(St.TextureCachePolicy.FOREVER,
                                                       uri, size, size)
        } else {
            iconBox.child = textureCache.load_icon_name('stock_person', iconBox._size);
        }

        return iconBox;
    },

    _presenceChanged: function(contact, type, status, message) {
        let name = contact.get_alias();
        let title;

        switch(type) {
            case Tp.ConnectionPresenceType.AVAILABLE:
                title = _("%s").format(name);
                break;
            case Tp.ConnectionPresenceType.AWAY:
                title = _("%s (away)").format(name);
                break;
            case Tp.ConnectionPresenceType.EXTENDED_AWAY:
                title = _("%s (away)").format(name);
                break;
            case Tp.ConnectionPresenceType.HIDDEN:
                title = _("%s (hidden)").format(name);
                break;
            case Tp.ConnectionPresenceType.BUSY:
                title = _("%s (busy)").format(name);
                break;
            case Tp.ConnectionPresenceType.OFFLINE:
                title = _("%s (offline)").format(name);
                break;
            default:
                // Unrecognized presence type. Abort without changing anything
                print("unrecognized type");
                return;
        }

        if (message !== "") {
            title += ' <i>(' + GLib.markup_escape_text(message, -1) + ')</i>';
        }

        this._title = title;
        this._ensureNotification();
        this._notification.update(title);
    },

    _ensureNotification: function() {
    },

    _gotChannelRequest: function (chanReqPath, ex) {
        if (ex) {
            log ('EnsureChannelRemote failed? ' + ex);
            return;
        }

        let chanReq = new Telepathy.ChannelRequest(DBus.session, Tp.CHANNEL_DISPATCHER_BUS_NAME, chanReqPath);
        chanReq.ProceedRemote();
    },

    _removeChannel: function(chan) {
        chan.disconnect(chan._invalidatedId);

        this._removeSource();
    },

    _removeSource: function() {
        this._contact.disconnect(this._contact._presenceChangedId);
        this.destroy();
    },

    get account() {
        return this._account;
    },

    get connection() {
        return this._conn;
    },

    get channel() {
        return this._chan;
    },

    get contact() {
        return this._contact;
    },

    get object_path() {
        return this._chan.get_object_path()
    },
};


function TextSource(account, conn, chan, contact) {
    this._init(account, conn, chan, contact);
};

TextSource.prototype = {
    __proto__: Source.prototype,
    
    _init: function(account, conn, chan, contact) {
        Source.prototype._init.call(this, account, conn, chan, contact);

        this._channelText = new Telepathy.ChannelText(DBus.session, conn.get_bus_name(), chan.get_object_path());
        this._channelText._receivedId = this._channelText.connect('Received', Lang.bind(this, this._messageReceived));
        this._channelText.ListPendingMessagesRemote(false, Lang.bind(this, this._gotPendingMessages));
    },

    clicked: function() {
        channelDispatcher.EnsureChannelRemote(this._account.get_object_path(),
                                              { 'org.freedesktop.Telepathy.Channel.ChannelType': Tp.IFACE_CHANNEL_TYPE_TEXT,
                                                'org.freedesktop.Telepathy.Channel.TargetHandle': this._contact.get_handle(),
                                                'org.freedesktop.Telepathy.Channel.TargetHandleType': Tp.HandleType.CONTACT },
                                              global.get_current_time(),
                                              '',
                                              Lang.bind(this, this._gotChannelRequest));

        MessageTray.Source.prototype.clicked.call(this);
    },

    respond: function(text) {
        this._channelText.SendRemote(Tp.ChannelTextMessageType.NORMAL, text);
    },
             
    _ensureNotification: function() {
        if (!Main.messageTray.contains(this))
            Main.messageTray.add(this);

        if (!this._notification) {
            let id = this._chan.get_object_path();

            this._notification = new TextNotification(id, this, this._title);
        }
    },

    _gotPendingMessages: function(msgs, err) {
        if (!msgs)
            return;

        for (let i = 0; i < msgs.length; i++)
            this._messageReceived.apply(this, [this._chan].concat(msgs[i]));
    },

    _messageReceived: function(channel, id, timestamp, sender,
                               type, flags, text) {
        this._ensureNotification();
        this._notification.appendMessage(text);
        this.notify(this._notification);
    },

    _removeChannel: function(chan) {
        chan.disconnect(chan._invalidatedId);
        this._channelText.disconnect(this._channelText._receivedId);

        this._removeSource();
    },
};


function TextNotification(id, source, title) {
    this._init(id, source, title);
}

TextNotification.prototype = {
    __proto__: MessageTray.Notification.prototype,

    _init: function(id, source, title) {
        MessageTray.Notification.prototype._init.call(this, id, source, title);
        this._title = title;

        this.actor.connect('button-press-event', Lang.bind(this, this._onButtonPress));

        this._responseEntry = new St.Entry({ style_class: 'chat-response' });
        this._responseEntry.clutter_text.connect('key-focus-in', Lang.bind(this, this._onEntryFocused));
        this._responseEntry.clutter_text.connect('activate', Lang.bind(this, this._onEntryActivated));
        this.setActionArea(this._responseEntry);

        this._history = [];
    },

    appendMessage: function(text, asTitle) {
        if (asTitle)
            this.update(text);
        else
            this.update(this._title, text);
        this._append(text, 'chat-received');
    },

    _append: function(text, style) {
        let body = this.addBody(text);
        body.add_style_class_name(style);
        this.scrollTo(St.Side.BOTTOM);

        let now = new Date().getTime() / 1000;
        this._history.unshift({ actor: body, time: now });

        if (this._history.length > 1) {
            // Keep the scrollback from growing too long. If the most
            // recent message (before the one we just added) is within
            // SCROLLBACK_RECENT_TIME, we will keep
            // SCROLLBACK_RECENT_LENGTH previous messages. Otherwise
            // we'll keep SCROLLBACK_IDLE_LENGTH messages.

            let lastMessageTime = this._history[1].time;
            let maxLength = (lastMessageTime < now - SCROLLBACK_RECENT_TIME) ?
                SCROLLBACK_IDLE_LENGTH : SCROLLBACK_RECENT_LENGTH;
            if (this._history.length > maxLength) {
                let expired = this._history.splice(maxLength);
                for (let i = 0; i < expired.length; i++)
                    expired[i].actor.destroy();
            }
        }
    },

    _onButtonPress: function(notification, event) {
        if (!this._active)
            return false;

        let source = event.get_source ();
        while (source) {
            if (source == notification)
                return false;
            source = source.get_parent();
        }

        // @source is outside @notification, which has to mean that
        // we have a pointer grab, and the user clicked outside the
        // notification, so we should deactivate.
        this._deactivate();
        return true;
    },

    _onEntryFocused: function() {
        if (this._active)
            return;

        if (!Main.pushModal(this.actor))
            return;
        Clutter.grab_pointer(this.actor);

        this._active = true;
        Main.messageTray.lock();
    },

    _onEntryActivated: function() {
        let text = this._responseEntry.get_text();
        if (text == '') {
            this._deactivate();
            return;
        }

        this._responseEntry.set_text('');
        this._append(text, 'chat-sent');
        this.source.respond(text);
    },

    _deactivate: function() {
        if (this._active) {
            Clutter.ungrab_pointer(this.actor);
            Main.popModal(this.actor);
            global.stage.set_key_focus(null);

            // We have to do this after calling popModal(), because
            // that will return the keyboard focus to
            // this._responseEntry (because that's where it was when
            // pushModal() was called), which will cause
            // _onEntryFocused() to be called again, but we don't want
            // it to do anything.
            this._active = false;

            Main.messageTray.unlock();
        }
    }
};


function AVSource(account, conn, chan, contact) {
    this._init(account, conn, chan, contact);
};

AVSource.prototype = {
    __proto__: Source.prototype,

    _init: function(account, conn, chan, contact) {
        Source.prototype._init.call(this, account, conn, chan, contact);

        this.notify(this._notification);
    },

    clicked: function() {
        this._removeSource();
        MessageTray.Source.prototype.clicked.call(this);
    },

    createIcon: function(size) {
        let iconBox = new St.Bin({ style_class: 'avatar-box' });
        iconBox._size = size;
        let textureCache = St.TextureCache.get_default();
        iconBox.child = textureCache.load_icon_name('call-start', iconBox._size);

        return iconBox;
    },

    _ensureNotification: function() {
        if (!Main.messageTray.contains(this))
            Main.messageTray.add(this);

        if (!this._notification) {
            let id = this._chan.get_object_path();
            let title = this._contact.get_alias();

            this._notification = new MessageTray.Notification(id, this, title, "Incoming call");
        }
    },

    _removeChannel: function(chan) {
        chan.disconnect(chan._invalidatedId);
        this._removeSource();
    },

    _removeSource: function() {
        this.emit('missed');
        this.destroy();
    },
};


function MissedCallsSource() {
    this._init();
}

MissedCallsSource.prototype = {
    __proto__: MessageTray.Source.prototype,

    _init: function() {
        MessageTray.Source.prototype._init.call(this, "MissedCallsSource");

        this._ensureNotification();
    },

    addCall: function(contact) {
        let time = new Date();

        this._ensureNotification();
        this._notification.addCall(time, contact);
        this.notify(this._notification);
    },

    clicked: function() {
        this._removeSource();
        MessageTray.Source.prototype.clicked.call(this);
    },

    createIcon: function(size) {
        let iconBox = new St.Bin({ style_class: 'avatar-box' });
        iconBox._size = size;
        let textureCache = St.TextureCache.get_default();
        iconBox.child = textureCache.load_icon_name('appointment-missed', iconBox._size);

        return iconBox;
    },
    
    _ensureNotification: function() {
        if (!Main.messageTray.contains(this))
            Main.messageTray.add(this);

        if (!this._notification) {
            this._notification = new MissedCallsNotification("MissedCallsList", this, "Missed calls");
            this._notification._destroyId = this._notification.connect(
                    'destroy', Lang.bind(this, this._removeNotification));
        }
    },
    
    _removeNotification: function() {
        this._removeSource();
    },

    _removeSource: function() {
        this.destroy();
    },
};

function MissedCallsNotification(id, source, title) {
    this._init(id, source, title);
}

MissedCallsNotification.prototype = {
    __proto__: MessageTray.Notification.prototype,
     
    _init: function(id, source, title) {
        MessageTray.Notification.prototype._init.call(this, id, source, title);
        this._calls = [];
    },

    addCall: function(time, contact) {
        let formatted_time = time.getHours() + ":" + time.getMinutes();
        let text = "[" + formatted_time + "] " + contact.get_alias();

        let id = contact.get_identifier();
        let count = 1;

        let len = this._calls.length;
        for (let i = 0; i < len; i++) {
            if (this._calls[i]._contactId == id) {
                count = this._cals[i].callCount;
            }
        }

        if (count > 1) {
            text += " (" + count + ")";
        }

        let callBox = new St.BoxLayout({ style_class: 'call-entry',
                                         reactive: true,
                                         track_hover: true });
        callBox.connect('notify::hover', Lang.bind(this, function(box) {this._callHover(box, close)} ));

        let call = new St.Button({ style_class: 'call-entry item',
                                   label: text,
                                   x_align: St.Align.START });
        this._calls.push(call);

        let close = new St.Button({ style_class: 'call-entry close' });
        close.hide();
        close.connect('clicked', Lang.bind(this, function() { this._removeCall(call) }));

        let closeBin = new St.Bin({ reactive: true });
        closeBin.child = close;

        callBox.add(call);
        callBox.add(closeBin);

        this.addActor(callBox);
    },

    _callHover: function(box, close) {
        if (box.hover) {
            print("hover on");
            close.opacity = 0;
            close.show();
            Tweener.addTween(close,
                             { opacity: 255,
                               time: CALL_FADE_TIME,
                               transition: 'easeInQuad'});
        } else {
            print("hover off");
            Tweener.addTween(close,
                             { opacity: 0,
                               time: CALL_FADE_TIME,
                               transition: 'easeOutQuad',
                               onComplete: function() { close.hide() }});
        }
    },

    _removeCall: function(call) {
        // if this is the only call in the list, remove the list
        if (this._calls && this._calls.length == 1) {
            this.destroy();
            return;
        }

        //remove call from array
        this._calls.splice(this._calls.indexOf(call), 1);
        call.parent.destroy();
        call.destroy();
    },
};
