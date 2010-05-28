/* -*- mode: js2; js2-basic-offset: 4; indent-tabs-mode: nil -*- */

const Clutter = imports.gi.Clutter;
const DBus = imports.dbus;
const GLib = imports.gi.GLib;
const Lang = imports.lang;
const Shell = imports.gi.Shell;
const Signals = imports.signals;
const St = imports.gi.St;
const Gettext = imports.gettext.domain('gnome-shell');
const _ = Gettext.gettext;
const Tp = imports.gi.TelepathyGLib;

const Main = imports.ui.main;
const MessageTray = imports.ui.messageTray;
const Telepathy = imports.misc.telepathy;

let channelDispatcher;

// See Notification.appendMessage
const SCROLLBACK_RECENT_TIME = 15 * 60; // 15 minutes
const SCROLLBACK_RECENT_LENGTH = 20;
const SCROLLBACK_IDLE_LENGTH = 5;

// This is GNOME Shell's implementation of the Telepathy 'Client'
// interface. Specifically, the shell is a Telepathy 'Observer', which
// lets us see messages even if they belong to another app (eg,
// Empathy).

function Client() {
    this._init();
};

Client.prototype = {
    _init : function() {
        this._connections = {};
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

        // We only care about single-user text-based chats
        this.observer.add_observer_filter({
            'org.freedesktop.Telepathy.Channel.ChannelType': Tp.IFACE_CHANNEL_TYPE_TEXT,
            'org.freedesktop.Telepathy.Channel.TargetHandleType': Tp.HandleType.CONTACT,
        });

        try {
            this.observer.register();
        } catch (e) {
            throw new Error("Couldn't register SimpleObserver. Error: \n" + e);
        }

        this.approver = Tp.SimpleApprover.new(dbus, "GnomeShell", true,
                Lang.bind(this, this.ApproveChannels));

        // We only care about single-user text-based chats
        this.approver.add_approver_filter({
            'org.freedesktop.Telepathy.Channel.ChannelType': Tp.IFACE_CHANNEL_TYPE_TEXT,
            'org.freedesktop.Telepathy.Channel.TargetHandleType': Tp.HandleType.CONTACT,
        });

        try {
            this.approver.register();
        } catch (e) {
            throw new Error("Couldn't register SimpleApprover. Error: \n" + e);
        }
    },

    ApproveChannels: function(approver, account, conn, channels,
                              dispatch_op, context, user_data) {
        print("\nApproveChannels:");
        print(approver);
        print(account);
        print(conn);
        print("Channels: ");
        for(let i in channels) {
            print("\t" + channels[i]);
        }
    },

    ObserveChannels: function(observer, account, conn, channels,
                              dispatch_op, requests, context, user_data) {
        let connPath = conn.get_object_path();
        let connName = conn.get_bus_name();

        if(!this._connections[connPath]) {
            this._connections[connPath] = conn;

            conn.invalidatedId = conn.connect('invalidated', Lang.bind(this, this._removeConnection, conn));

            conn.connectionPresence = new Telepathy.ConnectionSimplePresence(DBus.session, connName, connPath);
            conn.presenceChangedId = conn.connectionPresence.connect(
                'PresencesChanged', Lang.bind(this, this._presencesChanged));
        }

        for (let i in channels) {
            let chan = channels[i];
            let [targetHandle, targetHandleType] = chan.get_handle();

            if (this._sources[connPath + ':' + targetHandle]) {
                continue;
            }

            Shell.Global.get_tp_contacts(
                    conn,
                    1, [targetHandle],
                    2, [Tp.ContactFeature.ALIAS, Tp.ContactFeature.AVATAR_DATA],
                    Lang.bind(this, 
                        function(connection, contacts, failed) {
                            let contact = contacts[0];

                            let source = new Source(account, conn, contact, chan);
                            source._destroyId = source.connect('destroy', Lang.bind(this, this._removeSource));
                            this._sources[source.object_path] = source;
                        }), null);
        }
 
        // Allow dbus method to return
        context.accept();
    },

    // Presence of a contact has changed.
    _presencesChanged: function(proxy, presences, err) {
        let connPath = proxy.getPath();

        for (let handle in presences) {
            let source = this._sources[connPath + ':' + handle];
            if(source) {
                source.changePresence(presences[handle]);
            }
        }
    },

    _removeConnection: function(conn) {
        conn.connectionPresence.disconnect(conn.presenceChangedId);
        delete this._connections[conn.get_object_path];
    },

    _removeSource: function(source) {
        source.disconnect(source._destroyId);
        delete this._sources[source.object_path];
        return;
    },

    get Interfaces() {
        return [ Tp.IFACE_CLIENT_OBSERVER ]
    },
};


function Source(account, conn, contact, chan) {
    this._init(account, conn, contact, chan);
}

