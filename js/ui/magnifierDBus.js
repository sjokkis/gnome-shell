/* -*- mode: js2; js2-basic-offset: 4; indent-tabs-mode: nil -*- */

const DBus = imports.dbus;
const Main = imports.ui.main;

const MAG_SERVICE_NAME = 'org.gnome.Magnifier';
const MAG_SERVICE_PATH = '/org/gnome/Magnifier';
const ZOOM_SERVICE_NAME = 'org.gnome.Magnifier.ZoomRegion';
const ZOOM_SERVICE_PATH = '/org/gnome/Magnifier/ZoomRegion';

// Subset of gnome-mag's Magnifier dbus interface -- to be expanded.  See:
// http://git.gnome.org/browse/gnome-mag/tree/xml/...Magnifier.xml
const MagnifierIface = {
    name: MAG_SERVICE_NAME,
    methods: [
                { name: 'setActive', inSignature: 'b', outSignature: '' },
                { name: 'isActive', inSignature: '', outSignature: 'b' },
                { name: 'showCursor', inSignature: '', outSignature: '' },
                { name: 'hideCursor', inSignature: '', outSignature: ''  },
                { name: 'createZoomRegion', inSignature: 'ddaiai', outSignature: 'o' },
                { name: 'addZoomRegion', inSignature: 'o', outSignature: 'b' },
                { name: 'getZoomRegions', inSignature: '', outSignature: 'ao' },
                { name: 'clearAllZoomRegions', inSignature: '', outSignature: '' },
                { name: 'fullScreenCapable', inSignature: '', outSignature: 'b' },

                { name: 'setCrosswireSize', inSignature: 'i', outSignature: '' },
                { name: 'getCrosswireSize', inSignature: '', outSignature: 'i' },
                { name: 'setCrosswireLength', inSignature: 'i', outSignature: '' },
                { name: 'getCrosswireLength', inSignature: '', outSignature: 'i' },
                { name: 'setCrosswireClip', inSignature: 'b', outSignature: '' },
                { name: 'getCrosswireClip', inSignature: '', outSignature: 'b' },
                { name: 'setCrosswireColor', inSignature: 'u', outSignature: '' },
                { name: 'getCrosswireColor', inSignature: '', outSignature: 'u' }
             ],
    signals: [],
    properties: []
};

// Subset of gnome-mag's ZoomRegion dbus interface -- to be expanded.  See:
// http://git.gnome.org/browse/gnome-mag/tree/xml/...ZoomRegion.xml
const ZoomRegionIface = {
    name: ZOOM_SERVICE_NAME,
    methods: [
                { name: 'setMagFactor', inSignature: 'dd', outSignature: ''},
                { name: 'getMagFactor', inSignature: '', outSignature: 'dd' },
                { name: 'setRoi', inSignature: 'ai', outSignature: '' },
                { name: 'getRoi', inSignature: '', outSignature: 'ai' },
                { name: 'shiftContentsTo', inSignature: 'ii', outSignature: 'b' },
                { name: 'moveResize', inSignature: 'ai', outSignature: '' }
             ],
    signals: [],
    properties: []
};

// For making unique ZoomRegion DBus proxy object paths of the form:
// '/org/gnome/Magnifier/ZoomRegion/zoomer0',
// '/org/gnome/Magnifier/ZoomRegion/zoomer1', etc.
let _zoomRegionInstanceCount = 0;

function ShellMagnifier() {
    this._init();
}

