//
//  Created by Bradley Austin Davis on 2015/09/01
//  Copyright 2015 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

Script.include("constants.js");
Script.include("utils.js");
Script.include("highlighter.js");
Script.include("omniTool/models/modelBase.js");
Script.include("omniTool/models/wand.js");

OmniToolModules = {};
OmniToolModuleType = null;

OmniTool = function(side) {
    this.OMNI_KEY = "OmniTool";
    this.MAX_FRAMERATE = 30;
    this.UPDATE_INTERVAL = 1.0 / this.MAX_FRAMERATE
    this.SIDE = side;
    this.PALM = 2 * side;
    this.ACTION = findAction(side ? "ACTION2" : "ACTION1");
    this.ALT_ACTION = findAction(side ? "ACTION1" : "ACTION2");

    this.highlighter = new Highlighter();
    this.ignoreEntities = {};
    this.nearestOmniEntity = {
        id: null,
        inside: false,
        position: null,
        distance: Infinity,
        radius: 0,
        omniProperties: {},
        boundingBox: null,
    };
    
    this.activeOmniEntityId = null;
    this.lastUpdateInterval = 0;
    this.tipLength = 0.4;
    this.active = false;
    this.module = null;
    this.moduleEntityId = null;
    this.lastScanPosition = ZERO_VECTOR;
    this.model = new Wand();
    this.model.setLength(this.tipLength);

    // Connect to desired events
    var _this = this;
    Controller.actionEvent.connect(function(action, state) {
        _this.onActionEvent(action, state);
    });

    Script.update.connect(function(deltaTime) {
        _this.lastUpdateInterval += deltaTime;
        if (_this.lastUpdateInterval >= _this.UPDATE_INTERVAL) {
            _this.onUpdate(_this.lastUpdateInterval);
            _this.lastUpdateInterval = 0;
        }
    });

    Script.scriptEnding.connect(function() {
        _this.onCleanup();
    });
}

OmniTool.prototype.onCleanup = function(action) {
    this.unloadModule();
}

OmniTool.prototype.onActionEvent = function(action, state) {
    // FIXME figure out the issues when only one spatial controller is active 
    // logDebug("Action: " + action + " " + state);

    if (this.module && this.module.onActionEvent) {
        this.module.onActionEvent(action, state);
    }

    if (action == this.ACTION) {
        if (state) {
            this.onClick();
        } else {
            this.onRelease();
        }
    }

    // FIXME Does not work
    //// with only one controller active (listed as 2 here because 'tip' + 'palm')
    //// then treat the alt action button as the action button
}

OmniTool.prototype.getOmniToolData = function(entityId) {
    return getEntityCustomData(this.OMNI_KEY, entityId, null);
}

OmniTool.prototype.setOmniToolData = function(entityId, data) {
    setEntityCustomData(this.OMNI_KEY, entityId, data);
}

OmniTool.prototype.updateOmniToolData = function(entityId, data) {
    var currentData = this.getOmniToolData(entityId) || {};
    for (var key in data) {
        currentData[key] = data[key];
    }
    setEntityCustomData(this.OMNI_KEY, entityId, currentData);
}

OmniTool.prototype.setActive = function(active) {
    if (active === this.active) {
        return;
    }
    logDebug("omnitool changing active state: " + active);
    this.active = active;
    this.model.setVisible(this.active);
    
    if (this.module && this.module.onActiveChanged) {
        this.module.onActiveChanged(this.side);
    }
}


OmniTool.prototype.onUpdate = function(deltaTime) {
    // FIXME this returns data if either the left or right controller is not on the base
    this.position = Controller.getSpatialControlPosition(this.PALM);
    // When on the base, hydras report a position of 0
    this.setActive(Vec3.length(this.position) > 0.001);

    var rawRotation = Controller.getSpatialControlRawRotation(this.PALM);
    this.rotation = Quat.multiply(MyAvatar.orientation, rawRotation);

    this.model.setTransform({
        rotation: this.rotation,
        position: this.position,
    });
    
    this.scan();
    
    if (this.module && this.module.onUpdate) {
        this.module.onUpdate(deltaTime);
    }
}

OmniTool.prototype.onClick = function() {
    // First check to see if the user is switching to a new omni module
    if (this.nearestOmniEntity.inside && this.nearestOmniEntity.omniProperties.script) {
        this.activateNewOmniModule();
        return;
    }
    
    // Next check if there is an active module and if so propagate the click
    // FIXME how to I switch to a new module?
    if (this.module && this.module.onClick) {
        this.module.onClick();
        return;
    }
}

OmniTool.prototype.onRelease = function() {
    // FIXME how to I switch to a new module?
    if (this.module && this.module.onRelease) {
        this.module.onRelease();
        return;
    }
    logDebug("Base omnitool does nothing on release");
}

