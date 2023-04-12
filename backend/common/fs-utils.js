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

    /**
     * Remove a directory recursively
     * @param {string} dirPath - The path to the directory
     * @param {boolean} removeSelf - Whether to remove the directory itself
     * @returns {void}
     * @example rmDir('C:/Users/JohnDoe/Desktop/MyFolder', true); 
     */
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

    /**
     * Get all files and directories in a directory
     * @param {string} srcPath - The path to the directory
     * @param {function} cb - The callback function
     * @returns {array} The files and directories in the directory
     */
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

    /**
     * Get the directories in a directory
     * @param {string} srcPath - The path to the directory
     * @param {function} cb - The callback function
     * @returns {array} The directories in the directory
     */
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

    /**
     * Get the size of a file in bytes
     * @param {string} filename - The path to the file
     * @returns {number} The size of the file in bytes
     */
    function getFileSizeInBytes(filename) {
        var stats = fs.statSync(filename);
        return stats.size;
    }

    /**
     * Get the size of a directory recursively
     * @param {string} dir - The path to the directory
     * @returns {number} The size of the directory in bytes
     */
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
    
    /**
     * Count the number of files in a directory recursively
     * @param {string} dirPath - The path to the directory
     * @returns {number} The number of files in the directory
     */
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