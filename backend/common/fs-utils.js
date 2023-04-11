/*
 * Copyright 2023 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

module.exports = (function () {
    'use strict';

    const fs = require('fs'),
        path = require('path'),        
        async = require('async');

    const { readdir, stat } = require('fs/promises');

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

    const dirSize = async dir => {
        const files = await readdir( dir, { withFileTypes: true } );
    
        const paths = files.map( async file => {
            const dir_path = path.join( dir, file.name );
    
            if ( file.isDirectory() ) 
                return await dirSize( dir_path );
    
            if ( file.isFile() ) {
                const { size } = await stat( dir_path );
                return size;
            }
    
            return 0;
        });
    
        return ( await Promise.all( paths ) ).flat( Infinity ).reduce( ( i, size ) => i + size, 0 );
    }
    
    const dirCount = async dirPath => {
        // Count the number of files in a directory recursively
        const files = await readdir(dirPath, { withFileTypes: true });

        const paths = files.map(async file => {
            const dir_path = path.join(dirPath, file.name);

            if (file.isDirectory())
                return await dirCount(dir_path);

            if (file.isFile())
                return 1;

            return 0;
        });

        return (await Promise.all(paths)).flat(Infinity).reduce((i, count) => i + count, 0);
    }
    
    return {
        rmDir: rmDir,
        getDirItems: getDirItems,
        getDirs: getDirs,
        getFileSizeInBytes: getFileSizeInBytes,
        dirCount: dirCount,
        dirSize: dirSize
    };
})();