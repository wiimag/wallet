/*
 * Copyright 2023 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 * 
 * Service used to manage user accounts.
 */
// @ts-check

const _ = require('lodash');
const crypto = require("crypto");
const EodService = require('./eod-service');
const HttpService = require('./http-service');
const MailService = require('./mail-service');

const { access } = require('fs');
const minimist = require('minimist');
const opts = minimist(process.argv.slice(2));

// TODO: Add support to save and load the database from a file
const DB_USERS_PATH = 'artifacts/users.json';

/**
 * @typedef {string} UserId - Represents a unique ID to store data.
 */

/**
 * @typedef {Object} User - Defines a user object
 * @property {string} id - User unique ID
 * @property {string} email - Email of the user to contact him.
 * @property {string} [name] - Full name of the user
 * @property {string} [apikey] - Developer API key used to access the API directly.
 * @property {string} [veriftoken] - Volatile verification code used to match the user verification link
 * @property {AuthToken} [authtoken] - Active authentification token used to access the system.
 * @property {Object} [preferences] - User global preferences
 * @property {Object} [sessions] - User active sessions
 */


/**
 * @typedef {Object} UserAccess - Defines a user access to the database or services
 * @extends {AuthToken}
 * @property {string} [reason] - Reason used to access the system.
 */

/**
 * @typedef {Object} AuthToken - Defines a user access to the database or services
 * @property {number} time - Time access to the services or data
 * @property {string} code - Unique authentification code used to access the system
 * @property {string} [apikey] - API key used to execute the HTTP request.
 * @property {string} [email] - Email of the user to contact him.
 * @property {string} ipAddress - IP address used to access the system
 * @property {string} [ipAgent] - User agent
 * @property {string} [ipAddresses] - Proxy IP addresses (if any)
 */

/**
 * @typedef {Object} UserDbSchema - Defines the root database schema used to store `everything`.
 * @property {Array<User>} users - User with their system accesses
 * @property {Object<UserId, Array<UserAccess>>} accesses - User logged accesses to the backend
 */

