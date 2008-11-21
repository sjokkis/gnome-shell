/* -*- mode: js2; js2-basic-offset: 4; -*- */

const Signals = imports.signals;
const Shell = imports.gi.Shell;
const Clutter = imports.gi.Clutter;

const Main = imports.ui.main;

const OVERLAY_COLOR = new Clutter.Color();
OVERLAY_COLOR.from_pixel(0x00000044);

const BOX_BACKGROUND_COLOR = new Clutter.Color();
BOX_BACKGROUND_COLOR.from_pixel(0x000000cc);

const BOX_TEXT_COLOR = new Clutter.Color();
BOX_TEXT_COLOR.from_pixel(0xffffffff);

const BOX_WIDTH = 320;
const BOX_HEIGHT = 56;

function RunDialog() {
    this._init();
};

RunDialog.prototype = {
    _init : function() {
        let global = Shell.global_get();

        // All actors are inside _group.
        this._group = new Clutter.Group();
        global.stage.add_actor(this._group);

        this._overlay = new Clutter.Rectangle({ color: OVERLAY_COLOR,
                                                width: global.screen_width,
                                                height: global.screen_height,
                                                border_width: 0,
                                                reactive: true });
        this._group.add_actor(this._overlay);

        let boxGroup = new Clutter.Group();
        boxGroup.set_position((global.screen_width - BOX_WIDTH) / 2,
                              (global.screen_height - BOX_HEIGHT) / 2);
        this._group.add_actor(boxGroup);

        let box = new Clutter.Rectangle({ color: BOX_BACKGROUND_COLOR,
                                          reactive: false,
                                          width: BOX_WIDTH,
                                          height: BOX_HEIGHT,
                                          border_width: 0 });
        boxGroup.add_actor(box);

        let label = new Clutter.Label({ color: BOX_TEXT_COLOR,
                                        font_name: '18px Sans',
                                        text: 'Please enter a command:' });
        label.set_position(6, 6);
        boxGroup.add_actor(label);

        this._entry = new Clutter.Entry({ color: BOX_TEXT_COLOR,
                                          font_name: '20px Sans Bold',
                                          reactive: true,
                                          text: '',
                                          entry_padding: 0,
                                          width: BOX_WIDTH - 12,
                                          height: BOX_HEIGHT - 12 });
        // TODO: Implement relative positioning using Tidy.
        this._entry.set_position(6, 30);
        boxGroup.add_actor(this._entry);

        let me = this;

        this._entry.connect('activate', function (o, e) {
            me.hide();
            me._run(o.get_text());
            return false;
        });

        // TODO: Detect escape key and make it cancel the operation.
        //       Use me.on_cancel() if it exists. Something like this:
        // this._entry.connect('key-press-event', function(o, e) {
        //     if (the pressed key is the escape key) {
        //         me.hide();
        //         me.emit('cancel');
        //         return false;
        //     } else
        //         return true;
        // });

        global.focus_stage();
        global.stage.set_key_focus(this._entry);
    },

    _run : function(command) {
        if (command) {
            var p = new Shell.Process({'args' : [command]});
            try {
                p.run();
            } catch (e) {
                // TODO: Give the user direct feedback.
                log('Could not run command ' + command + '.');
            }
        }

	this.emit('run');
    },

    show : function() {
        this._group.show_all();
    },

    hide : function() {
        this._group.hide();
    },

    destroy : function(){
        this._group.destroy();
    }
};
Signals.addSignalMethods(RunDialog.prototype);