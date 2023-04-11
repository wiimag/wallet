/*
 * Copyright 2023 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 * 
 * SMTP Server: 184.107.112.56, 65.175.74.173
 */
// @ts-check

const _ = require('lodash'),
    nodemailer = require('nodemailer'),
    minimist = require('minimist');

const opts = minimist(process.argv.slice(2));

const MAIL_HOST = process.env["MAIL_HOST"] || opts["mail-host"] || "equals-forty-two.com";
const MAIL_PORT = Number(process.env["MAIL_PORT"]) || Number(opts["mail-port"]) || 465;
const MAIL_USER = process.env["MAIL_USER"] || opts["mail-user"] || "noreply@infineis.com";
const MAIL_PSWD = process.env["MAIL_PSWD"] || opts["mail-pswd"];
const WEB_HOST_URL = process.env["WEB_HOST_URL"] || opts["host-url"] || "https://infineis.com";

if (!_.isString(MAIL_HOST)) { console.error("Invalid mail server host: " + MAIL_HOST); process.exit(1); }
if (!_.isNumber(MAIL_PORT)) { console.error("Invalid mail server port: " + MAIL_PORT); process.exit(1); }
if (!_.isString(MAIL_USER)) { console.error("Invalid mail server username (MAIL_USER): " + MAIL_USER); process.exit(1); }
if (!MAIL_PSWD || !_.isString(MAIL_PSWD)) { console.error("Invalid mail server password (MAIL_PSWD)"); process.exit(1); }
if (!WEB_HOST_URL || !_.isString(WEB_HOST_URL)) { console.error("Invalid web host URL (WEB_HOST_URL)"); process.exit(1); }

class MailService {

    constructor() {

        this.transporter = nodemailer.createTransport({
            host: MAIL_HOST,
            port: MAIL_PORT,
            secure: true, // secure:true for port 465, secure:false for port 587
            auth: {
                user: MAIL_USER,
                pass: MAIL_PSWD
            }
        });

        /**
         * @param {Error?} error 
         * @param {boolean?} success 
         * @returns {number|undefined} exit code
         */
        const transporterHandler = (error, success) => {
            if (error || !success) {
                console.error("Failed to verify mail server.\r\n" + error);
                return process.exit(1);
            }
            console.info("Mail server verification successful.");
        };
        
        this.transporter.verify(transporterHandler);
    }

    /**
     * Sends a email
     * @param {string} email 
     * @param {string} subject 
     * @param {string} html 
     * @param {string} text 
     * @returns {Promise<Object>}
     */
    sendMail(email, subject, html, text) {
        let mailOptions = {
            from: '"Infineis" <noreply@infineis.com>',
            to: email,
            subject, 
            text, 
            html: html.replace(/%HOST_URL%/g, WEB_HOST_URL)
        };
    
        return new Promise((resolve, reject) => {
            this.transporter.sendMail(mailOptions, (err, info) => {
                if (err)
                    return reject(err);
                console.log('Message %s sent: %s', info.messageId, info.response);
                resolve(info);
            });
        });
    }
}

module.exports = MailService;
