/*
 * Copyright 2023 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */
// @ts-check

const _ = require('lodash');
const path = require('path'),
    express = require('express'),
    logger = require('morgan'),
    opts = require('minimist')(process.argv.slice(2));
const cors = require('cors');
const fileUpload = require('express-fileupload');

const fs = require('fs');
const https = require('https');

let credentials = null;
let useHttps = opts['use-https'];
if (useHttps) {
    const sslKeyPath = path.resolve(__dirname + '/../../certs/selfsigned.key');
    const sslCertPath = path.resolve(__dirname + '/../../certs/selfsigned.crt');

    console.log('Checking SSL certificats at ' + sslCertPath);
    if (useHttps && fs.existsSync(sslKeyPath) && fs.existsSync(sslCertPath)) {
        credentials = {
        key: fs.readFileSync(sslKeyPath),
        cert: fs.readFileSync(sslCertPath)
        };
    } else {
        useHttps = false;
    }
}

/**
 * @typedef {Object} RequestCacheSettings
 * @extends {Request}
 * @property {string} cachePath - Path to cache file
 * @property {string} cacheService - Name of cache service

/**
 * @typedef {express.Request} Request
 * @property {Object} socket - IP socket data
 * @property {function(string):string?} header - Extract request header information
 * @extends {http.IncomingMessage}
 */

/**
 * @typedef {express.Response} Response
 * @property {function(Object):void} json - Commits a JSON payload
 * @property {function(Object):void} send - Commits a JSON payload
 * @property {function(Object):void} status - Commits a JSON payload
 * @property {function(Object):void} end - Commits a JSON payload
 */

/**
 * @typedef {express.NextFunction} Next
 * @property {function(Object):void} json - Commits a JSON payload
 * @property {function(Object):void} send - Commits a JSON payload
 * @property {function(Object):void} status - Commits a JSON payload
 * @property {function(Object):void} end - Commits a JSON payload
 * @property {function(Object):void} redirect - Commits a JSON payload
 * @property {function(Object):void} render - Commits a JSON payload
 * @property {function(Object):void} sendFile - Commits a JSON payload
 * @property {function(Object):void} sendStatus - Commits a JSON payload
 * @property {function(Object):void} set - Commits a JSON payload
 * @property {function(Object):void} type - Commits a JSON payload
 * @property {function(Object):void} write - Commits a JSON payload
 * @property {function(Object):void} writeHead - Commits a JSON payload
 * @property {function(Object):void} writeContinue - Commits a JSON payload
 * @property {function(Object):void} writeProcessing - Commits a JSON payload
 */

/**
 * Retrieve the host IP address.
 */
function _getIP() {
    const os = require('os');

    let ip = null;
    let ifaces = os.networkInterfaces();
    if (ifaces === null)
        throw new Error('Failed to get network interface');

    Object.keys(ifaces).forEach(ifname => {
        let alias = 0;

        if (ip || ifname === null || ifaces === null) {
            return;
        }

        (ifaces[ifname] || []).forEach(function (iface) {
            if ('IPv4' !== iface.family || iface.internal !== false) {
                // skip over internal (i.e. 127.0.0.1) and non-ipv4 addresses
                return;
            }

            if (alias < 1) {
                ip = iface.address;
            }
            ++alias;
        });
    });

    return ip;
}

class HttpService {
    constructor () {

        /** {core.Express} */
        this.app = null;
        this.server = null;
        this.router = express.Router();
        this.proxyRouter = express.Router();
        this.routes = {
            get: {},
            post: {},
            put: {},
            delete: {}
        };

        // @ts-ignore Start http server to serve backend static files and requests.
        return this.createServer(process.env.PORT || parseInt(opts.port) || (useHttps ? 443 : 80));
    }

    /**
     * Create express server instance
     * @param {number} port 
     * @returns Promise resolution
     */
    createServer(port) {
        return new Promise(resolve => {
            var favicon = require('serve-favicon');

            this.contentRoot = path.join(__dirname, '..', 'public');

            var app = express();

            app.use(logger('dev'));
            app.use(favicon(path.join(this.contentRoot, 'favicon.ico')));
            app.use(express.static(this.contentRoot));            
            app.use(cors());

            app.use((req, res, next) => {
                this.proxyRouter(req, res, next);
            });

            // Enable files upload
            app.use(fileUpload({createParentPath: true}));
            
            // @ts-ignore
            app.use(express.json({limit: "10mb", extended: true}))
            app.use(express.urlencoded({limit: "10mb", extended: true, parameterLimit: 16}))

            app.use((err, req, res, next) => {
                if (err instanceof SyntaxError) {
                    console.error(err);
                    return this.sendError(req, res, 415, err, req.body);
                }
                return next();
            });

            app.use((req, res, next) => {
                this.router(req, res, next);
            });

            // Define global API
            const packageInfo = require('../package.json');
            app.get('/version', (req, res) => res.json({
                url: req.path,
                name: packageInfo.name,
                description: packageInfo.description,
                version: packageInfo.version
            }));

            // Start server and listen to connections
            this.server = (credentials ? https.createServer(credentials, app) : app).listen(port, () => {
                var host = process.env.HOST_URL || opts.ip || _getIP();
                let addr = this.server.address();
                if (addr === null)
                    throw new Error('Invalid server address');
                // @ts-ignore
                let port = addr.port;
                this.url = (credentials ? "https" : "http") + "://" + host + (":" + port);
                // Remove default ports from server url
                this.url = this.url.replace(":80", "").replace(":443", "");
                console.log('Http server started on %s', this.url);
                resolve(this);
            });

            this.app = app;
            return this;
        });
    }

    isPromise(p) {
        if (
          typeof p === 'object' &&
          typeof p.then === 'function' &&
          typeof p.catch === 'function'
        ) {
          return true;
        }
      
        return false;
      }

    /**
     * @param {string} type       - HTTP method type
     * @param {string} route      - Route to register
     * @param {function} callback - Callback function to be called when the route is requested
     * @param {boolean} [isProxy] - If true, the route will be routed through the proxy router
     * @returns 
     */
    register(type, route, callback, isProxy) {
        var r = this.routes[type][route];
        if (r === undefined) {
            r = this.routes[type][route] = {
                fn: callback,
                routed: null
            }

            if (isProxy) {
                r.routed = this.proxyRouter[type].apply(this.proxyRouter, [route, (req, res, next) => r.fn(req, res, next)]);
            } else {
                r.routed = this.router[type].apply(this.router, [route, (req, res, next) => r.fn(req, res, next)]);
            }
        } else {
            r.fn = callback;
        }
        return r.routed;
    }

    /**
     * 
     * @param {Request} req - Http request lending to this error
     * @param {Response} res - Http response to return the error message
     * @param {number} code - Http status error code
     * @param {Error|string|null} [err] - Error message
     * @param {Object} [payload] - Optional error content to publish
     * 
     * @example {
     *    "code" : 404,
     *    "err" : "Invalid case descriptor",
     *    "time" : 1670074602354,
     *    "url" : "/api/case/new1"
     * }
     */
    sendError (req, res, code, err, payload) {
        
        // TODO: Log all errors
        if (opts.debug)
            console.error(err);
        return res.status(code).json({
            method: req.method,
            url: req.path,
            code: code,
            time: Date.now(),
            err: _.isError(err) ? err.message : err?.toString(),
            more: payload
        });
    }

    release() {
        if (this.server === null)
            throw new Error('Http service not initialized');
        return this.server.close();
    }
}

module.exports = HttpService;
