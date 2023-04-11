/*
 * Copyright 2023 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

module.exports = (function () {
    'use strict';

    var fs = require('fs'),
        path = require('path'),        
        async = require('async');

    function rmDir(dirPath, removeSelf) {
        var files = [];
        if (removeSelf === undefined) {
            removeSelf = true;
        }
        try { files = fs.readdirSync(dirPath); }
        catch(e) { return; }
        if (files.length > 0) {
            for (var i = 0; i < files.length; i++) {
                var filePath = dirPath + '/' + files[i];
                if (fs.statSync(filePath).isFile()) {
                    fs.unlinkSync(filePath);
                } else {
                    rmDir(filePath);
                }
            }
        }
        if (removeSelf) {
            fs.rmdirSync(dirPath);
        }
    }

    function getDirItems(srcPath, cb) {
        fs.readdir(srcPath, function (err, files) {
            if(err) {
                console.error(err);
                return cb([]);
            }
            var iterator = function (file, cb) {
                fs.stat(path.join(srcPath, file), function (err, stats) {
                    if(err) {
                        console.error(err);
                        return cb(false);
                    }
                    cb(stats.isDirectory() || stats.isFile());
                });
            };
            async.filter(files, iterator, cb);
        });
    }

    function getDirs(srcPath, cb) {
        fs.readdir(srcPath, function (err, files) {
            if(err) {
                console.error(err);
                return cb([]);
            }
            var iterator = function (file, cb) {
                fs.stat(path.join(srcPath, file), function (err, stats) {
                    if(err) {
                        console.error(err);
                        return cb(false);
                    }
                    cb(stats.isDirectory());
                });
            };
            async.filter(files, iterator, cb);
        });
    }

    function getFileSizeInBytes(filename) {
        var stats = fs.statSync(filename);
        return stats.size;
    }
    
    return {
        rmDir: rmDir,
        getDirItems: getDirItems,
        getDirs: getDirs,
        getFileSizeInBytes: getFileSizeInBytes
    };
})();