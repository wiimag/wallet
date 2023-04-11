/*
 * Copyright 2023 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 * 
 * Service used to compute wallet reports, raise alerts, etc.
 */
// @ts-check

const _ = require('lodash');
const fs = require('fs/promises');
const path = require('path');
const md5 = require('md5');
const EodService = require('./eod-service');
const UserService = require('./user-service');
const HttpService = require('./http-service');
const MailService = require('./mail-service');
const fileUpload = require('express-fileupload');

/**
 * Main REST api service.
 * This service will eventually forward all routes to other services.
 * Until then, we will prototype most features directly here.
 */
 class WalletService {

    /**
     * @param {EodService} eodService 
     * @param {HttpService} httpService 
     * @param {UserService} userService
     * @param {MailService} mailService
     */
    constructor(eodService, httpService, userService, mailService) {
        this.eodService = eodService;
        this.userService = userService;
        this.httpService = httpService;
        this.mailService = mailService;

        /*httpService.register('get', '/api/cases', this.routeGetCases.bind(this));
        httpService.register('get', '/api/case/:id', this.routeGetCase.bind(this));
        httpService.register('get', '/api/case/:id/share', this.routeShareCase.bind(this));
        httpService.register('get', '/api/case/:id/users', this.routeListCaseUsers.bind(this));
        httpService.register('get', '/api/case/:case_id/file/:file_id', this.routeDownloadCaseFile.bind(this));
        httpService.register('get', '/api/case/files/:id', this.routeGetCaseFileList.bind(this));
        httpService.register('post', '/api/case/:id', this.routePostCase.bind(this));
        httpService.register('post', '/api/case/file/:id', this.routeUploadFile.bind(this));
        httpService.register('post', '/api/case/sync/files/:id', this.routePostSyncFiles.bind(this));
        httpService.register('post', '/api/case/:id/annotation', this.routePostAnnotation.bind(this));
        httpService.register('delete', '/api/case/:id/annotation/:annotation_id', this.routeDeleteAnnotation.bind(this));*/
    }
}

module.exports = WalletService;
