/*
 * Copyright 2023 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 * 
 * Service used to bootstrap the application.
 */
// @ts-check

const _ = require('lodash');
const packageInfo = require('../package.json');

class ApplicationService {
    constructor() {

    }

    version () {
        return packageInfo.version;
    }

    name () {
        return packageInfo.name;
    }

    description () {
        return packageInfo.description;
    }

    appDir () {
        return __dirname + '/../';
    }
}

module.exports = ApplicationService;