ShellMagnifier.prototype = {
    _init: function() {
        this._zoomers = {};
        DBus.session.exportObject(MAG_SERVICE_PATH, this);
    },

    /**
     * setActive:
     * @activate:   Boolean to activate or de-activate the magnifier.
     */
    setActive: function(activate) {
        Main.magnifier.setActive(activate);
    },

    /**
     * isActive:
     * @return  Whether the magnifier is active (boolean).
     */
    isActive: function() {
        return Main.magnifier.isActive();
    },

    /**
     * showCursor:
     * Show the system mouse pointer.
     */
    showCursor: function() {
        Main.magnifier.showSystemCursor();
    },

    /**
     * hideCursor:
     * Hide the system mouse pointer.
     */
    hideCursor: function() {
        Main.magnifier.hideSystemCursor();
    },

    /**
     * createZoomRegion:
     * Create a new ZoomRegion and return its object path.
     * @xMagFactor:     The power to set horizontal magnification of the
     *                  ZoomRegion.  A value of 1.0 means no magnification.  A
     *                  value of 2.0 doubles the size.
     * @yMagFactor:     The power to set the vertical magnification of the
     *                  ZoomRegion.
     * @roi             Array of integers defining the region of the
     *                  screen/desktop to magnify.  The array has the form
     *                  [x, y, width, height].
     * @viewPort        Array of integers, [ x, y, width, height ] that defines
     *                  the position of the ZoomRegion on screen.
     * @return          The newly created ZoomRegion.
     */
    createZoomRegion: function(xMagFactor, yMagFactor, roi, viewPort) {
        let ROI = { x: roi[0], y: roi[1], width: roi[2], height: roi[3] };
        let viewBox = { x: viewPort[0], y: viewPort[1], width: viewPort[2], height: viewPort[3] };
        let realZoomRegion = Main.magnifier.createZoomRegion(xMagFactor, yMagFactor, ROI, viewBox);
        let objectPath = ZOOM_SERVICE_PATH + '/zoomer' + _zoomRegionInstanceCount;
        _zoomRegionInstanceCount++;

        let zoomRegionProxy = new ShellMagnifierZoomRegion(objectPath, realZoomRegion);
        let proxyAndZoomRegion = {};
        proxyAndZoomRegion.proxy = zoomRegionProxy;
        proxyAndZoomRegion.zoomRegion = realZoomRegion;
        this._zoomers[objectPath] = proxyAndZoomRegion;
        return objectPath;
    },

    /**
     * addZoomRegion:
     * Append the given ZoomRegion to the magnifier's list of ZoomRegions.
     * @zoomerObjectPath:   The object path for the zoom region proxy.
     */
    addZoomRegion: function(zoomerObjectPath) {
        let proxyAndZoomRegion = this._zoomers[zoomerObjectPath];
        if (proxyAndZoomRegion && proxyAndZoomRegion.zoomRegion) {
            Main.magnifier.addZoomRegion(proxyAndZoomRegion.zoomRegion);
            return true;
        }
        else
            return false;
    },

    /**
     * getZoomRegions:
     * Return a list of ZoomRegion object paths for this Magnifier.
     * @return:     The Magnifier's zoom region list as an array of DBus object
     *              paths.
     */
    getZoomRegions: function() {
        // There may be more ZoomRegions in the magnifier itself than have
        // been added through dbus.  Make sure all of them are associated with
        // an object path and proxy.
        let zoomRegions = Main.magnifier.getZoomRegions();
        let objectPaths = [];
        let thoseZoomers = this._zoomers;
        zoomRegions.forEach (function(aZoomRegion, index, array) {
            let found = false;
            for (let objectPath in thoseZoomers) {
                let proxyAndZoomRegion = thoseZoomers[objectPath];
                if (proxyAndZoomRegion.zoomRegion === aZoomRegion) {
                    objectPaths.push(objectPath);
                    found = true;
                    break;
                }
            }
            if (!found) {
                // Got a ZoomRegion with no DBus proxy, make one.
                let newPath =  ZOOM_SERVICE_PATH + '/zoomer' + _zoomRegionInstanceCount;
                _zoomRegionInstanceCount++;
                let zoomRegionProxy = new ShellMagnifierZoomRegion(newPath, aZoomRegion);
                let proxyAndZoomer = {};
                proxyAndZoomer.proxy = zoomRegionProxy;
                proxyAndZoomer.zoomRegion = aZoomRegion;
                thoseZoomers[newPath] = proxyAndZoomer;
                objectPaths.push(newPath);
            }
        });
        return objectPaths;
    },

    /**
     * clearAllZoomRegions:
     * Remove all the zoom regions from this Magnfier's ZoomRegion list.
     */
    clearAllZoomRegions: function() {
        Main.magnifier.clearAllZoomRegions();
        for (let objectPath in this._zoomers) {
            let proxyAndZoomer = this._zoomers[objectPath];
            proxyAndZoomer.proxy = null;
            proxyAndZoomer.zoomRegion = null;
            delete this._zoomers[objectPath];
            DBus.session.unexportObject(proxyAndZoomer);
        }
        this._zoomers = {};
    },

    /**
     * fullScreenCapable:
     * Consult if the Magnifier can magnify in full-screen mode.
     * @return  Always return true.
     */
    fullScreenCapable: function() {
        return true;
    },

    /**
     * setCrosswireSize:
     * Set the crosswire size of all ZoomRegions.
     * @size:   The thickness of each line in the cross wire.
     */
     setCrosswireSize: function(size) {
        Main.magnifier.setCrosshairsThickness(size);
     },

    /**
     * getCrosswireSize:
     * Get the crosswire size of all ZoomRegions.
     * @return:   The thickness of each line in the cross wire.
     */
     getCrosswireSize: function() {
        return Main.magnifier.getCrosshairsThickness();
     },

    /**
     * setCrosswireLength:
     * Set the crosswire length of all zoom-regions..
     * @size:   The length of each line in the cross wire.
     */
     setCrosswireLength: function(length) {
        Main.magnifier.setCrosshairsLength(length);
     },

    /**
     * setCrosswireSize:
     * Set the crosswire size of all zoom-regions.
     * @size:   The thickness of each line in the cross wire.
     */
     getCrosswireLength: function() {
        return Main.magnifier.getCrosshairsLength();
     },

    /**
     * setCrosswireClip:
     * Set if the crosswire will be clipped by the cursor image..
     * @clip:   Flag to indicate whether to clip the crosswire.
     */
     setCrosswireClip: function(clip) {
        Main.magnifier.setCrosshairsClip(clip);
     },

    /**
     * getCrosswireClip:
     * Get the crosswire clip value.
     * @return:   Whether the crosswire is clipped by the cursor image.
     */
     getCrosswireClip: function() {
        return Main.magnifier.getCrosshairsClip();
     },

    /**
     * setCrosswireColor:
     * Set the crosswire color of all ZoomRegions.
     * @color:   Unsigned int of the form rrggbbaa.
     */
     setCrosswireColor: function(color) {
        Main.magnifier.setCrosshairsColor('#' + color.toString(16));
     },

    /**
     * getCrosswireClip:
     * Get the crosswire color of all ZoomRegions.
     * @return:   The crosswire color as an unsigned int in the form rrggbbaa.
     */
     getCrosswireColor: function() {
        let colorString = Main.magnifier.getCrosshairsColor();
        // Drop the leading '#'.
        return parseInt(colorString.slice(1), 16);
     }
};