Source.prototype = {
    __proto__:  MessageTray.Source.prototype,

    _init: function(account, conn, contact, chan) {
        this._account = account;
        this._conn = conn;
        this._chan = chan;
        this._contact = contact;

        chan._invalidatedId = chan.connect('invalidated', Lang.bind(this, this._removeChannel));

        this._channelText = new Telepathy.ChannelText(DBus.session, conn.get_bus_name(), chan.get_object_path());
        this._channelText._receivedId = this._channelText.connect('Received', Lang.bind(this, this._messageReceived));
        this._channelText.ListPendingMessagesRemote(false, Lang.bind(this, this._gotPendingMessages));

        MessageTray.Source.prototype._init.call(this, contact.get_identifier());

        this._ensureNotification();
    },

    createIcon: function(size) {
        let iconBox = new St.Bin({ style_class: 'avatar-box' });
        iconBox._size = size;
        iconBox.connect('notify::hover', function(){ print("hover"); });
        let textureCache = St.TextureCache.get_default();

        let file = this._contact.get_avatar_file();
        if(file) {
            let uri = file.get_uri();
            iconBox.child = textureCache.load_uri_sync(St.TextureCachePolicy.FOREVER,
                                                       uri, size, size)
        } else {
            iconBox.child = textureCache.load_icon_name('stock_person', iconBox._size);
        }

        return iconBox;
    },

    clicked: function() {
        let handle = this._contact.get_handle();
        channelDispatcher.EnsureChannelRemote(this._account.get_object_path(),
                                              { 'org.freedesktop.Telepathy.Channel.ChannelType': Tp.IFACE_CHANNEL_TYPE_TEXT,
                                                'org.freedesktop.Telepathy.Channel.TargetHandle': handle,
                                                'org.freedesktop.Telepathy.Channel.TargetHandleType': Tp.HandleType.CONTACT },
                                              global.get_current_time(),
                                              '',
                                              Lang.bind(this, this._gotChannelRequest));

        MessageTray.Source.prototype.clicked.call(this);
    },

    respond: function(text) {
        this._channelText.SendRemote(Tp.ChannelTextMessageType.NORMAL, text);
    },

    _gotChannelRequest: function (chanReqPath, ex) {
        if (ex) {
            log ('EnsureChannelRemote failed? ' + ex);
            return;
        }

        let chanReq = new Telepathy.ChannelRequest(DBus.session, Tp.CHANNEL_DISPATCHER_BUS_NAME, chanReqPath);
        chanReq.ProceedRemote();
    },

    _gotPendingMessages: function(msgs, err) {
        if (!msgs)
            return;

        for (let i = 0; i < msgs.length; i++)
            this._messageReceived.apply(this, [this._chan].concat(msgs[i]));
    },

    _removeChannel: function(chan) {
        chan.disconnect(chan._invalidatedId);
        this._channelText.disconnect(this._channelText._receivedId);

        this._removeSource();
    },

    _removeSource: function() {
        this.destroy();
    },

    _ensureNotification: function() {
        if (!Main.messageTray.contains(this))
            Main.messageTray.add(this);

        if (!this._notification) {
            let id = this._contact.get_identifier();
            this._notification = new Notification(id, this);
        }
    },

    _messageReceived: function(channel, id, timestamp, sender,
                               type, flags, text) {
        this._ensureNotification();
        this._notification.appendMessage(text);
        this.notify(this._notification);
    },

    changePresence: function(presence) {
        let [type, status, status_message] = presence;
        let msg;

        switch(type) {
            case Tp.ConnectionPresenceType.AVAILABLE:
                msg = _("%s is online").format(this.name);
                break;
            case Tp.ConnectionPresenceType.AWAY:
                msg = _("%s is away").format(this.name);
                break;
            case Tp.ConnectionPresenceType.EXTENDED_AWAY:
                msg = _("%s is away").format(this.name);
                break;
            case Tp.ConnectionPresenceType.HIDDEN:
                msg = _("%s is hidden").format(this.name);
                break;
            case Tp.ConnectionPresenceType.BUSY:
                msg = _("%s is busy").format(this.name);
                break;
            case Tp.ConnectionPresenceType.OFFLINE:
                msg = _("%s is offline").format(this.name);
                break;
            default:
                // Unrecognized presence type. shouldn't ever happen.
                msg = null;
        }

        if (status_message !== "")
            msg += ' <i>(' + GLib.markup_escape_text(status_message, -1) + ')</i>';

        this._ensureNotification();
        this._notification.appendMessage(msg, true);
        this.notify(this._notification);
    },

    get name() {
        return this._contact.get_alias();
    },
    
    get object_path() {
        return this._conn.get_object_path() + ':' + this._contact.get_handle()
    },
};


function Notification(id, source) {
    this._init(id, source);
}

Notification.prototype = {
    __proto__:  MessageTray.Notification.prototype,

    _init: function(id, source) {
        MessageTray.Notification.prototype._init.call(this, id, source, source.name);
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
            this.update(this.source.name, text);
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
