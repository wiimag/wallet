/*
 * Copyright 2023 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

module.exports = (function () {
    "use strict";

    var _ = require("lodash"),
        ws = require("nodejs-websocket");

    // Static variables

    function Connection() {
        this.connection = null;
        this.handlers = {};
    }

    Connection.prototype = {
        connect: function (ip, port, namespaces, onConnected) {
            var connectUrl = "ws://" + ip + ":" + port;
            if (namespaces.length > 0) {
                connectUrl += "/?namespaces=" + namespaces.join(",");
            }

            console.log("Connecting to", connectUrl);

            this.connection = ws.connect(connectUrl, {}, function () {});

            this.connection.on("close", function (close, reason) {
                console.log("connection closed: ", close, reason);
            });

            this.connection.on("error", function (err) {
                console.log("connection error: ", err);
                if (onConnected) {
                    onConnected(new Error("Can't connect to url" + connectUrl));
                }
            });

            this.connection.on("text", function (str) {
                this.dispatchCmd(str);
            }.bind(this));

            this.connection.on("binary", function (stream) {});

            this.connection.on("connect", function () {
                if (onConnected) {
                    onConnected(null, this);
                    onConnected = null;
                }
            }.bind(this));

            return this.connection;
        },

        sendCmd: function (namespace, type, data) {
            var cmd = {
                data: data,
                namespace: namespace,
                type: type
            };

            var cmdStr = JSON.stringify(cmd);
            this.connection.sendText(cmdStr);
        },

        send: function (msg) {
            if (_.isObject(msg)) {
                return this.connection.sendText(JSON.stringify(msg));
            }

            return this.connection.sendText(msg);
        },

        registerHandler: function (namespace, type, handler) {
            if (!this.handlers[namespace]) {
                this.handlers[namespace] = {};
            }

            if (!this.handlers[namespace][type]) {
                this.handlers[namespace][type] = handler;
            }
        },

        unregisterHandler: function (namespace, type) {
            if (this.handlers[namespace] && this.handlers[namespace][type]) {
                delete this.handlers[namespace][type];
            }
        },

        dispatchCmd: function (cmdStr) {
            var cmd = JSON.parse(cmdStr);
            cmd.namespace = cmd.namespace || "";
            if (cmd.type) {
                var handlerByType = null;
                var handlersByNamespace = this.handlers[cmd.namespace];
                if (handlersByNamespace) {
                    handlerByType = handlersByNamespace[cmd.type];
                    if (handlerByType) {
                        handlerByType(cmd.namespace, cmd.type, cmd.data || cmd);
                    }
                }
            } else {
                console.error("Malformed command: ", cmdStr);
            }
        }
    };

    return Connection;
})();