/**
 * ShellMagnifierZoomRegion:
 * Object that implements the DBus ZoomRegion interface.
 * @zoomerObjectPath:   String that is the path to a DBus ZoomRegion.
 * @zoomRegion:         The actual zoom region associated with the object path.
 */
function ShellMagnifierZoomRegion(zoomerObjectPath, zoomRegion) {
    this._init(zoomerObjectPath, zoomRegion);
}

ShellMagnifierZoomRegion.prototype = {
    _init: function(zoomerObjectPath, zoomRegion) {
        this._zoomRegion = zoomRegion;
        DBus.session.proxifyObject(this, ZOOM_SERVICE_NAME, zoomerObjectPath);
        DBus.session.exportObject(zoomerObjectPath, this);
    },

    /**
     * setMagFactor:
     * @xMagFactor:     The power to set the horizontal magnification factor to
     *                  of the magnified view.  A value of 1.0 means no
     *                  magnification.  A value of 2.0 doubles the size.
     * @yMagFactor:     The power to set the vertical magnification factor to
     *                  of the magnified view.
     */
    setMagFactor: function(xMagFactor, yMagFactor) {
        this._zoomRegion.setMagFactor(xMagFactor, yMagFactor);
    },

    /**
     * getMagFactor:
     * @return  an array, [xMagFactor, yMagFactor], containing the horizontal
     *          and vertical magnification powers.  A value of 1.0 means no
     *          magnification.  A value of 2.0 means the contents are doubled
     *          in size, and so on.
     */
    getMagFactor: function() {
        return this._zoomRegion.getMagFactor();
    },

    /**
     * setRoi:
     * Sets the "region of interest" that the ZoomRegion is magnifying.
     * @roi     Array, [x, y, width, height], defining the region of the screen to
     *          magnify. The values are in screen (unmagnified) coordinate
     *          space.
     */
    setRoi: function(roi) {
        let roiObject = { x: roi[0], y: roi[1], width: roi[2], height: roi[3] };
        this._zoomRegion.setROI(roiObject);
    },

    /**
     * getRoi:
     * Retrieves the "region of interest" -- the rectangular bounds of that part
     * of the desktop that the magnified view is showing (x, y, width, height).
     * The bounds are given in non-magnified coordinates.
     * @return  an array, [x, y, width, height], representing the bounding
     *          rectangle of what is shown in the magnified view.
     */
    getRoi: function() {
        return this._zoomRegion.getROI();
    },

    /**
     * Set the "region of interest" by centering the given screen coordinate
     * within the zoom region.
     * @x       The x-coord of the point to place at the center of the zoom region.
     * @y       The y-coord.
     * @return  Whether the shift was successful (for GS-mag, this is always
     *          true).
     */
    shiftContentsTo: function(x, y) {
        this._zoomRegion.scrollContentsTo(x, y);
        return true;
    },

    /**
     * moveResize
     * Sets the position and size of the ZoomRegion on screen.
     * @viewPort    Array, [x, y, width, height], defining the position and size
     *              on screen to place the zoom region.
     */
    moveResize: function(viewPort) {
        let viewRect = { x: viewPort[0], y: viewPort[1], width: viewPort[2], height: viewPort[3] };
        this._zoomRegion.setViewPort(viewRect);
    }
};

DBus.conformExport(ShellMagnifier.prototype, MagnifierIface);
DBus.conformExport(ShellMagnifierZoomRegion.prototype, ZoomRegionIface);