/**
 * Main REST api service.
 * This service will eventually forward all routes to other services.
 * Until then, we will prototype most features directly here.
 */
 class UserService {

    /**
     * @param {HttpService} httpService 
     * @param {MailService} mailService
     */
    constructor(httpService, mailService) {

        /** @type {UserDbSchema} */
        this.db = {
            users: [],
            accesses: {}
        };

        /** @type {boolean} */
        this.dirty = false;

        this.httpService = httpService;
        this.mailService = mailService;

        // Request user connection
        httpService.register('get', '/api/connect', this.routeConnectUser.bind(this));
        httpService.register('get', '/api/disconnect', this.routeDisconnectUser.bind(this));
        httpService.register('get', '/connect/verification', this.routeVerifyConnection.bind(this));

        // Request current user information
        httpService.register('get', '/api/user', this.routeGetUser.bind(this));

        if (opts.debug) {
            httpService.register('get', '/api/db/content', this.routeDebugDbContent.bind(this));
        }
    }

    /**
     * Marks the database as dirty so it gets saved automatically later.
     */
    markDirty () {
        this.dirty = true;
    }

    /**
     * Retrieve a user from its apikey (if any).
     * @param {string} apiKey
     * @returns {Promise<User>} 
     */
    findUserApiKey (apiKey) {
        // TODO: Add a system to check request abuse to find valide keys.
        //       - If the IP changes too often over time, then disable the key.

        return new Promise((resolve, reject) => {
            if (!apiKey)
                return reject(new Error('Invalid API key'));

            for (let user of this.db.users) {
                if (user.apikey === apiKey) {
                    return resolve(user);
                }
            }

            return reject(new Error('User not found'));
        });
    }

    /**
     * @param {HttpService.Request} req 
     * @param {string} [accessReason] - Reason for the access
     * @returns {Promise<User>}
     */
    authUser(req, accessReason) {
        const accessInfo = this.extractRequestAccessInfo(req);
        if (!accessInfo.ipAgent)
            return Promise.reject(new Error('Invalid user agent'));

        const reqAccessString = `${req.method} ${req.path}`;
        if (accessInfo.apikey) {
            return this.findUserApiKey(accessInfo.apikey)
                .then(user => this.logUserConnectAccess(user, accessInfo, accessReason ?? reqAccessString));
        }

        return new Promise((resolve, reject) => {
            if (!accessInfo.code)
                throw new Error('Invalid code');
            let db = this.db;
            for (const user of db.users) {
                if (!user.authtoken || user.authtoken.code !== accessInfo.code)
                    continue;

                if (!this.checkUserAuthentification(user, accessInfo))
                    return reject(new Error('Invalid user authentification'));

                return this.logUserConnectAccess(user, accessInfo, accessReason ?? reqAccessString)
                    .then(resolve)
                    .catch(reject);
            }        
        });
    }

    /**
     * 
     * @param {HttpService.Request} req 
     * @param {HttpService.Response} res 
     * @returns Promise<User>
     */
    routeGetUser (req, res) {
        return this.authUser(req).then(user => res.json({
            name: user.name,
            email: user.email,
            preferences: {},
            sessions: user.sessions
        })).catch(err => this.httpService.sendError(req, res, 401, err));
    }

    /**
     * Returns the database JSON content (only used for debugging)
     * @param {HttpService.Request} req 
     * @param {HttpService.Response} res 
     */
    routeDebugDbContent (req, res) {
        return this.authUser(req)
            .then(user => res.json({
                user: user,
                db: this.db
            })).catch(err => this.httpService.sendError(req, res, 401, err));
    }

    /**
     * Formats an HTML message to send the user connect verification email.
     * @param {User} user - User to send email too.
     * @param {string} code - Verification code.
     * @returns {Promise}
     */
    sendVerificationEmail(user, code) {
        if (!user.email)
            throw new Error('Invalid user email address');
            
        let subject = "Infineis Sign-In Verification";
        let verificationLink =  `${this.httpService.url}/connect/verification?c=${code}`;
        let htmlMessage = `
        <!DOCTYPE html>
        <html>
        <head>
        <meta http-equiv="Content-Type" content="text/html; charset=UTF-8">
        <meta http-equiv="X-UA-Compatible" content="IE=edge">
        <meta name="viewport" content="initial-scale=1.0">
        <meta name="format-detection" content="telephone=no">
        <title>${subject}</title>
        <link rel="shortcut icon" type="image/x-icon" href="https://infineis.com/favicon.ico">
        </head>
        <body style="font-family:'Roboto',sans-serif; padding:0; background-color:#ffffff; box-sizing:border-box">
            Hi ${user.name},<br/>
            <p>
            You have requested access to <img width="16" style="width:16px;vertical-align:sub;" alt="=infineis_logo" src="https://infineis.com/favicon.ico"/> <b>Infineis Cloud Services</b>
            <br/>The following link will finish the verification process and sign you in the application.
            </p>
            
            <p style="margin:20px 0px;text-align:left;font-family:'Courier New',Courier,monospace;font-size:14px;color:#333333;background-color:#eaeaea">
            <a href="${verificationLink}">${verificationLink}</a>
            <p/>

            <p>
            If you have not requested that verification link, please report it to <a href="https://infineis.com/support">https://infineis.com/support</a>.
            </p>
            <p>
            Have a nice day!
            </p>
            <p style="font-size:10px;margin:1em 0">
            The information in this message is confidential.
            </p>
            <p style="font-size:10px;margin:1em 0">
            ** PLEASE DO NOT REPLY TO THIS EMAIL.
            </p>
        </body>
        </html>`;
        return this.mailService.sendMail(user.email, subject, htmlMessage, htmlMessage);
    }

    /**
     * @param {User} user 
     * @param {AuthToken} accessInfo 
     * @returns {boolean} True if the user authentification info is valid.
     */
    checkUserAuthentification (user, accessInfo) {
        if (user.authtoken && user.authtoken.code === accessInfo.code) {
            if (user.authtoken.ipAgent === accessInfo.ipAgent && 
                user.authtoken.ipAddress == accessInfo.ipAddress &&
                user.authtoken.ipAddresses == accessInfo.ipAddresses) {
                    return true;
            }

            return false;
        }

        return false;
    }

    /**
     * @param {AuthToken} accessInfo 
     * @returns {boolean} True if the access info matches an user authentification info.
     */
    checkAuthentification (accessInfo) {
        const db = this.db;
        for (const user of db.users) {
            if (this.checkUserAuthentification(user, accessInfo))
                return true;
        }

        return false;
    }

    /**
     * Validates if the HTTP request authentification is valid.
     * @param {HttpService.Request} req 
     * @param {HttpService.Response} res 
     * @param {AuthToken} accessInfo 
     */
    verifyAuthentification (req, res, accessInfo) {

        let db = this.db;
        for (const user of db.users) {
            if (user.authtoken && user.authtoken.code === accessInfo.code) {
                if (this.checkUserAuthentification(user, accessInfo)) {
                    return this.logUserConnectAccess(user, accessInfo, 'Login').then(user => {
                        return res.json({
                            id: user.id,
                            name: user.name,
                            email: user.email,
                            authtoken: user.authtoken
                        });
                    });
                }

                return Promise.all([
                    this.clearUserAuthInfo(user),
                    this.logUserConnectAccess(user, accessInfo, 'Invalid user information'),
                    this.httpService.sendError(req, res, 403, 'Invalid authentification information')
                ]);
            }
        }

        return this.httpService.sendError(req, res, 404, 'Invalid authentification code');
    }

    /**
     * Clears the user authentification information.
     * @param {User} user 
     */
    clearUserAuthInfo (user) {
        user.authtoken = undefined;
        user.veriftoken = undefined;
        this.markDirty();
    }

    /**
     * Logs to the database the user access to the system.
     * @param {User} user 
     * @param {UserAccess|AuthToken} auth 
     * @param {string} reason 
     */
    logUserConnectAccess (user, auth, reason) {
        return new Promise((resolve) => {
            if (!this.db.accesses.hasOwnProperty(user.id) || !_.isArray(this.db.accesses[user.id]))
                this.db.accesses[user.id] = [];
        
            // Remove access 7 days olds
            let userAccesses = this.db.accesses[user.id];
            userAccesses = _.uniqBy(_.filter(userAccesses, e => {
                let dt = Date.now() - e.time;
                return dt < 86400000 * 7;
            }), e => {
                return `${e.ipAddress}/${e.ipAgent}/${e.reason}`;
            });
            
            auth.reason = reason;
            userAccesses.push(auth);
            this.db.accesses[user.id] = userAccesses;
            return resolve(user);
        });
    }

    /**
     * Checks if the verification info is valid for a given user.
     * @param {Object} info 
     * @param {User} user 
     * @param {number} time 
     * @param {Function} resolve 
     * @param {Function} reject 
     */
    checkVerification (info, user, time, resolve, reject) {
        if (!user.veriftoken)
            return reject(new Error('No verification code'));

        if (user.authtoken != null && user.authtoken.code && user.authtoken.time > time) {
            user.veriftoken = undefined;
            info.authtoken = user.authtoken;
            this.markDirty();
            return resolve(info);
        }

        if (Date.now() - time > 60 * 1000)
            return reject(new Error('Verification timeout'));

        setTimeout(() => this.checkVerification(info, user, time, resolve, reject), 100);
    }

    /**
     * Initiates the user verification loop waiting for him to validate the verification link in order to get authentificated.
     * @param {Object} info 
     * @param {User} user 
     * @param {number} time 
     * @returns Promise
     */
    waitForVerification (info, user, time) {
        return new Promise((resolve, reject) => {
            return this.checkVerification(info, user, time, resolve, reject);
        });
    }

    /**
     * Extracts for the HTTP request the user authentification access information.
     * @param {HttpService.Request} req 
     * @returns {UserAccess}
     */
    extractRequestAccessInfo (req) {
        return {
            time: Date.now(),
            code: req.query.auth,
            email: req.query.email,
            apikey: req.query.apikey,
            ipAgent: req.header('user-agent'),
            ipAddress: req.socket.remoteAddress,
            ipAddresses: req.header('x-forwarded-for')
        };
    }

    /**
     * Handles the connect API route.
     * @param {HttpService.Request} req 
     * @param {HttpService.Response} res 
     */
    routeConnectUser (req, res) {
        const requestAccessInfo = this.extractRequestAccessInfo(req);

        if (requestAccessInfo.apikey)
            return this.authUser(req).then(user => {
                return res.json({
                    id: user.id,
                    name: user.name,
                    email: user.email,
                    authtoken: user.authtoken
                });
            });

        if (requestAccessInfo.code)
            return this.verifyAuthentification(req, res, requestAccessInfo);

        if (!requestAccessInfo.email)
            return this.httpService.sendError(req, res, 401, 'Invalid email address');

        let db = this.db;
        for (const user of db.users) {
            if (user.email?.toLowerCase() === requestAccessInfo.email.toLowerCase()) {
                user.authtoken = undefined;
                user.veriftoken = crypto.randomBytes(16).toString("hex");
                this.markDirty();
                return Promise.all([
                    this.logUserConnectAccess(user, requestAccessInfo, 'Verify email'),
                    this.sendVerificationEmail(user, user.veriftoken).then(mailInfo => {
                        // Wait for user to click verification link
                        return this.waitForVerification(mailInfo, user, Date.now());
                    }).then(connectionInfo => {
                        connectionInfo.id = user.id;
                        connectionInfo.name = user.name;

                        if (!user.authtoken)
                            throw new Error('Invalid user authentification token');

                        user.authtoken.ipAgent = requestAccessInfo.ipAgent;
                        user.authtoken.ipAddress = requestAccessInfo.ipAddress;
                        user.authtoken.ipAddresses = requestAccessInfo.ipAddresses;
                        this.markDirty();
                        return res.json(connectionInfo);
                    }).catch(err => {
                        this.clearUserAuthInfo(user);
                        return this.httpService.sendError(req, res, 503, 'Failed to send verification code: ' + err?.message);
                    })
                ]);
            }
        }

        return this.httpService.sendError(req, res, 401, 'Email address ' + requestAccessInfo.email + ' not registered');
    }

    /**
     * Disconnect a user and clear its authentification information.
     * @param {HttpService.Request} req 
     * @param {HttpService.Response} res 
     */
     routeDisconnectUser (req, res) {
        const requestAccessInfo = this.extractRequestAccessInfo(req);
        let authCode = requestAccessInfo.code;
        if (authCode) {
            let db = this.db;
            for (const user of db.users) {
                if (user.authtoken && user.authtoken.code === authCode) {
                    this.clearUserAuthInfo(user);
                    this.logUserConnectAccess(user, this.extractRequestAccessInfo(req), 'Disconnected');
                    return res.json({
                        name: user.name,
                        disconnected: true
                    });
                }
            }
        }

        return this.httpService.sendError(req, res, 401, 'Invalid authentification code');
    }

    /**
     * Handles the user HTTP verification route.
     * @param {HttpService.Request} req 
     * @param {HttpService.Response} res 
     */
    routeVerifyConnection (req, res) {
        let code = req.query.c;
        if (!code)
            return this.httpService.sendError(req, res, 404, 'Invalid code');

        let db = this.db;
        for (const user of db.users) {
            if (user.veriftoken === code) {
                return this.logUserConnectAccess(user, this.extractRequestAccessInfo(req), 'Validate email access').then(user => {
                    user.authtoken = { 
                        time: Date.now(),
                        code: crypto.randomBytes(16).toString("hex")
                    };
                    this.markDirty();
                    return res.redirect('https://infineis.com');
                })
            }
        }

        return this.httpService.sendError(req, res, 406);
    }
}

module.exports = UserService;
