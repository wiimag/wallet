#!/usr/bin/env node
/*
 * Copyright 2023 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */
// @ts-check

global.__base = __dirname + '/';

const _ = require('./common/lodash-ext');
const fs = require('fs'),
    path = require('path'),
    async = require('async');

/**
 * @typedef {Object} ServiceInstance
 */

/**
 * @typedef {Object|string} Service
 * @property {string} name - Name of the service
 * @property {string} [script] - Script file path
 * @property {NodeRequire} [module] - Script file path
 * @property {ServiceInstance?} instance - Service allocated instance once the module is loaded
 * @property {Array<Service>} [dependencies] - Service dependencies
 * @property {number} [reloaded] - Indicates the time the service was reloaded
 */

/**
 * Main entry point
 */
(function () {
    'use strict';

    /**
     * @type {Service[]}
     */
    var services = [];

    /**
     * 
     * @param {string} name 
     * @returns {Service|null|undefined}
     */
    function findServiceByName(name) {
        return _.find(services, function (service) {
            return service.name === name;
        });
    }

    /**
     * Get service dependencies.
     * @param {string} serviceName 
     * @param {Array<string|Service>} [closure]
     * @param {string?} [parentServiceName]
     * @returns {(Service|string)[]}
     */
    function getNestedDependencies(serviceName, closure, parentServiceName) {
        let service = findServiceByName(serviceName);

        if (!service) {
            throw new Error("Service " + serviceName + " doesn't exist.");
        }

        parentServiceName = parentServiceName || serviceName;
        closure = closure || [];
        var dependencies = _.map(service.dependencies, v => { return v.name || v; });

        dependencies.forEach(dep => {

            if (!closure)
                throw new Error('Closure cannot be undefined');
            closure.push(dep);

            if (parentServiceName && closure.indexOf(parentServiceName) >= 0) {
                throw new Error('Cyclic dependency found when resolving ' + (parentServiceName || "<none>") + ' and ' + serviceName);
            }

            // @ts-ignore
            var nestedDependencies = getNestedDependencies(dep, closure, parentServiceName);
            dependencies = _.union(dependencies, nestedDependencies);
        });

        return dependencies;
    }

    /**
     * Install service dependency injection and load all services.
     */
    function installServices() {
        let servicesFolderPath = path.join(__dirname, "services");
        let serviceScripts = fs.readdirSync(servicesFolderPath);
        _.each(serviceScripts, function (serviceScript) {

            var fullScriptPath = path.join(servicesFolderPath, serviceScript);

            var module = require(fullScriptPath);
            if (!module)
                throw new Error('Invalid service module');

            /** @type {Service} */
            let service = {
                script: fullScriptPath,
                module: module,
                name: _.toCamelCase(module.name),
                instance: null,
                dependencies: _.getParamNames(module)
            };

            services.push(service);
        });

        // Watch for service script changes.
        fs.watch(servicesFolderPath, (event, filename) => {
            if (event != "change" || !filename)
                return;
            
            let serviceScriptFullPath = path.join(servicesFolderPath, filename);
            _.find(services, service => {
                let tdiff = Date.now() - (service.reloaded || 0);
                if (service.script === serviceScriptFullPath && tdiff > 1000) {
                    console.log('Reloading', filename);

                    // Find all services dependent on the current service
                    var dependents = _.filter(services, s => {
                        return _.map(s.dependencies, d => { return d.name || d; }).indexOf(service.name) >= 0;
                    });
                    dependents.push(service);

                    // Reload all dependencies
                    dependents.reverse().forEach(d => {

                        let tdiff = Date.now() - (d.reloaded || 0);
                        if (tdiff < 1000)
                            return;

                        if (!d.script)
                            throw new Error('Invalid module script');

                        var cacheBck = require.cache[require.resolve(d.script)],
                            dModuleBck = d.module,
                            dInstanceBck = d.instance;
                        try {
                            // Delete previous module from cache
                            delete require.cache[require.resolve(d.script)];

                            // Reinstantiate service instance.
                            d.module = require(d.script);

                            if (d.instance) {
                                if (_.isFunction(d.instance.release)) {
                                    d.instance.release();
                                }

                                delete d.instance;
                            }

                            return resolveService(d.name).then(instance => d.instance = instance);
                        } catch (err) {
                            require.cache[require.resolve(d.script)] = cacheBck;
                            d.module = dModuleBck;
                            d.instance = dInstanceBck;
                            console.warn("Failed to reload service.", err);
                        }
                    });
                }
            });
        });
    }

    /**
     * Resolve a single service.
     * @param {string} serviceName - service name
     * @returns {object} returns the resolved service instance, if any.
     */
    function resolveService(serviceName) {
        // Check if the request service exists.
        var service = findServiceByName(serviceName);

        if (!service) {
            return Promise.reject(new Error('Cannot found service ' + serviceName));
        }

        if (service.instance) {
            return Promise.resolve(service.instance);
        }

        // Gather all dependencies
        var allDependencies = getNestedDependencies(service.name);

        // Resolve dependencies
        return Promise.all(allDependencies.map(dependency => {
            return resolveService(dependency.name || dependency).then(resolvedDependency => {
                if (!resolvedDependency) {
                    throw new Error("Failed to resolve service dependency");
                }

                if (!service || !service.dependencies)
                    throw new Error('Invalid service');

                var depIndex = service.dependencies.indexOf(dependency);
                if (depIndex >= 0) {
                    service.dependencies[depIndex] = { name: dependency, instance: resolvedDependency };
                }
            });
        })).then(function () {
            if (!service || !service.dependencies)
                throw new Error('Invalid service');

            // Create instance
            return Promise.resolve(_.construct(service.module, _.map(service.dependencies, 'instance'))).then(instance => {
                if (!service || !service.dependencies)
                    throw new Error('Invalid service');

                if (!service.reloaded)
                    console.info("Created", service.name);
                service.instance = instance;
                service.reloaded = Date.now();
                return service.instance;
            });
        }).catch(err => {
            console.error(err);
            process.exit(-1);
        });
    }

    /**
     * @returns {Promise}
     */
    function resolveAllServices() {

        /**
         * @typedef {Object} DependencyMapEntry
         * @property {string?} name
         * @property {number} count
         */

        /** @type Array<DependencyMapEntry> */
        let dependencyMap = [];

        /**
         * @param {string?} serviceName 
         */
        function _pushToDepMap(serviceName) {
            if (!_.find(dependencyMap, item => {
                if (item.name === serviceName) {
                    item.count++;
                    return true;
                }

                return false;
            })) {
                dependencyMap.push({ name: serviceName, count: 1 });
            }
        }

        services.forEach(service => {
            _pushToDepMap(service.name);

            service.dependencies.forEach(dep => {
                _pushToDepMap(dep);
            });
        });

        var orderedServiceNamesByDependency = _.map(_.sortBy(dependencyMap, 'count').reverse(), 'name');

        return orderedServiceNamesByDependency.reduce(async (p, serviceName) => {
            if (!serviceName)
                throw new Error('Invalid service name');
            await p;
            return resolveService(serviceName);
        }, Promise.resolve()); // initial
    }

    /**
     * Starts the main node services.
     * @param {function(Error|null|undefined):void} callback - Called when all main services are started and initialized.
     */
    function start(callback) {
        async.waterfall([
            function (next) {
                // Bootstrap services
                next();
            }
        ], function (err) {
            callback(err);
        });
    }

    installServices();
    resolveAllServices();
    start(err => {
        if (err) {
            console.error("Failed to start server.", err);
            return process.abort();
        }
    });

    process.on('SIGINT', () => {
        console.warn("Exiting...");
        process.exit();
    });
}());