// FIXME resturn a structure of all nearby entities to distances
OmniTool.prototype.findNearestOmniEntity = function(maxDistance, selector)  {
    if (!maxDistance) {
        maxDistance = 2.0;
    }
    var resultDistance = Infinity;
    var resultId = null;
    var resultProperties = null;
    var resultOmniData = null;
    var ids = Entities.findEntities(this.model.tipPosition, maxDistance);
    for (var i in ids) {
        var entityId = ids[i];
        if (this.ignoreEntities[entityId]) {
            continue;
        }
        var omniData = this.getOmniToolData(entityId);
        if (!omniData) {
            // FIXME find a place to flush this information
            this.ignoreEntities[entityId] = true;
            continue;
        }
        
        // Let searchers query specifically
        if (selector && !selector(entityId, omniData)) {
            continue;
        }
        
        var properties = Entities.getEntityProperties(entityId);
        var distance = Vec3.distance(this.model.tipPosition, properties.position);
        if (distance < resultDistance) {
            resultDistance = distance;
            resultId = entityId;
        }
    }

    return resultId;
}

OmniTool.prototype.getPosition = function() {
    return this.model.tipPosition;
}

OmniTool.prototype.onEnterNearestOmniEntity = function() {
    this.nearestOmniEntity.inside = true;
    this.highlighter.highlight(this.nearestOmniEntity.id);
    logDebug("On enter omniEntity " + this.nearestOmniEntity.id);
}

OmniTool.prototype.onLeaveNearestOmniEntity = function() {
    this.nearestOmniEntity.inside = false;
    this.highlighter.highlight(null);
    logDebug("On leave omniEntity " + this.nearestOmniEntity.id);
}

OmniTool.prototype.setNearestOmniEntity = function(entityId) {
    if (entityId && entityId !== this.nearestOmniEntity.id) {
        if (this.nearestOmniEntity.id && this.nearestOmniEntity.inside) {
            this.onLeaveNearestOmniEntity();
        }
        this.nearestOmniEntity.id = entityId;
        this.nearestOmniEntity.omniProperties = this.getOmniToolData(entityId);
        var properties = Entities.getEntityProperties(entityId);
        this.nearestOmniEntity.position = properties.position;
        // FIXME use a real bounding box, not a sphere
        var bbox = properties.boundingBox;
        this.nearestOmniEntity.radius = Vec3.length(Vec3.subtract(bbox.center, bbox.brn));
        this.highlighter.setRotation(properties.rotation);
        this.highlighter.setSize(Vec3.multiply(1.05, bbox.dimensions));
    }
}

OmniTool.prototype.scan = function() {
    var scanDistance = Vec3.distance(this.model.tipPosition, this.lastScanPosition);
    
    if (scanDistance < 0.005) {
        return;
    }
    
    this.lastScanPosition = this.model.tipPosition;
    
    this.setNearestOmniEntity(this.findNearestOmniEntity());
    if (this.nearestOmniEntity.id) {
        var distance = Vec3.distance(this.model.tipPosition, this.nearestOmniEntity.position);
        // track distance on a half centimeter basis
        if (Math.abs(this.nearestOmniEntity.distance - distance) > 0.005) {
            this.nearestOmniEntity.distance = distance;
            if (!this.nearestOmniEntity.inside && distance < this.nearestOmniEntity.radius) {
                this.onEnterNearestOmniEntity();
            }

            if (this.nearestOmniEntity.inside && distance > this.nearestOmniEntity.radius + 0.01) {
                this.onLeaveNearestOmniEntity();
            }
        }
    }
}

OmniTool.prototype.unloadModule = function() {
    if (this.module && this.module.onUnload) {
        this.module.onUnload();
    }
    this.module = null;
    this.moduleEntityId = null;
}

OmniTool.prototype.activateNewOmniModule = function() {
    // Support the ability for scripts to just run without replacing the current module
    var script = this.nearestOmniEntity.omniProperties.script;
    if (script.indexOf("/") < 0) {
        script = "omniTool/modules/" + script;
    }

    // Reset the tool type
    OmniToolModuleType = null;
    logDebug("Including script path: " + script);
    try {
        Script.include(script);
    } catch(err) {
        logWarn("Failed to include script: " + script + "\n" + err);
        return;
    }

    // If we're building a new module, unload the old one
    if (OmniToolModuleType) {
        logDebug("New OmniToolModule: " + OmniToolModuleType);
        this.unloadModule();

        try {
            this.module = new OmniToolModules[OmniToolModuleType](this, this.nearestOmniEntity.id);
            this.moduleEntityId = this.nearestOmniEntity.id;
            if (this.module.onLoad) {
                this.module.onLoad();
            }
        } catch(err) {
            logWarn("Failed to instantiate new module: " + err);
        }
    }
}

// FIXME find a good way to sync the two omni tools
OMNI_TOOLS = [ new OmniTool(0), new OmniTool(1) ];
